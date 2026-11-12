/* SPDX-License-Identifier: MIT
 * Settings that last between runs: the game type, the enemy, terrain,
 * controls, view, sound and the players' keys. Kept as a small text file
 * in the user's settings folder (SDL_GetPrefPath):
 *   macOS   ~/Library/Application Support/OpenFirePower/settings.ini
 *   Linux   ~/.local/share/OpenFirePower/settings.ini
 *   Windows %APPDATA%\OpenFirePower\settings.ini
 * Command-line options override them for one run without saving. */
#ifndef OFP_SDL2_SETTINGS_H
#define OFP_SDL2_SETTINGS_H

#include "common/volume.h"

enum { EXPLOSIONS_FULL, EXPLOSIONS_SIMPLE, EXPLOSIONS_OFF, EXPLOSION_STYLES };

enum { ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_FIRE, ACT_MINE, ACT_COUNT };

#define BOTS_FILL 8                  /* AI BOTS: FILL (up to what the map holds) */

typedef struct {
    int  mode;                       /* MODE_* */
    int  ai_level;                   /* AI_OFF .. AI_HARD */
    int  steer;                      /* STEER_TANK / STEER_DIRECT */
    int  zoom, night, fullscreen;    /* night: TIME_* for random maps (map files bring their own) */
    int  radar;                      /* 1 local map, 2 local tank dots */
    int  volume;                     /* 0 .. 10 */
    char style[64];
    char map[1024];                  /* map file to play; "" = a new random battlefield */
    char server[64];                 /* the address last joined over the network */
    char player_name[16];            /* my name on servers */
    char server_password[64];        /* the password typed last (for a server that wants one) */
    char rejoin[64];                 /* a network match we were in, "host:port" (the game closed in it: join again) */
    long rejoin_until;               /* (until then: seconds since 1970; a match is held for its players a while) */
    int  direct_save;                /* DIRECT CONNECT: keep it in MY SERVERS */
    int  n_mine;                     /* MY SERVERS: ones not on the public list (address, password) */
    char mine[8][64], mine_pw[8][64];
    char listing[256];               /* the server listing service (FIND SERVERS) */
    int  host_bots;                  /* computer players in a game hosted here (0 .. 3) */
    int  host_time;                  /* minutes per local/hosted match, 1..30 */
    int  powerups;                   /* enhanced games only */
    int  match_lives;                /* 1..99 or 255 unlimited */
    int  host_rule;                  /* hosting: when time runs out, TIME_RULE_* */
    int  host_start;                 /* hosting: start this many s after enough are ready (0 = wait for all) */
    int  host_watch;                 /* hosting: 0 watching live, 1 30 s behind, 2 no watching */
    int  host_maps;                  /* hosting: 0 the map on the main page, 1 the world's maps in order, 2 random, 3 vote */
    int  net_locals;                 /* players at this machine in a network game: 1 or 2 */
    int  check_updates;              /* ask the listing for the newest version at start */
    int  music;                      /* 0 off, 1 .. 10 */
    int  sides;                      /* 0 blue and red, 1 blue and orange with marks (colour-blind) */
    int  weather;                    /* hosted games: clear or thunderstorm */
    int auto_effects, stick_deadzone;
    int  weather_effects;            /* show weather without changing the match or its lighting */
    int  extras;                     /* EXTRA_*: pickups, the hill */
    int  pad_fire, pad_mine;         /* the pad's buttons: SDL_CONTROLLER_BUTTON_*, -1 = as ever */
    int  pad_layout;                 /* PAD_CLASSIC (the stick drives) or PAD_TRIGGERS (RT forward, LT back) */
    int  local_type;                 /* LOCAL GAME's mode: 0 capture the flag, 1 conquest, 2 annihilation */
    int  map_size;
    int  red_marks;                  /* battle marks: the soldiers' red ones too (1) */
    int  quick_goal;                 /* QUICK MATCH: the mode it looks for first (0: any; QUICK_GOALS' index) */
    int  explosions;                 /* FULL, SIMPLE or OFF, visual effects only */
    int  ground_fires;               /* flames and smoke from wrecks and ruins */
    int  effects;                    /* 1: screen shake, the engines of tanks near you; 0: calm */                   /* a random battlefield's: SIZE_SMALL .. SIZE_HUGE (LARGE, the original's) */
    int  bots_side[2];               /* (bots_side[0]: how many computer players, BOTS_FILL: as many as the map holds) */
    int  power_save;                 /* menus at 30 frames a second (none while nobody touches them), 10 in the background */
    int  rumble;                     /* a pad rumbles when hit, firing, destroyed (rumble=0: not) */
    int  adapt_skill;                /* ENEMY: ADAPTIVE: where it got to (0 easy .. 200 hard) */
    char favs[16][72], recent[10][72], friends[24][16];   /* server browser: favourites, joined lately, played with */
    int  nfavs, nrecent, nfriends;
    unsigned char player_key[32];    /* who I am on servers (made once, kept in settings.ini) */
    int  have_key;
    int  keys[4][ACT_COUNT][2];      /* players 1-4's key sets: SDL scancode, main and spare (0 = none; 3, 4 empty at first) */
} Settings;

void settings_defaults(Settings *s);
void settings_default_keys(Settings *s);
void settings_add_mine(Settings *s, const char *addr, const char *pw);   /* (MY SERVERS: in, or its password new) */
void settings_drop_mine(Settings *s, int i);
int  settings_load(Settings *s);              /* 0 = read; defaults stay where missing */
int  settings_save(const Settings *s);
const char *settings_path(void);

#endif
