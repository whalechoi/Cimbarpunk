// SPDX-License-Identifier: GPL-3.0-only
#include "diagnostics/RotatingLogger.h"
#include "fakes/FakeDecoder.h"
#include "output/IOutputStore.h"
#include "pipeline/DecodeWorker.h"

#include <QScopeGuard>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <future>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

using cimbarpunk::DecodeWorker;
using cimbarpunk::DecodedPayload;
using cimbarpunk::IOutputStore;
using cimbarpunk::OutputResult;
using cimbarpunk::RotatingLogger;
using cimbarpunk::ScreenSelection;
using cimbarpunk::test::FakeDecoder;

namespace cimbarpunk {

class DecodeWorkerTestAccess final {
public:
    [[nodiscard]] static bool waitUntilRejectingFrames(
        const DecodeWorker& worker, const std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (worker.m_state.load(std::memory_order_acquire) == DecodeWorker::WorkerState::Accepting) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::yield();
        }
        return true;
    }

    [[nodiscard]] static bool hasJoinableThread(const DecodeWorker& worker) {
        return worker.m_thread.joinable();
    }
};

} // namespace cimbarpunk

namespace {

class FakeOutputStore final : public IOutputStore {
public:
    bool prepareDirectory(const QString& directory, QString* error) override {
        const std::scoped_lock lock(m_mutex);
        ++m_prepareCount;
        m_preparedDirectory = directory;
        m_prepareThread = std::this_thread::get_id();
        if (!m_prepareSucceeds && error != nullptr) {
            *error = m_prepareError;
        }
        return m_prepareSucceeds;
    }

    OutputResult commit(const DecodedPayload& payload, const QString& directory) override {
        const std::scoped_lock lock(m_mutex);
        ++m_commitCount;
        m_committedPayload = payload;
        m_committedDirectory = directory;
        m_commitThread = std::this_thread::get_id();
        m_stateChanged.notify_all();
        return m_commitResult;
    }

    void cleanupRegisteredTemporaryFiles() override {
    }

    [[nodiscard]] bool waitForCommitCount(
        const int count, const std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
        std::unique_lock lock(m_mutex);
        return m_stateChanged.wait_for(lock, timeout, [this, count] { return m_commitCount >= count; });
    }

    [[nodiscard]] int commitCount() const {
        const std::scoped_lock lock(m_mutex);
        return m_commitCount;
    }

    [[nodiscard]] int prepareCount() const {
        const std::scoped_lock lock(m_mutex);
        return m_prepareCount;
    }

    [[nodiscard]] QString preparedDirectory() const {
        const std::scoped_lock lock(m_mutex);
        return m_preparedDirectory;
    }

    [[nodiscard]] std::thread::id prepareThread() const {
        const std::scoped_lock lock(m_mutex);
        return m_prepareThread;
    }

    [[nodiscard]] QString committedDirectory() const {
        const std::scoped_lock lock(m_mutex);
        return m_committedDirectory;
    }

    [[nodiscard]] std::thread::id commitThread() const {
        const std::scoped_lock lock(m_mutex);
        return m_commitThread;
    }

    bool m_prepareSucceeds = true;
    QString m_prepareError;
    OutputResult m_commitResult{.ok = true, .finalPath = QStringLiteral("committed.bin")};

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_stateChanged;
    DecodedPayload m_committedPayload;
    QString m_preparedDirectory;
    QString m_committedDirectory;
    std::thread::id m_commitThread;
    std::thread::id m_prepareThread;
    int m_prepareCount = 0;
    int m_commitCount = 0;
};

class WorkerSignalRecorder final {
public:
    void recordFrameAccepted() {
        const std::scoped_lock lock(m_mutex);
        ++m_frameAcceptedCount;
    }

    void recordProgress(const double progress) {
        const std::scoped_lock lock(m_mutex);
        m_progress.push_back(progress);
    }

    void recordCompleted(const OutputResult& result) {
        const std::scoped_lock lock(m_mutex);
        ++m_completedCount;
        m_completedResult = result;
        m_completedThread = std::this_thread::get_id();
        m_stateChanged.notify_all();
    }

    void recordFailed(const QString& message) {
        const std::scoped_lock lock(m_mutex);
        ++m_failedCount;
        m_failureMessage = message;
        m_stateChanged.notify_all();
    }

    [[nodiscard]] bool waitForCompleted(
        const int count, const std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
        std::unique_lock lock(m_mutex);
        return m_stateChanged.wait_for(lock, timeout, [this, count] { return m_completedCount >= count; });
    }

    [[nodiscard]] bool waitForFailed(
        const int count, const std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
        std::unique_lock lock(m_mutex);
        return m_stateChanged.wait_for(lock, timeout, [this, count] { return m_failedCount >= count; });
    }

    [[nodiscard]] int completedCount() const {
        const std::scoped_lock lock(m_mutex);
        return m_completedCount;
    }

    [[nodiscard]] int frameAcceptedCount() const {
        const std::scoped_lock lock(m_mutex);
        return m_frameAcceptedCount;
    }

    [[nodiscard]] std::vector<double> progress() const {
        const std::scoped_lock lock(m_mutex);
        return m_progress;
    }

    [[nodiscard]] OutputResult completedResult() const {
        const std::scoped_lock lock(m_mutex);
        return m_completedResult;
    }

    [[nodiscard]] std::thread::id completedThread() const {
        const std::scoped_lock lock(m_mutex);
        return m_completedThread;
    }

    [[nodiscard]] int failedCount() const {
        const std::scoped_lock lock(m_mutex);
        return m_failedCount;
    }

    [[nodiscard]] QString failureMessage() const {
        const std::scoped_lock lock(m_mutex);
        return m_failureMessage;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_stateChanged;
    std::vector<double> m_progress;
    OutputResult m_completedResult;
    QString m_failureMessage;
    std::thread::id m_completedThread;
    int m_frameAcceptedCount = 0;
    int m_completedCount = 0;
    int m_failedCount = 0;
};

QImage solidImage(const QColor& color) {
    QImage image(32, 32, QImage::Format_RGB32);
    image.fill(color);
    return image;
}

ScreenSelection fullFrameSelection() {
    return {QStringLiteral("screen"), QRectF(0, 0, 32, 32), QRectF(0, 0, 32, 32)};
}

} // namespace

class DecodeWorkerTest final : public QObject {
    Q_OBJECT

private slots:
    void keepsOnlyTheLatestFrameWhileDecodeIsBusy() {
        FakeDecoder decoder;
        decoder.blockFirstDecode();
        FakeOutputStore outputStore;
        QTemporaryDir logDirectory;
        QVERIFY(logDirectory.isValid());
        RotatingLogger logger(logDirectory.path());
        QVERIFY(logger.install());
        DecodeWorker worker(decoder, outputStore, logger);
        const auto releaseDecoder = qScopeGuard([&decoder] { decoder.releaseFirstDecode(); });

        QString error;
        QVERIFY2(worker.start(fullFrameSelection(), logDirectory.path(), &error), qPrintable(error));
        worker.submitFrame(solidImage(Qt::red));
        QVERIFY2(decoder.waitForDecodeCount(1), "the first frame never reached decode");

        worker.submitFrame(solidImage(Qt::green));
        worker.submitFrame(solidImage(Qt::blue));
        decoder.releaseFirstDecode();
        QVERIFY2(decoder.waitForDecodeCount(2), "the replacement frame never reached decode");
        worker.stop();

        const std::vector<QColor> seen = decoder.seenColors();
        QCOMPARE(seen.size(), std::size_t{2});
        QCOMPARE(seen.at(0), QColor(Qt::red));
        QCOMPARE(seen.at(1), QColor(Qt::blue));
        QVERIFY(std::find(seen.cbegin(), seen.cend(), QColor(Qt::green)) == seen.cend());
    }

    void commitsAndEmitsOnlyTheFirstCompletedPayloadOnTheWorkerThread() {
        DecodedPayload payload{
            .suggestedName = QStringLiteral("payload.bin"),
            .fallbackName = QStringLiteral("stream-1"),
            .compressedBytes = QByteArrayLiteral("compressed")
        };
        FakeDecoder decoder({cimbarpunk::DecodeUpdate{
            .recognized = true,
            .progress = 0.5,
            .completed = payload,
        }});
        FakeOutputStore outputStore;
        QTemporaryDir outputDirectory;
        QVERIFY(outputDirectory.isValid());
        RotatingLogger logger(outputDirectory.path());
        QVERIFY(logger.install());
        DecodeWorker worker(decoder, outputStore, logger);
        WorkerSignalRecorder recordedSignals;
        QObject::connect(&worker, &DecodeWorker::frameAccepted, &worker,
            [&recordedSignals] { recordedSignals.recordFrameAccepted(); }, Qt::DirectConnection);
        QObject::connect(&worker, &DecodeWorker::progressChanged, &worker,
            [&recordedSignals](const double value) { recordedSignals.recordProgress(value); }, Qt::DirectConnection);
        QObject::connect(&worker, &DecodeWorker::completed, &worker,
            [&recordedSignals](const OutputResult& result) { recordedSignals.recordCompleted(result); }, Qt::DirectConnection);
        const auto stopWorker = qScopeGuard([&worker] { worker.stop(); });

        QString error;
        QVERIFY2(worker.start(fullFrameSelection(), outputDirectory.path(), &error), qPrintable(error));
        const std::thread::id callerThread = std::this_thread::get_id();
        worker.submitFrame(solidImage(Qt::red));
        QVERIFY2(outputStore.waitForCommitCount(1), "the completed payload was not committed");
        QVERIFY2(recordedSignals.waitForCompleted(1), "completion was not emitted");

        worker.submitFrame(solidImage(Qt::blue));
        worker.stop();

        QCOMPARE(decoder.decodeCount(), std::size_t{1});
        QCOMPARE(outputStore.commitCount(), 1);
        QCOMPARE(recordedSignals.completedCount(), 1);
        QCOMPARE(recordedSignals.frameAcceptedCount(), 1);
        QCOMPARE(recordedSignals.progress(), std::vector<double>{0.5});
        QVERIFY(recordedSignals.completedResult().ok);
        QCOMPARE(recordedSignals.completedResult().finalPath, QStringLiteral("committed.bin"));
        QCOMPARE(outputStore.committedDirectory(), outputDirectory.path());
        QCOMPARE(recordedSignals.completedThread(), outputStore.commitThread());
        QVERIFY(recordedSignals.completedThread() != callerThread);
    }

    void convertsDecoderExceptionsToAStableErrorAndDetailedDiagnostic() {
        FakeDecoder decoder;
        decoder.setFailure(FakeDecoder::Failure::StandardException);
        FakeOutputStore outputStore;
        QTemporaryDir outputDirectory;
        QVERIFY(outputDirectory.isValid());
        RotatingLogger logger(outputDirectory.path());
        QVERIFY(logger.install());
        DecodeWorker worker(decoder, outputStore, logger);
        WorkerSignalRecorder recordedSignals;
        QObject::connect(&worker, &DecodeWorker::failed, &worker,
            [&recordedSignals](const QString& message) { recordedSignals.recordFailed(message); },
            Qt::DirectConnection);
        const auto stopWorker = qScopeGuard([&worker] { worker.stop(); });

        QString error;
        QVERIFY2(worker.start(fullFrameSelection(), outputDirectory.path(), &error), qPrintable(error));
        worker.submitFrame(solidImage(Qt::yellow));
        QVERIFY2(recordedSignals.waitForFailed(1), "the decoder exception did not become a failure signal");
        worker.stop();

        QCOMPARE(recordedSignals.failedCount(), 1);
        QCOMPARE(recordedSignals.failureMessage(), QStringLiteral("解码过程中发生错误"));
        QCOMPARE(outputStore.commitCount(), 0);
        QFile logFile(outputDirectory.filePath(QStringLiteral("cimbarpunk.log")));
        QVERIFY(logFile.open(QIODevice::ReadOnly));
        const QByteArray diagnostic = logFile.readAll();
        QVERIFY(diagnostic.contains("Decode worker exception"));
        QVERIFY(diagnostic.contains("decoder exploded"));
    }

    void convertsUnknownExceptionsToTheSameStableError() {
        FakeDecoder decoder;
        decoder.setFailure(FakeDecoder::Failure::UnknownException);
        FakeOutputStore outputStore;
        QTemporaryDir outputDirectory;
        QVERIFY(outputDirectory.isValid());
        RotatingLogger logger(outputDirectory.path());
        QVERIFY(logger.install());
        DecodeWorker worker(decoder, outputStore, logger);
        WorkerSignalRecorder recordedSignals;
        QObject::connect(&worker, &DecodeWorker::failed, &worker,
            [&recordedSignals](const QString& message) { recordedSignals.recordFailed(message); },
            Qt::DirectConnection);
        const auto stopWorker = qScopeGuard([&worker] { worker.stop(); });

        QString error;
        QVERIFY2(worker.start(fullFrameSelection(), outputDirectory.path(), &error), qPrintable(error));
        worker.submitFrame(solidImage(Qt::cyan));
        QVERIFY2(recordedSignals.waitForFailed(1), "the unknown exception did not become a failure signal");
        worker.stop();

        QCOMPARE(recordedSignals.failedCount(), 1);
        QCOMPARE(recordedSignals.failureMessage(), QStringLiteral("解码过程中发生错误"));
        QFile logFile(outputDirectory.filePath(QStringLiteral("cimbarpunk.log")));
        QVERIFY(logFile.open(QIODevice::ReadOnly));
        QVERIFY(logFile.readAll().contains("unknown exception"));
    }

    void stopWaitsForInFlightDecodeAndCanBeRepeated() {
        using namespace std::chrono_literals;

        FakeDecoder decoder;
        decoder.blockFirstDecode();
        FakeOutputStore outputStore;
        QTemporaryDir outputDirectory;
        QVERIFY(outputDirectory.isValid());
        RotatingLogger logger(outputDirectory.path());
        QVERIFY(logger.install());
        DecodeWorker worker(decoder, outputStore, logger);
        const auto releaseAndStop = qScopeGuard([&] {
            decoder.releaseFirstDecode();
            worker.stop();
        });

        QString error;
        QVERIFY2(worker.start(fullFrameSelection(), outputDirectory.path(), &error), qPrintable(error));
        worker.submitFrame(solidImage(Qt::red));
        QVERIFY2(decoder.waitForDecodeCount(1), "the blocking decode call was not entered");

        std::promise<void> stopEnteredPromise;
        std::future<void> stopEntered = stopEnteredPromise.get_future();
        std::promise<void> stopReturnedPromise;
        std::future<void> stopReturned = stopReturnedPromise.get_future();
        std::jthread stopper([&] {
            stopEnteredPromise.set_value();
            worker.stop();
            stopReturnedPromise.set_value();
        });
        const auto releaseBlockedStopper = qScopeGuard([&decoder] { decoder.releaseFirstDecode(); });
        QVERIFY(stopEntered.wait_for(2s) == std::future_status::ready);
        QVERIFY2(cimbarpunk::DecodeWorkerTestAccess::waitUntilRejectingFrames(worker),
            "stop() did not close the frame entrance");
        QCOMPARE(stopReturned.wait_for(100ms), std::future_status::timeout);

        decoder.releaseFirstDecode();
        QVERIFY2(stopReturned.wait_for(2s) == std::future_status::ready,
            "stop() did not return after the in-flight decode was released");
        stopper.join();

        worker.stop();
        QVERIFY2(worker.start(fullFrameSelection(), outputDirectory.path(), &error), qPrintable(error));
        worker.submitFrame(solidImage(Qt::blue));
        QVERIFY2(decoder.waitForDecodeCount(2), "a new worker session did not accept its frame");
        worker.stop();

        QCOMPARE(decoder.decodeCount(), std::size_t{2});
        QCOMPARE(decoder.seenColors(), std::vector<QColor>({QColor(Qt::red), QColor(Qt::blue)}));
        QCOMPARE(decoder.resetCount(), 4);
        QVERIFY(!cimbarpunk::DecodeWorkerTestAccess::hasJoinableThread(worker));
    }

    void startPreparesSynchronouslyAndRepeatedStartIsANoop() {
        FakeDecoder decoder;
        FakeOutputStore outputStore;
        QTemporaryDir firstDirectory;
        QTemporaryDir secondDirectory;
        QVERIFY(firstDirectory.isValid());
        QVERIFY(secondDirectory.isValid());
        RotatingLogger logger(firstDirectory.path());
        QVERIFY(logger.install());
        DecodeWorker worker(decoder, outputStore, logger);
        const auto stopWorker = qScopeGuard([&worker] { worker.stop(); });
        const std::thread::id callerThread = std::this_thread::get_id();

        QString error;
        QVERIFY2(worker.start(fullFrameSelection(), firstDirectory.path(), &error), qPrintable(error));
        QVERIFY2(worker.start(fullFrameSelection(), secondDirectory.path(), &error), qPrintable(error));

        QCOMPARE(outputStore.prepareCount(), 1);
        QCOMPARE(outputStore.preparedDirectory(), firstDirectory.path());
        QCOMPARE(outputStore.prepareThread(), callerThread);
        QCOMPARE(decoder.resetCount(), 1);
        worker.stop();
        QCOMPARE(decoder.resetCount(), 2);
    }

    void failedPreparationDoesNotStartOrResetTheDecoder() {
        FakeDecoder decoder;
        FakeOutputStore outputStore;
        outputStore.m_prepareSucceeds = false;
        outputStore.m_prepareError = QStringLiteral("directory unavailable");
        QTemporaryDir logDirectory;
        QVERIFY(logDirectory.isValid());
        RotatingLogger logger(logDirectory.path());
        QVERIFY(logger.install());
        DecodeWorker worker(decoder, outputStore, logger);

        QString error;
        QVERIFY(!worker.start(fullFrameSelection(), QStringLiteral("unavailable"), &error));
        QCOMPARE(error, QStringLiteral("directory unavailable"));
        QCOMPARE(outputStore.prepareCount(), 1);
        QCOMPARE(decoder.resetCount(), 0);
        worker.submitFrame(solidImage(Qt::red));
        worker.stop();
        QCOMPARE(decoder.decodeCount(), std::size_t{0});
    }

    void emitsOnlyFiniteProgressInsideTheUnitInterval() {
        FakeDecoder decoder({
            cimbarpunk::DecodeUpdate{.progress = std::numeric_limits<double>::quiet_NaN()},
            cimbarpunk::DecodeUpdate{.progress = -0.01},
            cimbarpunk::DecodeUpdate{.progress = 1.01},
            cimbarpunk::DecodeUpdate{.progress = 0.25},
        });
        FakeOutputStore outputStore;
        QTemporaryDir outputDirectory;
        QVERIFY(outputDirectory.isValid());
        RotatingLogger logger(outputDirectory.path());
        QVERIFY(logger.install());
        DecodeWorker worker(decoder, outputStore, logger);
        WorkerSignalRecorder recordedSignals;
        QObject::connect(&worker, &DecodeWorker::progressChanged, &worker,
            [&recordedSignals](const double value) { recordedSignals.recordProgress(value); },
            Qt::DirectConnection);
        const auto stopWorker = qScopeGuard([&worker] { worker.stop(); });

        QString error;
        QVERIFY2(worker.start(fullFrameSelection(), outputDirectory.path(), &error), qPrintable(error));
        const QColor colors[]{Qt::red, Qt::green, Qt::blue, Qt::yellow};
        for (std::size_t index = 0; index < std::size(colors); ++index) {
            worker.submitFrame(solidImage(colors[index]));
            QVERIFY2(decoder.waitForDecodeCount(index + 1), "a progress frame was not decoded");
        }
        worker.stop();

        QCOMPARE(recordedSignals.progress(), std::vector<double>{0.25});
    }

    void stopDuringDecodeDiscardsACoincidentCompletion() {
        using namespace std::chrono_literals;

        DecodedPayload payload{
            .suggestedName = QStringLiteral("cancelled.bin"),
            .fallbackName = QStringLiteral("cancelled-stream"),
            .compressedBytes = QByteArrayLiteral("compressed")
        };
        FakeDecoder decoder({cimbarpunk::DecodeUpdate{.progress = 1.0, .completed = payload}});
        decoder.blockFirstDecode();
        FakeOutputStore outputStore;
        QTemporaryDir outputDirectory;
        QVERIFY(outputDirectory.isValid());
        RotatingLogger logger(outputDirectory.path());
        QVERIFY(logger.install());
        DecodeWorker worker(decoder, outputStore, logger);
        WorkerSignalRecorder recordedSignals;
        QObject::connect(&worker, &DecodeWorker::completed, &worker,
            [&recordedSignals](const OutputResult& result) { recordedSignals.recordCompleted(result); },
            Qt::DirectConnection);
        const auto releaseAndStop = qScopeGuard([&] {
            decoder.releaseFirstDecode();
            worker.stop();
        });

        QString error;
        QVERIFY2(worker.start(fullFrameSelection(), outputDirectory.path(), &error), qPrintable(error));
        worker.submitFrame(solidImage(Qt::magenta));
        QVERIFY2(decoder.waitForDecodeCount(1), "the completing decode call was not entered");

        std::promise<void> stopReturnedPromise;
        std::future<void> stopReturned = stopReturnedPromise.get_future();
        std::jthread stopper([&] {
            worker.stop();
            stopReturnedPromise.set_value();
        });
        const auto releaseBlockedStopper = qScopeGuard([&decoder] { decoder.releaseFirstDecode(); });
        QVERIFY2(cimbarpunk::DecodeWorkerTestAccess::waitUntilRejectingFrames(worker),
            "stop() did not close the frame entrance");
        QCOMPARE(stopReturned.wait_for(0s), std::future_status::timeout);

        decoder.releaseFirstDecode();
        QVERIFY2(stopReturned.wait_for(2s) == std::future_status::ready,
            "stop() did not join after decode returned");
        stopper.join();

        QCOMPARE(outputStore.commitCount(), 0);
        QCOMPARE(recordedSignals.completedCount(), 0);
        QVERIFY(!cimbarpunk::DecodeWorkerTestAccess::hasJoinableThread(worker));
    }
};

QTEST_GUILESS_MAIN(DecodeWorkerTest)
#include "tst_decode_worker.moc"
