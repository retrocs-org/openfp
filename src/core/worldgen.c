/* SPDX-License-Identifier: MIT
 * Procedural maps: lakes, rivers, roads, rocks, tree clumps and walled
 * compounds, mixed per biome. Real maps will come from map files / the editor. */
#include <string.h>
#include "core/world.h"

typedef struct {
    const char *name;
    int lakes, lake_min, lake_max;
    int rivers;
    int roads;          /* per direction */
    int rock_clusters;
    int clumps, clump_parts;
    int compounds, bunkers;
} BiomeParams;

static const BiomeParams biomes[BIOME_COUNT] = {
    /*            name       lakes min max riv roads rocks clumps parts comp bunk */
    [BIOME_CLASSIC] = { "classic", 1, 3, 5, 0, 3,  6,   70, 3, 13, 30 },
    [BIOME_DESERT]  = { "desert",  1, 2, 4, 0, 2, 45,   18, 1, 10, 22 },
    [BIOME_JUNGLE]  = { "jungle",  3, 3, 7, 2, 2, 10,  170, 3,  9, 18 },
    [BIOME_WINTER]  = { "winter",  5, 4, 8, 0, 3, 22,   80, 2, 12, 26 },
    [BIOME_SCIFI]   = { "scifi",   2, 2, 5, 1, 4, 30,   45, 2, 14, 30 },   /* plasma pools, crystals */
    [BIOME_VOLCANIC]  = { "volcanic",  1, 3, 6, 3, 2, 40,  20, 1, 10, 26 },   /* lava rivers, basalt, little grows */
    [BIOME_WASTELAND] = { "wasteland", 2, 2, 4, 0, 3, 28,  25, 1, 18, 34 },   /* toxic pools, a ruined town to fight through */
    [BIOME_MOON]      = { "moon",      6, 2, 4, 0, 2, 48,  14, 1, 11, 24 },   /* craters (frozen), dust, crystals */
    [BIOME_SWAMP]     = { "swamp",     7, 2, 4, 2, 2, 8,  150, 3,  8, 16 },   /* pools everywhere, mud, dense growth */
};

static const BiomeParams *P;

Biome biome_from_name(const char *name)
{
    int i;
    for (i = 0; i < BIOME_COUNT; i++)
        if (name && !strcmp(name, biomes[i].name))
            return (Biome)i;
    return BIOME_CLASSIC;
}

const char *biome_name(Biome b) { return biomes[b < BIOME_COUNT ? b : 0].name; }

static const char *const time_names[TIME_COUNT] = { "day", "dusk", "night" };
const char *time_name(int t) { return time_names[t >= 0 && t < TIME_COUNT ? t : 0]; }
int time_from_name(const char *name)
{
    int i;
    for (i = 0; i < TIME_COUNT; i++)
        if (name && !strncmp(name, time_names[i], strlen(time_names[i]))) return i;
    return TIME_DAY;
}

static const char *const weather_names[WEATHER_COUNT] = {
    "clear", "storm", "changing", "rain", "snow", "fog", "sandstorm", "cloudy", "eruption", "meteor", "snowstorm"
};

const uint8_t weather_cloud_span[WEATHER_CLOUD_ROWS] = { 9, 14, 17, 20, 24, 26, 27, 27, 25, 22, 19, 14, 8 };

const char *weather_name(int weather)
{
    return weather_names[weather >= 0 && weather < WEATHER_COUNT ? weather : WEATHER_CLEAR];
}

const char *weather_label(int weather)
{
    static const char *const names[WEATHER_COUNT] = {
        "CLEAR", "THUNDERSTORM", "CHANGING", "RAIN", "SNOW", "FOG", "SANDSTORM", "CLOUDY", "ERUPTION", "METEOR SHOWER", "SNOWSTORM"
    };
    return names[weather >= 0 && weather < WEATHER_COUNT ? weather : WEATHER_CLEAR];
}

int weather_from_name(const char *name)
{
    size_t len = strcspn(name, " \t\r\n#");
    int weather;
    if (len == 12 && !strncmp(name, "thunderstorm", len)) return WEATHER_STORM;
    for (weather = 0; weather < WEATHER_COUNT; weather++)
        if (len == strlen(weather_names[weather]) && !strncmp(name, weather_names[weather], len))
            return weather;
    return WEATHER_CLEAR;
}

int weather_allowed(Biome biome, int weather)
{
    if (weather == WEATHER_CLEAR) return 1;
    if (biome == BIOME_MOON) return weather == WEATHER_METEOR;
    if (biome == BIOME_SCIFI) return 0;
    switch (weather) {
    case WEATHER_SNOW: case WEATHER_SNOWSTORM: return biome == BIOME_WINTER;
    case WEATHER_SANDSTORM: return biome == BIOME_DESERT || biome == BIOME_WASTELAND;
    case WEATHER_ERUPTION: return biome == BIOME_VOLCANIC;
    case WEATHER_STORM: case WEATHER_RAIN: return biome != BIOME_VOLCANIC && biome != BIOME_WINTER;
    case WEATHER_CHANGING: return biome != BIOME_VOLCANIC;
    case WEATHER_FOG: case WEATHER_CLOUDY: return 1;
    default: return 0;
    }
}

int weather_step(Biome biome, int weather, int direction, int allow_auto)
{
    int count = WEATHER_COUNT + !!allow_auto;
    if (weather < 0 || weather >= count) weather = WEATHER_CLEAR;
    do weather = (weather + (direction < 0 ? -1 : 1) + count) % count;
    while (weather != WEATHER_COUNT && !weather_allowed(biome, weather));
    return weather;
}

int weather_visual(Biome biome, int weather)
{
    return biome == BIOME_WINTER && (weather == WEATHER_STORM || weather == WEATHER_RAIN) ?
        WEATHER_SNOWSTORM : weather;
}

int weather_preview(Biome biome, int weather, uint32_t ms)
{
    if (weather == WEATHER_CHANGING) weather = (ms / 4000) % 2 ? WEATHER_STORM : WEATHER_CLEAR;
    return weather_visual(biome, weather);
}

int weather_lightning(uint32_t tick)
{
    uint32_t cycle = tick / 400;
    uint32_t strike = 150 + (cycle * 2654435761u >> 24) % 200;
    return tick % 400 >= strike && tick % 400 < strike + 5;
}

static uint32_t rng_next(uint32_t *s)
{
    uint32_t x = *s;                       /* xorshift32 */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

static int rng_range(uint32_t *s, int lo, int hi)
{
    return lo + (int)(rng_next(s) % (uint32_t)(hi - lo + 1));
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* a world's amounts, for this map's size: by area (lakes, trees, compounds...) or along it (rivers,
 * roads); a LARGE map (the original's size) has them as they are */
static int by_area(const Map *m, int n) { long k = ((long)n * m->w * m->h + 8192) / 16384; return n <= 0 ? 0 : k < 1 ? 1 : (int)k; }
static int along(const Map *m, int n) { int k = (n * m->w + 64) / 128; return n <= 0 ? 0 : k < 1 ? 1 : k; }

static void gen_lakes(Map *m, uint32_t *r)
{
    int i, dx, dy;
    for (i = 0; i < by_area(m, P->lakes); i++) {
        int cx = rng_range(r, 15, m->w - 16);
        int cy = rng_range(r, 15, m->h - 16);
        int rad = rng_range(r, P->lake_min, P->lake_max);
        for (dy = -rad - 1; dy <= rad + 1; dy++)
            for (dx = -rad - 1; dx <= rad + 1; dx++)
                if (dx * dx + dy * dy <= rad * rad + rng_range(r, -rad, rad))
                    map_set(m, cx + dx, cy + dy, TILE_WATER);
    }
}

static void gen_rivers(Map *m, uint32_t *r)
{
    int i, p, w;
    for (i = 0; i < along(m, P->rivers); i++) {   /* meandering north-south rivers */
        int x = rng_range(r, 16, m->w - 17), dir = 0;
        for (p = 0; p < m->h; p++) {
            if ((rng_next(r) & 7) == 0) dir = rng_range(r, -1, 1);
            x = clamp(x + dir, 6, m->w - 9);
            for (w = 0; w < 3; w++)
                map_set(m, x + w, p, TILE_WATER);
        }
    }
}

static void gen_roads(Map *m, uint32_t *r)
{
    int i, p;
    for (i = 0; i < along(m, P->roads); i++) {    /* east-west roads, they cross water as causeways */
        int y = rng_range(r, 10, m->h - 12);
        for (p = 0; p < m->w; p++) {
            if ((rng_next(r) & 15) == 0)
                y = clamp(y + rng_range(r, -1, 1), 4, m->h - 6);
            map_set(m, p, y, TILE_ROAD);
            map_set(m, p, y + 1, TILE_ROAD);
        }
    }
    for (i = 0; i < along(m, P->roads); i++) {    /* north-south roads */
        int x = rng_range(r, 10, m->w - 12);
        for (p = 0; p < m->h; p++) {
            if ((rng_next(r) & 15) == 0)
                x = clamp(x + rng_range(r, -1, 1), 4, m->w - 6);
            map_set(m, x, p, TILE_ROAD);
            map_set(m, x + 1, p, TILE_ROAD);
        }
    }
}

static void gen_scatter(Map *m, uint32_t *r)
{
    int i, k;
    for (i = 0; i < by_area(m, P->rock_clusters); i++) {   /* rock clusters */
        int cx = rng_range(r, 3, m->w - 4), cy = rng_range(r, 3, m->h - 4);
        int n = rng_range(r, 3, 8);
        for (k = 0; k < n; k++)
            {   /* (one draw at a time: compilers may evaluate arguments in any order) */
                int dy = rng_range(r, -2, 2), dx = rng_range(r, -2, 2);
                map_set(m, cx + dx, cy + dy, TILE_ROCK);
            }
    }
    for (i = 0; i < by_area(m, P->clumps); i++) {   /* tree clumps; neighbours merge visually */
        int cx = rng_range(r, 3, m->w - 4), cy = rng_range(r, 3, m->h - 4);
        int parts = rng_range(r, 1, P->clump_parts), p, dx, dy;
        for (p = 0; p < parts; p++) {
            int bx = cx + rng_range(r, -2, 2), by = cy + rng_range(r, -2, 2);
            int rad = rng_range(r, 0, 2);
            for (dy = -rad; dy <= rad; dy++)
                for (dx = -rad; dx <= rad; dx++)
                    if (dx * dx + dy * dy <= rad * rad + 1 && map_get(m, bx + dx, by + dy) == TILE_SAND)
                        map_set(m, bx + dx, by + dy, TILE_TREE);
        }
        (void)k;
    }
    for (i = 0; i < by_area(m, (P->clumps + 3) / 4); i++) {   /* groves: trees overhead, as in the original (hide under them) */
        int cx = rng_range(r, 6, m->w - 7), cy = rng_range(r, 6, m->h - 7);
        int parts = rng_range(r, 2, 4), p, dx, dy;
        for (p = 0; p < parts; p++) {
            int bx = cx + rng_range(r, -2, 2), by = cy + rng_range(r, -2, 2), rad = rng_range(r, 1, 2);
            for (dy = -rad; dy <= rad; dy++)
                for (dx = -rad; dx <= rad; dx++)
                    if (dx * dx + dy * dy <= rad * rad + 1 && map_get(m, bx + dx, by + dy) == TILE_SAND)
                        map_set(m, bx + dx, by + dy, TILE_CANOPY);
        }
    }
}

static void walled_plot(Map *m, int x0, int y0, int x1, int y1)
{
    int x, y;
    for (y = y0 - 1; y <= y1 + 1; y++)              /* clear the plot */
        for (x = x0 - 1; x <= x1 + 1; x++)
            map_set(m, x, y, TILE_SAND);
    for (x = x0; x <= x1; x++) {
        map_set(m, x, y0, TILE_WALL);
        map_set(m, x, y1, TILE_WALL);
    }
    for (y = y0; y <= y1; y++) {
        map_set(m, x0, y, TILE_WALL);
        map_set(m, x1, y, TILE_WALL);
    }
}

static int try_place(Map *m, StructType t, int x, int y, int team)
{
    if (!map_area_is(m, x, y, struct_info[t].w, struct_info[t].h, 1, TILE_SAND))
        return -1;
    return map_place_structure(m, t, x, y, team);
}

/* inner corners: watch towers or guns */
static void corner_defences(Map *m, uint32_t *r, int x0, int y0, int x1, int y1, int team, int guns)
{
    static const uint8_t pick[] = { ST_TOWER, ST_T_MG, ST_T_CANNON, ST_T_TWIN, ST_TOWER };
    int cx[4] = { x0 + 1, x1 - 1, x0 + 1, x1 - 1 }, cy[4] = { y0 + 1, y0 + 1, y1 - 1, y1 - 1 }, i;
    for (i = 0; i < 4; i++)
        map_place_structure(m, (StructType)(guns ? pick[rng_range(r, 1, 3)] : pick[rng_range(r, 0, 4)]),
                            cx[i], cy[i], team);
}

static void gen_compounds(Map *m, uint32_t *r)
{
    int i, k;
    for (i = 0; i < by_area(m, P->compounds); i++) {
        int w  = rng_range(r, 10, 16), h = rng_range(r, 8, 13);
        int x0 = rng_range(r, 4, m->w - w - 5), y0 = rng_range(r, 4, m->h - h - 5);
        int x1 = x0 + w - 1, y1 = y0 + h - 1;
        int side = rng_range(r, 0, 3);

        walled_plot(m, x0, y0, x1, y1);
        /* two-tile gate in the middle of one side */
        if (side == 0) { map_set(m, (x0+x1)/2, y0, TILE_SAND); map_set(m, (x0+x1)/2+1, y0, TILE_SAND); }
        if (side == 1) { map_set(m, (x0+x1)/2, y1, TILE_SAND); map_set(m, (x0+x1)/2+1, y1, TILE_SAND); }
        if (side == 2) { map_set(m, x0, (y0+y1)/2, TILE_SAND); map_set(m, x0, (y0+y1)/2+1, TILE_SAND); }
        if (side == 3) { map_set(m, x1, (y0+y1)/2, TILE_SAND); map_set(m, x1, (y0+y1)/2+1, TILE_SAND); }

        corner_defences(m, r, x0, y0, x1, y1, TEAM_ENEMY, 0);

        /* one big building first, then whatever else fits, with a gap */
        for (k = 0; k < 12; k++) {
            static const uint8_t big[] = { ST_HQ, ST_DOME, ST_POWER, ST_WAREHOUSE };
            static const uint8_t inside[] = { ST_BARRACKS, ST_BARRACKS, ST_BUNKER, ST_DEPOT, ST_DOME,
                                              ST_WAREHOUSE, ST_RADAR, ST_POWER, ST_T_ROCKET, ST_T_HEAVY,
                                              ST_T_MG, ST_DEPOT, ST_T_TWIN, ST_T_MORTAR };
            StructType t = (StructType)(k == 0 ? big[rng_range(r, 0, 3)] : inside[rng_range(r, 0, 13)]);
            int bw = struct_info[t].w, bh = struct_info[t].h, bx, by;
            if (x1 - x0 - 3 < bw || y1 - y0 - 3 < bh)
                continue;
            bx = rng_range(r, x0 + 2, x1 - 1 - bw);
            by = rng_range(r, y0 + 2, y1 - 1 - bh);
            if (bx + bw > x1 - 1 || by + bh > y1 - 1)
                continue;
            try_place(m, t, bx, by, TEAM_ENEMY);
        }
    }
}

static void gen_outposts(Map *m, uint32_t *r)
{
    static const uint8_t lone[] = { ST_TOWER, ST_TOWER, ST_BUNKER, ST_BUNKER, ST_DEPOT, ST_BARRACKS,
                                    ST_T_MG, ST_T_MG, ST_T_CANNON, ST_T_CANNON, ST_T_ROCKET, ST_T_TWIN,
                                    ST_T_MORTAR, ST_POWCAMP, ST_POWCAMP, ST_RADAR, ST_WAREHOUSE };
    int i;
    for (i = 0; i < by_area(m, P->bunkers + 8); i++) {  /* single structures out in the open */
        StructType t = (StructType)lone[rng_range(r, 0, 16)];
        {
            int py = rng_range(r, 3, m->h - 6), px = rng_range(r, 3, m->w - 7);
            try_place(m, t, px, py, TEAM_ENEMY);
        }
    }
}

/* A home base: walls, a gate on the south side right below the garage
 * door, so the tank rolls straight out; first aid and fuel beside it. */
static void road_out(Map *m, int x, int y1)
{
    int y;
    for (y = y1 + 1; y < y1 + 5 && y < m->h - 1; y++) {   /* a road out of the gate */
        map_set(m, x, y, TILE_ROAD);
        map_set(m, x + 1, y, TILE_ROAD);
    }
}

/* asphalt on plain ground only: never over buildings, walls or water */
static void pave(Map *m, int x0, int y0, int x1, int y1)
{
    int x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            if (map_get(m, x, y) == TILE_SAND && !m->sid[y][x])
                map_set(m, x, y, TILE_ROAD);
}

static void gate(Map *m, int x0, int y0, int x1, int y1)
{
    int x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            map_set(m, x, y, TILE_SAND);
}

/* The player's base, after the original: a big walled camp with its own
 * road network, three garages side by side (one per tank), and the camp's
 * services around them. 34 x 26 tiles. */
static int camp_garages = 4;                 /* 3 for the original's camp, 4 for four players */
void worldgen_set_camp_garages(int n) { camp_garages = n < 3 ? 3 : n > MAX_GARAGES ? MAX_GARAGES : n; }

/* a small map (64 tiles across): a compact base, 22 x 16, the same for both sides: four garages
 * in a row onto an apron, the headquarters, the flag house and first aid behind, two guns by the road
 * out; a south gate down the road, an east gate at the apron's end */
#define SMALL_BASE_W 22
#define SMALL_BASE_H 16
static int base_w(const Map *m) { return m->w <= 64 ? SMALL_BASE_W : BASE_W; }
static int base_h(const Map *m) { return m->w <= 64 ? SMALL_BASE_H : BASE_H; }
static void gen_compact_base(Map *m, int x0, int y0, Base *b, int t)
{
    int x1 = x0 + SMALL_BASE_W - 1, y1 = y0 + SMALL_BASE_H - 1, i;
    walled_plot(m, x0, y0, x1, y1);
    b->x0 = x0; b->y0 = y0; b->x1 = x1; b->y1 = y1;
    b->num_garages = camp_garages;
    b->garages[3] = -1;
    for (i = 0; i < camp_garages && i < 4; i++) {
        int gx = x0 + 4 + i * 4, gy = y0 + 6;
        b->garages[i] = map_place_structure(m, ST_GARAGE, gx, gy, t);
        b->class_tx[i] = gx + 1;
        b->class_ty[i] = gy + 1;
    }
    if (camp_garages < 4) { b->garages[3] = b->garages[1]; b->class_tx[3] = b->class_tx[1]; b->class_ty[3] = b->class_ty[1]; }
    b->garage = b->garages[1];
    b->spawn_tx = b->class_tx[1];
    b->spawn_ty = b->class_ty[1];
    try_place(m, ST_HQ, x0 + 2, y0 + 2, t);
    b->flag_house = try_place(m, ST_FLAGHOUSE, x0 + 10, y0 + 2, t);
    try_place(m, ST_FIRSTAID, x0 + 16, y0 + 2, t);
    pave(m, x0 + 2, y0 + 9, x1 - 1, y0 + 10);          /* the apron, out to the east gate */
    pave(m, x0 + 10, y0 + 11, x0 + 11, y1 - 1);        /* the road to the south gate */
    try_place(m, ST_T_TWIN, x0 + 7, y1 - 3, t);        /* the guns by the road out */
    try_place(m, ST_T_TWIN, x0 + 14, y1 - 3, t);
    gate(m, x0 + 9, y1, x0 + 12, y1);
    gate(m, x1, y0 + 9, x1, y0 + 10);
}

static void gen_player_base(Map *m, uint32_t *r, int x0, int y0, Base *b)
{
    int x1 = x0 + BASE_W - 1, y1 = y0 + BASE_H - 1, i, t = TEAM_PLAYER;
    if (m->w <= 64) { (void)r; gen_compact_base(m, x0, y0, b, t); return; }   /* (a small map: compact) */
    walled_plot(m, x0, y0, x1, y1);
    b->x0 = x0; b->y0 = y0; b->x1 = x1; b->y1 = y1;

    /* garages: rows 11-13, doors open onto the apron in row 14; one per tank
     * class, and a fourth to the west for a fourth player */
    b->num_garages = camp_garages;
    b->garages[3] = -1;
    for (i = 0; i < camp_garages; i++) {
        int gx = i < 3 ? x0 + 12 + i * 4 : x0 + 8, gy = y0 + 11;
        b->garages[i] = map_place_structure(m, ST_GARAGE, gx, gy, t);
        b->class_tx[i] = gx + 1;
        b->class_ty[i] = gy + 1;
    }
    b->garage = b->garages[1];
    b->spawn_tx = b->class_tx[1];
    b->spawn_ty = b->class_ty[1];

    /* the back of the camp: command, flag, power, radar */
    try_place(m, ST_HQ, x0 + 3, y0 + 3, t);
    try_place(m, ST_DOME, x0 + 9, y0 + 3, t);
    b->flag_house = try_place(m, ST_FLAGHOUSE, x0 + 16, y0 + 4, t);
    try_place(m, ST_POWER, x0 + 21, y0 + 3, t);
    try_place(m, ST_RADAR, x0 + 27, y0 + 3, t);
    try_place(m, ST_WAREHOUSE, x0 + 26, y0 + 8, t);
    /* the sides: barracks, stores */
    try_place(m, ST_BARRACKS, x0 + 3, y0 + 9, t);
    try_place(m, ST_BARRACKS, x0 + 3, y0 + 13, t);
    try_place(m, ST_WAREHOUSE, x0 + 26, y0 + 12, t);
    /* the front: first aid and fuel either side of the main road */
    try_place(m, ST_FIRSTAID, x0 + 10, y0 + 18, t);   /* clear of the three garage lanes */
    try_place(m, ST_DEPOT, x0 + 19, y0 + 18, t);
    try_place(m, ST_BUNKER, x0 + 3, y0 + 18, t);
    try_place(m, ST_DEPOT, x0 + 28, y0 + 18, t);
    /* our own guns on the walls */
    corner_defences(m, r, x0, y0, x1, y1, t, 1);
    try_place(m, ST_T_CANNON, x0 + 14, y1 - 2, t);
    try_place(m, ST_T_CANNON, x0 + 19, y1 - 2, t);
    try_place(m, ST_T_TWIN, x1 - 2, y0 + 16, t);
    try_place(m, ST_T_TWIN, x0 + 1 + 1, y0 + 16, t);

    /* roads last, on whatever ground is left */
    pave(m, x0 + 8, y0 + 14, x0 + 22, y0 + 15);       /* apron in front of the garages */
    pave(m, x0 + 16, y0 + 14, x0 + 17, y1 - 1);       /* main road to the south gate */
    pave(m, x0 + 2, y0 + 21, x1 - 1, y0 + 22);        /* avenue to the east gate */
    pave(m, x0 + 8, y0 + 8, x0 + 8, y0 + 22);         /* west lane */
    pave(m, x0 + 24, y0 + 7, x0 + 25, y0 + 22);       /* east lane */
    pave(m, x0 + 8, y0 + 7, x0 + 25, y0 + 8);         /* back road */
    gate(m, x0 + 15, y1, x0 + 18, y1);                /* wide south gate */
    gate(m, x1, y0 + 21, x1, y0 + 22);                /* east gate */
    road_out(m, x0 + 16, y1);
}

/* The enemy fortress: the same size, three garages, and guns everywhere */
static void gen_enemy_base(Map *m, uint32_t *r, int x0, int y0, Base *b)
{
    int x1 = x0 + BASE_W - 1, y1 = y0 + BASE_H - 1, i, t = TEAM_ENEMY;
    if (m->w <= 64) { (void)r; gen_compact_base(m, x0, y0, b, t); return; }
    walled_plot(m, x0, y0, x1, y1);
    b->x0 = x0; b->y0 = y0; b->x1 = x1; b->y1 = y1;

    b->num_garages = camp_garages;                    /* one per tank class, side by side; a fourth to the west, */
    for (i = 0; i < camp_garages; i++) {              /* as the camp has: even sides of four (the original: three) */
        int gx = i < 3 ? x0 + 11 + i * 4 : x0 + 7, gy = y0 + 11;
        b->garages[i] = map_place_structure(m, ST_GARAGE, gx, gy, t);
        b->class_tx[i] = gx + 1;
        b->class_ty[i] = gy + 1;
    }
    if (camp_garages < 4) {
        b->garages[3] = b->garages[1];
        b->class_tx[3] = b->class_tx[1];
        b->class_ty[3] = b->class_ty[1];
    }
    b->garage = b->garages[1];
    b->spawn_tx = b->class_tx[1];
    b->spawn_ty = b->class_ty[1];

    b->flag_house = try_place(m, ST_FLAGHOUSE, x0 + 16, y0 + 3, t);  /* deep inside */
    try_place(m, ST_T_HEAVY, x0 + 12, y0 + 3, t);                    /* guarding the flag */
    try_place(m, ST_T_HEAVY, x0 + 20, y0 + 3, t);
    try_place(m, ST_HQ, x0 + 3, y0 + 3, t);
    try_place(m, ST_POWER, x0 + 26, y0 + 3, t);
    try_place(m, ST_RADAR, x0 + 27, y0 + 7, t);
    try_place(m, ST_T_MORTAR, x0 + 9, y0 + 7, t);
    try_place(m, ST_T_MORTAR, x0 + 22, y0 + 7, t);
    try_place(m, ST_BARRACKS, x0 + 3, y0 + 9, t);
    try_place(m, ST_BARRACKS, x0 + 3, y0 + 13, t);
    try_place(m, ST_BARRACKS, x0 + 28, y0 + 12, t);
    try_place(m, ST_HELIPAD, x0 + 24, y0 + 12, t);
    try_place(m, ST_T_ROCKET, x0 + 7, y0 + 12, t);
    try_place(m, ST_FIRSTAID, x0 + 10, y0 + 18, t);
    try_place(m, ST_DEPOT, x0 + 21, y0 + 18, t);
    try_place(m, ST_DOME, x0 + 3, y0 + 18, t);
    try_place(m, ST_WAREHOUSE, x0 + 27, y0 + 18, t);
    corner_defences(m, r, x0, y0, x1, y1, t, 1);
    try_place(m, ST_T_TWIN, x0 + 14, y1 - 2, t);                     /* inside the gate */
    try_place(m, ST_T_TWIN, x0 + 19, y1 - 2, t);
    try_place(m, ST_T_CANNON, x1 - 2, y0 + 16, t);
    try_place(m, ST_T_CANNON, x0 + 2, y0 + 16, t);

    pave(m, camp_garages >= 4 ? x0 + 7 : x0 + 10, y0 + 14, x0 + 22, y0 + 15);   /* the apron in front of the garages */
    pave(m, x0 + 16, y0 + 14, x0 + 17, y1 - 1);
    pave(m, x0 + 2, y0 + 21, x1 - 1, y0 + 22);
    pave(m, x0 + 8, y0 + 9, x1 - 8, y0 + 9);
    gate(m, x0 + 15, y1, x0 + 18, y1);
    gate(m, x0, y0 + 21, x0, y0 + 22);                /* west gate */
    road_out(m, x0 + 16, y1);
    try_place(m, ST_T_CANNON, x0 + 13, y1 + 2, t);    /* guarding the road out */
    try_place(m, ST_T_CANNON, x0 + 20, y1 + 2, t);
}

/* nothing hostile right next to where the player starts */
static void clear_enemy_guns_near(Map *m, const Base *b, int radius)
{
    int i, x, y, cx = (b->x0 + b->x1) / 2, cy = (b->y0 + b->y1) / 2;
    for (i = 0; i < m->num_st; i++) {
        Structure *st = &m->st[i];
        const StructInfo *si = &struct_info[st->type];
        int dx = st->x - cx, dy = st->y - cy;
        if (st->hp == 0 || st->team != TEAM_ENEMY || (!(si->flags & SF_TURRET) && st->type != ST_BARRACKS))
            continue;                                   /* guns, and barracks full of soldiers */
        if (dx * dx + dy * dy > radius * radius)
            continue;
        for (y = st->y; y < st->y + si->h; y++)
            for (x = st->x; x < st->x + si->w; x++)
                if (m->sid[y][x] == i + 1)
                    map_set(m, x, y, TILE_SAND);
        st->hp = 0;
        st->type = ST_NONE;                         /* gone, not destroyed */
    }
}

/* where the anti-camping helicopters live; never right next to the player */
static void gen_helipads(Map *m, uint32_t *r, const Base *player)
{
    int i, placed = 0, cx = (player->x0 + player->x1) / 2, cy = (player->y0 + player->y1) / 2;
    for (i = 0; i < 200 && placed < by_area(m, 5); i++) {
        int x = rng_range(r, 4, m->w - 8), y = rng_range(r, 4, m->h - 8);
        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) < 40 * 40)
            continue;
        if (try_place(m, ST_HELIPAD, x, y, TEAM_ENEMY) >= 0)
            placed++;
    }
}

void worldgen_generate(Map *m, uint32_t seed, Biome biome, Base bases[2])
{
    uint32_t r = seed ^ 0x9E3779B9u ^ ((uint32_t)biome * 0x85EBCA6Bu);
    int i, corner, px, py, ex, ey;
    if (r == 0)
        r = 1;

    P = &biomes[biome < BIOME_COUNT ? biome : 0];
    map_fill(m, TILE_SAND);
    gen_lakes(m, &r);
    gen_rivers(m, &r);
    gen_roads(m, &r);
    gen_scatter(m, &r);
    gen_compounds(m, &r);
    gen_outposts(m, &r);

    /* the two home bases in opposite corners of the map */
    corner = rng_range(&r, 0, 3);
    {   /* (a small or medium map: the bases close to their corners, the middle left open; larger: as ever) */
        int sm = m->w < 128, a = sm ? 2 : 4, b = sm ? 4 : 10, c = sm ? 5 : 7, d = sm ? 7 : 12;
        px = (corner & 1) ? m->w - base_w(m) - rng_range(&r, a, b) : rng_range(&r, a, b);
        py = (corner & 2) ? m->h - base_h(m) - rng_range(&r, c, d) : rng_range(&r, a, b);
        ex = (corner & 1) ? rng_range(&r, a, b) : m->w - base_w(m) - rng_range(&r, a, b);
        ey = (corner & 2) ? rng_range(&r, a, b) : m->h - base_h(m) - rng_range(&r, c, d);
    }
    gen_enemy_base(m, &r, ex, ey, &bases[TEAM_ENEMY]);
    gen_player_base(m, &r, px, py, &bases[TEAM_PLAYER]);
    clear_enemy_guns_near(m, &bases[TEAM_PLAYER], 36);
    gen_helipads(m, &r, &bases[TEAM_PLAYER]);

    for (i = 0; i < m->w; i++) {                   /* indestructible border */
        map_set(m, i, 0, TILE_ROCK);
        map_set(m, i, m->h - 1, TILE_ROCK);
    }
    for (i = 0; i < m->h; i++) {
        map_set(m, 0, i, TILE_ROCK);
        map_set(m, m->w - 1, i, TILE_ROCK);
    }
    map_prune(m);                                   /* nothing half built over */
}

void worldgen_bases(Map *m, uint32_t seed, int px, int py, int ex, int ey, Base bases[2])
{
    uint32_t r = seed ^ 0x5BD1E995u;
    int i;
    if (r == 0) r = 1;
    P = &biomes[0];
    if (px < 1) px = 1;
    if (py < 1) py = 1;
    if (px > m->w - base_w(m) - 1) px = m->w - base_w(m) - 1;
    if (py > m->h - base_h(m) - 6) py = m->h - base_h(m) - 6;    /* room for the road out */
    if (ex < 1) ex = 1;
    if (ey < 1) ey = 1;
    if (ex > m->w - base_w(m) - 1) ex = m->w - base_w(m) - 1;
    if (ey > m->h - base_h(m) - 6) ey = m->h - base_h(m) - 6;
    gen_enemy_base(m, &r, ex, ey, &bases[TEAM_ENEMY]);
    gen_player_base(m, &r, px, py, &bases[TEAM_PLAYER]);
    clear_enemy_guns_near(m, &bases[TEAM_PLAYER], 36);
    for (i = 0; i < m->w; i++) {
        map_set(m, i, 0, TILE_ROCK);
        map_set(m, i, m->h - 1, TILE_ROCK);
    }
    for (i = 0; i < m->h; i++) {
        map_set(m, 0, i, TILE_ROCK);
        map_set(m, m->w - 1, i, TILE_ROCK);
    }
    map_prune(m);
}

/* Buildings partly built over (a base laid on top of a compound, say) are
 * gone: without this their soldiers would still come out of nowhere. */
void map_prune(Map *m)
{
    int i, x, y;
    for (i = 0; i < m->num_st; i++) {
        Structure *st = &m->st[i];
        const StructInfo *si = &struct_info[st->type];
        int whole = st->type != ST_NONE;
        for (y = st->y; whole && y < st->y + si->h; y++)
            for (x = st->x; whole && x < st->x + si->w; x++)
                whole = x < m->w && y < m->h && m->sid[y][x] == i + 1;
        if (whole || st->type == ST_NONE) continue;
        if (st->hp == 0 && si->hp) continue;            /* destroyed already: its remains stay */
        for (y = st->y; y < st->y + si->h && y < m->h; y++)       /* what's left of it goes too */
            for (x = st->x; x < st->x + si->w && x < m->w; x++)
                if (m->sid[y][x] == i + 1) map_set(m, x, y, TILE_SAND);
        st->type = ST_NONE;
        st->hp = 0;
    }
}

void worldgen_protect_camp(Map *m, const Base *b) { clear_enemy_guns_near(m, b, 36); map_prune(m); }
