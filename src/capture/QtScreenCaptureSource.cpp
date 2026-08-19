// SPDX-License-Identifier: GPL-3.0-only
#include "capture/QtScreenCaptureSource.h"

#include <QMediaCaptureSession>
#include <QMetaObject>
#include <QPointer>
#include <QScreen>
#include <QScreenCapture>
#include <QVideoFrame>
#include <QVideoSink>

#include <algorithm>
#include <utility>

namespace cimbarpunk {

namespace detail {

class QtScreenCaptureBackend final : public IScreenCaptureBackend {
public:
    ~QtScreenCaptureBackend() override {
        stop();
    }

    void setCallbacks(Callbacks callbacks) override {
        m_callbacks = std::move(callbacks);
    }

    void setScreen(QScreen* screen) override {
        m_screen = screen;
    }

    void start() override {
        stop();
        m_stopPending = false;
        m_attempt = std::make_unique<Attempt>(m_callbacks);
        m_starting = true;
        m_attempt->setScreen(m_screen.data());
        if (!m_stopPending) {
            m_attempt->start();
        }
        m_starting = false;
        if (m_stopPending) {
            m_attempt->stop();
            m_attempt.reset();
            m_stopPending = false;
        }
    }

    void stop() override {
        if (m_attempt == nullptr) {
            return;
        }

        m_attempt->stop();
        if (m_starting) {
            m_stopPending = true;
            return;
        }
        m_attempt.reset();
    }

private:
    class Attempt final {
    public:
        explicit Attempt(Callbacks callbacks)
            : m_callbacks(std::move(callbacks)) {
            m_session.setScreenCapture(&m_capture);
            m_session.setVideoSink(&m_sink);

            QObject::connect(&m_capture, &QScreenCapture::activeChanged, &m_capture,
                [this](const bool active) {
                    if (m_callbacks.activeChanged) {
                        m_callbacks.activeChanged(active);
                    }
                });
            QObject::connect(&m_capture, &QScreenCapture::errorOccurred, &m_capture,
                [this](const QScreenCapture::Error error, const QString& message) {
                    if (error != QScreenCapture::NoError && m_callbacks.failed) {
                        m_callbacks.failed(message);
                    }
                });
            QObject::connect(&m_sink, &QVideoSink::videoFrameChanged, &m_sink,
                [this](const QVideoFrame& frame) {
                    const QImage image = frame.toImage();
                    if (!image.isNull() && m_callbacks.frameReady) {
                        m_callbacks.frameReady(image);
                    }
                });
        }

        ~Attempt() {
            m_session.setVideoSink(nullptr);
            m_session.setScreenCapture(nullptr);
        }

        void setScreen(QScreen* screen) {
            m_capture.setScreen(screen);
        }

        void start() {
            m_capture.setActive(true);
        }

        void stop() {
            m_capture.setActive(false);
        }

    private:
        Callbacks m_callbacks;
        QScreenCapture m_capture;
        QVideoSink m_sink;
        QMediaCaptureSession m_session;
    };

    Callbacks m_callbacks;
    QPointer<QScreen> m_screen;
    std::unique_ptr<Attempt> m_attempt;
    bool m_starting = false;
    bool m_stopPending = false;
};

} // namespace detail

QtScreenCaptureSource::QtScreenCaptureSource(QObject* parent)
    : QtScreenCaptureSource(std::make_unique<detail::QtScreenCaptureBackend>(),
          std::chrono::seconds(5), parent) {
}

QtScreenCaptureSource::QtScreenCaptureSource(
    std::unique_ptr<detail::IScreenCaptureBackend> backend,
    const std::chrono::milliseconds startupTimeout, QObject* parent)
    : ICaptureSource(parent)
    , m_backend(std::move(backend))
    , m_startupTimeout(std::max(startupTimeout, std::chrono::milliseconds(1))) {
    m_startupTimer.setSingleShot(true);
    m_startupTimer.setInterval(m_startupTimeout);
}

QtScreenCaptureSource::~QtScreenCaptureSource() {
    ++m_captureGeneration;
    cancelStartupTimeout();
    if (m_backend != nullptr) {
        clearBackendCallbacks();
        m_started = false;
        disconnectScreenSignals();
        m_backend->stop();
    }
}

bool QtScreenCaptureSource::start(QScreen* screen, QString* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (screen == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("无法启动屏幕捕获：未选择显示器");
        }
        return false;
    }

    if (m_started || m_startInProgress) {
        if (error != nullptr) {
            *error = m_startInProgress ? QStringLiteral("屏幕捕获正在启动")
                                       : QStringLiteral("屏幕捕获已在运行");
        }
        return false;
    }
    if (m_backend == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("屏幕捕获后端不可用");
        }
        return false;
    }

    m_startFailure.clear();
    m_failureEmitted = false;
    m_startInProgress = true;
    m_started = true;
    const quint64 generation = ++m_captureGeneration;
    installBackendCallbacks(generation);
    connectScreenSignals(screen, generation);
    m_backend->setScreen(screen);
    if (m_started) {
        m_backend->start();
    }
    m_startInProgress = false;
    if (!m_started || generation != m_captureGeneration) {
        m_backend->stop();
        clearBackendCallbacks();
        updateActive(false);
        if (error != nullptr) {
            *error = m_startFailure.isEmpty() ? QStringLiteral("屏幕捕获在启动期间停止")
                                              : m_startFailure;
        }
        return false;
    }
    if (!m_active) {
        armStartupTimeout(generation);
    }
    return true;
}

void QtScreenCaptureSource::stop() {
    if (!m_started) {
        cancelStartupTimeout();
        return;
    }

    m_started = false;
    ++m_captureGeneration;
    cancelStartupTimeout();
    disconnectScreenSignals();
    m_backend->stop();
    clearBackendCallbacks();
    updateActive(false);
}

void QtScreenCaptureSource::installBackendCallbacks(const quint64 generation) {
    detail::IScreenCaptureBackend::Callbacks callbacks;
    callbacks.frameReady = [this, generation](const QImage& frame) {
        handleFrame(generation, frame);
    };
    callbacks.activeChanged = [this, generation](const bool active) {
        handleActiveChanged(generation, active);
    };
    callbacks.failed = [this, generation](const QString& message) {
        handleFailure(generation, message);
    };
    m_backend->setCallbacks(std::move(callbacks));
}

void QtScreenCaptureSource::clearBackendCallbacks() {
    if (m_backend != nullptr) {
        m_backend->setCallbacks({});
    }
}

void QtScreenCaptureSource::handleFrame(const quint64 generation, const QImage& frame) {
    if (generation != m_captureGeneration || !m_started || frame.isNull()) {
        return;
    }

    const QImage ownedFrame = frame.copy();
    if (!ownedFrame.isNull()) {
        emit frameReady(ownedFrame);
    }
}

void QtScreenCaptureSource::handleActiveChanged(
    const quint64 generation, const bool active) {
    if (generation != m_captureGeneration || !m_started) {
        return;
    }
    if (active) {
        cancelStartupTimeout();
    }
    updateActive(active);
}

void QtScreenCaptureSource::handleFailure(
    const quint64 generation, const QString& message) {
    if (generation != m_captureGeneration || !m_started || m_failureEmitted) {
        return;
    }

    m_failureEmitted = true;
    const QString failure = message.isEmpty() ? QStringLiteral("屏幕捕获失败") : message;
    m_startFailure = failure;
    stop();
    emit failed(failure);
}

void QtScreenCaptureSource::updateActive(const bool active) {
    if (active == m_active) {
        return;
    }

    m_active = active;
    emit activeChanged(active);
}

void QtScreenCaptureSource::armStartupTimeout(const quint64 generation) {
    cancelStartupTimeout();
    m_startupTimeoutConnection = connect(&m_startupTimer, &QTimer::timeout, this,
        [this, generation] {
            if (generation == m_captureGeneration && m_started && !m_active) {
                handleFailure(generation, QStringLiteral("屏幕捕获未能启动"));
            }
        });
    m_startupTimer.start();
}

void QtScreenCaptureSource::cancelStartupTimeout() {
    m_startupTimer.stop();
    disconnect(m_startupTimeoutConnection);
    m_startupTimeoutConnection = {};
}

void QtScreenCaptureSource::connectScreenSignals(
    QScreen* screen, const quint64 generation) {
    disconnectScreenSignals();
    if (screen == nullptr) {
        return;
    }

    const auto changed = [this, generation] {
        handleFailure(generation,
            QStringLiteral("显示器配置在捕获期间发生变化，请重新选择区域"));
    };
    m_screenConnections[0] = connect(screen, &QScreen::geometryChanged, this,
        [changed](const QRect&) { changed(); });
    m_screenConnections[1] = connect(screen, &QScreen::logicalDotsPerInchChanged, this,
        [changed](qreal) { changed(); });
    m_screenConnections[2] = connect(screen, &QScreen::physicalDotsPerInchChanged, this,
        [changed](qreal) { changed(); });
    m_screenConnections[3] = connect(screen, &QScreen::orientationChanged, this,
        [changed](Qt::ScreenOrientation) { changed(); });
    m_screenConnections[4] = connect(screen, &QObject::destroyed, this,
        [changed] { changed(); });
}

void QtScreenCaptureSource::disconnectScreenSignals() {
    for (QMetaObject::Connection& connection : m_screenConnections) {
        disconnect(connection);
        connection = {};
    }
}

} // namespace cimbarpunk
