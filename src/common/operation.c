/* SPDX-License-Identifier: MIT */
#include <string.h>
#include "common/operation.h"

enum { GUN_X = 61, GUN_Y = 39, POW_X = 76, POW_Y = 30, MED_X = 23, MED_Y = 38 };

static void paint(Map *m, int l, int t, int r, int b, TileType tile)
{
    int x, y;
    for (y = t; y <= b; y++)
        for (x = l; x <= r; x++) map_set(m, x, y, tile);
}

static int build_ground(Game *g, char *text, unsigned cap, int biome, int challenge)
{
    Map *m = &g->map;
    int y;
    map_set_next_size(SIZE_MEDIUM);
    map_fill(m, TILE_WATER);
    map_set_next_size(SIZE_LARGE);
    for (y = 13; y <= 80; y++) {
        int bend = (y < 23 ? 23 - y : y > 70 ? y - 70 : 0) / 2;
        int ripple = ((y / 3) % 5 == 1) - ((y / 5) % 4 == 3);
        paint(m, 7 + bend, y, 36 - bend + ripple, y, TILE_SAND);
        paint(m, 59 + bend - ripple, y, 88 - bend, y, TILE_SAND);
    }
    paint(m, 19, 34, 80, 37, TILE_ROAD);                /* north causeway */
    paint(m, 25, 64, 72, 67, TILE_ROAD);                /* alternate crossing */
    paint(m, 28, 36, 31, 66, TILE_ROAD);
    paint(m, 68, 36, 71, 66, TILE_ROAD);
    paint(m, 33, 21, 35, 24, TILE_CANOPY);
    paint(m, 15, 50, 18, 53, TILE_CANOPY);
    paint(m, 62, 54, 64, 57, TILE_CANOPY);
    paint(m, 78, 54, 80, 56, TILE_CANOPY);
    paint(m, 73, 24, 84, 24, TILE_WALL);
    paint(m, 73, 24, 73, 33, TILE_WALL);
    paint(m, 84, 24, 84, 33, TILE_WALL);
    paint(m, 45, 48, 47, 50, TILE_SAND);
    paint(m, 49, 81, 52, 83, TILE_SAND);
    m->quiet = m->preserve_roads = m->has_weather = 1;
    m->time = TIME_DAY; m->weather = WEATHER_CLEAR; m->has_extras = 0;
    map_custom_begin(m, g->bases, 4, MAP_MODE_CLASSIC);
    if (!map_custom_zone(m, g->bases, TEAM_PLAYER, 10, 24, 32, 47) ||
        !map_custom_zone(m, g->bases, TEAM_ENEMY, 73, 43, 86, 55)) return 0;
    map_place_structure(m, ST_GARAGE, 15, 28, TEAM_PLAYER);
    map_place_structure(m, ST_GARAGE, 22, 28, TEAM_PLAYER);
    map_place_structure(m, ST_GARAGE, 74, 46, TEAM_ENEMY);
    map_place_structure(m, ST_GARAGE, 81, 46, TEAM_ENEMY);
    map_place_structure(m, ST_FIRSTAID, MED_X, MED_Y, TEAM_PLAYER);
    map_place_structure(m, ST_DEPOT, 12, 41, TEAM_PLAYER);
    map_place_structure(m, ST_T_CANNON, GUN_X, GUN_Y, TEAM_INDEPENDENT);
    map_place_structure(m, ST_POWCAMP, POW_X, POW_Y, TEAM_ENEMY);
    map_place_structure(m, ST_WAREHOUSE, 76, 26, TEAM_ENEMY);
    if (challenge == CHALLENGE_RAID + 1) {
        map_place_structure(m, ST_T_CANNON, 66, 48, TEAM_INDEPENDENT);
        map_place_structure(m, ST_T_CANNON, 78, 60, TEAM_INDEPENDENT);
    }
    return mapfile_save(m, g->bases, biome, "Field Challenge", text, cap) != 0;
}

static int start(Game *g, DesertOperation *op, const MatchSetup *crew, int challenge)
{
    static char text[MAPFILE_MAX];
    MatchSetup ms = {0};
    int i;
    memset(op, 0, sizeof *op);
    op->challenge = challenge;
    ms.seed = 1987u;
    ms.biome = BIOME_DESERT;
    if (challenge == CHALLENGE_RESCUE + 1) ms.biome = BIOME_JUNGLE;
    if (challenge == CHALLENGE_RAID + 1) ms.biome = BIOME_VOLCANIC;
    ms.players = crew->players == 2 ? 2 : 1;
    ms.cls[0] = TANK_MEDIUM;
    ms.cls[1] = TANK_HEAVY;
    ms.steer[0] = crew->steer[0];
    ms.steer[1] = crew->steer[1];
    ms.relaxed_pace = crew->relaxed_pace;
    ms.time_limit = 8 * 60;
    if (challenge) ms.time_limit = challenge == CHALLENGE_RESCUE + 1 ? 3 * 60 : 2 * 60;
    ms.time_rule = TIME_RULE_DRAW;
    if (!build_ground(g, text, sizeof text, ms.biome, challenge) || match_start(g, &ms, text) != 0 || g->num_tanks != ms.players) return -1;
    op->gun = g->map.sid[GUN_Y][GUN_X] - 1;
    op->prison = g->map.sid[POW_Y][POW_X] - 1;
    op->medic = g->map.sid[MED_Y][MED_X] - 1;
    if (op->gun < 0 || op->prison < 0 || op->medic < 0) return -1;
    memset(g->mines, 0, sizeof g->mines);                /* keep the scripted crossings clear */
    for (i = 0; i < 2; i++) g->flags[i].state = FLAG_HIDDEN;
    for (i = 0; i < ms.players; i++) g->tanks[i].lives = challenge == CHALLENGE_RAID + 1 ? 1 : 3;
    op->guns[0] = op->gun; op->gun_count = 1;
    if (challenge == CHALLENGE_RAID + 1) {
        op->guns[1] = g->map.sid[48][66] - 1;
        op->guns[2] = g->map.sid[60][78] - 1;
        if (op->guns[1] < 0 || op->guns[2] < 0) return -1;
        op->gun_count = 3;
    }
    op->phase = challenge == CHALLENGE_BRIDGE + 1 ? OP_CAPTURE : OP_GUN;
    if (op->phase == OP_CAPTURE) {
        g->extras = EXTRA_CONQUEST;
        g->num_cp = 1; g->cp_tx[0] = 48; g->cp_ty[0] = 35;
        g->cp_owner[0] = -1; g->cp_take[0] = 0;
    }
    return 0;
}

int operation_start(Game *g, DesertOperation *op, const MatchSetup *crew) { return start(g, op, crew, 0); }
int operation_challenge_start(Game *g, DesertOperation *op, const MatchSetup *crew, int challenge)
{
    if (challenge < 0 || challenge >= CHALLENGE_COUNT) return -1;
    return start(g, op, crew, challenge + 1);
}

const char *operation_name(const DesertOperation *op)
{
    static const char *const names[] = { "SAND BRIDGE", "TIMED RESCUE", "BRIDGE CAPTURE", "ONE TANK RAID" };
    return names[op->challenge >= 0 && op->challenge <= CHALLENGE_COUNT ? op->challenge : 0];
}

void operation_step(Game *g, DesertOperation *op)
{
    int i, living = 0, free_pow = 0;
    if (op->phase == OP_INACTIVE || op->phase >= OP_WON) return;
    op->saved = op->aboard = 0;
    for (i = 0; i < g->num_tanks; i++) {
        const Tank *t = &g->tanks[i];
        op->saved += t->saved;
        op->aboard += t->pows;
        living += !t->out;
    }
    op->destroyed = 0;
    for (i = 0; i < op->gun_count; i++) op->destroyed += !g->map.st[op->guns[i]].hp;
    if (op->challenge == CHALLENGE_BRIDGE + 1 || op->challenge == CHALLENGE_RAID + 1) {
        int won = op->challenge == CHALLENGE_BRIDGE + 1 ? g->cp_owner[0] == TEAM_PLAYER : op->destroyed == op->gun_count;
        if (won) { op->phase = OP_WON; g->winner = TEAM_PLAYER; }
        else if (!living || g->time_up) { op->phase = OP_LOST; g->winner = TEAM_ENEMY; }
        else if (op->challenge == CHALLENGE_RAID + 1)
            for (i = 0; i < op->gun_count; i++) if (g->map.st[op->guns[i]].hp) { op->gun = op->guns[i]; break; }
        return;
    }
    if (op->saved >= OP_RESCUE_GOAL && !g->map.st[op->gun].hp) {
        op->phase = OP_WON;
        g->winner = TEAM_PLAYER;
        return;
    }
    if (!g->map.st[op->prison].hp)
        for (i = 0; i < MAX_MEN; i++) free_pow += g->men[i].kind == MAN_POW;
    if (!living || g->time_up || !g->map.st[op->medic].hp ||
        (!g->map.st[op->prison].hp && free_pow + op->aboard + op->saved < OP_RESCUE_GOAL)) {
        op->phase = OP_LOST;
        g->time_up = 0;
        g->winner = TEAM_ENEMY;
    } else if (g->map.st[op->gun].hp) op->phase = OP_GUN;
    else if (g->map.st[op->prison].hp) op->phase = OP_PRISON;
    else op->phase = op->aboard + op->saved >= OP_RESCUE_GOAL ? OP_RETURN : OP_PICKUP;
}

int operation_target(const Game *g, const DesertOperation *op, int player, fx *x, fx *y)
{
    int id;
    const Tank *t = &g->tanks[player];
    if (op->phase == OP_CAPTURE) { *x = FX(48 * TILE_SIZE + 8); *y = FX(35 * TILE_SIZE + 8); return 1; }
    if (op->phase == OP_GUN) id = op->gun;
    else if (op->phase == OP_PRISON) id = op->prison;
    else if (op->phase == OP_RETURN || (op->phase == OP_PICKUP && t->pows >= tank_stats[t->cls].pows)) id = op->medic;
    else if (op->phase == OP_PICKUP) {
        int i, best = -1, closest = 0;
        for (i = 0; i < MAX_MEN; i++) if (g->men[i].kind == MAN_POW) {
            int d = fx_to_int(fx_abs(t->x - g->men[i].x) + fx_abs(t->y - g->men[i].y));
            if (best < 0 || d < closest) { best = i; closest = d; }
        }
        if (best >= 0) { *x = g->men[best].x; *y = g->men[best].y; return 1; }
        id = op->medic;
    } else return 0;
    {
        const Structure *st = &g->map.st[id];
        const StructInfo *info = &struct_info[st->type];
        *x = FX(st->x * TILE_SIZE + info->w * TILE_SIZE / 2);
        *y = FX(st->y * TILE_SIZE + info->h * TILE_SIZE / 2);
    }
    return 1;
}

const char *operation_prompt(const DesertOperation *op)
{
    static const char *const text[] = {
        "", "DESTROY THE BRIDGE GUN", "BREACH THE PRISON", "STOP BESIDE PRISONERS",
        "RETURN TO FIRST AID", "HOLD THE BRIDGE CHECKPOINT", "CHALLENGE COMPLETE", "CHALLENGE FAILED"
    };
    return text[op->phase];
}
