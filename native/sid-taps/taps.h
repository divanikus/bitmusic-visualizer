// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 Bit Music Visualizer contributors
#pragma once
#include <array>
#include <cstdint>

// Private to one decoder thread. No allocation or locking on chip cycles.
struct BmSidTaps {
    static constexpr int Capacity = 16384;
    bool capture = false;
    uint32_t muteMask = 0;
    unsigned noiseIndex = 0; // Upstream's shared dither cursor must not race between decoders.
    int64_t samples = 0;
    std::array<std::array<float, Capacity>, 3> history{};
    std::array<double, 3> sum{};
    unsigned cycles = 0;
    void cycle(float a, float b, float c) {
        sum[0] += a; sum[1] += b; sum[2] += c; ++cycles;
    }
    void sample() {
        for (int i = 0; i < 3; ++i) {
            // Average the actual envelope-modulated DAC signal over one output
            // interval. Display units only: this is not a post-filter audio stem.
            history[i][samples % Capacity] = cycles ? float(sum[i] * 32768.0 / cycles) : 0;
            sum[i] = 0;
        }
        cycles = 0; ++samples;
    }
    void clear() { samples = 0; sum = {}; cycles = 0; for (auto &v : history) v.fill(0); }
};
extern thread_local BmSidTaps *bm_sid_context;
struct BmSidScope {
    BmSidTaps *previous;
    explicit BmSidScope(BmSidTaps *context) : previous(bm_sid_context) { bm_sid_context = context; }
    ~BmSidScope() { bm_sid_context = previous; }
};
