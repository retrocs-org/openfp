/* SPDX-License-Identifier: MIT
 * Music: a small synthesiser that plays a song for each world, made up as it goes from
 * a few lines of data (tempo, key, chords, and how the bass, the tune and the drums go).
 * No samples, no files: four voices (bass, lead, pad, drums) mixed into the sound
 * device's stream, a few dozen operations a sample. Loops for ever, 16 bars a turn.
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

static const Song songs[] = {
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

void music_play(int which)
{
    SDL_AtomicSet(&want_song, which >= 0 && which < MUSIC_SONGS ? which : -1);
}

void music_volume(int level) { SDL_AtomicSet(&want_volume, volume_clamp(level)); }

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
    "classic", "desert", "jungle", "winter", "scifi", "volcanic", "wasteland", "moon", "swamp", "menu", "credits"
};
static const struct { const char *file, *title, *author; } tracks[MUSIC_SONGS] = {
#define TRACK(id, file, world, title, author) [id] = { file, title, author },
    OFP_SOUNDTRACK(TRACK)
#undef TRACK
};
static int16_t *pcm[MUSIC_SONGS];
static int pcm_samples[MUSIC_SONGS];
static int user_track[MUSIC_SONGS];
static char loaded_file[MUSIC_SONGS][32];

const char *music_name(int which) { return song_file[which]; }
static xmp_context mod_ctx[MUSIC_SONGS];
static int mod_ok[MUSIC_SONGS];
static int16_t mod_buf[512];
static int mod_n, mod_at;
static char credit[MUSIC_SONGS][120];                   /* "TITLE BY AUTHOR (LICENCE)" from credits.txt */
static char mod_title[MUSIC_SONGS][64];
static SDL_atomic_t started_song;                       /* (a song just began: the game shows its credit) */

static int load_module(int k, const char *dir)
{
    static const char *ext[] = { ".mod", ".xm", ".s3m", ".it" };
    int e;
    for (e = 0; e < 4; e++) {
        char path[1200];
        struct xmp_module_info mi;
        snprintf(path, sizeof path, "%.1100s%s%s", dir, song_file[k], ext[e]);
        if (xmp_load_module(mod_ctx[k], path) != 0) continue;
        if (xmp_start_player(mod_ctx[k], RATE, XMP_FORMAT_MONO) != 0) { xmp_release_module(mod_ctx[k]); continue; }
        snprintf(loaded_file[k], sizeof loaded_file[k], "%s%s", song_file[k], ext[e]);
        xmp_get_module_info(mod_ctx[k], &mi);
        snprintf(mod_title[k], sizeof mod_title[k], "%s", mi.mod && mi.mod->name[0] ? mi.mod->name : song_file[k]);
        return 1;
    }
    return 0;
}

/* credits.txt: one line a song, "file | title | author | licence" (# for comments) */
static void load_credits(const char *dir, int user)
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
            *bar = 0;
            p = bar + 1;
        }
        for (k = 0; k < n; k++) {                        /* (trim the ends) */
            size_t l = strlen(field[k]);
            while (l && (field[k][l - 1] == ' ' || field[k][l - 1] == '\n' || field[k][l - 1] == '\r')) field[k][--l] = 0;
        }
        if (n < 3) continue;
        for (k = 0; k < MUSIC_SONGS; k++)
            if (user_track[k] == user && loaded_file[k][0] &&
                (!strcmp(field[0], loaded_file[k]) || !strcmp(field[0], song_file[k])))
                snprintf(credit[k], sizeof credit[k], "%.50s BY %.40s%s%.20s%s", field[1], field[2], n > 3 ? " (" : "", n > 3 ? field[3] : "", n > 3 ? ")" : "");
    }
    fclose(f);
}

void music_load(const char *data_dir, const char *user_dir)
{
    char dir[1100], udir[1100];
    int k;
    snprintf(dir, sizeof dir, "%.1080s/music/", data_dir);
    snprintf(udir, sizeof udir, "%.1080smusic/", user_dir ? user_dir : "");
    if (getenv("OFP_MUSIC_DIR")) snprintf(udir, sizeof udir, "%.1080s/", getenv("OFP_MUSIC_DIR"));

    for (k = 0; k < MUSIC_SONGS; k++) {
        if (!mod_ctx[k]) mod_ctx[k] = xmp_create_context();
        if (mod_ctx[k])
            mod_ok[k] = (user_dir || getenv("OFP_MUSIC_DIR")) && load_module(k, udir);
        user_track[k] = mod_ok[k];
        if (!mod_ok[k] && tracks[k].file) {
            char path[1200];
            snprintf(path, sizeof path, "%.1100soriginal/%s", dir, tracks[k].file);
            pcm[k] = music_pcm_load(path, RATE, &pcm_samples[k]);
            if (pcm[k]) {
                snprintf(loaded_file[k], sizeof loaded_file[k], "%s", tracks[k].file);
                snprintf(credit[k], sizeof credit[k], "%s BY %s", tracks[k].title, tracks[k].author);
            }
        }
        if (!mod_ok[k] && !pcm[k] && mod_ctx[k]) mod_ok[k] = load_module(k, dir);
    }
    load_credits(dir, 0);
    if (user_dir || getenv("OFP_MUSIC_DIR")) load_credits(udir, 1);
}

int music_has_module(int which) { return which >= 0 && which < MUSIC_SONGS && mod_ok[which]; }

const char *music_credit(int which)
{
    if (which < 0 || which >= MUSIC_SONGS || (!mod_ok[which] && !pcm[which])) return "";
    return credit[which][0] ? credit[which] : mod_title[which];
}

int music_started(void) { return SDL_AtomicSet(&started_song, -1); }

static int module_sample(int k)
{
    if (mod_at >= mod_n) {                               /* the next stretch (the song loops for ever) */
        if (xmp_play_buffer(mod_ctx[k], mod_buf, (int)sizeof mod_buf, 0) != 0) { mod_n = 0; return 0; }
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
    if (ws != song) {                                      /* fade out, then start the new one */
        fade -= 0.0005f;
        if (fade <= 0.0f) {
            fade = 0.0f; song = ws; pos = 0; last_step = -1; env_bass = env_lead = 0;
            if (song >= 0 && mod_ok[song]) { xmp_restart_module(mod_ctx[song]); mod_n = mod_at = 0; }
            SDL_AtomicSet(&started_song, song);
        }
    } else if (fade < 1.0f) fade += 0.0005f;
    if (song < 0 || !level) return 0;
    gain = fade * ((float)level / VOLUME_MAX);
    if (mod_ok[song]) {                                  /* a module: as it was written, at the chosen level */
        return (int)((float)module_sample(song) * gain * 0.42f);
    }
    if (pcm[song]) {
        int value = pcm[song][pos++];
        if (pos == (uint32_t)pcm_samples[song]) pos = 0;
        return (int)((float)value * gain * 0.42f);
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
    music_volume(6);
}

void music_close(void)
{
    int k;
    for (k = 0; k < MUSIC_SONGS; k++) {
        if (mod_ok[k]) xmp_end_player(mod_ctx[k]);
        if (mod_ctx[k]) xmp_free_context(mod_ctx[k]);
        mod_ctx[k] = NULL;
        mod_ok[k] = 0;
        SDL_free(pcm[k]);
        pcm[k] = NULL;
        credit[k][0] = loaded_file[k][0] = 0;
    }
    song = -1;
    fade = 0;
}
