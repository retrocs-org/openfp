/* SPDX-License-Identifier: MIT
 * The game's core for a web page (WebAssembly): set a match up from a replay's header,
 * feed it each tick's buttons, read back what to draw. The same rules as everywhere else,
 * so a replay plays back exactly as it was played. Built by tools/build_web.sh. */
#include <stdlib.h>
#include <string.h>
#include "core/match.h"
#include "core/game.h"
#include "common/rules.h"

#define EXPORT(name) __attribute__((export_name(name)))

static Game g;

EXPORT("ofp_alloc") void *ofp_alloc(int n) { return malloc((size_t)n); }
EXPORT("ofp_free")  void ofp_free(void *p) { free(p); }

/* per: cls, team, steer, bot for each of the eight players (32 bytes) */
EXPORT("ofp_start")
int ofp_start(int seed, int biome, int time, int garages, int versus, int ai, int players, const unsigned char *per,
              int time_limit, int rule, int weather, const char *map, int extras, int map_size, int tanks, int bot_commands,
              int classic_clear, int world_rules, int lives, int relaxed_pace, int rocket_tanks, int leaders, int surface_rules, int powerups)
{
    MatchSetup ms;
    int p;
    memset(&ms, 0, sizeof ms);
    ms.seed = (uint32_t)seed; ms.biome = (uint8_t)biome; ms.time = (uint8_t)time; ms.camp_garages = (uint8_t)garages;
    ms.versus = (uint8_t)versus; ms.ai_level = (uint8_t)ai; ms.players = (uint8_t)players;
    for (p = 0; p < MATCH_PLAYERS; p++) {
        ms.cls[p] = per[p * 4]; ms.team[p] = per[p * 4 + 1]; ms.steer[p] = per[p * 4 + 2]; ms.bot[p] = per[p * 4 + 3];
    }
    ms.time_limit = (uint16_t)time_limit; ms.time_rule = (uint8_t)rule; ms.weather = (uint8_t)weather;
    ms.extras = (uint8_t)extras;
    ms.bot_commands = bot_commands == 1;
    ms.legacy_weather = classic_clear != 1;
    ms.legacy_rules = world_rules != 1;
    ms.legacy_surfaces = surface_rules != 1;
    ms.no_powerups = surface_rules == 1 && !powerups;
    ms.lives = (uint8_t)lives;
    ms.relaxed_pace = (uint8_t)relaxed_pace;
    ms.rocket_tanks = (uint8_t)rocket_tanks;
    ms.leader_mask = (uint8_t)leaders;
    ms.map_size = (uint8_t)map_size;                     /* (a random battlefield's size + 1; 0: large) */
    return tanks > 0 ? match_start_recorded(&g, &ms, map && map[0] ? map : NULL, tanks)   /* (as recorded: an older one's tanks) */
                     : match_start(&g, &ms, map && map[0] ? map : NULL);
}

EXPORT("ofp_tick") void ofp_tick(const unsigned char *inputs)
{
    InputBits in[MAX_TANKS];
    int i;
    for (i = 0; i < MAX_TANKS; i++) in[i] = i < g.num_tanks ? inputs[i] : 0;
    game_tick(&g, in);
}

EXPORT("ofp_checksum") unsigned ofp_checksum(void) { return game_checksum(&g); }
EXPORT("ofp_rules") unsigned ofp_rules(void) { return net_rules(); }     /* (the rules fingerprint: CI compares) */

/* 0 tick, 1 tanks, 2 winner, 3 time up, 4 biome, 5 time of day, 6 weather, 7..9 the limits */
EXPORT("ofp_info") int ofp_info(int what)
{
    switch (what) {
    case 0: return (int)g.tick;   case 1: return g.num_tanks;   case 2: return g.winner;
    case 3: return g.time_up;     case 4: return g.biome;       case 5: return g.map.time;
    case 6: return g.weather;     case 7: return MAX_SHOTS;     case 8: return MAX_MEN;
    case 9: return MAX_HELIS;     case 10: return (int)g.time_limit;
    case 11: return g.extras;     case 12: return g.hill_tx;   case 13: return g.hill_ty;
    case 14: return (int)g.hill[0]; case 15: return (int)g.hill[1]; case 16: return g.hill_holder;
    /* the modes: conquest's points and score, team deathmatch's kills, the warlords, the last one, an arena */
    case 17: return g.num_cp;       case 18: return g.cp_score[0]; case 19: return g.cp_score[1];
    case 20: case 21: {                                  /* (team deathmatch: each side's kills) */
        int k, n = 0;
        for (k = 0; k < g.num_tanks; k++) if ((g.tanks[k].team & 1) == what - 20) n += g.tanks[k].tank_kills;
        return n;
    }
    case 22: return g.jug[0];       case 23: return g.jug[1];
    case 24: return g.jug_score[0]; case 25: return g.jug_score[1];
    case 26: return g.ffa_winner;   case 27: return g.map.arena;
    case 28: return g.map.w;                             /* (the map's own size: 64 .. 192) */
    /* kill confirmed's and hardpoint's: the scores, the point that counts, how many tags there can be */
    case 29: return g.hp_active;    case 30: return g.hp_score[0]; case 31: return g.hp_score[1];
    case 32: return g.kc_score[0];  case 33: return g.kc_score[1]; case 34: return MAX_TAGS;
    case 35: return (int)((HP_MOVE - g.tick % HP_MOVE) / TICK_RATE);   /* (seconds before the point moves) */
    case 36: return g.weather_changing ? WEATHER_CHANGING : g.weather;
    default: return 0;
    }
}

/* a tile: its type, and (bits 8+) the team + 1 of the structure on it, bit 12 if it stands */
EXPORT("ofp_tile") int ofp_tile(int x, int y)
{
    int t, sid;
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return TILE_ROCK;
    t = g.map.type[y][x];
    sid = g.map.sid[y][x];
    if (sid) {
        const Structure *st = &g.map.st[sid - 1];
        t |= ((st->team & 1) + 1) << 8;
        if (st->hp) t |= 1 << 12;
    }
    return t;
}

/* 0 x, 1 y (px), 2 angle (0..65535), 3 team, 4 on the field, 5 class, 6 carries the flag, 7 armour, 8 score */
EXPORT("ofp_tank") int ofp_tank(int i, int f)
{
    const Tank *t = &g.tanks[i < 0 || i >= MAX_TANKS ? 0 : i];
    switch (f) {
    case 0: return (int)(t->x >> 16);   case 1: return (int)(t->y >> 16);   case 2: return t->angle;
    case 3: return t->team;             case 4: return t->active && !t->respawn;   case 5: return t->cls;
    case 6: return t->has_flag;         case 7: return t->armor;             case 8: return (int)t->score;
    default: return 0;
    }
}

/* shots: 0 active, 1 x, 2 y, 3 team, 4 kind */
EXPORT("ofp_shot") int ofp_shot(int i, int f)
{
    const Shot *s = &g.shots[i];
    return f == 0 ? s->active : f == 1 ? (int)(s->x >> 16) : f == 2 ? (int)(s->y >> 16) : f == 3 ? s->team : s->kind;
}

/* men: 0 kind (0 none), 1 x, 2 y, 3 team */
EXPORT("ofp_man") int ofp_man(int i, int f)
{
    const Man *m = &g.men[i];
    return f == 0 ? m->kind : f == 1 ? (int)(m->x >> 16) : f == 2 ? (int)(m->y >> 16) : m->team;
}

/* helicopters: 0 active, 1 x, 2 y */
EXPORT("ofp_heli") int ofp_heli(int i, int f)
{
    const Heli *h = &g.helis[i];
    return f == 0 ? h->active : f == 1 ? (int)(h->x >> 16) : (int)(h->y >> 16);
}

/* pickups: 0 active, 1 tile x, 2 tile y, 3 kind */
EXPORT("ofp_pickup") int ofp_pickup(int i, int f)
{
    const Pickup *p = &g.pickups[i < 0 || i >= MAX_PICKUPS ? 0 : i];
    return f == 0 ? p->active : f == 1 ? p->tx : f == 2 ? p->ty : p->kind;
}

/* the flags (index = owning team): 0 x, 1 y, 2 state, 3 carried by (tank or 255) */
/* conquest's capture point i: its tile (0, 1), whose it is (2: -1 nobody's), how far taken (3: -CP_TAKE..CP_TAKE) */
EXPORT("ofp_cp") int ofp_cp(int i, int f)
{
    if (i < 0 || i >= g.num_cp) return 0;
    return f == 0 ? g.cp_tx[i] : f == 1 ? g.cp_ty[i] : f == 2 ? g.cp_owner[i] : g.cp_take[i];
}

/* kill confirmed's tag i: 0 lying there, 1 x, 2 y (pixels), 3 the fallen tank's side, 4 ticks left */
EXPORT("ofp_tag") int ofp_tag(int i, int f)
{
    if (i < 0 || i >= MAX_TAGS) return 0;
    return f == 0 ? g.tags[i].active : f == 1 ? (int)(g.tags[i].x >> 16) : f == 2 ? (int)(g.tags[i].y >> 16)
         : f == 3 ? g.tags[i].team : g.tags[i].ttl;
}

EXPORT("ofp_flag") int ofp_flag(int i, int f)
{
    const Flag *fl = &g.flags[i & 1];
    return f == 0 ? (int)(fl->x >> 16) : f == 1 ? (int)(fl->y >> 16) : f == 2 ? fl->state : fl->carrier;
}
