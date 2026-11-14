/* SPDX-License-Identifier: MIT
 * src/platform/amiga/local/internal.h: what the Amiga client's parts share (local.c and the files in local/, each its
 * own translation unit): the headers, the screen's numbers, the shared types, and the shared state and
 * functions, declared here and defined in the one part that owns each (its file named beside it below,
 * in the order they come). The art and sounds are local/art.c's (and amiga_sound.c's, amiga_music.c's). */
#ifndef OFP_AMIGA_LOCAL_INTERNAL_H
#define OFP_AMIGA_LOCAL_INTERNAL_H
#include <stdio.h>
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <graphics/displayinfo.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/lowlevel.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/lowlevel.h>
#include "core/match.h"
#include "common/net.h"
#include "platform/amiga/local/profile.h"
#ifdef OFP_AMIGA_ONLINE
#include <stdlib.h>
#include "common/client.h"
#include "platform/amiga/public_servers.h"
#endif
#include "platform/amiga/menu_settings.h"
#include "platform/amiga/amiga_sound.h"
#include "platform/amiga/amiga_music.h"
#ifdef OFP_AMIGA_ONLINE
#include "common/training.h"
#include "common/operation.h"
#include "common/replay.h"
#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/keymap.h>
#endif
#define OFP_AMIGA_IDS_ONLY                  /* (the art's and the sounds' names and numbers: the data is local/art.c's) */
#include "platform/amiga/sounds_8bit.h"
#include "common/soundtrack.h"
#include "platform/amiga/faces_art.h"
#ifdef OFP_AMIGA_ONLINE
#include "platform/amiga/map_catalog.h"
#endif
#if defined(OFP_AMIGA_AGA) || defined(OFP_AMIGA_AGA32) || defined(OFP_AMIGA_AGA256) || defined(OFP_NATIVE_AGA)
#error This release supports only the 16-colour Amiga renderer.
#endif
#undef OFP_AMIGA_IDS_ONLY

enum { DEPTH = 4 };
enum { COLOURS = 1 << DEPTH, PLANE_MASK = COLOURS - 1 };
enum { WIDTH = 320, HEIGHT = 256, VIEW = 160, TOP = 16,
       TILE_PX = 8, BOTTOM = 200, FIELD_H = BOTTOM - TOP,
       FIELD_TOP = HEIGHT,
#ifdef OFP_AMIGA_ONLINE
       ROWS = HEIGHT / TILE_PX + 1,
#else
       ROWS = BOTTOM / TILE_PX + 1,
#endif
       SCROLL_WORD = 16, FIELD_STRIDE = VIEW + SCROLL_WORD,
       BUFFER_W = 2 * FIELD_STRIDE, ARENA_SIZE = 64 };
enum { INK = 0, TEAM_ONE = 8, TEAM_TWO, WHITE, GOLD, TRACK, REPAIR, SHADE, PANEL };
#ifdef OFP_AMIGA_AGA
enum { BLOOD_PEN = AGA_ALERT_PEN };
#else
enum { BLOOD_PEN = 6 };
#endif
enum { SKULL_START = 28, SKULL_END = 118 };

extern struct ExecBase *SysBase;

enum { TANK_SIZE = 32, DIRECTIONS = 32, TANK_PLANE_BYTES = TANK_SIZE * TANK_SIZE / 8,
       TANK_FRAME_BYTES = (DEPTH + 1) * TANK_PLANE_BYTES,
       TANK_FRAMES =
#ifdef OFP_AMIGA_ONLINE
       6 * DIRECTIONS,
#else
       2 * DIRECTIONS,
#endif
       PROP_DIRECTIONS = 8,
       PROP_FRAMES = PROP_KINDS * 3 * PROP_DIRECTIONS,
       TURRET_DIRECTIONS = 8, TURRET_VARIANTS = 6, MAN_FRAMES = 16, MAN_VARIANTS = 3,
       TURRET_FIRST = TANK_FRAMES + PROP_FRAMES,
       MAN_FIRST = TURRET_FIRST + TURRET_DIRECTIONS * TURRET_VARIANTS,
       BATTLE_FIRST = MAN_FIRST + MAN_FRAMES * MAN_VARIANTS,
       BATTLE_FRAMES = 4, BATTLE_VARIANTS = 6,
       ROCKET_FIRST = BATTLE_FIRST + BATTLE_FRAMES * BATTLE_VARIANTS,
#ifdef OFP_AMIGA_ONLINE
       ROCKET_DIRECTIONS = 16, ROCKET_FRAMES = 2 * ROCKET_DIRECTIONS,
#else
       ROCKET_FRAMES = 0,
#endif
       SPRITE_CHIP_BYTES = (ROCKET_FIRST + ROCKET_FRAMES) * TANK_FRAME_BYTES };
enum { BATTLE_IMPACT, BATTLE_BLAST, BATTLE_EMBER, BATTLE_GATE_H, BATTLE_GATE_V, BATTLE_SKULL };
enum { RADAR_SIZE = 40, RADAR_X = 112, RADAR_Y = BOTTOM + 6, GAUGE_WIDTH = 43 };
typedef struct {
    uint32_t score;
    int lives, pows, pows_max, mines, has_flag, fuel, armor;
} HudState;
#ifdef OFP_AMIGA_ONLINE
#define REPLAY_DIR "PROGDIR:replays"
#endif
#ifdef OFP_AMIGA_ONLINE
#define MAX_REPLAYS 14
#endif
#ifdef OFP_AMIGA_ONLINE
enum { CLASSIC_VIEW = 224, EDITOR_WIDTH = 240, EDITOR_BOTTOM = 224, EDITOR_FIELD_H = EDITOR_BOTTOM - TOP };
#endif
enum { PRACTICE_MAP = 2, RANDOM_SMALL = 3, RANDOM_MEDIUM = 4, MY_MAP = 5, RANDOM_LARGE = 6, SWITCHBACK = 7, TWIN_BOULEVARDS = 8, LOCAL_MAPS = 9 };
enum { LOCAL_MODES = 7 };
#ifdef OFP_AMIGA_ONLINE
typedef struct { int x0, y0, x1, y1; } ViewDamage;       /* exclusive right and bottom */
static inline void add_view_damage(ViewDamage *damage, int x0, int y0, int x1, int y1)
{
    if (x0 < damage->x0) damage->x0 = x0;
    if (y0 < damage->y0) damage->y0 = y0;
    if (x1 > damage->x1) damage->x1 = x1;
    if (y1 > damage->y1) damage->y1 = y1;
}
#endif
typedef struct {
    int left, width, rows, world_x, world_y, phase_x, phase_y;
#ifdef OFP_AMIGA_ONLINE
    ViewDamage *damage;
#endif
} View;

#ifdef OFP_AMIGA_ONLINE                              /* the online client's menus, pages and rows (menus.c) */
enum { RAW_EVENTS = IDCMP_RAWKEY | IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW,
       ONLINE_LOOKAHEAD = 4, ONLINE_DRAW_MS = 40, ONLINE_CATCHUP = 8,
       LOCAL_TICK_MS = 1000 / TICK_RATE };
enum { MENU_MAIN, MENU_OPTIONS, MENU_GRAPHICS, MENU_AUDIO, MENU_LOCAL, MENU_NETWORK, MENU_REPLAYS, MENU_PLAY, MENU_TOOLS, MENU_SETTINGS,
       MENU_TEAMS, MENU_SIGNIN, MENU_CONTROLS, MENU_PROFILE_EDIT, MENU_PROFILE_DELETE, MENU_KEYMAP, MENU_ABOUT, MENU_CHALLENGES, MENU_DIAGNOSTICS };
enum { TOOL_REPLAYS, TOOL_EDITOR, TOOL_BENCHMARK, TOOL_CONTROLLERS, TOOL_BACK, TOOL_ROWS };
enum { SET_GAME, SET_GRAPHICS, SET_AUDIO, SET_CONTROLS, SET_BACK, SET_ROWS };
enum { GRAPH_MARKS, GRAPH_WEATHER, GRAPH_EXPLOSIONS, GRAPH_FIRES, GRAPH_SURFACE, GRAPH_AUTO, GRAPH_BACK, GRAPH_ROWS };
enum { MAIN_PLAY, MAIN_TOOLS, MAIN_SETTINGS, MAIN_ABOUT, MAIN_QUIT, MAIN_P1, MAIN_P2, MAIN_ROWS };
enum { LG_WORLD, LG_MODE, LG_SIZE, LG_MAP, LG_NEXT, LG_BACK, LG_ROWS };
enum { TM_PLAYERS, TM_ENEMY, TM_START, TM_BACK, TM_ROWS };
enum { SERVER_UNCHECKED, SERVER_REACHABLE, SERVER_OFFLINE };
enum { SIGNED_OUT = -1, JOIN_NEW = AMIGA_PROFILES, JOIN_LEAVE };
#define MY_MAP_PATH "PROGDIR:maps/my-map.ofpmap"
#define MY_MAP_TEMP "PROGDIR:maps/my-map.tmp"
#define MY_MAP_BACKUP "PROGDIR:maps/my-map.bak"
#endif

typedef struct {                                     /* SAVED servers: what each said when asked (menus.c) */
    ServerInfo info;
    int state, ping;
} SavedServer;
#ifdef OFP_AMIGA_ONLINE
extern const int catalog_map_count;                 /* (local/art.c: map_catalog.h's maps) */
#endif

/* ---- shared state and functions (generated from where they are defined, by build: OFP_AMIGA_ONLINE the
 *      online client, OFP_AMIGA_AGA the 64-colour one, OFP_LOCAL_DUEL the local client's) ---- */
#if defined(OFP_AMIGA_ONLINE)
extern char font8x8_basic[128][8];
#endif
#if defined(OFP_AMIGA_AGA)
extern const uint32_t aga_palette[AGA_WORLD_FIRST];
extern const uint32_t aga_blind_ramp[3];
extern const uint32_t aga_world_palette[9][COLOURS - AGA_WORLD_FIRST];
extern const uint32_t aga_original_palette[COLOURS - AGA_WORLD_FIRST];
#endif
extern const uint16_t classic_palette[16];
extern const uint8_t classic_tiles[768][DEPTH][8];
extern const uint32_t classic_tanks[2][32][32][DEPTH + 1];
extern const uint8_t classic_tanks_bounds[2][32][4];
#ifdef OFP_AMIGA_ONLINE
extern const uint32_t classic_rocket_tanks[2][ROCKET_DIRECTIONS][32][DEPTH + 1];
extern const uint8_t classic_rocket_tanks_bounds[2][ROCKET_DIRECTIONS][4];
#endif
extern const uint32_t classic_vehicles[18][8][32][DEPTH + 1];
extern const uint8_t classic_vehicles_bounds[18][8][4];
extern const uint32_t classic_turrets[6][8][32][DEPTH + 1];
extern const uint8_t classic_turrets_bounds[6][8][4];
extern const uint32_t classic_men[3][16][32][DEPTH + 1];
extern const uint8_t classic_men_bounds[3][16][4];
extern const uint32_t classic_battle[BATTLE_VARIANTS][BATTLE_FRAMES][32][DEPTH + 1];
extern const uint8_t classic_battle_bounds[BATTLE_VARIANTS][BATTLE_FRAMES][4];
extern const uint8_t classic_structures[20][2][48][DEPTH + 1][8];
extern const char classic_dust_crossing[];
extern const char classic_perimeter_road[];
extern const char classic_switchback[];
extern const char classic_twin_boulevards[];
#if defined(OFP_AMIGA_ONLINE)
extern const uint8_t faces_art[10][4][24][3];
#endif
extern struct GfxBase *GfxBase;
extern struct IntuitionBase *IntuitionBase;
extern struct Library *LowLevelBase;
extern Game game;
extern struct Screen *screen;
extern struct Window *window;
extern struct BitMap *frame, *scroll_scratch;
extern struct ScreenBuffer *display_buffers[2];
extern struct MsgPort *safe_port, *display_port;
extern int back_buffer , swap_pending;
extern struct RastPort presentation;
extern UBYTE *sprite_chip;
extern UWORD *blank_pointer;
extern struct RastPort canvas;
extern uint32_t tile_cache[ROWS][BUFFER_W / TILE_PX];
extern int camera_x[2], camera_y[2], camera_ready[2];
extern uint8_t terrain_art[MAP_H][MAP_W];
extern HudState hud[2];
extern int detailed_radar;
extern int hud_ready;
extern uint8_t keys[128];
extern int active, quit, input_mode[2], solo;
#if defined(OFP_AMIGA_ONLINE)
extern int replay_on, replay_follow;
extern Replay rec, watch;
extern int recording;
extern uint32_t watch_tick, watch_next;
extern int watch_speed , watch_paused;
extern char replay_names[MAX_REPLAYS][32];
extern int nreplays;
extern struct Library *KeymapBase;
extern int editor_on, title_on;
extern NetClient online;
extern int online_playing, local_playing, menu_steer;
#endif
extern int bot_avoid, bot_turn;
#if !defined(OFP_AMIGA_ONLINE)
extern int restart;
#endif
extern uint32_t seed ;
extern int map_choice;
#if defined(OFP_AMIGA_ONLINE)
extern const char *const map_names[] ;
#endif
struct LocalMode { const char *name; uint8_t extras; };   /* LOCAL GAME's modes (local.c) */
extern const struct LocalMode local_modes[];
#if defined(OFP_AMIGA_ONLINE)
extern const char *const level_names[] ;
#endif
extern int mode_choice, level_choice , cls_choice , world_choice;
uint8_t art_at(int x, int y);
void refresh_art(int x, int y);
#if defined(OFP_AMIGA_ONLINE)
void editor_start(void);
void draw_editor_panels(void);
void editor_cursor(const View *view);
void editor_step(uint32_t now);
void editor_toggle_menu(void);
void editor_camera(int *x, int *y);
void editor_cancel_mouse(void);
void editor_mouse(ULONG type, UWORD code, int x, int y);
void editor_wheel(int x, int y, int delta);
void editor_raw_key(UWORD code);
int editor_text_input(UWORD code, UWORD qualifier);
int editor_time_preview(void);
int editor_return_from_test(void);
int editor_test_running(void);
void editor_menu_begin(void);
void editor_menu_present(void);
void menu_option_row(int selected, int left, int right, int y, const char *name, const char *value);
const char *my_map_text(void);
void read_my_map_metadata(void);
#endif
void label(int x, int y, int pen, const char *text);
void fill(int pen, int x0, int y0, int x1, int y1);
#if defined(OFP_AMIGA_ONLINE)
void inset(int x, int y, int width, int height, int rim);
#endif
void wait_for_display(void);
#if defined(OFP_AMIGA_ONLINE)
void present_frame(void);
#endif
extern int music_on ;
void update_music(int in_match);
enum { PALETTE_MENU = -2, PALETTE_DIRTY = -1 };
#if defined(OFP_AMIGA_ONLINE)
extern int paused;
extern int palette_shown , colour_blind;
#endif
void base_palette(uint32_t pal[COLOURS]);
void show_palette(const uint32_t pal[COLOURS]);
#if defined(OFP_AMIGA_ONLINE)
extern int training_on;
extern Training tr;
#endif
void draw_frame(void);
extern const uint8_t default_keys[2][6] ;
#if defined(OFP_AMIGA_ONLINE)
extern const char *const world_names[] ;
extern AmigaMenuSettings settings;
extern AmigaPublicList public_list;
extern int public_tab , public_checked, public_failed;
extern char online_status[96], edit_text[256];
extern int pause_row;
int orders_available(void);
void orders_accept(void);
void orders_seat_step(void);
extern int chat_edit;
extern char chat_line[NET_CHAT];
extern uint32_t chat_until;
extern int online_follow;
int online_view_tank(void);
extern int recorded;
extern int menu_page, menu_row, menu_edit;
extern int replay_loading;
extern int team_ready[2], host_side, team_tank[2];
extern int signed_profile[2] ;
extern int join_player;
extern int online_dirty , online_enter, online_escape, online_refresh;
extern uint32_t local_tick, local_draw;
void save_menu_settings(void);
void apply_menu_settings(void);
void menu_change(int page);
void settings_back(void);
void open_signin(int player, int return_page);
void close_signin(void);
int profile_menu_active(void);
void profile_back(void);
void main_profile_action(int player);
int menu_rows(void);
int local_classic(void);
void menu_side_step(int dir);
void controls_accept(void);
void profile_accept(void);
void submenu_back(void);
int profile_tank(int player);
int profile_steering(int player);
void unpause(void);
void record_result(int won);
const uint8_t *player_keys(int player);
const char *raw_key_name(uint8_t code, char text[16]);
void online_events(void);
void online_clear_lobby_focus(void);
void online_chat_begin(void);
void local_center(int center, int y, int pen, const char *text);
void online_menu(void);
void network_accept(void);
void network_back(void);
void check_server(void);
void online_set_playing(int playing);
void online_disconnect(const char *reason);
void local_begin(int one_player, int side, int second_cls);
void local_join_second(void);
void local_step(uint32_t now);
void rec_begin(const MatchSetup *ms, const char *map);
void rec_tick(const InputBits *in);
void rec_end(void);
void list_replays(void);
void watch_start(const char *name);
void watch_end(void);
void draw_watch_strip(void);
void watch_step(uint32_t now);
InputBits player_key_action(int player, UWORD code);
InputBits keymap_action(const uint8_t *mapping, UWORD code);
InputBits joystick_actions(ULONG joy);
#endif
/* The first game controller uses Amiga port 1. The second uses the mouse port. */
enum { AMIGA_JOY1_PORT = 1, AMIGA_JOY2_PORT = 0 };
InputBits player_joystick(int player);
int player_joystick_port(int player);
InputBits player_input(int player);
InputBits solo_bot_input(void);
#ifdef OFP_AMIGA_ONLINE
int light_local_ai(void);
void reset_field_bots(void);
InputBits field_bot_input(int player);
#endif
#if !defined(OFP_AMIGA_ONLINE)
void events(void);
#endif
void blit_planar_words(struct BitMap *src, int sx, int sy, struct BitMap *dst, int dx, int dy,
                       int width, int height, int shift);
int open_display(void);
void close_display(void);
#if defined(OFP_AMIGA_ONLINE)
void menu_accept(void);
int team_finish_name(int save);
extern DesertOperation operation;
int start_desert_operation(void);
int run_benchmark(void);
int run_pacing_benchmark(void);
int run_detailed_benchmark(void);
int editor_recovery_test(void);
int run_stability_test(const char *host, int port, int cycles);
void draw_title_overlay(void);
void title_start(void);
void title_step(uint32_t now);
extern int original_look;
#endif
extern int art_world;
#if defined(OFP_AMIGA_ONLINE)
void reset_match_view(void);
int random_map(int map);
extern int local_map_count ;
extern int my_map_size, my_map_biome, my_map_arena, my_map_mode;
int selected_map_size(int map);
int map_fits_filters(int map, int world, int mode, int size);
int first_named_map(int world, int mode, int size);
unsigned local_world_modes(int world);
int random_for_size(int size);
const char *map_label(int map);
const char *local_match_setup(MatchSetup *setup, uint32_t match_seed);
#endif
void new_match(int side, int second_cls);
View update_terrain(int player);
uint32_t *load_sprite_bank(uint32_t *dest, int count, const uint32_t (*source)[TANK_SIZE][DEPTH + 1]);
#if defined(OFP_AMIGA_ONLINE)
void box(const View *view, int wx0, int wy0, int wx1, int wy1, int pen);
#endif
extern int namarks, amark_next, marks_no_blood;
void marks_add(const Effect *e);
/* The graphics menu draws the same effects with a NULL view. */
void draw_blood_stain(const View *view, int x, int y);
void draw_simple_blast(const View *view, int x, int y, int radius, int hot);
int hud_left(int panel);
void hud_radar_origin(int panel, int *x, int *y);
#ifdef OFP_AMIGA_ONLINE
int classic_solo_view(void);
#endif
void draw_objects(const View *view);
#if defined(OFP_AMIGA_ONLINE)
extern const struct CatalogMap catalog_maps[];
extern const uint8_t original_tiles[768][DEPTH][8];
extern const uint8_t original_structures[20][2][48][DEPTH + 1][8];
#endif
extern const uint8_t large_classic_canopy[16][DEPTH + 1][8];
#ifdef OFP_AMIGA_AGA
extern const uint8_t canopy_tiles[9][128][DEPTH + 1][8];
#else
extern const uint8_t canopy_tiles[9][64][DEPTH + 1][8];
#endif
#if !defined(OFP_AMIGA_AGA) && !defined(OFP_LOCAL_DUEL)
extern const uint16_t world_ground_pens[9][2];
#endif
#if !defined(OFP_LOCAL_DUEL)
extern const uint8_t large_winter_canopy[16][DEPTH + 1][8];
extern const uint8_t world_tiles[8][768][DEPTH][8];
#endif



#endif
