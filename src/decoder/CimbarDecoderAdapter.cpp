// SPDX-License-Identifier: GPL-3.0-only
#include "decoder/CimbarDecoderAdapter.h"

#include "cimb_translator/Config.h"
#include "compression/zstd_header_check.h"
#include "encoder/Decoder.h"
#include "extractor/Corners.h"
#include "extractor/Deskewer.h"
#include "extractor/Scanner.h"
#include "fountain/fountain_decoder_sink.h"

#include <QByteArray>
#include <QString>

#include <opencv2/core.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cimbarpunk {

namespace {

constexpr std::array<int, 4> kCandidateModes{68, 66, 67, 4};

unsigned chunkSizeForMode(int mode)
{
    return cimbar::Config::temp_conf(mode).fountain_chunk_size();
}

int colorCorrectionForMode(int mode)
{
    return mode == 4 ? 1 : 2;
}

} // namespace

class CimbarDecoderAdapter::Impl {
public:
    Impl()
    {
        reset();
    }

    void reset()
    {
        m_lockedMode.reset();
        m_decoder.reset();
        m_completed = false;
        m_pendingCompletion.reset();
        m_sink = makeSink(chunkSizeForMode(kCandidateModes.front()));
    }

    DecodeUpdate decode(const QImage& rgbFrame)
    {
        DecodeUpdate update;
        if (m_completed || rgbFrame.isNull() || rgbFrame.format() != QImage::Format_RGB888) {
            return update;
        }

        cv::Mat input(rgbFrame.height(), rgbFrame.width(), CV_8UC3,
            const_cast<uchar*>(rgbFrame.constBits()), rgbFrame.bytesPerLine());
        Scanner scanner(input);
        const std::vector<Anchor> anchors = scanner.scan();
        if (anchors.size() < 4) {
            return update;
        }

        const Corners corners(anchors);
        if (m_lockedMode.has_value()) {
            decodeMode(input, corners, *m_lockedMode, *m_decoder, update);
            finishUpdate(update);
            return update;
        }

        for (const int mode : kCandidateModes) {
            cimbar::Config::update(mode);
            auto candidateDecoder = std::make_unique<Decoder>();
            const unsigned decodedBytes = decodeMode(input, corners, mode, *candidateDecoder, update);
            if (decodedBytes == 0) {
                continue;
            }

            m_lockedMode = mode;
            m_decoder = std::move(candidateDecoder);
            const unsigned requiredChunkSize = chunkSizeForMode(mode);
            if (m_sink->chunk_size() != requiredChunkSize) {
                m_sink = makeSink(requiredChunkSize);
            }
            break;
        }

        finishUpdate(update);
        return update;
    }

private:
    std::unique_ptr<fountain_decoder_sink> makeSink(unsigned chunkSize)
    {
        const auto onStore = [this](const std::string& fallbackName,
                                 const std::vector<std::uint8_t>& compressedBytes) {
            DecodedPayload payload;
            payload.fallbackName = QString::fromStdString(fallbackName);
            const std::string embeddedName = cimbar::zstd_header_check::get_filename(
                compressedBytes.data(), compressedBytes.size());
            payload.suggestedName = QString::fromUtf8(embeddedName.data(),
                static_cast<qsizetype>(embeddedName.size()));
            payload.compressedBytes = QByteArray(
                reinterpret_cast<const char*>(compressedBytes.data()),
                static_cast<qsizetype>(compressedBytes.size()));
            m_pendingCompletion = std::move(payload);
            m_completed = true;
            return embeddedName.empty() ? fallbackName : embeddedName;
        };
        return std::make_unique<fountain_decoder_sink>(chunkSize, onStore);
    }

    unsigned decodeMode(const cv::Mat& input, const Corners& corners, int mode,
        Decoder& decoder, DecodeUpdate& update)
    {
        cimbar::Config::update(mode);
        const cimbar::conf configuration = cimbar::Config::temp_conf(mode);
        const cimbar::vec_xy imageSize{configuration.image_size_x, configuration.image_size_y};
        Deskewer deskewer(0, imageSize, cimbar::Config::anchor_size());
        cv::Mat extracted = deskewer.deskew(input, corners);
        if (extracted.empty()) {
            return 0;
        }

        const bool shouldPreprocess = !corners.is_granular_scale(imageSize);
        const unsigned decodedBytes = decoder.decode_fountain(
            extracted, *m_sink, shouldPreprocess, colorCorrectionForMode(mode));
        if (decodedBytes > 0) {
            update.recognized = true;
        }
        return decodedBytes;
    }

    void finishUpdate(DecodeUpdate& update)
    {
        if (m_pendingCompletion.has_value()) {
            update.progress = 1.0;
            update.completed = std::move(m_pendingCompletion);
            m_pendingCompletion.reset();
            return;
        }

        const std::vector<double> progress = m_sink->get_progress();
        if (!progress.empty()) {
            update.progress = std::clamp(*std::max_element(progress.begin(), progress.end()), 0.0, 1.0);
        }
    }

    std::optional<int> m_lockedMode;
    std::unique_ptr<Decoder> m_decoder;
    std::unique_ptr<fountain_decoder_sink> m_sink;
    bool m_completed = false;
    std::optional<DecodedPayload> m_pendingCompletion;
};

CimbarDecoderAdapter::CimbarDecoderAdapter()
    : m_impl(std::make_unique<Impl>())
{
}

CimbarDecoderAdapter::~CimbarDecoderAdapter() = default;

void CimbarDecoderAdapter::reset()
{
    m_impl->reset();
}

DecodeUpdate CimbarDecoderAdapter::decode(const QImage& rgbFrame)
{
    return m_impl->decode(rgbFrame);
}

} // namespace cimbarpunk
