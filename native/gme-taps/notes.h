// Optional pitch-state capture for the pinned libgme. LGPL-2.1-or-later.
#pragma once
#include "gme.h"
#include <stdint.h>
struct BmNotes;
extern "C" {
BLARGG_EXPORT BmNotes* bm_notes_create();
BLARGG_EXPORT void bm_notes_delete(BmNotes*);
BLARGG_EXPORT BmNotes* bm_notes_enter(BmNotes*);
BLARGG_EXPORT void bm_notes_reset(BmNotes*);
// 1: known tonal voice (Hz=0 means gated off); 0: unavailable.
BLARGG_EXPORT int bm_notes_read(BmNotes*, int channel, int64_t sample, float* hz);
}
bool bm_notes_active();
void bm_notes_clock(double rate);
void bm_notes_nes(long begin, long end, const float* periods);
void bm_notes_end(long clocks);
