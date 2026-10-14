// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (c) 2026 Bit Music Visualizer contributors.
#pragma once
#include "gme.h"
struct BmPokey;
extern "C" {
BLARGG_EXPORT BmPokey *bm_pokey_create(int clock, int sampleRate);
BLARGG_EXPORT void bm_pokey_delete(BmPokey *);
BLARGG_EXPORT void bm_pokey_write(BmPokey *, int time, int address, int value);
BLARGG_EXPORT void bm_pokey_end(BmPokey *, int clocks);
BLARGG_EXPORT int bm_pokey_read(BmPokey *, short *voices, int frames);
}
