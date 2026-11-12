#define _POSIX_C_SOURCE 200809L   /* (setenv) */
/* SPDX-License-Identifier: MIT
 * The music: a tracker module plays (and loops), its credit comes from credits.txt, and a
 * song without a module still gets the synthesiser. Uses tests/music/ (OFP_MUSIC_DIR). */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "platform/sdl2/music.h"

static double rms(int n, int *peak)
{
    double sum = 0;
    int i;
    *peak = 0;
    for (i = 0; i < n; i++) { int v = music_sample(); sum += (double)v * v; if (abs(v) > *peak) *peak = abs(v); }
    return sqrt(sum / n);
}

int main(int argc, char **argv)
{
    int peak, ok = 1;
    double a, b;
    if (argc < 2) { fprintf(stderr, "usage: test_music DIR\n"); return 2; }
    setenv("OFP_MUSIC_DIR", argv[1], 1);
    music_init();
    music_load("/nonexistent", NULL);
    music_volume(2);
    printf("classic: %s, credit \"%s\"\n", music_has_module(MUSIC_CLASSIC) ? "a module" : "NO MODULE", music_credit(MUSIC_CLASSIC));
    ok &= music_has_module(MUSIC_CLASSIC) && music_credit(MUSIC_CLASSIC)[0];
    music_play(MUSIC_CLASSIC);
    rms(22050, &peak);                                   /* (the fade in) */
    a = rms(22050 * 20, &peak);                          /* 20 s: longer than the tune, so it has looped */
    printf("  20 s of it: level %.0f, peak %d\n", a, peak);
    ok &= a > 200 && peak < 32000;
    printf("  just started: %d\n", music_started());
    printf("desert: %s\n", music_has_module(MUSIC_DESERT) ? "a module?!" : "no module: the synthesiser");
    ok &= !music_has_module(MUSIC_DESERT);
    music_play(MUSIC_DESERT);
    rms(22050 * 3, &peak);
    b = rms(22050 * 5, &peak);
    printf("  5 s of it: level %.0f\n", b);
    ok &= b > 200;
    printf(ok ? "music ok\n" : "music: FAILED\n");
    return !ok;
}
