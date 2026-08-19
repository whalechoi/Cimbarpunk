// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "pipeline/FramePipeline.h"
#include "pipeline/IFrameProcessor.h"
#include "pipeline/LatestFrameMailbox.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace cimbarpunk {

class IDecoder;
class IOutputStore;
class RotatingLogger;
class DecodeWorkerTestAccess;

// The injected decoder, output store, and logger must outlive this worker.
// Processing signals are emitted by the worker thread. A DirectConnection
// handler may request stop, but that self-stop only cancels/wakes and returns;
// a later call from another thread (normally the owner/destructor) performs
// the join and decoder reset. Never destroy the worker from its own signal.
class DecodeWorker final : public IFrameProcessor {
    Q_OBJECT

public:
    DecodeWorker(IDecoder& decoder, IOutputStore& outputStore, RotatingLogger& logger,
        QObject* parent = nullptr);
    ~DecodeWorker() override;

    DecodeWorker(const DecodeWorker&) = delete;
    DecodeWorker& operator=(const DecodeWorker&) = delete;

    bool start(const ScreenSelection& selection, const QString& outputDirectory, QString* error) override;
    void submitFrame(const QImage& fullScreenFrame) override;
    void stop() override;

private:
    friend class DecodeWorkerTestAccess;

    enum class WorkerState { Stopped, Accepting, Completing };
    using ThreadEntry = std::function<void(std::stop_token)>;
    using ThreadLauncher = std::function<std::jthread(ThreadEntry)>;
    using BeforeAdmission = std::function<void()>;
    using BeforeJoin = std::function<void()>;

    DecodeWorker(IDecoder& decoder, IOutputStore& outputStore, RotatingLogger& logger,
        ThreadLauncher threadLauncher, BeforeAdmission beforeAdmission, BeforeJoin beforeJoin,
        QObject* parent);

    void run(std::stop_token stopToken);

    IDecoder& m_decoder;
    IOutputStore& m_outputStore;
    RotatingLogger& m_logger;
    ThreadLauncher m_threadLauncher;
    BeforeAdmission m_beforeAdmission;
    BeforeJoin m_beforeJoin;
    FramePipeline m_pipeline;
    LatestFrameMailbox m_mailbox;
    QString m_outputDirectory;
    std::mutex m_lifecycleMutex;
    std::mutex m_admissionMutex;
    std::mutex m_decodeMutex;
    std::jthread m_thread;
    std::atomic<WorkerState> m_state = WorkerState::Stopped;
    std::atomic_bool m_cancelRequested = false;
    std::atomic<quint64> m_sessionGeneration = 0;
};

} // namespace cimbarpunk
