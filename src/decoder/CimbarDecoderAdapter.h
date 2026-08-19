// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "decoder/IDecoder.h"

#include <memory>

namespace cimbarpunk {

class CimbarDecoderAdapter final : public IDecoder {
public:
    CimbarDecoderAdapter();
    ~CimbarDecoderAdapter() override;

    CimbarDecoderAdapter(const CimbarDecoderAdapter&) = delete;
    CimbarDecoderAdapter& operator=(const CimbarDecoderAdapter&) = delete;

    void reset() override;
    DecodeUpdate decode(const QImage& rgbFrame) override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace cimbarpunk
