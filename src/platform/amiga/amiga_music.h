/* SPDX-License-Identifier: MIT */
#ifndef OFP_AMIGA_MUSIC_H
#define OFP_AMIGA_MUSIC_H

enum { AMIGA_MUSIC_DEFAULT_LEVEL = 7 };

/* Song IDs follow common/soundtrack.h. */
void music_play(int song);          /* -1: none (the effects get all four channels back) */
int  music_song(void);              /* what plays, -1 none */
void music_close(void);
void music_volume(int level);
void music_update(void);
#endif
