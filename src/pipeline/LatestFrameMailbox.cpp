// SPDX-License-Identifier: GPL-3.0-only
#include "pipeline/LatestFrameMailbox.h"

#include <QMutexLocker>

#include <utility>

namespace cimbarpunk {

void LatestFrameMailbox::replace(QImage frame) {
    QMutexLocker locker(&m_mutex);
    if (m_stopped) {
        return;
    }
    if (m_frame.has_value()) {
        ++m_droppedCount;
    }
    m_frame = std::move(frame);
    m_frameAvailable.wakeOne();
}

std::optional<QImage> LatestFrameMailbox::take() {
    QMutexLocker locker(&m_mutex);
    const quint64 generation = m_stopGeneration;
    while (!m_frame.has_value() && !m_stopped && generation == m_stopGeneration) {
        m_frameAvailable.wait(&m_mutex);
    }
    if (m_stopped || generation != m_stopGeneration) {
        return std::nullopt;
    }

    std::optional<QImage> frame = std::move(m_frame);
    m_frame.reset();
    return frame;
}

void LatestFrameMailbox::stop() {
    QMutexLocker locker(&m_mutex);
    if (!m_stopped) {
        m_stopped = true;
        ++m_stopGeneration;
    }
    m_frameAvailable.wakeAll();
}

void LatestFrameMailbox::reset() {
    QMutexLocker locker(&m_mutex);
    m_frame.reset();
    m_stopped = false;
    m_droppedCount = 0;
    m_frameAvailable.wakeAll();
}

quint64 LatestFrameMailbox::droppedCount() const {
    QMutexLocker locker(&m_mutex);
    return m_droppedCount;
}

} // namespace cimbarpunk
