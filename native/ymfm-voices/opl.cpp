// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021, Aaron Giles
// Copyright (c) 2026, Bit Music Visualizer contributors
// See ym2203.h for the complete notice.
#include <ymfm_opl.cpp>
#include "voices.h"

template<class Base> void bitmusic::OplVoices<Base>::generate_voices(VoiceSample &out) {
    using Engine=typename Base::fm_engine;
    out={};
    this->m_fm.clock(Engine::ALL_CHANNELS);
    if constexpr(std::is_same_v<Base,ymfm::y8950>) this->m_adpcm_b.clock();
    // The core owns rhythm/operator pairing. A 4-op pair contributes through
    // its first channel; the second is silent while assigned to that pair.
    for(unsigned c=0;c<Engine::CHANNELS;++c) {
        typename Engine::output_data voice{};
        this->m_fm.output(voice,Base::OUTPUTS==4 ? 0:1,32767,1u<<c);
        for(unsigned b=0;b<Base::OUTPUTS;++b) out.raw[0][c][b]=voice.data[b];
    }
    if constexpr(std::is_same_v<Base,ymfm::y8950>) {
        typename Engine::output_data adpcm{};
        this->m_adpcm_b.output(adpcm,3);
        out.raw[0][9][0]=adpcm.data[0];
    }
}
template class bitmusic::OplVoices<ymfm::ym3526>;
template class bitmusic::OplVoices<ymfm::ym3812>;
template class bitmusic::OplVoices<ymfm::y8950>;
template class bitmusic::OplVoices<ymfm::ymf262>;
