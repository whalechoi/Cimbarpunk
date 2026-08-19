// SPDX-License-Identifier: GPL-3.0-only
#include "pipeline/DecodeWorker.h"

#include "decoder/IDecoder.h"
#include "diagnostics/RotatingLogger.h"
#include "output/IOutputStore.h"

#include <cmath>
#include <exception>

namespace cimbarpunk {

DecodeWorker::DecodeWorker(
    IDecoder& decoder, IOutputStore& outputStore, RotatingLogger& logger, QObject* parent)
    : IFrameProcessor(parent)
    , m_decoder(decoder)
    , m_outputStore(outputStore)
    , m_logger(logger) {
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
    m_state.store(WorkerState::Accepting, std::memory_order_release);
    m_thread = std::jthread([this](const std::stop_token stopToken) { run(stopToken); });
    return true;
}

void DecodeWorker::submitFrame(const QImage& fullScreenFrame) {
    if (m_state.load(std::memory_order_acquire) != WorkerState::Accepting) {
        return;
    }
    m_mailbox.replace(fullScreenFrame);
}

void DecodeWorker::stop() {
    const std::scoped_lock lock(m_lifecycleMutex);
    m_state.store(WorkerState::Stopped, std::memory_order_release);
    if (!m_thread.joinable()) {
        return;
    }

    m_thread.request_stop();
    m_mailbox.stop();
    m_thread.join();
    m_decoder.reset();
}

void DecodeWorker::run(const std::stop_token stopToken) {
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
            DecodeUpdate update = m_decoder.decode(*prepared);
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
                const OutputResult result = m_outputStore.commit(*update.completed, m_outputDirectory);
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
        if (m_state.exchange(WorkerState::Stopped, std::memory_order_acq_rel)
            == WorkerState::Stopped) {
            return;
        }
        m_mailbox.stop();
        m_logger.write(QStringLiteral("Decode worker exception: %1")
                           .arg(QString::fromUtf8(exception.what())));
        emit failed(QStringLiteral("解码过程中发生错误"));
    } catch (...) {
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
