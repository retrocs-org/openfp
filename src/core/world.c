/* SPDX-License-Identifier: MIT */
#include <string.h>
#include "core/world.h"

const TileInfo tile_info[TILE_COUNT] = {
    /* SAND     */ { 0,                         0, TILE_SAND   },
    /* ROAD     */ { 0,                         0, TILE_ROAD   },
    /* WATER    */ { TF_SOLID,                  0, TILE_WATER  },
    /* ROCK     */ { TF_SOLID | TF_BLOCKS_SHOT, 0, TILE_ROCK   },
    /* TREE     */ { TF_BLOCKS_SHOT,            1, TILE_STUMP  },   /* tanks flatten bushes */
    /* STUMP    */ { 0,                         0, TILE_STUMP  },
    /* WALL     */ { TF_SOLID | TF_BLOCKS_SHOT, 3, TILE_RUBBLE },
    /* RUBBLE   */ { 0,                         0, TILE_RUBBLE },
    /* BUILDING */ { TF_SOLID | TF_BLOCKS_SHOT, 6, TILE_RUINS  },
    /* RUINS    */ { 0,                         0, TILE_RUINS  },
    /* FLOOR    */ { 0,                         0, TILE_FLOOR  },
    /* CANOPY   */ { 0,                         0, TILE_CANOPY },   /* (trees overhead: you hide under them) */
    /* ICE      */ { 0,                         0, TILE_ICE },
    /* OIL      */ { 0,                         0, TILE_OIL },
    /* CRATER   */ { TF_SOLID,                  0, TILE_CRATER },
};

int map_surface_allowed(Biome biome, int classic, TileType tile)
{
    if (tile == TILE_WATER) return biome != BIOME_MOON;
    if (tile == TILE_ICE) return !classic && biome == BIOME_WINTER;
    if (tile == TILE_CRATER) return !classic && biome == BIOME_MOON;
    if (tile == TILE_OIL) return !classic && biome != BIOME_CLASSIC && biome != BIOME_WINTER && biome != BIOME_MOON;
    return 1;
}

void map_normalize_surfaces(Map *m, Biome biome, int classic)
{
    int x, y;
    for (y = 0; y < m->h; y++) for (x = 0; x < m->w; x++) {
        TileType t = (TileType)m->type[y][x];
        if (map_surface_allowed(biome, classic, t)) continue;
        map_set(m, x, y, biome == BIOME_MOON && (t == TILE_WATER || t == TILE_ICE) ? (classic ? TILE_ROCK : TILE_CRATER) :
            t == TILE_CRATER ? TILE_ROCK : t == TILE_OIL ? TILE_ROAD : TILE_SAND);
    }
}

#define FLOOR_AT(x, y) (1u << ((y) * 4 + (x)))

const int map_size_tiles[MAP_SIZES] = { 64, 96, 128, 192 };
const char *const map_size_name[MAP_SIZES] = { "small", "medium", "large", "huge" };

static int next_size = SIZE_LARGE;                       /* (the next new map's: a random battlefield's) */
void map_set_next_size(int size) { next_size = size >= 0 && size < MAP_SIZES ? size : SIZE_LARGE; }

int map_size_fits(int size) { return size >= 0 && size < MAP_SIZES && map_size_tiles[size] <= MAP_W; }

void map_set_size(Map *m, int size)
{
    int x, y;
    if (size < 0 || size >= MAP_SIZES) size = SIZE_LARGE;
    while (size > 0 && !map_size_fits(size)) size--;    /* (never past the grid: match_start refuses such a map first) */
    m->size = (uint8_t)size;
    m->w = m->h = (int16_t)map_size_tiles[size];
    for (y = 0; y < MAP_H; y++)                          /* outside it: rock, nothing on it */
        for (x = 0; x < MAP_W; x++)
            if (x >= m->w || y >= m->h) { m->type[y][x] = TILE_ROCK; m->hp[y][x] = 0; m->sid[y][x] = 0; }
}

const StructInfo struct_info[ST_TYPE_COUNT] = {
    /*                  name         w  h  hp  pts flags              floor */
    [ST_NONE]      = { "none",      0, 0,  0,  0, 0,                 0 },
    [ST_TOWER]     = { "tower",     1, 1,  2,  5, 0,                 0 },
    [ST_BUNKER]    = { "bunker",    2, 2,  6, 10, 0,                 0 },
    [ST_DEPOT]     = { "depot",     2, 2,  3, 15, 0,                 0 },
    [ST_BARRACKS]  = { "barracks",  3, 2,  8, 20, 0,                 0 },
    [ST_DOME]      = { "dome",      3, 3, 10, 30, 0,                 0 },
    [ST_HQ]        = { "hq",        4, 3, 16, 50, 0,                 0 },
    [ST_GARAGE]    = { "garage",    3, 3,  0,  0, SF_INDESTRUCTIBLE, FLOOR_AT(1, 1) | FLOOR_AT(1, 2) },
    [ST_FIRSTAID]  = { "firstaid",  2, 2,  5,  0, 0,                 0 },
    [ST_POWCAMP]   = { "powcamp",   3, 3,  6, 20, 0,                 0 },
    [ST_RADAR]     = { "radar",     2, 2,  5, 25, 0,                 0 },
    [ST_POWER]     = { "power",     3, 2, 10, 40, 0,                 0 },
    [ST_WAREHOUSE] = { "warehouse", 3, 2,  8, 20, 0,                 0 },
    [ST_T_MG]      = { "mg",        1, 1,  4, 15, SF_TURRET,         0 },
    [ST_T_CANNON]  = { "cannon",    1, 1,  6, 20, SF_TURRET,         0 },
    [ST_T_ROCKET]  = { "rocket",    2, 2,  8, 35, SF_TURRET,         0 },
    [ST_T_HEAVY]   = { "heavy",     2, 2, 12, 45, SF_TURRET,         0 },
    [ST_FLAGHOUSE] = { "flag",      2, 2, 12, 60, 0,                 0 },
    [ST_HELIPAD]   = { "helipad",   3, 3,  0,  0, SF_INDESTRUCTIBLE, 0x777 },   /* all floor */
    [ST_T_TWIN]    = { "twin",      1, 1,  5, 20, SF_TURRET,         0 },
    [ST_T_MORTAR]  = { "mortar",    2, 2,  7, 35, SF_TURRET,         0 },
    [ST_GATE_H]    = { "gate-h",    4, 1,  6, 15, SF_GATE,           0 },
    [ST_GATE_V]    = { "gate-v",    1, 4,  6, 15, SF_GATE,           0 },
};

void map_fill(Map *m, TileType t)
{
    m->num_points = 0;                                   /* (a new map: no capture points of its own yet) */
    m->arena = 0; m->num_spawns = 0;                     /* (nor is it an arena) */
    m->players = m->custom_bases = m->mode = 0;
    m->quiet = 0;
    m->preserve_roads = 0;
    memset(&m->details, 0, sizeof m->details);
    memset(m->type, (int)t, sizeof m->type);
    memset(m->hp, tile_info[t].hp, sizeof m->hp);
    memset(m->sid, 0, sizeof m->sid);
    m->num_st = 0;
    map_set_size(m, next_size);                          /* (its size: rock outside it) */
}

void map_set(Map *m, int tx, int ty, TileType t)
{
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H)
        return;
    m->type[ty][tx] = (uint8_t)t;
    m->hp[ty][tx]   = tile_info[t].hp;
    m->sid[ty][tx]  = 0;
}

int map_area_is(const Map *m, int x0, int y0, int w, int h, int margin, TileType t)
{
    int x, y;
    for (y = y0 - margin; y < y0 + h + margin; y++)
        for (x = x0 - margin; x < x0 + w + margin; x++)
            if (map_get(m, x, y) != t || (x >= 0 && y >= 0 && x < MAP_W && y < MAP_H && m->sid[y][x]))
                return 0;
    return 1;
}

int map_place_structure(Map *m, StructType type, int tx, int ty, int team)
{
    const StructInfo *si = &struct_info[type];
    Structure *st;
    int x, y;
    if (type <= ST_NONE || type >= ST_TYPE_COUNT || m->num_st >= MAX_STRUCTS ||
        tx < 0 || ty < 0 || tx + si->w > MAP_W || ty + si->h > MAP_H)
        return -1;
    st = &m->st[m->num_st];
    st->type = (uint8_t)type;
    st->hp = si->hp;
    st->x = (uint8_t)tx;
    st->y = (uint8_t)ty;
    st->team = (uint8_t)team;
    st->cooldown = 0;
    st->aim = type == ST_HELIPAD ? 0 : 0x8000;   /* guns point south, pads start ready */
    for (y = ty; y < ty + si->h; y++)
        for (x = tx; x < tx + si->w; x++) {
            int floor = (si->floor >> ((y - ty) * 4 + (x - tx))) & 1;
            map_set(m, x, y, floor ? TILE_FLOOR : TILE_BUILDING);
            m->sid[y][x] = (uint16_t)(m->num_st + 1);
        }
    return m->num_st++;
}

TileType map_get(const Map *m, int tx, int ty)
{
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H)
        return TILE_ROCK;
    return (TileType)m->type[ty][tx];
}

int map_damage(Map *m, int tx, int ty, int dmg, int *struct_out)
{
    TileType t;
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H)
        return DMG_NONE;
    if (m->sid[ty][tx]) {                         /* a structure takes the hit */
        int idx = m->sid[ty][tx] - 1, x, y;
        Structure *st = &m->st[idx];
        const StructInfo *si = &struct_info[st->type];
        if (!st->hp || (si->flags & SF_INDESTRUCTIBLE))
            return DMG_NONE;
        if (st->hp > dmg) {
            st->hp = (uint8_t)(st->hp - dmg);
            return DMG_HIT;
        }
        st->hp = 0;
        for (y = st->y; y < st->y + si->h; y++)
            for (x = st->x; x < st->x + si->w; x++)
                if (m->sid[y][x] == idx + 1) {
                    if (si->flags & SF_GATE) {
                        map_set(m, x, y, TILE_ROAD);
                        m->sid[y][x] = (uint16_t)(idx + 1);  /* keeps the decorative barrier from reappearing */
                    } else map_set(m, x, y, TILE_RUINS);
                }
        if (struct_out) *struct_out = idx;
        return DMG_STRUCT;
    }
    t = (TileType)m->type[ty][tx];
    if (tile_info[t].hp == 0)
        return DMG_NONE;
    if (m->hp[ty][tx] > dmg) {
        m->hp[ty][tx] = (uint8_t)(m->hp[ty][tx] - dmg);
        return DMG_HIT;
    }
    map_set(m, tx, ty, (TileType)tile_info[t].destroyed_to);
    return DMG_TILE;
}

int map_box_solid_for_team(const Map *m, fx x, fx y, fx half, int team)
{
    /* -1 makes the right/bottom edge exclusive */
    int tx0 = fx_to_int(x - half)     >> TILE_SHIFT;
    int tx1 = fx_to_int(x + half - 1) >> TILE_SHIFT;
    int ty0 = fx_to_int(y - half)     >> TILE_SHIFT;
    int ty1 = fx_to_int(y + half - 1) >> TILE_SHIFT;
    int tx, ty;

    for (ty = ty0; ty <= ty1; ty++)
        for (tx = tx0; tx <= tx1; tx++)
            if (tile_info[map_get(m, tx, ty)].flags & TF_SOLID) {
                if (team >= 0 && tx >= 0 && ty >= 0 && tx < m->w && ty < m->h && m->sid[ty][tx]) {
                    const Structure *st = &m->st[m->sid[ty][tx] - 1];
                    if (st->team == team && (struct_info[st->type].flags & SF_GATE)) continue;
                }
                return 1;
            }
    return 0;
}

int map_box_solid(const Map *m, fx x, fx y, fx half)
{
    return map_box_solid_for_team(m, x, y, half, -1);
}

/* an arena's bases: off the map, no garages, no flag house (the core's camp data, with nothing built) */
void map_no_camps(Base bases[2])
{
    int k;
    memset(bases, 0, 2 * sizeof bases[0]);
    for (k = 0; k < 2; k++) {
        bases[k].x0 = bases[k].x1 = bases[k].y0 = bases[k].y1 = -64;
        bases[k].flag_house = -1;
        bases[k].garage = -1;
    }
}


const Biome playable_biomes[PLAYABLE_BIOMES] = {
    BIOME_CLASSIC, BIOME_MOON, BIOME_WINTER, BIOME_DESERT,
    BIOME_JUNGLE, BIOME_SWAMP, BIOME_VOLCANIC, BIOME_WASTELAND
};

Biome biome_step(Biome current, int direction)
{
    int i;
    for (i = 0; i < PLAYABLE_BIOMES; i++) if (playable_biomes[i] == current)
        return playable_biomes[(i + PLAYABLE_BIOMES + direction) % PLAYABLE_BIOMES];
    return BIOME_MOON;
}
