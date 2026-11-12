/* SPDX-License-Identifier: MIT */
#ifndef OFP_AMIGA_MODPLAY_H
#define OFP_AMIGA_MODPLAY_H

enum { MOD_CHANNELS = 4, MOD_SAMPLES = 31 };

typedef struct {                     /* what a channel plays, as Paula takes it */
    const unsigned char *data;       /* the sample, from its start (8-bit signed) */
    unsigned long length;            /* bytes */
    const unsigned char *loop;       /* then this, round and round (NULL: silence after the sample) */
    unsigned long loop_length;       /* bytes */
    unsigned short period;           /* Paula's: 3546895 / period samples a second (PAL) */
    unsigned char volume;            /* 0..64 */
    unsigned char trigger;           /* 1: start the sample again this tick */
} ModVoice;

typedef struct { unsigned long offset, length, loop_start, loop_length; int volume; } ModSample;
typedef struct { int sample, period, volume, fx, arg; } ModChannel;

typedef struct {
    const unsigned char *mod, *patterns, *sample_data;
    unsigned long sample_bytes;
    ModSample sample[MOD_SAMPLES];
    ModChannel channel[MOD_CHANNELS];
    ModVoice voice[MOD_CHANNELS];
    int length, position, row, speed, tick;
} ModPlayer;

/* 0 = ok. samples: where the module's sample data is for playing (mod_sample_bytes() of it, e.g. copied to
 * chip memory), or NULL to play it where it is */
int  mod_open(ModPlayer *m, const unsigned char *mod, unsigned long size, const unsigned char *samples);
unsigned long mod_sample_bytes(const ModPlayer *m);
void mod_tick(ModPlayer *m);         /* 50 times a second; then m->voice[] says what to play */
#endif
