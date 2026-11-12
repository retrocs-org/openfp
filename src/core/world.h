/* SPDX-License-Identifier: MIT
 * Tile map with per-tile destruction state. */
#ifndef OFP_WORLD_H
#define OFP_WORLD_H

#include <stdint.h>
#include "core/fixed.h"

#ifndef OFP_MAP_MAX                 /* the biggest map the compiled grid holds */
#define OFP_MAP_MAX 192
#endif
#define MAP_W      OFP_MAP_MAX     /* (a map's own size: Map.w, Map.h) */
#define MAP_H      OFP_MAP_MAX
enum { SIZE_SMALL, SIZE_MEDIUM, SIZE_LARGE, SIZE_HUGE, MAP_SIZES };   /* 64, 96, 128 (the original's), 192 tiles */
enum { SIZE_ANY = MAP_SIZES };    /* local map browser filter only */
extern const int map_size_tiles[MAP_SIZES];
extern const char *const map_size_name[MAP_SIZES];   /* "small", "medium", "large", "huge" */
#define TILE_SHIFT 4
#define TILE_SIZE  (1 << TILE_SHIFT)        /* 16 px */
#define WORLD_W_PX (MAP_W * TILE_SIZE)
#define WORLD_H_PX (MAP_H * TILE_SIZE)

typedef enum {
    TILE_SAND,
    TILE_ROAD,
    TILE_WATER,
    TILE_ROCK,
    TILE_TREE,
    TILE_STUMP,
    TILE_WALL,
    TILE_RUBBLE,
    TILE_BUILDING,
    TILE_RUINS,
    TILE_FLOOR,     /* concrete floor inside garages: drivable, indestructible */
    TILE_CANOPY,    /* trees overhead, as in the original: drive under, hidden; shots pass; it stays */
    TILE_ICE,      /* enhanced Winter: drivable, very little grip */
    TILE_OIL,      /* enhanced terrestrial worlds: slippery road spill */
    TILE_CRATER,   /* dry lunar chasm, impassable but shots pass over */
    TILE_COUNT
} TileType;

#define TF_SOLID       0x01   /* blocks vehicles */
#define TF_BLOCKS_SHOT 0x02   /* stops projectiles */

typedef struct {
    uint8_t flags;
    uint8_t hp;            /* 0 = indestructible */
    uint8_t destroyed_to;  /* TileType it turns into */
} TileInfo;

extern const TileInfo tile_info[TILE_COUNT];

/* Multi-tile structures. Every tile they cover is TILE_BUILDING (solid,
 * stops shots) and points at the structure through sid[][]; damage to any
 * of its tiles goes to the structure, and when it falls the whole
 * footprint turns to ruins at once. */
typedef enum {
    ST_NONE,
    ST_TOWER,       /* 1x1 watch tower                          */
    ST_BUNKER,      /* 2x2 concrete bunker                      */
    ST_DEPOT,       /* 2x2 fuel depot                           */
    ST_BARRACKS,    /* 3x2 barracks                             */
    ST_DOME,        /* 3x3 bio-dome                             */
    ST_HQ,          /* 4x3 command building                     */
    ST_GARAGE,      /* 3x3 tank garage, indestructible; spawn   */
    ST_FIRSTAID,    /* 2x2 first-aid station (POW drop-off)     */
    ST_POWCAMP,     /* 3x3 prisoner camp                        */
    ST_RADAR,       /* 2x2 radar station                        */
    ST_POWER,       /* 3x2 power plant                          */
    ST_WAREHOUSE,   /* 3x2 warehouse                            */
    ST_T_MG,        /* 1x1 machine-gun nest     } gun turrets:  */
    ST_T_CANNON,    /* 1x1 cannon turret        } they aim and  */
    ST_T_ROCKET,    /* 2x2 rocket launcher      } fire at enemy */
    ST_T_HEAVY,     /* 2x2 heavy gun emplacement} tanks         */
    ST_FLAGHOUSE,   /* 2x2 flag bunker: blow it open to get the flag */
    ST_HELIPAD,     /* 3x3 helicopter pad: drivable, launches helicopters */
    ST_T_TWIN,      /* 1x1 twin auto-cannon: short bursts           */
    ST_T_MORTAR,    /* 2x2 mortar pit: lobs shells over walls        */
    ST_GATE_H, ST_GATE_V,   /* four-tile destructible base gates */
    ST_TYPE_COUNT
} StructType;

#define SF_INDESTRUCTIBLE 0x01
#define SF_TURRET         0x02
#define SF_GATE           0x04

typedef struct {
    const char *name;
    uint8_t  w, h;          /* footprint in tiles */
    uint8_t  hp;            /* hits to destroy */
    uint8_t  score;         /* points, x5 */
    uint8_t  flags;         /* SF_* */
    uint16_t floor;         /* bit (y*4+x) set = drivable floor tile, not wall */
} StructInfo;

extern const StructInfo struct_info[ST_TYPE_COUNT];

#define MAX_STRUCTS  640
#define STRUCT_MAX_W 4
#define STRUCT_MAX_H 3
#define TEAM_PLAYER  0
#define TEAM_ENEMY   1
#define TEAM_INDEPENDENT 2   /* map turrets hostile to both tank teams */

typedef struct {
    uint8_t  type, hp, x, y;    /* x, y = top-left tile; hp 0 = destroyed */
    uint8_t  team, cooldown, burst, smoulder;   /* smoulder: destroyed and still glowing */
    uint16_t aim;               /* turrets: angle16 the gun points at;
                                   helipads: 0 = helicopter parked and ready,
                                   PAD_AWAY = it is flying, else ticks to a new one;
                                   destroyed: the tick it fell (low 16 bits) */
} Structure;

#define PAD_AWAY 0xFFFF

typedef struct {
    char description[161], author[41], contact[81];
} MapDetails;

typedef struct {
    uint8_t   type[MAP_H][MAP_W];
    uint8_t   hp[MAP_H][MAP_W];
    uint16_t  sid[MAP_H][MAP_W];        /* 1-based index into st[], 0 = none */
    Structure st[MAX_STRUCTS];
    int       num_st;
    uint8_t   time;                     /* TIME_*: when the battle is fought (from the map file) */
    uint8_t   size;                     /* SIZE_*: how big (size= line; LARGE without one, the original's) */
    int16_t   w, h;                     /* its own width and height, tiles; outside them, rock */
    /* the map's own weather and extras (weather=, extras= lines): has_* 0 = it doesn't say,
     * and the server or the menu decides */
    uint8_t   weather, extras, has_weather, has_extras;
    /* conquest: the map's own capture points (points= line; 0: placed by the game) */
    uint8_t   num_points;
    int16_t   point_tx[3], point_ty[3];
    /* an arena (arena=yes): no camps; tanks come in at its spawn points (spawns= line) */
    uint8_t   arena, num_spawns;
    uint8_t   preserve_roads;           /* keep authored asphalt on open base tiles */
    uint8_t   quiet;                    /* no generated vehicles, patrols or barracks reinforcements */
    uint8_t   custom_bases, mode;      /* player-built bases and their chosen game mode */
    uint8_t   players;                /* players= : how many it's made for (0: as many as fit) */
    int16_t   spawn_tx[8], spawn_ty[8];
    MapDetails details;
} Map;

/* An aligned two-by-two canopy is a single large crown in the imported maps. */
static inline int map_canopy_block(const Map *m, int tx, int ty)
{
    int x = tx & ~1, y = ty & ~1;
    return x + 1 < m->w && y + 1 < m->h &&
           m->type[y][x] == TILE_CANOPY && m->type[y][x + 1] == TILE_CANOPY &&
           m->type[y + 1][x] == TILE_CANOPY && m->type[y + 1][x + 1] == TILE_CANOPY;
}

void     map_fill(Map *m, TileType t);   /* (the whole grid, then the next size: rock outside it) */
void     map_set_size(Map *m, int size);   /* a size (SIZE_*): w, h, and rock outside them */
void     map_set_next_size(int size);      /* the next map_fill()'s (a random battlefield's; LARGE by default) */
int      map_size_fits(int size);   /* does a map of this size (SIZE_*) fit this build's grid? */
void     map_set(Map *m, int tx, int ty, TileType t);   /* also resets hp, unlinks structure */
TileType map_get(const Map *m, int tx, int ty);         /* outside = rock */

enum { DMG_NONE = -1, DMG_HIT = 0, DMG_TILE = 1, DMG_STRUCT = 2 };
/* Damage the tile (or the structure on it). Returns DMG_*; for DMG_STRUCT
 * *struct_out gets the index of the structure that was destroyed. */
int      map_damage(Map *m, int tx, int ty, int dmg, int *struct_out);

/* Place a structure with its top-left at tx,ty. The area must be on the
 * map; it is overwritten. Returns the index, or -1 if out of room. */
int      map_place_structure(Map *m, StructType type, int tx, int ty, int team);
/* Does the footprint (plus `margin` tiles around it) only contain `t`? */
int      map_area_is(const Map *m, int x0, int y0, int w, int h, int margin, TileType t);

/* Non-zero if an axis-aligned box (centre x,y, half-size) touches a solid tile. */
int      map_box_solid(const Map *m, fx x, fx y, fx half);
int      map_box_solid_for_team(const Map *m, fx x, fx y, fx half, int team);

/* Terrain families. A map style (tile art) names the biome it generates;
 * the biome only changes how the generator mixes the same tile types. */
typedef enum { BIOME_CLASSIC, BIOME_DESERT, BIOME_JUNGLE, BIOME_WINTER, BIOME_SCIFI,
               BIOME_VOLCANIC, BIOME_WASTELAND, BIOME_MOON, BIOME_SWAMP, BIOME_COUNT } Biome;   /* (new ones after: replays keep theirs) */

/* The legacy Sci-Fi ID is kept for old maps, replays and network games. */
#define PLAYABLE_BIOMES 8
extern const Biome playable_biomes[PLAYABLE_BIOMES];
Biome biome_step(Biome current, int direction);
int map_surface_allowed(Biome biome, int classic, TileType tile);
void map_normalize_surfaces(Map *m, Biome biome, int classic);

enum { WEATHER_CLEAR, WEATHER_STORM, WEATHER_CHANGING, WEATHER_RAIN,
       WEATHER_SNOW, WEATHER_FOG, WEATHER_SANDSTORM, WEATHER_CLOUDY, WEATHER_ERUPTION,
       WEATHER_METEOR, WEATHER_SNOWSTORM, WEATHER_COUNT };
enum { WEATHER_CLOUD_ROWS = 13 };
extern const uint8_t weather_cloud_span[WEATHER_CLOUD_ROWS];
const char *weather_name(int weather);
const char *weather_label(int weather);
int         weather_from_name(const char *name);
int         weather_allowed(Biome biome, int weather);
int         weather_step(Biome biome, int weather, int direction, int allow_auto);
int         weather_visual(Biome biome, int weather);
int         weather_preview(Biome biome, int weather, uint32_t ms);
int         weather_lightning(uint32_t tick);

/* time of day, a property of each map */
enum { TIME_DAY, TIME_DUSK, TIME_NIGHT, TIME_COUNT };
const char *time_name(int t);
int         time_from_name(const char *name);

Biome    biome_from_name(const char *name);           /* unknown -> classic */
const char *biome_name(Biome b);

/* Each side's home base: where its tanks start (inside the garage). Our camp
 * has four garages, the enemy fortress three: that's how many players a side
 * can have on a map. */
#define MAX_GARAGES 4
typedef struct {
    int spawn_tx, spawn_ty;     /* garage floor tile (the middle garage) */
    int garage;                 /* structure index of the middle garage */
    int num_garages;            /* how many tanks of this side can start at once (one per garage) */
    int garages[MAX_GARAGES];   /* structure index; the first three are one per tank class */
    int class_tx[MAX_GARAGES], class_ty[MAX_GARAGES];   /* floor tile inside each of them */
    int flag_house;             /* structure index, -1 if it did not fit */
    int x0, y0, x1, y1;         /* base walls */
} Base;

/* Procedural map with a home base for each team. */
void     worldgen_generate(Map *m, uint32_t seed, Biome biome, Base bases[2]);

/* Home bases are 34 x 26 tiles. worldgen_bases() builds both on a map that
 * came from somewhere else (a map file): player's camp with its top-left at
 * (px,py), the enemy fortress at (ex,ey); then the rock border. */
#define BASE_W 34
#define BASE_H 26
void     worldgen_bases(Map *m, uint32_t seed, int px, int py, int ex, int ey, Base bases[2]);
void     map_prune(Map *m);               /* drop buildings that were partly built over */
void     worldgen_set_camp_garages(int n);  /* our camp: 3 garages (the original's) or 4 (four players) */
void     worldgen_protect_camp(Map *m, const Base *b);   /* no enemy guns or barracks right by it */

/* ---- map files (.ofpmap) ---------------------------------------------------------
 * A small text format, read and written in memory (the core does no file
 * I/O; the frontend reads and writes the files):
 *
 *   OFPMAP 1
 *   name=Original P1 t1
 *   description=A short description
 *   author=Map maker
 *   contact=@mapmaker
 *   biome=classic
 *   bases=garages            optional: single garage per side, using the coordinates below
 *   base=player 4 94          top-left of each home base
 *   base=enemy 90 6
 *   time=night                day, dusk or night (day if left out)
 *   tiles                     then MAP_H lines of MAP_W characters:
 *   ....==~~^^TT,#:x          . sand  = road  ~ water  ^ rock  T bush
 *                             , flattened bush  # wall  : rubble  x ruins
 *                             I ice  o oil spill  O dry crater (enhanced worlds only)
 *   struct=bunker enemy 40 52 one line per building or gun: type, side (player, enemy or independent), x, y
 *   end
 *
 * The bases are rebuilt from their positions when a map loads, so what is
 * inside them isn't stored. Unknown lines are ignored. */
enum { MAP_MODE_CLASSIC, MAP_MODE_FLAG, MAP_MODE_CONQUEST, MAP_MODE_TDM, MAP_MODE_ANNIHILATION,
       MAP_MODE_WARLORD, MAP_MODE_HARDPOINT, MAP_MODE_KILLCONF, MAP_MODE_COUNT };
extern const char *const map_mode_names[MAP_MODE_COUNT];
int      map_mode_has_flags(int mode);
/* A negative mode means the map has no authored rules. */
int      map_allows_weather(int mode, Biome biome);
int      map_editor_mode_for_world(int mode, Biome biome);
int      map_editor_allows_weather(int mode, Biome biome);
int      map_mode_from_name(const char *name);
int      map_custom_players(int size, int players, int direction);
int      map_custom_count(const Map *m, int team, int type);
int      map_custom_contains(const Base *base, int type, int x, int y);
/* Infer ownership from a complete footprint. New garages and flag buildings
 * must have a team; unclaimed turrets are independent. */
int      map_editor_owner(const Map *m, const Base bases[2], int type, int x, int y);
void     map_editor_update_turrets(Map *m, const Base bases[2]);
typedef struct {
    int active, team, start_x, start_y, edge_x, edge_y;
    Base original, bounds;
} MapAreaEdit;
void     map_area_start(MapAreaEdit *edit, const Base bases[2], int team, int x, int y, int handle_radius);
void     map_area_move(MapAreaEdit *edit, const Map *m, int x, int y);
int      map_area_finish(MapAreaEdit *edit, const Map *m, Base bases[2]);
int      map_custom_zone(const Map *m, Base bases[2], int team, int x0, int y0, int x1, int y1);
const char *map_custom_error(const Map *m, const Base bases[2]);
void     map_custom_begin(Map *m, Base bases[2], int players, int mode);
void     map_custom_clear_generated_bases(Map *m, Base bases[2], int players, int mode);
Biome    mapfile_biome(const char *text);             /* the header alone */
int      mapfile_load(Map *m, Base bases[2], Biome *biome, char *name, int name_len,
                      const char *text, uint32_t seed);              /* 0 = ok */
void     map_no_camps(Base bases[2]);                 /* (an arena: the camps' data, nothing built) */
unsigned mapfile_save(const Map *m, const Base bases[2], Biome biome, const char *name,
                      char *out, unsigned cap);                     /* bytes written, 0 = too small */
/* Editor recovery accepts unfinished garage counts; playable map loading stays strict. */
int mapfile_load_draft(Map *m, Base bases[2], Biome *biome, char *name, int name_len, const char *text, uint32_t seed);
unsigned mapfile_save_draft(const Map *m, const Base bases[2], Biome biome, const char *name, char *out, unsigned cap);
#define  MAPFILE_MAX (MAP_H * (MAP_W + 1) + MAX_STRUCTS * 40 + 512)

#endif
