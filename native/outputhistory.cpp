#include "outputhistory.h"
#include <algorithm>

static_assert(std::atomic<uint32_t>::is_always_lock_free && std::atomic<quint64>::is_always_lock_free,
              "Output capture requires lock-free atomics on this target.");
void OutputHistory::push(const short *pcm, size_t frames, quint64 generation, quint64 token, int positionMs) {
    // Sequential consistency deliberately keeps the seqlock ordering explicit,
    // including the samples. Only ~44k packed stores per second are required.
    ++sequence_;
    quint64 count = generation_ == generation && token_ == token ? count_.load() : 0;
    const size_t skip = frames > Capacity ? frames - Capacity : 0;
    for (size_t i = skip; i < frames; ++i) {
        const uint32_t packed = uint16_t(pcm[i*2]) | (uint32_t(uint16_t(pcm[i*2+1])) << 16);
        samples_[(count + i) % Capacity] = packed;
    }
    count_ = count + frames; generation_ = generation; token_ = token; position_ = positionMs;
    ++sequence_;
}
OutputFrame OutputHistory::read() const {
    OutputFrame result;
    result.left.resize(ScopeFrames); result.right.resize(ScopeFrames);
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto sequence = sequence_.load();
        if (!sequence || (sequence & 1)) continue;
        const auto count = count_.load();
        result.generation = generation_; result.token = token_; result.positionMs = position_;
        const int available = int(std::min<quint64>(count, ScopeFrames));
        const int padding = ScopeFrames - available;
        for (int i = 0; i < padding; ++i) result.left[i] = result.right[i] = 0;
        for (int i = 0; i < available; ++i) {
            const auto pair = samples_[(count - available + i) % Capacity].load();
            result.left[padding + i] = int16_t(pair & 0xffff);
            result.right[padding + i] = int16_t(pair >> 16);
        }
        if (sequence == sequence_.load()) { result.sequence = sequence; return result; }
    }
    return {}; // GUI may retain its last confirmed snapshot for this generation.
}
