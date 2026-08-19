// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "core/SessionTypes.h"

#include <QImage>

namespace cimbarpunk {

class IDecoder {
public:
    virtual ~IDecoder() = default;
    virtual void reset() = 0;
    virtual DecodeUpdate decode(const QImage& rgbFrame) = 0;
};

} // namespace cimbarpunk
