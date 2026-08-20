// SPDX-License-Identifier: GPL-3.0-only
#include "output/OutputStore.h"
#include "output/LibcimbarPayloadWriter.h"

#include "settings/SettingsStore.h"

#include "zstd/zstd.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <stdexcept>

using cimbarpunk::DecodedPayload;
using cimbarpunk::OutputStore;
using cimbarpunk::SettingsStore;
using cimbarpunk::makeLibcimbarPayloadWriter;
using cimbarpunk::sanitizeFilename;
using cimbarpunk::uniqueDestination;

namespace {

bool writeTextFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    return file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

DecodedPayload payload(QString suggestedName, QString fallbackName = QStringLiteral("42")) {
    return DecodedPayload{std::move(suggestedName), std::move(fallbackName), QByteArrayLiteral("compressed")};
}

QByteArray knownCompressedPayload() {
    return QByteArray::fromHex("28b52ffd60e8028d000050303132333435363738390100db5b1524");
}

QByteArray compressedPayloadFor(const QByteArray& bytes) {
    QByteArray compressed(static_cast<qsizetype>(ZSTD_compressBound(static_cast<size_t>(bytes.size()))), Qt::Uninitialized);
    const size_t compressedSize = ZSTD_compress(compressed.data(), static_cast<size_t>(compressed.size()),
        bytes.constData(), static_cast<size_t>(bytes.size()), 1);
    Q_ASSERT(!ZSTD_isError(compressedSize));
    compressed.truncate(static_cast<qsizetype>(compressedSize));
    return compressed;
}

} // namespace

class OutputStoreTest final : public QObject {
    Q_OBJECT

private slots:
    void sanitizesUntrustedSenderNames() {
        QCOMPARE(sanitizeFilename(QStringLiteral("../CON.txt")), QString());
        QCOMPARE(sanitizeFilename(QStringLiteral("CON.foo.bar")), QString());
        QCOMPARE(sanitizeFilename(QStringLiteral("LPT1 .txt")), QString());
        QCOMPARE(sanitizeFilename(QStringLiteral("a<b>:c?.txt")), QStringLiteral("a_b__c_.txt"));
        QCOMPARE(sanitizeFilename(QStringLiteral("  report.  ")), QStringLiteral("  report"));
        QCOMPARE(sanitizeFilename(QStringLiteral("résumé.txt")), QStringLiteral("résumé.txt"));
    }

    void usesFallbackForEmptyOrReservedSenderName() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        QSettings settings(temporaryDirectory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SettingsStore settingsStore(settings);
        const QString outputDirectory = temporaryDirectory.filePath(QStringLiteral("decoded"));
        settingsStore.setOutputDirectory(outputDirectory);

        const auto writer = [](const QString& path, QByteArrayView, QString*) { return writeTextFile(path, QByteArrayLiteral("decoded")); };
        OutputStore store(settingsStore, writer);

        const auto emptyResult = store.commit(payload(QString()), outputDirectory);
        QVERIFY(emptyResult.ok);
        QVERIFY(QFileInfo(emptyResult.finalPath).fileName().startsWith(QStringLiteral("cimbar-42-")));
        QVERIFY(emptyResult.finalPath.endsWith(QStringLiteral(".bin")));

        const auto reservedResult = store.commit(payload(QStringLiteral("../CON.txt")), outputDirectory);
        QVERIFY(reservedResult.ok);
        QVERIFY(QFileInfo(reservedResult.finalPath).fileName().startsWith(QStringLiteral("cimbar-42-")));
        QVERIFY(reservedResult.finalPath.endsWith(QStringLiteral(".bin")));
    }

    void numbersCollisionsBeforeFinalExtensionWithoutOverwriting() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString directory = temporaryDirectory.path();
        const QString report = QDir(directory).filePath(QStringLiteral("report.txt"));
        const QString reportOne = QDir(directory).filePath(QStringLiteral("report (1).txt"));
        const QString archive = QDir(directory).filePath(QStringLiteral("archive.tar.zst"));
        QVERIFY(writeTextFile(report, QByteArrayLiteral("original")));
        QVERIFY(writeTextFile(reportOne, QByteArrayLiteral("original-one")));
        QVERIFY(writeTextFile(archive, QByteArrayLiteral("archive-original")));

        QCOMPARE(uniqueDestination(directory, QStringLiteral("report.txt")),
            QDir(directory).filePath(QStringLiteral("report (2).txt")));
        QCOMPARE(uniqueDestination(directory, QStringLiteral("archive.tar.zst")),
            QDir(directory).filePath(QStringLiteral("archive.tar (1).zst")));

        QSettings settings(temporaryDirectory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SettingsStore settingsStore(settings);
        settingsStore.setOutputDirectory(directory);
        const auto writer = [](const QString& path, QByteArrayView, QString*) { return writeTextFile(path, QByteArrayLiteral("decoded")); };
        OutputStore store(settingsStore, writer);
        const auto result = store.commit(payload(QStringLiteral("report.txt")), directory);

        QVERIFY(result.ok);
        QCOMPARE(QFileInfo(result.finalPath).fileName(), QStringLiteral("report (2).txt"));
        QCOMPARE(readFile(report), QByteArrayLiteral("original"));
        QCOMPARE(readFile(reportOne), QByteArrayLiteral("original-one"));
    }

    void registersSameDirectoryTemporaryFileBeforeWritingAndUnregistersAfterCommit() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString outputDirectory = temporaryDirectory.filePath(QStringLiteral("decoded"));
        QSettings settings(temporaryDirectory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SettingsStore settingsStore(settings);
        settingsStore.setOutputDirectory(outputDirectory);
        QString observedTemporaryPath;

        const auto writer = [&settingsStore, &observedTemporaryPath](const QString& path, QByteArrayView, QString*) {
            observedTemporaryPath = path;
            const QStringList registered = settingsStore.registeredTemporaryFiles();
            if (!QFileInfo(path).isAbsolute() || !registered.contains(path)) {
                return false;
            }
            return writeTextFile(path, QByteArrayLiteral("decoded"));
        };
        OutputStore store(settingsStore, writer);

        const auto result = store.commit(payload(QStringLiteral("report.txt")), outputDirectory);

        QVERIFY(result.ok);
        QVERIFY(QRegularExpression(QStringLiteral(R"(^.+[\\/]\.cimbarpunk-[0-9a-f-]{36}\.part$)"), QRegularExpression::CaseInsensitiveOption)
                    .match(observedTemporaryPath)
                    .hasMatch());
        QCOMPARE(QFileInfo(observedTemporaryPath).absolutePath(), QDir(outputDirectory).absolutePath());
        QCOMPARE(settingsStore.registeredTemporaryFiles(), QStringList{});
        QVERIFY(!QFile::exists(observedTemporaryPath));
        QCOMPARE(readFile(result.finalPath), QByteArrayLiteral("decoded"));
    }

    void removesTemporaryFileAndRegistryEntryWhenWriterFails() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString outputDirectory = temporaryDirectory.filePath(QStringLiteral("decoded"));
        QSettings settings(temporaryDirectory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SettingsStore settingsStore(settings);
        settingsStore.setOutputDirectory(outputDirectory);
        QString temporaryPath;

        const auto writer = [&temporaryPath](const QString& path, QByteArrayView, QString* error) {
            temporaryPath = path;
            writeTextFile(path, QByteArrayLiteral("partial"));
            *error = QStringLiteral("decompression failed");
            return false;
        };
        OutputStore store(settingsStore, writer);

        const auto result = store.commit(payload(QStringLiteral("report.txt")), outputDirectory);

        QVERIFY(!result.ok);
        QCOMPARE(result.error, QStringLiteral("decompression failed"));
        QVERIFY(!QFile::exists(temporaryPath));
        QVERIFY(!QFile::exists(QDir(outputDirectory).filePath(QStringLiteral("report.txt"))));
        QCOMPARE(settingsStore.registeredTemporaryFiles(), QStringList{});
    }

    void removesTemporaryFileAndRegistryEntryWhenWriterThrows() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString outputDirectory = temporaryDirectory.filePath(QStringLiteral("decoded"));
        QSettings settings(temporaryDirectory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SettingsStore settingsStore(settings);
        settingsStore.setOutputDirectory(outputDirectory);
        QString temporaryPath;

        const auto writer = [&temporaryPath](const QString& path, QByteArrayView, QString*) -> bool {
            temporaryPath = path;
            writeTextFile(path, QByteArrayLiteral("partial"));
            throw std::runtime_error("decompression failed");
        };
        OutputStore store(settingsStore, writer);

        const auto result = store.commit(payload(QStringLiteral("report.txt")), outputDirectory);

        QVERIFY(!result.ok);
        QCOMPARE(result.error, QStringLiteral("decompression failed"));
        QVERIFY(!QFile::exists(temporaryPath));
        QVERIFY(!QFile::exists(QDir(outputDirectory).filePath(QStringLiteral("report.txt"))));
        QCOMPARE(settingsStore.registeredTemporaryFiles(), QStringList{});
    }

    void malformedCompressedPayloadDoesNotCommitFinalFile() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString outputDirectory = temporaryDirectory.filePath(QStringLiteral("decoded"));
        QSettings settings(temporaryDirectory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SettingsStore settingsStore(settings);
        settingsStore.setOutputDirectory(outputDirectory);
        OutputStore store(settingsStore, makeLibcimbarPayloadWriter());

        const auto result = store.commit(DecodedPayload{
            QStringLiteral("report.txt"), QStringLiteral("42"), QByteArrayLiteral("not a zstd frame")}, outputDirectory);

        QVERIFY(!result.ok);
        QVERIFY(!QFile::exists(QDir(outputDirectory).filePath(QStringLiteral("report.txt"))));
        QCOMPARE(settingsStore.registeredTemporaryFiles(), QStringList{});
    }

    void productionWriterCommitsKnownZstdPayload() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString outputDirectory = temporaryDirectory.filePath(QStringLiteral("decoded"));
        QSettings settings(temporaryDirectory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SettingsStore settingsStore(settings);
        settingsStore.setOutputDirectory(outputDirectory);
        OutputStore store(settingsStore, makeLibcimbarPayloadWriter());

        const auto result = store.commit(DecodedPayload{
            QStringLiteral("report.txt"), QStringLiteral("42"), knownCompressedPayload()}, outputDirectory);

        QByteArray expected;
        for (int index = 0; index < 100; ++index) {
            expected.append(QByteArrayLiteral("0123456789"));
        }
        QVERIFY(result.ok);
        QCOMPARE(readFile(result.finalPath), expected);
        QCOMPARE(settingsStore.registeredTemporaryFiles(), QStringList{});
    }

    void productionWriterAcceptsCompleteSkippablePaddingFrame() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString outputDirectory = temporaryDirectory.filePath(QStringLiteral("decoded"));
        QSettings settings(temporaryDirectory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SettingsStore settingsStore(settings);
        settingsStore.setOutputDirectory(outputDirectory);
        OutputStore store(settingsStore, makeLibcimbarPayloadWriter());
        QByteArray paddedPayload = knownCompressedPayload();
        paddedPayload.append(
            QByteArray::fromHex("502a4d180c000000000000000000000000000000"));

        const auto result = store.commit(DecodedPayload{
            QStringLiteral("report.txt"), QStringLiteral("42"), paddedPayload}, outputDirectory);

        QByteArray expected;
        for (int index = 0; index < 100; ++index) {
            expected.append(QByteArrayLiteral("0123456789"));
        }
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(readFile(result.finalPath), expected);
        QCOMPARE(settingsStore.registeredTemporaryFiles(), QStringList{});
    }

    void productionWriterPreservesBinaryLineFeeds() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString outputDirectory = temporaryDirectory.filePath(QStringLiteral("decoded"));
        QSettings settings(temporaryDirectory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SettingsStore settingsStore(settings);
        settingsStore.setOutputDirectory(outputDirectory);
        OutputStore store(settingsStore, makeLibcimbarPayloadWriter());
        const QByteArray expected = QByteArrayLiteral("first\\nsecond\\n");

        const auto result = store.commit(DecodedPayload{
            QStringLiteral("lines.bin"), QStringLiteral("42"), compressedPayloadFor(expected)}, outputDirectory);

        QVERIFY(result.ok);
        QCOMPARE(readFile(result.finalPath), expected);
    }

    void productionWriterRejectsTruncatedTrailingFrame() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString outputDirectory = temporaryDirectory.filePath(QStringLiteral("decoded"));
        QSettings settings(temporaryDirectory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SettingsStore settingsStore(settings);
        settingsStore.setOutputDirectory(outputDirectory);
        OutputStore store(settingsStore, makeLibcimbarPayloadWriter());
        QByteArray incompletePayload = knownCompressedPayload();
        incompletePayload.append(QByteArray::fromHex("28b52ffd"));

        const auto result = store.commit(DecodedPayload{
            QStringLiteral("report.txt"), QStringLiteral("42"), incompletePayload}, outputDirectory);

        QVERIFY(!result.ok);
        QVERIFY(!QFile::exists(QDir(outputDirectory).filePath(QStringLiteral("report.txt"))));
        QCOMPARE(settingsStore.registeredTemporaryFiles(), QStringList{});
    }

    void cleanupRemovesOnlyRegisteredSameDirectoryTemporaryFiles() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString outputDirectory = temporaryDirectory.filePath(QStringLiteral("decoded"));
        QVERIFY(QDir().mkpath(outputDirectory));
        const QString registered = QDir(outputDirectory).filePath(QStringLiteral(".cimbarpunk-11111111-1111-1111-1111-111111111111.part"));
        const QString unregistered = QDir(outputDirectory).filePath(QStringLiteral(".cimbarpunk-22222222-2222-2222-2222-222222222222.part"));
        const QString nonTemporary = QDir(outputDirectory).filePath(QStringLiteral("ordinary-file.txt"));
        const QString outside = temporaryDirectory.filePath(QStringLiteral(".cimbarpunk-33333333-3333-3333-3333-333333333333.part"));
        QVERIFY(writeTextFile(registered, QByteArrayLiteral("registered")));
        QVERIFY(writeTextFile(unregistered, QByteArrayLiteral("unregistered")));
        QVERIFY(writeTextFile(nonTemporary, QByteArrayLiteral("ordinary")));
        QVERIFY(writeTextFile(outside, QByteArrayLiteral("outside")));

        QSettings settings(temporaryDirectory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        SettingsStore settingsStore(settings);
        settingsStore.setOutputDirectory(outputDirectory);
        settingsStore.registerTemporaryFile(registered);
        settingsStore.registerTemporaryFile(nonTemporary);
        settingsStore.registerTemporaryFile(outside);
        OutputStore store(settingsStore, {});

        store.cleanupRegisteredTemporaryFiles();

        QVERIFY(!QFile::exists(registered));
        QVERIFY(QFile::exists(unregistered));
        QVERIFY(QFile::exists(nonTemporary));
        QVERIFY(QFile::exists(outside));
        QCOMPARE(settingsStore.registeredTemporaryFiles(), QStringList{});
    }
};

QTEST_GUILESS_MAIN(OutputStoreTest)
#include "tst_output_store.moc"
