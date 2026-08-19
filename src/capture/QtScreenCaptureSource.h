// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "capture/ICaptureSource.h"

#include <QMetaObject>

#include <array>
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
        std::unique_ptr<detail::IScreenCaptureBackend> backend, QObject* parent = nullptr);

    friend class ::QtScreenCaptureSourceTest;

    void handleFrame(const QImage& frame);
    void handleActiveChanged(bool active);
    void handleFailure(const QString& message);
    void connectScreenSignals(QScreen* screen);
    void disconnectScreenSignals();

    std::unique_ptr<detail::IScreenCaptureBackend> m_backend;
    std::array<QMetaObject::Connection, 5> m_screenConnections;
    QString m_startFailure;
    bool m_started = false;
    bool m_startInProgress = false;
    bool m_active = false;
    bool m_failureEmitted = false;
};

} // namespace cimbarpunk
