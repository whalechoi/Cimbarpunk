// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "core/SessionTypes.h"

#include <QMetaObject>
#include <QObject>
#include <QTimer>

#include <chrono>
#include <functional>
#include <optional>

class QScreen;

namespace cimbarpunk {

class ICaptureSource;
class IFrameProcessor;
class SettingsStore;

class CaptureSession final : public QObject {
    Q_OBJECT
    Q_PROPERTY(cimbarpunk::SessionState state READ state NOTIFY stateChanged)

public:
    using ScreenResolver = std::function<QScreen*(QStringView)>;

    CaptureSession(ICaptureSource& captureSource, IFrameProcessor& frameProcessor,
        SettingsStore& settingsStore, ScreenResolver screenResolver,
        std::chrono::milliseconds noFrameTimeout = std::chrono::milliseconds(5000),
        QObject* parent = nullptr);
    ~CaptureSession() override;

    CaptureSession(const CaptureSession&) = delete;
    CaptureSession& operator=(const CaptureSession&) = delete;

    [[nodiscard]] SessionState state() const;

    bool beginSelection();
    bool selectionCreated(const ScreenSelection& selection);
    bool confirmSelection();
    bool cancel();
    bool stop();
    void shutdown();

signals:
    void stateChanged(cimbarpunk::SessionState state);
    void selectionRequested();
    void progressChanged(double progress);
    void completed(const cimbarpunk::OutputResult& result);
    void failed(const QString& message);

private:
    [[nodiscard]] bool transitionTo(SessionState next);
    [[nodiscard]] bool isCurrentCapture(quint64 generation) const;
    void connectCapture(quint64 generation);
    void disconnectCapture();
    void cleanup();
    void finishSuccess(const OutputResult& result);
    void finishFailure(const QString& message);
    void queueIdle();

    [[nodiscard]] static bool isValidSelection(const ScreenSelection& selection);
    [[nodiscard]] static std::optional<QRectF> normalizedRect(const ScreenSelection& selection);

    ICaptureSource& m_captureSource;
    IFrameProcessor& m_frameProcessor;
    SettingsStore& m_settingsStore;
    ScreenResolver m_screenResolver;
    QTimer m_noFrameWatchdog;
    std::chrono::milliseconds m_noFrameTimeout;
    SessionState m_state = SessionState::Idle;
    std::optional<ScreenSelection> m_selection;
    QString m_outputDirectory;
    quint64 m_captureGeneration = 0;
    bool m_watchdogArmed = false;
    bool m_cleaningUp = false;
    bool m_idleQueued = false;
    bool m_shutdown = false;

    QMetaObject::Connection m_frameConnection;
    QMetaObject::Connection m_activeConnection;
    QMetaObject::Connection m_captureFailureConnection;
    QMetaObject::Connection m_frameAcceptedConnection;
    QMetaObject::Connection m_progressConnection;
    QMetaObject::Connection m_completedConnection;
    QMetaObject::Connection m_processorFailureConnection;
};

} // namespace cimbarpunk
