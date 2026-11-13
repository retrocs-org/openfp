/* SPDX-License-Identifier: MIT
 * On-demand sampled music, tracker overrides and a small synthesizer fallback.
 * File decoding and reclamation run outside the audio callback.
 */
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/volume.h"
#include "platform/sdl2/music.h"
#include "platform/sdl2/music_pcm.h"
#include <xmp.h>

#define RATE 22050

typedef struct {
    int         bpm;
    int         root;            /* MIDI note of the key */
    const int  *scale;           /* 7 steps */
    int         chords[8];       /* scale degrees, 2 bars each */
    const char *bass;            /* 16 steps a bar: digit = chord note (0 root, 2 third, 4 fifth, 7 octave), - rest */
    const char *lead;            /* 32 steps (2 bars): digit = scale step above the chord root, - rest, = hold */
    const char *drums;           /* 16 steps: k kick, s snare, h hat, t tom, - rest */
    int         wave;            /* lead: 0 pulse, 1 triangle, 2 bell */
    int         pad;             /* a soft chord underneath */
} Song;

static const int MINOR[7]    = { 0, 2, 3, 5, 7, 8, 10 };
static const int PHRYGIAN[7] = { 0, 1, 4, 5, 7, 8, 10 };   /* (with a raised third: the desert) */
static const int DORIAN[7]   = { 0, 2, 3, 5, 7, 9, 10 };
static const int AEOLIAN[7]  = { 0, 2, 3, 5, 7, 8, 10 };

static const Song songs[MUSIC_SONGS] = {
    /* classic: a march */
    { 112, 45, MINOR,    { 0, 5, 3, 4, 0, 5, 6, 4 },
      "0-0-4-0-0-0-4-7-", "0-2-4-=-2-1-0---4-4-5-4-2---0---", "k-h-s-hkk-h-s-hh", 0, 1 },
    /* desert: slow and hot */
    { 96, 50, PHRYGIAN,  { 0, 1, 0, 6, 0, 1, 5, 0 },
      "0---0-4-0---7-4-", "4-=-5-4-1-=-0---4-5-6-5-4-1-=---", "k--hs--hk-k-s--t", 1, 1 },
    /* jungle: drums first */
    { 120, 43, DORIAN,   { 0, 0, 3, 3, 4, 3, 0, 6 },
      "0--00--40--00-7-", "------0-2-4-----5-4-2-0---------", "k-tkt-s-k-ttk-st", 1, 0 },
    /* winter: still and cold */
    { 84, 52, AEOLIAN,   { 0, 5, 2, 6, 0, 3, 4, 4 },
      "0-------4-------", "4---=---2---0---5---4---=-------", "k-------s-------", 2, 1 },
    /* sci-fi: arpeggios */
    { 128, 40, MINOR,    { 0, 0, 5, 5, 3, 3, 4, 6 },
      "0-07-0-70-07-0-7", "0247024702470247424742474247424-", "k-hhs-hhk-hhs-hk", 0, 1 },
    /* the menus: the march, quieter */
    [SOUNDTRACK_CREDITS] = { 100, 45, MINOR, { 0, 3, 4, 0, 5, 3, 6, 4 },
      "0---4---0---4---", "0---2---4---=---2---1---0-------", "k-------s-------", 1, 1 },
    [MUSIC_MENU] = { 100, 45, MINOR,    { 0, 3, 4, 0, 5, 3, 6, 4 },
      "0---4---0---4---", "0---2---4---=---2---1---0-------", "k-------s-------", 1, 1 },
};

static SDL_atomic_t want_song;         /* -1 = silence */
static SDL_atomic_t want_volume;       /* 0..10 */
static int song = -1;                  /* the mixer thread's current song */
static uint32_t pos;                   /* samples into the song */
static uint32_t noise = 0x12345u;
static float fade;                     /* 0..1, for changing songs without a click */

/* the voices: phase accumulators and envelopes */
static double ph_bass, ph_lead, ph_pad[3], ph_kick;
static float env_bass, env_lead, env_kick, env_snare, env_hat, env_tom, kick_f;
static double f_bass, f_lead, f_pad[3];
static int last_step = -1;

static double note_hz(int midi) { return 440.0 * pow(2.0, (midi - 69) / 12.0); }

static int scale_note(const Song *s, int degree)   /* scale degree (any) to MIDI */
{
    int oct = degree >= 0 ? degree / 7 : -((-degree + 6) / 7);
    int d = degree - oct * 7;
    return s->root + oct * 12 + s->scale[d];
}

/* a new step of the song: what starts now */
static void step(const Song *s, int stp)
{
    int bar = stp / 16, in_bar = stp % 16, chord = s->chords[(bar / 2) % 8];
    char b = s->bass[in_bar], l = s->lead[(stp % 32)], d = s->drums[in_bar];
    if (b >= '0' && b <= '9') {
        int k = b - '0';
        f_bass = note_hz(scale_note(s, chord + (k == 7 ? 7 : k)) - 12);
        env_bass = 1.0f;
    }
    if (l >= '0' && l <= '9') {
        f_lead = note_hz(scale_note(s, chord + (l - '0')) + 12);
        env_lead = 1.0f;
    }
    if (in_bar == 0) {                                     /* the pad: the chord, a bar at a time */
        f_pad[0] = note_hz(scale_note(s, chord));
        f_pad[1] = note_hz(scale_note(s, chord + 2));
        f_pad[2] = note_hz(scale_note(s, chord + 4));
    }
    if (d == 'k') { env_kick = 1.0f; kick_f = 140.0f; ph_kick = 0; }
    if (d == 's') env_snare = 1.0f;
    if (d == 'h') env_hat = 0.6f;
    if (d == 't') { env_tom = 1.0f; kick_f = 220.0f; ph_kick = 0; }
}

/* ---- tracker modules: data/music/<song>.mod (.xm, .s3m, .it), or your own in the music
 * folder beside settings.ini; libxmp-lite plays them. A song without one: the synthesiser. */
static const char *const song_file[MUSIC_SONGS] = {
    "classic", "desert", "jungle", "winter", "scifi", "volcanic", "wasteland", "moon", "swamp", "menu", "credits", "cannon-tube", "snow-theme",
    "space-atmosphere", "new-beginning", "lost-in-the-desert", "desert-of-dreams-mix", "urban-jungle-2061", "jungle-pro-sensory", "iron-wasteland"
};
static const struct { const char *file, *title, *author; } tracks[MUSIC_SONGS] = {
#define TRACK(id, file, world, title, author) [id] = { file, title, author },
    OFP_SOUNDTRACK(TRACK)
#undef TRACK
};
typedef struct {
    int16_t *pcm;
    int samples;
    float gain;
    xmp_context module;
    char credit[120];
} Track;

/* Only the playing track and a prepared transition are resident. */
static Track *loaded[MUSIC_SONGS];
static SDL_atomic_t ready[MUSIC_SONGS], started_song;
static SDL_atomic_t stopping;
static SDL_AudioDeviceID music_device;
static SDL_Thread *loader;
static SDL_mutex *loader_lock;
static SDL_cond *loader_wake;
static char track_dir[1100], user_dir[1100];
static int have_user_dir;
static int16_t mod_buf[512];
static int mod_n, mod_at;

const char *music_name(int which) { return which >= 0 && which < MUSIC_SONGS ? song_file[which] : ""; }

static void free_track(Track *track)
{
    if (!track) return;
    if (track->module) {
        xmp_end_player(track->module);
        xmp_free_context(track->module);
    }
    SDL_free(track->pcm);
    SDL_free(track);
}

static int load_module(Track *track, int k, const char *dir, char *file, size_t size)
{
    static const char *ext[] = { ".mod", ".xm", ".s3m", ".it" };
    xmp_context context = xmp_create_context();
    int e;
    if (!context) return 0;
    for (e = 0; e < 4; e++) {
        char path[1200];
        struct xmp_module_info mi;
        snprintf(path, sizeof path, "%.1100s%s%s", dir, song_file[k], ext[e]);
        if (xmp_load_module(context, path) != 0) continue;
        if (xmp_start_player(context, RATE, XMP_FORMAT_MONO) != 0) { xmp_release_module(context); continue; }
        track->module = context;
        snprintf(file, size, "%s%s", song_file[k], ext[e]);
        xmp_get_module_info(context, &mi);
        snprintf(track->credit, sizeof track->credit, "%s", mi.mod && mi.mod->name[0] ? mi.mod->name : song_file[k]);
        return 1;
    }
    xmp_free_context(context);
    return 0;
}

/* credits.txt: file | title | author | licence. Read only while preparing a track. */
static void load_credit(Track *track, int which, const char *dir, const char *file)
{
    char path[1200], line[400];
    FILE *f;
    snprintf(path, sizeof path, "%.1100scredits.txt", dir);
    if (!(f = fopen(path, "r"))) return;
    while (fgets(line, sizeof line, f)) {
        char *field[4], *p = line;
        int n = 0, k;
        if (line[0] == '#' || line[0] == '\n') continue;
        while (n < 4) {
            char *bar = strchr(p, '|');
            while (*p == ' ') p++;
            field[n++] = p;
            if (!bar) break;
            *bar = 0; p = bar + 1;
        }
        for (k = 0; k < n; k++) {
            size_t l = strlen(field[k]);
            while (l && (field[k][l - 1] == ' ' || field[k][l - 1] == '\n' || field[k][l - 1] == '\r')) field[k][--l] = 0;
        }
        if (n >= 3 && (!strcmp(field[0], file) || !strcmp(field[0], song_file[which])))
            snprintf(track->credit, sizeof track->credit, "%.50s BY %.40s%s%.20s%s", field[1], field[2], n > 3 ? " (" : "", n > 3 ? field[3] : "", n > 3 ? ")" : "");
    }
    fclose(f);
}

static Track *load_track(int k)
{
    Track *track = SDL_calloc(1, sizeof *track);
    char file[64] = "";
    const char *dir = track_dir;
    if (!track) return NULL;  /* The synthesizer remains available on allocation failure. */
    track->gain = 1.0f;
    if (have_user_dir && load_module(track, k, user_dir, file, sizeof file)) dir = user_dir;
    else {
        if (tracks[k].file) {
            char path[1200];
            snprintf(path, sizeof path, "%.1100soriginal/%s", track_dir, tracks[k].file);
            track->pcm = music_pcm_load(path, RATE, &track->samples);
            if (track->pcm) {
                track->gain = music_pcm_loudness_gain(track->pcm, track->samples, RATE);
                snprintf(file, sizeof file, "%s", tracks[k].file);
                snprintf(track->credit, sizeof track->credit, "%s BY %s", tracks[k].title, tracks[k].author);
            }
        }
        if (!track->pcm) load_module(track, k, track_dir, file, sizeof file);
    }
    if (file[0]) load_credit(track, k, dir, file);
    return track;
}

static void lock_audio(void) { if (music_device) SDL_LockAudioDevice(music_device); }
static void unlock_audio(void) { if (music_device) SDL_UnlockAudioDevice(music_device); }

static void prepare_track(int which)
{
    Track *retired[MUSIC_SONGS], *track;
    int k;
    if (which < 0 || !SDL_AtomicGet(&want_volume) || SDL_AtomicGet(&ready[which])) return;
    /* Detach unused data under the device lock; free it after releasing the lock. */
    lock_audio();
    for (k = 0; k < MUSIC_SONGS; k++) {
        retired[k] = NULL;
        if (k != song) {
            retired[k] = loaded[k]; loaded[k] = NULL;
            SDL_AtomicSet(&ready[k], 0);
        }
    }
    unlock_audio();
    for (k = 0; k < MUSIC_SONGS; k++) free_track(retired[k]);
    track = load_track(which);
    if (SDL_AtomicGet(&stopping) || which != SDL_AtomicGet(&want_song)) { free_track(track); return; }
    lock_audio();
    loaded[which] = track;
    SDL_AtomicSet(&ready[which], 1);
    unlock_audio();
}

static int load_worker(void *unused)
{
    (void)unused;
    SDL_LockMutex(loader_lock);
    while (!SDL_AtomicGet(&stopping)) {
        int which = SDL_AtomicGet(&want_song);
        if (which < 0 || !SDL_AtomicGet(&want_volume) || SDL_AtomicGet(&ready[which]))
            SDL_CondWait(loader_wake, loader_lock);
        else {
            SDL_UnlockMutex(loader_lock);
            prepare_track(which);
            SDL_LockMutex(loader_lock);
        }
    }
    SDL_UnlockMutex(loader_lock);
    return 0;
}

static void request_track(void)
{
    if (loader) {
        SDL_LockMutex(loader_lock);
        SDL_CondSignal(loader_wake);
        SDL_UnlockMutex(loader_lock);
    } else prepare_track(SDL_AtomicGet(&want_song));  /* Offline previews and no-thread fallback. */
}

void music_play(int which)
{
    which = which >= 0 && which < MUSIC_SONGS ? which : -1;
    if (SDL_AtomicSet(&want_song, which) != which || (which >= 0 && !SDL_AtomicGet(&ready[which]))) request_track();
}

void music_volume(int level)
{
    level = volume_clamp(level);
    if (!SDL_AtomicSet(&want_volume, level) && level) request_track();
}

void music_load(const char *data_dir, const char *preferences_dir)
{
    snprintf(track_dir, sizeof track_dir, "%.1080s/music/", data_dir);
    snprintf(user_dir, sizeof user_dir, "%.1080smusic/", preferences_dir ? preferences_dir : "");
    have_user_dir = preferences_dir != NULL || getenv("OFP_MUSIC_DIR") != NULL;
    if (getenv("OFP_MUSIC_DIR")) snprintf(user_dir, sizeof user_dir, "%.1080s/", getenv("OFP_MUSIC_DIR"));
}

void music_set_device(SDL_AudioDeviceID device)
{
    music_device = device;
    loader_lock = SDL_CreateMutex();
    loader_wake = SDL_CreateCond();
    if (loader_lock && loader_wake) loader = SDL_CreateThread(load_worker, "music-loader", NULL);
    if (!loader) {
        if (loader_wake) SDL_DestroyCond(loader_wake);
        if (loader_lock) SDL_DestroyMutex(loader_lock);
        loader_wake = NULL; loader_lock = NULL;
    }
}

int music_has_module(int which)
{
    int result;
    lock_audio();
    result = which >= 0 && which < MUSIC_SONGS && loaded[which] && loaded[which]->module;
    unlock_audio();
    return result;
}

const char *music_credit(int which)
{
    static char copy[120];  /* Called by the main thread, never by the mixer. */
    lock_audio();
    snprintf(copy, sizeof copy, "%s", which >= 0 && which < MUSIC_SONGS && loaded[which] ? loaded[which]->credit : "");
    unlock_audio();
    return copy;
}

size_t music_cached_bytes(void)
{
    size_t bytes = 0;
    int k;
    lock_audio();
    for (k = 0; k < MUSIC_SONGS; k++) if (loaded[k]) bytes += (size_t)loaded[k]->samples * sizeof(int16_t);
    unlock_audio();
    return bytes;
}

int music_cached_tracks(void)
{
    int k, count = 0;
    lock_audio();
    for (k = 0; k < MUSIC_SONGS; k++) count += loaded[k] != NULL;
    unlock_audio();
    return count;
}

int music_started(void) { return SDL_AtomicSet(&started_song, -1); }

static int module_sample(int k)
{
    if (mod_at >= mod_n) {                               /* the next stretch (the song loops for ever) */
        if (xmp_play_buffer(loaded[k]->module, mod_buf, (int)sizeof mod_buf, 0) != 0) { mod_n = 0; return 0; }
        mod_n = (int)(sizeof mod_buf / sizeof mod_buf[0]);
        mod_at = 0;
    }
    return mod_buf[mod_at++];
}

int music_sample(void)
{
    const Song *s;
    float out, rnd, gain, dt = 1.0f / RATE;
    int stp, ws = SDL_AtomicGet(&want_song);
    int level = SDL_AtomicGet(&want_volume);
    if (ws >= 0 && !SDL_AtomicGet(&ready[ws])) ws = song; /* Keep playing while the loader works. */
    if (ws != song) {                                      /* fade out, then start the new one */
        fade -= 0.0005f;
        if (fade <= 0.0f) {
            fade = 0.0f; song = ws; pos = 0; last_step = -1; env_bass = env_lead = 0;
            if (song >= 0 && loaded[song] && loaded[song]->module) { xmp_restart_module(loaded[song]->module); mod_n = mod_at = 0; }
            SDL_AtomicSet(&started_song, song);
        }
    } else if (fade < 1.0f) fade += 0.0005f;
    if (song < 0 || !level) return 0;
    gain = fade * ((float)level / VOLUME_MAX);
    if (loaded[song] && loaded[song]->module) {                                  /* a module: as it was written, at the chosen level */
        return (int)((float)module_sample(song) * gain * 0.42f);
    }
    if (loaded[song] && loaded[song]->pcm) {
        Track *track = loaded[song];
        int value = track->pcm[pos++];
        if (pos == (uint32_t)track->samples) pos = 0;
        return (int)((float)value * track->gain * gain * 0.42f);
    }
    s = &songs[song];
    if (!s->bpm) s = &songs[BIOME_SCIFI];
    stp = (int)((uint64_t)pos * (uint64_t)s->bpm * 4 / (60u * RATE));   /* 16th notes */
    if (stp != last_step) { last_step = stp; step(s, stp % 256); }
    pos++;
    noise = noise * 1664525u + 1013904223u;
    rnd = (float)(int32_t)noise / 2147483648.0f;

    /* bass: a soft square */
    ph_bass += f_bass / RATE; if (ph_bass >= 1.0) ph_bass -= 1.0;
    out = (ph_bass < 0.5 ? 0.22f : -0.22f) * env_bass;
    env_bass *= 0.99985f;
    /* lead: pulse, triangle or bell */
    ph_lead += f_lead / RATE; if (ph_lead >= 1.0) ph_lead -= 1.0;
    {
        float w = s->wave == 0 ? (ph_lead < 0.25 ? 1.0f : -0.33f)
                : s->wave == 1 ? (float)(ph_lead < 0.5 ? 4.0 * ph_lead - 1.0 : 3.0 - 4.0 * ph_lead)
                : (float)sin(6.2831853 * ph_lead) * 0.8f + (float)sin(6.2831853 * 3.0 * ph_lead) * 0.2f;
        out += w * 0.13f * env_lead;
        env_lead *= s->wave == 2 ? 0.99990f : 0.99975f;
    }
    /* pad */
    if (s->pad) {
        int k;
        for (k = 0; k < 3; k++) {
            ph_pad[k] += f_pad[k] / RATE; if (ph_pad[k] >= 1.0) ph_pad[k] -= 1.0;
            out += (float)(ph_pad[k] < 0.5 ? 4.0 * ph_pad[k] - 1.0 : 3.0 - 4.0 * ph_pad[k]) * 0.035f;
        }
    }
    /* drums: a falling sine for the kick and toms, noise for snare and hats */
    if (env_kick > 0.001f || env_tom > 0.001f) {
        ph_kick += kick_f * dt; if (ph_kick >= 1.0) ph_kick -= 1.0;
        out += (float)sin(6.2831853 * ph_kick) * (0.45f * env_kick + 0.25f * env_tom);
        kick_f *= 0.9997f;
        env_kick *= 0.9993f;
        env_tom *= 0.9995f;
    }
    out += rnd * (0.18f * env_snare + 0.06f * env_hat);
    env_snare *= 0.9990f;
    env_hat *= 0.9960f;

    out *= gain * 0.9f;
    return (int)(out * 9000.0f);
}

void music_init(void)
{
    SDL_AtomicSet(&want_song, -1);
    SDL_AtomicSet(&started_song, -1);
    SDL_AtomicSet(&stopping, 0);
    SDL_AtomicSet(&want_volume, 6);
    song = -1; fade = 0; pos = 0;
}

void music_close(void)
{
    int k;
    /* Pause first, but keep the device alive until the worker has released it. */
    if (music_device) SDL_PauseAudioDevice(music_device, 1);
    SDL_AtomicSet(&stopping, 1);
    if (loader) {
        SDL_LockMutex(loader_lock);
        SDL_CondSignal(loader_wake);
        SDL_UnlockMutex(loader_lock);
        SDL_WaitThread(loader, NULL);
        loader = NULL;
        SDL_DestroyCond(loader_wake); SDL_DestroyMutex(loader_lock);
        loader_wake = NULL; loader_lock = NULL;
    }
    for (k = 0; k < MUSIC_SONGS; k++) {
        free_track(loaded[k]); loaded[k] = NULL;
        SDL_AtomicSet(&ready[k], 0);
    }
    music_device = 0;
    song = -1; fade = 0;
    SDL_AtomicSet(&want_song, -1);
    SDL_AtomicSet(&started_song, -1);
}
