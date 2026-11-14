/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/amiga/amiga_music.h"
#include "platform/amiga/menu_settings.h"

#define SETTINGS_FILE "PROGDIR:OpenFirePower.cfg"
#define SETTINGS_TEMP "PROGDIR:OpenFirePower.cfg.new"
#define SETTINGS_BACKUP "PROGDIR:OpenFirePower.cfg.bak"

enum { KEY_LAYOUT_VERSION = 4 };

static int hex_digit(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

void amiga_profile_reset(AmigaProfile *pr, int slot)
{
    static const uint8_t defaults[6] = AMIGA_PLAYER_ONE_KEYS;
    memset(pr, 0, sizeof *pr);
    pr->face = slot;
    pr->tank = -1;
    memcpy(pr->keys, defaults, sizeof defaults);
}

static int read_keys(const char *value, uint8_t keys[6])
{
    unsigned raw[6];
    int j, valid = 1;
    if (sscanf(value, "%x,%x,%x,%x,%x,%x", &raw[0], &raw[1], &raw[2], &raw[3], &raw[4], &raw[5]) != 6)
        return 0;
    for (j = 0; j < 6; j++) {
        if (raw[j] < AMIGA_KEY_BIND_LIMIT) keys[j] = (uint8_t)raw[j];
        else valid = 0;
    }
    return valid;
}

void amiga_settings_load(AmigaMenuSettings *s)
{
    static const int legacy_music[] = { 0, 3, 6, VOLUME_MAX };
    static const int legacy_effects[] = { 0, 3, 7, VOLUME_MAX };
    char line[128];
    FILE *file;
    static const uint8_t old_arrows[6] = { 0x4c, 0x4d, 0x4f, 0x4e, 0x44, 0x41 };
    static const uint8_t wasd[6] = AMIGA_WASD_KEYS;
    static const uint8_t defaults[6] = AMIGA_PLAYER_ONE_KEYS;
    int i, key_layout = 0, profile_slots = 0, music_off = 0, volume_steps = 0;
    int saved_music = -1, saved_effects = -1;
    memset(s, 0, sizeof *s);
    for (i = 0; i < AMIGA_PROFILES; i++) {
        amiga_profile_reset(&s->profile[i], i);
        s->profile[i].input = s->profile[i].steer = -1;   /* (none saved: the old global INPUT and STEERING's) */
    }
    strcpy(s->profile[0].name, "Amiga");
    s->music = AMIGA_MUSIC_DEFAULT_LEVEL;
    s->match_lives = 5;
    s->powerups = 1;
    s->match_minutes = 20;
    s->effects = legacy_effects[2];
    s->controls[0].input = s->controls[1].input = -1;
    strcpy(s->server[0], "10.0.2.2:29870");
    s->level = 1; s->cls = 1; s->players = 1; s->map_size = -1;
    file = fopen(SETTINGS_FILE, "r");
    if (!file) file = fopen(SETTINGS_BACKUP, "r");
    if (!file) {
        s->world = BIOME_DESERT;
        return;
    }
    while (fgets(line, sizeof line, file)) {
        char *value = strchr(line, '=');
        int n;
        if (!value) continue;
        *value++ = 0;
        value[strcspn(value, "\r\n")] = 0;
        n = atoi(value);
        if (!strcmp(line, "current") && n >= 0 && n < AMIGA_PROFILES) s->current = n;
        else if (!strcmp(line, "key_layout")) key_layout = n;
        else if (!strcmp(line, "profile_slots")) profile_slots = n;
        else if (!strcmp(line, "joystick")) s->joystick = n != 0;
        else if (!strcmp(line, "swap_joysticks")) s->swap_joysticks = n == 1;
        else if (!strcmp(line, "direct")) s->direct = n != 0;
        else if (!strcmp(line, "powerups")) s->powerups = !!n;
        else if (!strcmp(line, "match_lives")) s->match_lives = n == 255 ? 255 : n < 1 ? 5 : n > 99 ? 99 : n;
        else if (!strcmp(line, "match_minutes")) s->match_minutes = n < 1 ? 20 : n > 30 ? 30 : n;
        else if (!strcmp(line, "radar_off")) s->radar_off = n != 0;
        else if (!strcmp(line, "map") && n >= 0 && n < 4096) s->map = n;
        else if (!strcmp(line, "mode") && n >= 0 && n < 7) s->mode = n;
        else if (!strcmp(line, "level") && n >= 0 && n < 3) s->level = n;
        else if (!strcmp(line, "cls") && n >= 0 && n <= TANK_RHINO) s->cls = n;
        else if (!strcmp(line, "players") && (n == 1 || n == 2)) s->players = n;
        else if (!strcmp(line, "world") && n >= 0 && n <= AMIGA_WORLD_REMASTER) s->world = n == AMIGA_WORLD_REMASTER ? BIOME_DESERT : n == BIOME_SCIFI ? BIOME_MOON : n;
        else if (!strcmp(line, "map_size") && ((n >= SIZE_SMALL && n <= SIZE_LARGE) || n == SIZE_ANY)) s->map_size = n;
        else if (!strcmp(line, "allies") && n >= 0 && n <= 3) s->allies = n;
        else if (!strcmp(line, "enemies") && n >= 0 && n <= 3) s->enemies = n;
        else if (!strcmp(line, "sides")) s->sides = n != 0;
        else if (!strcmp(line, "replay_n") && n >= 0 && n < 1000) s->replay_n = n;
        else if (!strcmp(line, "marks")) s->marks = n != 0;
        else if (!strcmp(line, "music_off")) music_off = n != 0;
        else if (!strcmp(line, "volume_steps")) volume_steps = n;
        else if (!strcmp(line, "music_volume") && n >= 0 && n <= VOLUME_MAX) saved_music = n;
        else if (!strcmp(line, "effects_volume") && n >= 0 && n <= VOLUME_MAX) saved_effects = n;
        else if (!strcmp(line, "auto_effects")) s->auto_effects = n != 0;
        else if (!strcmp(line, "weather_off")) s->weather_off = n != 0;
        else if (!strcmp(line, "explosions") && n >= 0 && n < EXPLOSION_STYLES) s->explosions = n;
        else if (!strcmp(line, "ground_fires_off")) s->ground_fires_off = n != 0;
        else if (!strcmp(line, "surface_fx_off")) s->surface_fx_off = n != 0;
        else if (!strcmp(line, "input1") || !strcmp(line, "input2")) {
            if (value[0] >= '0' && value[0] < '0' + AMIGA_INPUT_MODES && value[1] == 0)
                s->controls[line[5] - '1'].input = n;
        } else if (!strcmp(line, "keys1") || !strcmp(line, "keys2")) {
            AmigaSeatControls *controls = &s->controls[line[4] - '1'];
            controls->has_keys = read_keys(value, controls->keys);
        } else if (strlen(line) == 2 && line[1] >= '0' && line[1] <= '9') {
            i = line[1] - '0';
            if (line[0] == 'p' && i < AMIGA_PROFILES)
                snprintf(s->profile[i].name, NET_NAME, "%.15s", value);
            if (line[0] == 'a' && i < AMIGA_PROFILES && strlen(value) == 64) {
                int j; for(j=0;j<64&&hex_digit(value[j])>=0;j++) {}
                if(j==64) strcpy(s->profile[i].session,value);
            }
            if (line[0] == 'd' && i < AMIGA_PROFILES) s->profile[i].cloud_dirty = n != 0;
            if (line[0] == 'w' && i < AMIGA_PROFILES && n >= 0) s->profile[i].wins = n;      /* (its record) */
            if (line[0] == 'm' && i < AMIGA_PROFILES && n >= 0) s->profile[i].matches = n;
            if (line[0] == 'f' && i < AMIGA_PROFILES && n >= 0 && n < 10) s->profile[i].face = n;      /* its face */
            if (line[0] == 'r' && i < AMIGA_PROFILES && n >= 0 && n < 10000) s->profile[i].rating = n;  /* its rating */
            if (line[0] == 'i' && i < AMIGA_PROFILES) s->profile[i].input = n != 0;                    /* its controls */
            if (line[0] == 't' && i < AMIGA_PROFILES) s->profile[i].steer = n != 0;
            if (line[0] == 'c' && i < AMIGA_PROFILES) read_keys(value, s->profile[i].keys);
            if (line[0] == 'v' && i < AMIGA_PROFILES && n >= -1 && n <= TANK_RHINO) s->profile[i].tank = n;
            if (line[0] == 's' && i < AMIGA_SERVERS)
                snprintf(s->server[i], AMIGA_ADDRESS, "%.34s", value);
            if (line[0] == 'k' && i < AMIGA_PROFILES && strlen(value) == 64) {
                int j;
                s->profile[i].has_key = 0;
                for (j = 0; j < 32; j++) {
                    int hi = hex_digit(value[j * 2]), lo = hex_digit(value[j * 2 + 1]);
                    if (hi < 0 || lo < 0) break;
                    s->profile[i].key[j] = (uint8_t)(hi * 16 + lo);
                }
                s->profile[i].has_key = j == 32;
            }
        }
    }
    fclose(file);
    if (music_off) s->music = VOLUME_DISABLED;
    if (volume_steps == VOLUME_MAX) {
        if (saved_music >= 0) s->music = saved_music;
        if (saved_effects >= 0) s->effects = saved_effects;
    } else {
        if (saved_music >= 0 && saved_music < 4) s->music = legacy_music[saved_music];
        if (saved_effects >= 0 && saved_effects < 4) s->effects = legacy_effects[saved_effects];
    }
    if (!profile_slots) for (i = 1; i < AMIGA_PROFILES; i++) {
        AmigaProfile *pr = &s->profile[i];
        char placeholder[NET_NAME];
        snprintf(placeholder, sizeof placeholder, "Player %d", i + 1);
        if (i != s->current && !strcmp(pr->name, placeholder) && !pr->has_key &&
            !pr->matches && !pr->wins && !pr->rating && pr->face == i && pr->tank < 0 &&
            pr->input <= 0 && pr->steer <= 0 &&
            (!memcmp(pr->keys, old_arrows, sizeof old_arrows) || !memcmp(pr->keys, wasd, sizeof wasd) ||
             !memcmp(pr->keys, defaults, sizeof defaults)))
            pr->name[0] = 0;
    }
    for (i = 0; i < AMIGA_PROFILES; i++) {               /* (saved before controls were a profile's: the old global ones) */
        uint8_t *keys = s->profile[i].keys;
        if ((key_layout < KEY_LAYOUT_VERSION && !memcmp(keys, wasd, sizeof wasd)) ||
            (key_layout < 3 && !memcmp(keys, old_arrows, sizeof old_arrows)))
            memcpy(keys, defaults, sizeof defaults);
        if (s->profile[i].input < 0) s->profile[i].input = s->joystick;
        if (s->profile[i].steer < 0) s->profile[i].steer = s->direct;
    }
}

int amiga_settings_save(const AmigaMenuSettings *s)
{
    FILE *file = fopen(SETTINGS_TEMP, "w");
    int i, j, failed, backup;
    if (!file) return -1;
    fprintf(file, "current=%d\njoystick=%d\ndirect=%d\nradar_off=%d\nmap=%d\nmode=%d\nlevel=%d\ncls=%d\nplayers=%d\nworld=%d\n",
            s->current, s->joystick, s->direct, s->radar_off, s->map, s->mode, s->level, s->cls, s->players, s->world);
    fprintf(file, "powerups=%d\n", s->powerups);
    fprintf(file, "match_lives=%d\nmatch_minutes=%d\n", s->match_lives, s->match_minutes);
    fprintf(file, "map_size=%d\nkey_layout=%d\nprofile_slots=1\n", s->map_size, KEY_LAYOUT_VERSION);
    fprintf(file, "swap_joysticks=%d\n", s->swap_joysticks);
    fprintf(file, "allies=%d\nenemies=%d\nsides=%d\nreplay_n=%d\nmarks=%d\n", s->allies, s->enemies, s->sides,
            s->replay_n, s->marks);
    fprintf(file, "volume_steps=%d\nmusic_volume=%d\neffects_volume=%d\nweather_off=%d\n",
            VOLUME_MAX, s->music, s->effects, s->weather_off);
    fprintf(file, "auto_effects=%d\n", s->auto_effects);
    fprintf(file, "explosions=%d\nground_fires_off=%d\nsurface_fx_off=%d\n", s->explosions, s->ground_fires_off, s->surface_fx_off);
    for (i = 0; i < 2; i++) {
        const AmigaSeatControls *controls = &s->controls[i];
        const uint8_t *keys = controls->keys;
        if (controls->input >= 0) fprintf(file, "input%d=%d\n", i + 1, controls->input);
        if (controls->has_keys) fprintf(file, "keys%d=%x,%x,%x,%x,%x,%x\n", i + 1,
                                        keys[0], keys[1], keys[2], keys[3], keys[4], keys[5]);
    }
    for (i = 0; i < AMIGA_PROFILES; i++) {
        const AmigaProfile *pr = &s->profile[i];
        fprintf(file, "p%d=%s\nw%d=%d\nm%d=%d\n", i, pr->name, i, pr->wins, i, pr->matches);
        fprintf(file, "f%d=%d\nr%d=%d\ni%d=%d\nt%d=%d\nc%d=%x,%x,%x,%x,%x,%x\n", i, pr->face, i, pr->rating,
                i, pr->input > 0, i, pr->steer > 0, i, pr->keys[0], pr->keys[1], pr->keys[2], pr->keys[3], pr->keys[4], pr->keys[5]);
        if (pr->session[0]) fprintf(file,"a%d=%s\nd%d=%d\n",i,pr->session,i,pr->cloud_dirty);
        if (pr->tank >= 0) fprintf(file, "v%d=%d\n", i, pr->tank);
        if (s->profile[i].has_key) {
            fprintf(file, "k%d=", i);
            for (j = 0; j < 32; j++) fprintf(file, "%02x", (unsigned)s->profile[i].key[j]);
            fputc('\n', file);
        }
    }
    for (i = 0; i < AMIGA_SERVERS; i++) fprintf(file, "s%d=%s\n", i, s->server[i]);
    failed = ferror(file);
    if (fclose(file) || failed) { remove(SETTINGS_TEMP); return -1; }
    remove(SETTINGS_BACKUP);
    backup = rename(SETTINGS_FILE, SETTINGS_BACKUP) == 0;
    if (rename(SETTINGS_TEMP, SETTINGS_FILE)) {
        if (backup) rename(SETTINGS_BACKUP, SETTINGS_FILE);
        remove(SETTINGS_TEMP);
        return -1;
    }
    if (backup) remove(SETTINGS_BACKUP);
    return 0;
}

int amiga_server_address(const char *address, char host[AMIGA_ADDRESS], uint16_t *port)
{
    const char *colon = strchr(address, ':');
    size_t n = colon ? (size_t)(colon - address) : strlen(address), i;
    unsigned long number = 29870;
    if (!n || n >= AMIGA_ADDRESS) return -1;
    for (i = 0; i < n; i++) {
        char ch = address[i];
        if (!(ch >= '0' && ch <= '9') && !(ch >= 'A' && ch <= 'Z') &&
            !(ch >= 'a' && ch <= 'z') && ch != '.' && ch != '-') return -1;
    }
    if (colon) {
        char *end;
        if (colon[1] < '0' || colon[1] > '9') return -1;
        number = strtoul(colon + 1, &end, 10);
        if (*end || !number || number > 65535) return -1;
    }
    memcpy(host, address, n);
    host[n] = 0;
    *port = (uint16_t)number;
    return 0;
}
