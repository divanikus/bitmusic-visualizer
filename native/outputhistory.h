#pragma once
#include "engine.h"

// Single callback writer, GUI readers. Atomic sample pairs avoid data races even
// if a callback overwrites the history while a reader copies it. The sequence
// validates a complete snapshot; the writer never waits for the reader.
class OutputHistory {
public:
    void push(const short *pcm, size_t frames, quint64 generation, quint64 token, int positionMs);
    OutputFrame read() const;
private:
    static constexpr size_t Capacity = ScopeFrames * 2;
    std::array<std::atomic<uint32_t>, Capacity> samples_{};
    std::atomic<quint64> sequence_{0}, count_{0}, generation_{0}, token_{0};
    std::atomic<int> position_{0};
};
