// SPDX-License-Identifier: GPL-3.0-only
#include "session/CaptureSession.h"

#include "capture/ICaptureSource.h"
#include "pipeline/IFrameProcessor.h"
#include "settings/SettingsStore.h"

#include <QImage>
#include <QScreen>
#include <QStringView>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace cimbarpunk {

namespace {

struct Transition {
    SessionState from;
    SessionState to;
};

constexpr std::array legalTransitions{
    Transition{SessionState::Idle, SessionState::Selecting},
    Transition{SessionState::Selecting, SessionState::Adjusting},
    Transition{SessionState::Selecting, SessionState::Cancelled},
    Transition{SessionState::Selecting, SessionState::Error},
    Transition{SessionState::Adjusting, SessionState::Capturing},
    Transition{SessionState::Adjusting, SessionState::Cancelled},
    Transition{SessionState::Adjusting, SessionState::Error},
    Transition{SessionState::Capturing, SessionState::Completed},
    Transition{SessionState::Capturing, SessionState::Cancelled},
    Transition{SessionState::Capturing, SessionState::Error},
    Transition{SessionState::Completed, SessionState::Idle},
    Transition{SessionState::Error, SessionState::Idle},
    Transition{SessionState::Cancelled, SessionState::Idle},
};

bool isTransient(const SessionState state) {
    return state == SessionState::Completed || state == SessionState::Error
        || state == SessionState::Cancelled;
}

bool isFiniteRect(const QRectF& rect) {
    return std::isfinite(rect.x()) && std::isfinite(rect.y()) && std::isfinite(rect.width())
        && std::isfinite(rect.height()) && std::isfinite(rect.right())
        && std::isfinite(rect.bottom());
}

QString failureMessage(const QString& supplied, const QString& fallback) {
    return supplied.isEmpty() ? fallback : supplied;
}

} // namespace

CaptureSession::CaptureSession(ICaptureSource& captureSource, IFrameProcessor& frameProcessor,
    SettingsStore& settingsStore, ScreenResolver screenResolver,
    const std::chrono::milliseconds noFrameTimeout, QObject* parent)
    : QObject(parent)
    , m_captureSource(captureSource)
    , m_frameProcessor(frameProcessor)
    , m_settingsStore(settingsStore)
    , m_screenResolver(std::move(screenResolver))
    , m_noFrameTimeout(std::max(noFrameTimeout, std::chrono::milliseconds(1))) {
    m_noFrameWatchdog.setSingleShot(true);
    m_noFrameWatchdog.setInterval(m_noFrameTimeout);
    connect(&m_noFrameWatchdog, &QTimer::timeout, this, [this] {
        if (m_state == SessionState::Capturing && m_watchdogArmed && !m_cleaningUp) {
            finishFailure(QStringLiteral("连续 5 秒未收到可用画面"));
        }
    });
}

CaptureSession::~CaptureSession() {
    m_shutdown = true;
    cleanup();
}

SessionState CaptureSession::state() const {
    return m_state;
}

bool CaptureSession::beginSelection() {
    if (m_shutdown || !transitionTo(SessionState::Selecting)) {
        return false;
    }

    if (m_state == SessionState::Selecting) {
        emit selectionRequested();
    }
    return true;
}

bool CaptureSession::selectionCreated(const ScreenSelection& selection) {
    if (m_shutdown || m_state != SessionState::Selecting || !isValidSelection(selection)) {
        return false;
    }

    m_selection = selection;
    return transitionTo(SessionState::Adjusting);
}

bool CaptureSession::confirmSelection() {
    if (m_shutdown || m_state != SessionState::Adjusting || !m_selection.has_value()) {
        return false;
    }

    m_outputDirectory = m_settingsStore.outputDirectory();
    const ScreenSelection selectionSnapshot = *m_selection;
    QScreen* screen = m_screenResolver ? m_screenResolver(QStringView(selectionSnapshot.screenId)) : nullptr;
    if (m_state != SessionState::Adjusting) {
        return false;
    }
    if (screen == nullptr) {
        finishFailure(QStringLiteral("无法找到所选显示器，请重新选择区域"));
        return false;
    }

    QString startError;
    if (!m_frameProcessor.start(selectionSnapshot, m_outputDirectory, &startError)) {
        finishFailure(failureMessage(startError, QStringLiteral("无法启动解码处理")));
        return false;
    }

    const quint64 generation = ++m_captureGeneration;
    connectCapture(generation);
    if (!transitionTo(SessionState::Capturing)) {
        cleanup();
        return false;
    }
    if (m_state != SessionState::Capturing || m_shutdown) {
        return false;
    }

    startError.clear();
    if (!m_captureSource.start(screen, &startError)) {
        if (m_state == SessionState::Capturing) {
            finishFailure(failureMessage(startError, QStringLiteral("无法启动屏幕捕获")));
        }
        return false;
    }
    return m_state == SessionState::Capturing;
}

bool CaptureSession::cancel() {
    if (m_cleaningUp || (m_state != SessionState::Selecting && m_state != SessionState::Adjusting
            && m_state != SessionState::Capturing)) {
        return false;
    }

    cleanup();
    if (!transitionTo(SessionState::Cancelled)) {
        return false;
    }
    queueIdle();
    return true;
}

bool CaptureSession::stop() {
    if (m_state != SessionState::Capturing) {
        return false;
    }
    return cancel();
}

void CaptureSession::shutdown() {
    if (m_shutdown) {
        cleanup();
        return;
    }

    m_shutdown = true;
    if (m_state == SessionState::Selecting || m_state == SessionState::Adjusting
        || m_state == SessionState::Capturing) {
        cleanup();
        if (transitionTo(SessionState::Cancelled)) {
            queueIdle();
        }
        return;
    }
    cleanup();
}

bool CaptureSession::transitionTo(const SessionState next) {
    const bool legal = std::ranges::any_of(legalTransitions,
        [this, next](const Transition& transition) {
            return transition.from == m_state && transition.to == next;
        });
    if (!legal) {
        return false;
    }

    m_state = next;
    if (next == SessionState::Selecting || next == SessionState::Idle) {
        m_selection.reset();
        m_outputDirectory.clear();
    }
    emit stateChanged(m_state);
    return true;
}

bool CaptureSession::isCurrentCapture(const quint64 generation) const {
    return !m_shutdown && !m_cleaningUp && m_state == SessionState::Capturing
        && generation == m_captureGeneration;
}

void CaptureSession::connectCapture(const quint64 generation) {
    disconnectCapture();

    m_frameConnection = connect(&m_captureSource, &ICaptureSource::frameReady, this,
        [this, generation](const QImage& frame) {
            if (isCurrentCapture(generation)) {
                m_frameProcessor.submitFrame(frame);
            }
        });
    m_activeConnection = connect(&m_captureSource, &ICaptureSource::activeChanged, this,
        [this, generation](const bool active) {
            if (active && isCurrentCapture(generation)) {
                m_watchdogArmed = true;
                m_noFrameWatchdog.start();
            }
        });
    m_captureFailureConnection = connect(&m_captureSource, &ICaptureSource::failed, this,
        [this, generation](const QString& message) {
            if (isCurrentCapture(generation)) {
                finishFailure(failureMessage(message, QStringLiteral("屏幕捕获失败")));
            }
        });
    m_frameAcceptedConnection = connect(&m_frameProcessor, &IFrameProcessor::frameAccepted, this,
        [this, generation] {
            if (isCurrentCapture(generation) && m_watchdogArmed) {
                m_noFrameWatchdog.start();
            }
        });
    m_progressConnection = connect(&m_frameProcessor, &IFrameProcessor::progressChanged, this,
        [this, generation](const double progress) {
            if (isCurrentCapture(generation)) {
                emit progressChanged(progress);
            }
        });
    m_completedConnection = connect(&m_frameProcessor, &IFrameProcessor::completed, this,
        [this, generation](const OutputResult& result) {
            if (!isCurrentCapture(generation)) {
                return;
            }
            if (result.ok) {
                finishSuccess(result);
            } else {
                finishFailure(failureMessage(result.error, QStringLiteral("无法保存解码文件")));
            }
        });
    m_processorFailureConnection = connect(&m_frameProcessor, &IFrameProcessor::failed, this,
        [this, generation](const QString& message) {
            if (isCurrentCapture(generation)) {
                finishFailure(failureMessage(message, QStringLiteral("解码处理失败")));
            }
        });
}

void CaptureSession::disconnectCapture() {
    disconnect(m_frameConnection);
    disconnect(m_activeConnection);
    disconnect(m_captureFailureConnection);
    disconnect(m_frameAcceptedConnection);
    disconnect(m_progressConnection);
    disconnect(m_completedConnection);
    disconnect(m_processorFailureConnection);
    m_frameConnection = {};
    m_activeConnection = {};
    m_captureFailureConnection = {};
    m_frameAcceptedConnection = {};
    m_progressConnection = {};
    m_completedConnection = {};
    m_processorFailureConnection = {};
}

void CaptureSession::cleanup() {
    if (m_cleaningUp) {
        return;
    }

    m_cleaningUp = true;
    m_watchdogArmed = false;
    m_noFrameWatchdog.stop();
    disconnectCapture();
    m_captureSource.stop();
    m_frameProcessor.stop();
    m_cleaningUp = false;
}

void CaptureSession::finishSuccess(const OutputResult& result) {
    if (m_state != SessionState::Capturing) {
        return;
    }

    const std::optional<ScreenSelection> successfulSelection = m_selection;
    cleanup();
    if (successfulSelection.has_value()) {
        const std::optional<QRectF> persistedRect = normalizedRect(*successfulSelection);
        if (persistedRect.has_value()) {
            m_settingsStore.saveSelection(QStringView(successfulSelection->screenId), *persistedRect);
        }
    }
    if (!transitionTo(SessionState::Completed)) {
        return;
    }
    emit completed(result);
    queueIdle();
}

void CaptureSession::finishFailure(const QString& message) {
    if (m_state != SessionState::Selecting && m_state != SessionState::Adjusting
        && m_state != SessionState::Capturing) {
        return;
    }

    cleanup();
    if (!transitionTo(SessionState::Error)) {
        return;
    }
    emit failed(message);
    queueIdle();
}

void CaptureSession::queueIdle() {
    if (m_idleQueued || !isTransient(m_state)) {
        return;
    }

    m_idleQueued = true;
    QMetaObject::invokeMethod(this,
        [this] {
            m_idleQueued = false;
            if (!isTransient(m_state)) {
                return;
            }
            (void)transitionTo(SessionState::Idle);
        },
        Qt::QueuedConnection);
}

bool CaptureSession::isValidSelection(const ScreenSelection& selection) {
    return !selection.screenId.isEmpty() && isFiniteRect(selection.screenGeometry)
        && selection.screenGeometry.width() > 0.0 && selection.screenGeometry.height() > 0.0
        && isFiniteRect(selection.logicalRect) && selection.logicalRect.width() > 0.0
        && selection.logicalRect.height() > 0.0
        && selection.screenGeometry.contains(selection.logicalRect);
}

std::optional<QRectF> CaptureSession::normalizedRect(const ScreenSelection& selection) {
    if (!isValidSelection(selection)) {
        return std::nullopt;
    }

    const QRectF& screen = selection.screenGeometry;
    const QRectF& logical = selection.logicalRect;
    return QRectF((logical.x() - screen.x()) / screen.width(),
        (logical.y() - screen.y()) / screen.height(), logical.width() / screen.width(),
        logical.height() / screen.height());
}

} // namespace cimbarpunk
