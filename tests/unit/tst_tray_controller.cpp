// SPDX-License-Identifier: GPL-3.0-only
#include "settings/SettingsStore.h"
#include "tray/TrayController.h"

#include <QAction>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QSettings>
#include <QSignalSpy>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QUrl>
#include <QXmlStreamReader>
#include <QtTest/QTest>

#include <optional>

namespace {

QStringList visibleMenuEntries(const QMenu& menu) {
    QStringList entries;
    for (const QAction* action : menu.actions()) {
        if (!action->isVisible()) {
            continue;
        }
        entries.append(action->isSeparator() ? QStringLiteral("<separator>") : action->text());
    }
    return entries;
}

} // namespace

class TrayControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void idleMenuHasTheSpecifiedVisibleOrderWithoutShowingTheTray() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings settings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        cimbarpunk::SettingsStore store(settings);
        cimbarpunk::TrayController controller(store, inertOperations());

        QCOMPARE(visibleMenuEntries(*controller.m_menu),
            QStringList({QStringLiteral("状态：空闲"), QStringLiteral("开始捕获…"),
                QStringLiteral("打开保存目录"), QStringLiteral("更改保存目录…"),
                QStringLiteral("<separator>"), QStringLiteral("退出")}));
        QVERIFY(!controller.m_trayIcon->isVisible());
        QVERIFY(!controller.m_trayIcon->icon().isNull());
        QVERIFY(!controller.m_trayIcon->icon().pixmap(32, 32).isNull());

        QFile icon(QStringLiteral(":/cimbarpunk/icons/tray.svg"));
        QVERIFY(icon.open(QIODevice::ReadOnly));
        QXmlStreamReader xml(icon.readAll());
        QVERIFY(xml.readNextStartElement());
        QCOMPARE(xml.name(), QLatin1StringView("svg"));
        while (!xml.atEnd()) {
            xml.readNext();
        }
        QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
    }

    void captureStateDisablesStartAndDirectoryChangeAndShowsStop() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings settings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        cimbarpunk::SettingsStore store(settings);
        cimbarpunk::TrayController controller(store, inertOperations());

        controller.setCaptureActive(true);

        QCOMPARE(controller.m_statusAction->text(), QStringLiteral("状态：正在捕获"));
        QVERIFY(!controller.m_startAction->isEnabled());
        QVERIFY(!controller.m_changeDirectoryAction->isEnabled());
        QVERIFY(controller.m_stopAction->isVisible());
        QVERIFY(controller.m_stopAction->isEnabled());
        QVERIFY(!controller.m_progressAction->isVisible());

        controller.setProgress(0.42);
        QVERIFY(controller.m_progressAction->isVisible());
        QCOMPARE(controller.m_progressAction->text(), QStringLiteral("进度：42%"));

        controller.setProgress(std::nullopt);
        QVERIFY(!controller.m_progressAction->isVisible());

        controller.setCaptureActive(false);
        QVERIFY(controller.m_startAction->isEnabled());
        QVERIFY(controller.m_changeDirectoryAction->isEnabled());
        QVERIFY(!controller.m_stopAction->isVisible());
        QVERIFY(!controller.m_progressAction->isVisible());
    }

    void menuActionsEmitCommandsWithoutOpeningExternalUi() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings settings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        cimbarpunk::SettingsStore store(settings);
        const QString originalDirectory = directory.filePath(QStringLiteral("original"));
        const QString replacementDirectory = directory.filePath(QStringLiteral("replacement"));
        store.setOutputDirectory(originalDirectory);

        QList<QUrl> openedUrls;
        int chooseCalls = 0;
        cimbarpunk::TrayController::PlatformOperations operations{
            .openUrl = [&openedUrls](const QUrl& url) {
                openedUrls.append(url);
                return true;
            },
            .chooseDirectory = [&chooseCalls, &replacementDirectory](const QString&, const QString&) {
                ++chooseCalls;
                return replacementDirectory;
            },
            .showMessage = [](const QString&, const QString&) {},
        };
        cimbarpunk::TrayController controller(store, std::move(operations));
        QSignalSpy startSpy(&controller, &cimbarpunk::TrayController::startCapture);
        QSignalSpy stopSpy(&controller, &cimbarpunk::TrayController::stopCapture);
        QSignalSpy openSpy(&controller, &cimbarpunk::TrayController::openOutputDirectory);
        QSignalSpy changeSpy(&controller, &cimbarpunk::TrayController::changeOutputDirectory);
        QSignalSpy quitSpy(&controller, &cimbarpunk::TrayController::quitRequested);

        controller.m_startAction->trigger();
        controller.m_openDirectoryAction->trigger();
        controller.m_changeDirectoryAction->trigger();
        controller.m_quitAction->trigger();

        QCOMPARE(startSpy.size(), 1);
        QCOMPARE(openSpy.size(), 1);
        QCOMPARE(changeSpy.size(), 1);
        QCOMPARE(quitSpy.size(), 1);
        QCOMPARE(chooseCalls, 1);
        QCOMPARE(store.outputDirectory(), replacementDirectory);
        QCOMPARE(openedUrls, QList<QUrl>{QUrl::fromLocalFile(originalDirectory)});

        controller.setCaptureActive(true);
        controller.m_startAction->trigger();
        controller.m_changeDirectoryAction->trigger();
        controller.m_stopAction->trigger();

        QCOMPARE(startSpy.size(), 1);
        QCOMPARE(changeSpy.size(), 1);
        QCOMPARE(chooseCalls, 1);
        QCOMPARE(stopSpy.size(), 1);
    }

    void cancelledDirectoryChoiceLeavesThePersistedDirectoryUnchanged() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings settings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        cimbarpunk::SettingsStore store(settings);
        store.setOutputDirectory(directory.filePath(QStringLiteral("original")));
        cimbarpunk::TrayController::PlatformOperations operations = inertOperations();
        operations.chooseDirectory = [](const QString&, const QString&) { return QString(); };
        cimbarpunk::TrayController controller(store, std::move(operations));

        controller.m_changeDirectoryAction->trigger();

        QCOMPARE(store.outputDirectory(), directory.filePath(QStringLiteral("original")));
    }

    void savedFileNotificationUsesTheFilenameAndClickOpensItsContainingDirectory() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings settings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        cimbarpunk::SettingsStore store(settings);
        QString notificationTitle;
        QString notificationBody;
        QList<QUrl> openedUrls;
        cimbarpunk::TrayController::PlatformOperations operations{
            .openUrl = [&openedUrls](const QUrl& url) {
                openedUrls.append(url);
                return true;
            },
            .chooseDirectory = [](const QString&, const QString&) { return QString(); },
            .showMessage = [&notificationTitle, &notificationBody](const QString& title,
                               const QString& body) {
                notificationTitle = title;
                notificationBody = body;
            },
        };
        cimbarpunk::TrayController controller(store, std::move(operations));
        const QString savedPath = directory.filePath(QStringLiteral("nested/saved.bin"));

        controller.notifySavedFile(savedPath);

        QCOMPARE(notificationTitle, QStringLiteral("Cimbarpunk"));
        QCOMPARE(notificationBody, QStringLiteral("已保存：saved.bin"));
        QVERIFY(QMetaObject::invokeMethod(controller.m_trayIcon.get(), "messageClicked",
            Qt::DirectConnection));
        QCOMPARE(openedUrls,
            QList<QUrl>{QUrl::fromLocalFile(QFileInfo(savedPath).absolutePath())});
    }

private:
    static cimbarpunk::TrayController::PlatformOperations inertOperations() {
        return {
            .openUrl = [](const QUrl&) { return true; },
            .chooseDirectory = [](const QString&, const QString&) { return QString(); },
            .showMessage = [](const QString&, const QString&) {},
        };
    }
};

QTEST_MAIN(TrayControllerTest)
#include "tst_tray_controller.moc"
