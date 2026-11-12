/* SPDX-License-Identifier: MIT
 * The Amiga client: the state its parts share (declared in local/internal.h), and the ground's look
 * from the map. The rest, by subject, in local/: each part its own translation unit. */
#include "platform/amiga/local/internal.h"
struct GfxBase *GfxBase;
struct IntuitionBase *IntuitionBase;
struct Library *LowLevelBase;

Game game;
struct Screen *screen;
struct Window *window;
struct BitMap *frame, *scroll_scratch;
struct ScreenBuffer *display_buffers[2];
struct MsgPort *safe_port, *display_port;
int back_buffer = 1, swap_pending;
struct RastPort presentation;
UBYTE *sprite_chip;
UWORD *blank_pointer;
struct RastPort canvas;
uint32_t tile_cache[ROWS][BUFFER_W / TILE_PX];
int camera_x[2], camera_y[2], camera_ready[2];
uint8_t terrain_art[MAP_H][MAP_W];
HudState hud[2];
int detailed_radar;
int hud_ready;
uint8_t keys[128];
int active = 1, quit, solo;
int input_mode[2] = { AMIGA_INPUT_BOTH, AMIGA_INPUT_BOTH };
int replay_on, replay_follow;          /* watching a replay: which tank the camera follows */
#ifdef OFP_AMIGA_ONLINE
Replay rec, watch;
int recording;
uint32_t watch_tick, watch_next;
int watch_speed = 2, watch_paused;     /* speed: halves (1 half, 2 normal, 4 twice, 8 four times) */
char replay_names[MAX_REPLAYS][32];
int nreplays;
void rec_begin(const MatchSetup *ms, const char *map);   /* (below: our matches, recorded) */
const char *my_map_text(void);               /* (below: the editor's map) */
struct Library *KeymapBase;
int editor_on, title_on;
void draw_title_overlay(void);
void draw_editor_panels(void);
void rec_tick(const InputBits *in);
void draw_watch_strip(void);
#endif
#ifdef OFP_AMIGA_ONLINE
NetClient online;
int online_playing, local_playing, menu_steer;
#endif
int bot_avoid, bot_turn;
#ifndef OFP_AMIGA_ONLINE
int restart;
#endif
uint32_t seed = 2026;
int map_choice;
const char *const map_names[] = { "Dust Crossing", "Perimeter Road", "PRACTICE ARENA", "RANDOM SMALL", "RANDOM MEDIUM", "MY MAP", "RANDOM LARGE", "Switchback", "Twin Boulevards" };
/* LOCAL GAME's modes (the desktop's team modes), the enemy's level, our tank */
const struct LocalMode local_modes[] = {
    { "CAPTURE THE FLAG", 0 }, { "CONQUEST", EXTRA_CONQUEST }, { "HARDPOINT", EXTRA_HARDPOINT },
    { "TEAM DEATHMATCH", EXTRA_TDM }, { "KILL CONFIRMED", EXTRA_KILLCONF }, { "ANNIHILATION", EXTRA_ANNIHILATION },
    { "WARLORD", EXTRA_WARLORD } };
const char *const level_names[] = { "EASY", "NORMAL", "HARD" };
int mode_choice, level_choice = 1, cls_choice = TANK_MEDIUM, world_choice;

#ifdef OFP_AMIGA_ONLINE
void editor_cursor(const View *view);        /* (below: the map editor's) */
#endif

/* Classic's connected atlas columns encode north, east, south and west. */
uint8_t art_at(int x, int y)
{
    static const uint8_t groups[TILE_COUNT] = {
        0, 1, 2, 0, 3, 0, 4, 0, 5, 6, 0, 7, 8, 9, 10
    };
    const Map *map = &game.map;
    int tile = map->type[y][x], group = groups[tile], column = 0;
    if (!group) {
        column = (x * 7 + y * 11) & 15;
    } else {
        if (y == 0 || groups[map->type[y - 1][x]] == group) column |= 1;
        if (x == map->w - 1 || groups[map->type[y][x + 1]] == group) column |= 2;
        if (y == map->h - 1 || groups[map->type[y + 1][x]] == group) column |= 4;
        if (x == 0 || groups[map->type[y][x - 1]] == group) column |= 8;
    }
    return (uint8_t)((tile << 4) | column);
}

void refresh_art(int x, int y)
{
    terrain_art[y][x] = art_at(x, y);
    if (x > 0) terrain_art[y][x - 1] = art_at(x - 1, y);
    if (x < game.map.w - 1) terrain_art[y][x + 1] = art_at(x + 1, y);
    if (y > 0) terrain_art[y - 1][x] = art_at(x, y - 1);
    if (y < game.map.h - 1) terrain_art[y + 1][x] = art_at(x, y + 1);
}

