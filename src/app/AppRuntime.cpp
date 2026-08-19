// SPDX-License-Identifier: GPL-3.0-only
#include "app/AppRuntime.h"

#include "capture/QtScreenCaptureSource.h"
#include "core/Version.h"
#include "decoder/CimbarDecoderAdapter.h"
#include "diagnostics/RotatingLogger.h"
#include "output/LibcimbarPayloadWriter.h"
#include "output/OutputStore.h"
#include "pipeline/DecodeWorker.h"
#include "selection/ScreenIdentity.h"
#include "selection/SelectionOverlayController.h"
#include "session/CaptureSession.h"
#include "settings/SettingsStore.h"
#include "tray/TrayController.h"

#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QStringView>

#include <utility>

namespace cimbarpunk {

namespace {

QScreen* resolveScreen(const QStringView screenId) {
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen != nullptr && ScreenIdentity::fromScreen(*screen) == screenId) {
            return screen;
        }
    }
    return nullptr;
}

bool isActiveState(const SessionState state) {
    return state == SessionState::Selecting || state == SessionState::Adjusting
        || state == SessionState::Capturing;
}

} // namespace

class AppRuntime::Production final {
public:
    Production()
        : settings()
        , logger()
        , output(settings, makeLibcimbarPayloadWriter())
        , decoder()
        , worker(decoder, output, logger)
        , capture()
        , session(capture, worker, settings, resolveScreen)
        , overlay()
        , tray(settings) {
    }

    // Dependency owners are intentionally declared in construction order.
    // Reverse destruction closes the UI and session before the worker's
    // externally-owned decoder/output/logger dependencies.
    SettingsStore settings;
    RotatingLogger logger;
    OutputStore output;
    CimbarDecoderAdapter decoder;
    DecodeWorker worker;
    QtScreenCaptureSource capture;
    CaptureSession session;
    SelectionOverlayController overlay;
    TrayController tray;
};

AppRuntime::AppRuntime(QObject* parent)
    : QObject(parent)
    , m_production(std::make_unique<Production>())
    , m_ports(productionPorts(*m_production)) {
    wire();
}

AppRuntime::AppRuntime(Ports ports, QObject* parent)
    : QObject(parent)
    , m_ports(std::move(ports)) {
    wire();
}

AppRuntime::~AppRuntime() {
    shutdownOnce();
}

void AppRuntime::start() {
    if (m_started) {
        return;
    }
    m_started = true;
    if (m_ports.installLogger) {
        m_ports.installLogger();
    }
    if (m_ports.cleanupTemporaryFiles) {
        m_ports.cleanupTemporaryFiles();
    }
    if (m_ports.showTray) {
        m_ports.showTray();
    }
}

AppRuntime::Ports AppRuntime::productionPorts(Production& production) {
    return {
        .onTrayStart = [&production](QObject* context, Ports::VoidHandler handler) {
            QObject::connect(&production.tray, &TrayController::startCapture, context,
                std::move(handler));
        },
        .onTrayStop = [&production](QObject* context, Ports::VoidHandler handler) {
            QObject::connect(&production.tray, &TrayController::stopCapture, context,
                std::move(handler));
        },
        .onTrayQuit = [&production](QObject* context, Ports::VoidHandler handler) {
            QObject::connect(&production.tray, &TrayController::quitRequested, context,
                std::move(handler));
        },
        .onOverlayAccepted = [&production](QObject* context, Ports::SelectionHandler handler) {
            QObject::connect(&production.overlay, &SelectionOverlayController::accepted, context,
                std::move(handler));
        },
        .onOverlayCancelled = [&production](QObject* context, Ports::VoidHandler handler) {
            QObject::connect(&production.overlay, &SelectionOverlayController::cancelled, context,
                std::move(handler));
        },
        .onSessionStateChanged = [&production](QObject* context, Ports::StateHandler handler) {
            QObject::connect(&production.session, &CaptureSession::stateChanged, context,
                std::move(handler));
        },
        .onSessionProgress = [&production](QObject* context, Ports::ProgressHandler handler) {
            QObject::connect(&production.session, &CaptureSession::progressChanged, context,
                std::move(handler));
        },
        .onSessionCompleted = [&production](QObject* context, Ports::CompletedHandler handler) {
            QObject::connect(&production.session, &CaptureSession::completed, context,
                std::move(handler));
        },
        .onSessionFailed = [&production](QObject* context, Ports::FailureHandler handler) {
            QObject::connect(&production.session, &CaptureSession::failed, context,
                std::move(handler));
        },
        .installLogger = [&production] {
            if (production.logger.install()) {
                production.logger.write(
                    QStringLiteral("Cimbarpunk version=%1 started")
                        .arg(QString::fromLatin1(versionString().data())));
            }
        },
        .cleanupTemporaryFiles = [&production] {
            production.output.cleanupRegisteredTemporaryFiles();
        },
        .showTray = [&production] { production.tray.show(); },
        .screenAtCursor = [] { return QGuiApplication::screenAt(QCursor::pos()); },
        .primaryScreen = [] { return QGuiApplication::primaryScreen(); },
        .screenIdentity = [](QScreen* screen) {
            return screen == nullptr ? QString() : ScreenIdentity::fromScreen(*screen);
        },
        .restoreSelection = [&production](const QString& screenId) {
            return production.settings.restoreSelection(QStringView(screenId));
        },
        .beginSelection = [&production] { return production.session.beginSelection(); },
        .selectionCreated = [&production](const ScreenSelection& selection) {
            return production.session.selectionCreated(selection);
        },
        .confirmSelection = [&production] { return production.session.confirmSelection(); },
        .cancelSession = [&production] { (void)production.session.cancel(); },
        .shutdownSession = [&production] { production.session.shutdown(); },
        .showOverlay = [&production](QScreen* screen, std::optional<QRectF> normalizedRect) {
            production.overlay.showForScreen(screen, normalizedRect);
        },
        .enterOverlayCaptureMode = [&production] { production.overlay.enterCaptureMode(); },
        .setOverlayProgress = [&production](std::optional<double> progress) {
            production.overlay.setProgress(progress);
        },
        .hideOverlay = [&production] { production.overlay.hide(); },
        .setTrayCaptureActive = [&production](const bool active) {
            production.tray.setCaptureActive(active);
        },
        .setTrayProgress = [&production](std::optional<double> progress) {
            production.tray.setProgress(progress);
        },
        .notifySavedFile = [&production](const QString& finalPath) {
            production.tray.notifySavedFile(finalPath);
        },
        .notifyFailure = [&production](const QString& message) {
            production.tray.notifyFailure(message);
        },
        .quitApplication = [] { QCoreApplication::quit(); },
    };
}

void AppRuntime::wire() {
    if (m_ports.onTrayStart) {
        m_ports.onTrayStart(this, [this] { beginSelection(); });
    }
    if (m_ports.onTrayStop) {
        m_ports.onTrayStop(this, [this] {
            if (m_ports.cancelSession) {
                m_ports.cancelSession();
            }
        });
    }
    if (m_ports.onTrayQuit) {
        m_ports.onTrayQuit(this, [this] { quit(); });
    }
    if (m_ports.onOverlayAccepted) {
        m_ports.onOverlayAccepted(this,
            [this](const ScreenSelection& selection) { acceptSelection(selection); });
    }
    if (m_ports.onOverlayCancelled) {
        m_ports.onOverlayCancelled(this, [this] {
            if (m_ports.cancelSession) {
                m_ports.cancelSession();
            }
        });
    }
    if (m_ports.onSessionStateChanged) {
        m_ports.onSessionStateChanged(this, [this](const SessionState state) {
            updateState(state);
        });
    }
    if (m_ports.onSessionProgress) {
        m_ports.onSessionProgress(this,
            [this](const double progress) { updateProgress(progress); });
    }
    if (m_ports.onSessionCompleted) {
        m_ports.onSessionCompleted(this,
            [this](const OutputResult& result) { complete(result); });
    }
    if (m_ports.onSessionFailed) {
        m_ports.onSessionFailed(this, [this](const QString& message) { fail(message); });
    }
}

void AppRuntime::beginSelection() {
    m_terminalHandled = false;
    if (!m_ports.beginSelection || !m_ports.beginSelection()) {
        return;
    }

    QScreen* screen = m_ports.screenAtCursor ? m_ports.screenAtCursor() : nullptr;
    if (screen == nullptr && m_ports.primaryScreen) {
        screen = m_ports.primaryScreen();
    }
    if (screen == nullptr) {
        if (m_ports.cancelSession) {
            m_ports.cancelSession();
        }
        if (m_ports.notifyFailure) {
            m_ports.notifyFailure(QStringLiteral("没有可用显示器"));
        }
        return;
    }

    const QString screenId = m_ports.screenIdentity ? m_ports.screenIdentity(screen) : QString();
    const std::optional<QRectF> restored = m_ports.restoreSelection
        ? m_ports.restoreSelection(screenId)
        : std::nullopt;
    if (m_ports.showOverlay) {
        m_ports.showOverlay(screen, restored);
    }
}

void AppRuntime::acceptSelection(const ScreenSelection& selection) {
    if (!m_ports.selectionCreated || !m_ports.selectionCreated(selection)) {
        return;
    }
    if (m_ports.enterOverlayCaptureMode) {
        m_ports.enterOverlayCaptureMode();
    }
    if (m_ports.setOverlayProgress) {
        m_ports.setOverlayProgress(std::nullopt);
    }
    if (m_ports.setTrayProgress) {
        m_ports.setTrayProgress(std::nullopt);
    }
    if (m_ports.confirmSelection) {
        (void)m_ports.confirmSelection();
    }
}

void AppRuntime::updateState(const SessionState state) {
    if (m_ports.setTrayCaptureActive) {
        m_ports.setTrayCaptureActive(isActiveState(state));
    }
    if (state == SessionState::Idle) {
        if (m_ports.setOverlayProgress) {
            m_ports.setOverlayProgress(std::nullopt);
        }
        if (m_ports.setTrayProgress) {
            m_ports.setTrayProgress(std::nullopt);
        }
    } else if (state == SessionState::Cancelled && m_ports.hideOverlay) {
        m_ports.hideOverlay();
    }
}

void AppRuntime::updateProgress(const double progress) {
    if (m_ports.setOverlayProgress) {
        m_ports.setOverlayProgress(progress);
    }
    if (m_ports.setTrayProgress) {
        m_ports.setTrayProgress(progress);
    }
}

void AppRuntime::complete(const OutputResult& result) {
    if (m_terminalHandled || !result.ok) {
        return;
    }
    m_terminalHandled = true;
    if (m_ports.hideOverlay) {
        m_ports.hideOverlay();
    }
    if (m_ports.notifySavedFile) {
        m_ports.notifySavedFile(result.finalPath);
    }
}

void AppRuntime::fail(const QString& message) {
    if (m_terminalHandled) {
        return;
    }
    m_terminalHandled = true;
    if (m_ports.hideOverlay) {
        m_ports.hideOverlay();
    }
    if (m_ports.notifyFailure) {
        m_ports.notifyFailure(message);
    }
}

void AppRuntime::shutdownOnce() {
    if (m_shutdown) {
        return;
    }
    m_shutdown = true;
    if (m_ports.shutdownSession) {
        m_ports.shutdownSession();
    }
}

void AppRuntime::quit() {
    if (m_quitting) {
        return;
    }
    m_quitting = true;
    shutdownOnce();
    if (m_ports.quitApplication) {
        m_ports.quitApplication();
    }
}

} // namespace cimbarpunk
