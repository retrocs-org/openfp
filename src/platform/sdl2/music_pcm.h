/* SPDX-License-Identifier: MIT */
#ifndef OFP_MUSIC_PCM_H
#define OFP_MUSIC_PCM_H
#include <stdint.h>

int16_t *music_pcm_load(const char *path, int rate, int *samples);
#endif
