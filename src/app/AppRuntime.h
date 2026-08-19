// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "core/SessionTypes.h"

#include <QObject>

#include <functional>
#include <memory>
#include <optional>

class AppRuntimeTest;
class QScreen;

namespace cimbarpunk {

class AppRuntime final : public QObject {
    Q_OBJECT

public:
    explicit AppRuntime(QObject* parent = nullptr);
    ~AppRuntime() override;

    AppRuntime(const AppRuntime&) = delete;
    AppRuntime& operator=(const AppRuntime&) = delete;

    void start();

private:
    friend class ::AppRuntimeTest;

    struct Ports {
        using VoidHandler = std::function<void()>;
        using StateHandler = std::function<void(SessionState)>;
        using SelectionHandler = std::function<void(const ScreenSelection&)>;
        using ProgressHandler = std::function<void(double)>;
        using CompletedHandler = std::function<void(const OutputResult&)>;
        using FailureHandler = std::function<void(const QString&)>;

        std::function<void(QObject*, VoidHandler)> onTrayStart;
        std::function<void(QObject*, VoidHandler)> onTrayStop;
        std::function<void(QObject*, VoidHandler)> onTrayQuit;
        std::function<void(QObject*, SelectionHandler)> onOverlayAccepted;
        std::function<void(QObject*, VoidHandler)> onOverlayCancelled;
        std::function<void(QObject*, StateHandler)> onSessionStateChanged;
        std::function<void(QObject*, ProgressHandler)> onSessionProgress;
        std::function<void(QObject*, CompletedHandler)> onSessionCompleted;
        std::function<void(QObject*, FailureHandler)> onSessionFailed;

        std::function<void()> installLogger;
        std::function<void()> cleanupTemporaryFiles;
        std::function<void()> showTray;
        std::function<QScreen*()> screenAtCursor;
        std::function<QScreen*()> primaryScreen;
        std::function<QString(QScreen*)> screenIdentity;
        std::function<std::optional<QRectF>(const QString&)> restoreSelection;
        std::function<bool()> beginSelection;
        std::function<bool(const ScreenSelection&)> selectionCreated;
        std::function<bool()> confirmSelection;
        std::function<void()> cancelSession;
        std::function<void()> shutdownSession;
        std::function<void(QScreen*, std::optional<QRectF>)> showOverlay;
        std::function<void()> enterOverlayCaptureMode;
        std::function<void(std::optional<double>)> setOverlayProgress;
        std::function<void()> hideOverlay;
        std::function<void(bool)> setTrayCaptureActive;
        std::function<void(std::optional<double>)> setTrayProgress;
        std::function<void(const QString&)> notifySavedFile;
        std::function<void(const QString&)> notifyFailure;
        std::function<void()> quitApplication;
    };

    class Production;

    explicit AppRuntime(Ports ports, QObject* parent = nullptr);
    static Ports productionPorts(Production& production);

    void wire();
    void beginSelection();
    void acceptSelection(const ScreenSelection& selection);
    void updateState(SessionState state);
    void updateProgress(double progress);
    void complete(const OutputResult& result);
    void fail(const QString& message);
    void shutdownOnce();
    void quit();

    std::unique_ptr<Production> m_production;
    Ports m_ports;
    bool m_started = false;
    bool m_terminalHandled = false;
    bool m_shutdown = false;
    bool m_quitting = false;
};

} // namespace cimbarpunk
