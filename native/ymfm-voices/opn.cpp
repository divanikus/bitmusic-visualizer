// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021, Aaron Giles
// Copyright (c) 2026, Bit Music Visualizer contributors
// See ym2203.h for the complete BSD notice and disclaimer.

// Compile the unchanged upstream implementation and our derived class together:
// its OPN templates are defined in this translation unit, not exported as an API.
#include <ymfm_opn.cpp>
#include "ym2203.h"

void bitmusic::Ym2203Voices::clockVoices() {
    m_fm.clock(fm_engine::ALL_CHANNELS);
    for (int c=0;c<3;++c) {
        fm_engine::output_data voice{};
        m_fm.output(voice,0,32767,1u<<c);
        last_.channels[c] = voice.data[0];
    }
    for (int mask=0;mask<8;++mask) {
        fm_engine::output_data mix{};
        for (int c=0;c<3;++c) if (mask & (1<<c)) mix.data[0] += last_.channels[c];
        mix.roundtrip_fp();
        last_.fmMix[mask] = mix.data[0];
    }
}
