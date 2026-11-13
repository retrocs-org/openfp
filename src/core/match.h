/* SPDX-License-Identifier: MIT
 * Everything that decides how a match begins, in one small struct. Given the
 * same setup (and map text), match_start() builds the very same game on any
 * machine, which is what replays and network play rest on: after that, only
 * the players' buttons move it on. */
#ifndef OFP_MATCH_H
#define OFP_MATCH_H

#include "core/game.h"

#define MATCH_PLAYERS 8
#define MATCH_DEFAULT_SECONDS (20 * 60)
#define MATCH_MAX_SECONDS (30 * 60)
#define MATCH_UNLIMITED_LIVES 255
#define MATCH_MAX_LIVES 99
#define MATCH_BOT_RAIDER 0x80        /* bot[] flag: skip the opening patrol */
#define MATCH_BOT_SIEGE  0x40        /* bot[] flag: attack enemy guns on the way to the flag */

typedef struct {
    uint32_t seed;
    uint8_t  biome;                  /* for a generated map */
    uint8_t  time;                   /* TIME_* for a generated map (a map file brings its own) */
    uint8_t  camp_garages;           /* 3 (the original's camp) or 4 */
    uint8_t  map_size;               /* a random battlefield's: SIZE_* + 1 (0: not said, LARGE); a map file: its own */
    uint8_t  versus;                 /* players on both sides, no computer tank */
    uint8_t  ai_level;               /* AI_OFF .. AI_HARD: the computer tank, if not versus */
    uint8_t  players;                /* 1 .. MATCH_PLAYERS; they are tanks 0 .. players-1 */
    uint8_t  cls[MATCH_PLAYERS];     /* TankClass */
    uint8_t  team[MATCH_PLAYERS];    /* TEAM_PLAYER / TEAM_ENEMY */
    uint8_t  steer[MATCH_PLAYERS];   /* STEER_TANK / STEER_DIRECT */
    uint8_t  bot[MATCH_PLAYERS];     /* AI_* to let the computer drive this player's tank (0 = a person) */
    uint16_t time_limit;             /* seconds; 0 selects 20 minutes, maximum 30 minutes */
    uint8_t lives;                   /* 0 selects 5; 1..99 tanks per player; 255 unlimited */
    uint8_t legacy_surfaces;         /* recordings before ice/oil and temporary buffs */
    uint8_t legacy_arena_spawns;     /* recordings before team-order-independent arena starts */
    uint8_t no_powerups;             /* authoritative local/server switch */
    uint8_t legacy_rules;            /* old recordings keep original vehicles/lives/timing */
    uint8_t  time_rule;              /* TIME_RULE_*: when it's up, more points wins, or a draw */
    uint8_t  weather;                /* WEATHER_* (travels at the end of a MATCH message, and in replays) */
    uint8_t  extras;                 /* EXTRA_*: pickups, a hill (the same way) */
    uint8_t  relaxed_pace;           /* slower ground movement for local play, recorded in replays */
    uint8_t  rocket_tanks;            /* player-slot mask for heavy tanks fitted with a rocket rack */
    uint8_t  bot_commands;            /* the input stream includes the bots' commands */
    uint8_t  legacy_weather;          /* old replay: preserve its original weather rules */
    uint8_t  leader_mask;             /* local starting leaders by player slot */
} MatchSetup;

/* map_text: an .ofpmap, or NULL for a generated battlefield. 0 = ok. Every
 * player gets a garage of their own: their tank's, or the next free one. */
int match_room(const MatchSetup *ms, const char *map_text);
int match_sides(const Game *g, uint8_t extras, int room[2]);
int match_room_sides(const MatchSetup *ms, const char *map_text, int sides[2]);   /* (the same, from the map's text) */   /* each side's room (team modes: even) */   /* tanks the map holds (arena: spawns; camps: garages) */
int  match_start(Game *g, const MatchSetup *ms, const char *map_text);
int  match_start_recorded(Game *g, const MatchSetup *ms, const char *map_text, int recorded_tanks);
int  match_weather(const Game *g, int classic, int fallback);
/* The in-game benchmark uses a fixed map and an equal number of bots per side. */
enum { BENCH_TICKS = 500, BENCH_FRAMES = 100, BENCH_TICKS_PER_FRAME = 2, BENCH_MAX_MS = 4000 };
int  match_benchmark_start(Game *g, int tanks_per_side);
void match_benchmark_step(Game *g, int ticks);
/* a short fingerprint of the setup and map, to check two machines agree */
uint32_t match_hash(const MatchSetup *ms, const char *map_text);

#endif
