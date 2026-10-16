// Original MIT integration. Called synchronously on the decoder's worker.
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void bm_xmp_begin(const int *mix, int frames, int amplify);
void bm_xmp_voice(int channel, const int *mix);
void bm_xmp_end(const int *mix);
#ifdef __cplusplus
}
#endif
