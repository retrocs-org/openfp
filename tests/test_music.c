#define _POSIX_C_SOURCE 200809L   /* (setenv) */
/* SPDX-License-Identifier: MIT
 * The music: a tracker module plays (and loops), its credit comes from credits.txt, and a
 * song without a module still gets the synthesiser. Uses tests/music/ (OFP_MUSIC_DIR). */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "platform/sdl2/music.h"
#include "platform/sdl2/music_pcm.h"

static double rms(int n, int *peak)
{
    double sum = 0;
    int i;
    *peak = 0;
    for (i = 0; i < n; i++) { int v = music_sample(); sum += (double)v * v; if (abs(v) > *peak) *peak = abs(v); }
    return sqrt(sum / n);
}

static int check_loudness(void)
{
    int16_t quiet[22050], loud[22050], impulse[22050] = {0}, silent[100] = {0};
    int i;
    float a, b, c;
    for (i = 0; i < 22050; i++) {
        /* The first half is silence; it must not drive the adjustment. */
        quiet[i] = i < 11025 ? 0 : (i & 1 ? 2000 : -2000);
        loud[i] = i < 11025 ? 0 : (i & 1 ? 16000 : -16000);
    }
    a = music_pcm_loudness_gain(quiet, 22050, 22050);
    b = music_pcm_loudness_gain(loud, 22050, 22050);
    impulse[500] = -32768;
    c = music_pcm_loudness_gain(impulse, 22050, 22050);
    return fabs(a * 2000 - b * 16000) < 1 && a > 1 && b < 1 &&
           c * 32768 <= 30001 && music_pcm_loudness_gain(silent, 100, 22050) == 1;
}

static void test_callback(void *unused, Uint8 *output, int bytes)
{
    int i;
    int16_t *samples = (int16_t *)output;
    (void)unused;
    for (i = 0; i < bytes / 2; i++) samples[i] = (int16_t)music_sample();
}

static int check_background(const char *fixtures)
{
    SDL_AudioSpec wanted = {0};
    int cycle, ok = 1;
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    SDL_setenv("OFP_MUSIC_DIR", fixtures, 1);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return 0;
    wanted.freq = 22050; wanted.format = AUDIO_S16SYS;
    wanted.channels = 1; wanted.samples = 256; wanted.callback = test_callback;
    for (cycle = 0; cycle < 3; cycle++) {
        SDL_AudioDeviceID device;
        Uint32 until;
        int started = -1, i;
        music_init(); music_load("/nonexistent", NULL);
        device = SDL_OpenAudioDevice(NULL, 0, &wanted, NULL, 0);
        if (!device) { ok = 0; break; }
        music_set_device(device);
        music_play(MUSIC_CLASSIC);
        SDL_PauseAudioDevice(device, 0);
        until = SDL_GetTicks() + 3000;
        while (SDL_GetTicks() < until && started != MUSIC_CLASSIC) {
            started = music_started(); SDL_Delay(5);
        }
        ok &= started == MUSIC_CLASSIC && music_has_module(MUSIC_CLASSIC);
        /* Rapid changes, mute/unmute and teardown while a request is pending. */
        for (i = 0; i < 20; i++) {
            music_play(i % MUSIC_SONGS);
            music_volume(i % 2 ? 10 : 0);
            SDL_Delay(2);
            ok &= music_cached_tracks() <= 2;
        }
        music_close(); SDL_CloseAudioDevice(device);
        ok &= music_cached_tracks() == 0;
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return ok;
}

int main(int argc, char **argv)
{
    int peak, ok = 1;
    double a, b;
    if (argc < 2) { fprintf(stderr, "usage: test_music DIR\n"); return 2; }
    setenv("OFP_MUSIC_DIR", argv[1], 1);
    music_init();
    music_load("/nonexistent", NULL);
    ok &= music_cached_tracks() == 0 && music_cached_bytes() == 0;
    music_volume(0); music_play(MUSIC_CLASSIC);
    ok &= music_cached_tracks() == 0;
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
    /* Newly added songs also have a safe synthesizer fallback when assets are absent. */
    music_play(SOUNDTRACK_SNOW_THEME); rms(22050 * 2, &peak); ok &= peak > 0;
    music_play(SOUNDTRACK_CANNON_TUBE); rms(22050 * 2, &peak); ok &= peak > 0;
    music_close();
    if (argc > 2) {
        int i;
        size_t previous = 0, total = 0, maximum = 0;
        unsetenv("OFP_MUSIC_DIR");
        music_init(); music_load(argv[2], NULL); music_volume(10);
        ok &= music_cached_bytes() == 0;
        for (i = 0; i < SOUNDTRACK_COUNT; i++) {
            music_play(i); rms(22050 * 2, &peak);
            a = rms(22050 * 4, &peak);
            printf("%s: level %.0f, peak %d, %s\n", music_name(i), a, peak, music_credit(i));
            ok &= a > 100 && peak < 32000;
            if (i != BIOME_SCIFI) ok &= music_credit(i)[0] != 0;
            ok &= music_cached_tracks() <= 2;
            {
                size_t bytes = music_cached_bytes(), current = bytes - previous;
                total += current; previous = current;
                if (bytes > maximum) maximum = bytes;
            }
        }
        printf("decoded catalog: %zu bytes; maximum resident PCM: %zu bytes\n", total, maximum);
        ok &= maximum < total / 2;
        music_close();
        ok &= music_cached_tracks() == 0 && music_cached_bytes() == 0;
    }
    printf("sample/volume/cache checks: %s\n", ok ? "ok" : "FAILED");
    { int passed = check_loudness(); printf("loudness: %s\n", passed ? "ok" : "FAILED"); ok &= passed; }
    { int passed = check_background(argv[1]); printf("background playback: %s\n", passed ? "ok" : "FAILED"); ok &= passed; }
    printf(ok ? "music ok\n" : "music: FAILED\n");
    return !ok;
}
