// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "core/SessionTypes.h"

#include <QImage>

#include <optional>

namespace cimbarpunk {

class FramePipeline final {
public:
    void configure(ScreenSelection selection);
    [[nodiscard]] std::optional<QImage> prepare(QImage frame) const;

private:
    ScreenSelection m_selection;
};

} // namespace cimbarpunk
