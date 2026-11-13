/* SPDX-License-Identifier: MIT */
#include "platform/sdl2/pref_path.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include "common/volume.h"
#include "platform/sdl2/audio.h"
#include "platform/sdl2/theme.h"
#include "platform/sdl2/music.h"
#include "platform/sdl2/music_pcm.h"

#define RATE      22050
#define VOICES    20
#define LOOP_ENGINE (VOICES - 1)
#define LOOP_HELI   (VOICES - 2)
#define LOOP_ENGINE2 (VOICES - 3)
#define LOOP_ENGINE3 (VOICES - 4)
#define LOOP_ENGINE4 (VOICES - 5)
#define LOOP_NEAR   (VOICES - 6)       /* the nearest other tank's engine: louder as it comes */

enum { S_EXPLODE, S_EXPLODE_SMALL, S_CANNON, S_MG, S_ROCKET, S_MORTAR, S_HIT, S_SPLAT,
       S_PICKUP, S_MINE, S_HELI, S_ENGINE, S_SPLASH, S_LAUGH, S_COUNT };
static const char *names[S_COUNT] = { "explode", "explode_small", "cannon", "mg", "rocket", "mortar",
                                      "hit", "splat", "pickup", "mine", "heli", "engine", "splash" };

typedef struct { Sint16 *data; Uint32 len; } Sample;          /* len in samples */
typedef struct { const Sample *s; Uint32 pos, step; int vol, loop, pan; } Voice;  /* pos/step 16.16; pan -256 left .. 256 right */

static SDL_AudioDeviceID dev;
static Sample  samples[S_COUNT];
static Voice   voices[VOICES];
static uint16_t seen[MAX_EFFECTS];
static uint16_t bonus_seen[MAX_TANKS][2];
static uint16_t laughed_at[MAX_TANKS];
static int     muted, last_msg_t, last_mines, last_lives, last_msg_t2, last_mines2, ears2 = -1, ears_n = 0;
static int     effects_gain = 256;
static int     cur_pan;                  /* where the next sound comes from (set while the effects are heard) */
static int     stereo;                   /* the device has two channels */

static Sint16 clip16(int v) { return (Sint16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

static void mix(void *ud, Uint8 *stream, int len)
{
    Sint16 *out = (Sint16 *)stream;
    int frames = len / (stereo ? 4 : 2), i, v;
    (void)ud;
    for (i = 0; i < frames; i++) {
        int left = 0, right = 0, m;
        for (v = 0; v < VOICES; v++) {
            Voice *vc = &voices[v];
            Uint32 idx;
            int smp;
            if (!vc->s || !vc->vol) continue;
            idx = vc->pos >> 16;
            if (idx >= vc->s->len) {
                if (vc->loop) { vc->pos -= vc->s->len << 16; idx = vc->pos >> 16; }
                else { vc->s = NULL; continue; }
            }
            smp = vc->s->data[idx] * vc->vol / 256;
            /* (the pan: full in the near ear, less in the far one) */
            left += vc->pan > 0 ? smp * (256 - vc->pan) / 256 : smp;
            right += vc->pan < 0 ? smp * (256 + vc->pan) / 256 : smp;
            vc->pos += vc->step;
        }
        m = music_sample();
        left = left * 3 / 5 * effects_gain / 256 + m;
        right = right * 3 / 5 * effects_gain / 256 + m;
        if (muted) left = right = 0;
        if (stereo) { out[2 * i] = clip16(left); out[2 * i + 1] = clip16(right); }
        else out[i] = clip16((left + right) / 2);
    }
}

static void free_samples(void)
{
    int i;
    for (i = 0; i < S_COUNT; i++) { SDL_free(samples[i].data); samples[i].data = NULL; samples[i].len = 0; }
}

static int load_one(int id)
{
    char path[1300];
    SDL_AudioSpec spec;
    Uint8 *buf;
    Uint32 blen;
    SDL_AudioCVT cvt;
    snprintf(path, sizeof path, "%s/sounds/default/%s.wav", theme_data_dir(), names[id]);
    if (!SDL_LoadWAV(path, &spec, &buf, &blen)) return -1;
    if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 1, RATE) < 0) {
        SDL_FreeWAV(buf);
        return -1;
    }
    cvt.len = (int)blen;
    cvt.buf = (Uint8 *)SDL_malloc((size_t)blen * (size_t)(cvt.len_mult > 0 ? cvt.len_mult : 1) + 16);
    if (!cvt.buf) { SDL_FreeWAV(buf); return -1; }
    memcpy(cvt.buf, buf, blen);
    SDL_FreeWAV(buf);
    if (cvt.needed && SDL_ConvertAudio(&cvt) < 0) { SDL_free(cvt.buf); return -1; }
    samples[id].data = (Sint16 *)cvt.buf;
    samples[id].len = (Uint32)(cvt.needed ? cvt.len_cvt : cvt.len) / 2;
    return 0;
}

static void load_samples(void)
{
    char path[1300];
    int i, count = 0;
    memset(voices, 0, sizeof voices);
    for (i = 0; i < S_LAUGH; i++) load_one(i);
    snprintf(path, sizeof path, "%s/sounds/original/laugh-evil-1.ogg", theme_data_dir());
    samples[S_LAUGH].data = music_pcm_load(path, RATE, &count);
    samples[S_LAUGH].len = (Uint32)count;
}

int audio_init(int effects_level, int music_level)
{
    SDL_AudioSpec want, have;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return -1;
    SDL_zero(want);
    want.freq = RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;                                   /* (stereo: where things happen, left or right) */
    want.samples = 512;
    want.callback = mix;
    music_init();
    music_volume(music_level);
    {   /* the music's modules, if there are any (before the sound starts: the mixer reads them) */
        char *pref = ofp_pref_path();
        music_load(theme_data_dir(), pref);
        SDL_free(pref);
    }
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) { music_close(); return -1; }
    music_set_device(dev);
    stereo = have.channels == 2;
    load_samples();
    audio_set_volume(effects_level);
    SDL_PauseAudioDevice(dev, 0);
    return 0;
}

void audio_shutdown(void)
{
    music_close();
    if (dev) SDL_CloseAudioDevice(dev);
    dev = 0;
    free_samples();
}

void audio_set_volume(int level)
{
    if (dev) SDL_LockAudioDevice(dev);
    effects_gain = volume_clamp(level) * 256 / VOLUME_MAX;
    if (dev) SDL_UnlockAudioDevice(dev);
}

void audio_toggle_mute(void)
{
    if (dev) SDL_LockAudioDevice(dev);
    muted = !muted;
    if (dev) SDL_UnlockAudioDevice(dev);
}
int  audio_muted(void) { return muted || !dev; }

void audio_reset(void)
{
    int i;
    for (i = 0; i < MAX_EFFECTS; i++) seen[i] = 0;
    memset(laughed_at, 0, sizeof laughed_at);
    memset(bonus_seen, 0, sizeof bonus_seen);
    last_msg_t = last_msg_t2 = 0;
    last_mines = last_lives = last_mines2 = -1;
    ears2 = -1;
}

void audio_quiet(void)
{
    if (!dev) return;
    SDL_LockAudioDevice(dev);
    voices[LOOP_ENGINE].vol = voices[LOOP_HELI].vol = voices[LOOP_ENGINE2].vol = 0;
    voices[LOOP_ENGINE3].vol = voices[LOOP_ENGINE4].vol = voices[LOOP_NEAR].vol = 0;
    SDL_UnlockAudioDevice(dev);
}

/* one-shot on a free voice (or steal the quietest); call with the device locked */
static void play(int id, int vol, int pitch256)
{
    int v, best = 0, bestvol = 1 << 30;
    if (!samples[id].data || vol <= 0) return;
    for (v = 0; v < LOOP_NEAR; v++) {
        if (!voices[v].s) { best = v; break; }
        if (voices[v].vol < bestvol) { bestvol = voices[v].vol; best = v; }
    }
    voices[best].s = &samples[id];
    voices[best].pos = 0;
    voices[best].step = (Uint32)pitch256 << 8;
    voices[best].vol = vol > 256 ? 256 : vol;
    voices[best].loop = 0;
    voices[best].pan = cur_pan;
}

void audio_menu_feedback(int confirm)
{
    if (!dev) return;
    SDL_LockAudioDevice(dev);
    cur_pan = 0;
    play(confirm ? S_PICKUP : S_HIT, confirm ? 80 : 48, confirm ? 312 : 384);
    SDL_UnlockAudioDevice(dev);
}

void audio_skull_laugh(int tank, uint16_t losses)
{
    if (!dev || laughed_at[tank] == losses) return;
    laughed_at[tank] = losses;
    SDL_LockAudioDevice(dev);
    cur_pan = 0;
    play(S_LAUGH, 220, 256);
    SDL_UnlockAudioDevice(dev);
}

static void loop(int voice, int id, int vol, int pitch256)
{
    Voice *vc = &voices[voice];
    if (!samples[id].data) return;
    if (vc->s != &samples[id]) { vc->s = &samples[id]; vc->pos = 0; }
    vc->loop = 1;
    vc->step = (Uint32)pitch256 << 8;
    vc->vol = vol < 0 ? 0 : vol > 256 ? 256 : vol;
}

static int falloff1(const Game *g, int listener, fx x, fx y, int range)
{
    const Tank *t = &g->tanks[listener];
    int dx = fx_to_int(x - t->x), dy = fx_to_int(y - t->y);
    int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    return d >= range ? 0 : 256 - d * 256 / range;
}

static int falloff(const Game *g, int listener, fx x, fx y, int range)   /* the nearest ear */
{
    int a = falloff1(g, listener, x, y, range), b = ears2 >= 0 ? falloff1(g, ears2, x, y, range) : 0, i;
    if (b > a) a = b;
    for (i = 2; i < ears_n; i++) {
        b = falloff1(g, i, x, y, range);
        if (b > a) a = b;
    }
    return a;
}

static int near_engines = 1;
void audio_set_near_engines(int on) { near_engines = on; }

/* the nearest other tank (to the first listener), moving, within 12 tiles: its engine, louder as it comes,
 * a little deeper than yours (call with the device locked) */
static void near_engine(const Game *g, int listeners)
{
    const Tank *me = &g->tanks[0], *best = NULL;
    int i, best_d = 12 * TILE_SIZE;
    if (near_engines && me->active)
        for (i = listeners; i < g->num_tanks; i++) {
            const Tank *o = &g->tanks[i];
            int dx, dy, d;
            if (!o->active || o->respawn || o->speed == 0) continue;
            dx = fx_to_int(o->x) - fx_to_int(me->x); dy = fx_to_int(o->y) - fx_to_int(me->y);
            if (abs(dx) >= best_d || abs(dy) >= best_d) continue;
            d = abs(dx) > abs(dy) ? abs(dx) + abs(dy) / 2 : abs(dy) + abs(dx) / 2;   /* (near enough a distance) */
            if (d < best_d) { best_d = d; best = o; }
        }
    if (best) {
        fx top = tank_stats[best->cls].max_fwd;
        int sp = top ? (int)((int64_t)fx_abs(best->speed) * 256 / top) : 0;
        int pitch = 230 * (200 + sp * 120 / 256) / 256, vol = (12 * TILE_SIZE - best_d) * 90 / (12 * TILE_SIZE);
        loop(LOOP_NEAR, S_ENGINE, vol, pitch);
    } else voices[LOOP_NEAR].vol = 0;
}

static void engine_for(const Tank *me, int voice)
{
    if (me->active && !me->respawn) {
        static const int base[TANK_CLASS_COUNT] = { 300, 256, 210 };
        fx top = tank_stats[me->cls].max_fwd;
        int sp = top ? (int)((int64_t)fx_abs(me->speed) * 256 / top) : 0;
        int turning = me->angle != me->prev_angle && sp < 80;   /* pivoting on its tracks */
        int pitch = base[me->cls] * (200 + sp * 120 / 256) / 256, vol = 70 + sp * 70 / 256;
        if (turning) { pitch = pitch * 88 / 100; vol += 30; }   /* the engine labours and drops */
        loop(voice, S_ENGINE, vol, pitch);
    } else {
        voices[voice].vol = 0;
    }
}

void audio_update_n(const Game *g, int listeners)
{
    ears_n = listeners > 2 ? listeners : 0;
    audio_update2(g, 0, listeners >= 2 ? 1 : -1);
    if (!dev) return;
    SDL_LockAudioDevice(dev);
    if (listeners >= 3) engine_for(&g->tanks[2], LOOP_ENGINE3); else voices[LOOP_ENGINE3].vol = 0;
    if (listeners >= 4) engine_for(&g->tanks[3], LOOP_ENGINE4); else voices[LOOP_ENGINE4].vol = 0;
    near_engine(g, listeners > 0 ? listeners : 1);
    SDL_UnlockAudioDevice(dev);
}

void audio_update2(const Game *g, int listener, int listener2)
{
    ears2 = listener2;
    audio_update(g, listener);
    if (!dev) return;
    SDL_LockAudioDevice(dev);
    if (listener2 >= 0) {
        const Tank *p2 = &g->tanks[listener2];
        engine_for(p2, LOOP_ENGINE2);
        if (p2->msg_t > last_msg_t2 && (p2->msg == MSG_FLAG || p2->msg == MSG_EXTRA_LIFE || p2->msg == MSG_WIN))
            play(S_PICKUP, 230, 256);
        last_msg_t2 = p2->msg_t;
        if (last_mines2 >= 0 && p2->mines < last_mines2 && !p2->respawn) play(S_MINE, 160, 256);
        last_mines2 = p2->mines;
    } else {
        voices[LOOP_ENGINE2].vol = 0;
    }
    SDL_UnlockAudioDevice(dev);
}

void audio_update(const Game *g, int listener)
{
    int i;
    const Tank *me = &g->tanks[listener];
    if (!dev) return;
    SDL_LockAudioDevice(dev);
    for (i = 0; i < MAX_EFFECTS; i++) {
        const Effect *e = &g->effects[i];
        int near;
        if (!e->active || e->delay || seen[i] == e->serial) continue;
        seen[i] = e->serial;
        near = falloff(g, listener, e->x, e->y, e->kind == EFF_BLAST ? 520 : 360);
        {   /* left or right of us: about the width of the view gets most of the way into one ear */
            int dx = fx_to_int(e->x) - fx_to_int(me->x);
            cur_pan = dx * 200 / 150;
            if (cur_pan > 200) cur_pan = 200;
            if (cur_pan < -200) cur_pan = -200;
        }
        if ((e->kind == EFF_PUFF || e->kind == EFF_HIT || e->kind == EFF_EXPLOSION) &&
            map_get(&g->map, fx_to_int(e->x) >> TILE_SHIFT, fx_to_int(e->y) >> TILE_SHIFT) == TILE_WATER) {
            play(S_SPLASH, e->kind == EFF_EXPLOSION ? near : near / 2, 230 + (int)(e->serial % 50));
            continue;                                    /* it went in the water */
        }
        switch (e->kind) {
        case EFF_BLAST:     play(S_EXPLODE, near, 230 + (int)(g->tick % 40)); break;
        case EFF_EXPLOSION: play(S_EXPLODE_SMALL, near * 3 / 4, 220 + (int)(e->serial % 70)); break;
        case EFF_HIT:
        case EFF_SPARK:     play(S_HIT, near / 2, 230 + (int)(e->serial % 50)); break;
        case EFF_SPLAT:     play(S_SPLAT, near, 256); break;
        case EFF_CHEER:     play(S_PICKUP, near / 2, 256); break;
        case EFF_MUZZLE:
            switch (e->sub) {
            case SHOT_BULLET: play(S_MG, near * 2 / 3, 240 + (int)(e->serial % 40)); break;
            case SHOT_ROCKET: play(S_ROCKET, near * 2 / 3, 256); break;
            case SHOT_MORTAR: play(S_MORTAR, near, 256); break;
            case SHOT_HEAVY:  play(S_CANNON, near, 200); break;
            default:          play(S_CANNON, near * 3 / 4, 256 + (int)(e->serial % 30)); break;
            }
            break;
        default: break;
        }
    }
    for (i = 0; i < g->num_tanks; i++) {
        const Tank *t = &g->tanks[i];
        int k;
        for (k = 0; k < 2; k++) {
            uint16_t ticks = k ? t->rapid_ticks : t->boost_ticks;
            if (ticks > bonus_seen[i][k]) play(S_PICKUP, falloff(g, listener, t->x, t->y, 200), k ? 448 : 340);
            bonus_seen[i][k] = ticks;
        }
    }
    /* things the HUD announces */
    cur_pan = 0;                                         /* (our own news: in the middle) */
    if (me->msg_t > last_msg_t && (me->msg == MSG_FLAG || me->msg == MSG_EXTRA_LIFE || me->msg == MSG_WIN))
        play(S_PICKUP, 230, me->msg == MSG_EXTRA_LIFE ? 300 : 256);
    last_msg_t = me->msg_t;
    cur_pan = 0;
    if (last_mines >= 0 && me->mines < last_mines && !me->respawn) play(S_MINE, 160, 256);
    last_mines = me->mines;
    (void)last_lives;

    engine_for(me, LOOP_ENGINE);               /* each tank drones differently and revs with speed */
    {   /* the nearest helicopter */
        int best = 0;
        for (i = 0; i < MAX_HELIS; i++)
            if (g->helis[i].active) {
                int f = falloff(g, listener, g->helis[i].x, g->helis[i].y, 420);
                if (f > best) best = f;
            }
        if (best) loop(LOOP_HELI, S_HELI, best * 3 / 4, 256);
        else voices[LOOP_HELI].vol = 0;
    }
    if (listener == 0) near_engine(g, 1);
    SDL_UnlockAudioDevice(dev);
}
