/* SPDX-License-Identifier: MIT
 * TRAINING, shared by every frontend (training.h). */
#include <string.h>
#include "common/training.h"
#include "core/match.h"

/* the nearest trees to tank 0 (0: the map has none) */
static int nearest_trees(const Game *g, fx *tx, fx *ty)
{
    const Tank *t = &g->tanks[0];
    int x, y, found = 0;
    int64_t bd = 0;
    for (y = 1; y < g->map.h - 1; y++)
        for (x = 1; x < g->map.w - 1; x++) {
            int64_t dx, dy, d;
            if (g->map.type[y][x] != TILE_CANOPY) continue;
            dx = ((int64_t)FX(x * TILE_SIZE + TILE_SIZE / 2) - t->x) >> 8; dy = ((int64_t)FX(y * TILE_SIZE + TILE_SIZE / 2) - t->y) >> 8;
            d = dx * dx + dy * dy;
            if (!found || d < bd) { found = 1; bd = d; *tx = FX(x * TILE_SIZE + TILE_SIZE / 2); *ty = FX(y * TILE_SIZE + TILE_SIZE / 2); }
        }
    return found;
}

/* the training ground: a small map, your camp and their fortress, a grove beside your camp; the buildings
 * and the prison camp are training_clean_up's, once the match has started (a base= line builds a base
 * afresh, guns and all) */
static char ground_text[MAPFILE_MAX];
const char *training_ground(void)
{
    static Game tg;
    Map *m = &tg.map;
    int i, n = 0, plain = 0, x, y, k;
    map_set_next_size(SIZE_SMALL);
    worldgen_set_camp_garages(4);
    game_init(&tg, 2026u, BIOME_CLASSIC);
    map_set_next_size(SIZE_LARGE);
    for (y = 1; y < m->h - 1; y++)                       /* the country: clear (its roads kept) */
        for (x = 1; x < m->w - 1; x++) {
            int inb = 0;
            for (k = 0; k < 2; k++) inb |= x >= tg.bases[k].x0 && x <= tg.bases[k].x1 && y >= tg.bases[k].y0 && y <= tg.bases[k].y1;
            if (!inb && m->type[y][x] != TILE_ROAD) map_set(m, x, y, TILE_SAND);
        }
    (void)i; (void)n; (void)plain;
    {   /* the grove: beside our camp's way out (the buildings: after the match starts, train_clean_up) */
        const Base *pb = &tg.bases[TEAM_PLAYER];
        int gx = pb->x1 + 3 < m->w - 6 ? pb->x1 + 3 : pb->x0 - 6, gy = (pb->y0 + pb->y1) / 2;
        for (y = gy - 2; y <= gy + 2; y++)
            for (x = gx; x < gx + 4; x++)
                if (x > 0 && y > 0 && x < m->w - 1 && y < m->h - 1 && m->type[y][x] == TILE_SAND) map_set(m, x, y, TILE_CANOPY);
    }
    if (!mapfile_save(m, tg.bases, BIOME_CLASSIC, "Training", ground_text, sizeof ground_text)) ground_text[0] = 0;
    return ground_text;
}

void training_setup(MatchSetup *ms)
{
    memset(ms, 0, sizeof *ms);
    ms->seed = 2026u;
    ms->players = 1;
    ms->team[0] = TEAM_PLAYER;
    ms->cls[0] = TANK_MEDIUM;
    ms->ai_level = AI_OFF;                               /* (no computer tank) */
    ms->weather = WEATHER_CLEAR;
    ms->camp_garages = 4;
}

void training_clean_up(Game *g)
{
    static Structure was[MAX_STRUCTS];
    static uint8_t renum[MAX_STRUCTS];
    Map *m = &g->map;
    int i, n = m->num_st, plain = 0, x, y;
    memcpy(was, m->st, sizeof was);
    for (i = 0; i < n; i++) {                            /* every footprint: clear */
        const StructInfo *si = &struct_info[was[i].type];
        for (y = was[i].y; y < was[i].y + si->h; y++)
            for (x = was[i].x; x < was[i].x + si->w; x++) { map_set(m, x, y, TILE_SAND); m->sid[y][x] = 0; }
    }
    m->num_st = 0;
    for (i = 0; i < n; i++) {                            /* what's kept, placed again (in the same order) */
        const Structure *st = &was[i];
        const StructInfo *si = &struct_info[st->type];
        int keep = st->team == TEAM_PLAYER ? !(si->flags & SF_TURRET)
                 : !(si->flags & SF_TURRET) && st->type != ST_HELIPAD && st->type != ST_BARRACKS && st->type != ST_POWCAMP &&
                   (st->type == ST_GARAGE || plain++ < 3);
        renum[i] = 255;
        if (keep) { int k = map_place_structure(m, (StructType)st->type, st->x, st->y, st->team); if (k >= 0) renum[i] = (uint8_t)k; }
    }
    for (i = 0; i < MAX_HELIS; i++) g->helis[i].active = 0;   /* (no helicopters: none in the air either, */
    g->calm = 1;                                       /*  none called in for standing still) */
    for (i = 0; i < MAX_MEN; i++) {                      /* their soldiers: gone; ours: their barracks' new number */
        Man *mn = &g->men[i];
        if (mn->kind == MAN_SOLDIER && mn->team != TEAM_PLAYER) mn->kind = MAN_NONE;
        else if (mn->kind == MAN_SOLDIER && mn->home != 255) mn->home = mn->home < n ? renum[mn->home] : 255;
    }
    {   /* the prison camp: the nearest clear 3 x 3 to halfway between the bases */
        const Base *pb = &g->bases[TEAM_PLAYER], *eb = &g->bases[TEAM_ENEMY];
        int mx = ((pb->x0 + pb->x1) / 2 + (eb->x0 + eb->x1) / 2) / 2, my = ((pb->y0 + pb->y1) / 2 + (eb->y0 + eb->y1) / 2) / 2, r, placed = 0;
        for (r = 0; r < 20 && !placed; r++)
            for (y = my - r; y <= my + r && !placed; y++)
                for (x = mx - r; x <= mx + r && !placed; x++) {
                    int a, b, ok = x > 1 && y > 1 && x + 4 < m->w && y + 4 < m->h;
                    for (b = -1; b <= 3 && ok; b++) for (a = -1; a <= 3 && ok; a++) ok = m->type[y + b][x + a] == TILE_SAND && !m->sid[y + b][x + a];
                    if (ok) placed = map_place_structure(m, ST_POWCAMP, x, y, TEAM_ENEMY) >= 0;
                }
    }
}

void training_begin(Training *tr, const Game *g)
{
    tr->step = TR_DRIVE;
    tr->mark = 0;
    tr->x0 = g->tanks[0].x;
    tr->y0 = g->tanks[0].y;
}

int training_target(const Game *g, const Training *tr, fx *tx, fx *ty)
{
    const Tank *t = &g->tanks[0];
    int i, best = -1;
    int64_t bd = 0;
    if (tr->step == TR_RESCUE) {
        if (t->pows) {                                   /* men on board: to first aid */
            for (i = 0; i < g->map.num_st; i++) {
                const Structure *st = &g->map.st[i];
                int64_t dx, dy, d;
                if (st->type != ST_FIRSTAID || st->team != TEAM_PLAYER || !st->hp) continue;
                dx = (int64_t)FX(st->x * TILE_SIZE + TILE_SIZE) - t->x; dy = (int64_t)FX(st->y * TILE_SIZE + TILE_SIZE) - t->y;
                d = (dx >> 8) * (dx >> 8) + (dy >> 8) * (dy >> 8);
                if (best < 0 || d < bd) { best = i; bd = d; }
            }
            if (best < 0) return 0;
            *tx = FX(g->map.st[best].x * TILE_SIZE + TILE_SIZE); *ty = FX(g->map.st[best].y * TILE_SIZE + TILE_SIZE);
            return 1;
        }
        for (i = 0; i < MAX_MEN; i++) {                  /* else: the nearest prisoner */
            const Man *m = &g->men[i];
            int64_t dx, dy, d;
            if (m->kind != MAN_POW) continue;
            dx = (int64_t)m->x - t->x; dy = (int64_t)m->y - t->y;
            d = (dx >> 8) * (dx >> 8) + (dy >> 8) * (dy >> 8);
            if (best < 0 || d < bd) { best = i; bd = d; }
        }
        if (best >= 0) { *tx = g->men[best].x; *ty = g->men[best].y; return 1; }
        for (i = 0; i < g->map.num_st; i++) {          /* none out yet: the nearest prison camp */
            const Structure *st = &g->map.st[i];
            int64_t dx, dy, d;
            if (st->type != ST_POWCAMP || !st->hp) continue;
            dx = (int64_t)FX(st->x * TILE_SIZE + TILE_SIZE * 3 / 2) - t->x; dy = (int64_t)FX(st->y * TILE_SIZE + TILE_SIZE * 3 / 2) - t->y;
            d = (dx >> 8) * (dx >> 8) + (dy >> 8) * (dy >> 8);
            if (best < 0 || d < bd) { best = i; bd = d; }
        }
        if (best < 0) return 0;
        *tx = FX(g->map.st[best].x * TILE_SIZE + TILE_SIZE * 3 / 2); *ty = FX(g->map.st[best].y * TILE_SIZE + TILE_SIZE * 3 / 2);
        return 1;
    }
    if (tr->step == TR_TREES) return nearest_trees(g, tx, ty);   /* the nearest trees */
    if (tr->step == TR_FLAG) {                          /* the enemy's flag, or home with it */
        const Flag *f = t->has_flag ? &g->flags[TEAM_PLAYER] : &g->flags[TEAM_ENEMY];
        *tx = t->has_flag ? f->home_x : f->x; *ty = t->has_flag ? f->home_y : f->y;
        return 1;
    }
    return 0;
}

void training_check(const Game *g, Training *tr)
{
    const Tank *t = &g->tanks[0];
    int64_t dx = (int64_t)t->x - tr->x0, dy = (int64_t)t->y - tr->y0;
    int next = tr->step;
    switch (tr->step) {
    case TR_DRIVE:  if ((dx >> 16) * (dx >> 16) + (dy >> 16) * (dy >> 16) > 140 * 140) next = TR_SHOOT; break;
    case TR_SHOOT:  if ((int)t->score > tr->mark) next = TR_MINE; break;
    case TR_MINE:   if ((int)t->mines < tr->mark) next = TR_TREES; break;
    case TR_TREES: {                                     /* under the trees (none on this map: on) */
        fx ax, ay;
        int tx = fx_to_int(t->x) >> TILE_SHIFT, ty = fx_to_int(t->y) >> TILE_SHIFT;
        if (!nearest_trees(g, &ax, &ay) || g->map.type[ty][tx] == TILE_CANOPY) next = TR_RESCUE;
        break;
    }
    case TR_RESCUE: if (t->saved > 0) next = TR_FLAG; break;
    case TR_FLAG:   if (g->winner == TEAM_PLAYER) next = TR_DONE; break;
    default: break;
    }
    if (next != tr->step) {
        tr->step = next;
        tr->mark = tr->step == TR_SHOOT ? (int)t->score : tr->step == TR_MINE ? (int)t->mines : 0;
    }
}
