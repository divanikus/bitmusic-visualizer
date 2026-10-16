#include "capture.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {
thread_local XmpCapture *active = nullptr;
void finishVoice(const int *mix) {
    if (!active) return;
    auto &c = *active;
    if (c.source >= 0 && c.source < int(c.channels.size())) {
        auto &out = c.channels[c.source];
        for (int i = 0; i < c.samples; ++i)
            out[i] += float(int64_t(mix[i]) - c.previous[i]) * c.scale;
    }
}
}
XmpCaptureScope::XmpCaptureScope(XmpCapture *capture) : previous_(active) { active = capture; }
XmpCaptureScope::~XmpCaptureScope() { active = previous_; }
extern "C" void bm_xmp_begin(const int *mix, int frames, int amplify) {
    if (!active) return;
    auto &c = *active;
    c.samples = frames*2; c.source = -1;
    c.scale = std::ldexp(1.f, amplify-12); // libxmp's signed 16-bit downmix, before clipping.
    c.previous.assign(mix, mix+c.samples);
    for (auto &out : c.channels) out.assign(c.samples, 0.f);
}
extern "C" void bm_xmp_voice(int channel, const int *mix) {
    if (!active) return;
    finishVoice(mix);
    active->source = channel;
    std::copy_n(mix, active->samples, active->previous.begin());
}
extern "C" void bm_xmp_end(const int *mix) {
    finishVoice(mix);
    if (active) active->source = -1;
}
