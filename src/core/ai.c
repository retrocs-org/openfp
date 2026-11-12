/* SPDX-License-Identifier: MIT
 *
 * The computer opponent.
 *
 * Each tick it looks at the game and decides which buttons to press, exactly
 * like a player at the keyboard, so it obeys every rule a player does.
 *
 * What it wants, in order:
 *   - carrying your flag: run it home (and drop mines on a chaser, on hard)
 *   - you are carrying its flag: chase you down
 *   - badly hurt or low on fuel: back to its garage until patched up
 *   - its own flag lying about: take it back
 *   - your flag lying in the open: fetch it
 *   - otherwise: raid your camp and blast open your flag bunker
 * and whenever it sees you on the way, it turns and fights.
 *
 * Routes come from a cost field over the tile map (Dijkstra from the goal):
 * roads are quick, walls and buildings can be shot through at a price, water,
 * rock, garages and its own side's mines are no-go. The tank follows the
 * field downhill, two tiles ahead, shooting whatever stands in the way.
 *
 * No floats, no OS calls: the same game state gives the same buttons
 * everywhere, so games replay and will run in lockstep over a network.
 */
#include <string.h>
#include "core/game.h"
#include "core/game_internal.h"

#define NOWAY      0xFFFFu
#define REPATH     40                /* ticks between route updates */

/* aim slack, sight in px, % chance to pull the trigger, when the first raid
 * on your camp comes (ticks), how close to its camp you may come (tiles) */
/* react: ticks it must have you in sight before it fires; weakest: it picks the weakest enemy in sight, not the nearest */
typedef struct { int16_t tol; int detect, chance; uint32_t raid_at; int guard, react, weakest; } Skill;
static const Skill skill[4] = {
    { 0, 0, 0, 0, 0, 0, 0 },
    { 0x0C00, 100, 25, 4 * 60 * TICK_RATE, 24, 32, 0 },   /* easy: sloppy, slow to react, waits long before raiding */
    { 0x0600, 150, 65, 150 * TICK_RATE,     34, 12, 0 },   /* normal */
    { 0x0380, 200, 100, 90 * TICK_RATE,     44, 2, 1 },    /* hard: sharp, quick, leads its shots, mines pursuers, */
};                                                          /*       finishes off the weakest it sees */

/* ---- route field --------------------------------------------------------------------- */

/* Each edge costs at most (60 + 4 * 255) * 14 / 10. A ring larger
 * than that keeps pending distances ordered without a binary heap. */
enum { ROUTE_BUCKETS = 2048 };
static uint16_t route_head[ROUTE_BUCKETS];
static uint16_t route_next[MAP_W * MAP_H], route_prev[MAP_W * MAP_H];
static uint8_t  mine_here[MAP_H][MAP_W];

static inline void route_queue(uint16_t tile, unsigned distance, unsigned old)
{
    unsigned bucket = distance & (ROUTE_BUCKETS - 1);
    uint16_t first;
    if (old != NOWAY) {
        uint16_t prev = route_prev[tile], next = route_next[tile];
        if (prev == NOWAY) route_head[old & (ROUTE_BUCKETS - 1)] = next;
        else route_next[prev] = next;
        if (next != NOWAY) route_prev[next] = prev;
    }
    first = route_head[bucket];
    route_next[tile] = first;
    route_prev[tile] = NOWAY;
    if (first != NOWAY) route_prev[first] = tile;
    route_head[bucket] = tile;
}

/* cost of driving onto a tile, 0 = no way */
static inline int tile_cost(const Game *g, int tx, int ty, int team)
{
    TileType tt;
    const uint8_t *row = g->map.type[ty];
    int sid, c;
    if (mine_here[ty][tx]) return 0;                    /* our own minefields */
    tt = row[tx];
    sid = g->map.sid[ty][tx];
    if (tt == TILE_WATER || tt == TILE_ROCK) return 0;
    if (sid) {
        const Structure *st = &g->map.st[sid - 1];
        if ((struct_info[st->type].flags & SF_GATE) && (!st->hp || st->team == team)) return 6;
        if (tt == TILE_FLOOR) return 10;                 /* garage floors, helipads */
        if (struct_info[st->type].flags & SF_INDESTRUCTIBLE) return 0;
        if (st->team == team) return 0;                  /* we don't knock down our own */
        return 60 + st->hp * 4;                          /* their building: shoot a way through */
    }
    if (tile_info[tt].flags & TF_SOLID)
        return tile_info[tt].hp ? 45 : 0;                /* a wall: shoot a gap */
    c = tt == TILE_ROAD ? 6 : tt == TILE_TREE ? 12 :     /* (the roads: faster, so worth a little way round) */
        tt == TILE_SAND && (g->weather == WEATHER_STORM || g->biome == BIOME_SWAMP) ? 12 : 10;   /* (heavy going: the more so) */
    if ((tile_info[row[tx - 1]].flags | tile_info[row[tx + 1]].flags |
         tile_info[g->map.type[ty - 1][tx]].flags | tile_info[g->map.type[ty + 1][tx]].flags) & TF_SOLID)
        c += 4;                                         /* keep a little clear of walls */
    return c;
}

/* A single bounded local job; network/server AI always uses the synchronous path.
 * Local clients record resolved inputs, so replay simulation never runs this scheduler. */
static unsigned local_route_budget, local_route_d, local_route_tick;
static int local_route_pending, local_route_goal, local_route_stage, local_route_y;
static Game *local_route_game;
static AiState *local_route_owner;
static uint16_t local_route_dist[MAP_W * MAP_H];

static int build_route(Game *g, AiState *a, int team, int gx, int gy)
{
    static uint16_t cost[MAP_H][MAP_W];
    int i, x, y, pending = 1, goal;
    int width = g->map.w < MAP_W ? g->map.w : MAP_W;
    int height = g->map.h < MAP_H ? g->map.h : MAP_H;
    unsigned d = 0, work = 0;
    uint16_t *dist = local_route_budget ? local_route_dist : a->dist;
    if (local_route_budget && local_route_owner && (local_route_game != g || g->tick < local_route_tick))
        local_route_owner = NULL;
    if (local_route_budget && local_route_owner) {
        if (local_route_owner != a) return 0;
        pending = local_route_pending; d = local_route_d; goal = local_route_goal;
        if (!local_route_stage) goto prepare_cost;
        goto resume_search;
    }
    if (!local_route_budget) local_route_owner = NULL;
    memset(mine_here, 0, sizeof mine_here);
    for (i = 0; i < MAX_MINES; i++) {                    /* we know where our side's mines are */
        const Mine *m = &g->mines[i];
        if (m->active && m->team == team) {
            int mx = fx_to_int(m->x) >> TILE_SHIFT, my = fx_to_int(m->y) >> TILE_SHIFT;
            if (mx >= 0 && my >= 0 && mx < MAP_W && my < MAP_H) mine_here[my][mx] = 1;
        }
    }
    for (y = 0; y < height; y++)                        /* only the map's own area */
        memset(cost[y], 0, width * sizeof cost[0][0]);
    if (local_route_budget) {
        local_route_owner = a; local_route_game = g; local_route_tick = g->tick;
        local_route_stage = 0; local_route_y = 1; local_route_goal = gy * MAP_W + gx;
    }
prepare_cost:
    for (y = local_route_budget ? local_route_y : 1; y < height && y < MAP_H - 1; y++) {
        for (x = 1; x < width && x < MAP_W - 1; x++)
            cost[y][x] = (uint16_t)tile_cost(g, x, y, team);
        work += (unsigned)width;
        if (local_route_budget && work >= local_route_budget && y + 1 < height && y + 1 < MAP_H - 1) {
            local_route_y = y + 1;
            return 0;
        }
    }
    if (local_route_budget) { gx = local_route_goal % MAP_W; gy = local_route_goal / MAP_W; local_route_stage = 1; }
    for (i = 0; i < g->num_props; i++) {                 /* parked vehicles: in the way, all of them */
        const Prop *p = &g->props[i];
        const PropInfo *pi = &prop_info[p->kind];
        fx sn = fx_sin(p->angle), cs = fx_cos(p->angle), al, ac;
        if (!p->alive) continue;
        for (al = -pi->half_l; al <= pi->half_l; al += FX(6))
            for (ac = -pi->half_w; ac <= pi->half_w; ac += FX(5)) {
                int px = fx_to_int(p->x + fx_mul(al, sn) + fx_mul(ac, cs)) >> TILE_SHIFT;
                int py = fx_to_int(p->y - fx_mul(al, cs) + fx_mul(ac, sn)) >> TILE_SHIFT;
                if (px < 0 || py < 0 || px >= MAP_W || py >= MAP_H || !cost[py][px]) continue;
                /* ours: drive round; theirs: worth a shell if it's the short way */
                cost[py][px] = (uint16_t)(p->team == team ? 0 : cost[py][px] < 60 ? cost[py][px] + 50 : cost[py][px]);
            }
    }
    for (y = 0; y < height; y++)
        memset(&dist[y * MAP_W], 0xFF, width * sizeof dist[0]);
    if (gx < 0 || gy < 0 || gx >= width || gy >= height) return 1;
    memset(route_head, 0xff, sizeof route_head);
    goal = gy * MAP_W + gx;
    dist[goal] = 0;
    route_queue((uint16_t)goal, 0, NOWAY);
    if (local_route_budget) {
        local_route_owner = a; local_route_game = g; local_route_tick = g->tick;
        local_route_goal = goal;
    }
resume_search:
    work = 0;
    while (pending) {
        unsigned straight, diagonal, bucket = d & (ROUTE_BUCKETS - 1);
        uint16_t tile = route_head[bucket], next;
        int cx, cy, k;
        if (tile == NOWAY) { d++; continue; }
        next = route_next[tile];
        route_head[bucket] = next;
        if (next != NOWAY) route_prev[next] = NOWAY;
        pending--;
        work++;
        cx = tile % MAP_W;
        cy = tile / MAP_W;
        /* Reverse search: the cost of the tile we'd be driving onto. */
        straight = tile != goal && cost[cy][cx] ? cost[cy][cx] : 10;
        diagonal = straight * 14 / 10;
        for (k = 0; k < 8; k++) {
            static const int dx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, dy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
            int nx = cx + dx[k], ny = cy + dy[k];
            unsigned nd, neighbour, old;
            if ((unsigned)nx >= (unsigned)width || (unsigned)ny >= (unsigned)height || !cost[ny][nx]) continue;
            if (k >= 4 && (!cost[cy][nx] || !cost[ny][cx])) continue;   /* no cutting corners */
            nd = d + (k < 4 ? straight : diagonal);
            neighbour = ny * MAP_W + nx;
            old = dist[neighbour];
            if (nd < old) {
                dist[neighbour] = (uint16_t)nd;
                route_queue((uint16_t)neighbour, nd, old);
                pending += old == NOWAY;
            }
        }
        if (local_route_budget && work >= local_route_budget && pending) {
            local_route_pending = pending; local_route_d = d;
            return 0;
        }
    }
    if (local_route_budget) {
        memcpy(a->dist, local_route_dist, sizeof a->dist);
        local_route_owner = NULL;
    }
    return 1;
}

/* the best of the 8 tiles around (x,y) on the way to the goal, or -1 */
static int downhill(const AiState *a, int x, int y)
{
    int k, best = -1;
    uint32_t bd = a->dist[y * MAP_W + x];
    for (k = 0; k < 8; k++) {
        static const int dx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, dy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
        int nx = x + dx[k], ny = y + dy[k];
        if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) continue;
        if (a->dist[ny * MAP_W + nx] < bd) { bd = a->dist[ny * MAP_W + nx]; best = ny * MAP_W + nx; }
    }
    return best;
}

/* ---- driving -------------------------------------------------------------------------- */

static InputBits steer(const Game *g, const Tank *t, angle16 want, int go)
{
    const TankStats *st = world_tank_stats(g->vehicle_world, (TankClass)t->cls);
    int16_t diff = (int16_t)(want - t->angle);
    InputBits in = 0;
    int slack = (int)st->turn_rate;
    if (diff > slack) in |= IN_RIGHT;
    else if (diff < -slack) in |= IN_LEFT;
    if (go > 0 && diff > -0x2800 && diff < 0x2800) in |= IN_UP;     /* roughly lined up: go */
    if (go < 0) in |= IN_DOWN;
    return in;
}

/* is something worth shooting right ahead: their building or turret, a
 * wall in our way, one of their vehicles? */
static int target_ahead(const Game *g, const Tank *t, int range)
{
    fx s = fx_sin(t->angle), c = fx_cos(t->angle);
    fx end_x = t->x + s * range, end_y = t->y - c * range;
    fx left = (end_x < t->x ? end_x : t->x) - FX(8), right = (end_x > t->x ? end_x : t->x) + FX(8);
    fx top = (end_y < t->y ? end_y : t->y) - FX(8), bottom = (end_y > t->y ? end_y : t->y) + FX(8);
    uint8_t candidates[MAX_PROPS];
    int d, i, count = 0;
    for (i = 0; i < g->num_props; i++) {
        const Prop *p = &g->props[i];
        if (p->alive && p->x > left && p->x < right && p->y > top && p->y < bottom)
            candidates[count++] = (uint8_t)i;
    }
    for (d = 10; d < range; d += 6) {
        fx x = t->x + s * d, y = t->y - c * d;
        int tx = fx_to_int(x) >> TILE_SHIFT, ty = fx_to_int(y) >> TILE_SHIFT;
        TileType tt = map_get(&g->map, tx, ty);
        if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return 0;
        for (i = 0; i < count; i++) {
            const Prop *p = &g->props[candidates[i]];
            if (gi_dist(p->x, p->y, x, y) < FX(8)) return p->team != t->team;
        }
        if (g->map.sid[ty][tx] && (tile_info[tt].flags & TF_BLOCKS_SHOT)) {
            const Structure *st = &g->map.st[g->map.sid[ty][tx] - 1];
            return st->team != t->team && !(struct_info[st->type].flags & SF_INDESTRUCTIBLE);
        }
        if (tile_info[tt].flags & TF_BLOCKS_SHOT)
            return d < 30 && tile_info[tt].hp && (tile_info[tt].flags & TF_SOLID);   /* wall right in front */
    }
    return 0;
}

/* Shoot enemy guns before driving into their fire on the way to a flag. */
static int raid_target(const Game *g, const Tank *t, fx goal_x, fx goal_y, angle16 *aim)
{
    fx range = FX(160), goal_dist = gi_dist(t->x, t->y, goal_x, goal_y);
    int i, found = 0;
    for (i = 0; i < g->map.num_st; i++) {
        const Structure *st = &g->map.st[i];
        fx x, y, d;
        if (!st->hp || st->team == t->team ||
            (!(struct_info[st->type].flags & SF_TURRET) && st->type != ST_FLAGHOUSE)) continue;
        gi_struct_centre(&g->map, i, &x, &y);
        d = gi_dist(t->x, t->y, x, y);
        if (d >= range || gi_dist(x, y, goal_x, goal_y) > goal_dist + FX(48) ||
            !gi_clear_shot(g, t->x, t->y, x, y, i + 1)) continue;
        *aim = fx_atan2(x - t->x, y - t->y);
        range = d;
        found = 1;
    }
    return found;
}

static int tile_of(fx v) { return fx_to_int(v) >> TILE_SHIFT; }

/* can the tank's whole body slide straight from a to b? */
static int body_clear(const Game *g, const Tank *t, fx bx, fx by, int team)
{
    fx ax = t->x, ay = t->y;
    int k, steps = fx_to_int(gi_dist(ax, ay, bx, by)) / 4 + 1;
    for (k = 1; k <= steps; k++) {
        fx x = ax + (fx)((int64_t)(bx - ax) * k / steps), y = ay + (fx)((int64_t)(by - ay) * k / steps);
        if (map_box_solid_for_team(&g->map, x, y, TANK_HALF, team)) return 0;
    }
    return 1;
}
static int abs16(int v) { return v < 0 ? -v : v; }

static int near_camp(const Base *b, const Tank *foe, int tiles)
{
    int cx = (b->x0 + b->x1) / 2, cy = (b->y0 + b->y1) / 2;
    int dx = abs16(tile_of(foe->x) - cx), dy = abs16(tile_of(foe->y) - cy);
    return dx < tiles && dy < tiles;
}

/* somewhere on our side worth keeping an eye on: one of our buildings within
 * reach of our camp, or a random spot on open ground near it */
static void pick_patrol(Game *g, AiState *a, int team)
{
    const Base *b = &g->bases[team & 1];
    int cx = (b->x0 + b->x1) / 2, cy = (b->y0 + b->y1) / 2, tries;
    for (tries = 0; tries < 40 && g->map.num_st > 0; tries++) {
        int i = gi_range(g, 0, g->map.num_st - 1), x, y;
        const Structure *st = &g->map.st[i];
        if (st->team != team || st->hp == 0) continue;
        x = st->x + struct_info[st->type].w / 2;
        y = st->y + struct_info[st->type].h + 1;
        if (abs16(x - cx) > 50 || abs16(y - cy) > 50 || (x > b->x0 && x < b->x1 && y > b->y0 && y < b->y1)) continue;
        if (y >= MAP_H - 1 || (tile_info[g->map.type[y][x]].flags & TF_SOLID) || g->map.type[y][x] == TILE_WATER) continue;
        {   /* trees within 6 tiles: wait under them (an ambush: unseen, as you're unseen there) */
            int dx2, dy2, best2 = 999;
            for (dy2 = -6; dy2 <= 6; dy2++)
                for (dx2 = -6; dx2 <= 6; dx2++) {
                    int tx2 = x + dx2, ty2 = y + dy2, d2 = dx2 * dx2 + dy2 * dy2;
                    if (tx2 < 1 || ty2 < 1 || tx2 >= g->map.w - 1 || ty2 >= g->map.h - 1 || d2 >= best2) continue;
                    if (g->map.type[ty2][tx2] == TILE_CANOPY) { best2 = d2; a->pat_tx = (int16_t)tx2; a->pat_ty = (int16_t)ty2; }
                }
            if (best2 == 999) { a->pat_tx = (int16_t)x; a->pat_ty = (int16_t)y; }
        }
        a->pat_t = (uint16_t)(30 * TICK_RATE);
        return;
    }
    a->pat_tx = (int16_t)(cx + gi_range(g, -20, 20)); a->pat_ty = (int16_t)(cy + gi_range(g, -20, 20));
    a->pat_t = (uint16_t)(20 * TICK_RATE);
}

int game_orders_allowed(const Game *g)
{
    return g->enhanced_rules && g->biome != BIOME_CLASSIC &&
           !(g->map.custom_bases && g->map.mode == MAP_MODE_CLASSIC) &&
           !(g->extras & EXTRA_ANNIHILATION);
}
const char *game_order_name(int kind)
{
    static const char *const names[] = { "CANCEL ORDER", "ATTACK OBJECTIVE", "DEFEND HERE", "ESCORT ME", "REGROUP HERE" };
    return names[kind >= 0 && kind < ORDER_COUNT ? kind : 0];
}
int game_order_active(const Game *g, int team)
{
    const TeamOrder *o;
    if ((unsigned)team > 1 || !game_orders_allowed(g)) return 0;
    o = &g->team_order[team];
    return o->kind > ORDER_NONE && o->kind < ORDER_COUNT && o->issuer < g->num_tanks &&
           (int32_t)(o->until - g->tick) > 0 && g->team_leader[team] == o->issuer &&
           !g->tanks[o->issuer].ai && !g->tanks[o->issuer].out &&
           (g->tanks[o->issuer].team & 1) == team;
}
int game_team_order(Game *g, int leader, int kind)
{
    TeamOrder *o; Tank *t; int team;
    if (!game_orders_allowed(g) || (unsigned)leader >= (unsigned)g->num_tanks ||
        (unsigned)kind >= ORDER_COUNT) return 0;
    t = &g->tanks[leader]; team = t->team & 1;
    if (t->ai || !t->active || t->out || t->respawn || g->team_leader[team] != leader) return 0;
    o = &g->team_order[team];
    o->kind = (uint8_t)kind; o->issuer = (uint8_t)leader;
    o->tx = (int16_t)tile_of(t->x); o->ty = (int16_t)tile_of(t->y);
    if (kind == ORDER_ATTACK) {
        int i, best = -1, distance = MAP_W * MAP_H * 2;
        o->tx = (int16_t)tile_of(g->flags[1 - team].x);
        o->ty = (int16_t)tile_of(g->flags[1 - team].y);
        for (i = 0; i < g->num_cp; i++) if (g->cp_owner[i] != team) {
            int dx = g->cp_tx[i] - tile_of(t->x), dy = g->cp_ty[i] - tile_of(t->y);
            if (dx * dx + dy * dy < distance) { best = i; distance = dx * dx + dy * dy; }
        }
        if (best >= 0) { o->tx = g->cp_tx[best]; o->ty = g->cp_ty[best]; }
    }
    o->until = g->tick + 30 * TICK_RATE;
    return 1;
}

InputBits ai_think(Game *g, int idx)
{
    Tank *t = &g->tanks[idx];
    AiState *a = &g->ai[idx];
    Skill adj = skill[t->ai > AI_HARD ? AI_HARD : t->ai];
    const Skill *sk = &adj;
    const TankStats *ts = world_tank_stats(g->vehicle_world, (TankClass)t->cls);
    const Base *home = &g->bases[t->team & 1];
    int raider = (g->raid_bots & (1u << idx)) != 0;
    int siege = (g->siege_bots & (1u << idx)) != 0;
    if (g->adapt && t->team == TEAM_ENEMY) {             /* adaptive: between easy, normal and hard */
        int s = g->adapt_skill < 0 ? 0 : g->adapt_skill > 200 ? 200 : g->adapt_skill;
        const Skill *lo = &skill[s >= 100 ? AI_NORMAL : AI_EASY], *hi = &skill[s >= 100 ? AI_HARD : AI_NORMAL];
        int f = s >= 100 ? s - 100 : s;                  /* 0..100 of the way */
        adj.tol = (int16_t)(lo->tol + (hi->tol - lo->tol) * f / 100);
        adj.detect = lo->detect + (hi->detect - lo->detect) * f / 100;
        adj.chance = lo->chance + (hi->chance - lo->chance) * f / 100;
        adj.raid_at = lo->raid_at - (uint32_t)((int64_t)(lo->raid_at - hi->raid_at) * f / 100);
        adj.guard = lo->guard + (hi->guard - lo->guard) * f / 100;
    }
    if (g->weather == WEATHER_STORM) adj.detect = adj.detect * 7 / 10;   /* nobody sees as far in a storm */
    Flag *own = &g->flags[t->team & 1], *theirs = &g->flags[1 - (t->team & 1)];
    const Tank *foe = NULL;
    InputBits in = 0;
    int gx, gy, i, mode, seen = 0, tx = tile_of(t->x), ty = tile_of(t->y), a_pick, a_hill, a_cp = -1;
    const Tank *escort = NULL;                           /* (warlord: the one of ours we guard) */
    fx foe_d = 0;
    uint32_t fuel_cap = (uint32_t)ts->fuel * TICK_RATE;

    if (t->respawn || t->out) { a->repath = 0; return 0; }
    if (a->mine_cd) a->mine_cd--;

    for (i = 0; i < g->num_tanks; i++) {                 /* the nearest enemy tank */
        const Tank *o = &g->tanks[i];
        fx d;
        if (!o->active || o->respawn || o == t) continue;
        if (o->team == t->team && !(g->extras & EXTRA_ANNIHILATION)) continue;   /* (every tank for itself: all foes) */
        if ((g->extras & EXTRA_ANNIHILATION) && (o->guard || near_camp(home, o, 0))) continue;   /* (not at the garages) */
        d = gi_dist(o->x, o->y, t->x, t->y);
        if (map_get(&g->map, tile_of(o->x), tile_of(o->y)) == TILE_CANOPY && d > FX(4 * TILE_SIZE)) continue;   /* (under the trees: unseen, unless close) */
        if (!foe || d < foe_d) { foe = o; foe_d = d; }
    }
    if (g->extras & EXTRA_WARLORD) {                  /* warlord: theirs is the one to get; every other one of us guards ours */
        int ej = g->jug[1 - (t->team & 1)], wl_own = g->jug[t->team & 1], me = (int)(t - g->tanks), j, mates = 0;
        for (j = 0; j < me; j++) mates += (g->tanks[j].team & 1) == (t->team & 1) && j != wl_own;
        escort = wl_own >= 0 && wl_own != me && (mates & 1) && g->tanks[wl_own].active && !g->tanks[wl_own].respawn ? &g->tanks[wl_own] : NULL;
        if (escort) {                                    /* (an escort: whoever's nearest our warlord, if they're close) */
            const Tank *near_wl = NULL;
            fx nd = 0;
            for (j = 0; j < g->num_tanks; j++) {
                const Tank *o = &g->tanks[j];
                fx d;
                if (!o->active || o->respawn || (o->team & 1) == (t->team & 1)) continue;
                d = gi_dist(o->x, o->y, escort->x, escort->y);
                if (!near_wl || d < nd) { near_wl = o; nd = d; }
            }
            foe = near_wl && nd < FX(14 * TILE_SIZE) ? near_wl : NULL;
            foe_d = foe ? gi_dist(foe->x, foe->y, t->x, t->y) : 0;
        } else if (ej >= 0 && ej < g->num_tanks && g->tanks[ej].active && !g->tanks[ej].respawn) {
            foe = &g->tanks[ej];
            foe_d = gi_dist(foe->x, foe->y, t->x, t->y);
        }
    }
    if (sk->weakest && !escort && !(g->extras & EXTRA_WARLORD)) {   /* hard: the weakest it can see, to finish it off */
        const Tank *weak = NULL;
        int wa = 0;
        for (i = 0; i < g->num_tanks; i++) {
            const Tank *o = &g->tanks[i];
            int pct;
            if (!o->active || o->respawn || o == t || o->guard) continue;
            if (o->team == t->team && !(g->extras & EXTRA_ANNIHILATION)) continue;
            if (gi_dist(o->x, o->y, t->x, t->y) >= FX(sk->detect) || !gi_clear_shot(g, t->x, t->y, o->x, o->y, 0)) continue;
            pct = o->armor * 100 / (game_armor_max(g, i) ? game_armor_max(g, i) : 1);
            if (!weak || pct < wa) { weak = o; wa = pct; }
        }
        if (weak) { foe = weak; foe_d = gi_dist(weak->x, weak->y, t->x, t->y); }
    }
    if (foe && foe_d < FX(sk->detect) && gi_clear_shot(g, t->x, t->y, foe->x, foe->y, 0))
        seen = 1;
    a->seen = seen ? (uint16_t)(a->seen + 1) : 0;

    /* the extras: a pickup that would help, nearer than home; the hill, if it's ours to hold */
    {
        int need_armor = t->armor * 100 < ts->armor * 60, need_fuel = t->fuel * 100u < fuel_cap * 40u, need_mines = t->mines == 0;
        int hx = home->class_tx[t->cls < 3 ? t->cls : 1], hy = home->class_ty[t->cls < 3 ? t->cls : 1];
        int best = -1, k, best_d = 0;
        a_pick = -1;
        if ((g->extras & EXTRA_PICKUPS) && (need_armor || need_fuel || need_mines) && !t->has_flag) {
            for (k = 0; k < MAX_PICKUPS; k++) {
                const Pickup *pk = &g->pickups[k];
                int d;
                if (!pk->active) continue;
                if (!((pk->kind == PICKUP_REPAIR && need_armor) || (pk->kind == PICKUP_FUEL && need_fuel) ||
                      (pk->kind == PICKUP_MINES && need_mines))) continue;
                d = (pk->tx - tx) * (pk->tx - tx) + (pk->ty - ty) * (pk->ty - ty);
                if (best < 0 || d < best_d) { best = k; best_d = d; }
            }
            if (best >= 0 && best_d < (hx - tx) * (hx - tx) + (hy - ty) * (hy - ty) && best_d < 45 * 45) a_pick = best;
        }
        a_hill = 0;
        if ((g->extras & EXTRA_HILL) && g->hill_holder != (t->team & 1) && !t->has_flag) {
            int mates = 0, me_k = 0, j;
            for (j = 0; j < g->num_tanks; j++)
                if (g->tanks[j].ai && (g->tanks[j].team & 1) == (t->team & 1)) { if (&g->tanks[j] == t) me_k = mates; mates++; }
            if (mates > 1) a_hill = (me_k & 1) == 0;       /* every other one of us holds the hill */
            else a_hill = g->hill[1 - (t->team & 1)] > g->hill[t->team & 1] || g->hill_holder == 1 - (t->team & 1) ||
                          (g->tick / (60 * TICK_RATE)) % 2 == 0;   /* alone: a minute the hill, a minute the flag */
        }
    }
    if (g->extras & EXTRA_CONQUEST) {                    /* conquest: the nearest point that isn't ours */
        int k, best_d = 0, me = t->team & 1, mine_k = 0, mates = 0, j;
        for (j = 0; j < g->num_tanks; j++)
            if (g->tanks[j].ai && (g->tanks[j].team & 1) == me) { if (&g->tanks[j] == t) mine_k = mates; mates++; }
        {   /* a point of ours with an enemy on it: the nearest of us goes to defend it */
            int k2, best_def = -1, best_dd = 0;
            for (k2 = 0; k2 < g->num_cp; k2++) {
                fx px2 = FX(g->cp_tx[k2] * TILE_SIZE + TILE_SIZE / 2), py2 = FX(g->cp_ty[k2] * TILE_SIZE + TILE_SIZE / 2);
                int threat = 0, nearest = 1, dd;
                if (g->cp_owner[k2] != me) continue;
                for (j = 0; j < g->num_tanks && !threat; j++) {
                    const Tank *o = &g->tanks[j];
                    threat = o->active && !o->respawn && (o->team & 1) != me && gi_dist(o->x, o->y, px2, py2) < FX((CP_RADIUS + 3) * TILE_SIZE);
                }
                if (!threat) continue;
                dd = (int)(gi_dist(t->x, t->y, px2, py2) >> 16);
                for (j = 0; j < g->num_tanks && nearest; j++) {   /* (am I the nearest of us?) */
                    const Tank *o = &g->tanks[j];
                    if (o == t || !o->ai || !o->active || o->respawn || (o->team & 1) != me) continue;
                    if ((int)(gi_dist(o->x, o->y, px2, py2) >> 16) < dd) nearest = 0;
                }
                if (nearest && (best_def < 0 || dd < best_dd)) { best_def = k2; best_dd = dd; }
            }
            if (best_def >= 0) a_cp = best_def;
        }
        if (a_cp < 0) {
            int second = -1, second_d = 0;
            for (k = 0; k < g->num_cp; k++) {
                int d = (g->cp_tx[k] - tx) * (g->cp_tx[k] - tx) + (g->cp_ty[k] - ty) * (g->cp_ty[k] - ty);
                int ours = g->cp_owner[k] == me && (me ? g->cp_take[k] <= -CP_TAKE / 2 : g->cp_take[k] >= CP_TAKE / 2);
                if (ours) continue;                      /* (held, and not being taken from us) */
                if (a_cp < 0 || d < best_d) { second = a_cp; second_d = best_d; a_cp = k; best_d = d; }
                else if (second < 0 || d < second_d) { second = k; second_d = d; }
            }
            if (second >= 0 && mates >= 3 && (mine_k & 1)) a_cp = second;   /* (three or more of us: spread out, every other one) */
        }
        if (a_cp < 0 && g->num_cp && (mine_k & 1))      /* all ours: every other one of us guards one */
            a_cp = (int)((tx + ty + mine_k) % g->num_cp);
    }
    int help_flag = 0, a_tag = -1;
    {   /* our flag at home, an enemy near it: the two of us nearest it come (a call for help) */
        int j2, foe_near = 0, closer = 0;
        fx hx = own->home_x, hy = own->home_y;
        if (own->state == FLAG_GROUND && own->x == own->home_x && own->y == own->home_y && !(g->extras & (EXTRA_CONQUEST | EXTRA_ANNIHILATION | EXTRA_TDM | EXTRA_WARLORD))) {
            for (j2 = 0; j2 < g->num_tanks && !foe_near; j2++) {
                const Tank *o = &g->tanks[j2];
                foe_near = o->active && !o->respawn && (o->team & 1) != (t->team & 1) && gi_dist(o->x, o->y, hx, hy) < FX(14 * TILE_SIZE);
            }
            if (foe_near) {
                fx mine = gi_dist(t->x, t->y, hx, hy);
                for (j2 = 0; j2 < g->num_tanks; j2++) {
                    const Tank *o = &g->tanks[j2];
                    if (o != t && o->ai && o->active && !o->respawn && (o->team & 1) == (t->team & 1) && gi_dist(o->x, o->y, hx, hy) < mine) closer++;
                }
                help_flag = closer < 2;
            }
        }
    }
    if (g->extras & EXTRA_HARDPOINT) a_cp = g->num_cp ? g->hp_active : -1;   /* hardpoint: the point that counts */
    {   /* kill confirmed: a tag within 20 tiles, either side's (confirm ours, deny theirs) */
        int k, bd = 20 * 20;
        a_tag = -1;
        if (g->extras & EXTRA_KILLCONF)
            for (k = 0; k < MAX_TAGS; k++) {
                int dx, dy;
                if (!g->tags[k].active) continue;
                dx = tile_of(g->tags[k].x) - tile_of(t->x); dy = tile_of(g->tags[k].y) - tile_of(t->y);
                if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; a_tag = k; }
            }
    }
    /* what do we want? */
    if (t->has_flag) {
        mode = AIM_HOME;
    } else if (!g->map.arena && t->armor * 100 < ts->armor * (siege ? 10 : 20)) {
        mode = AIM_REPAIR;                               /* badly hurt: back to mend, whatever else (not with their flag) */
    } else if (help_flag && foe) {
        mode = AIM_DEFEND;                               /* our flag's in danger: to it */
    } else if (own->state == FLAG_CARRIED && own->carrier < g->num_tanks) {
        mode = AIM_CHASE;
    } else if (a_pick >= 0) {
        mode = AIM_PICKUP;                               /* (nearer than home: the pickup) */
    } else if (a_cp >= 0 && !(t->armor * 100 < ts->armor * 35)) {
        mode = AIM_POINT;                                /* conquest: a point to take (or to guard) */
    } else if (a_tag >= 0) {
        mode = AIM_TAG;                                  /* kill confirmed: the tag */
    } else if (g->drop.active && t->armor * 100 < game_armor_max(g, (int)(t - g->tanks)) * 70) {
        mode = AIM_DROP;                                 /* hurt, and a supply drop's down: for it */
    } else if (escort && !foe) {
        mode = AIM_ESCORT;                               /* warlord: nobody near ours, so by its side */
    } else if ((g->extras & (EXTRA_ANNIHILATION | EXTRA_TDM | EXTRA_WARLORD | EXTRA_KILLCONF)) && foe && !(t->armor * 100 < ts->armor * 35 || t->fuel * 100u < fuel_cap * 25u)) {
        mode = AIM_HUNT;                                 /* annihilation: the nearest of them */
    } else if (!g->map.arena &&                          /* (an arena: no garage; the pickups are the mending) */
               ((a->mode == AIM_REPAIR && (t->armor < ts->armor || t->fuel < fuel_cap * 19 / 20)) ||
                t->armor * 100 < ts->armor * (siege ? 15 : 35) || t->fuel * 100u < fuel_cap * 25u)) {
        mode = AIM_REPAIR;
    } else if (own->state == FLAG_GROUND && (own->x != own->home_x || own->y != own->home_y)) {
        mode = AIM_RECOVER;
    } else if (theirs->state == FLAG_GROUND) {
        mode = AIM_FETCH;
    } else if (!siege && foe && near_camp(home, foe, sk->guard)) {
        mode = AIM_DEFEND;                               /* you're too close to our camp */
    } else if (a_hill) {
        mode = AIM_HILL;                                 /* the hill isn't ours: take it, and stay */
    } else if (raider || g->tick >= sk->raid_at) {
        mode = AIM_RAID;
    } else {
        mode = AIM_PATROL;                               /* early on: guard our side of the map */
    }
    if (mode == AIM_PATROL && (a->pat_t == 0 || (abs16(tx - a->pat_tx) < 3 && abs16(ty - a->pat_ty) < 3))) {
        pick_patrol(g, a, t->team);
    }
    if (a->pat_t) a->pat_t--;
    switch (mode) {
    case AIM_HOME:    gx = home->spawn_tx; gy = home->spawn_ty; break;
    case AIM_REPAIR: {
        int garage = siege && t->garage < home->num_garages ? t->garage : t->cls < 3 ? t->cls : 1;
        gx = home->class_tx[garage]; gy = home->class_ty[garage]; break;
    }
    case AIM_CHASE:   gx = tile_of(g->tanks[own->carrier].x); gy = tile_of(g->tanks[own->carrier].y); break;
    case AIM_RECOVER: gx = tile_of(own->x); gy = tile_of(own->y); break;
    case AIM_DEFEND:  gx = tile_of(foe->x); gy = tile_of(foe->y); break;
    case AIM_PATROL:  gx = a->pat_tx; gy = a->pat_ty; break;
    case AIM_HILL:    gx = g->hill_tx; gy = g->hill_ty; break;
    case AIM_POINT:   gx = g->cp_tx[a_cp]; gy = g->cp_ty[a_cp]; break;
    case AIM_HUNT:    gx = tile_of(foe->x); gy = tile_of(foe->y); break;
    case AIM_ESCORT:  gx = tile_of(escort->x); gy = tile_of(escort->y); break;
    case AIM_DROP:    gx = g->drop.tx; gy = g->drop.ty; break;
    case AIM_TAG:     gx = tile_of(g->tags[a_tag].x); gy = tile_of(g->tags[a_tag].y); break;
    case AIM_PICKUP:  gx = g->pickups[a_pick].tx; gy = g->pickups[a_pick].ty; break;
    default:          gx = tile_of(theirs->x); gy = tile_of(theirs->y); break;   /* the bunker, or the flag */
    }
    if (game_order_active(g, t->team & 1) && mode != AIM_HOME && mode != AIM_REPAIR) {
        const TeamOrder *o = &g->team_order[t->team & 1];
        mode = AIM_POINT; /* explicit waypoint, while still fighting and repairing */
        gx = o->tx; gy = o->ty;
        if (o->kind == ORDER_ESCORT) {
            gx = tile_of(g->tanks[o->issuer].x); gy = tile_of(g->tanks[o->issuer].y);
        }
        if (gx < 1) gx = 1;
        if (gy < 1) gy = 1;
        if (gx >= g->map.w - 1) gx = g->map.w - 2;
        if (gy >= g->map.h - 1) gy = g->map.h - 2;
        if (!seen && abs16(gx - tx) < 3 && abs16(gy - ty) < 3) return 0;
    }
    if (mode != a->mode || a->repath == 0 ||
        (gx - a->goal_tx) * (gx - a->goal_tx) + (gy - a->goal_ty) * (gy - a->goal_ty) > 4) {
        if (build_route(g, a, t->team, gx, gy)) {
            if (local_route_budget) { gx = local_route_goal % MAP_W; gy = local_route_goal / MAP_W; }
            a->mode = (uint8_t)mode;
            a->goal_tx = (int16_t)gx; a->goal_ty = (int16_t)gy;
            a->repath = REPATH + (uint16_t)gi_range(g, 0, 10);
        }
    } else {
        a->repath--;
    }

    /* in the garage being patched up: just wait */
    if (mode == AIM_REPAIR && g->map.sid[ty][tx] &&
        g->map.st[g->map.sid[ty][tx] - 1].type == ST_GARAGE && g->map.st[g->map.sid[ty][tx] - 1].team == t->team)
        return 0;

    /* stuck against something: back off a bit, turning */
    if ((g->tick & 31) == 0) {                           /* getting anywhere? (closer on the route, or moving) */
        uint16_t here = a->dist[ty * MAP_W + tx];
        int moved = gi_dist(a->last_x, a->last_y, t->x, t->y) >= FX(6);
        if (!seen && ((here != NOWAY && here >= a->last_d) || !moved)) a->stuck++;
        else a->stuck = 0;
        a->last_x = t->x; a->last_y = t->y; a->last_d = here;
        if (a->stuck >= 3) { a->back = (uint8_t)gi_range(g, 15, 28); a->stuck = 0; a->dodge ^= 1; }
    }
    if (a->back) {
        a->back--;
        return (InputBits)(IN_DOWN | (a->dodge ? IN_LEFT : IN_RIGHT));
    }

    if (siege && !seen && (mode == AIM_RAID || mode == AIM_FETCH)) {
        angle16 aim;
        if (raid_target(g, t, theirs->x, theirs->y, &aim)) {
            int16_t diff = (int16_t)(aim - t->angle);
            a->stuck = 0;
            in = steer(g, t, aim, 0);
            if (diff > -0x0400 && diff < 0x0400) in |= IN_FIRE;
            return in;
        }
    }

    if (seen && mode != AIM_HOME && mode != AIM_REPAIR &&
        (!siege || (mode != AIM_RAID && mode != AIM_FETCH) || foe_d < FX(95))) {
        /* a fight: face them (leading the shot on hard), keep a sensible distance */
        int lead = t->ai >= AI_HARD ? fx_to_int(foe_d) / 4 : 0;   /* ticks the shell will fly */
        fx ax = foe->x + fx_mul(foe->speed, fx_sin(foe->angle)) * lead;
        fx ay = foe->y - fx_mul(foe->speed, fx_cos(foe->angle)) * lead;
        angle16 want = fx_atan2(ax - t->x, ay - t->y);
        int16_t diff = (int16_t)(want - t->angle), tol = sk->tol;
        int go = foe_d > FX(95) ? 1 : foe_d < FX(45) ? -1 : 0;
        if (siege && (mode == AIM_RAID || mode == AIM_FETCH) && foe_d >= FX(45)) go = 1;
        if (t->ai >= AI_HARD && foe_d < FX(140)) {        /* don't sit still in their sights */
            int16_t looking = (int16_t)(fx_atan2(t->x - foe->x, t->y - foe->y) - foe->angle);
            if (looking > -0x0C00 && looking < 0x0C00 && ((g->tick / 40) & 1)) go = 1;
        }
        in = steer(g, t, want, go);
        if (diff > -tol && diff < tol && a->seen >= (uint16_t)sk->react && gi_range(g, 0, 99) < sk->chance)
            in |= IN_FIRE;                               /* (only once it's had you in sight a moment: its level) */
        return in;
    }

    /* follow the route downhill, aiming two tiles ahead */
    {
        int n1 = downhill(a, tx, ty), n2 = n1 >= 0 ? downhill(a, n1 % MAP_W, n1 / MAP_W) : -1;
        int aim = n1;
        angle16 want;
        if (n2 >= 0 && body_clear(g, t, FX((n2 % MAP_W) * TILE_SIZE + TILE_SIZE / 2),
                                  FX((n2 / MAP_W) * TILE_SIZE + TILE_SIZE / 2), siege ? t->team : -1))
            aim = n2;                                     /* look two tiles ahead when the way is open */
        if (aim >= 0) {
            fx wx = FX((aim % MAP_W) * TILE_SIZE + TILE_SIZE / 2), wy = FX((aim / MAP_W) * TILE_SIZE + TILE_SIZE / 2);
            /* the next tile is a wall or building: face it and shoot through */
            if (n1 >= 0 && (tile_info[g->map.type[n1 / MAP_W][n1 % MAP_W]].flags & TF_SOLID))
                wx = FX((n1 % MAP_W) * TILE_SIZE + TILE_SIZE / 2), wy = FX((n1 / MAP_W) * TILE_SIZE + TILE_SIZE / 2);
            want = fx_atan2(wx - t->x, wy - t->y);
        } else {
            want = fx_atan2(FX(gx * TILE_SIZE + 8) - t->x, FX(gy * TILE_SIZE + 8) - t->y);   /* no route: go direct */
        }
        in = steer(g, t, want, 1);
    }
    if (target_ahead(g, t, mode == AIM_HOME ? 60 : 130))   /* shoot what's in the way */
        in |= IN_FIRE;
    if (siege && seen && (mode == AIM_RAID || mode == AIM_FETCH) && a->seen >= (uint16_t)sk->react) {
        int16_t diff = (int16_t)(fx_atan2(foe->x - t->x, foe->y - t->y) - t->angle);
        if (diff > -0x0400 && diff < 0x0400) in |= IN_FIRE;
    }
    if (foe && t->ai >= AI_HARD && mode == AIM_HOME && !a->mine_cd && foe_d < FX(90)) {
        int16_t behind = (int16_t)(fx_atan2(foe->x - t->x, foe->y - t->y) - t->angle);
        if (behind > 0x6000 || behind < -0x6000) { in |= IN_MINE; a->mine_cd = 120; }   /* a gift for the chaser */
    }
    return in;
}

void ai_local_reset(void) { local_route_owner = NULL; local_route_budget = 0; }

InputBits ai_think_local(Game *g, int idx, unsigned budget)
{
    InputBits input;
    /* This interface is valid only when all bot inputs are recorded explicitly. */
    if (!g->bot_commands || idx < 0 || idx >= g->num_tanks) return 0;
    if ((g->tanks[idx].respawn || g->tanks[idx].out) && local_route_owner == &g->ai[idx]) local_route_owner = NULL;
    local_route_budget = budget ? budget : 512;
    input = ai_think(g, idx);
    local_route_budget = 0;
    return input;
}
