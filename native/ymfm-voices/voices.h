// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021, Aaron Giles
// Copyright (c) 2026, Bit Music Visualizer contributors
// See ym2203.h and LICENSES/ymfm-voices-BSD-3-Clause.txt for the full notice.
#pragma once
#include <ymfm_opm.h>
#include <ymfm_opl.h>
#include <ymfm_opn.h>
#include <array>
#include <algorithm>
#include <type_traits>

namespace bitmusic {
struct SsgSample { std::array<int32_t,3> sums{}; int divisor=1; };
struct VoiceSample {
    static constexpr int Capacity=18;
    using Bus = std::array<int32_t,4>;
    using Stage = std::array<Bus,Capacity>;
    std::array<Stage,2> raw{};
    SsgSample ssg;
    int stages=1;
    // OPN uses raw slots 0..5 FM, 9..14 ADPCM-A and 15 ADPCM-B.
    // Slots 6..8 are supplied by the SSG resampler. Other chips use 0..N-1.
    int32_t voice(int channel,int bus) const {
        if (channel>=6 && channel<9 && ssgPresent)
            return ssg.sums[channel-6]*2/(3*ssg.divisor);
        return stages==1 ? raw[0][channel][bus] : (raw[0][channel][bus]+raw[1][channel][bus])/2;
    }
    bool ssgPresent=false;
    enum class Dac { Floating, Clamp16 };
    Bus mix(uint32_t enabled,int count,Dac dac) const {
        Bus result{};
        for(int stage=0;stage<stages;++stage) {
            ymfm::ymfm_output<4> sum{};
            for(int c=0;c<count;++c) if(enabled&(1u<<c))
                for(int b=0;b<4;++b) sum.data[b]+=raw[stage][c][b];
            if(dac==Dac::Floating) sum.roundtrip_fp(); else sum.clamp16();
            for(int b=0;b<4;++b) result[b]+=sum.data[b];
        }
        if(stages==2) for(auto &b:result) b/=2;
        return result;
    }
    int32_t ssgMix(uint32_t enabled) const {
        int32_t sum=0;
        for(int c=0;c<3;++c) if(enabled&(1u<<(c+6))) sum+=ssg.sums[c];
        return sum*2/(3*ssg.divisor);
    }
};
class Ym2151Voices : public ymfm::ym2151 {
public:
    using ym2151::ym2151;
    void generate_voices(VoiceSample &out);
};
template<class Base> class OplVoices : public Base {
public:
    using Base::Base;
    void generate_voices(VoiceSample &out);
};
using Ym3526Voices=OplVoices<ymfm::ym3526>;
using Ym3812Voices=OplVoices<ymfm::ym3812>;
using Y8950Voices=OplVoices<ymfm::y8950>;
using Ymf262Voices=OplVoices<ymfm::ymf262>;

template<class Base> class OpnSampleVoices : public Base {
public:
    explicit OpnSampleVoices(ymfm::ymfm_interface &intf) : Base(intf), ssg_(this->m_ssg) {}
    void reset() { Base::reset(); last_={}; }
    void generate_voices(VoiceSample &out);
private:
    void clockVoices();
    VoiceSample::Stage last_{};
    ymfm::ssg_resampler<SsgSample,0,true> ssg_;
};
using Ym2608Voices=OpnSampleVoices<ymfm::ym2608>;
using Ym2610Voices=OpnSampleVoices<ymfm::ym2610>;
using Ym2610bVoices=OpnSampleVoices<ymfm::ym2610b>;
}
