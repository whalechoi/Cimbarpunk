// SPDX-License-Identifier: GPL-3.0-only
#include "pipeline/FramePipeline.h"

#include "selection/SelectionModel.h"

#include <utility>

namespace cimbarpunk {

void FramePipeline::configure(ScreenSelection selection) {
    m_selection = std::move(selection);
}

std::optional<QImage> FramePipeline::prepare(QImage frame) const {
    if (frame.isNull() || frame.size().isEmpty()) {
        return std::nullopt;
    }

    SelectionModel selectionModel;
    selectionModel.setScreenGeometry(m_selection.screenGeometry);
    selectionModel.setSelection(m_selection.logicalRect);
    const QRect crop = selectionModel.mapToFrame(frame.size());
    if (crop.isEmpty()) {
        return std::nullopt;
    }

    QImage prepared = frame.copy(crop).convertToFormat(QImage::Format_RGB888);
    if (prepared.isNull() || prepared.size().isEmpty()) {
        return std::nullopt;
    }
    prepared.detach();
    return prepared;
}

} // namespace cimbarpunk
