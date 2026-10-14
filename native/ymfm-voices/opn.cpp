// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021, Aaron Giles
// Copyright (c) 2026, Bit Music Visualizer contributors
// See ym2203.h for the complete BSD notice and disclaimer.

// Compile the unchanged upstream implementation and our derived class together:
// its OPN templates are defined in this translation unit, not exported as an API.
#include <ymfm_opn.cpp>
#include "ym2203.h"
#include "voices.h"

// Preserve the original shared SSG rounding (sum before division), while
// retaining individual numerators for visualization and arbitrary user mutes.
template<> void ymfm::ssg_resampler<bitmusic::SsgSample,0,true>::write_to_output(
    bitmusic::SsgSample *out,int32_t a,int32_t b,int32_t c,int32_t divisor) {
    out->sums={a,b,c}; out->divisor=divisor; ++m_sampindex;
}

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

template<class Base> void bitmusic::OpnSampleVoices<Base>::clockVoices() {
    using Engine=typename Base::fm_engine;
    uint32_t mask;
    if constexpr(std::is_same_v<Base,ymfm::ym2608>) mask=(this->m_irq_enable&0x80)?63:7;
    else mask=this->m_fm_mask;
    const auto env=this->m_fm.clock(std::is_same_v<Base,ymfm::ym2608>?Engine::ALL_CHANNELS:mask);
    if ((env&3)==0) {
        if constexpr(std::is_same_v<Base,ymfm::ym2608>) this->m_adpcm_a.clock((env&4)?15:63);
        else this->m_eos_status |= this->m_adpcm_a.clock(63);
    }
    this->m_adpcm_b.clock();
    if constexpr(!std::is_same_v<Base,ymfm::ym2608>) {
        uint8_t eos=(this->m_adpcm_b.status()&ymfm::adpcm_b_channel::STATUS_EOS)?0x40:0;
        if ((eos^this->m_eos_status)&0x40) this->m_eos_status=(this->m_eos_status&~0xc0)|eos|(eos<<1);
    }
    last_={};
    for(int c=0;c<6;++c) {
        typename Engine::output_data fm{},adpcm{};
        this->m_fm.output(fm,1,32767,mask&(1u<<c));
        this->m_adpcm_a.output(adpcm,1u<<c);
        for(int b=0;b<2;++b) { last_[c][b]=fm.data[b]; last_[9+c][b]=adpcm.data[b]; }
    }
    typename Engine::output_data delta{};
    this->m_adpcm_b.output(delta,1);
    for(int b=0;b<2;++b) last_[15][b]=delta.data[b];
}
template<class Base> void bitmusic::OpnSampleVoices<Base>::generate_voices(VoiceSample &out) {
    // The player selects minimum fidelity. Preserve all native conversion ratios.
    if constexpr(std::is_same_v<Base,ymfm::ym2608>) {
        switch(this->m_fm.clock_prescale()) {
        case 3: ssg_.configure(1,3); break;
        case 2: ssg_.configure(1,6); break;
        default: ssg_.configure(2,3); break;
        }
    } else ssg_.configure(2,9);
    out={}; out.ssgPresent=true;
    const auto index=ssg_.sampindex();
    if (this->m_fm_samples_per_output) {
        if(index%this->m_fm_samples_per_output==0) clockVoices();
        out.raw[0]=last_;
    } else {
        if(index%3==0) clockVoices();
        out.raw[0]=last_;
        if(index%3==1) { clockVoices(); out.raw[1]=last_; out.stages=2; }
    }
    ssg_.resample(&out.ssg,1);
}
template class bitmusic::OpnSampleVoices<ymfm::ym2608>;
template class bitmusic::OpnSampleVoices<ymfm::ym2610>;
template class bitmusic::OpnSampleVoices<ymfm::ym2610b>;
template class ymfm::ssg_resampler<bitmusic::SsgSample,0,true>;
