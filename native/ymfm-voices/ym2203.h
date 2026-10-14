// BSD 3-Clause License
//
// Copyright (c) 2021, Aaron Giles
// Copyright (c) 2026, Bit Music Visualizer contributors
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once
#include <ymfm_opn.h>
#include <array>

namespace bitmusic {
// Based on ymfm's ym2203::generate/clock_fm. The pinned upstream is unmodified.
// Advance the three FM channels once, collecting each channel's real output.
// Keep all DAC mix combinations so user mutes can change after resampling
// without disturbing feedback/envelopes or leaking the previous mute state.
class Ym2203Voices : public ymfm::ym2203 {
public:
    using ymfm::ym2203::ym2203;
    struct Voices {
        std::array<int32_t,6> channels{}; // FM before shared DAC, then resampled SSG.
        std::array<int32_t,8> fmMix{}; // Exact shared-DAC output for each FM mask.
    };
    void reset() { ymfm::ym2203::reset(); last_ = {}; }
    void generate_voices(Voices &out) {
        const auto index = m_ssg_resampler.sampindex();
        if (m_fm_samples_per_output != 0) {
            if (index % m_fm_samples_per_output == 0) clockVoices();
            out = last_;
        } else {
            const auto step = index % 3;
            if (step == 0) clockVoices();
            out = last_;
            if (step == 1) {
                clockVoices();
                for (int c=0;c<3;++c) out.channels[c] = (out.channels[c]+last_.channels[c])/2;
                for (int mask=0;mask<8;++mask) out.fmMix[mask] = (out.fmMix[mask]+last_.fmMix[mask])/2;
            }
        }
        output_data ssg{};
        m_ssg_resampler.resample(&ssg,1);
        for (int c=0;c<3;++c) out.channels[c+3] = ssg.data[c+1];
    }
private:
    void clockVoices();
    Voices last_{};
};
}
