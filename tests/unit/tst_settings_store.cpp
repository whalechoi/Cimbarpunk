// SPDX-License-Identifier: GPL-3.0-only
#include "settings/SettingsStore.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest/QTest>

using cimbarpunk::SettingsStore;

class SettingsStoreTest final : public QObject {
    Q_OBJECT

private slots:
    void returnsDownloadsCimbarpunkDirectoryByDefault() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        QSettings settings(temporaryDirectory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);

        SettingsStore store(settings);

        QCOMPARE(store.outputDirectory(),
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + QStringLiteral("/Cimbarpunk"));
    }

    void persistsOutputSelectionAndAbsolutePendingFile() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString settingsPath = temporaryDirectory.filePath(QStringLiteral("settings.ini"));
        const QString pendingFile = QDir(temporaryDirectory.path()).absoluteFilePath(QStringLiteral("write.part"));

        {
            QSettings settings(settingsPath, QSettings::IniFormat);
            SettingsStore store(settings);
            store.setOutputDirectory(QStringLiteral("D:/decoded"));
            store.saveSelection(QStringLiteral("display:primary"), QRectF(0.125, 0.25, 0.5, 0.625));
            store.registerTemporaryFile(pendingFile);
        }

        QSettings restoredSettings(settingsPath, QSettings::IniFormat);
        SettingsStore restored(restoredSettings);
        QCOMPARE(restored.outputDirectory(), QStringLiteral("D:/decoded"));
        QCOMPARE(restored.restoreSelection(QStringLiteral("display:primary")), std::optional<QRectF>{QRectF(0.125, 0.25, 0.5, 0.625)});
        QCOMPARE(restored.registeredTemporaryFiles(), QStringList{pendingFile});
    }

    void rejectsMalformedPersistedRectangleAndMismatchedScreen() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        QSettings settings(temporaryDirectory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        settings.setValue(QStringLiteral("selection/screenId"), QStringLiteral("display:primary"));
        settings.setValue(QStringLiteral("selection/normalizedRect"), QStringList{QStringLiteral("not"), QStringLiteral("a"), QStringLiteral("rectangle")});
        settings.sync();

        SettingsStore store(settings);
        QVERIFY(!store.restoreSelection(QStringLiteral("display:primary")).has_value());
        QVERIFY(!store.restoreSelection(QStringLiteral("display:secondary")).has_value());
    }

    void synchronizesTemporaryFileRegistrationAndRemoval() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString settingsPath = temporaryDirectory.filePath(QStringLiteral("settings.ini"));
        const QString pendingFile = QDir(temporaryDirectory.path()).absoluteFilePath(QStringLiteral("write.part"));
        QSettings settings(settingsPath, QSettings::IniFormat);
        SettingsStore store(settings);

        store.registerTemporaryFile(pendingFile);
        QSettings afterRegistration(settingsPath, QSettings::IniFormat);
        QCOMPARE(afterRegistration.value(QStringLiteral("output/pendingTemporaryFiles")).toStringList(), QStringList{pendingFile});

        store.unregisterTemporaryFile(pendingFile);
        QSettings afterRemoval(settingsPath, QSettings::IniFormat);
        QCOMPARE(afterRemoval.value(QStringLiteral("output/pendingTemporaryFiles")).toStringList(), QStringList{});
    }
};

QTEST_GUILESS_MAIN(SettingsStoreTest)
#include "tst_settings_store.moc"
