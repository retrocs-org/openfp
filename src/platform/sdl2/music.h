/* SPDX-License-Identifier: MIT */
#ifndef OFP_MUSIC_H
#define OFP_MUSIC_H

#include <SDL.h>
#include <stddef.h>
#include "common/soundtrack.h"

enum { MUSIC_CLASSIC = BIOME_CLASSIC, MUSIC_DESERT = BIOME_DESERT, MUSIC_MENU = SOUNDTRACK_MENU, MUSIC_SONGS = SOUNDTRACK_COUNT };

void music_init(void);
void music_close(void);
const char *music_name(int song);
void music_play(int song);      /* -1 = none; changes fade */
void music_volume(int level);   /* 0 off, 1 .. 10 */
int  music_sample(void);        /* (the mixer: the next sample) */
/* Configure paths only; tracks are prepared by music_play when needed.
 * tracker modules: <data>/music/<song>.mod|.xm|.s3m|.it, and <user>/music/ first
 * (user_dir ends in a separator; OFP_MUSIC_DIR in the environment overrides it) */
void music_load(const char *data_dir, const char *user_dir);
/* Attach the paused audio device before playback; close music before closing it. */
void music_set_device(SDL_AudioDeviceID device);
/* PCM residency diagnostics; loading can temporarily hold a second transition track. */
size_t music_cached_bytes(void);
int music_cached_tracks(void);
int  music_has_module(int song);
const char *music_credit(int song);   /* "TITLE BY AUTHOR (LICENCE)", or the module's own title */
int  music_started(void);       /* a song that just began, once (else -1): to show its credit */

#endif
