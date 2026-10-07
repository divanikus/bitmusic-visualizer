// Optional visualization taps for the pinned libgme. LGPL-2.1-or-later.
#include "taps.h"
#include "Sms_Apu.h"
#include "Spc_Dsp.h"
#include "Blip_Buffer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
namespace {
constexpr int Capacity = 32768;
struct Signal {
    std::array<std::array<float, 2>, Capacity> samples{};
    int64_t written = 0;
    void push(float l, float r) { samples[written++ % Capacity] = {l, r}; }
    float at(int64_t frame, int side) const {
        if (frame < 0 || frame >= written || frame < written - Capacity) return 0;
        return samples[frame % Capacity][side];
    }
};
thread_local BmTaps* current = nullptr;
}
struct BmTaps {
    int kind, feedback = 0, width = 0;
    double fmRate = 44100;
    double spcRatio = 32000.0 / 44100.0;
    std::array<Signal, 15> signals;
    Sms_Apu psg;
    Blip_Buffer psgBuffer, pcmBuffer;
    Blip_Synth<blip_med_quality, 1> pcmSynth;
    explicit BmTaps(int type) : kind(type) {}
};
BmTaps* bm_taps_create(int kind) { return new BmTaps(kind); }
void bm_taps_delete(BmTaps* taps) { delete taps; }
BmTaps* bm_taps_enter(BmTaps* taps) { auto previous = current; current = taps; return previous; }
bool bm_taps_active() { return current != nullptr; }
void bm_taps_reset(BmTaps* t) {
    if (!t) return;
    for (auto& signal : t->signals) signal.written = 0;
    if (t->kind != 3) {
        t->psg.reset(t->feedback, t->width);
        t->psgBuffer.clear(); t->pcmBuffer.clear();
    }
}
int bm_taps_read(BmTaps* t, int channel, int64_t endFrame, int frames, float* left, float* right) {
    if (!t || channel < 0 || channel >= 15) return 0;
    const auto& signal = t->signals[channel];
    const double rate = (channel < (t->kind == 1 ? 6 : 9)) ? t->fmRate : 44100.0;
    // libgme quantizes its FIR ratio; using nominal 32000/44100 drifts on long SPCs.
    const double ratio = t->kind == 3 ? t->spcRatio : rate / 44100.0;
    // Snes_Spc::reset_buf() prefixes half its extra buffer with silence.
    // That is four stereo frames before DSP tap #0, not captured DSP output.
    // Omitting this offset can request unwritten samples when the resampler's
    // read-ahead runs out, briefly marking every SPC voice unready.
    const int offset = t->kind == 3 ? Spc_Dsp::extra_size / 4 : 0;
    // Core buffers run ahead; read by playback frame, never by latest write.
    if (endFrame > 0 && int64_t(std::ceil((endFrame - 1) * ratio - offset)) >= signal.written) return 0;
    if (int64_t(std::floor(std::max(0.0, (endFrame - frames) * ratio - offset))) < signal.written - Capacity) return 0;
    for (int i = 0; i < frames; ++i) {
        const double position = (endFrame - frames + i) * ratio - offset;
        const int64_t at = static_cast<int64_t>(std::floor(position));
        const float f = float(position - at);
        left[i] = signal.at(at, 0) * (1 - f) + signal.at(at + 1, 0) * f;
        right[i] = signal.at(at, 1) * (1 - f) + signal.at(at + 1, 1) * f;
    }
    return 1;
}
void bm_taps_fm(const float* pairs, int channels) {
    if (!current) return;
    for (int i = 0; i < channels; ++i) current->signals[i].push(pairs[i * 2], pairs[i * 2 + 1]);
}
void bm_taps_spc(const float* pairs) { bm_taps_fm(pairs, 8); }
void bm_taps_spc_ratio(double ratio) { if (current) current->spcRatio = ratio; }
void bm_taps_vgm_configure(double rate, long clock, int feedback, int width, double gain) {
    if (!current) return;
    auto& t = *current;
    t.fmRate = rate; t.feedback = feedback; t.width = width;
    // Mirror only inexpensive PSG/DAC synthesis, not the FM emulator.
    if (t.psgBuffer.set_sample_rate(44100) || t.pcmBuffer.set_sample_rate(44100)) throw std::bad_alloc();
    t.psgBuffer.clock_rate(clock); t.pcmBuffer.clock_rate(clock);
    t.psgBuffer.bass_freq(80); t.pcmBuffer.bass_freq(80);
    t.psg.output(&t.psgBuffer); t.psg.volume(0.135 * 3 * gain);
    t.pcmSynth.volume(0.1115 / 256 * 3 * gain);
    bm_taps_reset(&t);
}
void bm_taps_psg(long time, int data, bool stereo) {
    if (!current) return;
    if (stereo) current->psg.write_ggstereo(time, data); else current->psg.write_data(time, data);
}
void bm_taps_pcm(long time, int delta) {
    if (current) current->pcmSynth.offset_inline(time, delta, &current->pcmBuffer);
}
void bm_taps_vgm_end(long clocks) {
    if (!current) return;
    auto& t = *current;
    t.psg.end_frame(clocks); t.psgBuffer.end_frame(clocks); t.pcmBuffer.end_frame(clocks);
    std::array<short, 4096> pcm{}, psg{};
    while (t.psgBuffer.samples_avail()) {
        const int count = int(t.psgBuffer.read_samples(psg.data(), int(psg.size())));
        t.pcmBuffer.read_samples(pcm.data(), count);
        for (int i = 0; i < count; ++i) {
            if (t.kind == 1) t.signals[6].push(pcm[i], pcm[i]);
            t.signals[t.kind == 1 ? 7 : 9].push(psg[i], psg[i]);
        }
    }
}
