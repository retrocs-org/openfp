/* SPDX-License-Identifier: MIT */
#ifndef OFP_AMIGA_MENU_SETTINGS_H
#define OFP_AMIGA_MENU_SETTINGS_H

#include "common/net.h"
#include "common/volume.h"

/* Raw key order: up, down, left, right, fire, mine. */
#define AMIGA_PLAYER_ONE_KEYS { 0x4c, 0x4d, 0x4f, 0x4e, AMIGA_KEY_LAMIGA, AMIGA_KEY_LALT }
#define AMIGA_WASD_KEYS       { 0x11, 0x21, 0x20, 0x22, 0x10, 0x12 }
enum {
    AMIGA_KEY_LALT = 0x64, AMIGA_KEY_RALT, AMIGA_KEY_LAMIGA, AMIGA_KEY_RAMIGA,
    AMIGA_KEY_BIND_LIMIT
};

enum { AMIGA_PROFILES = 3, AMIGA_SERVERS = 4, AMIGA_ADDRESS = 35, AMIGA_WORLD_REMASTER = BIOME_COUNT };
enum { AMIGA_INPUT_KEYS, AMIGA_INPUT_JOY, AMIGA_INPUT_BOTH, AMIGA_INPUT_MODES };
typedef struct {
    char name[NET_NAME];
    uint8_t key[32];
    int has_key;
    char session[65];                 /* cloud session; passwords are never saved */
    int matches, wins;                 /* its record: local and online */
    int face;                          /* its face (0..9: faces_art.h, the desktop's ten) */
    int rating;                        /* its rating, the last server's leaderboard said (0: none yet) */
    int input, steer;                  /* legacy input mode, and original (1) or tank (0) steering */
    int tank;                          /* -1 uses the previous game defaults */
    uint8_t keys[6];                   /* legacy Player 1 layout */
} AmigaProfile;

typedef struct {
    int input, has_keys;
    uint8_t keys[6];
} AmigaSeatControls;

typedef struct {
    AmigaProfile profile[AMIGA_PROFILES];
    AmigaSeatControls controls[2];
    char server[AMIGA_SERVERS][AMIGA_ADDRESS];
    int current, joystick, direct, radar_off, map;
    int swap_joysticks;                 /* swap the two seats' physical ports */
    int mode, level, cls, players;     /* LOCAL GAME: its mode, the enemy's level, our tank, 1 or 2 players */
    int match_lives, match_minutes, powerups;
    int world, map_size;                         /* LOCAL GAME: a random battlefield's world (BIOME_*) */
    int allies, enemies;               /* LOCAL GAME: computer players with us, and against (0..3) */
    int sides;                         /* OPTIONS: 1: colour-blind sides (the green side blue) */
    int replay_n;                      /* the last kept replay's number (match-NNN.ofprep) */
    int marks;                         /* OPTIONS: battle marks: 0 all, 1 no soldiers' (no blood) */
    int music, effects;                /* Audio levels from 0 (off) to 10 */
    int auto_effects;
    int weather_off;                   /* Hide weather in matches */
    int explosions, ground_fires_off, surface_fx_off;  /* Visual settings only */
} AmigaMenuSettings;

enum { EXPLOSIONS_FULL, EXPLOSIONS_SIMPLE, EXPLOSIONS_OFF, EXPLOSION_STYLES };

void amiga_settings_load(AmigaMenuSettings *settings);
int amiga_settings_save(const AmigaMenuSettings *settings);
void amiga_profile_reset(AmigaProfile *profile, int slot);
int amiga_server_address(const char *address, char host[AMIGA_ADDRESS], uint16_t *port);
#endif
