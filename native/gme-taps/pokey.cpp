// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (c) 2026 Bit Music Visualizer contributors.
#include "pokey.h"
#include "Sap_Apu.h"
#include <algorithm>
#include <array>
#include <memory>

struct BmPokey {
    Sap_Apu_Impl tables;
    Sap_Apu apu;
    std::array<Blip_Buffer, 4> buffers;
};
BmPokey *bm_pokey_create(int clock, int sampleRate) {
    auto p = std::make_unique<BmPokey>();
    for (int i = 0; i < 4; ++i) {
        if (p->buffers[i].set_sample_rate(sampleRate)) return nullptr;
        p->buffers[i].clock_rate(clock);
        p->buffers[i].bass_freq(20);
        p->apu.osc_output(i, &p->buffers[i]);
    }
    p->tables.volume(0.8);
    p->apu.reset(&p->tables);
    return p.release();
}
void bm_pokey_delete(BmPokey *p) { delete p; }
void bm_pokey_write(BmPokey *p, int time, int address, int value) {
    p->apu.write_data(time, 0xd200u + unsigned(address), value);
}
void bm_pokey_end(BmPokey *p, int clocks) {
    p->apu.end_frame(clocks);
    for (auto &b : p->buffers) b.end_frame(clocks);
}
int bm_pokey_read(BmPokey *p, short *voices, int frames) {
    const int count = std::min(frames, int(p->buffers[0].samples_avail()));
    // Four planar rows, each with the caller's stride. Never disconnect a voice:
    // linked dividers/high-pass paths must advance even when that voice is muted.
    for (int i = 0; i < 4; ++i) p->buffers[i].read_samples(voices + i * frames, count);
    return count;
}
