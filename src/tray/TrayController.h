// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>
#include <memory>
#include <optional>

class QAction;
class QMenu;
class QSystemTrayIcon;
class TrayControllerTest;

namespace cimbarpunk {

class SettingsStore;

class TrayController final : public QObject {
    Q_OBJECT

public:
    explicit TrayController(SettingsStore& settingsStore, QObject* parent = nullptr);
    ~TrayController() override;

    TrayController(const TrayController&) = delete;
    TrayController& operator=(const TrayController&) = delete;

    void show();
    void setCaptureActive(bool active);
    void setProgress(std::optional<double> progress);
    void notifySavedFile(const QString& finalPath);
    void notifyFailure(const QString& message);

signals:
    void startCapture();
    void stopCapture();
    void openOutputDirectory();
    void changeOutputDirectory();
    void quitRequested();

private:
    friend class ::TrayControllerTest;

    struct PlatformOperations {
        std::function<bool(const QUrl&)> openUrl;
        std::function<QString(const QString& caption, const QString& initialDirectory)> chooseDirectory;
        std::function<void(const QString& title, const QString& body)> showMessage;
    };

    TrayController(SettingsStore& settingsStore, PlatformOperations operations,
        QObject* parent = nullptr);

    void openDirectory(const QString& directory);
    void chooseOutputDirectory();
    void showMessage(const QString& body);
    void updateMenu();

    SettingsStore& m_settingsStore;
    PlatformOperations m_operations;
    std::unique_ptr<QMenu> m_menu;
    std::unique_ptr<QSystemTrayIcon> m_trayIcon;
    QAction* m_statusAction = nullptr;
    QAction* m_progressAction = nullptr;
    QAction* m_startAction = nullptr;
    QAction* m_stopAction = nullptr;
    QAction* m_openDirectoryAction = nullptr;
    QAction* m_changeDirectoryAction = nullptr;
    QAction* m_quitAction = nullptr;
    QString m_notificationDirectory;
    std::optional<double> m_progress;
    bool m_captureActive = false;
};

} // namespace cimbarpunk
