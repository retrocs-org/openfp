/* SPDX-License-Identifier: MIT
 * A small ProTracker (M.K., four channels, 31 samples) player: what each channel should be playing, a tick at
 * a time (50 a second, the Amiga's vertical blank), for a frontend to hand to Paula (amiga_music.c) or a mixer
 * (tests/test_modplay.c). Plain C, no OS calls, so the same code is tested on the desktop.
 *
 * Enough for the game's own music (tools/amiga/gen_music.py) and simple modules: notes, samples and their
 * loops, finetune-free periods, and the effects A (volume slide), B (position jump), C (volume), D (pattern
 * break), E C (note cut), F (speed; a tempo of 32 or more is ignored: the vertical blank sets the pace). */
#include <string.h>
#include "platform/amiga/modplay.h"

static unsigned be16(const unsigned char *p) { return (unsigned)p[0] << 8 | p[1]; }

int mod_open(ModPlayer *m, const unsigned char *mod, unsigned long size, const unsigned char *samples)
{   /* samples: a copy of the module's sample data (chip memory, for Paula), or NULL: the module's own */
    unsigned long at;
    int i, top = 0;
    memset(m, 0, sizeof *m);
    if (size < 1084 || memcmp(mod + 1080, "M.K.", 4)) return -1;
    m->mod = mod;
    m->length = mod[950];
    if (m->length < 1 || m->length > 128) return -1;
    for (i = 0; i < 128; i++) if (mod[952 + i] > top) top = mod[952 + i];
    m->patterns = mod + 1084;
    at = 1084 + (unsigned long)(top + 1) * 1024;
    for (i = 0; i < MOD_SAMPLES; i++) {
        const unsigned char *h = mod + 20 + i * 30;
        ModSample *s = &m->sample[i];
        s->length = be16(h + 22) * 2UL;
        s->volume = h[25] > 64 ? 64 : h[25];
        s->loop_start = be16(h + 26) * 2UL;
        s->loop_length = be16(h + 28) * 2UL;
        if (s->loop_start + s->loop_length > s->length) s->loop_length = s->length > s->loop_start ? s->length - s->loop_start : 0;
        s->offset = at - 1084 - (unsigned long)(top + 1) * 1024;   /* (into the sample data, after the patterns) */
        at += s->length;
    }
    if (at > size) return -1;
    m->sample_data = samples ? samples : mod + 1084 + (unsigned long)(top + 1) * 1024;
    m->sample_bytes = at - 1084 - (unsigned long)(top + 1) * 1024;
    m->speed = 6;
    m->tick = m->speed;                                   /* (the first tick reads the first row) */
    return 0;
}

unsigned long mod_sample_bytes(const ModPlayer *m) { return m->sample_bytes; }

static void row(ModPlayer *m)
{
    const unsigned char *cell = m->patterns + (unsigned long)m->mod[952 + m->position] * 1024 + m->row * 16;
    int c, jump = -1, brk = -1;
    for (c = 0; c < MOD_CHANNELS; c++, cell += 4) {
        ModChannel *ch = &m->channel[c];
        ModVoice *v = &m->voice[c];
        int sample = (cell[0] & 0xf0) | cell[2] >> 4, per = (cell[0] & 15) << 8 | cell[1];
        int fx = cell[2] & 15, arg = cell[3];
        ch->fx = fx; ch->arg = arg;
        if (sample >= 1 && sample <= MOD_SAMPLES) {
            ch->sample = sample;
            ch->volume = m->sample[sample - 1].volume;
        }
        if (per && ch->sample) {
            const ModSample *s = &m->sample[ch->sample - 1];
            ch->period = per;
            v->data = m->sample_data + s->offset;
            v->length = s->length;
            v->loop = s->loop_length > 2 ? m->sample_data + s->offset + s->loop_start : 0;
            v->loop_length = s->loop_length > 2 ? s->loop_length : 0;
            v->trigger = 1;
        }
        if (fx == 0xC) ch->volume = arg > 64 ? 64 : arg;
        else if (fx == 0xF && arg) { if (arg < 32) m->speed = arg; }
        else if (fx == 0xB) jump = arg;
        else if (fx == 0xD) brk = (arg >> 4) * 10 + (arg & 15);
        if (fx == 0xE && (arg >> 4) == 0xC && (arg & 15) == 0) ch->volume = 0;
        v->period = (unsigned short)ch->period;
        v->volume = (unsigned char)ch->volume;
    }
    if (jump >= 0 || brk >= 0) {
        m->position = jump >= 0 ? jump : m->position + 1;
        m->row = brk >= 0 && brk < 64 ? brk : 0;
    } else if (++m->row >= 64) {
        m->row = 0;
        m->position++;
    }
    if (m->position >= m->length) m->position = 0;        /* (round and round) */
}

static void effects(ModPlayer *m)                         /* (between rows) */
{
    int c;
    for (c = 0; c < MOD_CHANNELS; c++) {
        ModChannel *ch = &m->channel[c];
        if (ch->fx == 0xA) {
            int up = ch->arg >> 4, down = ch->arg & 15;
            ch->volume += up ? up : -down;
            if (ch->volume < 0) ch->volume = 0;
            if (ch->volume > 64) ch->volume = 64;
        } else if (ch->fx == 0xE && (ch->arg >> 4) == 0xC && m->tick == (ch->arg & 15)) ch->volume = 0;
        m->voice[c].volume = (unsigned char)ch->volume;
    }
}

void mod_tick(ModPlayer *m)
{
    int c;
    for (c = 0; c < MOD_CHANNELS; c++) m->voice[c].trigger = 0;
    if (++m->tick >= m->speed) {
        m->tick = 0;
        row(m);
    } else effects(m);
}
