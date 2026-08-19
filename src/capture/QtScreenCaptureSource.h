// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "capture/ICaptureSource.h"

#include <QMetaObject>
#include <QTimer>

#include <array>
#include <chrono>
#include <functional>
#include <memory>

class QtScreenCaptureSourceTest;

namespace cimbarpunk {

namespace detail {

class IScreenCaptureBackend {
public:
    struct Callbacks {
        std::function<void(const QImage&)> frameReady;
        std::function<void(bool)> activeChanged;
        std::function<void(const QString&)> failed;
    };

    virtual ~IScreenCaptureBackend() = default;
    virtual void setCallbacks(Callbacks callbacks) = 0;
    virtual void setScreen(QScreen* screen) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
};

} // namespace detail

class QtScreenCaptureSource final : public ICaptureSource {
    Q_OBJECT

public:
    explicit QtScreenCaptureSource(QObject* parent = nullptr);
    ~QtScreenCaptureSource() override;

    bool start(QScreen* screen, QString* error) override;
    void stop() override;

private:
    explicit QtScreenCaptureSource(
        std::unique_ptr<detail::IScreenCaptureBackend> backend,
        std::chrono::milliseconds startupTimeout, QObject* parent = nullptr);

    friend class ::QtScreenCaptureSourceTest;

    void installBackendCallbacks(quint64 generation);
    void clearBackendCallbacks();
    void handleFrame(quint64 generation, const QImage& frame);
    void handleActiveChanged(quint64 generation, bool active);
    void handleFailure(quint64 generation, const QString& message);
    void updateActive(bool active);
    void armStartupTimeout(quint64 generation);
    void cancelStartupTimeout();
    void connectScreenSignals(QScreen* screen, quint64 generation);
    void disconnectScreenSignals();

    std::unique_ptr<detail::IScreenCaptureBackend> m_backend;
    std::array<QMetaObject::Connection, 5> m_screenConnections;
    QMetaObject::Connection m_startupTimeoutConnection;
    QTimer m_startupTimer;
    QString m_startFailure;
    std::chrono::milliseconds m_startupTimeout;
    quint64 m_captureGeneration = 0;
    bool m_started = false;
    bool m_startInProgress = false;
    bool m_active = false;
    bool m_failureEmitted = false;
};

} // namespace cimbarpunk
