// YM2413 adapter for libgme's bundled emu2413 core. LGPL-2.1-or-later.
#include "Ym2413_Emu.h"
#include "ext/emu2413.h"
#include "taps.h"
#include <algorithm>
Ym2413_Emu::Ym2413_Emu() : opll(nullptr) {}
Ym2413_Emu::~Ym2413_Emu() { if (opll) OPLL_delete(opll); }
int Ym2413_Emu::set_rate(double rate, double clock) {
    if (opll) OPLL_delete(opll);
    opll = OPLL_new(static_cast<uint32_t>(clock), static_cast<uint32_t>(rate));
    if (!opll) return 1;
    OPLL_setChipType(opll, 0); OPLL_resetPatch(opll, 0);
    return 0;
}
void Ym2413_Emu::reset() { if (opll) { OPLL_reset(opll); OPLL_resetPatch(opll, 0); } }
void Ym2413_Emu::write(int address, int data) {
    if (address == 0x0e && ((opll->reg[0x0e] ^ data) & 0x20))
        for (int ch = 6; ch < 14; ++ch) opll->ch_out[ch] = 0;
    OPLL_writeReg(opll, address, data);
}
void Ym2413_Emu::mute_voices(int mask) {
    // One public voice per hardware channel. Rhythm reuses channels 7-9.
    uint32_t mapped = mask & 0x1ff;
    if (mask & (1 << 6)) mapped |= OPLL_MASK_BD;
    if (mask & (1 << 7)) mapped |= OPLL_MASK_HH | OPLL_MASK_SD;
    if (mask & (1 << 8)) mapped |= OPLL_MASK_TOM | OPLL_MASK_CYM;
    OPLL_setMask(opll, mapped);
    // emu2413 leaves skipped ch_out entries untouched; clear their tails.
    constexpr int groups[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 6, 7, 7, 8, 8};
    for (int i = 0; i < 14; ++i) if (mask & (1 << groups[i])) opll->ch_out[i] = 0;
}
void Ym2413_Emu::run(int count, sample_t* out) {
    for (int i = 0; i < count; ++i) {
        int32_t pair[2]; OPLL_calcStereo(opll, pair);
        for (int side = 0; side < 2; ++side) out[i * 2 + side] = static_cast<short>(std::clamp(int(out[i * 2 + side]) + pair[side], -32768, 32767));
        if (bm_taps_active()) {
            float channels[18];
            for (int ch = 0; ch < 9; ++ch) {
                float signal = opll->ch_out[ch];
                if (opll->rhythm_mode) {
                    if (ch == 6) signal = opll->ch_out[9];
                    if (ch == 7) signal = float(opll->ch_out[10]) + opll->ch_out[11];
                    if (ch == 8) signal = float(opll->ch_out[12]) + opll->ch_out[13];
                }
                channels[ch * 2] = channels[ch * 2 + 1] = signal;
            }
            bm_taps_fm(channels, 9);
        }
    }
}
