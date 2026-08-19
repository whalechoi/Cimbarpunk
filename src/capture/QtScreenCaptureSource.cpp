// SPDX-License-Identifier: GPL-3.0-only
#include "capture/QtScreenCaptureSource.h"

#include <QMediaCaptureSession>
#include <QMetaObject>
#include <QScreen>
#include <QScreenCapture>
#include <QVideoFrame>
#include <QVideoSink>

#include <utility>

namespace cimbarpunk {

namespace detail {

class QtScreenCaptureBackend final : public IScreenCaptureBackend {
public:
    QtScreenCaptureBackend() {
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

    ~QtScreenCaptureBackend() override {
        stop();
        m_session.setVideoSink(nullptr);
        m_session.setScreenCapture(nullptr);
    }

    void setCallbacks(Callbacks callbacks) override {
        m_callbacks = std::move(callbacks);
    }

    void setScreen(QScreen* screen) override {
        m_capture.setScreen(screen);
    }

    void start() override {
        m_capture.setActive(true);
    }

    void stop() override {
        m_capture.setActive(false);
    }

private:
    Callbacks m_callbacks;
    QScreenCapture m_capture;
    QVideoSink m_sink;
    QMediaCaptureSession m_session;
};

} // namespace detail

QtScreenCaptureSource::QtScreenCaptureSource(QObject* parent)
    : QtScreenCaptureSource(std::make_unique<detail::QtScreenCaptureBackend>(), parent) {
}

QtScreenCaptureSource::QtScreenCaptureSource(
    std::unique_ptr<detail::IScreenCaptureBackend> backend, QObject* parent)
    : ICaptureSource(parent)
    , m_backend(std::move(backend)) {
    if (m_backend == nullptr) {
        return;
    }

    detail::IScreenCaptureBackend::Callbacks callbacks;
    callbacks.frameReady = [this](const QImage& frame) { handleFrame(frame); };
    callbacks.activeChanged = [this](const bool active) { handleActiveChanged(active); };
    callbacks.failed = [this](const QString& message) { handleFailure(message); };
    m_backend->setCallbacks(std::move(callbacks));
}

QtScreenCaptureSource::~QtScreenCaptureSource() {
    if (m_backend != nullptr) {
        m_backend->setCallbacks({});
        if (m_started) {
            disconnectScreenSignals();
            m_backend->stop();
        }
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
    connectScreenSignals(screen);
    m_backend->setScreen(screen);
    if (m_started) {
        m_backend->start();
    }
    m_startInProgress = false;
    if (!m_started) {
        m_backend->stop();
        handleActiveChanged(false);
        if (error != nullptr) {
            *error = m_startFailure.isEmpty() ? QStringLiteral("屏幕捕获在启动期间停止")
                                              : m_startFailure;
        }
        return false;
    }
    return true;
}

void QtScreenCaptureSource::stop() {
    if (!m_started) {
        return;
    }

    m_started = false;
    disconnectScreenSignals();
    m_backend->stop();
    handleActiveChanged(false);
}

void QtScreenCaptureSource::handleFrame(const QImage& frame) {
    if (!m_started || frame.isNull()) {
        return;
    }

    const QImage ownedFrame = frame.copy();
    if (!ownedFrame.isNull()) {
        emit frameReady(ownedFrame);
    }
}

void QtScreenCaptureSource::handleActiveChanged(const bool active) {
    if ((active && !m_started) || active == m_active) {
        return;
    }

    m_active = active;
    emit activeChanged(active);
}

void QtScreenCaptureSource::handleFailure(const QString& message) {
    if (!m_started || m_failureEmitted) {
        return;
    }

    m_failureEmitted = true;
    m_startFailure = message.isEmpty() ? QStringLiteral("屏幕捕获失败") : message;
    stop();
    emit failed(m_startFailure);
}

void QtScreenCaptureSource::connectScreenSignals(QScreen* screen) {
    disconnectScreenSignals();
    if (screen == nullptr) {
        return;
    }

    const auto changed = [this] {
        handleFailure(QStringLiteral("显示器配置在捕获期间发生变化，请重新选择区域"));
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
