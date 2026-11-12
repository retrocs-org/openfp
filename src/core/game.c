/* SPDX-License-Identifier: MIT */
#include <string.h>
#include "core/game.h"
#include "core/match.h"


const TankStats tank_stats[TANK_CLASS_COUNT] = {
    [TANK_LIGHT] = {
        "JACKAL", "FAST, THIN ARMOUR",
        FX_FRAC(21, 10), FX(1), FX_FRAC(12, 100), FX_FRAC(8, 100), 0x0440, 12,
        30, 200, 10, 10, 5 },
    [TANK_MEDIUM] = {
        "BISON", "THE ALL-ROUNDER",
        FX_FRAC(17, 10), FX_FRAC(8, 10), FX_FRAC(8, 100), FX_FRAC(6, 100), 0x0333, 15,
        50, 300, 15, 15, 5 },
    [TANK_HEAVY] = {
        "MAMMOTH", "SLOW BUT TOUGH",
        FX_FRAC(13, 10), FX_FRAC(6, 10), FX_FRAC(5, 100), FX_FRAC(5, 100), 0x0240, 19,
        80, 400, 20, 20, 5 },
};

/* Enhanced-world vehicles. Classic always uses the original table above. */
static const TankStats volcano_tanks[TANK_CLASS_COUNT] = {
    { "SALAMANDER", "QUICK ASSAULT VEHICLE", FX_FRAC(20,10), FX(1), FX_FRAC(12,100), FX_FRAC(8,100), 0x0440, 11, 36, 210, 12, 10, 5 },
    { "BASILISK", "ARMOURED LAVA RAIDER", FX_FRAC(16,10), FX_FRAC(8,10), FX_FRAC(8,100), FX_FRAC(6,100), 0x0333, 16, 60, 310, 18, 15, 5 },
    { "BEHEMOTH", "SIEGE ARMOUR", FX_FRAC(12,10), FX_FRAC(6,10), FX_FRAC(5,100), FX_FRAC(5,100), 0x0240, 21, 96, 420, 24, 20, 5 }
};
static const TankStats moon_tanks[TANK_CLASS_COUNT] = {
    { "LYNX", "AGILE LUNAR SCOUT", FX_FRAC(23,10), FX_FRAC(11,10), FX_FRAC(10,100), FX_FRAC(5,100), 0x0480, 13, 26, 250, 8, 10, 5 },
    { "ORION", "LONG-RANGE ROVER", FX_FRAC(18,10), FX_FRAC(9,10), FX_FRAC(7,100), FX_FRAC(4,100), 0x0360, 16, 45, 375, 12, 15, 5 },
    { "TITAN", "HEAVY MOON CRAWLER", FX_FRAC(14,10), FX_FRAC(7,10), FX_FRAC(5,100), FX_FRAC(3,100), 0x0260, 20, 72, 500, 16, 20, 5 }
};

const TankStats *world_tank_stats(int biome, TankClass cls)
{
    if ((unsigned)cls >= TANK_CLASS_COUNT) cls = TANK_MEDIUM;
    return biome == BIOME_VOLCANIC ? &volcano_tanks[cls] :
           biome == BIOME_MOON ? &moon_tanks[cls] : &tank_stats[cls];
}

const PropInfo prop_info[PROP_KINDS] = {
    /*                 name         half_w   half_l   hp pts blast crew */
    [PROP_TRUCK]     = { "truck",     FX(5),   FX(11),  3, 10,  90, 2 },
    [PROP_TANKER]    = { "tanker",    FX(5),   FX(12),  2, 15, 255, 1 },
    [PROP_JEEP]      = { "jeep",      FX(4),   FX(6),   1,  5,  60, 1 },
    [PROP_APC]       = { "apc",       FX(6),   FX(10),  5, 20, 120, 3 },
    [PROP_HALFTRACK] = { "halftrack", FX(5),   FX(10),  4, 15, 100, 2 },
    [PROP_GUN]       = { "gun",       FX(5),   FX(7),   2, 10,  70, 1 },
};

static const uint8_t destroy_score[TILE_COUNT] = {
    [TILE_TREE] = 5, [TILE_WALL] = 10, [TILE_BUILDING] = 50,
};

#define SHOT_SPEED   FX(4)
#define SHOT_TTL     45            /* ticks, ~180 px range */
#define NOSE_DIST    FX(11)
#define TANK_SHOT_DAMAGE 10
#define HIT_RADIUS   FX(7)

/* gun turrets: how far they see, how fast they turn and shoot, how well
 * they aim (spread) and how many shots per burst */
typedef struct {
    fx       range;
    uint8_t  reload;
    fx       speed;
    uint8_t  damage;
    angle16  turn;
    uint8_t  kind, ttl;
    fx       muzzle;               /* barrel length from the turret centre */
    angle16  spread;               /* +- random aim error */
    uint8_t  burst, burst_gap;     /* shots per burst, ticks between them */
} TurretInfo;

static const TurretInfo turret_info[ST_TYPE_COUNT] = {
    /*               range   reload speed        dmg turn    kind         ttl muzzle  spread  burst gap */
    [ST_T_MG]     = { FX(140),  9, FX(5),           1, 0x0500, SHOT_BULLET, 30, FX(7),  0x0380, 1, 0 },
    [ST_T_TWIN]   = { FX(160), 55, FX_FRAC(55, 10), 2, 0x0400, SHOT_BULLET, 32, FX(8),  0x0200, 4, 5 },
    [ST_T_CANNON] = { FX(170), 50, FX(4),           4, 0x0280, SHOT_SHELL,  45, FX(9),  0x0120, 1, 0 },
    [ST_T_ROCKET] = { FX(220), 95, FX_FRAC(25, 10), 6, 0x0200, SHOT_ROCKET, 95, FX(10), 0x0300, 1, 0 },
    [ST_T_HEAVY]  = { FX(240), 75, FX(5),           8, 0x0180, SHOT_HEAVY,  50, FX(15), 0x00A0, 1, 0 },
    [ST_T_MORTAR] = { FX(260),120, FX(3),           7, 0x0200, SHOT_MORTAR,  0, FX(6),  0x0000, 1, 0 },
};

#define MORTAR_SPLASH FX(22)

static void tank_refill(const Game *g, Tank *t, int with_lives)
{
    const TankStats *st = world_tank_stats(g->vehicle_world, (TankClass)t->cls);
    t->drift_x = t->drift_y = 0;
    t->boost_ticks = t->rapid_ticks = 0;
    t->fuel  = (uint16_t)(st->fuel * TICK_RATE);
    t->armor = st->armor;
    t->mines = st->mines;
    t->pows  = 0;
    if (with_lives)
        t->lives = g->life_limit ? g->life_limit : st->lives;
}

static uint32_t surface_hash(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; return x ^ (x >> 16);
}

void game_prepare_surfaces(Game *g, uint32_t seed, int decorate)
{
    int x, y, i, n = 0, classic = !g->enhanced_rules;
    Map *m = &g->map;
    map_normalize_surfaces(m, (Biome)g->biome, classic);
    if (!decorate || classic) return;
    if (g->biome == BIOME_WINTER)
        for (y = 0; y < m->h; y++) for (x = 0; x < m->w; x++)
            if (m->type[y][x] == TILE_WATER && (surface_hash(seed ^ (uint32_t)(x / 10) * 193u ^ (uint32_t)(y / 10) * 977u) & 3u))
                map_set(m, x, y, TILE_ICE);
    if (g->biome == BIOME_MOON) return;
    for (i = 0; i < 1000 && n < m->w / 24; i++) {
        uint32_t h = surface_hash(seed + (uint32_t)i * 193u);
        int k, near = 0, dx, dy;
        x = 6 + (int)(h % (uint32_t)(m->w - 12)); y = 6 + (int)((h >> 16) % (uint32_t)(m->h - 12));
        if (m->type[y][x] != TILE_ROAD || m->sid[y][x]) continue;
        for (k = 0; k < 2; k++) if (x >= g->bases[k].x0 - 6 && x <= g->bases[k].x1 + 6 &&
            y >= g->bases[k].y0 - 6 && y <= g->bases[k].y1 + 6) near = 1;
        if (near) continue;
        for (dy = -1; dy <= 1; dy++) for (dx = -1; dx <= 1; dx++)
            if (dx * dx + dy * dy < 3 && !m->sid[y + dy][x + dx] && m->type[y + dy][x + dx] == TILE_ROAD)
                map_set(m, x + dx, y + dy, g->biome == BIOME_WINTER ? TILE_ICE : TILE_OIL);
        n++;
    }
}

static uint32_t next_random(uint32_t *rng)
{
    uint32_t x = *rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *rng = x;
}

static uint32_t grand(Game *g) { return next_random(&g->rng); }

static int random_range(uint32_t *rng, int lo, int hi)
{   /* An empty range still consumes one draw. */
    uint32_t r = next_random(rng);
    return hi < lo ? lo : lo + (int)(r % (uint32_t)(hi - lo + 1));
}

static int grange(Game *g, int lo, int hi) { return random_range(&g->rng, lo, hi); }

static fx dist_fx(fx ax, fx ay, fx bx, fx by)      /* cheap octagonal distance */
{
    fx dx = fx_abs(ax - bx), dy = fx_abs(ay - by);
    return dx > dy ? dx + dy / 2 : dy + dx / 2;
}

static void struct_centre(const Map *m, int idx, fx *x, fx *y)
{
    const Structure *st = &m->st[idx];
    *x = FX(st->x * TILE_SIZE + struct_info[st->type].w * TILE_SIZE / 2);
    *y = FX(st->y * TILE_SIZE + struct_info[st->type].h * TILE_SIZE / 2);
}

static int base_contains(const Base *b, int tx, int ty)
{
    return tx >= b->x0 && tx <= b->x1 && ty >= b->y0 && ty <= b->y1;
}

static int in_base(const Game *g, const Tank *t, int team)
{
    return base_contains(&g->bases[team & 1], fx_to_int(t->x) >> TILE_SHIFT, fx_to_int(t->y) >> TILE_SHIFT);
}

int game_gate_open(const Game *g, int x, int y)
{
    int team, owner = -1, i, nearby = 0;
    int tx = x / TILE_SIZE, ty = y / TILE_SIZE;
    for (i = 0; i < g->num_tanks; i++) {
        const Tank *t = &g->tanks[i];
        int dx, dy;
        if (!t->active || t->out || t->respawn) continue;
        dx = fx_to_int(t->x) - x;
        dy = fx_to_int(t->y) - y;
        if (dx * dx + dy * dy < (3 * TILE_SIZE) * (3 * TILE_SIZE))
            nearby |= 1 << (t->team & 1);
    }
    if (!nearby) return 0;
    if (tx >= 0 && ty >= 0 && tx < g->map.w && ty < g->map.h && g->map.sid[ty][tx]) {
        const Structure *st = &g->map.st[g->map.sid[ty][tx] - 1];
        if (struct_info[st->type].flags & SF_GATE)
            return !st->hp || (nearby & (1 << (st->team & 1)));
    }
    for (team = 0; team < 2; team++)
        if (!g->map.arena && base_contains(&g->bases[team], tx, ty)) {
            owner = team;
            break;
        }
    if (owner < 0) {
        /* The striped barriers in outpost wall gaps have no structure of their own. */
        fx nearest = FX(10 * TILE_SIZE);
        for (i = 0; i < g->map.num_st; i++) {
            const Structure *st = &g->map.st[i];
            fx sx, sy, distance;
            struct_centre(&g->map, i, &sx, &sy);
            distance = dist_fx(FX(x), FX(y), sx, sy);
            if (distance >= nearest) continue;
            nearest = distance;
            owner = st->team & 1;
        }
    }
    return owner >= 0 && (nearby & (1 << owner));
}

static int spawn_man(Game *g, ManKind kind, fx x, fx y, int home, int team)
{
    int i;
    for (i = 0; i < MAX_MEN; i++) {
        Man *m = &g->men[i];
        if (m->kind == MAN_NONE) {
            memset(m, 0, sizeof *m);
            m->kind = (uint8_t)kind;
            m->x = m->prev_x = m->tx = x;
            m->y = m->prev_y = m->ty = y;
            m->home = (uint8_t)(home < 255 ? home : 255);
            m->team = (uint8_t)(kind == MAN_POW ? TEAM_PLAYER : team);
            m->timer = (uint16_t)grange(g, 0, 60);
            m->cooldown = (uint8_t)grange(g, 20, 80);
            return i;
        }
    }
    return -1;
}

static void barracks_door(const Game *g, int idx, fx *x, fx *y)
{
    const Structure *st = &g->map.st[idx];
    *x = FX(st->x * TILE_SIZE + struct_info[st->type].w * TILE_SIZE / 2);
    *y = FX((st->y + struct_info[st->type].h) * TILE_SIZE + 6);
}

/* room for a vehicle here? open ground or road, clear of buildings and of
 * other vehicles */
static int room_for(const Game *g, PropKind kind, fx x, fx y, angle16 a)
{
    const PropInfo *pi = &prop_info[kind];
    fx s = fx_sin(a), c = fx_cos(a);
    int i, k;
    for (k = -2; k <= 2; k++) {                       /* sample along the body */
        fx along = pi->half_l * k / 2;
        for (i = -1; i <= 1; i++) {
            fx across = pi->half_w * i;
            fx px = x + fx_mul(along, s) + fx_mul(across, c), py = y - fx_mul(along, c) + fx_mul(across, s);
            int tx = fx_to_int(px) >> TILE_SHIFT, ty = fx_to_int(py) >> TILE_SHIFT;
            TileType t = map_get(&g->map, tx, ty);
            if ((t != TILE_SAND && t != TILE_ROAD && t != TILE_STUMP) || g->map.sid[ty][tx]) return 0;
        }
    }
    for (i = 0; i < g->num_props; i++)
        if (dist_fx(g->props[i].x, g->props[i].y, x, y) < pi->half_l + prop_info[g->props[i].kind].half_l + FX(3))
            return 0;
    return 1;
}

static void add_vehicle(Game *g, PropKind kind, fx x, fx y, angle16 a, int team)
{
    Prop *p;
    if (g->num_props >= MAX_PROPS || !room_for(g, kind, x, y, a)) return;
    p = &g->props[g->num_props++];
    memset(p, 0, sizeof *p);
    p->x = x; p->y = y; p->angle = a;
    p->kind = (uint8_t)kind;
    p->hp = prop_info[kind].hp;
    p->team = (uint8_t)team;
    p->alive = p->used = 1;
}

/* patrols: a few of the enemy's jeeps and trucks out on the roads drive them (3 on a large map) */
static int road_at(const Game *g, int tx, int ty)
{
    return tx > 0 && ty > 0 && tx < g->map.w - 1 && ty < g->map.h - 1 && g->map.type[ty][tx] == TILE_ROAD && !g->map.sid[ty][tx];
}
static int in_a_base(const Game *g, int tx, int ty)
{
    int k;
    for (k = 0; k < 2; k++)
        if (tx >= g->bases[k].x0 - 2 && tx <= g->bases[k].x1 + 2 && ty >= g->bases[k].y0 - 2 && ty <= g->bases[k].y1 + 8) return 1;
    return 0;
}
static void start_patrols(Game *g)
{
    int i, want = 2 + (g->map.w * g->map.h) / 16384, n = 0;
    for (i = 0; i < g->num_props && n < want; i++) {
        Prop *p = &g->props[i];
        int tx = fx_to_int(p->x) >> TILE_SHIFT, ty = fx_to_int(p->y) >> TILE_SHIFT;
        if (!p->alive || p->team != TEAM_ENEMY || (p->kind != PROP_JEEP && p->kind != PROP_TRUCK)) continue;
        if (!road_at(g, tx, ty) || in_a_base(g, tx, ty)) continue;
        p->moving = 1;
        p->to_tx = (int16_t)tx; p->to_ty = (int16_t)ty;
        p->x = FX(tx * TILE_SIZE + TILE_SIZE / 2); p->y = FX(ty * TILE_SIZE + TILE_SIZE / 2);
        n++;
    }
}

/* All vehicle collision and patrol reaches are below 32 pixels. Index
 * vehicle centers by 32-pixel columns, then run the existing exact tests. */
enum { PROP_COLUMN_SHIFT = FX_SHIFT + 5, PROP_COLUMNS = WORLD_W_PX / 32 };
typedef struct {
    uint8_t head[PROP_COLUMNS], next[MAX_PROPS];  /* one-based indices; zero ends a list */
} PropIndex;

static int prop_column(fx x)
{
    int column = x >> PROP_COLUMN_SHIFT;
    if (column < 0) return 0;
    if (column >= PROP_COLUMNS) return PROP_COLUMNS - 1;
    return column;
}

static void index_props(const Game *g, PropIndex *index)
{
    int i;
    memset(index->head, 0, sizeof index->head);
    for (i = 0; i < g->num_props; i++) {
        int column = prop_column(g->props[i].x);
        index->next[i] = index->head[column];
        index->head[column] = (uint8_t)(i + 1);
    }
}

static void move_prop_index(PropIndex *index, int i, int old_column, fx x)
{
    int column = prop_column(x);
    uint8_t *link;
    if (column == old_column) return;
    for (link = &index->head[old_column]; *link != i + 1; link = &index->next[*link - 1]) {}
    *link = index->next[i];
    index->next[i] = index->head[column];
    index->head[column] = (uint8_t)(i + 1);
}

static int patrol_blocked(const Game *g, const PropIndex *index, int i, int dir)
{
    const Prop *p = &g->props[i];
    fx half_l = prop_info[p->kind].half_l;
    int column, end = prop_column(p->x + FX(32));
    for (column = prop_column(p->x - FX(32)); column <= end; column++) {
        int id;
        for (id = index->head[column]; id; id = index->next[id - 1]) {
            const Prop *o = &g->props[id - 1];
            fx dx = o->x - p->x, dy = o->y - p->y;
            if (o == p || !o->used) continue;
            if ((dir == 0 ? -dy : dir == 1 ? dx : dir == 2 ? dy : -dx) <= 0) continue;
            if (dist_fx(o->x, o->y, p->x, p->y) < half_l + prop_info[o->kind].half_l + FX(4))
                return 1;
        }
    }
    return 0;
}

static void patrols_update(Game *g, PropIndex *index)
{
    static const int ddx[4] = { 0, 1, 0, -1 }, ddy[4] = { -1, 0, 1, 0 };   /* north, east, south, west */
    int i, k;
    for (i = 0; i < g->num_props; i++) {
        Prop *p = &g->props[i];
        fx cx, cy, speed = p->kind == PROP_JEEP ? FX_FRAC(9, 10) : FX_FRAC(6, 10), dx, dy;
        int dir = ((p->angle + 0x2000) >> 14) & 3, blocked = 0, old_column;
        if (!p->alive || !p->moving) continue;
        old_column = prop_column(p->x);
        cx = FX(p->to_tx * TILE_SIZE + TILE_SIZE / 2); cy = FX(p->to_ty * TILE_SIZE + TILE_SIZE / 2);
        dx = cx - p->x; dy = cy - p->y;
        if (fx_abs(dx) <= speed && fx_abs(dy) <= speed) {   /* there: the next tile of road (straight on if it can) */
            int tx = p->to_tx, ty = p->to_ty, opts[4], no = 0, back = (dir + 2) & 3, pick;
            p->x = cx; p->y = cy;
            move_prop_index(index, i, old_column, p->x);
            for (k = 0; k < 4; k++) if (k != back && road_at(g, tx + ddx[k], ty + ddy[k]) && !in_a_base(g, tx + ddx[k], ty + ddy[k])) opts[no++] = k;
            if (!no) pick = back;                        /* (a dead end: back) */
            else {
                int straight = -1;
                for (k = 0; k < no; k++) if (opts[k] == dir) straight = k;
                pick = straight >= 0 && grange(g, 0, 99) < 70 ? dir : opts[grange(g, 0, no - 1)];
            }
            if (!road_at(g, tx + ddx[pick], ty + ddy[pick])) continue;   /* (nowhere at all: it stays) */
            p->to_tx = (int16_t)(tx + ddx[pick]); p->to_ty = (int16_t)(ty + ddy[pick]);
            p->angle = (angle16)(pick * 0x4000);
            continue;
        }
        for (k = 0; k < g->num_tanks && !blocked; k++) {   /* a tank in the way, ahead: wait */
            const Tank *t = &g->tanks[k];
            fx ox = t->x - p->x, oy = t->y - p->y;
            if (!t->active || t->respawn) continue;
            if (dist_fx(t->x, t->y, p->x, p->y) < prop_info[p->kind].half_l + FX(14) && fx_mul(ox, FX(ddx[dir])) + fx_mul(oy, FX(ddy[dir])) > 0) blocked = 1;
        }
        if (!blocked) blocked = patrol_blocked(g, index, i, dir);
        if (blocked) {
            if (++p->wait > 2 * TICK_RATE) {             /* held up too long: turn back */
                p->wait = 0;
                p->to_tx = (int16_t)(fx_to_int(p->x) >> TILE_SHIFT); p->to_ty = (int16_t)(fx_to_int(p->y) >> TILE_SHIFT);
                p->angle = (angle16)(p->angle + 0x8000);
            }
            continue;
        }
        p->wait = 0;
        p->x += dx > speed ? speed : dx < -speed ? -speed : dx;
        p->y += dy > speed ? speed : dy < -speed ? -speed : dy;
        move_prop_index(index, i, old_column, p->x);
    }
}

static void place_vehicles(Game *g)
{
    int i, k, tries;
    const Base *pb = &g->bases[TEAM_PLAYER], *eb = &g->bases[TEAM_ENEMY];
    /* our motor pool: a row of trucks and jeeps in the east of the camp */
    for (k = 0; k < 4; k++)
        add_vehicle(g, k == 3 ? PROP_JEEP : k == 2 ? PROP_TANKER : PROP_TRUCK,
                    FX((pb->x0 + 27 + k * 1) * TILE_SIZE + 4 + k * 10), FX((pb->y0 + 16) * TILE_SIZE),
                    ANGLE_NORTH, TEAM_PLAYER);
    /* the enemy fortress: vehicles parked in the yard, off its roads so the
     * way out of the garage stays clear */
    for (tries = 0, k = 0; tries < 120 && k < 8; tries++) {
        static const uint8_t mix[] = { PROP_TRUCK, PROP_APC, PROP_TANKER, PROP_HALFTRACK, PROP_JEEP, PROP_GUN };
        int before = g->num_props, vx = grange(g, eb->x0 + 2, eb->x1 - 2), vy = grange(g, eb->y0 + 2, eb->y1 - 2), j, near_road = 0;
        for (j = -2; j <= 2 && !near_road; j++)
            for (i = -2; i <= 2 && !near_road; i++)
                near_road = map_get(&g->map, vx + i, vy + j) == TILE_ROAD;
        if (near_road) continue;
        {   /* (one draw at a time, last argument first: the order every build agrees on) */
            angle16 va = (angle16)(grange(g, 0, 7) * 0x2000);
            PropKind vk = (PropKind)mix[grange(g, 0, 5)];
            add_vehicle(g, vk, FX(vx * TILE_SIZE + 8), FX(vy * TILE_SIZE + 8), va, TEAM_ENEMY);
        }
        k += g->num_props > before;
    }
    /* convoys parked along the roads, and odd vehicles by enemy buildings */
    for (tries = 0, k = 0; tries < 400 && k < 12; tries++) {
        int tx = grange(g, 3, g->map.w - 4), ty = grange(g, 3, g->map.h - 4), n, horiz;
        fx x, y;
        if (map_get(&g->map, tx, ty) != TILE_ROAD) continue;
        if (tx >= pb->x0 - 6 && tx <= pb->x1 + 6 && ty >= pb->y0 - 6 && ty <= pb->y1 + 6) continue;
        if (tx >= eb->x0 - 4 && tx <= eb->x1 + 4 && ty >= eb->y0 - 4 && ty <= eb->y1 + 8) continue;  /* not in their roads */
        horiz = map_get(&g->map, tx + 1, ty) == TILE_ROAD && map_get(&g->map, tx - 1, ty) == TILE_ROAD;
        x = FX(tx * TILE_SIZE + 8); y = FX(ty * TILE_SIZE + 8);
        n = grange(g, 2, 4);
        for (i = 0; i < n; i++) {
            static const uint8_t convoy[] = { PROP_JEEP, PROP_TRUCK, PROP_TRUCK, PROP_TANKER, PROP_HALFTRACK, PROP_APC };
            add_vehicle(g, (PropKind)convoy[grange(g, 0, 5)], x, y, horiz ? ANGLE_EAST : ANGLE_SOUTH, TEAM_ENEMY);
            if (horiz) x -= FX(26); else y -= FX(26);
        }
        k++;
    }
    for (i = 0; i < g->map.num_st; i++) {
        const Structure *st = &g->map.st[i];
        if (st->team != TEAM_ENEMY || (st->type != ST_BARRACKS && st->type != ST_WAREHOUSE && st->type != ST_DEPOT))
            continue;
        if (grange(g, 0, 2) == 0) continue;
        add_vehicle(g, st->type == ST_DEPOT ? PROP_TANKER : grange(g, 0, 1) ? PROP_TRUCK : PROP_JEEP,
                    FX((st->x + struct_info[st->type].w) * TILE_SIZE + 10), FX(st->y * TILE_SIZE + 16),
                    ANGLE_NORTH, TEAM_ENEMY);
    }
}

static void game_populate(Game *g);

void game_init(Game *g, uint32_t seed, Biome biome)
{
    memset(g, 0, sizeof *g);
    g->biome = (uint8_t)biome;
    g->rng = (seed * 2654435761u) | 1u;
    g->winner = -1;
    worldgen_generate(&g->map, seed, biome, g->bases);
    game_populate(g);
}

int game_init_map(Game *g, uint32_t seed, const char *map_text)
{
    Biome b = BIOME_CLASSIC;
    memset(g, 0, sizeof *g);
    g->rng = (seed * 2654435761u) | 1u;
    g->winner = -1;
    if (mapfile_load(&g->map, g->bases, &b, NULL, 0, map_text, seed) != 0) return -1;
    g->biome = (uint8_t)b;
    game_populate(g);
    return 0;
}

/* flags, minefields, parked vehicles and soldiers on a fresh map */
static void game_populate(Game *g)
{
    int i, t;
    g->team_leader[TEAM_PLAYER] = g->team_leader[TEAM_ENEMY] = -1;
    g->spawn_tx = g->bases[TEAM_PLAYER].spawn_tx;
    g->spawn_ty = g->bases[TEAM_PLAYER].spawn_ty;
    for (t = 0; t < 2; t++) {                       /* each flag sits in its bunker */
        Flag *f = &g->flags[t];
        const Base *b = &g->bases[t];
        if (b->flag_house >= 0) {
            struct_centre(&g->map, b->flag_house, &f->x, &f->y);
            f->state = FLAG_HIDDEN;
        } else if (g->map.custom_bases) {
            f->x = FX(b->spawn_tx * TILE_SIZE + TILE_SIZE / 2);
            f->y = FX((b->spawn_ty + 2) * TILE_SIZE + TILE_SIZE / 2);
            f->state = FLAG_GROUND;
        } else {
            f->x = FX((b->x0 + b->x1) / 2 * TILE_SIZE);
            f->y = FX((b->y0 + 3) * TILE_SIZE);
            f->state = FLAG_GROUND;
        }
        f->home_x = f->x;
        f->home_y = f->y;
    }
    {   /* enemy minefields out on the battlefield, as in the original; never near our base */
        const Base *pb = &g->bases[TEAM_PLAYER];
        int pcx = (pb->x0 + pb->x1) / 2, pcy = (pb->y0 + pb->y1) / 2, placed = 0, tries;
        for (tries = 0; tries < 200 && placed < MAX_MINEFIELD_MINES; tries++) {
            int cx = grange(g, 4, g->map.w - 5), cy = grange(g, 4, g->map.h - 5), k, n = grange(g, 4, 6);
            if ((cx - pcx) * (cx - pcx) + (cy - pcy) * (cy - pcy) < 34 * 34) continue;
            for (k = 0; k < n && placed < MAX_MINEFIELD_MINES; k++) {
                int tx = cx + grange(g, -3, 3), ty = cy + grange(g, -3, 3);
                TileType tt = map_get(&g->map, tx, ty);
                Mine *mn;
                if ((tt != TILE_SAND && tt != TILE_ROAD) || g->map.sid[ty][tx]) continue;
                mn = &g->mines[placed++];
                mn->x = FX(tx * TILE_SIZE + grange(g, 4, 12));
                mn->y = FX(ty * TILE_SIZE + grange(g, 4, 12));
                mn->team = TEAM_ENEMY;
                mn->arm = 0;
                mn->active = 1;
            }
        }
    }
    if (g->map.quiet) return;
    place_vehicles(g);
    start_patrols(g);                               /* (some of them drive the roads) */
    for (i = 0; i < g->map.num_st; i++)             /* a few soldiers per barracks, both sides */
        if (g->map.st[i].type == ST_BARRACKS && g->map.st[i].hp) {
            fx x, y;
            int k, n = grange(g, 1, 3);
            barracks_door(g, i, &x, &y);
            for (k = 0; k < n; k++)
                {
                    int oy = grange(g, 0, 6), ox = grange(g, -10, 10);
                    spawn_man(g, MAN_SOLDIER, x + FX(ox), y + FX(oy), i, g->map.st[i].team);
                }
        }
}

int game_add_tank(Game *g, TankClass cls, int team, fx x, fx y)
{
    Tank *t;
    if (g->num_tanks >= MAX_TANKS)
        return -1;
    t = &g->tanks[g->num_tanks];
    memset(t, 0, sizeof *t);
    t->x = t->prev_x = x;
    t->y = t->prev_y = y;
    t->cls = (uint8_t)cls;
    t->team = (uint8_t)team;
    t->garage = 255;
    t->active = 1;
    tank_refill(g, t, 1);
    return g->num_tanks++;
}

/* each tank waits in its own garage, nose just out of the door */
static int open_ground(const Game *g, int tx, int ty, int r);

/* a side's half of an arena (team modes): the camp's top left, the fortress's bottom right */
static int own_half(const Game *g, int k, int team)
{
    return ((g->map.spawn_tx[k] + g->map.spawn_ty[k]) < g->map.w) == ((team & 1) == TEAM_PLAYER);
}

/* an arena: the spawn point furthest from every enemy (team modes: in its own half; the first time: one each) */
static int arena_spawn(const Game *g, const Tank *t)
{
    int k, j, best = -1, halves = !(g->extras & EXTRA_ANNIHILATION), in_half = 0;
    fx best_d = 0;
    int others = 0;
    if (halves) for (k = 0; k < g->map.num_spawns; k++) in_half += own_half(g, k, t->team);
    if (!in_half) halves = 0;                            /* (an arena that isn't split: all of it) */
    for (j = 0; j < g->num_tanks; j++) if (&g->tanks[j] != t && g->tanks[j].active && !g->tanks[j].respawn) others++;
    (void)others;
    if (g->tick == 0) {                                  /* the start: one each, round its half */
        int nth = (int)(t - g->tanks) / (halves ? 2 : 1), c = 0;
        for (k = 0; k < g->map.num_spawns; k++)
            if (!halves || own_half(g, k, t->team)) { if (c == nth % (halves ? in_half : g->map.num_spawns)) return k; c++; }
        return 0;
    }
    for (k = 0; k < g->map.num_spawns; k++) {
        fx sx = FX(g->map.spawn_tx[k] * TILE_SIZE + TILE_SIZE / 2), sy = FX(g->map.spawn_ty[k] * TILE_SIZE + TILE_SIZE / 2), near = 0;
        int first = 1;
        if (halves && !own_half(g, k, t->team)) continue;
        {   /* (a tank standing on it: not this one) */
            int busy = 0;
            for (j = 0; j < g->num_tanks; j++) {
                const Tank *o = &g->tanks[j];
                if (o != t && o->active && !o->respawn && dist_fx(o->x, o->y, sx, sy) < FX(24)) busy = 1;
            }
            if (busy) continue;
        }
        for (j = 0; j < g->num_tanks; j++) {
            const Tank *o = &g->tanks[j];
            fx d;
            if (o == t || !o->active || o->respawn) continue;
            if (halves && o->team == t->team) continue;   /* (team modes: the enemies only) */
            d = dist_fx(o->x, o->y, sx, sy);
            if (first || d < near) { near = d; first = 0; }
        }
        if (best < 0 || near > best_d) { best = k; best_d = near; }
    }
    return best < 0 ? 0 : best;
}

static void place_in_garage(Game *g, Tank *t)
{
    const Base *b = &g->bases[t->team & 1];
    if (g->map.arena && g->map.num_spawns) {             /* an arena: no garages, its spawn points */
        int k = arena_spawn(g, t);
        t->x = t->prev_x = FX(g->map.spawn_tx[k] * TILE_SIZE + TILE_SIZE / 2);
        t->y = t->prev_y = FX(g->map.spawn_ty[k] * TILE_SIZE + TILE_SIZE / 2);
        t->angle = t->prev_angle = (angle16)((k * 8192) & 0xFFFF);
        t->speed = 0;
        return;
    }
    int c = t->garage < b->num_garages ? t->garage : t->cls < 3 ? t->cls : 1;
    t->x = t->prev_x = FX(b->class_tx[c] * TILE_SIZE + TILE_SIZE / 2);
    t->y = t->prev_y = FX(b->class_ty[c] * TILE_SIZE + TILE_SIZE);
    t->angle = t->prev_angle = ANGLE_SOUTH;
    t->speed = 0;
}

int game_add_tank_at_base(Game *g, TankClass cls, int team)
{
    return game_add_tank_in_garage(g, cls, team, 255);
}

int game_add_tank_in_garage(Game *g, TankClass cls, int team, int garage)
{
    int i = game_add_tank(g, cls, team, 0, 0);
    if (i >= 0) {
        g->tanks[i].garage = (uint8_t)garage;
        place_in_garage(g, &g->tanks[i]);
    }
    return i;
}

void game_set_tank_class(Game *g, int idx, TankClass cls)
{
    if (idx >= 0 && idx < g->num_tanks && cls < TANK_CLASS_COUNT) {
        g->tanks[idx].cls = (uint8_t)cls;
        if (cls != TANK_HEAVY) g->tanks[idx].rocket_launcher = 0;
        tank_refill(g, &g->tanks[idx], 1);
    }
}

/* ---- effects ------------------------------------------------------------------- */

static Effect *spawn_effect_ex(Game *g, EffectKind kind, fx x, fx y, int delay, int size, angle16 dir)
{
    static const uint8_t life[EFF_KINDS] = { 8, 26, 10, 44, 5, 10, 6, 20 };
    int i;
    for (i = 0; i < MAX_EFFECTS; i++) {
        Effect *e = &g->effects[i];
        if (!e->active) {
            e->x = x; e->y = y;
            e->kind = (uint8_t)kind;
            e->life = e->ttl = life[kind];
            e->delay = (uint8_t)delay;
            e->size = (uint8_t)size;
            e->dir = dir;
            e->serial = ++g->effect_serial;
            e->sub = 0;
            e->active = 1;
            return e;
        }
    }
    return NULL;
}

static void spawn_effect(Game *g, EffectKind kind, fx x, fx y)
{
    spawn_effect_ex(g, kind, x, y, 0, 0, 0);
}

/* the whole footprint goes up, centre first, then a ripple outwards */
static void structure_explosion(Game *g, const Structure *st)
{
    const StructInfo *si = &struct_info[st->type];
    int cx = st->x * TILE_SIZE + si->w * TILE_SIZE / 2;
    int cy = st->y * TILE_SIZE + si->h * TILE_SIZE / 2;
    int x, y, size = si->w * si->h * 20;
    if (st->type == ST_DEPOT) size = 255;               /* fuel goes up big */
    spawn_effect_ex(g, EFF_BLAST, FX(cx), FX(cy), 0, size > 255 ? 255 : size, 0);
    for (y = 0; y < si->h; y++)
        for (x = 0; x < si->w; x++) {
            int px = (st->x + x) * TILE_SIZE + TILE_SIZE / 2, py = (st->y + y) * TILE_SIZE + TILE_SIZE / 2;
            int dist = (px > cx ? px - cx : cx - px) + (py > cy ? py - cy : cy - py);
            spawn_effect_ex(g, EFF_EXPLOSION, FX(px), FX(py), dist / 6 + ((x * 7 + y * 3) & 3), 80, 0);
        }
}

/* ---- shots ------------------------------------------------------------------------ */

static Shot *new_shot(Game *g)
{
    int i;
    for (i = 0; i < MAX_SHOTS; i++)
        if (!g->shots[i].active) {
            memset(&g->shots[i], 0, sizeof g->shots[i]);
            return &g->shots[i];
        }
    return NULL;
}

static void fire(Game *g, fx x, fx y, angle16 a, fx speed, int ttl, int kind, int damage,
                 int team, int owner, int src_sid, int target)
{
    Shot *sh = new_shot(g);
    if (!sh) return;
    sh->x = sh->prev_x = x;
    sh->y = sh->prev_y = y;
    sh->vx = fx_mul(speed, fx_sin(a));
    sh->vy = -fx_mul(speed, fx_cos(a));
    sh->ttl = (uint16_t)ttl;
    sh->kind = (uint8_t)kind;
    sh->damage = (uint8_t)damage;
    sh->team = (uint8_t)team;
    sh->owner = (uint8_t)owner;
    sh->src_sid = (uint16_t)src_sid;
    sh->target = (uint8_t)target;
    sh->life = (uint16_t)ttl;
    sh->active = 1;
    {
        Effect *e = spawn_effect_ex(g, EFF_MUZZLE, x, y, 0,
                                    kind == SHOT_HEAVY ? 200 : kind == SHOT_BULLET ? 40 : 110, a);
        if (e) e->sub = (uint8_t)kind;              /* lets the sound know what fired */
    }
}

/* warlord: is it a side's Warlord, and what armour can it have */
static int is_jug(const Game *g, const Tank *t)
{
    return (g->extras & EXTRA_WARLORD) && g->jug[t->team & 1] == (int8_t)(t - g->tanks);
}

static uint8_t armor_max(const Game *g, const Tank *t)
{
    int a = world_tank_stats(g->vehicle_world, (TankClass)t->cls)->armor;
    return (uint8_t)(is_jug(g, t) ? (a * 5 > 255 ? 255 : a * 5) : a);   /* (a boss: five times, up to 255) */
}

static int taken(const Game *g, const Tank *t, int dmg)   /* the damage it takes: a Warlord a third */
{
    return is_jug(g, t) ? (dmg / 3 > 1 ? dmg / 3 : 1) : dmg;
}

int game_armor_max(const Game *g, int tank) { return tank >= 0 && tank < g->num_tanks ? armor_max(g, &g->tanks[tank]) : 1; }

static void jug_boost(Game *g, Tank *t)              /* it becomes the Warlord: its armour, no mines */
{
    t->armor = armor_max(g, t);
    t->mines = 0;
}

static void tank_destroyed(Game *g, Tank *t)
{
    spawn_effect_ex(g, EFF_BLAST, t->x, t->y, 0, 150, t->angle);
    spawn_effect_ex(g, EFF_EXPLOSION, t->x + FX(5), t->y - FX(3), 4, 80, 0);
    spawn_effect_ex(g, EFF_EXPLOSION, t->x - FX(4), t->y + FX(4), 8, 80, 0);
    if (is_jug(g, t)) {                                  /* a Warlord down: the other side scores; the next of ours takes it on */
        int side = t->team & 1, idx = (int)(t - g->tanks), k;
        if (g->jug_score[1 - side] < 255) g->jug_score[1 - side]++;
        if (g->jug_score[1 - side] >= JUG_WIN && g->winner < 0) g->winner = (int8_t)(1 - side);
        for (k = 1; k <= g->num_tanks; k++) {
            int n = (idx + k) % g->num_tanks;
            if ((g->tanks[n].team & 1) == side && !g->tanks[n].out) {
                g->jug[side] = (int8_t)n;
                if (n != idx && g->tanks[n].active && !g->tanks[n].respawn) jug_boost(g, &g->tanks[n]);
                break;
            }
        }
    }
    t->armor = 0;
    t->speed = 0;
    t->lost++;
    t->pows_lost = (uint16_t)(t->pows_lost + t->pows);
    t->pows = 0;                                        /* nobody gets out of that */
    t->still = t->moving = 0;
    if (t->has_flag) {                                  /* the flag drops where we fell */
        Flag *f = &g->flags[1 - (t->team & 1)];
        f->state = FLAG_GROUND;
        f->x = t->x;
        f->y = t->y;
        t->has_flag = 0;
    }
    if ((g->extras & EXTRA_KILLCONF) && !t->out) {      /* kill confirmed: a tag where we fell */
        int k;
        for (k = 0; k < MAX_TAGS && g->tags[k].active; k++) {}
        if (k == MAX_TAGS) {                             /* (all lying: the oldest goes) */
            int old = 0;
            for (k = 1; k < MAX_TAGS; k++) if (g->tags[k].ttl < g->tags[old].ttl) old = k;
            k = old;
        }
        g->tags[k].x = t->x; g->tags[k].y = t->y; g->tags[k].team = (uint8_t)(t->team & 1);
        g->tags[k].active = 1; g->tags[k].ttl = KC_TAG_TICKS;
    }
    if (t->lives && g->life_limit != MATCH_UNLIMITED_LIVES &&
        (g->life_limit || !(g->extras & (EXTRA_TDM | EXTRA_WARLORD | EXTRA_KILLCONF | EXTRA_HARDPOINT)))) t->lives--;   /* (back every time) */
    if (t->lives == 0)
        t->out = 1;
    t->respawn = RESPAWN_TICKS;
}

static void say(Tank *t, int msg);

/* does this shot spare tank i? its own side's, as ever; every tank for itself: only the one who fired */
static int spares(const Game *g, const Shot *s, int i)
{
    if ((g->extras & EXTRA_ANNIHILATION) && s->owner < g->num_tanks)
        return s->owner == i || g->tanks[i].guard;       /* (just out of the garage: not yet) */
    return g->tanks[i].team == s->team;
}

static int hit_tank(Game *g, Shot *s)
{
    int i;
    for (i = 0; i < g->num_tanks; i++) {
        Tank *t = &g->tanks[i];
        if (!t->active || t->respawn || spares(g, s, i))
            continue;
        if (fx_abs(s->x - t->x) < HIT_RADIUS && fx_abs(s->y - t->y) < HIT_RADIUS) {
            spawn_effect_ex(g, EFF_SPARK, s->x, s->y, 0, s->damage * 10, fx_atan2(s->vx, s->vy));
            if (t->armor > taken(g, t, s->damage)) {
                t->armor = (uint8_t)(t->armor - taken(g, t, s->damage));
            } else {
                tank_destroyed(g, t);
                if (s->owner < g->num_tanks) {                  /* a tank got a tank */
                    Tank *k = &g->tanks[s->owner];
                    k->score += 500;
                    if (k->tank_kills < 255) k->tank_kills++;
                    say(k, t->out ? MSG_ENEMY_OUT : MSG_TANK_KILLED);
                }
            }
            return 1;
        }
    }
    return 0;
}

static void splat(Game *g, Man *m, int scorer)
{
    spawn_effect_ex(g, EFF_SPLAT, m->x, m->y, 0, m->kind == MAN_POW ? 60 : 100, 0);
    if (scorer >= 0 && scorer < g->num_tanks) {
        if (m->kind == MAN_SOLDIER && m->team != g->tanks[scorer].team) {
            g->tanks[scorer].score += 10;
            g->tanks[scorer].kills++;
        } else {
            g->tanks[scorer].pows_lost++;               /* one of ours, under our own tracks */
        }
    }
    m->kind = MAN_NONE;
}

static int hit_air_or_men(Game *g, Shot *s)
{
    int i;
    if (s->team != TEAM_ENEMY) {
        for (i = 0; i < MAX_HELIS; i++) {               /* helicopters */
            Heli *h = &g->helis[i];
            if (!h->active) continue;
            if (fx_abs(s->x - h->x) < FX(11) && fx_abs(s->y - h->y) < FX(11)) {
                spawn_effect_ex(g, EFF_SPARK, s->x, s->y, 0, 80, fx_atan2(s->vx, s->vy));
                if (--h->hp == 0) {
                    spawn_effect_ex(g, EFF_BLAST, h->x, h->y, 0, 120, h->angle);
                    h->active = 0;
                    if (h->pad >= 0)                       /* the pad gets a new one later */
                        g->map.st[h->pad].aim = 30 * TICK_RATE;
                    if (s->owner < g->num_tanks) {
                        g->tanks[s->owner].score += 150;
                        g->tanks[s->owner].helis_down++;
                    }
                }
                return 1;
            }
        }
    }
    for (i = 0; i < MAX_MEN; i++) {                     /* soldiers of the other side */
        Man *m = &g->men[i];
        if (m->kind != MAN_SOLDIER || m->team == s->team) continue;
        if (fx_abs(s->x - m->x) < FX(4) && fx_abs(s->y - m->y) < FX(4)) {
            splat(g, m, s->owner < g->num_tanks ? s->owner : -1);
            return 1;
        }
    }
    return 0;
}

/* how many men run out of a building when it falls */
static int occupants(int type)
{
    switch (type) {
    case ST_BARRACKS:  return 6;
    case ST_HQ:        return 7;
    case ST_BUNKER:    return 3;
    case ST_WAREHOUSE: return 3;
    case ST_POWER:     return 3;
    case ST_DOME:      return 3;
    case ST_RADAR:     return 2;
    case ST_TOWER:     return 1;
    case ST_T_MG: case ST_T_CANNON: case ST_T_HEAVY: case ST_T_ROCKET: return 1;
    default:           return 0;
    }
}

static int spawn_man(Game *g, ManKind kind, fx x, fx y, int home, int team);
static int prop_hit(const Prop *p, fx x, fx y, fx pad);

static int hit_prop(Game *g, const PropIndex *index, Shot *s)
{
    int first = g->num_props, column, end = prop_column(s->x + FX(32)), k;
    for (column = prop_column(s->x - FX(32)); column <= end; column++) {
        int id;
        for (id = index->head[column]; id; id = index->next[id - 1]) {
            const Prop *p = &g->props[id - 1];
            if (id - 1 < first && p->alive && prop_hit(p, s->x, s->y, 0)) first = id - 1;
        }
    }
    if (first < g->num_props) {
        Prop *p = &g->props[first];
        const PropInfo *pi = &prop_info[p->kind];
        if (s->owner >= g->num_tanks && p->team == s->team) return 1;   /* guns spare their own */
        if (p->hp > (s->kind == SHOT_HEAVY ? 2 : 1)) {
            p->hp = (uint8_t)(p->hp - (s->kind == SHOT_HEAVY ? 2 : 1));
            spawn_effect_ex(g, EFF_HIT, s->x, s->y, 0, 40, fx_atan2(s->vx, s->vy));
            return 1;
        }
        p->hp = 0;
        p->alive = 0;                                  /* a burnt-out wreck from now on */
        p->died = (uint16_t)g->tick;
        spawn_effect_ex(g, EFF_BLAST, p->x, p->y, 0, pi->blast, p->angle);
        if (s->owner < g->num_tanks && g->tanks[s->owner].team != p->team)
            g->tanks[s->owner].score += pi->score * 5u + (p->moving ? 50u : 0u);   /* (on the move: harder, worth more) */
        p->moving = 0;
        for (k = 0; k < pi->crew; k++) {               /* the crew bails out and runs */
            int oy = grange(g, -8, 8), ox = grange(g, -8, 8);
            int m = spawn_man(g, MAN_SOLDIER, p->x + FX(ox), p->y + FX(oy), 255, p->team);
            if (m >= 0) {
                g->men[m].state = MAN_FLEE;
                g->men[m].timer = (uint16_t)(1000 + grange(g, 60, 160));
                g->men[m].dir = (angle16)(fx_atan2(p->x - s->x, p->y - s->y) + grange(g, -0x2000, 0x2000));
            }
        }
        if (pi->blast == 255) {                        /* a fuel tanker takes things with it */
            for (k = 0; k < g->num_tanks; k++) {
                Tank *t = &g->tanks[k];
                if (t->active && !t->respawn && dist_fx(t->x, t->y, p->x, p->y) < FX(30)) {
                    if (t->armor > taken(g, t, 12)) t->armor = (uint8_t)(t->armor - taken(g, t, 12));
                    else tank_destroyed(g, t);
                }
            }
        }
        return 1;
    }
    return 0;
}

static void structure_fell(Game *g, int idx, fx from_x, fx from_y, int shooter)
{
    Structure *st = &g->map.st[idx];
    fx cx, cy;
    int k, n;
    struct_centre(&g->map, idx, &cx, &cy);
    st->aim = (uint16_t)g->tick;                      /* the remains glow for a while */
    st->smoulder = 1;
    if (st->type == ST_DEPOT && shooter >= 0 && shooter < g->num_tanks && st->team != g->tanks[shooter].team) {
        Tank *t = &g->tanks[shooter];                 /* blowing their fuel tops ours up */
        uint32_t cap = (uint32_t)world_tank_stats(g->vehicle_world, (TankClass)t->cls)->fuel * TICK_RATE, f = t->fuel + cap / 4;
        t->fuel = (uint16_t)(f > cap ? cap : f);
    }
    if ((n = occupants(st->type)) > 0) {                  /* survivors scatter, whoever's they are */
        const StructInfo *si = &struct_info[st->type];
        n = grange(g, (n + 1) / 2, n);
        for (k = 0; k < n; k++) {
            fx x = cx + FX(grange(g, -si->w * 6, si->w * 6)), y = cy + FX(grange(g, -si->h * 6, si->h * 6));
            int i = spawn_man(g, MAN_SOLDIER, x, y, 255, st->team);
            if (i >= 0) {
                g->men[i].state = MAN_FLEE;
                g->men[i].timer = (uint16_t)(1000 + grange(g, 80, 200));   /* counts down to 1000 */
                g->men[i].dir = (angle16)(fx_atan2(x - from_x, y - from_y) + grange(g, -0x1800, 0x1800));
            }
        }
    }
    if (st->type == ST_POWCAMP) {                        /* prisoners pour out */
        n = grange(g, 5, 9);
        for (k = 0; k < n; k++)
            {
                int oy = grange(g, -14, 14), ox = grange(g, -14, 14);
                spawn_man(g, MAN_POW, cx + FX(ox), cy + FX(oy), 255, TEAM_PLAYER);
            }
    }
    if (st->type == ST_FLAGHOUSE)                        /* the flag is out in the open */
        for (k = 0; k < 2; k++)
            if (g->bases[k].flag_house == idx && g->flags[k].state == FLAG_HIDDEN)
                g->flags[k].state = FLAG_GROUND;
}

static void steer_rocket(Game *g, Shot *s)
{
    /* rockets bend gently towards their target tank */
    const Tank *t;
    angle16 cur, want;
    int16_t diff;
    fx speed = FX_FRAC(25, 10) + FX_FRAC(1, 10) * (95 - s->ttl) / 8;    /* accelerate */
    if (s->target >= g->num_tanks) return;
    t = &g->tanks[s->target];
    cur = fx_atan2(s->vx, s->vy);
    if (t->active && !t->respawn) {
        want = fx_atan2(t->x - s->x, t->y - s->y);
        diff = (int16_t)(want - cur);
        if (diff > 0x0180) diff = 0x0180;
        if (diff < -0x0180) diff = -0x0180;
        cur = (angle16)(cur + diff);
    }
    if (speed > FX(5)) speed = FX(5);
    s->vx = fx_mul(speed, fx_sin(cur));
    s->vy = -fx_mul(speed, fx_cos(cur));
    if ((s->ttl & 3) == 0)                               /* smoke trail */
        spawn_effect_ex(g, EFF_PUFF, s->x, s->y, 0, 20, cur);
}

static void shots_update(Game *g, const PropIndex *index)
{
    int i;
    for (i = 0; i < MAX_SHOTS; i++) {
        Shot *s = &g->shots[i];
        int tx, ty, sid;
        TileType tt;
        if (!s->active)
            continue;
        if (s->kind == SHOT_ROCKET)
            steer_rocket(g, s);
        s->prev_x = s->x;
        s->prev_y = s->y;
        s->x += s->vx;              /* <= 5 px/tick < 16 px tile: no tunnelling */
        s->y += s->vy;
        if (s->kind == SHOT_MORTAR) {                     /* arcs over everything, lands, bursts */
            if (--s->ttl == 0) {
                int j;
                spawn_effect_ex(g, EFF_EXPLOSION, s->x, s->y, 0, 110, 0);
                for (j = 0; j < g->num_tanks; j++) {
                    Tank *t = &g->tanks[j];
                    fx d;
                    if (!t->active || t->respawn || spares(g, s, j)) continue;
                    d = dist_fx(t->x, t->y, s->x, s->y);
                    if (d < MORTAR_SPLASH) {
                        int dmg = taken(g, t, d < MORTAR_SPLASH / 2 ? s->damage : s->damage / 2);
                        if (t->armor > dmg) t->armor = (uint8_t)(t->armor - dmg);
                        else tank_destroyed(g, t);
                    }
                }
                s->active = 0;
            }
            continue;
        }
        if (hit_tank(g, s) || hit_air_or_men(g, s) || hit_prop(g, index, s)) {
            s->active = 0;
            continue;
        }
        tx = fx_to_int(s->x) >> TILE_SHIFT;
        ty = fx_to_int(s->y) >> TILE_SHIFT;
        tt = map_get(&g->map, tx, ty);
        sid = (tx >= 0 && ty >= 0 && tx < g->map.w && ty < g->map.h) ? g->map.sid[ty][tx] : 0;

        if ((tile_info[tt].flags & TF_BLOCKS_SHOT) && !(sid && sid == s->src_sid)) {
            int sidx = -1, r = DMG_NONE;
            /* our own guns never hit our buildings, but a tank may knock down
             * whatever it likes, its own side's too (garages excepted) */
            int own = sid && s->owner >= g->num_tanks && g->map.st[sid - 1].team == s->team;
            int may = s->owner < g->num_tanks || sid;               /* guns don't level terrain */
            if (!own && may) {
                int heavy = s->kind == SHOT_HEAVY || (s->kind == SHOT_ROCKET && s->owner < g->num_tanks);
                r = map_damage(&g->map, tx, ty, heavy ? 2 : 1, &sidx);
            }
            if (r == DMG_STRUCT) {
                const Structure *st = &g->map.st[sidx];
                if (s->owner < g->num_tanks && st->team != g->tanks[s->owner].team)
                    g->tanks[s->owner].score += struct_info[st->type].score * 5u;   /* no points for our own */
                structure_explosion(g, st);
                structure_fell(g, sidx, s->x - s->vx * 8, s->y - s->vy * 8, s->owner);
            } else if (r == DMG_TILE) {
                if (s->owner < g->num_tanks)
                    g->tanks[s->owner].score += destroy_score[tt];
                spawn_effect_ex(g, EFF_EXPLOSION, FX(tx * TILE_SIZE + TILE_SIZE / 2),
                                FX(ty * TILE_SIZE + TILE_SIZE / 2), 0, 90, 0);
            } else {
                spawn_effect_ex(g, s->kind == SHOT_ROCKET ? EFF_EXPLOSION : EFF_HIT, s->x, s->y, 0,
                                s->kind == SHOT_ROCKET ? 70 : s->damage * 8, fx_atan2(s->vx, s->vy));
            }
            s->active = 0;
            continue;
        }
        if (--s->ttl == 0) {
            if (s->kind == SHOT_ROCKET)
                spawn_effect_ex(g, EFF_EXPLOSION, s->x, s->y, 0, 60, 0);
            else
                spawn_effect(g, EFF_PUFF, s->x, s->y);
            s->active = 0;
        }
    }
}

/* ---- turrets ------------------------------------------------------------------------- */

/* can a shot get from (ax,ay) to (bx,by)? Walls, buildings, rocks and bushes
 * block it; the shooter's own structure doesn't */
static int clear_shot(const Game *g, fx ax, fx ay, fx bx, fx by, int own_sid)
{
    fx d = dist_fx(ax, ay, bx, by);
    int steps = fx_to_int(d) / 6 + 1, k;
    fx dx = bx - ax, dy = by - ay, x = ax, y = ay;
    fx step_x, step_y;
    int rem_x, rem_y, carry_x, carry_y, err_x = 0, err_y = 0;
    if (steps <= 1) return 1;
    step_x = dx / steps;
    step_y = dy / steps;
    rem_x = fx_abs(dx % steps);
    rem_y = fx_abs(dy % steps);
    carry_x = dx < 0 ? -1 : 1;
    carry_y = dy < 0 ? -1 : 1;
    /* Carry the fractional remainder to reproduce truncation toward zero
     * at each sample, with no 64-bit division in the loop. */
    for (k = 1; k < steps; k++) {
        int tx, ty;
        TileType tt;
        x += step_x;
        y += step_y;
        err_x += rem_x;
        err_y += rem_y;
        if (err_x >= steps) { x += carry_x; err_x -= steps; }
        if (err_y >= steps) { y += carry_y; err_y -= steps; }
        tx = fx_to_int(x) >> TILE_SHIFT;
        ty = fx_to_int(y) >> TILE_SHIFT;
        tt = map_get(&g->map, tx, ty);
        if ((tile_info[tt].flags & TF_BLOCKS_SHOT) &&
            !(own_sid && tx >= 0 && ty >= 0 && tx < g->map.w && ty < g->map.h && g->map.sid[ty][tx] == own_sid))
            return 0;
    }
    return 1;
}

static void turrets_update(Game *g)
{
    int i, j;
    for (i = 0; i < g->map.num_st; i++) {
        Structure *st = &g->map.st[i];
        const StructInfo *si = &struct_info[st->type];
        const TurretInfo *ti;
        fx cx, cy, best = 0;
        int target = -1;
        if (!(si->flags & SF_TURRET) || st->hp == 0)
            continue;
        ti = &turret_info[st->type];
        if (st->cooldown) st->cooldown--;
        cx = FX(st->x * TILE_SIZE + si->w * TILE_SIZE / 2);
        cy = FX(st->y * TILE_SIZE + si->h * TILE_SIZE / 2);
        for (j = 0; j < g->num_tanks; j++) {       /* nearest enemy tank in range */
            const Tank *t = &g->tanks[j];
            fx dx, dy, d;
            if (!t->active || t->respawn || t->team == st->team)
                continue;
            dx = fx_abs(t->x - cx);
            dy = fx_abs(t->y - cy);
            d = dx > dy ? dx + dy / 2 : dy + dx / 2;   /* cheap distance */
            if (d >= ti->range || (target >= 0 && d >= best))
                continue;
            /* only mortars lob over walls; everyone else needs a clear line */
            if (ti->kind != SHOT_MORTAR && !clear_shot(g, cx, cy, t->x, t->y, i + 1))
                continue;
            {
                best = d;
                target = j;
            }
        }
        if (target < 0)
            continue;
        {
            /* like the original, guns only fire along the 8 compass directions:
             * stay off those lines and they can't touch you (mortars and
             * homing rockets excepted) */
            const Tank *t = &g->tanks[target];
            angle16 exact = fx_atan2(t->x - cx, t->y - cy);
            angle16 want = (angle16)((exact + 0x1000) & 0xE000);
            int16_t off = (int16_t)(exact - want);
            int lined_up = (off > -0x0900 && off < 0x0900) || ti->kind == SHOT_MORTAR;
            int16_t diff = (int16_t)(want - st->aim);
            if (diff > (int16_t)ti->turn) diff = (int16_t)ti->turn;
            if (diff < -(int16_t)ti->turn) diff = (int16_t)-ti->turn;
            st->aim = (angle16)(st->aim + diff);
            diff = (int16_t)(want - st->aim);
            if (!st->cooldown && diff == 0 && lined_up) {
                fx mx = cx + fx_mul(ti->muzzle, fx_sin(st->aim));
                fx my = cy - fx_mul(ti->muzzle, fx_cos(st->aim));
                angle16 a = st->aim;
                if (ti->spread)                            /* nobody hits every time */
                    a = (angle16)(a + grange(g, -(int)ti->spread, (int)ti->spread));
                if (ti->kind == SHOT_MORTAR) {             /* lob it at where the tank is now */
                    fx d = dist_fx(t->x, t->y, cx, cy);
                    int flight = fx_to_int(d) / 3 + 10;
                    int oy = grange(g, -10, 10), ox = grange(g, -10, 10);
                    angle16 at = fx_atan2(t->x + FX(ox) - cx, t->y + FX(oy) - cy);
                    fire(g, mx, my, at, fx_mul(d, FX(1)) / flight, flight, SHOT_MORTAR, ti->damage,
                         st->team, 255, i + 1, target);
                } else {
                    fire(g, mx, my, a, ti->speed, ti->ttl, ti->kind, ti->damage, st->team, 255, i + 1, target);
                }
                if (st->type == ST_T_ROCKET) {             /* a pair of rockets */
                    fx ox = fx_mul(FX(4), fx_cos(st->aim)), oy = fx_mul(FX(4), fx_sin(st->aim));
                    fire(g, mx + ox, my + oy, st->aim, ti->speed, ti->ttl, ti->kind, ti->damage,
                         st->team, 255, i + 1, target);
                }
                if (ti->burst > 1) {                       /* auto-cannon: rattle off a burst */
                    if (++st->burst < ti->burst) st->cooldown = ti->burst_gap;
                    else { st->burst = 0; st->cooldown = ti->reload; }
                } else {
                    st->cooldown = ti->reload;
                }
            }
        }
    }
}

/* ---- tanks ------------------------------------------------------------------------------- */

/* is point (x,y) inside the box of a standing vehicle, grown by pad? */
static int prop_hit(const Prop *p, fx x, fx y, fx pad)
{
    const PropInfo *pi = &prop_info[p->kind];
    fx dx = x - p->x, dy = y - p->y;
    fx half_l = pi->half_l + pad, half_w = pi->half_w + pad;
    fx reach = half_l + half_w + FX(1);                /* encloses all headings, including rounding */
    fx s, c, along, across;
    if (fx_abs(dx) >= reach || fx_abs(dy) >= reach) return 0;
    s = fx_sin(p->angle);
    c = fx_cos(p->angle);
    along = fx_mul(dx, s) - fx_mul(dy, c);              /* forward axis */
    across = fx_mul(dx, c) + fx_mul(dy, s);
    return fx_abs(along) < half_l && fx_abs(across) < half_w;
}

static int path_blocked(const Game *g, const PropIndex *index, fx x, fx y, fx half, int team)
{
    int column, end = prop_column(x + FX(32));
    if (map_box_solid_for_team(&g->map, x, y, half, team)) return 1;
    for (column = prop_column(x - FX(32)); column <= end; column++) {
        int id;
        for (id = index->head[column]; id; id = index->next[id - 1]) {
            const Prop *p = &g->props[id - 1];
            if (p->alive && prop_hit(p, x, y, half)) return 1;
        }
    }
    return 0;
}

static void say(Tank *t, int msg) { t->msg = (uint8_t)msg; t->msg_t = 120; }

/* garage, first aid, mines, flags, and the anti-camping clock */
static void evacuee_start(Game *g, const Tank *tank, fx cx, fx cy)
{
    int i;
    fx dx = tank->x - cx, dy = tank->y - cy;
    for (i = 0; i < MAX_EVACUEES; i++) {
        Man *m = &g->evacuees[i];
        if (m->kind) continue;
        /* Aim for the near side of the two-by-two building, just inside its edge. */
        m->tx = cx;
        m->ty = cy;
        if (fx_abs(dx) > fx_abs(dy)) m->tx += dx < 0 ? -FX(13) : FX(13);
        else m->ty += dy < 0 ? -FX(13) : FX(13);
        m->dir = fx_atan2(m->tx - tank->x, m->ty - tank->y);
        m->x = m->prev_x = tank->x + 4 * fx_sin(m->dir);
        m->y = m->prev_y = tank->y - 4 * fx_cos(m->dir);
        m->kind = MAN_POW;
        m->state = MAN_ALERT;
        return;
    }
}

static void evacuees_update(Game *g)
{
    const fx pace = g->relaxed_pace ? FX_FRAC(105, 100) : FX_FRAC(14, 10);
    int i;
    for (i = 0; i < MAX_EVACUEES; i++) {
        Man *m = &g->evacuees[i];
        if (!m->kind) continue;
        if (dist_fx(m->x, m->y, m->tx, m->ty) <= pace) {
            m->kind = MAN_NONE;
            continue;
        }
        m->prev_x = m->x;
        m->prev_y = m->y;
        m->dir = fx_atan2(m->tx - m->x, m->ty - m->y);
        m->x += fx_mul(pace, fx_sin(m->dir));
        m->y -= fx_mul(pace, fx_cos(m->dir));
    }
}

static void tank_services(Game *g, int idx, InputBits in)
{
    Tank *t = &g->tanks[idx];
    const TankStats *st = world_tank_stats(g->vehicle_world, (TankClass)t->cls);
    int tx = fx_to_int(t->x) >> TILE_SHIFT, ty = fx_to_int(t->y) >> TILE_SHIFT, i;
    int home = in_base(g, t, t->team);
    uint16_t sid = g->map.sid[ty][tx];
    Flag *enemy_flag = &g->flags[1 - (t->team & 1)];

    if (sid && g->map.st[sid - 1].type == ST_GARAGE && g->map.st[sid - 1].team == t->team) {
        /* in one of our garages: refuel, repair, restock */
        uint16_t cap = (uint16_t)(st->fuel * TICK_RATE);
        t->fuel = (uint16_t)(t->fuel + cap / 150 + 1 > cap ? cap : t->fuel + cap / 150 + 1);
        if ((g->tick & 3) == 0 && t->armor < armor_max(g, t)) t->armor++;
        if (t->mines < st->mines && (g->tick % 25) == 0 && !is_jug(g, t)) t->mines++;   /* (a Warlord lays none) */
    } else if (home && t->speed == 0 && t->armor < armor_max(g, t) && (g->tick + (uint32_t)idx * 7u) % (2 * TICK_RATE) == 0) {
        t->armor++;                                     /* repairs in the field: standing still in your own base, slowly */
    }

    if (t->speed == 0 && t->pows) {                             /* unload at our first aid */
        for (i = 0; i < g->map.num_st; i++) {
            const Structure *fa = &g->map.st[i];
            fx cx, cy;
            if (fa->type != ST_FIRSTAID || fa->team != t->team || fa->hp == 0) continue;
            struct_centre(&g->map, i, &cx, &cy);
            if (dist_fx(cx, cy, t->x, t->y) > FX(38)) continue;
            if (++t->unload >= 10) {
                t->unload = 0;
                t->pows--;
                t->saved++;
                t->score += 50;
                evacuee_start(g, t, cx, cy);
                spawn_effect_ex(g, EFF_CHEER, cx, cy, 0, 60, 0);
                if (t->saved % MEN_PER_MINE == 0 && t->mines < 99)
                    t->mines++;                               /* every 5 men: a mine */
                if (t->saved % MEN_PER_LIFE == 0) {
                    if (g->life_limit != MATCH_UNLIMITED_LIVES && t->lives < 254) t->lives++;
                    say(t, MSG_EXTRA_LIFE);
                } else if (t->pows == 0) {
                    say(t, MSG_SAVED);
                }
            }
            break;
        }
    }

    if (t->mine_cd) t->mine_cd--;
    if ((in & IN_MINE) && !t->mine_cd && t->mines) {           /* drop a mine behind us */
        for (i = 0; i < MAX_MINES; i++)
            if (!g->mines[i].active) {
                Mine *m = &g->mines[i];
                m->x = t->x - fx_mul(FX(10), fx_sin(t->angle));
                m->y = t->y + fx_mul(FX(10), fx_cos(t->angle));
                m->team = t->team;
                m->arm = MINE_ARM;
                m->active = 1;
                t->mines--;
                t->mine_cd = 25;
                break;
            }
    }

    if (!(g->extras & EXTRA_NOFLAGS) &&                  /* (those have no flags to take) */
        enemy_flag->state == FLAG_GROUND && dist_fx(enemy_flag->x, enemy_flag->y, t->x, t->y) < FX(10)) {
        enemy_flag->state = FLAG_CARRIED;                       /* got it */
        enemy_flag->carrier = (uint8_t)idx;
        t->has_flag = 1;
        say(t, MSG_FLAG);
        for (i = 0; i < g->num_tanks; i++)                      /* and the other side hears of it */
            if (g->tanks[i].team != t->team) say(&g->tanks[i], MSG_FLAG_LOST);
    }
    {   /* our own flag lying out in the open: drive over it to take it back home */
        Flag *own = &g->flags[t->team & 1];
        if (own->state == FLAG_GROUND && (own->x != own->home_x || own->y != own->home_y) &&
            dist_fx(own->x, own->y, t->x, t->y) < FX(10)) {
            own->x = own->home_x;
            own->y = own->home_y;
            say(t, MSG_FLAG_BACK);
        }
    }
    if (t->has_flag && home && g->winner < 0) {                 /* brought it home: victory */
        g->winner = (int8_t)t->team;
        t->score += 1000;
        say(t, MSG_WIN);
        for (i = 0; i < g->num_tanks; i++)
            if (g->tanks[i].team != t->team) say(&g->tanks[i], MSG_LOST);
    }

    if (t->speed == 0 && !home) { if (t->still < 60000) t->still++; } else t->still = 0;
    if (t->speed != 0) { if (t->moving < 60000) t->moving++; } else t->moving = 0;
}

/* The original's controls: the stick gives the direction to go. Turn
 * towards it (or, if it points backwards, back up that way), and only put
 * the foot down once roughly lined up. Returns tank-style input bits. */
static InputBits direct_steering(Tank *t, const TankStats *st, InputBits in)
{
    int dx = !!(in & IN_RIGHT) - !!(in & IN_LEFT), dy = !!(in & IN_DOWN) - !!(in & IN_UP);
    InputBits out = (InputBits)(in & (IN_FIRE | IN_MINE));
    angle16 want, face;
    int16_t diff;
    int reverse;
    if (!dx && !dy)
        return out;                                   /* stick centred: roll to a stop */
    want = fx_atan2(FX(dx), FX(dy));
    diff = (int16_t)(want - t->angle);
    reverse = (diff > 0x5000 || diff < -0x5000) && t->speed <= 0;
    face = reverse ? (angle16)(want + 0x8000) : want;
    diff = (int16_t)(face - t->angle);
    if (diff > -(int16_t)st->turn_rate && diff < (int16_t)st->turn_rate)
        t->angle = face;                              /* lined up: snap to the 8 directions */
    else if (diff > 0) out |= IN_RIGHT;
    else out |= IN_LEFT;
    if (diff > -0x2000 && diff < 0x2000)
        out |= reverse ? IN_DOWN : IN_UP;
    return out;
}

static void tank_update(Game *g, const PropIndex *index, int idx, InputBits in)
{
    Tank *t = &g->tanks[idx];
    const TankStats *st = world_tank_stats(g->vehicle_world, (TankClass)t->cls);
    fx dx, dy, max_fwd = st->max_fwd, max_rev = st->max_rev;
    int blocked = 0, moving_axes = 0;
    int surface = map_get(&g->map, fx_to_int(t->x) >> TILE_SHIFT, fx_to_int(t->y) >> TILE_SHIFT);
    int slippery = g->enhanced_rules && (surface == TILE_ICE || surface == TILE_OIL);
    fx accel = slippery ? st->accel / 4 : st->accel;
    if (t->boost_ticks) { t->boost_ticks--; max_fwd = max_fwd * 5 / 4; max_rev = max_rev * 5 / 4; }
    if (t->rapid_ticks) t->rapid_ticks--;
    if (is_jug(g, t)) { max_fwd = max_fwd * 5 / 4; max_rev = max_rev * 5 / 4; }   /* (a Warlord: faster) */
    if (g->relaxed_pace) {
        max_fwd = max_fwd * 7 / 8;
        max_rev = max_rev * 7 / 8;
    }
    if (g->weather == WEATHER_STORM || g->biome == BIOME_SWAMP) {   /* a storm, or the swamp's mud: open ground is heavy going */
        int tx = (int)(t->x >> 20), ty = (int)(t->y >> 20);   /* (16 px tiles, Q16.16) */
        int storm = g->weather == WEATHER_STORM, open = tx >= 0 && tx < g->map.w && ty >= 0 && ty < g->map.h && g->map.type[ty][tx] == TILE_SAND;
        if (storm && g->biome == BIOME_VOLCANIC) {       /* ash: on everything, the roads too */
            max_fwd = max_fwd * 17 / 20;
            max_rev = max_rev * 17 / 20;
        } else if (open) {
            int k = storm && g->biome == BIOME_SWAMP ? 14 : 17;   /* (a storm in the swamp: the mud worse) */
            max_fwd = max_fwd * k / 20;
            max_rev = max_rev * k / 20;
        }
        if (storm && g->biome == BIOME_WASTELAND && open && t->active && !t->respawn && t->armor > 1 &&
            (g->tick + (uint32_t)idx * 17u) % (5 * TICK_RATE) == 0)
            t->armor--;                                  /* toxic rain: in the open it eats at you (never the last of it) */
    }

    if (t->msg_t) t->msg_t--;
    if (t->respawn) {                               /* wrecked: wait, then a fresh tank */
        if (t->out) return;
        if (--t->respawn == 0) {
            tank_refill(g, t, 0);
            place_in_garage(g, t);
            if (g->extras & EXTRA_ANNIHILATION) t->guard = FFA_GUARD;
            if (is_jug(g, t)) jug_boost(g, t);           /* (the Warlord, back: as it was) */
        }
        return;
    }
    if (t->guard) t->guard = (uint8_t)((in & IN_FIRE) ? 0 : t->guard - 1);   /* (firing ends it) */
    if (t->steer == STEER_DIRECT && t->speed == 0 && (in & IN_DOWN) && (in & IN_FIRE) &&
        !t->mine_cd && t->mines) {
        /* the original's mine: stop, pull down and fire (only with its controls;
         * with tank controls down + fire is reversing while shooting) */
        in = (InputBits)((in & ~(IN_DOWN | IN_FIRE | IN_UP | IN_LEFT | IN_RIGHT)) | IN_MINE);
    } else if (t->steer == STEER_DIRECT) {
        in = direct_steering(t, st, in);
    }
    if (in & (IN_UP | IN_DOWN)) {
        if (t->fuel && --t->fuel == 0) {             /* dry tank: stranded, a life gone */
            tank_destroyed(g, t);
            t->msg = MSG_NO_FUEL; t->msg_t = 150;
            return;
        }
    }
    if (in & IN_LEFT)  t->angle = (angle16)(t->angle - st->turn_rate);
    if (in & IN_RIGHT) t->angle = (angle16)(t->angle + st->turn_rate);

    if (in & IN_UP) {
        t->speed += accel;
        if (t->speed > max_fwd) t->speed = max_fwd;
    } else if (in & IN_DOWN) {
        t->speed -= accel;
        if (t->speed < -max_rev) t->speed = -max_rev;
    } else {                                             /* rolling to a stop (the moon: little grip, a long slide) */
        fx fr = g->biome == BIOME_MOON ? st->friction / (g->weather == WEATHER_STORM ? 6 : 3) : st->friction;   /* (a dust storm: slipperier) */
        if (slippery) fr /= 12;
        if (t->speed > 0) t->speed = t->speed > fr ? t->speed - fr : 0;
        else if (t->speed < 0) t->speed = -t->speed > fr ? t->speed + fr : 0;
    }

    dx = fx_mul(t->speed, fx_sin(t->angle));
    dy = -fx_mul(t->speed, fx_cos(t->angle));
    if (g->enhanced_rules) {
        if (slippery) {
            t->drift_x += (dx - t->drift_x) / 12;
            t->drift_y += (dy - t->drift_y) / 12;
            dx = t->drift_x; dy = t->drift_y;
        } else { t->drift_x = dx; t->drift_y = dy; }
    }
    if (dx) {
        moving_axes++;
        if (!path_blocked(g, index, t->x + dx, t->y, TANK_HALF, t->team)) t->x += dx;
        else { blocked++; t->drift_x = 0; }
    }
    if (dy) {
        moving_axes++;
        if (!path_blocked(g, index, t->x, t->y + dy, TANK_HALF, t->team)) t->y += dy;
        else { blocked++; t->drift_y = 0; }
    }
    if (blocked && blocked == moving_axes) { t->speed = 0; t->drift_x = t->drift_y = 0; }
    else if (blocked)                      t->speed -= t->speed / 8;
    if (t->speed) {                                   /* bushes go flat under the tracks */
        int x0 = fx_to_int(t->x - TANK_HALF) >> TILE_SHIFT, x1 = fx_to_int(t->x + TANK_HALF - 1) >> TILE_SHIFT;
        int y0 = fx_to_int(t->y - TANK_HALF) >> TILE_SHIFT, y1 = fx_to_int(t->y + TANK_HALF - 1) >> TILE_SHIFT;
        int x, y;
        for (y = y0; y <= y1; y++)
            for (x = x0; x <= x1; x++)
                if (map_get(&g->map, x, y) == TILE_TREE)
                    map_set(&g->map, x, y, TILE_STUMP);
    }

    if (t->reload) t->reload--;
    if ((in & IN_FIRE) && !t->reload) {
        int rocket = t->rocket_launcher;
        int reload = rocket ? 34 : st->reload;
        if (t->rapid_ticks) reload = (reload + 1) / 2;
        fire(g, t->x + fx_mul(NOSE_DIST, fx_sin(t->angle)), t->y - fx_mul(NOSE_DIST, fx_cos(t->angle)),
             t->angle, rocket ? FX(3) : SHOT_SPEED,
             rocket ? 62 : g->vehicle_world == BIOME_MOON ? 56 : SHOT_TTL,
             rocket ? SHOT_ROCKET : SHOT_SHELL, rocket ? 18 : TANK_SHOT_DAMAGE, t->team, idx, 0, 255);
        t->reload = (uint8_t)(is_jug(g, t) ? reload / 2 : reload);   /* (a Warlord: twice as fast) */
    }
    tank_services(g, idx, in);
}

/* ---- people ------------------------------------------------------------------------ */

static void man_step(Game *g, Man *m, fx tx, fx ty, fx speed)
{
    angle16 a = fx_atan2(tx - m->x, ty - m->y);
    if (g->relaxed_pace) speed = speed * 3 / 4;
    fx dx = fx_mul(speed, fx_sin(a)), dy = -fx_mul(speed, fx_cos(a));
    m->dir = a;
    if (!map_box_solid(&g->map, m->x + dx, m->y, FX(2))) m->x += dx;
    if (!map_box_solid(&g->map, m->x, m->y + dy, FX(2))) m->y += dy;
}

static void men_update(Game *g)
{
    int i, j;
    for (i = 0; i < MAX_MEN; i++) {
        Man *m = &g->men[i];
        int near = -1;
        fx best = 0;
        if (m->kind == MAN_NONE) continue;
        m->prev_x = m->x;
        m->prev_y = m->y;
        for (j = 0; j < g->num_tanks; j++) {                /* nearest player tank */
            const Tank *t = &g->tanks[j];
            fx d;
            if (!t->active || t->respawn || t->team != TEAM_PLAYER) continue;
            d = dist_fx(t->x, t->y, m->x, m->y);
            if (near < 0 || d < best) { near = j; best = d; }
        }
        /* any moving tank of the other side crushes */
        for (j = 0; j < g->num_tanks; j++) {
            const Tank *t = &g->tanks[j];
            if (!t->active || t->respawn || t->speed == 0) continue;
            if (m->kind == MAN_SOLDIER && t->team == m->team) continue;   /* our tanks don't run over our men */
            if (fx_abs(t->x - m->x) < FX(8) && fx_abs(t->y - m->y) < FX(8)) {
                splat(g, m, j);
                break;
            }
        }
        if (m->kind == MAN_NONE) continue;

        if (m->kind == MAN_POW) {
            if (near >= 0 && best < FX(180)) {
                Tank *t = &g->tanks[near];
                int cap = world_tank_stats(g->vehicle_world, (TankClass)t->cls)->pows;
                if (best < FX(14) && t->speed == 0 && t->pows < cap) {   /* climbs aboard */
                    t->pows++;
                    m->kind = MAN_NONE;
                    if (t->pows == cap) say(t, MSG_FULL);
                    continue;
                }
                if (best > FX(10))
                    man_step(g, m, t->x, t->y, FX_FRAC(9, 10));      /* runs to the tank */
                m->state = 1;
            } else {
                m->state = 0;                                       /* waves, mills about */
                if (++m->timer > 80) {
                    m->timer = 0;
                    m->tx = m->x + FX(grange(g, -12, 12));
                    m->ty = m->y + FX(grange(g, -12, 12));
                }
                if (dist_fx(m->tx, m->ty, m->x, m->y) > FX(2))
                    man_step(g, m, m->tx, m->ty, FX_FRAC(3, 10));
            }
        } else if (m->state == MAN_FLEE) {                          /* running for their lives */
            fx tx = m->x + fx_mul(FX(20), fx_sin(m->dir)), ty = m->y - fx_mul(FX(20), fx_cos(m->dir));
            fx ox = m->x, oy = m->y;
            angle16 keep = m->dir;
            man_step(g, m, tx, ty, FX_FRAC(13, 10));
            m->dir = keep;
            if (m->x == ox && m->y == oy)                           /* blocked: veer off */
                m->dir = (angle16)(m->dir + (grange(g, 0, 1) ? 0x2000 : -0x2000));
            if (--m->timer <= 1000) {                               /* catch breath, then fight */
                m->state = MAN_IDLE;
                m->timer = 0;
                m->tx = m->x; m->ty = m->y;
            }
        } else {                                                    /* a soldier, ours or theirs */
            /* the nearest enemy he can see: a tank or a soldier of the other side */
            fx fx_, fy_, fd = 0;
            int ftank = -1, found = 0;
            for (j = 0; j < g->num_tanks; j++) {
                const Tank *t = &g->tanks[j];
                fx d;
                if (!t->active || t->respawn || t->team == m->team) continue;
                d = dist_fx(t->x, t->y, m->x, m->y);
                if (d < FX(130) && (!found || d < fd)) { found = 1; fd = d; fx_ = t->x; fy_ = t->y; ftank = j; }
            }
            for (j = 0; j < MAX_MEN; j++) {
                const Man *o = &g->men[j];
                fx d;
                if (o->kind != MAN_SOLDIER || o->team == m->team) continue;
                d = dist_fx(o->x, o->y, m->x, m->y);
                if (d < FX(110) && (!found || d < fd)) { found = 1; fd = d; fx_ = o->x; fy_ = o->y; ftank = -1; }
            }
            if (found && clear_shot(g, m->x, m->y, fx_, fy_, 0)) {
                m->dir = fx_atan2(fx_ - m->x, fy_ - m->y);
                m->state = 1;
                if (m->cooldown) m->cooldown--;
                else {
                    fire(g, m->x + fx_mul(FX(4), fx_sin(m->dir)), m->y - fx_mul(FX(4), fx_cos(m->dir)),
                         (angle16)(m->dir + grange(g, -0x0300, 0x0300)), FX(3), 45, SHOT_BULLET, 1,
                         m->team, 255, 0, ftank >= 0 ? ftank : 255);
                    m->cooldown = (uint8_t)grange(g, 60, 110);
                }
                if (ftank >= 0 && fd < FX(50))                      /* and backs off from tanks */
                    man_step(g, m, m->x * 2 - fx_, m->y * 2 - fy_, FX_FRAC(6, 10));
            } else {
                m->state = 0;
                if (++m->timer > 120) {                             /* patrol near home */
                    fx hx = m->x, hy = m->y;
                    m->timer = 0;
                    if (m->home < g->map.num_st) barracks_door(g, m->home, &hx, &hy);
                    m->tx = hx + FX(grange(g, -40, 40));
                    m->ty = hy + FX(grange(g, -24, 40));
                }
                if (dist_fx(m->tx, m->ty, m->x, m->y) > FX(2))
                    man_step(g, m, m->tx, m->ty, FX_FRAC(4, 10));
            }
        }
    }
    if (!g->map.quiet && g->tick % (20 * TICK_RATE) == 0)                            /* barracks send replacements */
        for (i = 0; i < g->map.num_st; i++) {
            const Structure *st = &g->map.st[i];
            int count = 0;
            fx x, y;
            if (st->type != ST_BARRACKS || st->hp == 0) continue;
            for (j = 0; j < MAX_MEN; j++)
                count += g->men[j].kind == MAN_SOLDIER && g->men[j].home == i;
            if (count < 3) {
                barracks_door(g, i, &x, &y);
                spawn_man(g, MAN_SOLDIER, x, y, i, st->team);
            }
        }
}

/* ---- helicopters: punish standing still -------------------------------------------- */

static void helis_update(Game *g)
{
    int i, j;
    for (i = 0; i < g->map.num_st; i++) {                   /* replacement helicopters */
        Structure *p = &g->map.st[i];
        if (p->type == ST_HELIPAD && p->aim && p->aim != PAD_AWAY) p->aim--;
    }
    for (j = 0; j < g->num_tanks; j++) {                    /* call them in */
        Tank *t = &g->tanks[j];
        int active = 0;
        if (g->versus || g->calm || !t->active || t->respawn || t->team != TEAM_PLAYER || t->still < CAMP_TICKS) continue;
        if ((t->still - CAMP_TICKS) % (4 * TICK_RATE) != 0) continue;
        for (i = 0; i < MAX_HELIS; i++) active += g->helis[i].active;
        if (active >= 3) continue;
        for (i = 0; i < MAX_HELIS; i++)
            if (!g->helis[i].active) {
                Heli *h = &g->helis[i];
                angle16 a = (angle16)grand(g);
                int k, pad = -1;
                fx best = FX(700);
                memset(h, 0, sizeof *h);
                for (k = 0; k < g->map.num_st; k++) {        /* nearest helipad with one ready */
                    const Structure *p = &g->map.st[k];
                    fx px, py, d;
                    if (p->type != ST_HELIPAD || p->team == t->team || p->aim != 0) continue;
                    struct_centre(&g->map, k, &px, &py);
                    d = dist_fx(px, py, t->x, t->y);
                    if (d < best) { best = d; pad = k; }
                }
                if (pad >= 0) {                              /* takes off from its pad */
                    struct_centre(&g->map, pad, &h->x, &h->y);
                    h->prev_x = h->x; h->prev_y = h->y;
                    h->angle = h->prev_angle = fx_atan2(t->x - h->x, t->y - h->y);
                    g->map.st[pad].aim = PAD_AWAY;
                } else {                                     /* or flies in from off-screen */
                    h->x = h->prev_x = t->x + fx_mul(FX(240), fx_sin(a));
                    h->y = h->prev_y = t->y - fx_mul(FX(240), fx_cos(a));
                    h->angle = h->prev_angle = (angle16)(a + 0x8000);
                }
                h->pad = (int16_t)pad;
                h->hp = 3;
                h->target = (uint8_t)j;
                h->cooldown = 60;
                h->active = 1;
                say(t, MSG_HELI);
                break;
            }
    }
    for (i = 0; i < MAX_HELIS; i++) {
        Heli *h = &g->helis[i];
        const Tank *t;
        angle16 want;
        int16_t diff;
        fx d;
        if (!h->active) continue;
        h->prev_x = h->x; h->prev_y = h->y; h->prev_angle = h->angle;
        h->age++;
        t = &g->tanks[h->target];
        if (!t->active || t->respawn || t->moving > 8 * TICK_RATE) h->leaving = 1;
        d = dist_fx(t->x, t->y, h->x, h->y);
        if (h->leaving && h->pad >= 0) {                     /* fly home and land */
            fx px, py;
            struct_centre(&g->map, h->pad, &px, &py);
            want = fx_atan2(px - h->x, py - h->y);
            if (dist_fx(px, py, h->x, h->y) < FX(4)) {
                g->map.st[h->pad].aim = 0;
                h->active = 0;
                continue;
            }
        } else if (h->leaving) {
            want = (angle16)(fx_atan2(t->x - h->x, t->y - h->y) + 0x8000);
            if (d > FX(360) || h->age > 2000) { h->active = 0; continue; }
        } else if (d > FX(85)) {
            want = fx_atan2(t->x - h->x, t->y - h->y);
        } else {
            want = (angle16)(fx_atan2(t->x - h->x, t->y - h->y) + 0x4000);   /* circle */
        }
        diff = (int16_t)(want - h->angle);
        if (diff > 0x0300) diff = 0x0300;
        if (diff < -0x0300) diff = -0x0300;
        h->angle = (angle16)(h->angle + diff);
        h->x += fx_mul(FX_FRAC(22, 10), fx_sin(h->angle));
        h->y -= fx_mul(FX_FRAC(22, 10), fx_cos(h->angle));
        if (h->cooldown) h->cooldown--;
        else if (!h->leaving && d < FX(150)) {
            angle16 aim = fx_atan2(t->x - h->x, t->y - h->y);
            aim = (angle16)(aim + grange(g, -0x0300, 0x0300));
            fire(g, h->x, h->y, aim, FX_FRAC(45, 10), 40, SHOT_BULLET, 2, TEAM_ENEMY, 255, 0, h->target);
            h->cooldown = 55;
        }
    }
}

/* ---- mines and flags ---------------------------------------------------------------------- */

static void mines_update(Game *g)
{
    uint8_t people[MAX_MEN];
    int i, j, count = 0;
    /* Most slots are empty, especially on quiet maps. Scan them once,
     * then keep the original order for each mine. A previous mine can
     * still remove someone from the list by clearing their kind. */
    for (i = 0; i < MAX_MEN; i++)
        if (g->men[i].kind != MAN_NONE) people[count++] = (uint8_t)i;
    for (i = 0; i < MAX_MINES; i++) {
        Mine *mn = &g->mines[i];
        int boom = 0;
        if (!mn->active) continue;
        if (mn->arm) mn->arm--;
        /* proximity mines: anything driving over sets them off at once; only
         * the side that just laid one gets a moment to get clear of it */
        for (j = 0; j < g->num_tanks && !boom; j++) {
            const Tank *t = &g->tanks[j];
            boom = t->active && !t->respawn && !(mn->arm && t->team == mn->team) &&
                   fx_abs(t->x - mn->x) < FX(9) && fx_abs(t->y - mn->y) < FX(9);
        }
        for (j = 0; j < count && !boom && !mn->arm; j++) {
            const Man *m = &g->men[people[j]];
            boom = m->kind != MAN_NONE && fx_abs(m->x - mn->x) < FX(6) && fx_abs(m->y - mn->y) < FX(6);
        }
        if (!boom) continue;
        spawn_effect_ex(g, EFF_EXPLOSION, mn->x, mn->y, 0, 100, 0);
        for (j = 0; j < g->num_tanks; j++) {
            Tank *t = &g->tanks[j];
            if (!t->active || t->respawn) continue;
            if (dist_fx(t->x, t->y, mn->x, mn->y) < FX(20)) {
                if (t->armor > taken(g, t, 25)) t->armor = (uint8_t)(t->armor - taken(g, t, 25));
                else tank_destroyed(g, t);
            }
        }
        for (j = 0; j < MAX_MEN; j++)
            if (g->men[j].kind != MAN_NONE && dist_fx(g->men[j].x, g->men[j].y, mn->x, mn->y) < FX(20))
                splat(g, &g->men[j], -1);
        mn->active = 0;
    }
    for (i = 0; i < 2; i++) {                               /* carried flags ride along */
        Flag *f = &g->flags[i];
        if (f->state == FLAG_CARRIED && f->carrier < g->num_tanks) {
            f->x = g->tanks[f->carrier].x;
            f->y = g->tanks[f->carrier].y;
        }
    }
}

/* ---- extras: pickups and the hill ---------------------------------------------------- */
static int open_ground(const Game *g, int tx, int ty, int r)   /* r tiles round it: drivable, no buildings */
{
    int x, y;
    for (y = ty - r; y <= ty + r; y++) for (x = tx - r; x <= tx + r; x++) {
        if (x < 2 || y < 2 || x >= g->map.w - 2 || y >= g->map.h - 2) return 0;
        if ((tile_info[g->map.type[y][x]].flags & TF_SOLID) || g->map.sid[y][x]) return 0;
    }
    return 1;
}

static int near_a_base(const Game *g, int tx, int ty, int d)
{
    int b;
    for (b = 0; b < 2; b++)
        if (tx >= g->bases[b].x0 - d && tx <= g->bases[b].x1 + d && ty >= g->bases[b].y0 - d && ty <= g->bases[b].y1 + d) return 1;
    return 0;
}

static void place_pickup(Game *g, Pickup *p)        /* somewhere open, away from both camps */
{
    int tries;
    for (tries = 0; tries < 200; tries++) {
        int tx = grange(g, 6, g->map.w - 7);
        int ty = grange(g, 6, g->map.h - 7);
        if (!open_ground(g, tx, ty, 0) || near_a_base(g, tx, ty, 10)) continue;
        p->tx = (int16_t)tx; p->ty = (int16_t)ty; p->active = 1; p->back = 0;
        return;
    }
    p->active = 0; p->back = PICKUP_BACK;              /* (no room now: try again later) */
}

static void country_split(Game *g);
/* the country's buildings and guns: the nearer base's side (each half its own; the bases theirs). Conquest
 * plays it so; on a small map every mode does, as the fighting's among the buildings: all the fortress's,
 * their blasts would catch only its tanks */
void game_country_split(Game *g) { country_split(g); }
static void country_split(Game *g)
{
    int i;
    for (i = 0; i < g->map.num_st; i++) {
        Structure *st = &g->map.st[i];                   /* the nearer base's side (each half its own; the bases theirs) */
        int k, in = 0, d[2];
        if (st->team == TEAM_INDEPENDENT) continue;
        for (k = 0; k < 2; k++) {
            const Base *b = &g->bases[k];
            int dx = st->x - (b->x0 + b->x1) / 2, dy = st->y - (b->y0 + b->y1) / 2;
            if (st->x >= b->x0 && st->x <= b->x1 && st->y >= b->y0 && st->y <= b->y1) in = 1;
            d[k] = dx * dx + dy * dy;
        }
        if (!in) st->team = (uint8_t)(d[TEAM_PLAYER] <= d[TEAM_ENEMY] ? TEAM_PLAYER : TEAM_ENEMY);
    }
}

static void conquest_setup(Game *g);
static void near_open(const Game *g, int x, int y, int16_t *ox, int16_t *oy);
static void say(Tank *t, int msg);

void game_make_arena(Game *g)
{
    int k, x, y;
    for (k = 0; k < 2; k++) {                            /* the camps, their walls and roads out: cleared */
        const Base *b = &g->bases[k];
        for (y = b->y0 - 2; y <= b->y1 + 7; y++)
            for (x = b->x0 - 2; x <= b->x1 + 2; x++)
                if (x >= 0 && y >= 0 && x < g->map.w && y < g->map.h) { map_set(&g->map, x, y, TILE_SAND); g->map.sid[y][x] = 0; }
    }
    map_prune(&g->map);
    map_no_camps(g->bases);
    g->map.arena = 1;
    g->map.num_spawns = 8;
    for (k = 0; k < 8; k++) {                            /* eight round the middle, on open ground */
        /* (turned an eighth: none on the diagonal between the halves, four each side of it) */
        static const int8_t ox[8] = { 16, 39, 39, 16, -16, -39, -39, -16 }, oy[8] = { -39, -16, 16, 39, 39, 16, -16, -39 };
        near_open(g, g->map.w / 2 + ox[k] * g->map.w / 128, g->map.h / 2 + oy[k] * g->map.h / 128,   /* (the ring: the map's size) */
                  &g->map.spawn_tx[k], &g->map.spawn_ty[k]);
    }
}

/* the extras that need the match's tanks (match_start calls it once they're in) */
void game_extras_tanks(Game *g)
{
    int i;
    uint8_t extras = g->extras;
    if (extras & EXTRA_WARLORD) {                     /* a Warlord a side, picked by the match's own dice */
        int side, k, n, pick;
        for (side = 0; side < 2; side++) {
            for (k = n = 0; k < g->num_tanks; k++) n += (g->tanks[k].team & 1) == side;
            if (!n) continue;
            pick = (int)((g->rng >> (8 + side * 5)) % (uint32_t)n);
            for (k = 0; k < g->num_tanks; k++)
                if ((g->tanks[k].team & 1) == side && pick-- == 0) { g->jug[side] = (int8_t)k; jug_boost(g, &g->tanks[k]); break; }
        }
    }
    if (extras & EXTRA_ANNIHILATION)                     /* (every tank for itself: a moment to get out first) */
        for (i = 0; i < g->num_tanks; i++) g->tanks[i].guard = FFA_GUARD;
}

/* hardpoint: the active point moves on every HP_MOVE; a side alone on it scores one a second */
static void hardpoint_update(Game *g)
{
    int i, on[2] = { 0, 0 };
    fx px, py;
    if (!g->num_cp) return;
    g->hp_active = (uint8_t)((g->tick / HP_MOVE) % g->num_cp);
    if (g->tick % TICK_RATE) return;
    px = FX(g->cp_tx[g->hp_active] * TILE_SIZE + TILE_SIZE / 2); py = FX(g->cp_ty[g->hp_active] * TILE_SIZE + TILE_SIZE / 2);
    for (i = 0; i < g->num_tanks; i++) {
        const Tank *t = &g->tanks[i];
        if (t->active && !t->respawn && dist_fx(t->x, t->y, px, py) < FX(CP_RADIUS * TILE_SIZE)) on[t->team & 1] = 1;
    }
    if (on[0] != on[1]) g->hp_score[on[0] ? 0 : 1]++;
}

/* kill confirmed: a tag a foe drives over is a kill confirmed; its own side's, denied; old ones go */
static void tags_update(Game *g)
{
    int k, i;
    for (k = 0; k < MAX_TAGS; k++) {
        if (!g->tags[k].active) continue;
        if (!--g->tags[k].ttl) { g->tags[k].active = 0; continue; }
        for (i = 0; i < g->num_tanks; i++) {
            Tank *t = &g->tanks[i];
            if (!t->active || t->respawn || dist_fx(t->x, t->y, g->tags[k].x, g->tags[k].y) >= FX(10)) continue;
            if ((t->team & 1) != g->tags[k].team) { g->kc_score[t->team & 1]++; t->score += 50; }   /* confirmed */
            else t->score += 25;                                                                  /* denied */
            g->tags[k].active = 0;
            break;
        }
    }
}

void game_setup_extras(Game *g, uint8_t extras)
{
    int i;
    if (g->no_powerups) extras &= (uint8_t)~EXTRA_PICKUPS;
    g->extras = extras;
    memset(g->pickups, 0, sizeof g->pickups);
    g->hill_holder = -1;
    g->ffa_winner = -1;
    g->jug[0] = g->jug[1] = -1;
    g->jug_score[0] = g->jug_score[1] = 0;

    memset(g->tags, 0, sizeof g->tags);
    g->kc_score[0] = g->kc_score[1] = g->hp_score[0] = g->hp_score[1] = 0;
    g->hp_active = 0;
    if (extras & (EXTRA_CONQUEST | EXTRA_HARDPOINT)) conquest_setup(g);   /* (hardpoint: conquest's points, one at a time) */
    if (extras & EXTRA_PICKUPS)
        for (i = 0; i < MAX_PICKUPS; i++) { g->pickups[i].kind = (uint8_t)(i % (g->enhanced_rules ? PICKUP_KINDS : 3)); place_pickup(g, &g->pickups[i]); }
    if (extras & EXTRA_HILL) {                           /* the hill: open ground, as near the middle as there is */
        int r, found = 0;
        g->hill_tx = g->map.w / 2; g->hill_ty = g->map.h / 2;
        for (r = 0; r < g->map.w / 2 && !found; r++) {
            int x, y;
            for (y = g->map.h / 2 - r; y <= g->map.h / 2 + r && !found; y++)
                for (x = g->map.w / 2 - r; x <= g->map.w / 2 + r && !found; x++)
                    if ((y == g->map.h / 2 - r || y == g->map.h / 2 + r || x == g->map.w / 2 - r || x == g->map.w / 2 + r) &&
                        open_ground(g, x, y, 2)) { g->hill_tx = (int16_t)x; g->hill_ty = (int16_t)y; found = 1; }
        }
    }
}

/* open ground near (x, y), searching outward; where it found it */
static void near_open(const Game *g, int x, int y, int16_t *ox, int16_t *oy)
{
    int r, a, b;
    for (r = 0; r < 24; r++)
        for (b = y - r; b <= y + r; b++)
            for (a = x - r; a <= x + r; a++)
                if ((b == y - r || b == y + r || a == x - r || a == x + r) && open_ground(g, a, b, 2)) { *ox = (int16_t)a; *oy = (int16_t)b; return; }
    *ox = (int16_t)x; *oy = (int16_t)y;
}

int game_checkpoint_positions(const Game *g, int16_t xs[MAX_CP], int16_t ys[MAX_CP])
{
    int i;
    if (g->map.num_points) {
        int n = g->map.num_points > MAX_CP ? MAX_CP : g->map.num_points;
        for (i = 0; i < n; i++) { xs[i] = g->map.point_tx[i]; ys[i] = g->map.point_ty[i]; }
        return n;
    }
    {
        int mx = g->map.w / 2, my = g->map.h / 2;
        near_open(g, mx, my, &xs[0], &ys[0]);
        for (i = 0; i < 2; i++) {
            const Base *b = &g->bases[i];
            near_open(g, ((b->x0 + b->x1) / 2 + mx) / 2, ((b->y0 + b->y1) / 2 + my) / 2, &xs[i + 1], &ys[i + 1]);
        }
    }
    return 3;
}

static void conquest_setup(Game *g)
{
    int i;
    g->num_cp = (uint8_t)game_checkpoint_positions(g, g->cp_tx, g->cp_ty);
    for (i = 0; i < g->num_cp; i++) { g->cp_owner[i] = -1; g->cp_take[i] = 0; }
    g->cp_score[0] = g->cp_score[1] = 0;
    country_split(g);

}

static void conquest_update(Game *g)
{
    int i, k;
    for (i = 0; i < g->num_cp; i++) {
        int on[2] = { 0, 0 };
        fx cx = FX(g->cp_tx[i] * TILE_SIZE + TILE_SIZE / 2), cy = FX(g->cp_ty[i] * TILE_SIZE + TILE_SIZE / 2);
        for (k = 0; k < g->num_tanks; k++) {
            const Tank *t = &g->tanks[k];
            if (t->active && !t->respawn && dist_fx(t->x, t->y, cx, cy) < FX(CP_RADIUS * TILE_SIZE)) on[t->team & 1] = 1;
        }
        if (on[0] != on[1]) {                            /* one side alone on it: towards them */
            int step = on[0] ? 1 : -1;
            g->cp_take[i] = (int16_t)(g->cp_take[i] + step);
            if (g->cp_take[i] > CP_TAKE) g->cp_take[i] = CP_TAKE;
            if (g->cp_take[i] < -CP_TAKE) g->cp_take[i] = -CP_TAKE;
            if (g->cp_take[i] == CP_TAKE && g->cp_owner[i] != TEAM_PLAYER) { g->cp_owner[i] = TEAM_PLAYER; spawn_effect(g, EFF_CHEER, cx, cy); }
            if (g->cp_take[i] == -CP_TAKE && g->cp_owner[i] != TEAM_ENEMY) { g->cp_owner[i] = TEAM_ENEMY; spawn_effect(g, EFF_CHEER, cx, cy); }
            if (g->cp_owner[i] == TEAM_PLAYER && g->cp_take[i] <= 0) g->cp_owner[i] = -1;   /* (taken back to nobody's) */
            if (g->cp_owner[i] == TEAM_ENEMY && g->cp_take[i] >= 0) g->cp_owner[i] = -1;
        }
    }
    if (g->tick % TICK_RATE == 0 && g->winner < 0) {     /* a second: a point for each point held */
        for (i = 0; i < g->num_cp; i++) if (g->cp_owner[i] >= 0) g->cp_score[g->cp_owner[i] & 1]++;
        for (k = 0; k < 2; k++) if (g->cp_score[k] >= CONQUEST_WIN && g->winner < 0) g->winner = (int8_t)k;
    }
}

/* arenas: a supply drop every so often, somewhere open (everyone's told); the first over it gets a full refill */
static void drop_update(Game *g)
{
    int k;
    Pickup *d = &g->drop;
    if (!d->active) {
        if (++g->drop_wait < DROP_EVERY) return;
        g->drop_wait = 0;
        {   /* (two draws, one at a time: as two arguments of one call, C leaves their order to the compiler,
             *  and gcc and clang differ; so y first, then x, as gcc did: the same place on every machine) */
            int ry = (int)(grand(g) % (uint32_t)(g->map.h - 40));
            int rx = (int)(grand(g) % (uint32_t)(g->map.w - 40));
            near_open(g, 20 + rx, 20 + ry, &d->tx, &d->ty);
        }
        d->active = 1;
        for (k = 0; k < g->num_tanks; k++) if (g->tanks[k].active && !g->tanks[k].respawn) say(&g->tanks[k], MSG_DROP);
        return;
    }
    for (k = 0; k < g->num_tanks; k++) {
        Tank *t = &g->tanks[k];
        fx dx = t->x - FX(d->tx * TILE_SIZE + TILE_SIZE / 2), dy = t->y - FX(d->ty * TILE_SIZE + TILE_SIZE / 2);
        if (!t->active || t->respawn || dx > FX(12) || dx < -FX(12) || dy > FX(12) || dy < -FX(12)) continue;
        t->armor = armor_max(g, t);                      /* everything: armour, fuel, mines */
        t->fuel = (uint16_t)(world_tank_stats(g->vehicle_world, (TankClass)t->cls)->fuel * TICK_RATE);
        if (!is_jug(g, t)) t->mines = (uint8_t)(t->mines + 5 > 99 ? 99 : t->mines + 5);
        spawn_effect(g, EFF_CHEER, t->x, t->y);
        d->active = 0;
        break;
    }
}

static void extras_update(Game *g)
{
    int i, k;
    if (g->map.arena && !g->no_powerups) drop_update(g);                    /* (arenas only: never the camp maps) */
    if (g->extras & EXTRA_CONQUEST) conquest_update(g);
    if (g->extras & EXTRA_HARDPOINT) hardpoint_update(g);
    if (g->extras & EXTRA_KILLCONF) tags_update(g);
    if (g->extras & EXTRA_PICKUPS)
        for (i = 0; i < MAX_PICKUPS; i++) {
            Pickup *p = &g->pickups[i];
            if (!p->active) { if (p->back && --p->back == 0) place_pickup(g, p); continue; }
            for (k = 0; k < g->num_tanks; k++) {
                Tank *t = &g->tanks[k];
                const TankStats *ts = world_tank_stats(g->vehicle_world, (TankClass)t->cls);
                fx dx = t->x - FX(p->tx * TILE_SIZE + TILE_SIZE / 2), dy = t->y - FX(p->ty * TILE_SIZE + TILE_SIZE / 2);
                if (!t->active || t->respawn || dx > FX(12) || dx < -FX(12) || dy > FX(12) || dy < -FX(12)) continue;
                if (p->kind == PICKUP_REPAIR) t->armor = armor_max(g, t);
                else if (p->kind == PICKUP_MINES) { if (!is_jug(g, t)) t->mines = (uint8_t)(t->mines + (g->vehicle_world == BIOME_VOLCANIC ? 5 : 3) > 99 ? 99 : t->mines + (g->vehicle_world == BIOME_VOLCANIC ? 5 : 3)); }
                else if (p->kind == PICKUP_BOOST && g->enhanced_rules) t->boost_ticks = POWERUP_TICKS;
                else if (p->kind == PICKUP_RAPID && g->enhanced_rules) t->rapid_ticks = POWERUP_TICKS;
                else {
                    t->fuel = (uint16_t)(ts->fuel * TICK_RATE);
                    if (g->vehicle_world == BIOME_MOON && t->armor < armor_max(g, t)) {
                        int armor = t->armor + 10;
                        t->armor = (uint8_t)(armor > armor_max(g, t) ? armor_max(g, t) : armor);
                    }
                }
                spawn_effect(g, EFF_CHEER, t->x, t->y);
                p->active = 0; p->back = PICKUP_BACK;
                break;
            }
        }
    if (g->extras & EXTRA_HILL) {                        /* who's on the hill, alone? */
        int on[2] = { 0, 0 };
        fx cx = FX(g->hill_tx * TILE_SIZE + TILE_SIZE / 2), cy = FX(g->hill_ty * TILE_SIZE + TILE_SIZE / 2);
        for (k = 0; k < g->num_tanks; k++) {
            const Tank *t = &g->tanks[k];
            fx dx = t->x - cx, dy = t->y - cy;
            if (!t->active || t->respawn) continue;
            if (dx < FX(HILL_R * TILE_SIZE) && dx > -FX(HILL_R * TILE_SIZE) && dy < FX(HILL_R * TILE_SIZE) && dy > -FX(HILL_R * TILE_SIZE))
                on[t->team & 1] = 1;
        }
        g->hill_holder = (int8_t)(on[0] && !on[1] ? 0 : on[1] && !on[0] ? 1 : -1);
        if (g->hill_holder >= 0 && g->winner < 0 && ++g->hill[g->hill_holder] >= HILL_WIN) g->winner = g->hill_holder;
    }
}

/* the higher score wins; level: more tanks destroyed, then more men rescued; still level: -1 */
static int on_points(const Game *g)
{
    if ((g->extras & EXTRA_WARLORD) && g->jug_score[0] != g->jug_score[1])   /* (warlord: the score first) */
        return g->jug_score[0] > g->jug_score[1] ? TEAM_PLAYER : TEAM_ENEMY;
    if (g->extras & EXTRA_TDM) {                         /* (team deathmatch: the kills first) */
        int k2, kl[2] = { 0, 0 };
        for (k2 = 0; k2 < g->num_tanks; k2++) kl[g->tanks[k2].team & 1] += g->tanks[k2].tank_kills;
        if (kl[0] != kl[1]) return kl[0] > kl[1] ? TEAM_PLAYER : TEAM_ENEMY;
    }
    if ((g->extras & EXTRA_CONQUEST) && g->cp_score[0] != g->cp_score[1])   /* (conquest: the score first) */
        return g->cp_score[0] > g->cp_score[1] ? TEAM_PLAYER : TEAM_ENEMY;
    if ((g->extras & EXTRA_KILLCONF) && g->kc_score[0] != g->kc_score[1])
        return g->kc_score[0] > g->kc_score[1] ? TEAM_PLAYER : TEAM_ENEMY;
    if ((g->extras & EXTRA_HARDPOINT) && g->hp_score[0] != g->hp_score[1])
        return g->hp_score[0] > g->hp_score[1] ? TEAM_PLAYER : TEAM_ENEMY;
    int32_t pts[2] = { 0, 0 }, kills[2] = { 0, 0 }, saved[2] = { 0, 0 };
    int i;
    for (i = 0; i < g->num_tanks; i++) {
        int tm = g->tanks[i].team & 1;
        pts[tm] += (int32_t)g->tanks[i].score;
        kills[tm] += g->tanks[i].tank_kills;
        saved[tm] += g->tanks[i].saved;
    }
    if (pts[TEAM_PLAYER] != pts[TEAM_ENEMY]) return pts[TEAM_PLAYER] > pts[TEAM_ENEMY] ? TEAM_PLAYER : TEAM_ENEMY;
    if (kills[TEAM_PLAYER] != kills[TEAM_ENEMY]) return kills[TEAM_PLAYER] > kills[TEAM_ENEMY] ? TEAM_PLAYER : TEAM_ENEMY;
    if (saved[TEAM_PLAYER] != saved[TEAM_ENEMY]) return saved[TEAM_PLAYER] > saved[TEAM_ENEMY] ? TEAM_PLAYER : TEAM_ENEMY;
    return -1;
}

/* changing weather: in each 7 minutes, a storm comes in after 1.5 to 3.5 and lasts 1 to 2.5 (each time its own) */
static void weather_update(Game *g)
{
    uint32_t s = g->tick / TICK_RATE, cycle = s / 420, in = s % 420, h;
    h = (g->weather_seed ^ (cycle * 2654435761u)) * 2246822519u;
    h ^= h >> 15;
    {
        uint32_t start = 90 + h % 120, len = 60 + (h >> 8) % 90;
        g->weather = (uint8_t)(in >= start && in < start + len ? WEATHER_STORM : WEATHER_CLEAR);
    }
}

static void step_game(Game *g, const InputBits inputs[MAX_TANKS], InputBits *resolved)
{
    PropIndex props;
    int i;
    index_props(g, &props);
    if (g->weather_changing) weather_update(g);
    for (i = 0; i < g->num_tanks; i++) {
        Tank *t = &g->tanks[i];
        t->prev_x = t->x;
        t->prev_y = t->y;
        t->prev_angle = t->angle;
        InputBits in = inputs[i];
        if (in & IN_TAKEOVER) t->ai = 0;
        if (!t->active) continue;
        if (t->ai && (!g->bot_commands || resolved)) {
            in = ai_think(g, i);
            if (resolved) resolved[i] = in;
        }
        tank_update(g, &props, i, (InputBits)(in & ~IN_TAKEOVER));
    }
    turrets_update(g);
    for (i = 0; i < g->map.num_st; i++) {                   /* embers die down */
        Structure *st = &g->map.st[i];
        if (st->smoulder && (uint16_t)(g->tick - st->aim) > EMBER_TICKS) st->smoulder = 0;
    }
    men_update(g);
    evacuees_update(g);
    patrols_update(g, &props);
    if (!g->map.quiet) helis_update(g);
    mines_update(g);
    shots_update(g, &props);
    if (g->extras) extras_update(g);
    for (i = 0; i < MAX_EFFECTS; i++) {
        Effect *e = &g->effects[i];
        if (!e->active) continue;
        if (e->delay) { e->delay--; continue; }
        if (--e->ttl == 0) e->active = 0;
    }
    g->tick++;
    if (g->adapt && g->tick % (30 * TICK_RATE) == 0) {   /* adaptive: every 30 s, how's it going? */
        int lost[2] = { 0, 0 }, d;
        for (i = 0; i < g->num_tanks; i++) lost[g->tanks[i].team & 1] += g->tanks[i].lost;
        d = (lost[TEAM_ENEMY] - g->adapt_mark[TEAM_ENEMY]) - (lost[TEAM_PLAYER] - g->adapt_mark[TEAM_PLAYER]);
        if (d >= 1) g->adapt_skill = (int16_t)(g->adapt_skill + 20 * d);          /* you're winning: harder */
        else if (d <= -1) g->adapt_skill = (int16_t)(g->adapt_skill + 25 * d);    /* you're losing: easier */
        if (g->adapt_skill < 0) g->adapt_skill = 0;
        if (g->adapt_skill > 200) g->adapt_skill = 200;
        g->adapt_mark[0] = (int16_t)lost[0];
        g->adapt_mark[1] = (int16_t)lost[1];
    }
    if (g->time_limit && g->tick >= g->time_limit && g->winner < 0 && !g->time_up) {   /* the time is up */
        g->time_up = 1;
        if (g->time_rule == TIME_RULE_POINTS) g->winner = (int8_t)on_points(g);
    }
    if ((g->extras & EXTRA_ANNIHILATION) && g->winner < 0 && !g->time_up) {   /* every tank for itself: the last one */
        int left = 0, last = -1;
        for (i = 0; i < g->num_tanks; i++) if (!g->tanks[i].out) { left++; last = i; }
        if (left <= 1) {
            if (left == 1) { g->ffa_winner = (int8_t)last; g->winner = (int8_t)(g->tanks[last].team & 1); }
            else g->time_up = 1;                         /* (the last ones gone at once: a draw) */
        }
    }
    if ((g->extras & EXTRA_TDM) && g->winner < 0) {      /* team deathmatch: TDM_WIN enemy tanks */
        int kills[2] = { 0, 0 };
        for (i = 0; i < g->num_tanks; i++) kills[g->tanks[i].team & 1] += g->tanks[i].tank_kills;
        if (kills[0] >= TDM_WIN || kills[1] >= TDM_WIN) {   /* (both at once: the more kills; level: a draw) */
            if (kills[0] != kills[1]) g->winner = (int8_t)(kills[0] > kills[1] ? TEAM_PLAYER : TEAM_ENEMY);
            else g->time_up = 1;
        }
    }
    if ((g->extras & EXTRA_KILLCONF) && g->winner < 0 && (g->kc_score[0] >= KC_WIN || g->kc_score[1] >= KC_WIN))
        g->winner = (int8_t)(g->kc_score[0] >= KC_WIN ? TEAM_PLAYER : TEAM_ENEMY);   /* (one a tick: never both at once) */
    if ((g->extras & EXTRA_HARDPOINT) && g->winner < 0 && (g->hp_score[0] >= HP_WIN || g->hp_score[1] >= HP_WIN))
        g->winner = (int8_t)(g->hp_score[0] >= HP_WIN ? TEAM_PLAYER : TEAM_ENEMY);
    if (g->winner < 0 && !g->time_up && !(g->extras & EXTRA_ANNIHILATION)) {   /* a side all out (no lives left): the other's won */
        int has[2] = { 0, 0 }, in[2] = { 0, 0 };
        for (i = 0; i < g->num_tanks; i++) {
            const Tank *t = &g->tanks[i];
            has[t->team & 1] = 1;
            if (!t->out) in[t->team & 1] = 1;
        }
        if (has[0] && has[1] && in[0] != in[1]) g->winner = (int8_t)(in[0] ? TEAM_PLAYER : TEAM_ENEMY);
    }
    if (g->winner < 0 && !g->time_up && !g->humans_out) {   /* every person out, computer players left: no point going on */
        int people = 0, people_in = 0, side_in[2] = { 0, 0 };
        for (i = 0; i < g->num_tanks; i++) {
            const Tank *t = &g->tanks[i];
            if (!t->ai) { people++; if (!t->out) people_in++; }
            if (!t->out) side_in[t->team & 1] = 1;
        }
        if (people && !people_in && side_in[0] && side_in[1]) {   /* (a side all out ends it anyway) */
            g->humans_out = 1;
            g->winner = (int8_t)(g->versus ? on_points(g) : TEAM_ENEMY);   /* together: the players lost */
            if (g->winner < 0) g->time_up = 1;          /* (level on points: a draw, as when the time runs out) */
        }
    }
}

void game_tick(Game *g, const InputBits inputs[MAX_TANKS])
{
    step_game(g, inputs, NULL);
}

void game_tick_server(Game *g, InputBits inputs[MAX_TANKS])
{
    step_game(g, inputs, inputs);
}

static uint32_t fnv(uint32_t h, const void *p, unsigned n)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--) { h ^= *b++; h *= 16777619u; }
    return h;
}

/* hash integers byte by byte in a fixed order so big-endian (68k) and
 * little-endian (x86/ARM) machines produce the same checksum */
static uint32_t fnv_u32(uint32_t h, uint32_t v)
{
    uint8_t b[4];
    b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24);
    return fnv(h, b, 4);
}

uint32_t game_checksum(const Game *g)
{
    uint32_t h = 2166136261u;
    int i;
    if (g->enhanced_rules) h = fnv_u32(h, 0x53555246u | g->no_powerups);
    if (g->life_limit) h = fnv_u32(h, 0x4c490000u | (uint32_t)g->life_limit << 8 | g->vehicle_world);
    if (g->relaxed_pace) h = fnv_u32(h, 0x50414345u);
    if (g->bot_commands) h = fnv_u32(h, 0x424f5453u);
    {   /* the map's own area only (not the grid's rock past it): the same on any grid, and less to read */
        int y, w = g->map.w < MAP_W ? g->map.w : MAP_W;
        for (y = 0; y < g->map.h && y < MAP_H; y++) {
            h = fnv(h, g->map.type[y], (size_t)w * sizeof g->map.type[0][0]);
            h = fnv(h, g->map.hp[y], (size_t)w * sizeof g->map.hp[0][0]);
        }
    }
    for (i = 0; i < g->map.num_st; i++) {
        const Structure *st = &g->map.st[i];
        h = fnv_u32(h, (uint32_t)st->type << 24 | (uint32_t)st->hp << 16 | (uint32_t)st->x << 8 | st->y);
        h = fnv_u32(h, (uint32_t)st->aim << 8 | st->cooldown);
    }
    for (i = 0; i < g->num_tanks; i++) {
        const Tank *t = &g->tanks[i];
        if (g->enhanced_rules) {
            h = fnv_u32(fnv_u32(h, (uint32_t)t->drift_x), (uint32_t)t->drift_y);
            h = fnv_u32(h, (uint32_t)t->boost_ticks << 16 | t->rapid_ticks);
        }
        if (t->rocket_launcher) h = fnv_u32(h, 0x524f434bu);
        if (g->bot_commands) h = fnv_u32(h, t->ai);
        h = fnv_u32(h, (uint32_t)t->x);
        h = fnv_u32(h, (uint32_t)t->y);
        h = fnv_u32(h, t->angle);
        h = fnv_u32(h, (uint32_t)t->speed);
        h = fnv_u32(h, t->fuel);
        h = fnv_u32(h, t->score);
        h = fnv_u32(h, (uint32_t)t->armor << 16 | (uint32_t)t->lives << 8 | t->respawn);
    }
    for (i = 0; i < MAX_MEN; i++)
        if (g->men[i].kind) {
            h = fnv_u32(h, (uint32_t)g->men[i].x);
            h = fnv_u32(h, (uint32_t)g->men[i].y);
        }
    for (i = 0; i < MAX_HELIS; i++)
        if (g->helis[i].active) {
            h = fnv_u32(h, (uint32_t)g->helis[i].x);
            h = fnv_u32(h, (uint32_t)g->helis[i].y);
        }
    for (i = 0; i < 2; i++)
        h = fnv_u32(h, (uint32_t)g->flags[i].state << 24 ^ (uint32_t)g->flags[i].x);
    for (i = 0; i < g->num_props; i++) {
        h = fnv_u32(h, (uint32_t)g->props[i].hp << 8 | g->props[i].alive);
        if (g->props[i].moving) { h = fnv_u32(h, (uint32_t)g->props[i].x); h = fnv_u32(h, (uint32_t)g->props[i].y); }   /* (patrols: where) */
    }
    h = fnv_u32(h, (uint32_t)(uint8_t)g->winner);
    h = fnv_u32(h, g->time_up);
    if (g->humans_out) h = fnv_u32(h, 0x4855u);
    if (g->ffa_winner >= 0) h = fnv_u32(h, 0x4646u + (uint32_t)g->ffa_winner);
    if (g->map.arena) h = fnv_u32(fnv_u32(h, (uint32_t)g->drop.active << 16 | g->drop_wait), (uint32_t)g->drop.tx << 8 ^ (uint32_t)g->drop.ty);
    if (g->extras & EXTRA_WARLORD)                    /* (warlord: who, and the score) */
        h = fnv_u32(fnv_u32(h, (uint32_t)((g->jug[0] + 1) | (g->jug[1] + 1) << 8)), (uint32_t)(g->jug_score[0] | g->jug_score[1] << 8));
    if (g->extras & EXTRA_CONQUEST) {                    /* (conquest: who holds what, the score) */
        int ci;
        for (ci = 0; ci < g->num_cp; ci++) h = fnv_u32(fnv_u32(h, (uint32_t)(g->cp_owner[ci] + 1)), (uint32_t)(g->cp_take[ci] + CP_TAKE));
        h = fnv_u32(fnv_u32(h, g->cp_score[0]), g->cp_score[1]);
    }
    if (g->extras & EXTRA_KILLCONF) {                    /* (kill confirmed: the tags, the score) */
        int k;
        for (k = 0; k < MAX_TAGS; k++) if (g->tags[k].active) h = fnv_u32(fnv_u32(h, (uint32_t)g->tags[k].x), (uint32_t)g->tags[k].y ^ g->tags[k].team);
        h = fnv_u32(h, (uint32_t)g->kc_score[0] | (uint32_t)g->kc_score[1] << 16);
    }
    if (g->extras & EXTRA_HARDPOINT) h = fnv_u32(fnv_u32(h, (uint32_t)g->hp_score[0] | (uint32_t)g->hp_score[1] << 16), g->hp_active);
    if (g->adapt) h = fnv_u32(h, (uint32_t)(uint16_t)g->adapt_skill);
    if (g->extras) {
        int k;
        for (k = 0; k < MAX_PICKUPS; k++) {
            h = fnv_u32(h, (uint32_t)g->pickups[k].tx << 16 ^ (uint32_t)g->pickups[k].ty << 2 ^ g->pickups[k].active);
            if (g->enhanced_rules) h = fnv_u32(h, g->pickups[k].kind);
        }
        h = fnv_u32(h, g->hill[0]); h = fnv_u32(h, g->hill[1]);
    }
    h = fnv_u32(h, g->rng);
    h = fnv_u32(h, g->biome);
    return fnv_u32(h, g->tick);
}

void game_camera(const Game *g, int tank, int view_w, int view_h, int alpha256,
                 int *cam_x, int *cam_y)
{
    const Tank *t = &g->tanks[tank];
    int x = fx_to_int(fx_lerp(t->prev_x, t->x, alpha256)) - view_w / 2;
    int y = fx_to_int(fx_lerp(t->prev_y, t->y, alpha256)) - view_h / 2;
    if (x > (g->map.w * TILE_SIZE) - view_w) x = (g->map.w * TILE_SIZE) - view_w;
    if (y > (g->map.h * TILE_SIZE) - view_h) y = (g->map.h * TILE_SIZE) - view_h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    *cam_x = x;
    *cam_y = y;
}

/* ---- for ai.c ---------------------------------------------------------------------- */

void game_set_ai(Game *g, int tank, int level)
{
    if (tank < 0 || tank >= g->num_tanks) return;
    g->tanks[tank].ai = (uint8_t)level;
    g->tanks[tank].steer = STEER_TANK;
    memset(&g->ai[tank], 0, sizeof g->ai[tank]);
}

int gi_range(Game *g, int lo, int hi)
{
    return random_range(g->bot_commands ? &g->bot_rng : &g->rng, lo, hi);
}
fx  gi_dist(fx ax, fx ay, fx bx, fx by) { return dist_fx(ax, ay, bx, by); }
int gi_in_base(const Game *g, const Tank *t, int team) { return in_base(g, t, team); }
int gi_clear_shot(const Game *g, fx ax, fx ay, fx bx, fx by, int own_sid) { return clear_shot(g, ax, ay, bx, by, own_sid); }
void gi_struct_centre(const Map *m, int idx, fx *x, fx *y) { struct_centre(m, idx, x, y); }
