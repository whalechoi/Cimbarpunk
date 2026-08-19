// SPDX-License-Identifier: GPL-3.0-only
#include "app/AppRuntime.h"

#include <QScreen>
#include <QtTest/QTest>

#include <functional>
#include <optional>
#include <utility>

namespace {

QScreen* screenSentinel(const quintptr value) {
    return reinterpret_cast<QScreen*>(value);
}

struct FakeRuntime final {
    std::function<void()> trayStart;
    std::function<void()> trayStop;
    std::function<void()> trayQuit;
    std::function<void(const cimbarpunk::ScreenSelection&)> overlayAccepted;
    std::function<void()> overlayCancelled;
    std::function<void(cimbarpunk::SessionState)> sessionStateChanged;
    std::function<void(double)> sessionProgress;
    std::function<void(const cimbarpunk::OutputResult&)> sessionCompleted;
    std::function<void(const QString&)> sessionFailed;

    QStringList events;
    QScreen* cursorScreen = nullptr;
    QScreen* primaryScreen = nullptr;
    bool beginSelectionResult = true;
    bool selectionCreatedResult = true;
    bool confirmSelectionResult = true;
    std::optional<QRectF> restoredRect;
    std::optional<cimbarpunk::ScreenSelection> receivedSelection;
    QScreen* shownScreen = nullptr;
    std::optional<QRectF> shownRect;
    QList<std::optional<double>> overlayProgress;
    QList<std::optional<double>> trayProgress;
    QList<bool> trayCaptureStates;
    QStringList notifications;
    QStringList failures;
    int hideCount = 0;
    int cancelCount = 0;
};

cimbarpunk::ScreenSelection selection() {
    return {
        .screenId = QStringLiteral("screen-A"),
        .screenGeometry = QRectF(0, 0, 1920, 1080),
        .logicalRect = QRectF(100, 100, 800, 600),
    };
}

} // namespace

class AppRuntimeTest final : public QObject {
    Q_OBJECT

private slots:
    void startupCleansRegisteredFilesBeforeShowingTheTray() {
        FakeRuntime fake;
        cimbarpunk::AppRuntime runtime(makePorts(fake));

        runtime.start();
        runtime.start();

        QCOMPARE(fake.events, QStringList({QStringLiteral("logger.install"),
                                  QStringLiteral("output.cleanup"), QStringLiteral("tray.show")}));
    }

    void startUsesTheScreenAtTheCursorAndRestoresOnlyThatScreensSelection() {
        FakeRuntime fake;
        fake.cursorScreen = screenSentinel(0x101);
        fake.primaryScreen = screenSentinel(0x202);
        fake.restoredRect = QRectF(0.1, 0.2, 0.3, 0.4);
        cimbarpunk::AppRuntime runtime(makePorts(fake));

        fake.trayStart();

        QCOMPARE(fake.events, QStringList({QStringLiteral("session.beginSelection"),
                                  QStringLiteral("screen.atCursor"),
                                  QStringLiteral("screen.identity:cursor"),
                                  QStringLiteral("settings.restore:cursor"),
                                  QStringLiteral("overlay.show")}));
        QCOMPARE(fake.shownScreen, fake.cursorScreen);
        QCOMPARE(fake.shownRect, fake.restoredRect);
    }

    void startFallsBackToThePrimaryScreenWhenTheCursorHasNoScreen() {
        FakeRuntime fake;
        fake.primaryScreen = screenSentinel(0x303);
        cimbarpunk::AppRuntime runtime(makePorts(fake));

        fake.trayStart();

        QCOMPARE(fake.shownScreen, fake.primaryScreen);
        QVERIFY(fake.events.contains(QStringLiteral("screen.primary")));
        QCOMPARE(fake.events.count(QStringLiteral("overlay.show")), 1);
    }

    void overlayAcceptanceReachesTheSessionBeforeCapturePresentation() {
        FakeRuntime fake;
        cimbarpunk::AppRuntime runtime(makePorts(fake));
        const cimbarpunk::ScreenSelection accepted = selection();

        fake.overlayAccepted(accepted);

        QVERIFY(fake.receivedSelection.has_value());
        QCOMPARE(fake.receivedSelection->screenId, accepted.screenId);
        QCOMPARE(fake.events,
            QStringList({QStringLiteral("session.selectionCreated"),
                QStringLiteral("overlay.captureMode"), QStringLiteral("overlay.progress:none"),
                QStringLiteral("tray.progress:none"), QStringLiteral("session.confirmSelection")}));
    }

    void progressUpdatesBothOverlayAndTrayAndStateUpdatesTrayAvailability() {
        FakeRuntime fake;
        cimbarpunk::AppRuntime runtime(makePorts(fake));

        fake.sessionStateChanged(cimbarpunk::SessionState::Capturing);
        fake.sessionProgress(0.37);
        fake.sessionStateChanged(cimbarpunk::SessionState::Idle);

        QCOMPARE(fake.trayCaptureStates, QList<bool>({true, false}));
        QCOMPARE(fake.overlayProgress, QList<std::optional<double>>({0.37, std::nullopt}));
        QCOMPARE(fake.trayProgress,
            QList<std::optional<double>>({0.37, std::nullopt}));
    }

    void successfulCompletionHidesAndNotifiesExactlyOnceDespiteLateDuplicates() {
        FakeRuntime fake;
        cimbarpunk::AppRuntime runtime(makePorts(fake));
        const cimbarpunk::OutputResult result{
            .ok = true,
            .finalPath = QStringLiteral("D:/decoded/finished.bin"),
        };

        fake.sessionCompleted(result);
        fake.sessionCompleted(result);

        QCOMPARE(fake.hideCount, 1);
        QCOMPARE(fake.notifications, QStringList{result.finalPath});
    }

    void stopAndOverlayCancellationUseTheSessionCancellationPath() {
        FakeRuntime fake;
        cimbarpunk::AppRuntime runtime(makePorts(fake));

        fake.trayStop();
        fake.overlayCancelled();
        fake.sessionStateChanged(cimbarpunk::SessionState::Cancelled);

        QCOMPARE(fake.cancelCount, 2);
        QCOMPARE(fake.hideCount, 1);
    }

    void failureHidesTheOverlayAndReachesTheTray() {
        FakeRuntime fake;
        cimbarpunk::AppRuntime runtime(makePorts(fake));

        fake.sessionFailed(QStringLiteral("捕获失败"));

        QCOMPARE(fake.hideCount, 1);
        QCOMPARE(fake.failures, QStringList{QStringLiteral("捕获失败")});
    }

    void quitShutsTheSessionDownBeforeCallingTheInjectedQuitOnce() {
        FakeRuntime fake;
        {
            cimbarpunk::AppRuntime runtime(makePorts(fake));

            fake.trayQuit();
            fake.trayQuit();
        }

        QCOMPARE(fake.events,
            QStringList({QStringLiteral("session.shutdown"), QStringLiteral("application.quit")}));
    }

private:
    static cimbarpunk::AppRuntime::Ports makePorts(FakeRuntime& fake) {
        using VoidHandler = cimbarpunk::AppRuntime::Ports::VoidHandler;
        using StateHandler = cimbarpunk::AppRuntime::Ports::StateHandler;
        using SelectionHandler = cimbarpunk::AppRuntime::Ports::SelectionHandler;
        using ProgressHandler = cimbarpunk::AppRuntime::Ports::ProgressHandler;
        using CompletedHandler = cimbarpunk::AppRuntime::Ports::CompletedHandler;
        using FailureHandler = cimbarpunk::AppRuntime::Ports::FailureHandler;

        return {
            .onTrayStart = [&fake](QObject*, VoidHandler handler) {
                fake.trayStart = std::move(handler);
            },
            .onTrayStop = [&fake](QObject*, VoidHandler handler) {
                fake.trayStop = std::move(handler);
            },
            .onTrayQuit = [&fake](QObject*, VoidHandler handler) {
                fake.trayQuit = std::move(handler);
            },
            .onOverlayAccepted = [&fake](QObject*, SelectionHandler handler) {
                fake.overlayAccepted = std::move(handler);
            },
            .onOverlayCancelled = [&fake](QObject*, VoidHandler handler) {
                fake.overlayCancelled = std::move(handler);
            },
            .onSessionStateChanged = [&fake](QObject*, StateHandler handler) {
                fake.sessionStateChanged = std::move(handler);
            },
            .onSessionProgress = [&fake](QObject*, ProgressHandler handler) {
                fake.sessionProgress = std::move(handler);
            },
            .onSessionCompleted = [&fake](QObject*, CompletedHandler handler) {
                fake.sessionCompleted = std::move(handler);
            },
            .onSessionFailed = [&fake](QObject*, FailureHandler handler) {
                fake.sessionFailed = std::move(handler);
            },
            .installLogger = [&fake] { fake.events.append(QStringLiteral("logger.install")); },
            .cleanupTemporaryFiles = [&fake] {
                fake.events.append(QStringLiteral("output.cleanup"));
            },
            .showTray = [&fake] { fake.events.append(QStringLiteral("tray.show")); },
            .screenAtCursor = [&fake] {
                fake.events.append(QStringLiteral("screen.atCursor"));
                return fake.cursorScreen;
            },
            .primaryScreen = [&fake] {
                fake.events.append(QStringLiteral("screen.primary"));
                return fake.primaryScreen;
            },
            .screenIdentity = [&fake](QScreen* screen) {
                const QString id = screen == fake.cursorScreen ? QStringLiteral("cursor")
                                                               : QStringLiteral("primary");
                fake.events.append(QStringLiteral("screen.identity:") + id);
                return id;
            },
            .restoreSelection = [&fake](const QString& screenId) {
                fake.events.append(QStringLiteral("settings.restore:") + screenId);
                return fake.restoredRect;
            },
            .beginSelection = [&fake] {
                fake.events.append(QStringLiteral("session.beginSelection"));
                return fake.beginSelectionResult;
            },
            .selectionCreated = [&fake](const cimbarpunk::ScreenSelection& selected) {
                fake.events.append(QStringLiteral("session.selectionCreated"));
                fake.receivedSelection = selected;
                return fake.selectionCreatedResult;
            },
            .confirmSelection = [&fake] {
                fake.events.append(QStringLiteral("session.confirmSelection"));
                return fake.confirmSelectionResult;
            },
            .cancelSession = [&fake] {
                ++fake.cancelCount;
                fake.events.append(QStringLiteral("session.cancel"));
            },
            .shutdownSession = [&fake] {
                fake.events.append(QStringLiteral("session.shutdown"));
            },
            .showOverlay = [&fake](QScreen* screen, std::optional<QRectF> restored) {
                fake.events.append(QStringLiteral("overlay.show"));
                fake.shownScreen = screen;
                fake.shownRect = restored;
            },
            .enterOverlayCaptureMode = [&fake] {
                fake.events.append(QStringLiteral("overlay.captureMode"));
            },
            .setOverlayProgress = [&fake](std::optional<double> progress) {
                fake.overlayProgress.append(progress);
                fake.events.append(progress.has_value() ? QStringLiteral("overlay.progress:value")
                                                        : QStringLiteral("overlay.progress:none"));
            },
            .hideOverlay = [&fake] {
                ++fake.hideCount;
                fake.events.append(QStringLiteral("overlay.hide"));
            },
            .setTrayCaptureActive = [&fake](const bool active) {
                fake.trayCaptureStates.append(active);
                fake.events.append(active ? QStringLiteral("tray.active")
                                          : QStringLiteral("tray.idle"));
            },
            .setTrayProgress = [&fake](std::optional<double> progress) {
                fake.trayProgress.append(progress);
                fake.events.append(progress.has_value() ? QStringLiteral("tray.progress:value")
                                                        : QStringLiteral("tray.progress:none"));
            },
            .notifySavedFile = [&fake](const QString& path) {
                fake.notifications.append(path);
                fake.events.append(QStringLiteral("tray.notifySaved"));
            },
            .notifyFailure = [&fake](const QString& message) {
                fake.failures.append(message);
                fake.events.append(QStringLiteral("tray.notifyFailure"));
            },
            .quitApplication = [&fake] {
                fake.events.append(QStringLiteral("application.quit"));
            },
        };
    }
};

QTEST_MAIN(AppRuntimeTest)
#include "tst_app_runtime.moc"
