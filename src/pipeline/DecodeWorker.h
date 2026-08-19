// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "pipeline/FramePipeline.h"
#include "pipeline/IFrameProcessor.h"
#include "pipeline/LatestFrameMailbox.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace cimbarpunk {

class IDecoder;
class IOutputStore;
class RotatingLogger;
class DecodeWorkerTestAccess;

// The injected decoder, output store, and logger must outlive this worker.
// Processing signals are emitted by the worker thread; use the default queued
// delivery to thread-affine receivers and do not destroy the worker from a
// DirectConnection signal handler.
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

    void run(std::stop_token stopToken);

    IDecoder& m_decoder;
    IOutputStore& m_outputStore;
    RotatingLogger& m_logger;
    FramePipeline m_pipeline;
    LatestFrameMailbox m_mailbox;
    QString m_outputDirectory;
    std::mutex m_lifecycleMutex;
    std::jthread m_thread;
    std::atomic<WorkerState> m_state = WorkerState::Stopped;
};

} // namespace cimbarpunk
