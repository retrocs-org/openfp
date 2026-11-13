/* SPDX-License-Identifier: MIT */
#ifndef OFP_MUSIC_PCM_H
#define OFP_MUSIC_PCM_H
#include <stdint.h>

int16_t *music_pcm_load(const char *path, int rate, int *samples);
/* Fixed per-track adjustment, measured over active 400 ms blocks, with peak headroom. */
float music_pcm_loudness_gain(const int16_t *pcm, int samples, int rate);
#endif
