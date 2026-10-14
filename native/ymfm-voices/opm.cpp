// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021, Aaron Giles
// Copyright (c) 2026, Bit Music Visualizer contributors
// See ym2203.h for the complete notice.
#include <ymfm_opm.cpp>
#include "voices.h"

void bitmusic::Ym2151Voices::generate_voices(VoiceSample &out) {
    out={};
    m_fm.clock(fm_engine::ALL_CHANNELS);
    for(int c=0;c<8;++c) {
        fm_engine::output_data voice{};
        m_fm.output(voice,0,32767,1u<<c);
        for(int b=0;b<2;++b) out.raw[0][c][b]=voice.data[b];
    }
}
