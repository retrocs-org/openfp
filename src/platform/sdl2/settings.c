/* SPDX-License-Identifier: MIT */
#include "platform/sdl2/pref_path.h"
#include <SDL.h>
#include <stdio.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif
#include <stdlib.h>
#include <string.h>
#include "core/game.h"
#include "platform/sdl2/settings.h"
#include "platform/sdl2/input.h"
#include "platform/sdl2/select.h"

static const char *act_names[ACT_COUNT] = { "up", "down", "left", "right", "fire", "mine" };

void settings_default_keys(Settings *s)
{
    memcpy(s->keys, input_default_keys, sizeof s->keys);
}

static int old_default_keys(const Settings *s)
{
    static const int old[2][ACT_COUNT][2] = {
        { { SDL_SCANCODE_W, 0 }, { SDL_SCANCODE_S, 0 }, { SDL_SCANCODE_A, 0 }, { SDL_SCANCODE_D, 0 },
          { SDL_SCANCODE_SPACE, SDL_SCANCODE_LALT }, { SDL_SCANCODE_LSHIFT, SDL_SCANCODE_E } },
        { { SDL_SCANCODE_UP, 0 }, { SDL_SCANCODE_DOWN, 0 }, { SDL_SCANCODE_LEFT, 0 }, { SDL_SCANCODE_RIGHT, 0 },
          { SDL_SCANCODE_RCTRL, SDL_SCANCODE_RETURN }, { SDL_SCANCODE_RSHIFT, SDL_SCANCODE_SLASH } },
    };
    int p, a;
    for (p = 0; p < 2; p++)
        for (a = 0; a < ACT_COUNT; a++)
            if (s->keys[p][a][0] != old[p][a][0] ||
                (s->keys[p][a][1] && s->keys[p][a][1] != old[p][a][1])) return 0;
    return 1;
}

/* Use a name for the comma key so the comma separating two bindings stays unambiguous. */
static int key_from_name(const char *name)
{
    return !strcmp(name, "Comma") ? SDL_SCANCODE_COMMA : (int)SDL_GetScancodeFromName(name);
}

static const char *key_save_name(int scancode)
{
    return scancode == SDL_SCANCODE_COMMA ? "Comma" : SDL_GetScancodeName((SDL_Scancode)scancode);
}

void settings_defaults(Settings *s)
{
    memset(s, 0, sizeof *s);
    s->mode = MODE_SOLO;
    s->ai_level = AI_NORMAL;
    s->steer = STEER_TANK;
#ifdef __APPLE__
    s->volume = 5;
    s->music = 7;
#else
    s->volume = 8;
    s->music = 4;
#endif
    s->radar = 1;
    s->host_time = 20;
    s->match_lives = 5;
    s->powerups = 1;
    s->net_locals = 1;
    s->check_updates = 1;
    s->weather_effects = 1;
    s->stick_deadzone = 37;
    s->adapt_skill = 100;
    s->pad_fire = s->pad_mine = -1;
    s->pad_layout = PAD_TRIGGERS;                        /* (no longer set here: each profile's own) */
    s->power_save = 1;
    s->local_type = 0;                                   /* (capture the flag: the game as it was) */
    s->rumble = 1;
    s->bots_side[0] = BOTS_FILL;
    s->map_size = SIZE_LARGE;
    s->red_marks = 1;
    s->explosions = EXPLOSIONS_FULL;
    s->ground_fires = 1;
    s->effects = 1;                            /* (a random battlefield: the original's size) */                         /* (the team screen starts full; player 1 adds or takes away) */
    snprintf(s->server, sizeof s->server, "127.0.0.1");
    {
        const char *u = getenv("USER");
        if (!u || !u[0]) u = getenv("USERNAME");
        snprintf(s->player_name, sizeof s->player_name, "%.15s", u && u[0] ? u : "Player");
    }
    snprintf(s->listing, sizeof s->listing, "https://openfp.retrocs.org/api/");
    snprintf(s->style, sizeof s->style, "desert");
    settings_default_keys(s);
}

const char *settings_path(void)
{
    static char path[1024];
    if (!path[0]) {
        char *dir = ofp_pref_path();
        snprintf(path, sizeof path, "%ssettings.ini", dir ? dir : "./");
        SDL_free(dir);
    }
    return path;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

int settings_load(Settings *s)
{
    char line[1200];
    int version = 1, saved_music = -1;
    FILE *f = fopen(settings_path(), "r");
    if (!f) return -1;
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '='), *v, *nl;
        int p, a;
        if (!eq || line[0] == '#') continue;
        *eq = 0;
        v = eq + 1;
        if ((nl = strpbrk(v, "\r\n")) != NULL) *nl = 0;
        if (!strcmp(line, "mode"))            s->mode = clampi(atoi(v), 0, MODE_COUNT - 1);
        else if (!strcmp(line, "enemy"))      s->ai_level = clampi(atoi(v), AI_OFF, AI_ADAPTIVE);   /* (4: adaptive) */
        else if (!strcmp(line, "controls"))   s->steer = !strcmp(v, "original") ? STEER_DIRECT : STEER_TANK;
        else if (!strcmp(line, "version"))    version = atoi(v);
        else if (!strcmp(line, "zoom"))       s->zoom = atoi(v) != 0;
        else if (!strcmp(line, "night"))      s->night = atoi(v) ? TIME_NIGHT : TIME_DAY;   /* older files */
        else if (!strcmp(line, "time"))       s->night = time_from_name(v);
        else if (!strcmp(line, "fullscreen")) s->fullscreen = atoi(v) != 0;
        else if (!strcmp(line, "volume"))     s->volume = volume_clamp(atoi(v));
        else if (!strcmp(line, "radar"))      s->radar = 1;
        else if (!strcmp(line, "radar_detail")) s->radar = clampi(atoi(v), 1, 2);
        else if (!strcmp(line, "powerups")) s->powerups = !!atoi(v);
        else if (!strcmp(line, "match_lives")) s->match_lives = atoi(v) == 255 ? 255 : clampi(atoi(v), 1, 99);
        else if (!strcmp(line, "server"))     snprintf(s->server, sizeof s->server, "%s", v);
        else if (!strcmp(line, "name"))       snprintf(s->player_name, sizeof s->player_name, "%.15s", v);
        else if (!strcmp(line, "server_password")) snprintf(s->server_password, sizeof s->server_password, "%s", v);
        else if (!strcmp(line, "rejoin")) snprintf(s->rejoin, sizeof s->rejoin, "%s", v);
        else if (!strcmp(line, "rejoin_until")) s->rejoin_until = atol(v);
        else if (!strcmp(line, "direct_save")) s->direct_save = clampi(atoi(v), 0, 1);
        else if (!strcmp(line, "my_server") && s->n_mine < 8) {   /* "host:port password" (the password: optional) */
            const char *sp = strchr(v, ' ');
            snprintf(s->mine[s->n_mine], sizeof s->mine[0], "%.*s", (int)(sp ? sp - v : (int)strlen(v)), v);
            snprintf(s->mine_pw[s->n_mine], sizeof s->mine_pw[0], "%s", sp ? sp + 1 : "");
            if (s->mine[s->n_mine][0]) s->n_mine++;
        }
        else if (!strcmp(line, "listing"))    snprintf(s->listing, sizeof s->listing, "%s",
                                                       strcmp(v, "http://openfp.retrocs.org/api/") ? v : "https://openfp.retrocs.org/api/");   /* (the old default) */
        else if (!strcmp(line, "host_bots"))  s->host_bots = clampi(atoi(v), 0, 3);
        else if (!strcmp(line, "host_time"))  s->host_time = atoi(v) < 1 ? 20 : clampi(atoi(v), 1, 30);
        else if (!strcmp(line, "host_rule"))  s->host_rule = clampi(atoi(v), 0, 1);
        else if (!strcmp(line, "host_start")) s->host_start = clampi(atoi(v), 0, 300);
        else if (!strcmp(line, "host_watch")) s->host_watch = clampi(atoi(v), 0, 2);
        else if (!strcmp(line, "host_maps"))  s->host_maps = clampi(atoi(v), 0, 3);
        else if (!strcmp(line, "net_locals")) s->net_locals = clampi(atoi(v), 1, 4);
        else if (!strcmp(line, "check_updates")) s->check_updates = atoi(v) != 0;
        else if (!strcmp(line, "music"))      saved_music = volume_clamp(atoi(v));
        else if (!strcmp(line, "sides"))      s->sides = clampi(atoi(v), 0, 1);
        else if (!strcmp(line, "weather"))    s->weather = clampi(atoi(v), 0, 1);
        else if (!strcmp(line, "auto_effects")) s->auto_effects = atoi(v) != 0;
        else if (!strcmp(line, "stick_deadzone")) s->stick_deadzone = clampi(atoi(v), 12, 50);
        else if (!strcmp(line, "weather_effects")) s->weather_effects = atoi(v) != 0;
        else if (!strcmp(line, "extras"))     s->extras = clampi(atoi(v), 0, 3);
        else if (!strcmp(line, "power_save")) s->power_save = atoi(v) != 0;
        else if (!strcmp(line, "local_mode")) s->local_type = clampi(atoi(v), 0, 5);   /* (5: CLASSIC) */
        else if (!strcmp(line, "marks")) s->red_marks = atoi(v) != 0;
        else if (!strcmp(line, "quick_goal")) s->quick_goal = clampi(atoi(v), 0, 7);
        else if (!strcmp(line, "explosions")) s->explosions = clampi(atoi(v), 0, EXPLOSION_STYLES - 1);
        else if (!strcmp(line, "ground_fires")) s->ground_fires = atoi(v) != 0;
        else if (!strcmp(line, "effects")) s->effects = atoi(v) != 0;
        else if (!strcmp(line, "map_size")) {           /* small, medium, large, huge, any */
            int z;
            if (!strcmp(v, "any")) s->map_size = SIZE_ANY;
            for (z = 0; z < MAP_SIZES; z++) if (!strcmp(v, map_size_name[z])) s->map_size = z;
        }
        else if (!strcmp(line, "bots"))       s->bots_side[0] = !strcmp(v, "fill") ? BOTS_FILL : clampi(atoi(v), 0, 7);
        else if (!strcmp(line, "rumble"))     s->rumble = clampi(atoi(v), 0, 1);
        else if (!strcmp(line, "pad_layout")) s->pad_layout = clampi(atoi(v), 0, PAD_LAYOUTS - 1);
        else if (!strcmp(line, "pad_fire"))   s->pad_fire = strcmp(v, "default") ? (int)SDL_GameControllerGetButtonFromString(v) : -1;
        else if (!strcmp(line, "pad_mine"))   s->pad_mine = strcmp(v, "default") ? (int)SDL_GameControllerGetButtonFromString(v) : -1;
        else if (!strcmp(line, "adapt_skill")) s->adapt_skill = clampi(atoi(v), 0, 200);
        else if (!strcmp(line, "favourite") && s->nfavs < 16)    snprintf(s->favs[s->nfavs++], sizeof s->favs[0], "%s", v);
        else if (!strcmp(line, "recent") && s->nrecent < 10)     snprintf(s->recent[s->nrecent++], sizeof s->recent[0], "%s", v);
        else if (!strcmp(line, "friend") && s->nfriends < 24)    snprintf(s->friends[s->nfriends++], sizeof s->friends[0], "%s", v);
        else if (!strcmp(line, "player_key") && strlen(v) >= 64) {
            int i, ok = 1;
            for (i = 0; i < 32 && ok; i++) { unsigned x; ok = sscanf(v + i * 2, "%2x", &x) == 1; s->player_key[i] = (unsigned char)x; }
            s->have_key = ok;
        }
        else if (!strcmp(line, "terrain"))    snprintf(s->style, sizeof s->style, "%s", v);
        else if (!strcmp(line, "map"))        snprintf(s->map, sizeof s->map, "%s", v);
        else if (sscanf(line, "p%d_", &p) == 1 && p >= 1 && p <= 4)
            for (a = 0; a < ACT_COUNT; a++) {
                char key[32];
                snprintf(key, sizeof key, "p%d_%s", p, act_names[a]);
                if (!strcmp(line, key)) {
                    /* Older files could write a literal comma as the first key. */
                    int leading_comma_key = v[0] == ',' && (v[1] == ',' || !v[1]);
                    char *comma = strchr(v + leading_comma_key, ',');
                    if (comma) *comma = 0;
                    s->keys[p - 1][a][0] = key_from_name(v);
                    s->keys[p - 1][a][1] = comma ? key_from_name(comma + 1) : 0;
                }
            }
    }
    fclose(f);
    /* files from before version 2 may hold "original" controls switched on by
     * the old C hotkey by accident: start again from tank controls */
    if (version < 2) s->steer = STEER_TANK;
    if (version < 4 && old_default_keys(s))                 /* preserve custom bindings and sets 3 and 4 */
        memcpy(s->keys, input_default_keys, 2 * sizeof s->keys[0]);
    if (saved_music >= 0) {
        static const int old_levels[] = { 0, 4, 6, VOLUME_MAX };
        s->music = version < 3 ? old_levels[clampi(saved_music, 0, 3)] : saved_music;
    }
    if (!mode_available(s->mode)) s->mode = MODE_SOLO;
    return 0;
}

int settings_save(const Settings *s)
{
    /* only the user may read it: it holds the player key (and the server password) */
#ifndef _WIN32
    mode_t old = umask(077);                             /* a new file: rw for the user only */
    FILE *f = fopen(settings_path(), "w");
    umask(old);
    chmod(settings_path(), 0600);                        /* (and one written before) */
#else
    FILE *f = fopen(settings_path(), "w");
#endif
    int p, a;
    if (!f) return -1;
    fprintf(f, "# OpenFP settings, written by the game\nversion=4\n");
    fprintf(f, "mode=%d\nenemy=%d\ncontrols=%s\nzoom=%d\ntime=%s\nfullscreen=%d\nvolume=%d\n",
            s->mode, s->ai_level, s->steer == STEER_DIRECT ? "original" : "tank", s->zoom, time_name(s->night),
            s->fullscreen, s->volume);
    fprintf(f, "powerups=%d\n", s->powerups);
    fprintf(f, "auto_effects=%d\nstick_deadzone=%d\n", s->auto_effects, s->stick_deadzone);
    fprintf(f, "radar_detail=%d\nweather_effects=%d\nmatch_lives=%d\n", s->radar, s->weather_effects, s->match_lives);
    fprintf(f, "sides=%d\nweather=%d\nadapt_skill=%d\nextras=%d\n", s->sides, s->weather, s->adapt_skill, s->extras);
    fprintf(f, "explosions=%d\nground_fires=%d\n", s->explosions, s->ground_fires);
    fprintf(f, "marks=%d\neffects=%d\nquick_goal=%d\n", s->red_marks, s->effects, s->quick_goal);
    fprintf(f, "map_size=%s\n", s->map_size == SIZE_ANY ? "any" :
            map_size_name[s->map_size >= 0 && s->map_size < MAP_SIZES ? s->map_size : SIZE_LARGE]);
    fprintf(f, "power_save=%d\nlocal_mode=%d\nbots=%d\nrumble=%d\npad_layout=%d\n", s->power_save, s->local_type, s->bots_side[0],
            s->rumble, s->pad_layout);
    fprintf(f, "pad_fire=%s\npad_mine=%s\n",          /* (by name: "a", "rightshoulder"...) */
            s->pad_fire < 0 ? "default" : SDL_GameControllerGetStringForButton((SDL_GameControllerButton)s->pad_fire),
            s->pad_mine < 0 ? "default" : SDL_GameControllerGetStringForButton((SDL_GameControllerButton)s->pad_mine));
    {
        int i;
        for (i = 0; i < s->nfavs; i++) fprintf(f, "favourite=%s\n", s->favs[i]);
        for (i = 0; i < s->nrecent; i++) fprintf(f, "recent=%s\n", s->recent[i]);
        for (i = 0; i < s->nfriends; i++) fprintf(f, "friend=%s\n", s->friends[i]);
    }
    fprintf(f, "host_time=%d\nhost_rule=%d\nhost_start=%d\nhost_watch=%d\nhost_maps=%d\n", s->host_time, s->host_rule,
            s->host_start, s->host_watch, s->host_maps);
    fprintf(f, "direct_save=%d\n", s->direct_save);
    { int i; for (i = 0; i < s->n_mine; i++) fprintf(f, "my_server=%s%s%s\n", s->mine[i], s->mine_pw[i][0] ? " " : "", s->mine_pw[i]); }
    fprintf(f, "rejoin=%s\nrejoin_until=%ld\n", s->rejoin, s->rejoin_until);
    fprintf(f, "server=%s\nname=%s\nserver_password=%s\nlisting=%s\nhost_bots=%d\nnet_locals=%d\ncheck_updates=%d\nmusic=%d\n", s->server, s->player_name,
            s->server_password, s->listing, s->host_bots, s->net_locals < 1 ? 1 : s->net_locals, s->check_updates, s->music);
    if (s->have_key) {                                   /* who I am on servers: keep it secret, keep it safe */
        int i;
        fprintf(f, "player_key=");
        for (i = 0; i < 32; i++) fprintf(f, "%02x", s->player_key[i]);
        fprintf(f, "\n");
    }
    fprintf(f, "terrain=%s\nmap=%s\n", s->style, s->map);
    for (p = 0; p < 4; p++)
        for (a = 0; a < ACT_COUNT; a++) {
            const char *k0 = key_save_name(s->keys[p][a][0]);
            const char *k1 = key_save_name(s->keys[p][a][1]);
            fprintf(f, "p%d_%s=%s%s%s\n", p + 1, act_names[a], k0, k1[0] ? "," : "", k1);
        }
    fclose(f);
    return 0;
}

void settings_add_mine(Settings *s, const char *addr, const char *pw)
{
    int i;
    if (!addr || !addr[0]) return;
    for (i = 0; i < s->n_mine; i++) if (!strcmp(s->mine[i], addr)) break;   /* (there already: its password new) */
    if (i == s->n_mine) { if (s->n_mine >= 8) { memmove(s->mine, s->mine + 1, sizeof s->mine[0] * 7); memmove(s->mine_pw, s->mine_pw + 1, sizeof s->mine_pw[0] * 7); s->n_mine = 7; }
                          i = s->n_mine++; }
    snprintf(s->mine[i], sizeof s->mine[i], "%s", addr);
    snprintf(s->mine_pw[i], sizeof s->mine_pw[i], "%s", pw ? pw : "");
}

void settings_drop_mine(Settings *s, int i)
{
    if (i < 0 || i >= s->n_mine) return;
    memmove(s->mine + i, s->mine + i + 1, sizeof s->mine[0] * (size_t)(s->n_mine - i - 1));
    memmove(s->mine_pw + i, s->mine_pw + i + 1, sizeof s->mine_pw[0] * (size_t)(s->n_mine - i - 1));
    s->n_mine--;
}

