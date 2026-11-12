/* SPDX-License-Identifier: MIT
 * The Amiga's music player (src/platform/amiga/modplay.c) against libxmp-lite (the desktop's): each of the
 * game's songs (src/platform/amiga/music_mods.h) played by both, its loudness every tenth of a second
 * compared. Our player is mixed here as Paula would play it: each channel's sample at 3546895 / period
 * samples a second, its loop after it, at its volume. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <xmp.h>
#include "platform/amiga/modplay.h"
#include "platform/amiga/music_mods.h"

#define RATE 22050
#define SECONDS 20

typedef struct { const unsigned char *p, *loop; unsigned long len, loop_len; double pos; int on; } Paula;

static double correlate(const double *a, const double *b, int n)
{
    double ma = 0, mb = 0, sab = 0, saa = 0, sbb = 0;
    int i;
    for (i = 0; i < n; i++) { ma += a[i]; mb += b[i]; }
    ma /= n; mb /= n;
    for (i = 0; i < n; i++) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); }
    return saa > 0 && sbb > 0 ? sab / sqrt(saa * sbb) : 0;
}

int main(void)
{
    static const char *names[MUSIC_SONGS] = { "classic", "desert", "jungle", "winter", "scifi", "menu" };
    static double ours[SECONDS * 10], theirs[SECONDS * 10];
    int song, failures = 0;
    for (song = 0; song < MUSIC_SONGS; song++) {
        ModPlayer m;
        Paula ch[MOD_CHANNELS];
        xmp_context x = xmp_create_context();
        int16_t buf[RATE / 10];
        long s, block = 0;
        double energy = 0, r;
        if (mod_open(&m, music_mods[song], music_sizes[song], NULL) != 0) { printf("%s: doesn't open\n", names[song]); failures++; continue; }
        memset(ch, 0, sizeof ch);
        for (s = 0; s < (long)RATE * SECONDS; s++) {
            int c;
            double mix = 0;
            if (s % (RATE / 50) == 0) {                   /* the vertical blank */
                mod_tick(&m);
                for (c = 0; c < MOD_CHANNELS; c++)
                    if (m.voice[c].trigger) {
                        ch[c].p = m.voice[c].data; ch[c].len = m.voice[c].length;
                        ch[c].loop = m.voice[c].loop; ch[c].loop_len = m.voice[c].loop_length;
                        ch[c].pos = 0; ch[c].on = 1;
                    }
            }
            for (c = 0; c < MOD_CHANNELS; c++) {
                Paula *p = &ch[c];
                if (!p->on || !m.voice[c].period) continue;
                mix += (signed char)p->p[(unsigned long)p->pos] * m.voice[c].volume / 64.0;
                p->pos += 3546895.0 / m.voice[c].period / RATE;
                if (p->pos >= p->len) {
                    if (p->loop) { p->p = p->loop; p->len = p->loop_len; p->pos = fmod(p->pos - p->len, p->loop_len); }
                    else p->on = 0;
                }
            }
            energy += mix * mix;
            if ((s + 1) % (RATE / 10) == 0) { ours[block++] = sqrt(energy / (RATE / 10)); energy = 0; }
        }
        if (xmp_load_module_from_memory(x, music_mods[song], (long)music_sizes[song]) != 0 ||
            xmp_start_player(x, RATE, XMP_FORMAT_MONO) != 0) { printf("%s: libxmp can't play it\n", names[song]); failures++; continue; }
        xmp_set_player(x, XMP_PLAYER_INTERP, XMP_INTERP_NEAREST);
        for (block = 0; block < SECONDS * 10; block++) {
            int i;
            double e = 0;
            xmp_play_buffer(x, buf, sizeof buf, 0);
            for (i = 0; i < RATE / 10; i++) e += (double)buf[i] * buf[i];
            theirs[block] = sqrt(e / (RATE / 10));
        }
        xmp_end_player(x); xmp_release_module(x); xmp_free_context(x);
        r = correlate(ours, theirs, SECONDS * 10);
        printf("%-8s loudness against libxmp, every 0.1 s for %d s: correlation %.3f\n", names[song], SECONDS, r);
        if (r < 0.98) failures++;
    }
    if (failures) { printf("modplay: %d FAILED\n", failures); return 1; }
    printf("modplay ok\n");
    return 0;
}
