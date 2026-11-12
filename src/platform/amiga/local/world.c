/* SPDX-License-Identifier: MIT
 * src/platform/amiga/local/world.c: the match's world: maps and their setup, the ground, structures, trees, sprites, the objects in a view.
 * A part of the Amiga client, its own translation unit: what the parts share is in internal.h. */
#include "platform/amiga/local/internal.h"
#include "common/fx_budget.h"
#ifndef OFP_AMIGA_AGA
#include "platform/amiga/surface_art.h"
#endif
#ifndef OFP_ART_PREVIEW
#include <hardware/blit.h>
#include <hardware/custom.h>
#endif

/* the match's world: its ground (the classic's in the 68000 build) and its trees overhead */
static const uint8_t (*ground_tiles)[DEPTH][8] = classic_tiles;
#ifdef OFP_AMIGA_ONLINE
static const uint8_t (*ground_structures)[2][48][DEPTH + 1][8] = classic_structures;
int original_look;
#else
#define ground_structures classic_structures
#endif
#if defined(OFP_AMIGA_ONLINE) && !defined(OFP_AMIGA_AGA)
extern const uint32_t classic_classic_vehicles[6][32][32][5];
extern const uint32_t classic_volcanic_vehicles[6][32][32][5];
extern const uint32_t classic_moon_vehicles[6][32][32][5];
extern const uint8_t classic_classic_vehicles_bounds[6][32][4];
extern const uint8_t classic_volcanic_vehicles_bounds[6][32][4];
extern const uint8_t classic_moon_vehicles_bounds[6][32][4];
static const uint8_t (*vehicle_bounds)[32][4] = classic_classic_vehicles_bounds;
static int loaded_vehicle_world = -1;
static void choose_vehicle_art(void)
{
    int world = game.vehicle_world, k;
    const uint32_t (*bank)[32][32][5] = world == BIOME_VOLCANIC ? classic_volcanic_vehicles :
        world == BIOME_MOON ? classic_moon_vehicles : classic_classic_vehicles;
    uint32_t *dest = (uint32_t *)sprite_chip;
    vehicle_bounds = world == BIOME_VOLCANIC ? classic_volcanic_vehicles_bounds :
        world == BIOME_MOON ? classic_moon_vehicles_bounds : classic_classic_vehicles_bounds;
    if (world == loaded_vehicle_world || !sprite_chip) return;
    WaitBlit();
    for (k = 0; k < 6; k++) dest = load_sprite_bank(dest, DIRECTIONS, bank[k]);
    loaded_vehicle_world = world;
}
#endif
int art_world;
static void choose_world_art(void)
{
    art_world = game.biome < BIOME_COUNT ? game.biome : 0;
#ifndef OFP_LOCAL_DUEL
    ground_tiles = art_world == BIOME_CLASSIC ? classic_tiles : world_tiles[art_world - 1];
#ifdef OFP_AMIGA_ONLINE
    ground_structures = classic_structures;
    if (art_world == BIOME_CLASSIC && original_look) {
        ground_tiles = original_tiles;
        ground_structures = original_structures;
    }
#endif
#else
    ground_tiles = classic_tiles;
    art_world = 0;
#endif
}

/* Match an isolated two-by-two crown even when its top left tile is odd. */
static int large_canopy_quarter(int tx, int ty)
{
    static const int8_t quarter[16] = { -1, -1, -1, 2, -1, -1, 0, -1, -1, 3, -1, -1, 1, -1, -1, -1 };
    int q, x, y;
    if (art_world != BIOME_CLASSIC && art_world != BIOME_WINTER) return -1;
    q = quarter[terrain_art[ty][tx] & 15];
    if (q < 0) return -1;
    x = tx - (q & 1);
    y = ty - (q >> 1);
    if (x < 0 || y < 0 || x + 1 >= game.map.w || y + 1 >= game.map.h) return -1;
    return terrain_art[y][x] == ((TILE_CANOPY << 4) | 6) &&
           terrain_art[y][x + 1] == ((TILE_CANOPY << 4) | 12) &&
           terrain_art[y + 1][x] == ((TILE_CANOPY << 4) | 3) &&
           terrain_art[y + 1][x + 1] == ((TILE_CANOPY << 4) | 9) ? q : -1;
}

static const UBYTE (*canopy_art(int cell_x, int cell_y, int quarter))[TILE_PX]
{
    int tx = cell_x / 2, ty = cell_y / 2;
    int variant = terrain_art[ty][tx] & 15;
    if (quarter >= 0) {
        int part = (2 * (quarter >> 1) + (cell_y & 1)) * 4 +
                   2 * (quarter & 1) + (cell_x & 1);
#ifndef OFP_LOCAL_DUEL
        if (art_world == BIOME_WINTER) return large_winter_canopy[part];
#endif
        return large_classic_canopy[part];
    }
#ifdef OFP_AMIGA_AGA
    variant |= ((tx ^ ty ^ (tx >> 1)) & 1) << 4;
#endif
    return canopy_tiles[art_world][(variant << 2) | ((cell_y & 1) << 1) | (cell_x & 1)];
}

void reset_match_view(void)
{
    ai_local_reset();
    choose_world_art();
#if defined(OFP_AMIGA_ONLINE) && !defined(OFP_AMIGA_AGA)
    choose_vehicle_art();
#endif
    int x, y;
    for (y = 0; y < game.map.h; y++)
        for (x = 0; x < game.map.w; x++) terrain_art[y][x] = art_at(x, y);
    memset(tile_cache, 0xff, sizeof tile_cache);
    memset(camera_ready, 0, sizeof camera_ready);
    hud_ready = 0;
}

static const char *practice_map(void)
{
    static char map[ARENA_SIZE * (ARENA_SIZE + 1) + 96];
    static const char header[] = "OFPMAP 1\nsize=small\narena=yes\nspawns=27,32 36,32 32,24 32,39\ntiles\n";
    char *next = map;
    int x, y;

    memcpy(next, header, sizeof header - 1);
    next += sizeof header - 1;
    for (y = 0; y < ARENA_SIZE; y++) {
        for (x = 0; x < ARENA_SIZE; x++) {
            char tile = '.';
            int dx = x < 32 ? 31 - x : x - 32;
            int dy = y < 32 ? 31 - y : y - 32;
            if (x == 0 || y == 0 || x == ARENA_SIZE - 1 || y == ARENA_SIZE - 1) tile = '^';
            else if (dx == 0 || dy == 0) tile = '=';
            else if (dx >= 3 && dx <= 6 && dy >= 3 && dy <= 4) tile = '#';
            else if (dx >= 11 && dx <= 14 && dy >= 8 && dy <= 11) tile = 'T';
            else if (dx >= 17 && dx <= 20 && dy >= 17 && dy <= 20) tile = '~';
            *next++ = tile;
        }
        *next++ = '\n';
    }
    strcpy(next, "end\n");
    return map;
}

int random_map(int map)
{
    return map == RANDOM_SMALL || map == RANDOM_MEDIUM || map == RANDOM_LARGE;
}

#ifdef OFP_AMIGA_ONLINE
int local_map_count = LOCAL_MAPS;
int my_map_size = -1, my_map_biome, my_map_arena, my_map_mode = -1;

int selected_map_size(int map)
{
    if (map >= LOCAL_MAPS) return catalog_maps[map - LOCAL_MAPS].size;
    if (map == MY_MAP) return my_map_size;
    if (map == RANDOM_SMALL || map == PRACTICE_MAP) return SIZE_SMALL;
    if (map == RANDOM_MEDIUM) return SIZE_MEDIUM;
    return SIZE_LARGE;
}

int map_fits_filters(int map, int world, int mode, int size)
{
    int biome = map >= LOCAL_MAPS ? catalog_maps[map - LOCAL_MAPS].biome :
                map == MY_MAP ? my_map_biome : BIOME_CLASSIC;
    unsigned modes = map >= LOCAL_MAPS ? catalog_maps[map - LOCAL_MAPS].modes :
                     map == PRACTICE_MAP ? 120u : map == MY_MAP ? 95u : 1u;
    int map_size = selected_map_size(map);
    if (world == BIOME_CLASSIC && !random_map(map) &&
        map != 0 && map != 1 && map != SWITCHBACK && map != TWIN_BOULEVARDS && map != MY_MAP) return 0;
    if (random_map(map)) return map_size == (size == SIZE_ANY ? SIZE_LARGE : size);
    return map_size_fits(map_size) &&
           (size == SIZE_ANY || map_size == size) &&
           biome == world && ((map == MY_MAP && my_map_mode >= 0) || (modes & (1u << mode)));
}

int first_named_map(int world, int mode, int size)
{
    int map;
    for (map = 0; map < local_map_count; map++)
        if (!random_map(map) && map_fits_filters(map, world, mode, size)) return map;
    return -1;
}

unsigned local_world_modes(int world)
{
    unsigned modes = world == BIOME_CLASSIC ? 1u : 0u;
    int i;
    if (world == BIOME_CLASSIC) return 1u;                /* Original: the 1987 game */
    for (i = LOCAL_MAPS; i < local_map_count; i++)
        if (catalog_maps[i - LOCAL_MAPS].biome == world)
            modes |= catalog_maps[i - LOCAL_MAPS].modes;
    if (my_map_size >= 0 && my_map_biome == world) modes |= 1u;
    return modes ? modes : 1u;                           /* a world with only generated maps */
}

int random_for_size(int size)
{
    static const int choices[] = { RANDOM_SMALL, RANDOM_MEDIUM, RANDOM_LARGE };
    return choices[size == SIZE_ANY ? SIZE_LARGE : size];
}


const char *map_label(int map)
{
    return map < LOCAL_MAPS ? map_names[map] : catalog_maps[map - LOCAL_MAPS].name;
}

static const char *catalog_map_text(int index)
{
    static char text[MAPFILE_MAX];
    char path[112];
    FILE *f;
    size_t n;
    snprintf(path, sizeof path, "PROGDIR:maps/catalog/%s", catalog_maps[index].file);
    f = fopen(path, "r");
    if (!f) return NULL;
    n = fread(text, 1, sizeof text - 1, f);
    if (ferror(f) || fgetc(f) != EOF) n = 0;
    fclose(f);
    text[n] = 0;
    return n ? text : NULL;
}
#endif

const char *local_match_setup(MatchSetup *setup, uint32_t match_seed)
{
    static const char *const classic_maps[LOCAL_MAPS] = {
        [0] = classic_dust_crossing, [1] = classic_perimeter_road,
        [SWITCHBACK] = classic_switchback, [TWIN_BOULEVARDS] = classic_twin_boulevards
    };
    const char *map = map_choice == PRACTICE_MAP ? practice_map() :
                      map_choice < LOCAL_MAPS ? classic_maps[map_choice] : NULL;

    uint8_t x = local_modes[mode_choice % LOCAL_MODES].extras;
#ifdef OFP_AMIGA_ONLINE
    if (map_choice == MY_MAP) map = my_map_text();       /* (none saved yet: the first original level) */
    if (map_choice == MY_MAP && !map) map = classic_dust_crossing;
    if (map_choice >= LOCAL_MAPS) {
        map = catalog_map_text(map_choice - LOCAL_MAPS);
    }
#endif

    memset(setup, 0, sizeof *setup);
    setup->seed = match_seed;
    setup->players = 2;
    setup->versus = 1;
    setup->relaxed_pace = 1;
#ifdef OFP_AMIGA_ONLINE
    setup->lives = (uint8_t)settings.match_lives;
    setup->no_powerups = !settings.powerups;
    setup->time_limit = (uint16_t)(settings.match_minutes * 60);
#endif
#ifdef OFP_AMIGA_ONLINE
    setup->biome = (uint8_t)world_choice;
#else
    setup->biome = BIOME_CLASSIC;
#endif   /* (a random battlefield: its world; a map file: its own) */
    if (map_choice == RANDOM_SMALL) setup->map_size = SIZE_SMALL + 1;      /* (a random battlefield: its size) */
    if (map_choice == RANDOM_MEDIUM) setup->map_size = SIZE_MEDIUM + 1;
    if (map_choice == RANDOM_LARGE) setup->map_size = SIZE_LARGE + 1;
    if ((map_choice == PRACTICE_MAP
#ifdef OFP_AMIGA_ONLINE
         || (map_choice == MY_MAP && my_map_arena)
         || (map_choice >= LOCAL_MAPS && catalog_maps[map_choice - LOCAL_MAPS].arena)
#endif
        ) && !(x & (EXTRA_TDM | EXTRA_KILLCONF | EXTRA_ANNIHILATION | EXTRA_WARLORD)))
        x = EXTRA_ANNIHILATION;                          /* (the practice arena: no camps, so a deathmatch) */
    setup->extras = x;
    setup->team[1] = TEAM_ENEMY;
    setup->cls[0] = (uint8_t)cls_choice;
    setup->cls[1] = TANK_MEDIUM;
#ifdef OFP_AMIGA_ONLINE
    setup->cls[0] = (uint8_t)tank_chassis(cls_choice);
    if (cls_choice == TANK_RHINO && !local_classic()) setup->rocket_tanks = 1;
    if (local_classic()) setup->camp_garages = 3;
    setup->bot_commands = 1;
    if (solo && !light_local_ai()) setup->bot[1] = level_choice == 0 ? AI_EASY : level_choice == 2 ? AI_HARD : AI_NORMAL;
    setup->steer[0] = (uint8_t)menu_steer;
    setup->steer[1] = solo ? STEER_TANK : (uint8_t)profile_steering(1);
#endif
    return map;
}

void new_match(int side, int second_cls)
{
    MatchSetup setup;
    const char *map = local_match_setup(&setup, seed++);
    setup.team[0] = (uint8_t)side;
    setup.team[1] = (uint8_t)(1 - side);
    setup.leader_mask = solo ? 1 : 3;                    /* one human per team in local play */
    setup.cls[1] = (uint8_t)second_cls;
#ifdef OFP_AMIGA_ONLINE
    original_look = world_choice == BIOME_CLASSIC;
    setup.cls[1] = (uint8_t)tank_chassis(second_cls);
    if (second_cls == TANK_RHINO && !local_classic()) setup.rocket_tanks |= 2;
#endif
    if (!map && !random_map(map_choice)) { quit = 1; return; }
    if (match_start(&game, &setup, map) == 0) {
#ifdef OFP_AMIGA_ONLINE
        if (map_choice != PRACTICE_MAP) rec_begin(&setup, map);   /* (recorded: PROGDIR:replays/; not the practice arena,
                                                            which is changed below, after its start: a replay can't know) */
#endif
    } else {
        quit = 1;
        return;
    }
    if (map_choice == PRACTICE_MAP) {
        memset(game.mines, 0, sizeof game.mines);
        game.num_props = 0;
        game.tanks[0].angle = game.tanks[0].prev_angle = ANGLE_EAST;
        game.tanks[1].angle = game.tanks[1].prev_angle = ANGLE_WEST;
    }
#ifdef OFP_AMIGA_ONLINE
    reset_field_bots();
#endif
    bot_avoid = 0;
    bot_turn = IN_RIGHT;
    reset_match_view();
}

static void tile_pixels(int offset, const UBYTE pattern[DEPTH][TILE_PX],
                        const UBYTE overlay[DEPTH + 1][TILE_PX])
{
    int plane, row;
    int stride = frame->BytesPerRow;
    for (plane = 0; plane < DEPTH; plane++) {
        UBYTE *p = frame->Planes[plane] + offset;
        const UBYTE *pixels = pattern[plane];
        if (overlay) {
            for (row = 0; row < TILE_PX; row++, p += stride)
                *p = (pixels[row] & (UBYTE)~overlay[0][row]) | overlay[plane + 1][row];
        } else {
            for (row = 0; row < TILE_PX; row++, p += stride) *p = *pixels++;
        }
    }
}

/* Restore the two 8-pixel cells covered by one terrain half-row. */
static void restore_tile_half(const View *view, int offset, int col, int row,
                              uint32_t *cache, uint32_t key, unsigned quadrant,
                              const Structure *structure, unsigned structure_quadrant,
                              int damaged, int canopy, int large_tree, int cell_x, int cell_y)
{
    int side;
#ifdef OFP_AMIGA_ONLINE
    if (view->damage) add_view_damage(view->damage, col * TILE_PX, row * TILE_PX,
                                      (col + 2) * TILE_PX, (row + 1) * TILE_PX);
#else
    (void)view; (void)col; (void)row;
#endif
    for (side = 0; side < 2; side++) {
        const UBYTE (*overlay)[TILE_PX] = NULL;
        if (cache[side] == (key | side)) continue;
        cache[side] = key | side;
        if (canopy) overlay = canopy_art(cell_x + side, cell_y, large_tree);
        else if (structure) overlay = ground_structures[structure->type - 1][damaged][structure_quadrant + side];
#ifndef OFP_AMIGA_AGA
        if (quadrant >= TILE_ICE * 64 && quadrant < TILE_COUNT * 64)
            tile_pixels(offset + side, surface_tiles[quadrant / 64 - TILE_ICE][(quadrant + side) & 63], overlay);
        else
#endif
            tile_pixels(offset + side, ground_tiles[quadrant >= TILE_ICE * 64 ? TILE_SAND * 64 + ((quadrant + side) & 63) : quadrant + side], overlay);
    }
}

/* Word-aligned native bitplanes. A shifted copy reads and writes one extra
 * word at the right edge so the first word of every descending row can seed
 * the shifter. Its caller owns and restores that strip of the destination. */
void blit_planar_words(struct BitMap *src, int sx, int sy, struct BitMap *dst, int dx, int dy,
                     int width, int height, int shift)
{
#ifdef OFP_ART_PREVIEW
    BltBitMap(src, sx + shift, sy, dst, dx, dy, width, height, 0xc0, PLANE_MASK, NULL);
#else
    volatile struct Custom *chip = (volatile struct Custom *)0xdff000;
    int bytes = width / 8 + (shift ? 2 : 0);
    int from = sy * src->BytesPerRow + sx / 8, to = dy * dst->BytesPerRow + dx / 8;
    int descending = shift || (src == dst && to > from), plane;
    if (descending) {
        from += (height - 1) * src->BytesPerRow + bytes - 2;
        to += (height - 1) * dst->BytesPerRow + bytes - 2;
    }
    OwnBlitter();
    WaitBlit();
    chip->bltcon0 = (UWORD)(BC0F_SRCA | BC0F_DEST | A_TO_D | (shift << ASHIFTSHIFT));
    chip->bltcon1 = descending ? BC1F_DESC : 0;
    chip->bltafwm = chip->bltalwm = 0xffff;
    chip->bltamod = src->BytesPerRow - bytes;
    chip->bltdmod = dst->BytesPerRow - bytes;
    for (plane = 0; plane < DEPTH; plane++) {
        chip->bltapt = src->Planes[plane] + from;
        chip->bltdpt = dst->Planes[plane] + to;
        chip->bltsize = (UWORD)((height << HSIZEBITS) | (bytes / 2));
        WaitBlit();
    }
    DisownBlitter();
#endif
}

/* Scroll the intact portion of the viewport in chip RAM. Its cache moves
 * with it, including the dirty cells left by sprites in the previous frame. */
static void scroll_view(int player, int columns, int rows, int x, int y)
{
    int dx = x - camera_x[player], dy = y - camera_y[player];
    int sx = dx > 0 ? dx : 0, sy = dy > 0 ? dy : 0;
    int tx = dx < 0 ? -dx : 0, ty = dy < 0 ? -dy : 0;
    int wide = columns - sx - tx, high = rows - sy - ty;
    int left = player * FIELD_STRIDE / TILE_PX, row;
    if (camera_ready[player] && (dx || dy)) {
        if (wide <= 0 || high <= 0) {
            for (row = 0; row < rows; row++)
                memset(tile_cache[row] + left, 0xff, columns * sizeof tile_cache[0][0]);
        } else {
#ifdef OFP_NATIVE_AGA
            blit_planar_words(frame, (left + sx) * TILE_PX, FIELD_TOP + sy * TILE_PX,
                              frame, (left + tx) * TILE_PX, FIELD_TOP + ty * TILE_PX,
                              wide * TILE_PX, high * TILE_PX, 0);
#else
            if (!dx) {
                /* An in-place vertical blit only overlaps on whole scanlines. */
                BltBitMap(frame, left * TILE_PX, FIELD_TOP + sy * TILE_PX,
                          frame, left * TILE_PX, FIELD_TOP + ty * TILE_PX,
                          wide * TILE_PX, high * TILE_PX, 0xc0, PLANE_MASK, NULL);
            } else {
                /* Horizontal and diagonal scrolls retain a separate source. */
                BltBitMap(frame, (left + sx) * TILE_PX, FIELD_TOP + sy * TILE_PX,
                          scroll_scratch, 0, 0, wide * TILE_PX, high * TILE_PX, 0xc0, PLANE_MASK, NULL);
                BltBitMap(scroll_scratch, 0, 0, frame, (left + tx) * TILE_PX, FIELD_TOP + ty * TILE_PX,
                          wide * TILE_PX, high * TILE_PX, 0xc0, PLANE_MASK, NULL);
            }
            WaitBlit();
#endif
            for (row = 0; row < high; row++) {
                int i = ty > sy ? high - 1 - row : row;
                uint32_t *dest = tile_cache[ty + i] + left;
                memmove(dest + tx, tile_cache[sy + i] + left + sx, wide * sizeof *dest);
                memset(dest, 0xff, tx * sizeof *dest);
                memset(dest + tx + wide, 0xff, (columns - tx - wide) * sizeof *dest);
            }
            for (row = 0; row < rows; row++)
                if (row < ty || row >= ty + high)
                    memset(tile_cache[row] + left, 0xff, columns * sizeof tile_cache[0][0]);
        }
    }
    camera_x[player] = x;
    camera_y[player] = y;
    camera_ready[player] = 1;
}

View update_terrain(int player)
{
    View view;
    int row, x, y, offset, columns;
    int row_stride = frame->BytesPerRow * TILE_PX;
    int visible_width = solo ? WIDTH : VIEW, visible_height = BOTTOM;
#ifdef OFP_AMIGA_ONLINE
    static ViewDamage classic_damage;
    view.damage = NULL;
    if (classic_solo_view()) {
        visible_width = CLASSIC_VIEW;
        classic_damage = (ViewDamage){ BUFFER_W, HEIGHT, 0, 0 };
        view.damage = &classic_damage;
    }
    if (editor_on) { visible_width = EDITOR_WIDTH; visible_height = EDITOR_FIELD_H; }
    if (title_on) visible_height = HEIGHT;
#endif
    view.width = visible_width + SCROLL_WORD;
    columns = view.width / TILE_PX;
    view.rows = visible_height / TILE_PX + 1;
#ifdef OFP_AMIGA_ONLINE
    if (editor_on) editor_camera(&x, &y);
    else game_camera(&game, online_playing ? online_view_tank() : replay_on ? replay_follow : title_on ? (game.tick / 500) % 2 : player,
                visible_width, visible_height, 256, &x, &y);
#else
    game_camera(&game, player, visible_width, visible_height, 256, &x, &y);
#endif
    /* Keep overlapping terrain blits word aligned. Crop the padded
     * result at the exact camera position when presenting it. */
    view.phase_x = x % SCROLL_WORD;
    view.phase_y = y % TILE_PX;
    x = (x - view.phase_x) / TILE_PX;
    y /= TILE_PX;
    scroll_view(player, columns, view.rows, x, y);
    view.left = player * FIELD_STRIDE;
    view.world_x = x * TILE_PX;
    view.world_y = y * TILE_PX;
    offset = FIELD_TOP * frame->BytesPerRow + view.left / 8;
    for (row = 0; row < view.rows;) {
        int cell_y = y + row, tile_y = cell_y >> 1, half = (cell_y & 1) << 1;
        int pair = !half && row + 1 < view.rows;
        const uint8_t *tiles, *art;
        const uint16_t *sids;
        uint32_t *cache = tile_cache[row] + view.left / TILE_PX;
        int col, end = game.map.w * 2 - x;
        if (end > columns) end = columns;
        /* At the far map edge the extra row or column lies outside the
         * visible crop, which ends exactly at the boundary. */
        if (tile_y >= game.map.h) break;
        tiles = game.map.type[tile_y];
        art = terrain_art[tile_y];
        sids = game.map.sid[tile_y];
        /* A map tile shares its art and damage state across all four cells. */
        for (col = 0; col < end; col += 2) {
            int cell_x = x + col, tile_x = cell_x >> 1;
            unsigned type = tiles[tile_x], index = art[tile_x];
            unsigned quadrant, structure_quadrant = 0, sand = (tile_x * 7 + tile_y * 11) & 15;
            uint32_t key = 0;
            const Structure *structure = NULL;
            int damaged = 0, large_tree = -1, canopy;
            if (type != (index >> 4)) {
                refresh_art(tile_x, tile_y);
                index = art[tile_x];
            }
            if ((type == TILE_BUILDING || type == TILE_FLOOR) && sids[tile_x]) {
                unsigned sid = sids[tile_x];
                const StructInfo *info;
                structure = &game.map.st[sid - 1];
                info = &struct_info[structure->type];
                damaged = info->hp && structure->hp * 2 <= info->hp;
                structure_quadrant = (cell_y - structure->y * 2) * 8 + cell_x - structure->x * 2;
                index = sand;
                key = ((uint32_t)sid << 11) | ((uint32_t)damaged << 10);
                if (info->flags & SF_GATE) {
                    structure = NULL;
                    index = (TILE_ROAD << 4) | 15;
                }
            }
            canopy = (index >> 4) == TILE_CANOPY;
            if (canopy) {
                large_tree = large_canopy_quarter(tile_x, tile_y);
                key |= (uint32_t)(large_tree + 1) << 27;
            }
            key |= (index << 2) | half;
            quadrant = ((canopy ? sand : index) << 2) | half;
            if (cache[col] != key || cache[col + 1] != (key | 1))
                restore_tile_half(&view, offset + col, col, row, cache + col, key, quadrant,
                                  structure, structure_quadrant, damaged, canopy, large_tree, cell_x, cell_y);
            if (pair) {
                uint32_t *below = cache + BUFFER_W / TILE_PX + col;
                if (below[0] != (key | 2) || below[1] != (key | 3))
                    restore_tile_half(&view, offset + row_stride + col, col, row + 1, below, key | 2,
                                      quadrant | 2, structure, structure_quadrant + 8, damaged,
                                      canopy, large_tree, cell_x, cell_y + 1);
            }
        }
        row += 1 + pair;
        offset += row_stride * (1 + pair);
    }
    return view;
}

static void mark(const View *view, fx x, fx y, int radius, int pen);

/* Dirty spans for the current view, in 8-pixel cells. Overlays only
 * need to inspect rows and columns touched since the ground was drawn. */
static uint8_t dirty_first[ROWS], dirty_end[ROWS];

static void dirty_cells(const View *view, int x0, int y0, int x1, int y1)
{
    int first = x0 / TILE_PX, end = x1 / TILE_PX + 1, row;
#ifdef OFP_AMIGA_ONLINE
    if (view->damage) add_view_damage(view->damage, x0, y0, x1 + 1, y1 + 1);
#endif
    for (row = y0 / TILE_PX; row <= y1 / TILE_PX; row++) {
        uint32_t *cache = tile_cache[row] + view->left / TILE_PX;
        int col;
        for (col = first; col < end; col++) cache[col] = 0xffffffffu;
        if (first < dirty_first[row]) dirty_first[row] = (uint8_t)first;
        if (end > dirty_end[row]) dirty_end[row] = (uint8_t)end;
    }
}

static void draw_sprite(const View *view, fx world_x, fx world_y,
                        unsigned frame_index, const uint8_t bounds[4])
{
    int x = fx_to_int(world_x) - view->world_x - TANK_SIZE / 2;
    int y = fx_to_int(world_y) - view->world_y - TANK_SIZE / 2;
    int x0 = x + bounds[0], y0 = y + bounds[1];
    int x1 = x + bounds[2], y1 = y + bounds[3];
    int plane;
    UBYTE *mask = sprite_chip + frame_index * TANK_FRAME_BYTES;
    struct BitMap sprite;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > view->width) x1 = view->width;
    if (y1 > view->rows * TILE_PX) y1 = view->rows * TILE_PX;
    if (x0 >= x1 || y0 >= y1) return;

    dirty_cells(view, x0, y0, x1 - 1, y1 - 1);

    InitBitMap(&sprite, DEPTH, TANK_SIZE, TANK_SIZE);
    for (plane = 0; plane < DEPTH; plane++)
        sprite.Planes[plane] = mask + (plane + 1) * TANK_PLANE_BYTES;
    BltMaskBitMapRastPort(&sprite, x0 - x, y0 - y, &canvas,
                         view->left + x0, FIELD_TOP + y0, x1 - x0, y1 - y0, 0xe0, mask);
}

enum { GATE_POST, GATE_PANEL, GATE_ARM, GATE_BOLLARD };

static void draw_battle(const View *view, int art, int phase, fx x, fx y)
{
    draw_sprite(view, x, y, BATTLE_FIRST + art * BATTLE_FRAMES + phase,
                classic_battle_bounds[art][phase]);
}

static void draw_tank(const View *view, const Tank *tank)
{
    int team = tank->team & 1;
#ifdef OFP_AMIGA_ONLINE
    if (tank->rocket_launcher) {
        int direction = ((tank->angle + 0x800u) >> 12) & (ROCKET_DIRECTIONS - 1);
        draw_sprite(view, tank->x, tank->y, ROCKET_FIRST + team * ROCKET_DIRECTIONS + direction,
                    classic_rocket_tanks_bounds[team][direction]);
    } else
#endif
    {
        int direction = ((tank->angle + 0x400u) >> 11) & 31;
        draw_sprite(view, tank->x, tank->y, 
#if defined(OFP_AMIGA_ONLINE) && !defined(OFP_AMIGA_AGA)
                    (tank->cls * 2 + team) * DIRECTIONS + direction, vehicle_bounds[tank->cls * 2 + team][direction]
#else
                    team * DIRECTIONS + direction, classic_tanks_bounds[team][direction]
#endif
                    );
    }
    if (tank->has_flag)
        mark(view, tank->x, tank->y - FX(10), 2, team == 0 ? TEAM_TWO : TEAM_ONE);
}

uint32_t *load_sprite_bank(uint32_t *dest, int count,
                                  const uint32_t (*source)[TANK_SIZE][DEPTH + 1])
{
    int sprite, plane, row;
    for (sprite = 0; sprite < count; sprite++)
        for (plane = 0; plane <= DEPTH; plane++)
            for (row = 0; row < TANK_SIZE; row++)
                *dest++ = source[sprite][row][plane];
    return dest;
}

/* Write straight into the planes. Restore the marked tiles next frame. */
static void pattern_box(const View *view, int wx0, int wy0, int wx1, int wy1, int pen, int sync, UBYTE pattern)
{
    int x0 = wx0 - view->world_x, x1 = wx1 - view->world_x, y0 = wy0 - view->world_y, y1 = wy1 - view->world_y;
    int byte, plane, row, stride = frame->BytesPerRow;
    if (x1 < 0 || x0 >= view->width || y1 < 0 || y0 >= view->rows * TILE_PX) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= view->width) x1 = view->width - 1;
    if (y0 < 0) y0 = 0;
    if (y1 >= view->rows * TILE_PX) y1 = view->rows * TILE_PX - 1;
    dirty_cells(view, x0, y0, x1, y1);
    if (sync) WaitBlit();
    for (byte = x0 / 8; byte <= x1 / 8; byte++) {
        int offset = (FIELD_TOP + y0) * stride + view->left / 8 + byte;
        UBYTE mask = pattern;
        if (byte == x0 / 8) mask &= 0xff >> (x0 & 7);
        if (byte == x1 / 8) mask &= 0xff << (7 - (x1 & 7));
        for (plane = 0; plane < DEPTH; plane++) {
            UBYTE *pixels = frame->Planes[plane] + offset;
            UBYTE color = pen & (1 << plane) ? mask : 0;
            for (row = y0; row <= y1; row++, pixels += stride)
                *pixels = (*pixels & (UBYTE)~mask) | color;
        }
    }
}

static void box_pixels(const View *view, int wx0, int wy0, int wx1, int wy1, int pen, int sync)
{
    pattern_box(view, wx0, wy0, wx1, wy1, pen, sync, 0xff);
}

void box(const View *view, int wx0, int wy0, int wx1, int wy1, int pen)
{
    box_pixels(view, wx0, wy0, wx1, wy1, pen, 1);
}

static void mark(const View *view, fx x, fx y, int radius, int pen)
{
    int px = fx_to_int(x), py = fx_to_int(y);
    box(view, px - radius, py - radius, px + radius, py + radius, pen);
}

/* an area's outline, a tile's middle and a radius (tiles) out: four thin boxes */
static void outline(const View *view, int tx, int ty, int radius, int pen)
{
    int cx = tx * TILE_SIZE + TILE_SIZE / 2, cy = ty * TILE_SIZE + TILE_SIZE / 2, r = radius * TILE_SIZE;
    box(view, cx - r, cy - r, cx + r, cy - r + 1, pen);
    box(view, cx - r, cy + r - 1, cx + r, cy + r, pen);
    box(view, cx - r, cy - r, cx - r + 1, cy + r, pen);
    box(view, cx + r - 1, cy - r, cx + r, cy + r, pen);
    box(view, cx - 1, cy - 1, cx + 1, cy + 1, pen);
}

static int side_pen(int side) { return side == 0 ? TEAM_ONE : side == 1 ? TEAM_TWO : WHITE; }

/* battle marks: scorches, a building's debris, where soldiers fell (OPTIONS: MARKS: NO BLOOD, not those);
 * only drawn (the rules don't know of them); a new match, a clean field */
#define AMARKS 96
struct { int16_t x, y; uint8_t kind; } amarks[AMARKS];
int namarks, amark_next, marks_no_blood;
void marks_add(const Effect *e)
{
    if (e->kind != EFF_EXPLOSION && e->kind != EFF_BLAST && e->kind != EFF_SPLAT) return;
    amarks[amark_next].x = (int16_t)fx_to_int(e->x);
    amarks[amark_next].y = (int16_t)fx_to_int(e->y);
    amarks[amark_next].kind = e->kind;
    amark_next = (amark_next + 1) % AMARKS;
    if (namarks < AMARKS) namarks++;
}

static void effect_rect(const View *view, int x0, int y0, int x1, int y1, int pen)
{
#ifdef OFP_AMIGA_ONLINE
    if (!view) {
        fill(pen, x0, y0, x1, y1);
        return;
    }
#endif
    box_pixels(view, x0, y0, x1, y1, pen, 0);
}

static const int8_t blood_drops[][2] = {
    { -8, -3 }, { -6, 4 }, { -3, -7 }, { 4, -5 }, { 8, 2 }, { 4, 5 }, { 1, 6 }
};

static void draw_blood_drops(const View *view, int x, int y, int age)
{
    int i, mirror = ((x ^ y) & 1) ? -1 : 1;
    for (i = 0; i < (int)(sizeof blood_drops / sizeof blood_drops[0]); i++) {
        int dx = blood_drops[i][0] * mirror, dy = blood_drops[i][1];
        int px = x + dx + dx * age / 12, py = y + dy + dy * age / 12;
        effect_rect(view, px, py, px + (i & 1), py, BLOOD_PEN);
    }
}

void draw_blood_stain(const View *view, int x, int y)
{
    static const int8_t spans[][2] = {
        { -1, 1 }, { -3, 2 }, { -4, 3 }, { -2, 4 }, { -3, 2 }, { -1, 1 }
    };
    int i, mirror = ((x ^ y) & 1) ? -1 : 1;
    if (view) WaitBlit();
    for (i = 0; i < (int)(sizeof spans / sizeof spans[0]); i++) {
        int x0 = mirror > 0 ? spans[i][0] : -spans[i][1];
        int x1 = mirror > 0 ? spans[i][1] : -spans[i][0];
        effect_rect(view, x + x0, y + i - 3, x + x1, y + i - 3, BLOOD_PEN);
    }
    effect_rect(view, x - 1, y + 1, x + 1, y + 1, INK);
    draw_blood_drops(view, x, y, 0);
}

void draw_simple_blast(const View *view, int x, int y, int radius, int hot)
{
    int inner = (radius + 1) / 2;
    if (view) WaitBlit();
    effect_rect(view, x - inner, y - radius, x + inner, y + radius, TEAM_ONE);
    effect_rect(view, x - radius, y - inner, x + radius, y + inner, TEAM_ONE);
    effect_rect(view, x - inner, y - inner, x + inner, y + inner, GOLD);
    if (hot) effect_rect(view, x - 1, y - 1, x + 1, y + 1, WHITE);
}

static void draw_surface_trails(const View *view)
{
    static struct { int16_t x, y; uint32_t tick; uint8_t pen, used; } trails[64];
    static uint32_t last_tick;
    static unsigned next;
    int i;
    if (game.tick < last_tick) { memset(trails, 0, sizeof trails); next = 0; }
    if (game.tick != last_tick && !(game.tick & 3) && game.enhanced_rules) {
        last_tick = game.tick;
        for (i = 0; i < game.num_tanks; i++) {
            const Tank *t = &game.tanks[i];
            int x = fx_to_int(t->x), y = fx_to_int(t->y), tile = map_get(&game.map, x >> TILE_SHIFT, y >> TILE_SHIFT);
            if (!t->active || t->respawn || !t->speed || (tile != TILE_ICE && tile != TILE_OIL)) continue;
            trails[next].x = (int16_t)x; trails[next].y = (int16_t)y;
            trails[next].tick = game.tick; trails[next].pen = tile == TILE_ICE ? WHITE : INK; trails[next].used = 1;
            next = (next + 1) % 64;
        }
    }
    for (i = 0; i < 64; i++) if (trails[i].used && game.tick - trails[i].tick < 180) {
        int x = trails[i].x, y = trails[i].y;
        box(view, x-4, y, x-3, y+1, trails[i].pen);
        box(view, x+3, y, x+4, y+1, trails[i].pen);
    }
}

static void draw_marks(const View *view)
{
    int i;
    for (i = 0; i < namarks; i++) {
        int x = amarks[i].x, y = amarks[i].y, h = (x * 7 + y * 13) & 15;
        if (x < view->world_x - 12 || x > view->world_x + view->width + 12 || y < view->world_y - 12 || y > view->world_y + view->rows * TILE_PX + 12) continue;
        if (amarks[i].kind == EFF_SPLAT) {
            if (!marks_no_blood) draw_blood_stain(view, x, y);
        }
        else if (amarks[i].kind == EFF_BLAST) {      /* a building or tank went: a big scorch, debris round it */
            box(view, x - 6, y - 4, x + 6, y + 4, 12);
            box(view, x - 3, y - 2, x + 2, y + 2, 0);
            box(view, x - 9 + (h & 3), y + 5, x - 8 + (h & 3), y + 6, 4);
            box(view, x + 7, y - 6 + (h >> 2), x + 8, y - 5 + (h >> 2), 4);
        } else box(view, x - 3, y - 2, x + 3, y + 2, 12);   /* a shot's blast: a small scorch */
    }
}

/* the bases' gates (the desktop's rule): a gap of 2..5 open tiles in a straight wall, wall on both sides
 * beyond; grey posts at its ends, a striped barrier across, up (a stub) while an eligible tank is near */
static int wall_at(int x, int y) { return x >= 0 && y >= 0 && x < game.map.w && y < game.map.h && game.map.type[y][x] == TILE_WALL; }
static int open_at(int x, int y)
{
    int t = x >= 0 && y >= 0 && x < game.map.w && y < game.map.h ? game.map.type[y][x] : TILE_ROCK;
    if (t == TILE_ROAD && game.map.sid[y][x] &&
        (struct_info[game.map.st[game.map.sid[y][x] - 1].type].flags & SF_GATE)) return 0;
    return t == TILE_SAND || t == TILE_ROAD || t == TILE_FLOOR;
}
static void draw_blast_gate(const View *view, const Structure *gate)
{
    const StructInfo *info = &struct_info[gate->type];
    int x = gate->x * TILE_SIZE, y = gate->y * TILE_SIZE;
    int w = info->w * TILE_SIZE, h = info->h * TILE_SIZE;
    int vertical = gate->type == ST_GATE_V, offset;
    int open = game_gate_open(&game, x + w / 2, y + h / 2);
    int length = vertical ? h : w;
    if (!gate->hp) return;
    for (offset = 0; offset < length; offset += 8) {
        int end = offset == 0 || offset == length - 8;
        if (open && !end) continue;
        draw_battle(view, vertical ? BATTLE_GATE_V : BATTLE_GATE_H, end ? GATE_POST : GATE_PANEL,
                    FX(x + (vertical ? 8 : offset + 4)), FX(y + (vertical ? offset + 4 : 8)));
    }
}

static void draw_gates(const View *view)
{
    int tx0 = view->world_x / TILE_SIZE - 6, ty0 = view->world_y / TILE_SIZE - 6;
    int tx1 = (view->world_x + view->width) / TILE_SIZE + 1, ty1 = (view->world_y + view->rows * TILE_PX) / TILE_SIZE + 1, tx, ty, dir;
    for (ty = ty0; ty <= ty1; ty++)
        for (tx = tx0; tx <= tx1; tx++) {
            if (!wall_at(tx, ty)) continue;
            for (dir = 0; dir < 2; dir++) {
                int sx = dir ? 0 : 1, sy = dir ? 1 : 0, n = 0, ax, ay, bx, by, seg, len;
                if (!wall_at(tx - sx, ty - sy)) continue;
                while (n < 6 && open_at(tx + sx * (n + 1), ty + sy * (n + 1))) n++;
                if (n < 2 || n > 5 || !wall_at(tx + sx * (n + 1), ty + sy * (n + 1)) || !wall_at(tx + sx * (n + 2), ty + sy * (n + 2))) continue;
                ax = tx * TILE_SIZE + TILE_SIZE / 2; ay = ty * TILE_SIZE + TILE_SIZE / 2;
                bx = (tx + sx * (n + 1)) * TILE_SIZE + TILE_SIZE / 2; by = (ty + sy * (n + 1)) * TILE_SIZE + TILE_SIZE / 2;
                len = game_gate_open(&game, (ax + bx) / 2, (ay + by) / 2) ? 8 : dir ? by - ay : bx - ax;   /* (up: a stub at its post) */
                for (seg = 8; seg <= len && seg < (dir ? by - ay : bx - ax); seg += 8)
                    draw_battle(view, dir ? BATTLE_GATE_V : BATTLE_GATE_H, GATE_ARM,
                                FX(ax + sx * seg), FX(ay + sy * seg));
                draw_battle(view, dir ? BATTLE_GATE_V : BATTLE_GATE_H, GATE_BOLLARD, FX(ax), FX(ay));
                draw_battle(view, dir ? BATTLE_GATE_V : BATTLE_GATE_H, GATE_BOLLARD, FX(bx), FX(by));
            }
        }
}

/* the modes' own: conquest's points, the hardpoint, the hill, kill confirmed's tags, the warlords; and the
 * soldiers and helicopters (there, and to be seen, as on the desktop) */
static void draw_mode_objects(const View *view)
{
    int i, k;
    if (game.extras & EXTRA_CONQUEST)
        for (i = 0; i < game.num_cp; i++)
            outline(view, game.cp_tx[i], game.cp_ty[i], CP_RADIUS, side_pen(game.cp_owner[i]));
    if ((game.extras & EXTRA_HARDPOINT) && game.num_cp) {
        int a = game.hp_active, on[2] = { 0, 0 };
        fx px = FX(game.cp_tx[a] * TILE_SIZE + TILE_SIZE / 2), py = FX(game.cp_ty[a] * TILE_SIZE + TILE_SIZE / 2);
        for (k = 0; k < game.num_tanks; k++) {
            const Tank *t = &game.tanks[k];
            fx dx = t->x - px, dy = t->y - py;
            if (t->active && !t->respawn && dx < FX(CP_RADIUS * TILE_SIZE) && dx > -FX(CP_RADIUS * TILE_SIZE) &&
                dy < FX(CP_RADIUS * TILE_SIZE) && dy > -FX(CP_RADIUS * TILE_SIZE)) on[t->team & 1] = 1;
        }
        if (on[0] != on[1] || (game.tick / 8) % 2) outline(view, game.cp_tx[a], game.cp_ty[a], CP_RADIUS, on[0] != on[1] ? side_pen(on[0] ? 0 : 1) : GOLD);
    }
    if (game.extras & EXTRA_HILL) outline(view, game.hill_tx, game.hill_ty, HILL_R, side_pen(game.hill_holder));
    if (game.extras & EXTRA_KILLCONF)
        for (i = 0; i < MAX_TAGS; i++) {
            int x, y;
            if (!game.tags[i].active || (game.tags[i].ttl < 5 * TICK_RATE && (game.tick / 6) % 2)) continue;
            x = fx_to_int(game.tags[i].x); y = fx_to_int(game.tags[i].y);
            box(view, x - 1, y - 3, x + 1, y + 3, side_pen(game.tags[i].team));   /* (a little diamond: a cross) */
            box(view, x - 3, y - 1, x + 3, y + 1, side_pen(game.tags[i].team));
            box(view, x, y, x, y, WHITE);
        }
    if (game.extras & EXTRA_WARLORD)
        for (i = 0; i < 2; i++) {
            int w = game.jug[i], x, y;
            if (w < 0 || w >= game.num_tanks || !game.tanks[w].active || game.tanks[w].respawn) continue;
            x = fx_to_int(game.tanks[w].x); y = fx_to_int(game.tanks[w].y);
            box(view, x - 14, y - 14, x + 14, y - 13, GOLD); box(view, x - 14, y + 13, x + 14, y + 14, GOLD);
            box(view, x - 14, y - 14, x - 13, y + 14, GOLD); box(view, x + 13, y - 14, x + 14, y + 14, GOLD);
        }
    for (i = 0; i < MAX_MEN + MAX_EVACUEES; i++) {      /* soldiers, prisoners and first-aid arrivals */
        const Man *m = i < MAX_MEN ? &game.men[i] : &game.evacuees[i - MAX_MEN];
        if (m->kind == MAN_NONE) continue;
        {
            int variant = m->kind != MAN_SOLDIER ? 0 : m->team == TEAM_PLAYER ? 2 : 1;
            int direction = ((m->dir + 0x1000u) >> 13) & 7;
            int walking = m->x != m->prev_x || m->y != m->prev_y;
            unsigned stride_ticks = game.relaxed_pace ? 8 : 6;
            int sprite = direction * 2 + (walking ? (int)((game.tick / stride_ticks + (unsigned)i) & 1) : 0);
            draw_sprite(view, m->x, m->y, MAN_FIRST + variant * MAN_FRAMES + sprite,
                        classic_men_bounds[variant][sprite]);
        }
    }
    for (i = 0; i < MAX_HELIS; i++) {                 /* helicopters: a dark body, a gold rotor hub */
        const Heli *h = &game.helis[i];
        if (!h->active) continue;
        mark(view, h->x, h->y, 4, SHADE);
        mark(view, h->x, h->y, 1, GOLD);
    }
}

void fill(int pen, int x0, int y0, int x1, int y1);   /* (below: the HUD's) */
void label(int x, int y, int pen, const char *text);
/* The mode score, in the bottom HUD (redrawn only when it changes). */
int hud_left(int panel)
{
    return solo ? (WIDTH - VIEW) / 2 : panel * VIEW;
}

/* Composite leaves over objects only where a sprite dirtied the terrain. */
static void canopy_over(const View *view)
{
    int row, col, stride = frame->BytesPerRow;
    int cell_x = view->world_x / TILE_PX, cell_y = view->world_y / TILE_PX;
    WaitBlit();
    for (row = 0; row < view->rows && (cell_y + row) / 2 < game.map.h; row++) {
        int ty = (cell_y + row) / 2;
        const uint32_t *cache = tile_cache[row] + view->left / TILE_PX;
        for (col = dirty_first[row]; col < dirty_end[row] && (cell_x + col) / 2 < game.map.w; col++) {
            int tx = (cell_x + col) / 2, p, r, offset;
            const UBYTE (*leaves)[TILE_PX];
            if (cache[col] != 0xffffffffu || game.map.type[ty][tx] != TILE_CANOPY) continue;
            leaves = canopy_art(cell_x + col, cell_y + row,
                                large_canopy_quarter(tx, ty));
            offset = (FIELD_TOP + row * TILE_PX) * stride + view->left / 8 + col;
            for (r = 0; r < TILE_PX; r++, offset += stride) {
                UBYTE mask = leaves[0][r];
                for (p = 0; p < DEPTH; p++) {
                    UBYTE *pixel = frame->Planes[p] + offset;
                    *pixel = (*pixel & (UBYTE)~mask) | leaves[p + 1][r];
                }
            }
        }
    }
}

/* Garage roofs over the tanks parked inside (the desktop's order): where a sprite dirtied a garage's
 * cell, every row but the door's gets the building's pixels back on top. */
static void roofs_over(const View *view)
{
    int row, col, stride = frame->BytesPerRow;
    int cell_x = view->world_x / TILE_PX, cell_y = view->world_y / TILE_PX;
    WaitBlit();
    for (row = 0; row < view->rows && (cell_y + row) / 2 < game.map.h; row++) {
        int ty = (cell_y + row) / 2;
        const uint32_t *cache = tile_cache[row] + view->left / TILE_PX;
        for (col = dirty_first[row]; col < dirty_end[row] && (cell_x + col) / 2 < game.map.w; col++) {
            int tx = (cell_x + col) / 2, p, r, offset, damaged, sy, sx;
            unsigned sid;
            const Structure *st;
            const StructInfo *info;
            const UBYTE (*roof)[TILE_PX];
            if (cache[col] != 0xffffffffu ||
                (game.map.type[ty][tx] != TILE_BUILDING && game.map.type[ty][tx] != TILE_FLOOR)) continue;
            if (!(sid = game.map.sid[ty][tx])) continue;
            st = &game.map.st[sid - 1];
            info = &struct_info[st->type];
            if (st->type != ST_GARAGE) continue;
            sy = cell_y + row - st->y * 2;
            sx = cell_x + col - st->x * 2;
            if (sy >= (info->h - 1) * 2) continue;          /* (the door row: the tank shows, driving in or out) */
            damaged = info->hp && st->hp * 2 <= info->hp;
            roof = ground_structures[st->type - 1][damaged][sy * 8 + sx];
            offset = (FIELD_TOP + row * TILE_PX) * stride + view->left / 8 + col;
            for (r = 0; r < TILE_PX; r++, offset += stride) {
                UBYTE mask = roof[0][r];
                for (p = 0; p < DEPTH; p++) {
                    UBYTE *pixel = frame->Planes[p] + offset;
                    *pixel = (*pixel & (UBYTE)~mask) | roof[p + 1][r];
                }
            }
        }
    }
}

static void draw_weather(const View *view)
{
    int weather = weather_visual((Biome)game.biome, game.weather);
    int k, count, storm = weather == WEATHER_STORM, snow = weather == WEATHER_SNOW;
    int sand = weather == WEATHER_SANDSTORM, snowstorm = weather == WEATHER_SNOWSTORM;
    int clouds = weather == WEATHER_CLOUDY;
    int eruption = weather == WEATHER_ERUPTION, meteor = weather == WEATHER_METEOR;
    int time = game.map.time;
    uint32_t tick = game.tick;
    unsigned height = view->rows * TILE_PX - 7;
    if (!storm && !snow && !sand && !snowstorm && !clouds && !eruption && !meteor && weather != WEATHER_RAIN) return;
#ifdef OFP_AMIGA_ONLINE
    if (editor_on) { tick = net_ms() / 20; time = editor_time_preview(); }
#endif
    WaitBlit();
    if (clouds) {
        for (k = 0; k < 2; k++) {
            int x = view->world_x - 27 + (int)((tick / (4 + k) + 51 + 107 * k) % (unsigned)(view->width + 54));
            int y = view->world_y + (int)height * (k + 1) / 3;
            int row;
            for (row = 0; row < WEATHER_CLOUD_ROWS; row++) {
                int span = weather_cloud_span[row];
                pattern_box(view, x - span, y + row, x + span, y + row, INK, 0, row & 1 ? 0x22 : 0x88);
            }
        }
        return;
    }
    if (meteor) {
        int night = time == TIME_NIGHT;
        for (k = 0; k < (night ? 6 : 10); k++) {
            uint32_t h = (uint32_t)(k + 1) * 2654435761u;
            unsigned phase = (tick + (h >> 16)) % 96u;
            int x, y, tail = night ? 5 : 3;
            if (phase >= 28) continue;
            x = view->world_x + (int)(((h >> 8) + phase * 6) % (unsigned)view->width);
            y = view->world_y + (int)(((h >> 21) + phase * 3) % height);
            box_pixels(view, x - tail * 2, y - 2, x - tail - 1, y - 2, night ? TRACK : TEAM_ONE, 0);
            box_pixels(view, x - tail, y - 1, x - 1, y - 1, night ? WHITE : GOLD, 0);
            box_pixels(view, x, y, x + 1, y, WHITE, 0);
        }
        return;
    }
    if (eruption) {
        for (k = 0; k < 32; k++) {
            uint32_t h = (uint32_t)(k + 1) * 2654435761u;
            unsigned up = k < 10 ? 0u - tick * (1 + k % 3) : tick / 3;
            int x = view->world_x + (int)(((h >> 8) + tick * (1 + k % 3)) % (unsigned)view->width);
            int y = view->world_y + (int)(((h >> 20) + up) % height);
            if (k < 10) box_pixels(view, x - 1, y + 1, x, y + 1, TEAM_ONE, 0);
            box_pixels(view, x, y, x, y, k < 10 ? GOLD : TRACK, 0);
        }
        return;
    }
    count = snowstorm ? 68 : storm ? 40 : snow || sand ? 32 : 22;
    if (!solo) count /= 2;                              /* similar rain density across both halves */
    for (k = 0; k < count; k += 1 + (int)fx_budget_quality) {
        uint32_t h = (uint32_t)(k + 1) * 2654435761u;
        unsigned sway = (h + tick / 3) & 15u;
        unsigned dx = snow ? (sway < 8 ? sway : 15 - sway) :
            tick * (unsigned)(snowstorm ? 7 + k % 4 : sand ? 11 + k % 4 : storm ? 3 : 1);
        unsigned dy = sand ? tick / 3 : tick * (unsigned)((snowstorm ? 3 : storm ? 9 : snow ? 1 : 5) + k % 3);
        int x = view->world_x + (int)(((h >> 8) + dx) % (unsigned)view->width);
        int y = view->world_y + (int)(((h >> 20) + dy) % height);
        if (snowstorm) {
            box_pixels(view, x, y + 1, x + 2, y + 1, 5, 0);
            box_pixels(view, x + 2, y, x + 3 + k % 3, y, WHITE, 0);
        } else if (snow) {
            int flake = k % 3 == 0;
            if (flake) box_pixels(view, x, y + 1, x + 1, y + 1, 5, 0);
            box_pixels(view, x, y, x + flake, y, WHITE, 0);
        } else if (sand) {
            box_pixels(view, x, y, x + 2 + k % 4, y, k & 1 ? GOLD : 2, 0);
        } else {
            box_pixels(view, x, y, x, y + 2, 7, 0);
            if (storm) box_pixels(view, x + 1, y + 3, x + 1, y + 6, 7, 0);
        }
    }
}

static void draw_flag(const View *view, int x, int y, int team)
{
    int pen = team == TEAM_PLAYER ? TEAM_ONE : TEAM_TWO;
    box(view, x - 2, y + 3, x + 3, y + 4, 12);
    box(view, x - 2, y - 7, x - 1, y + 3, WHITE);
    box(view, x - 1, y - 7, x + 6, y - 2, INK);
    box(view, x, y - 6, x + 5, y - 3, pen);
    box(view, x, y - 6, x + 3, y - 6, WHITE);
}

static void draw_tracer(const View *view, const Shot *shot)
{
    int x = fx_to_int(shot->x), y = fx_to_int(shot->y);
    int rocket = shot->kind == SHOT_ROCKET || shot->kind == SHOT_HEAVY;
    int horizontal = fx_abs(shot->vx) >= fx_abs(shot->vy);
    int half_x = 1 + (horizontal && rocket), half_y = 1 + (!horizontal && rocket);
    if (rocket || shot->kind == SHOT_SHELL) {
        int tx = fx_to_int(shot->x - shot->vx), ty = fx_to_int(shot->y - shot->vy);
        box_pixels(view, tx - 1, ty - 1, tx + 1, ty + 1, INK, 0);
        box_pixels(view, tx, ty, tx + horizontal, ty + !horizontal, TEAM_ONE, 0);
    }
    box_pixels(view, x - half_x, y - half_y, x + half_x, y + half_y, INK, 0);
    if (rocket) box_pixels(view, x - horizontal, y - !horizontal, x + horizontal, y + !horizontal, GOLD, 0);
    box_pixels(view, x, y, x, y, rocket || shot->kind == SHOT_SHELL ? WHITE : GOLD, 0);
}

static void draw_capture_bars(const View *view)
{
    int i;
    if (!(game.extras & EXTRA_CONQUEST)) return;
    for (i = 0; i < game.num_cp; i++) {
        int take = game.cp_take[i], amount = take < 0 ? -take : take;
        int x = game.cp_tx[i] * TILE_SIZE + TILE_SIZE / 2;
        int y = game.cp_ty[i] * TILE_SIZE + TILE_SIZE / 2 + 19;
        int length = (amount * 48 + CP_TAKE - 1) / CP_TAKE;
        if (!amount || amount >= CP_TAKE) continue;
        box_pixels(view, x - 26, y - 1, x + 25, y + 6, INK, 0);
        box_pixels(view, x - 25, y, x + 24, y + 5, WHITE, 0);
        box_pixels(view, x - 24, y + 1, x + 23, y + 4, TRACK, 0);
        box_pixels(view, x - 24, y + 1, x - 25 + length, y + 4,
                   side_pen(take > 0 ? TEAM_PLAYER : TEAM_ENEMY), 0);
    }
}

static void draw_blast(const View *view, const Effect *effect)
{
    int art = effect->kind == EFF_BLAST ? BATTLE_BLAST : BATTLE_IMPACT;
    unsigned age = effect->life - effect->ttl;
#ifdef OFP_AMIGA_ONLINE
    if (settings.explosions == EXPLOSIONS_OFF) return;
    if (settings.explosions == EXPLOSIONS_SIMPLE || fx_budget_quality >= 2) {
        int x = fx_to_int(effect->x), y = fx_to_int(effect->y);
        int hot = age < (effect->life + 3u) / 4u;
        int radius = art == BATTLE_BLAST ? (hot ? 4 : 6) : (hot ? 2 : 3);
        if (age * 2u < effect->life &&
            x >= view->world_x - radius && x < view->world_x + view->width + radius &&
            y >= view->world_y - radius && y < view->world_y + view->rows * TILE_PX + radius)
            draw_simple_blast(view, x, y, radius, hot);
        return;
    }
#endif
    unsigned phase = age * BATTLE_FRAMES / effect->life;
    draw_battle(view, art, (int)phase, effect->x, effect->y);
}

static void draw_embers(const View *view, const Structure *st, const StructInfo *info)
{
#ifdef OFP_AMIGA_ONLINE
    if (settings.ground_fires_off) return;
#endif
    unsigned age = (uint16_t)(game.tick - st->aim);
    int phase = (int)((game.tick >> 3) + st->x + st->y) & (BATTLE_FRAMES - 1);
    int x = st->x * TILE_SIZE + info->w * TILE_SIZE / 2;
    int y = st->y * TILE_SIZE + info->h * TILE_SIZE / 2;
    if (age < TICK_RATE) return;                          /* the initial blast is still visible */
    draw_battle(view, BATTLE_EMBER, phase, FX(x), FX(y));
    if (age < 12 * TICK_RATE && info->w * info->h >= 4)
        draw_battle(view, BATTLE_EMBER, phase ^ 2, FX(x - 9), FX(y + 6));
}

static void draw_respawn(const View *view)
{
    int player = view->left / FIELD_STRIDE;
    int x, y, elapsed, phase;
    char text[16];
    const Tank *tank;
#ifdef OFP_AMIGA_ONLINE
    if (replay_on || title_on || (!local_playing && !online_playing)) return;
    if (online_playing) player = online.slot;
#endif
    if (player >= game.num_tanks) return;
    tank = &game.tanks[player];
    if (!tank->respawn) return;
    elapsed = RESPAWN_TICKS - tank->respawn;
    if (!tank->out && (elapsed < SKULL_START || elapsed >= SKULL_END)) return;
    phase = tank->out ? 2 : ((elapsed - SKULL_START) / 5) & 3;
    x = view->phase_x + (view->width - SCROLL_WORD) / 2;
    y = view->phase_y + (tank->out ? 56 : 83);
    box(view, view->world_x + x - 54, view->world_y + y - 20,
        view->world_x + x + 53, view->world_y + y + 29, SHADE);
    box_pixels(view, view->world_x + x - 53, view->world_y + y - 19,
               view->world_x + x + 52, view->world_y + y + 28, INK, 0);
    draw_battle(view, BATTLE_SKULL, phase, FX(view->world_x + x), FX(view->world_y + y));
    if (tank->out) strcpy(text, "OUT OF TANKS");
    else if (tank->lives == MATCH_UNLIMITED_LIVES) strcpy(text, "UNLIMITED TANKS");
    else snprintf(text, sizeof text, "%u TANK%s LEFT", (unsigned)tank->lives, tank->lives == 1 ? "" : "S");
    label(view->left + x - (int)strlen(text) * 4, FIELD_TOP + y + 23, GOLD, text);
}

void draw_objects(const View *view)
{
    int i;
    fx left = FX(view->world_x - TANK_SIZE / 2), right = left + FX(view->width + TANK_SIZE);
    fx top = FX(view->world_y - TANK_SIZE / 2), bottom = top + FX(view->rows * TILE_PX + TANK_SIZE);
    memset(dirty_first, 0xff, sizeof dirty_first);
    memset(dirty_end, 0, sizeof dirty_end);
#ifdef OFP_AMIGA_ONLINE
    if (editor_on) {
        draw_weather(view);
        editor_cursor(view);
        return;
    }
#endif
#ifdef OFP_AMIGA_ONLINE
    if (!settings.surface_fx_off) draw_surface_trails(view);
#else
    draw_surface_trails(view);
#endif
    draw_marks(view);                                    /* (under everything: on the ground) */
    draw_gates(view);
    for (i = 0; i < game.map.num_st; i++) {
        static const uint8_t turret_rows[ST_TYPE_COUNT] = {
            [ST_T_MG] = 0, [ST_T_CANNON] = 1, [ST_T_ROCKET] = 2,
            [ST_T_HEAVY] = 3, [ST_T_TWIN] = 4, [ST_T_MORTAR] = 5
        };
        const Structure *st = &game.map.st[i];
        const StructInfo *info = &struct_info[st->type];
        if (st->smoulder) draw_embers(view, st, info);
        else if (info->flags & SF_GATE) {
            int x = st->x * TILE_SIZE, y = st->y * TILE_SIZE;
            if (st->hp && x + info->w * TILE_SIZE > view->world_x && x < view->world_x + view->width &&
                y + info->h * TILE_SIZE > view->world_y && y < view->world_y + view->rows * TILE_PX)
                draw_blast_gate(view, st);
        } else if (st->hp && (info->flags & SF_TURRET)) {
            fx x = FX(st->x * TILE_SIZE + info->w * TILE_SIZE / 2);
            fx y = FX(st->y * TILE_SIZE + info->h * TILE_SIZE / 2);
            if (x >= left && x < right && y >= top && y < bottom) {
                int row = turret_rows[st->type], direction = ((st->aim + 0x1000u) >> 13) & 7;
                draw_sprite(view, x, y, TURRET_FIRST + row * TURRET_DIRECTIONS + direction,
                            classic_turrets_bounds[row][direction]);
                if (st->team == TEAM_INDEPENDENT || game.map.custom_bases) {
                    int px = st->x * TILE_SIZE + 1, py = st->y * TILE_SIZE + 1;
                    box(view, px, py, px + 7, py + 7, INK);
                    box(view, px + 2, py + 2, px + 5, py + 5,
                        st->team == TEAM_INDEPENDENT ? GOLD : side_pen(st->team));
                }
            }
        }
    }
    for (i = 0; i < game.num_props; i++) {
        const Prop *prop = &game.props[i];
        if (prop->used && prop->x >= left && prop->x < right && prop->y >= top && prop->y < bottom) {
            int variant = prop->kind * 3 + (prop->alive ? prop->team & 1 : 2);
            int direction = ((prop->angle + 0x1000u) >> 13) & 7;
            draw_sprite(view, prop->x, prop->y,
                        TANK_FRAMES + variant * PROP_DIRECTIONS + direction,
                        classic_vehicles_bounds[variant][direction]);
        }
    }
    for (i = 0; i < MAX_PICKUPS; i++) {
        const Pickup *p = &game.pickups[i];
        if (p->active) {
            int x = p->tx * TILE_SIZE + 8, y = p->ty * TILE_SIZE + 8, k;
            int pen = p->kind == PICKUP_BOOST ? 7 : p->kind == PICKUP_RAPID ? GOLD : REPAIR;
            box(view, x-4, y-4, x+4, y+4, INK);
            box_pixels(view, x-3, y-3, x+3, y+3, pen, 0);
            if (p->kind == PICKUP_REPAIR) {
                box_pixels(view, x-2, y, x+2, y, TEAM_ONE, 0); box_pixels(view, x, y-2, x, y+2, TEAM_ONE, 0);
            } else if (p->kind == PICKUP_BOOST) {
                for (k=0; k<3; k++) box_pixels(view, x-k, y-2+k, x+k, y-2+k, WHITE, 0);
                box_pixels(view, x, y, x, y+2, WHITE, 0);
            } else if (p->kind == PICKUP_RAPID) {
                box_pixels(view, x-2, y-2, x-1, y+2, INK, 0); box_pixels(view, x+1, y-2, x+2, y+2, INK, 0);
            } else if (p->kind == PICKUP_MINES) {
                for (k=-2; k<=2; k+=2) box_pixels(view, x+k, y, x+k, y+1, INK, 0);
            } else {
                box_pixels(view, x-2, y, x+2, y, INK, 0); box_pixels(view, x-1, y-2, x+1, y-2, INK, 0);
            }
        }
    }
    if (game.drop.active)
        mark(view, FX(game.drop.tx * TILE_SIZE + 8), FX(game.drop.ty * TILE_SIZE + 8), 3, GOLD);
    draw_mode_objects(view);                             /* (the modes' points, tags, rings; soldiers, helicopters) */
    for (i = 0; i < MAX_MINES; i++) {
        const Mine *mine = &game.mines[i];
#ifdef OFP_AMIGA_AGA
        const int alert = AGA_ALERT_PEN;
#else
        const int alert = TEAM_ONE;                     /* warm signal colour in the 16-colour palette */
#endif
        if (!mine->active) continue;
        {
            int x = fx_to_int(mine->x), y = fx_to_int(mine->y);
            box(view, x - 2, y - 2, x + 2, y + 2, INK);
            box_pixels(view, x - 1, y - 1, x + 1, y + 1, mine->arm ? 5 : (game.tick / 10) & 1 ? alert : TRACK, 0);
            box_pixels(view, x - 1, y - 1, x, y - 1, WHITE, 0);
        }
    }
    for (i = 0; i < game.num_tanks; i++) {
        const Tank *t = &game.tanks[i];
        if (!t->active || t->out) continue;
        if (t->respawn) mark(view, t->x, t->y, 3, TRACK);
        else {
            int x = fx_to_int(t->x), y = fx_to_int(t->y), k;
            draw_tank(view, t);
            for (k = 0; k < 2; k++) {
                int ticks = k ? t->rapid_ticks : t->boost_ticks;
                if (ticks) {
                    static const uint8_t glyphs[2][5] = { {6,5,6,5,6}, {6,5,6,5,5} };
                    int row, col, y0 = y + 13 + k*7;
                    box(view, x-13, y0, x+9, y0+5, INK);
                    for (row=0; row<5; row++) for (col=0; col<3; col++)
                        if (glyphs[k][row] & (4 >> col)) box_pixels(view, x-12+col, y0+row, x-12+col, y0+row, WHITE, 0);
                    box_pixels(view, x-7, y0+2, x-7+(ticks*14/POWERUP_TICKS), y0+3, k ? GOLD : 7, 0);
                }
            }
        }
    }
    roofs_over(view);
    for (i = 0; i < 2; i++) {
        const Flag *flag = &game.flags[i];
        if (flag->state == FLAG_GROUND)
            draw_flag(view, fx_to_int(flag->x), fx_to_int(flag->y), i);
        else if (flag->state == FLAG_CARRIED && flag->carrier < game.num_tanks) {
            const Tank *t = &game.tanks[flag->carrier];
            int x = fx_to_int(t->x), y = fx_to_int(t->y);
            draw_flag(view, x + 4, y - 7, i);
        }
    }
    for (i = 0; i < MAX_EFFECTS; i++) {
        const Effect *e = &game.effects[i];
        if (!e->active || e->delay) continue;
        if (e->kind == EFF_BLAST || e->kind == EFF_EXPLOSION) draw_blast(view, e);
        else if (e->kind == EFF_SPLAT && !marks_no_blood && e->life > e->ttl && e->life - e->ttl < 15 &&
                 e->x >= left && e->x < right && e->y >= top && e->y < bottom) {
            WaitBlit();
            draw_blood_drops(view, fx_to_int(e->x), fx_to_int(e->y), e->life - e->ttl);
        }
        else if (e->kind <= EFF_PUFF) mark(view, e->x, e->y, 2, e->kind == EFF_PUFF ? 5 : GOLD);
    }
    canopy_over(view);
#ifdef OFP_AMIGA_ONLINE
    if (!settings.weather_off)
#endif
        draw_weather(view);
    WaitBlit();                                          /* contrast cues stay above particles and weather */
    for (i = 0; i < MAX_SHOTS; i++)
        if (game.shots[i].active) draw_tracer(view, &game.shots[i]);
    draw_capture_bars(view);
    draw_respawn(view);
}
