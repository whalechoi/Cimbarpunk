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
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <system_error>
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
    using ThreadEntry = std::function<void(std::stop_token)>;
    using ThreadLauncher = std::function<std::jthread(ThreadEntry)>;

    [[nodiscard]] static std::unique_ptr<DecodeWorker> createWithTestSeams(
        IDecoder& decoder, IOutputStore& outputStore, RotatingLogger& logger,
        ThreadLauncher threadLauncher, std::function<void()> beforeAdmission,
        std::function<void()> beforeJoin = {}) {
        return std::unique_ptr<DecodeWorker>(new DecodeWorker(decoder, outputStore, logger,
            std::move(threadLauncher), std::move(beforeAdmission), std::move(beforeJoin), nullptr));
    }

    [[nodiscard]] static bool waitUntilCancellationRequested(
        const DecodeWorker& worker, const std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!worker.m_cancelRequested.load(std::memory_order_acquire)) {
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

    [[nodiscard]] static quint64 mailboxDroppedCount(const DecodeWorker& worker) {
        return worker.m_mailbox.droppedCount();
    }

    [[nodiscard]] static bool isFullyStopped(const DecodeWorker& worker) {
        return worker.m_state.load(std::memory_order_acquire) == DecodeWorker::WorkerState::Stopped
            && worker.m_cancelRequested.load(std::memory_order_acquire)
            && !worker.m_thread.joinable();
    }

    [[nodiscard]] static QString outputDirectory(const DecodeWorker& worker) {
        return worker.m_outputDirectory;
    }

    [[nodiscard]] static std::optional<QImage> takeMailboxFrame(DecodeWorker& worker) {
        return worker.m_mailbox.take();
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

class BlockingGate final {
public:
    void enterAndWait() {
        std::unique_lock lock(m_mutex);
        m_entered = true;
        m_stateChanged.notify_all();
        m_stateChanged.wait(lock, [this] { return m_released; });
    }

    [[nodiscard]] bool waitUntilEntered(
        const std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
        std::unique_lock lock(m_mutex);
        return m_stateChanged.wait_for(lock, timeout, [this] { return m_entered; });
    }

    void release() {
        const std::scoped_lock lock(m_mutex);
        m_released = true;
        m_stateChanged.notify_all();
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_stateChanged;
    bool m_entered = false;
    bool m_released = false;
};

class AbortStopBeforeJoin final {
};

class PausableThreadLauncher final {
public:
    using ThreadEntry = std::function<void(std::stop_token)>;

    std::jthread launch(ThreadEntry entry) {
        int launchNumber = 0;
        {
            const std::scoped_lock lock(m_mutex);
            launchNumber = ++m_launchCount;
            if (m_released.size() <= static_cast<std::size_t>(launchNumber)) {
                m_released.resize(static_cast<std::size_t>(launchNumber + 1), false);
            }
            m_stateChanged.notify_all();
        }
        return std::jthread([this, launchNumber, entry = std::move(entry)](
                                const std::stop_token stopToken) mutable {
            std::unique_lock lock(m_mutex);
            m_stateChanged.wait(lock, [this, launchNumber] {
                return m_released.at(static_cast<std::size_t>(launchNumber));
            });
            lock.unlock();
            entry(stopToken);
        });
    }

    void release(const int launchNumber) {
        const std::scoped_lock lock(m_mutex);
        if (m_released.size() <= static_cast<std::size_t>(launchNumber)) {
            m_released.resize(static_cast<std::size_t>(launchNumber + 1), false);
        }
        m_released.at(static_cast<std::size_t>(launchNumber)) = true;
        m_stateChanged.notify_all();
    }

    void releaseAll() {
        const std::scoped_lock lock(m_mutex);
        std::fill(m_released.begin(), m_released.end(), true);
        m_stateChanged.notify_all();
    }

    [[nodiscard]] bool waitForLaunchCount(
        const int count, const std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
        std::unique_lock lock(m_mutex);
        return m_stateChanged.wait_for(lock, timeout, [this, count] { return m_launchCount >= count; });
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_stateChanged;
    std::vector<bool> m_released{false};
    int m_launchCount = 0;
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
        QVERIFY2(cimbarpunk::DecodeWorkerTestAccess::waitUntilCancellationRequested(worker),
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
        QVERIFY2(cimbarpunk::DecodeWorkerTestAccess::waitUntilCancellationRequested(worker),
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

    void stopBeforeDecoderEntryPreventsANewDecodeCall() {
        using namespace std::chrono_literals;

        FakeDecoder decoder;
        FakeOutputStore outputStore;
        QTemporaryDir outputDirectory;
        QVERIFY(outputDirectory.isValid());
        RotatingLogger logger(outputDirectory.path());
        QVERIFY(logger.install());
        DecodeWorker worker(decoder, outputStore, logger);
        BlockingGate beforeDecoder;
        QObject::connect(&worker, &DecodeWorker::frameAccepted, &worker,
            [&beforeDecoder] { beforeDecoder.enterAndWait(); }, Qt::DirectConnection);
        const auto releaseAndStop = qScopeGuard([&] {
            beforeDecoder.release();
            worker.stop();
        });

        QString error;
        QVERIFY2(worker.start(fullFrameSelection(), outputDirectory.path(), &error), qPrintable(error));
        worker.submitFrame(solidImage(Qt::red));
        QVERIFY2(beforeDecoder.waitUntilEntered(), "frameAccepted did not reach the decoder-entry gate");

        std::promise<void> stopReturnedPromise;
        std::future<void> stopReturned = stopReturnedPromise.get_future();
        std::jthread stopper([&] {
            worker.stop();
            stopReturnedPromise.set_value();
        });
        const auto releaseBlockedStopper = qScopeGuard([&beforeDecoder] { beforeDecoder.release(); });
        QVERIFY2(cimbarpunk::DecodeWorkerTestAccess::waitUntilCancellationRequested(worker),
            "stop() did not win before decoder entry");
        QCOMPARE(stopReturned.wait_for(0s), std::future_status::timeout);

        beforeDecoder.release();
        QVERIFY2(stopReturned.wait_for(2s) == std::future_status::ready,
            "stop() did not join after the decoder-entry gate opened");
        stopper.join();

        QCOMPARE(decoder.decodeCount(), std::size_t{0});
        QVERIFY(!cimbarpunk::DecodeWorkerTestAccess::hasJoinableThread(worker));
    }

    void directProgressHandlerCanRequestStopWithoutSelfJoining() {
        using namespace std::chrono_literals;

        DecodedPayload payload{
            .suggestedName = QStringLiteral("direct-stop.bin"),
            .fallbackName = QStringLiteral("direct-stop-stream"),
            .compressedBytes = QByteArrayLiteral("compressed")
        };
        FakeDecoder decoder({cimbarpunk::DecodeUpdate{.progress = 1.0, .completed = payload}});
        FakeOutputStore outputStore;
        QTemporaryDir outputDirectory;
        QVERIFY(outputDirectory.isValid());
        RotatingLogger logger(outputDirectory.path());
        QVERIFY(logger.install());
        BlockingGate handlerGate;
        BlockingGate externalJoinGate;
        std::atomic_bool abortExternalStop = false;
        std::unique_ptr<DecodeWorker> worker = cimbarpunk::DecodeWorkerTestAccess::createWithTestSeams(
            decoder, outputStore, logger,
            [](cimbarpunk::DecodeWorkerTestAccess::ThreadEntry entry) {
                return std::jthread(std::move(entry));
            },
            {},
            [&] {
                externalJoinGate.enterAndWait();
                if (abortExternalStop.exchange(false, std::memory_order_acq_rel)) {
                    throw AbortStopBeforeJoin{};
                }
            });
        WorkerSignalRecorder recordedSignals;
        std::promise<void> selfStopReturnedPromise;
        std::future<void> selfStopReturned = selfStopReturnedPromise.get_future();
        QObject::connect(worker.get(), &DecodeWorker::progressChanged, worker.get(),
            [&](const double) {
                handlerGate.enterAndWait();
                worker->stop();
                selfStopReturnedPromise.set_value();
            },
            Qt::DirectConnection);
        QObject::connect(worker.get(), &DecodeWorker::completed, worker.get(),
            [&recordedSignals](const OutputResult& result) { recordedSignals.recordCompleted(result); },
            Qt::DirectConnection);
        const auto releaseGatesAndStop = qScopeGuard([&] {
            handlerGate.release();
            abortExternalStop.store(true, std::memory_order_release);
            externalJoinGate.release();
            try {
                worker->stop();
            } catch (const AbortStopBeforeJoin&) {
                worker->stop();
            }
        });

        QString error;
        QVERIFY2(worker->start(fullFrameSelection(), outputDirectory.path(), &error), qPrintable(error));
        worker->submitFrame(solidImage(Qt::yellow));
        const bool handlerEntered = handlerGate.waitUntilEntered();

        std::promise<bool> externalStopReturnedPromise;
        std::future<bool> externalStopReturned = externalStopReturnedPromise.get_future();
        std::jthread externalStopper([&] {
            try {
                worker->stop();
                externalStopReturnedPromise.set_value(true);
            } catch (const AbortStopBeforeJoin&) {
                externalStopReturnedPromise.set_value(false);
            }
        });
        const bool externalJoinPhaseEntered = externalJoinGate.waitUntilEntered();
        const bool externalStopWasWaiting =
            externalStopReturned.wait_for(0s) == std::future_status::timeout;

        handlerGate.release();
        const bool selfStopReturnedBeforeAbort =
            selfStopReturned.wait_for(2s) == std::future_status::ready;
        abortExternalStop.store(!selfStopReturnedBeforeAbort, std::memory_order_release);
        externalJoinGate.release();
        const bool externalStopReturnedInTime =
            externalStopReturned.wait_for(2s) == std::future_status::ready;
        const bool externalStopCompletedNormally =
            externalStopReturnedInTime && externalStopReturned.get();
        const bool selfStopEventuallyReturned = selfStopReturnedBeforeAbort
            || selfStopReturned.wait_for(2s) == std::future_status::ready;
        externalStopper.join();
        worker->stop();

        QVERIFY2(handlerEntered, "the direct progress handler did not reach its gate");
        QVERIFY2(externalJoinPhaseEntered,
            "the external stopper did not acquire lifecycle ownership and reach the join phase");
        QVERIFY2(externalStopWasWaiting,
            "the external stopper returned instead of waiting for the gated worker");
        QVERIFY2(selfStopReturnedBeforeAbort,
            "worker-thread stop() blocked behind the external stopper's lifecycle lock");
        QVERIFY2(selfStopEventuallyReturned, "worker-thread stop() did not return within bounds");
        QVERIFY2(externalStopReturnedInTime, "external stop() did not return within bounds");
        QVERIFY2(externalStopCompletedNormally, "external stop() required test abort recovery");
        QCOMPARE(decoder.decodeCount(), std::size_t{1});
        QCOMPARE(decoder.resetCount(), 2);
        QCOMPARE(outputStore.commitCount(), 0);
        QCOMPARE(recordedSignals.completedCount(), 0);
        QVERIFY(!cimbarpunk::DecodeWorkerTestAccess::hasJoinableThread(*worker));
    }

    void staleSubmitCannotCrossAStopRestartSessionBoundary() {
        FakeDecoder decoder;
        FakeOutputStore outputStore;
        QTemporaryDir outputDirectory;
        QVERIFY(outputDirectory.isValid());
        RotatingLogger logger(outputDirectory.path());
        QVERIFY(logger.install());
        PausableThreadLauncher launcher;
        launcher.release(1);
        BlockingGate oldAdmission;
        std::unique_ptr<DecodeWorker> worker = cimbarpunk::DecodeWorkerTestAccess::createWithTestSeams(
            decoder, outputStore, logger,
            [&launcher](cimbarpunk::DecodeWorkerTestAccess::ThreadEntry entry) {
                return launcher.launch(std::move(entry));
            },
            [&oldAdmission] { oldAdmission.enterAndWait(); });
        const auto releaseAndStop = qScopeGuard([&] {
            oldAdmission.release();
            launcher.releaseAll();
            worker->stop();
        });

        QString error;
        QVERIFY2(worker->start(fullFrameSelection(), outputDirectory.path(), &error), qPrintable(error));
        std::jthread staleSubmitter([&] { worker->submitFrame(solidImage(Qt::red)); });
        const auto releaseStaleSubmitter = qScopeGuard([&oldAdmission] { oldAdmission.release(); });
        QVERIFY2(oldAdmission.waitUntilEntered(), "the old submit did not reach its admission gate");

        worker->stop();
        QVERIFY2(worker->start(fullFrameSelection(), outputDirectory.path(), &error), qPrintable(error));
        QVERIFY2(launcher.waitForLaunchCount(2), "the replacement worker was not launched");

        oldAdmission.release();
        staleSubmitter.join();
        worker->submitFrame(solidImage(Qt::blue));
        QCOMPARE(cimbarpunk::DecodeWorkerTestAccess::mailboxDroppedCount(*worker), quint64{0});

        launcher.release(2);
        QVERIFY2(decoder.waitForDecodeCount(1), "the new session did not decode its blue frame");
        worker->stop();

        QCOMPARE(decoder.seenColors(), std::vector<QColor>{QColor(Qt::blue)});
        QVERIFY(!cimbarpunk::DecodeWorkerTestAccess::hasJoinableThread(*worker));
    }

    void threadLaunchFailureRollsBackAndAllowsNextSession() {
        FakeDecoder decoder;
        FakeOutputStore outputStore;
        QTemporaryDir outputDirectory;
        QVERIFY(outputDirectory.isValid());
        RotatingLogger logger(outputDirectory.path());
        QVERIFY(logger.install());
        std::atomic_int launchAttempts = 0;
        std::unique_ptr<DecodeWorker> worker = cimbarpunk::DecodeWorkerTestAccess::createWithTestSeams(
            decoder, outputStore, logger,
            [&launchAttempts](cimbarpunk::DecodeWorkerTestAccess::ThreadEntry entry) -> std::jthread {
                if (launchAttempts.fetch_add(1, std::memory_order_acq_rel) == 0) {
                    throw std::system_error(
                        std::make_error_code(std::errc::resource_unavailable_try_again),
                        "controlled thread launch failure");
                }
                return std::jthread(std::move(entry));
            },
            {});

        QString error;
        QVERIFY(!worker->start(fullFrameSelection(), outputDirectory.path(), &error));

        QCOMPARE(error, QStringLiteral("无法启动解码线程"));
        QCOMPARE(outputStore.prepareCount(), 1);
        QCOMPARE(decoder.resetCount(), 2);
        QVERIFY(cimbarpunk::DecodeWorkerTestAccess::isFullyStopped(*worker));
        QVERIFY(cimbarpunk::DecodeWorkerTestAccess::outputDirectory(*worker).isEmpty());
        worker->submitFrame(solidImage(Qt::red));

        std::promise<std::optional<QImage>> mailboxResultPromise;
        std::future<std::optional<QImage>> mailboxResult = mailboxResultPromise.get_future();
        std::jthread mailboxTaker([&] {
            mailboxResultPromise.set_value(
                cimbarpunk::DecodeWorkerTestAccess::takeMailboxFrame(*worker));
        });
        const bool mailboxStopped = mailboxResult.wait_for(std::chrono::milliseconds(250))
            == std::future_status::ready;
        if (!mailboxStopped) {
            worker->stop();
        }
        mailboxTaker.join();
        const std::optional<QImage> rolledBackFrame = mailboxResult.get();

        worker->stop();
        QVERIFY2(mailboxStopped, "thread-launch rollback did not stop the mailbox");
        QVERIFY(!rolledBackFrame.has_value());
        QCOMPARE(decoder.decodeCount(), std::size_t{0});
        QCOMPARE(decoder.resetCount(), 2);

        QFile logFile(outputDirectory.filePath(QStringLiteral("cimbarpunk.log")));
        QVERIFY(logFile.open(QIODevice::ReadOnly));
        const QByteArray diagnostic = logFile.readAll();
        QVERIFY(diagnostic.contains("Decode worker thread launch failed"));
        QVERIFY(diagnostic.contains("controlled thread launch failure"));

        error.clear();
        QVERIFY2(worker->start(fullFrameSelection(), outputDirectory.path(), &error), qPrintable(error));
        QCOMPARE(outputStore.prepareCount(), 2);
        QCOMPARE(decoder.resetCount(), 3);
        worker->submitFrame(solidImage(Qt::blue));
        QVERIFY2(decoder.waitForDecodeCount(1),
            "the recovered worker did not process a frame after the failed launch");
        worker->stop();

        QCOMPARE(decoder.seenColors(), std::vector<QColor>{QColor(Qt::blue)});
        QCOMPARE(decoder.resetCount(), 4);
        QVERIFY(cimbarpunk::DecodeWorkerTestAccess::isFullyStopped(*worker));
        worker->stop();
        QCOMPARE(decoder.resetCount(), 4);
        QVERIFY(!cimbarpunk::DecodeWorkerTestAccess::hasJoinableThread(*worker));
    }
};

QTEST_GUILESS_MAIN(DecodeWorkerTest)
#include "tst_decode_worker.moc"
