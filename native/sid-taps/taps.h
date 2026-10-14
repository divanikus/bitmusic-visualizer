// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 Bit Music Visualizer contributors
#pragma once
#include <array>
#include <cstdint>

// Raised only when C64 execution reads a ROM byte absent from the supplied image
// and from the player's minimal interrupt/reset scaffolding.
struct BmSidRomNeeded { int index; };

// Private to one decoder thread. No allocation or locking on chip cycles.
struct BmSidChipTaps {
    static constexpr int Capacity = 16384;
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
struct BmSidTaps {
    static constexpr int Capacity = BmSidChipTaps::Capacity;
    bool capture = false;
    bool discardRead = false;
    uint32_t muteMask = 0;
    unsigned noiseIndex = 0; // Per decoder, never the upstream singleton cursor.
    std::array<BmSidChipTaps, 3> chips;
    std::array<const void *, 3> owners{};
    int activeChip = 0, count = 0;
    void bind(const void *owner) {
        for (int i = 0; i < count; ++i) if (owners[i] == owner) return;
        if (count < 3) owners[count++] = owner;
    }
    void select(const void *owner) {
        activeChip = -1;
        for (int i = 0; i < count; ++i) if (owners[i] == owner) { activeChip = i; break; }
    }
    void clear() { for (auto &chip : chips) chip.clear(); }
};
extern thread_local BmSidTaps *bm_sid_context;
struct BmSidDiscardRead {
    BmSidTaps *context = bm_sid_context;
    bool previous = context && context->discardRead;
    BmSidDiscardRead() { if (context) context->discardRead = true; }
    ~BmSidDiscardRead() { if (context) context->discardRead = previous; }
};
struct BmSidScope {
    BmSidTaps *previous;
    explicit BmSidScope(BmSidTaps *context) : previous(bm_sid_context) { bm_sid_context = context; }
    ~BmSidScope() { bm_sid_context = previous; }
};
