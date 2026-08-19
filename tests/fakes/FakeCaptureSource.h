// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "capture/ICaptureSource.h"

#include <QStringList>

namespace cimbarpunk::test {

class FakeCaptureSource final : public ICaptureSource {
    Q_OBJECT

public:
    explicit FakeCaptureSource(QStringList* events = nullptr, QObject* parent = nullptr)
        : ICaptureSource(parent)
        , m_events(events) {
    }

    bool start(QScreen* screen, QString* error) override {
        ++startCalls;
        lastScreen = screen;
        record(QStringLiteral("capture.start"));
        if (!startSucceeds) {
            if (error != nullptr) {
                *error = startError;
            }
            return false;
        }

        active = true;
        if (announceActiveOnStart) {
            emit activeChanged(true);
        }
        return true;
    }

    void stop() override {
        ++stopCalls;
        record(QStringLiteral("capture.stop"));
        const bool wasActive = active;
        active = false;
        if (wasActive) {
            emit activeChanged(false);
        }
    }

    void sendFrame(const QImage& frame) {
        emit frameReady(frame);
    }

    void reportFailure(const QString& message) {
        emit failed(message);
    }

    void announceActive(bool value) {
        active = value;
        emit activeChanged(value);
    }

    bool startSucceeds = true;
    bool announceActiveOnStart = true;
    QString startError = QStringLiteral("capture start failed");
    int startCalls = 0;
    int stopCalls = 0;
    bool active = false;
    QScreen* lastScreen = nullptr;

private:
    void record(const QString& event) {
        if (m_events != nullptr) {
            m_events->append(event);
        }
    }

    QStringList* m_events;
};

} // namespace cimbarpunk::test
