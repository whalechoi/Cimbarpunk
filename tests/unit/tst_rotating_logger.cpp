// SPDX-License-Identifier: GPL-3.0-only
#include "diagnostics/RotatingLogger.h"

#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <memory>

using cimbarpunk::RotatingLogger;

namespace {

QByteArray readBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QString readFile(const QString& path) {
    return QString::fromUtf8(readBytes(path));
}

class FailingWriteFile final : public QFile {
public:
    int writeAttempts = 0;

protected:
    qint64 writeData(const char*, qint64) override {
        ++writeAttempts;
        return -1;
    }
};

} // namespace

class RotatingLoggerTest final : public QObject {
    Q_OBJECT

private slots:
    void rotatesBeforeTheCurrentFileWouldExceedTheLimit() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        RotatingLogger logger(temporaryDirectory.path(), 128);
        QVERIFY(logger.install());

        logger.write(QString(100, QLatin1Char('A')));
        logger.write(QString(100, QLatin1Char('B')));
        logger.write(QString(100, QLatin1Char('C')));
        logger.uninstall();

        QVERIFY(QFile::exists(temporaryDirectory.filePath(QStringLiteral("cimbarpunk.log"))));
        QVERIFY(QFile::exists(temporaryDirectory.filePath(QStringLiteral("cimbarpunk.log.1"))));
        QVERIFY(QFile::exists(temporaryDirectory.filePath(QStringLiteral("cimbarpunk.log.2"))));
        QVERIFY(!QFile::exists(temporaryDirectory.filePath(QStringLiteral("cimbarpunk.log.4"))));
        QCOMPARE(readFile(temporaryDirectory.filePath(QStringLiteral("cimbarpunk.log"))), QString(100, QLatin1Char('C')) + QLatin1Char('\n'));
        QCOMPARE(readFile(temporaryDirectory.filePath(QStringLiteral("cimbarpunk.log.1"))), QString(100, QLatin1Char('B')) + QLatin1Char('\n'));
        QCOMPARE(readFile(temporaryDirectory.filePath(QStringLiteral("cimbarpunk.log.2"))), QString(100, QLatin1Char('A')) + QLatin1Char('\n'));
    }

    void boundsAnOversizedSingleMessage() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        RotatingLogger logger(temporaryDirectory.path(), 32);
        QVERIFY(logger.install());

        logger.write(QString(100, QLatin1Char('X')));
        logger.uninstall();

        QFile currentFile(temporaryDirectory.filePath(QStringLiteral("cimbarpunk.log")));
        QVERIFY(currentFile.open(QIODevice::ReadOnly));
        QVERIFY(currentFile.size() <= 32);
    }

    void measuresTheLimitUsingExactOnDiskBytes() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        RotatingLogger logger(temporaryDirectory.path(), 8);
        QVERIFY(logger.install());

        logger.write(QString(7, QLatin1Char('A')));
        logger.uninstall();

        QCOMPARE(readBytes(temporaryDirectory.filePath(QStringLiteral("cimbarpunk.log"))), QByteArray("AAAAAAA\n"));
    }

    void truncatesOversizedUtf8AtCodePointBoundaryWithTerminator() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        RotatingLogger logger(temporaryDirectory.path(), 7);
        QVERIFY(logger.install());

        logger.write(QStringLiteral("éééé"));
        logger.uninstall();

        QCOMPARE(readBytes(temporaryDirectory.filePath(QStringLiteral("cimbarpunk.log"))), QStringLiteral("ééé\n").toUtf8());
    }

    void disablesTheSinkAfterAWriteFailure() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        auto failingFile = std::make_unique<FailingWriteFile>();
        FailingWriteFile* const observedFile = failingFile.get();
        RotatingLogger logger(temporaryDirectory.path(), 1024, std::move(failingFile));
        QVERIFY(logger.install());

        logger.write(QStringLiteral("first"));
        logger.write(QStringLiteral("second"));

        QCOMPARE(observedFile->writeAttempts, 1);
    }

    void writesOnlyImageMetadataNeverPixelBytes() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        RotatingLogger logger(temporaryDirectory.path(), 1024);
        QVERIFY(logger.install());

        QImage image(7, 11, QImage::Format_RGBA8888);
        image.fill(0xdeadc0de);
        logger.writeImageDiagnostics(image);
        logger.uninstall();

        const QByteArray rawDiagnostics = readBytes(temporaryDirectory.filePath(QStringLiteral("cimbarpunk.log")));
        const QString diagnostics = QString::fromUtf8(rawDiagnostics);
        QVERIFY(diagnostics.contains(QStringLiteral("7x11")));
        QVERIFY(diagnostics.contains(QStringLiteral("RGBA8888")));
        const QByteArray pixelBytes(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes());
        QVERIFY(!rawDiagnostics.contains(pixelBytes));
    }
};

QTEST_GUILESS_MAIN(RotatingLoggerTest)
#include "tst_rotating_logger.moc"
