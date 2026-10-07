// Optional visualization taps for the pinned libgme. LGPL-2.1-or-later.
#pragma once
#include "gme.h"
#include <stdint.h>
struct BmTaps;
extern "C" {
BLARGG_EXPORT BmTaps* bm_taps_create(int kind); // 1: YM2612, 2: YM2413, 3: SPC
BLARGG_EXPORT void bm_taps_delete(BmTaps*);
BLARGG_EXPORT BmTaps* bm_taps_enter(BmTaps*);
BLARGG_EXPORT void bm_taps_reset(BmTaps*);
BLARGG_EXPORT int bm_taps_read(BmTaps*, int channel, int64_t endFrame, int frames, float* left, float* right);
}
bool bm_taps_active();
void bm_taps_fm(const float* pairs, int channels);
void bm_taps_spc(const float* pairs);
void bm_taps_spc_ratio(double ratio);
void bm_taps_vgm_configure(double sampleRate, long clock, int feedback, int width, double gain);
void bm_taps_psg(long time, int data, bool stereo);
void bm_taps_pcm(long time, int delta);
void bm_taps_vgm_end(long clocks);
