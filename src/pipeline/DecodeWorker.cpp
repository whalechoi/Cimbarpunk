// SPDX-License-Identifier: GPL-3.0-only
#include "pipeline/DecodeWorker.h"

#include "decoder/IDecoder.h"
#include "diagnostics/RotatingLogger.h"
#include "output/IOutputStore.h"

#include <cmath>
#include <exception>

namespace cimbarpunk {

namespace {

thread_local DecodeWorker* activeDecodeWorker = nullptr;

class ActiveDecodeWorkerScope final {
public:
    explicit ActiveDecodeWorkerScope(DecodeWorker* worker)
        : m_previous(activeDecodeWorker) {
        activeDecodeWorker = worker;
    }

    ~ActiveDecodeWorkerScope() {
        activeDecodeWorker = m_previous;
    }

private:
    DecodeWorker* m_previous;
};

} // namespace

DecodeWorker::DecodeWorker(
    IDecoder& decoder, IOutputStore& outputStore, RotatingLogger& logger, QObject* parent)
    : DecodeWorker(decoder, outputStore, logger,
          [](ThreadEntry entry) { return std::jthread(std::move(entry)); }, {}, parent) {
}

DecodeWorker::DecodeWorker(IDecoder& decoder, IOutputStore& outputStore, RotatingLogger& logger,
    ThreadLauncher threadLauncher, BeforeAdmission beforeAdmission, QObject* parent)
    : IFrameProcessor(parent)
    , m_decoder(decoder)
    , m_outputStore(outputStore)
    , m_logger(logger)
    , m_threadLauncher(std::move(threadLauncher))
    , m_beforeAdmission(std::move(beforeAdmission)) {
}

DecodeWorker::~DecodeWorker() {
    stop();
}

bool DecodeWorker::start(
    const ScreenSelection& selection, const QString& outputDirectory, QString* error) {
    const std::scoped_lock lock(m_lifecycleMutex);
    if (m_state.load(std::memory_order_acquire) != WorkerState::Stopped) {
        return true;
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
    if (!m_outputStore.prepareDirectory(outputDirectory, error)) {
        return false;
    }

    m_pipeline.configure(selection);
    m_decoder.reset();
    m_mailbox.reset();
    m_outputDirectory = outputDirectory;
    m_cancelRequested.store(false, std::memory_order_release);
    {
        const std::scoped_lock admissionLock(m_admissionMutex);
        m_sessionGeneration.fetch_add(1, std::memory_order_acq_rel);
    }
    try {
        m_thread = m_threadLauncher(
            [this](const std::stop_token stopToken) { run(stopToken); });
    } catch (const std::exception& exception) {
        m_cancelRequested.store(true, std::memory_order_release);
        {
            const std::scoped_lock admissionLock(m_admissionMutex);
            m_sessionGeneration.fetch_add(1, std::memory_order_acq_rel);
            m_state.store(WorkerState::Stopped, std::memory_order_release);
            m_mailbox.stop();
        }
        m_decoder.reset();
        m_outputDirectory.clear();
        m_logger.write(QStringLiteral("Decode worker thread launch failed: %1")
                           .arg(QString::fromUtf8(exception.what())));
        if (error != nullptr) {
            *error = QStringLiteral("无法启动解码线程");
        }
        return false;
    } catch (...) {
        m_cancelRequested.store(true, std::memory_order_release);
        {
            const std::scoped_lock admissionLock(m_admissionMutex);
            m_sessionGeneration.fetch_add(1, std::memory_order_acq_rel);
            m_state.store(WorkerState::Stopped, std::memory_order_release);
            m_mailbox.stop();
        }
        m_decoder.reset();
        m_outputDirectory.clear();
        m_logger.write(QStringLiteral("Decode worker thread launch failed: unknown exception"));
        if (error != nullptr) {
            *error = QStringLiteral("无法启动解码线程");
        }
        return false;
    }
    {
        const std::scoped_lock admissionLock(m_admissionMutex);
        m_state.store(WorkerState::Accepting, std::memory_order_release);
    }
    return true;
}

void DecodeWorker::submitFrame(const QImage& fullScreenFrame) {
    const quint64 generation = m_sessionGeneration.load(std::memory_order_acquire);
    if (m_beforeAdmission) {
        m_beforeAdmission();
    }
    const std::scoped_lock admissionLock(m_admissionMutex);
    if (generation != m_sessionGeneration.load(std::memory_order_acquire)
        || m_cancelRequested.load(std::memory_order_acquire)
        || m_state.load(std::memory_order_acquire) != WorkerState::Accepting) {
        return;
    }
    m_mailbox.replace(fullScreenFrame);
}

void DecodeWorker::stop() {
    m_cancelRequested.store(true, std::memory_order_release);
    if (activeDecodeWorker == this) {
        const std::scoped_lock admissionLock(m_admissionMutex);
        m_sessionGeneration.fetch_add(1, std::memory_order_acq_rel);
        m_state.store(WorkerState::Stopped, std::memory_order_release);
        m_mailbox.stop();
        return;
    }

    const std::scoped_lock lock(m_lifecycleMutex);
    if (!m_thread.joinable()) {
        const std::scoped_lock admissionLock(m_admissionMutex);
        m_sessionGeneration.fetch_add(1, std::memory_order_acq_rel);
        m_state.store(WorkerState::Stopped, std::memory_order_release);
        m_mailbox.stop();
        return;
    }

    {
        const std::scoped_lock decodeLock(m_decodeMutex);
        const std::scoped_lock admissionLock(m_admissionMutex);
        m_sessionGeneration.fetch_add(1, std::memory_order_acq_rel);
        m_state.store(WorkerState::Stopped, std::memory_order_release);
        m_thread.request_stop();
        m_mailbox.stop();
    }
    m_thread.join();
    m_decoder.reset();
}

void DecodeWorker::run(const std::stop_token stopToken) {
    const ActiveDecodeWorkerScope activeScope(this);
    try {
        while (!stopToken.stop_requested()) {
            std::optional<QImage> frame = m_mailbox.take();
            if (!frame.has_value() || stopToken.stop_requested()) {
                return;
            }

            std::optional<QImage> prepared = m_pipeline.prepare(std::move(*frame));
            if (!prepared.has_value()) {
                continue;
            }

            emit frameAccepted();
            DecodeUpdate update;
            {
                const std::scoped_lock decodeLock(m_decodeMutex);
                if (m_cancelRequested.load(std::memory_order_acquire)
                    || m_state.load(std::memory_order_acquire) != WorkerState::Accepting
                    || stopToken.stop_requested()) {
                    return;
                }
                update = m_decoder.decode(*prepared);
            }
            if (m_cancelRequested.load(std::memory_order_acquire)) {
                return;
            }
            if (update.completed.has_value()) {
                WorkerState expected = WorkerState::Accepting;
                if (!m_state.compare_exchange_strong(expected, WorkerState::Completing,
                        std::memory_order_acq_rel, std::memory_order_acquire)) {
                    return;
                }
                m_mailbox.stop();
                if (update.progress.has_value() && std::isfinite(*update.progress)
                    && *update.progress >= 0.0 && *update.progress <= 1.0) {
                    emit progressChanged(*update.progress);
                }
                OutputResult result;
                {
                    const std::scoped_lock decodeLock(m_decodeMutex);
                    if (m_cancelRequested.load(std::memory_order_acquire)
                        || m_state.load(std::memory_order_acquire) != WorkerState::Completing
                        || stopToken.stop_requested()) {
                        return;
                    }
                    result = m_outputStore.commit(*update.completed, m_outputDirectory);
                }
                m_state.store(WorkerState::Stopped, std::memory_order_release);
                emit completed(result);
                return;
            }
            if (m_state.load(std::memory_order_acquire) != WorkerState::Accepting
                || stopToken.stop_requested()) {
                return;
            }
            if (update.progress.has_value() && std::isfinite(*update.progress)
                && *update.progress >= 0.0 && *update.progress <= 1.0) {
                emit progressChanged(*update.progress);
            }
        }
    } catch (const std::exception& exception) {
        if (m_cancelRequested.load(std::memory_order_acquire)) {
            return;
        }
        if (m_state.exchange(WorkerState::Stopped, std::memory_order_acq_rel)
            == WorkerState::Stopped) {
            return;
        }
        m_mailbox.stop();
        m_logger.write(QStringLiteral("Decode worker exception: %1")
                           .arg(QString::fromUtf8(exception.what())));
        emit failed(QStringLiteral("解码过程中发生错误"));
    } catch (...) {
        if (m_cancelRequested.load(std::memory_order_acquire)) {
            return;
        }
        if (m_state.exchange(WorkerState::Stopped, std::memory_order_acq_rel)
            == WorkerState::Stopped) {
            return;
        }
        m_mailbox.stop();
        m_logger.write(QStringLiteral("Decode worker exception: unknown exception"));
        emit failed(QStringLiteral("解码过程中发生错误"));
    }
}

} // namespace cimbarpunk
