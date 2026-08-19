// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QImage>
#include <QMutex>
#include <QWaitCondition>
#include <QtGlobal>

#include <optional>

namespace cimbarpunk {

class LatestFrameMailbox final {
public:
    void replace(QImage frame);
    [[nodiscard]] std::optional<QImage> take();
    void stop();
    void reset();
    [[nodiscard]] quint64 droppedCount() const;

private:
    mutable QMutex m_mutex;
    QWaitCondition m_frameAvailable;
    std::optional<QImage> m_frame;
    bool m_stopped = false;
    quint64 m_stopGeneration = 0;
    quint64 m_droppedCount = 0;
};

} // namespace cimbarpunk
