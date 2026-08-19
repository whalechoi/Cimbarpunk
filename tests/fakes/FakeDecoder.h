// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "decoder/IDecoder.h"

#include <QColor>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cimbarpunk::test {

class FakeDecoder final : public IDecoder {
public:
    enum class Failure { None, StandardException, UnknownException };

    explicit FakeDecoder(std::vector<DecodeUpdate> updates = {})
        : m_updates(std::move(updates)) {
    }

    ~FakeDecoder() override {
        releaseFirstDecode();
    }

    void reset() override {
        const std::scoped_lock lock(m_mutex);
        ++m_resetCount;
        m_sessionDecodeCount = 0;
    }

    DecodeUpdate decode(const QImage& rgbFrame) override {
        std::unique_lock lock(m_mutex);
        const std::size_t callIndex = m_sessionDecodeCount++;
        ++m_decodeCount;
        m_seenColors.push_back(rgbFrame.pixelColor(0, 0));
        m_stateChanged.notify_all();
        if (callIndex == 0 && m_blockFirstDecode) {
            m_firstDecodeReleased.wait(lock, [this] { return m_releaseFirstDecode; });
        }
        if (m_failure == Failure::StandardException) {
            throw std::runtime_error("decoder exploded");
        }
        if (m_failure == Failure::UnknownException) {
            throw 7;
        }
        return callIndex < m_updates.size() ? m_updates.at(callIndex) : DecodeUpdate{};
    }

    void setFailure(const Failure failure) {
        const std::scoped_lock lock(m_mutex);
        m_failure = failure;
    }

    void blockFirstDecode() {
        const std::scoped_lock lock(m_mutex);
        m_blockFirstDecode = true;
        m_releaseFirstDecode = false;
    }

    void releaseFirstDecode() {
        const std::scoped_lock lock(m_mutex);
        m_releaseFirstDecode = true;
        m_firstDecodeReleased.notify_all();
    }

    [[nodiscard]] bool waitForDecodeCount(
        const std::size_t count, const std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
        std::unique_lock lock(m_mutex);
        return m_stateChanged.wait_for(lock, timeout, [this, count] { return m_decodeCount >= count; });
    }

    [[nodiscard]] std::vector<QColor> seenColors() const {
        const std::scoped_lock lock(m_mutex);
        return m_seenColors;
    }

    [[nodiscard]] std::size_t decodeCount() const {
        const std::scoped_lock lock(m_mutex);
        return m_decodeCount;
    }

    [[nodiscard]] int resetCount() const {
        const std::scoped_lock lock(m_mutex);
        return m_resetCount;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_firstDecodeReleased;
    std::condition_variable m_stateChanged;
    std::vector<DecodeUpdate> m_updates;
    std::vector<QColor> m_seenColors;
    std::size_t m_sessionDecodeCount = 0;
    std::size_t m_decodeCount = 0;
    int m_resetCount = 0;
    bool m_blockFirstDecode = false;
    bool m_releaseFirstDecode = false;
    Failure m_failure = Failure::None;
};

} // namespace cimbarpunk::test
