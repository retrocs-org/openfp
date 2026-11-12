/* SPDX-License-Identifier: MIT
 * Sound: a small software mixer on SDL audio. It plays samples for
 * the game's effects, plus loops for the
 * player's engine and nearby helicopters. Purely an output: nothing here
 * feeds back into the simulation. */
#ifndef OFP_SDL2_AUDIO_H
#define OFP_SDL2_AUDIO_H

#include "core/game.h"

int  audio_init(int effects_level, int music_level);   /* 0 = ok; failure just means silence */
void audio_shutdown(void);
void audio_toggle_mute(void);
void audio_set_volume(int level);       /* effects, 0 .. 10 */
int  audio_muted(void);
void audio_reset(void);                 /* new game: forget what we've heard */
void audio_menu_feedback(int confirm);
void audio_skull_laugh(int tank, uint16_t losses);
void audio_update(const Game *g, int listener);   /* once per frame */
void audio_update2(const Game *g, int listener, int listener2);   /* split screen: both ears; -1 = none */
void audio_update_n(const Game *g, int listeners);                /* tanks 0..listeners-1 are the ears */
void audio_set_near_engines(int on);    /* the engines of other tanks near you (EFFECTS: FULL) */
void audio_quiet(void);                 /* stop loops (menus) */

#endif
