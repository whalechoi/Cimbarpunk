// SPDX-License-Identifier: GPL-3.0-only
#include "tray/TrayController.h"

#include "settings/SettingsStore.h"

#include <QAction>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>

#include <algorithm>
#include <cmath>
#include <utility>

namespace cimbarpunk {

namespace {

QString boundedSafeText(QString text, const qsizetype maximumCharacters,
    const QString& fallback) {
    for (QChar& character : text) {
        if (character.unicode() < 32 || character.unicode() == 127) {
            character = QLatin1Char('_');
        }
    }
    text = text.trimmed();
    if (text.isEmpty()) {
        text = fallback;
    }
    if (text.size() <= maximumCharacters) {
        return text;
    }

    text.truncate(maximumCharacters - 1);
    if (!text.isEmpty() && text.back().isHighSurrogate()) {
        text.chop(1);
    }
    text.append(QChar(0x2026));
    return text;
}

QString safeFilename(const QString& finalPath) {
    return boundedSafeText(QFileInfo(finalPath).fileName(), 48,
        QStringLiteral("文件"));
}

} // namespace

TrayController::TrayController(SettingsStore& settingsStore, QObject* parent)
    : TrayController(settingsStore,
          PlatformOperations{
              .openUrl = [](const QUrl& url) { return QDesktopServices::openUrl(url); },
              .chooseDirectory = [](const QString& caption, const QString& initialDirectory) {
                  return QFileDialog::getExistingDirectory(nullptr, caption, initialDirectory,
                      QFileDialog::ShowDirsOnly);
              },
              .showMessage = {},
              .supportsMessages = [] { return QSystemTrayIcon::supportsMessages(); },
          },
          parent) {
}

TrayController::TrayController(SettingsStore& settingsStore, PlatformOperations operations,
    QObject* parent)
    : QObject(parent)
    , m_settingsStore(settingsStore)
    , m_operations(std::move(operations))
    , m_menu(std::make_unique<QMenu>())
    , m_trayIcon(std::make_unique<QSystemTrayIcon>(
          QIcon(QStringLiteral(":/cimbarpunk/icons/tray.svg")))) {
    m_statusAction = m_menu->addAction(QStringLiteral("状态：空闲"));
    m_statusAction->setEnabled(false);
    m_progressAction = m_menu->addAction(QString());
    m_progressAction->setEnabled(false);
    m_startAction = m_menu->addAction(QStringLiteral("开始捕获…"));
    m_stopAction = m_menu->addAction(QStringLiteral("停止捕获"));
    m_openDirectoryAction = m_menu->addAction(QStringLiteral("打开保存目录"));
    m_changeDirectoryAction = m_menu->addAction(QStringLiteral("更改保存目录…"));
    m_menu->addSeparator();
    m_quitAction = m_menu->addAction(QStringLiteral("退出"));

    m_trayIcon->setContextMenu(m_menu.get());
    m_trayIcon->setToolTip(QStringLiteral("Cimbarpunk"));

    connect(m_startAction, &QAction::triggered, this, &TrayController::startCapture);
    connect(m_stopAction, &QAction::triggered, this, &TrayController::stopCapture);
    connect(m_openDirectoryAction, &QAction::triggered, this, [this] {
        emit openOutputDirectory();
        openDirectory(m_settingsStore.outputDirectory());
    });
    connect(m_changeDirectoryAction, &QAction::triggered, this, [this] {
        emit changeOutputDirectory();
        chooseOutputDirectory();
    });
    connect(m_quitAction, &QAction::triggered, this, &TrayController::quitRequested);
    connect(m_trayIcon.get(), &QSystemTrayIcon::messageClicked, this, [this] {
        if (!m_notificationDirectory.isEmpty()) {
            openDirectory(m_notificationDirectory);
        }
    });

    updateMenu();
}

TrayController::~TrayController() = default;

void TrayController::show() {
    m_trayIcon->show();
}

void TrayController::setCaptureActive(const bool active) {
    m_captureActive = active;
    if (active) {
        clearOutcomeStatus();
    }
    if (!active) {
        m_progress.reset();
    }
    updateMenu();
}

void TrayController::setProgress(const std::optional<double> progress) {
    if (progress.has_value() && std::isfinite(*progress)) {
        m_progress = std::clamp(*progress, 0.0, 1.0);
    } else {
        m_progress.reset();
    }
    updateMenu();
}

void TrayController::notifySavedFile(const QString& finalPath) {
    const QFileInfo fileInfo(finalPath);
    m_notificationDirectory = fileInfo.absolutePath();
    const QString filename = safeFilename(finalPath);
    if (supportsMessages()) {
        clearOutcomeStatus();
        showMessage(QStringLiteral("已保存：%1").arg(filename));
    } else {
        m_outcomeStatus = QStringLiteral("状态：已保存 %1").arg(filename);
        m_outcomeToolTip = QStringLiteral("Cimbarpunk — 已保存 %1").arg(filename);
        updateMenu();
    }
}

void TrayController::notifyFailure(const QString& message) {
    m_notificationDirectory.clear();
    if (supportsMessages()) {
        clearOutcomeStatus();
        showMessage(QStringLiteral("失败：%1")
                        .arg(boundedSafeText(message, 80, QStringLiteral("任务失败"))));
    } else {
        m_outcomeStatus = QStringLiteral("状态：任务失败");
        m_outcomeToolTip = QStringLiteral("Cimbarpunk — 任务失败");
        updateMenu();
    }
}

void TrayController::openDirectory(const QString& directory) {
    if (!directory.isEmpty() && m_operations.openUrl) {
        (void)m_operations.openUrl(QUrl::fromLocalFile(directory));
    }
}

void TrayController::chooseOutputDirectory() {
    if (!m_operations.chooseDirectory) {
        return;
    }

    const QString selected = m_operations.chooseDirectory(
        QStringLiteral("选择保存目录"), m_settingsStore.outputDirectory());
    if (!selected.isEmpty()) {
        m_settingsStore.setOutputDirectory(selected);
    }
}

void TrayController::showMessage(const QString& body) {
    if (m_operations.showMessage) {
        m_operations.showMessage(QStringLiteral("Cimbarpunk"), body);
        return;
    }
    m_trayIcon->showMessage(QStringLiteral("Cimbarpunk"), body,
        QSystemTrayIcon::Information);
}

bool TrayController::supportsMessages() const {
    return m_operations.supportsMessages ? m_operations.supportsMessages() : false;
}

void TrayController::clearOutcomeStatus() {
    m_outcomeStatus.clear();
    m_outcomeToolTip.clear();
}

void TrayController::updateMenu() {
    if (m_captureActive) {
        m_statusAction->setText(QStringLiteral("状态：正在捕获"));
        m_trayIcon->setToolTip(QStringLiteral("Cimbarpunk — 正在捕获"));
    } else if (!m_outcomeStatus.isEmpty()) {
        m_statusAction->setText(m_outcomeStatus);
        m_trayIcon->setToolTip(m_outcomeToolTip);
    } else {
        m_statusAction->setText(QStringLiteral("状态：空闲"));
        m_trayIcon->setToolTip(QStringLiteral("Cimbarpunk"));
    }
    m_startAction->setEnabled(!m_captureActive);
    m_stopAction->setVisible(m_captureActive);
    m_stopAction->setEnabled(m_captureActive);
    m_changeDirectoryAction->setEnabled(!m_captureActive);

    const bool showProgress = m_captureActive && m_progress.has_value();
    m_progressAction->setVisible(showProgress);
    if (showProgress) {
        m_progressAction->setText(
            QStringLiteral("进度：%1%").arg(qRound(*m_progress * 100.0)));
    }
}

} // namespace cimbarpunk
