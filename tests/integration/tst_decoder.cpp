// SPDX-License-Identifier: GPL-3.0-only
#include "decoder/CimbarDecoderAdapter.h"

#include "compression/zstd_decompressor.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QStringList>
#include <QtTest/QTest>

#include <algorithm>
#include <optional>
#include <sstream>

using cimbarpunk::CimbarDecoderAdapter;
using cimbarpunk::DecodedPayload;

namespace {

constexpr auto kSmallFrameChildArgument = "--decode-small-frame-child";

struct Fixture {
    QString sourceSha256;
    int mode = 0;
    QStringList orderedFrames;
    QStringList dropSafeFrames;
};

struct DecodeRun {
    std::optional<DecodedPayload> completed;
    int completions = 0;
    int progressUpdates = 0;
    bool recognized = false;
    bool progressValid = true;
    QString failedFrame;
};

QDir fixtureDirectory()
{
    return QDir(QString::fromUtf8(CIMBARPUNK_TEST_FIXTURE_DIR));
}

std::optional<Fixture> loadFixture()
{
    QFile manifestFile(fixtureDirectory().filePath(QStringLiteral("manifest.json")));
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(manifestFile.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    Fixture fixture;
    fixture.sourceSha256 = object.value(QStringLiteral("sourceSha256")).toString();
    fixture.mode = object.value(QStringLiteral("mode")).toInt();
    for (const QJsonValue& frame : object.value(QStringLiteral("orderedFrames")).toArray()) {
        fixture.orderedFrames.append(frame.toString());
    }
    for (const QJsonValue& frame : object.value(QStringLiteral("dropSafeFrames")).toArray()) {
        fixture.dropSafeFrames.append(frame.toString());
    }
    if (fixture.sourceSha256.isEmpty() || fixture.mode == 0 || fixture.orderedFrames.isEmpty()) {
        return std::nullopt;
    }
    return fixture;
}

DecodeRun decodeFrames(CimbarDecoderAdapter& adapter, const QStringList& frames, bool stopAtCompletion)
{
    DecodeRun run;
    const QDir frameDirectory(fixtureDirectory().filePath(QStringLiteral("mode68")));
    for (const QString& frameName : frames) {
        QImage image(frameDirectory.filePath(frameName));
        if (image.isNull()) {
            run.failedFrame = frameName;
            break;
        }
        image = image.convertToFormat(QImage::Format_RGB888);

        const cimbarpunk::DecodeUpdate update = adapter.decode(image);
        run.recognized = run.recognized || update.recognized;
        if (update.progress.has_value()
            && (*update.progress < 0.0 || *update.progress > 1.0)) {
            run.progressValid = false;
        }
        if (update.progress.has_value()) {
            ++run.progressUpdates;
        }
        if (update.completed.has_value()) {
            ++run.completions;
            run.completed = update.completed;
            if (stopAtCompletion) {
                break;
            }
        }
    }
    return run;
}

QStringList deterministicShuffle(QStringList frames)
{
    const QByteArray domain = QByteArrayLiteral("cimbarpunk-task7-shuffle-v1");
    std::sort(frames.begin(), frames.end(), [&](const QString& left, const QString& right) {
        const QByteArray leftKey = QCryptographicHash::hash(domain + left.toUtf8(), QCryptographicHash::Sha256);
        const QByteArray rightKey = QCryptographicHash::hash(domain + right.toUtf8(), QCryptographicHash::Sha256);
        if (leftKey == rightKey) {
            return left < right;
        }
        return leftKey < rightKey;
    });
    return frames;
}

std::optional<QByteArray> decompress(const QByteArray& compressed)
{
    cimbar::zstd_decompressor<std::stringstream> decompressor;
    if (!decompressor.write(compressed.constData(), static_cast<size_t>(compressed.size()))
        || !decompressor.good()) {
        return std::nullopt;
    }
    const std::string result = decompressor.str();
    return QByteArray(result.data(), static_cast<qsizetype>(result.size()));
}

QString sha256(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

std::optional<QByteArray> readSourceFixture()
{
    QFile sourceFile(fixtureDirectory().filePath(QStringLiteral("source.bin")));
    if (!sourceFile.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    return sourceFile.readAll();
}

} // namespace

class DecoderIntegrationTest final : public QObject {
    Q_OBJECT

private slots:
    void fixtureGeneratorDoesNotPublishManifestAfterFrameWriteFailure()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString fixturePath = temporaryDirectory.filePath(QStringLiteral("cimbar"));
        QVERIFY(QDir().mkpath(fixturePath));
        QFile staleManifest(QDir(fixturePath).filePath(QStringLiteral("manifest.json")));
        QVERIFY(staleManifest.open(QIODevice::WriteOnly));
        QCOMPARE(staleManifest.write(QByteArrayLiteral("stale")), qint64{5});
        staleManifest.close();

        QProcess generator;
        generator.start(QString::fromUtf8(CIMBARPUNK_FIXTURE_GENERATOR),
            {fixturePath, QStringLiteral("--fail-frame"), QStringLiteral("3")});
        QVERIFY(generator.waitForStarted());
        QVERIFY(generator.waitForFinished(30000));
        QCOMPARE(generator.exitStatus(), QProcess::NormalExit);
        QVERIFY2(generator.exitCode() != 0, generator.readAllStandardOutput().constData());
        QVERIFY(!QFile::exists(QDir(fixturePath).filePath(QStringLiteral("manifest.json"))));
    }

    void minimumSelectionSizedFrameReturnsWithoutEnteringScannerLoop()
    {
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(),
            {QString::fromLatin1(kSmallFrameChildArgument)});
        QVERIFY(child.waitForStarted());
        if (!child.waitForFinished(2000)) {
            child.kill();
            child.waitForFinished();
            QFAIL("32x32 RGB888 decode exceeded the bounded child-process deadline");
        }
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.exitCode(), 0);
    }

    void orderedFramesRecoverCommittedSource()
    {
        const auto fixture = loadFixture();
        QVERIFY(fixture.has_value());
        QCOMPARE(fixture->mode, 68);
        const auto sourceFixture = readSourceFixture();
        QVERIFY(sourceFixture.has_value());
        QCOMPARE(sourceFixture->size(), qsizetype{32768});
        QCOMPARE(sha256(*sourceFixture), fixture->sourceSha256);

        CimbarDecoderAdapter adapter;
        const DecodeRun run = decodeFrames(adapter, fixture->orderedFrames, true);

        QVERIFY2(run.failedFrame.isEmpty(), qPrintable(run.failedFrame));
        QVERIFY(run.recognized);
        QVERIFY(run.progressValid);
        QCOMPARE(run.completions, 1);
        QVERIFY(run.completed.has_value());
        QCOMPARE(run.completed->suggestedName, QStringLiteral("source.bin"));
        QVERIFY(!run.completed->fallbackName.isEmpty());
        const auto source = decompress(run.completed->compressedBytes);
        QVERIFY(source.has_value());
        QCOMPARE(sha256(*source), fixture->sourceSha256);
    }

    void shuffledFramesWithoutDropSafeFramesRecoverCommittedSource()
    {
        const auto fixture = loadFixture();
        QVERIFY(fixture.has_value());
        QVERIFY(fixture->dropSafeFrames.size() >= 2);

        QStringList frames = fixture->orderedFrames;
        QCOMPARE(frames.removeAll(fixture->dropSafeFrames.at(0)), 1);
        QCOMPARE(frames.removeAll(fixture->dropSafeFrames.at(1)), 1);
        const QStringList orderedWithoutDrops = frames;
        frames = deterministicShuffle(std::move(frames));
        QVERIFY(frames != orderedWithoutDrops);

        CimbarDecoderAdapter adapter;
        const DecodeRun run = decodeFrames(adapter, frames, true);

        QVERIFY2(run.failedFrame.isEmpty(), qPrintable(run.failedFrame));
        QVERIFY(run.recognized);
        QVERIFY(run.progressValid);
        QCOMPARE(run.completions, 1);
        QVERIFY(run.completed.has_value());
        const auto source = decompress(run.completed->compressedBytes);
        QVERIFY(source.has_value());
        QCOMPARE(sha256(*source), fixture->sourceSha256);
    }

    void duplicatesCannotCompleteTwiceAndResetStartsAnewDecode()
    {
        const auto fixture = loadFixture();
        QVERIFY(fixture.has_value());

        CimbarDecoderAdapter adapter;
        const QStringList duplicates(5, fixture->orderedFrames.constFirst());
        const DecodeRun duplicateRun = decodeFrames(adapter, duplicates, false);
        QCOMPARE(duplicateRun.completions, 0);
        QVERIFY(duplicateRun.progressValid);

        const DecodeRun completedRun = decodeFrames(adapter, fixture->orderedFrames, false);
        QCOMPARE(completedRun.completions, 1);
        QVERIFY(completedRun.completed.has_value());
        const auto completedSource = decompress(completedRun.completed->compressedBytes);
        QVERIFY(completedSource.has_value());
        QCOMPARE(sha256(*completedSource), fixture->sourceSha256);

        const DecodeRun ignoredRun = decodeFrames(adapter, fixture->orderedFrames, false);
        QCOMPARE(ignoredRun.completions, 0);
        QCOMPARE(ignoredRun.progressUpdates, 0);
        QVERIFY(!ignoredRun.recognized);

        adapter.reset();
        const DecodeRun resetRun = decodeFrames(adapter, fixture->orderedFrames, false);
        QCOMPARE(resetRun.completions, 1);
        QVERIFY(resetRun.completed.has_value());
        const auto source = decompress(resetRun.completed->compressedBytes);
        QVERIFY(source.has_value());
        QCOMPARE(sha256(*source), fixture->sourceSha256);
    }
};

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    if (application.arguments().size() == 2
        && application.arguments().at(1) == QString::fromLatin1(kSmallFrameChildArgument)) {
        QImage frame(32, 32, QImage::Format_RGB888);
        frame.fill(Qt::black);
        CimbarDecoderAdapter adapter;
        const cimbarpunk::DecodeUpdate update = adapter.decode(frame);
        return update.recognized || update.progress.has_value() || update.completed.has_value() ? 1 : 0;
    }

    DecoderIntegrationTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_decoder.moc"
