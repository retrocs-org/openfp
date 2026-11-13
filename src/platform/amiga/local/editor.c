/* SPDX-License-Identifier: MIT
 * src/platform/amiga/local/editor.c: the map editor (MY MAP), and F8: sharing it with the web site.
 * A part of the Amiga client, its own translation unit: what the parts share is in internal.h. */
#include "platform/amiga/local/internal.h"
#include "common/recovery.h"
#include "common/mapcheck.h"
#ifndef OFP_ART_PREVIEW
#include <libraries/asl.h>
#include <proto/asl.h>
struct Library *AslBase;
#endif

static int ed_x, ed_y, ed_brush, ed_category, ed_first_brush, ed_size, ed_world, ed_saved, ed_linked;
static int cam_x, cam_y, pan_x, pan_y, pan_distance, panning, painting;
static MapAreaEdit ed_area;
static int ed_area_team, ed_area_keyboard;
static int ed_show_time, ed_show_weather, ed_details, ed_detail_row, ed_detail_edit;
static char ed_name[64], ed_detail_draft[sizeof ((MapDetails *)0)->description];
static int ed_zoom, ed_zoom_x, ed_zoom_y, ed_zoom_new, ed_wait_fire;
static MapCheck ed_check;
static int ed_checking, ed_check_row, ed_check_scroll;
enum { CHECK_TOP = 145, CHECK_ROW_H = 13, CHECK_BUTTON_TOP = 233 };
static int ed_menu, ed_page, ed_row, ed_select, ed_confirm = -1, ed_yes, ed_dirty;
static Map *ed_test_map;
static Base ed_test_bases[2];
static int ed_test_cam_x, ed_test_cam_y;
static uint8_t ed_pressed[128];
static InputBits ed_joy, ed_test_joy;
enum { ED_MAIN, ED_FILE, ED_OPTIONS, ED_NETWORK };
enum { ED_OPEN, ED_SAVE, ED_RANDOM, ED_RESET, ED_WORLD, ED_WEATHER, ED_TIME, ED_MUSIC, ED_MAP_SIZE,
       ED_GAME_MODE, ED_PLAYERS, ED_APPLY, ED_UPLOAD, ED_TEST, ED_VALIDATE, ED_RESUME, ED_EXIT, ED_BACK, ED_REPLACE_SAVE,
       ED_SHOW_FILE, ED_SHOW_OPTIONS, ED_SHOW_NETWORK };
static const int ed_actions[][9] = {
    { ED_SHOW_FILE, ED_SHOW_OPTIONS, ED_SHOW_NETWORK, ED_TEST, ED_VALIDATE, ED_RESUME, ED_EXIT },
    { ED_OPEN, ED_SAVE, ED_RANDOM, ED_RESET, ED_BACK },
    { ED_WORLD, ED_WEATHER, ED_TIME, ED_MUSIC, ED_MAP_SIZE, ED_GAME_MODE, ED_PLAYERS, ED_APPLY, ED_BACK },
    { ED_UPLOAD, ED_BACK }
};
static const int ed_rows[] = { 7, 5, 9, 2 };
static int ed_option_world, ed_option_weather, ed_option_time, ed_option_music, ed_option_size, ed_option_mode, ed_option_players;
static int ed_option_auto_mode;
static const char mode_auto_notice[] = "MODE SET TO CAPTURE THE FLAG";
static uint32_t ed_seed = 7, ed_next_move;
static char ed_note[40];
static char ed_text[MAPFILE_MAX];
static uint32_t ed_autosave_at;
static int ed_recover;
#define ED_DRAFT "PROGDIR:maps/recovery"
static void ed_recovery_draw(void) {
    editor_menu_begin();
    label(24, 65, GOLD, "RECOVER EDITOR DRAFT?");
    label(24, 94, WHITE, "SAVED MAP IS UNCHANGED");
    label(24, 139, GOLD, "FIRE: RESTORE DRAFT");
    label(24, 165, WHITE, "MINE: DISCARD DRAFT");
    present_frame();
}
static AmigaPublicList share_job;                     /* F8: the map on its way to the web site's gallery */
enum { BRUSH_TOP = 47, BRUSH_ROW = 18, BRUSH_VISIBLE = 8, TERRAIN_BRUSHES = 10,
       OVERVIEW_X = 80, OVERVIEW_Y = 47, OVERVIEW_SIZE = 160 };
enum { TOOL_SAND, TOOL_ROAD, TOOL_WATER, TOOL_ROCK, TOOL_BUSH, TOOL_TREE, TOOL_WALL, TOOL_ICE, TOOL_OIL, TOOL_CRATER,
       TOOL_TOWER, TOOL_BUNKER, TOOL_DEPOT, TOOL_BARRACKS, TOOL_DOME, TOOL_HQ, TOOL_AID,
       TOOL_POW, TOOL_RADAR, TOOL_POWER, TOOL_WAREHOUSE, TOOL_FLAG, TOOL_HELIPAD,
       TOOL_GATE_H, TOOL_GATE_V, TOOL_MG, TOOL_CANNON, TOOL_ROCKET, TOOL_HEAVY, TOOL_TWIN, TOOL_MORTAR,
       TOOL_GARAGE };
static const struct { int kind; const char *name; } ed_brushes[] = {
    { TILE_SAND, "SAND" }, { TILE_ROAD, "ROAD" }, { TILE_WATER, "WATER" }, { TILE_ROCK, "ROCK" },
    { TILE_TREE, "BUSH" }, { TILE_CANOPY, "TREES" }, { TILE_WALL, "WALL" },
    { TILE_ICE, "ICE" }, { TILE_OIL, "OIL SPILL" }, { TILE_CRATER, "CRATER" },
    { ST_TOWER, "TOWER" }, { ST_BUNKER, "BUNKER" }, { ST_DEPOT, "DEPOT" },
    { ST_BARRACKS, "BARRACKS" }, { ST_DOME, "DOME" }, { ST_HQ, "HQ" }, { ST_FIRSTAID, "FIRST AID" },
    { ST_POWCAMP, "POW CAMP" }, { ST_RADAR, "RADAR" }, { ST_POWER, "POWER" }, { ST_WAREHOUSE, "WAREHOUSE" },
    { ST_FLAGHOUSE, "FLAG BASE" }, { ST_HELIPAD, "HELIPAD" }, { ST_GATE_H, "GATE H" }, { ST_GATE_V, "GATE V" },
    { ST_T_MG, "MG GUN" }, { ST_T_CANNON, "CANNON" }, { ST_T_ROCKET, "ROCKET" },
    { ST_T_HEAVY, "HEAVY GUN" }, { ST_T_TWIN, "TWIN GUN" }, { ST_T_MORTAR, "MORTAR" },
    { ST_GARAGE, "GARAGE" }
};
enum { CAT_GROUND, CAT_ROADS, CAT_NATURE, CAT_BASE, CAT_COUNT };
static const uint8_t ground_tools[] = { TOOL_SAND, TOOL_WATER, TOOL_ICE, TOOL_CRATER };
static const uint8_t road_tools[] = { TOOL_ROAD, TOOL_OIL };
static const uint8_t nature_tools[] = { TOOL_ROCK, TOOL_BUSH, TOOL_TREE };
static const uint8_t base_tools[] = { TOOL_WALL, TOOL_GATE_H, TOOL_GATE_V, TOOL_GARAGE, TOOL_FLAG, TOOL_HQ,
    TOOL_AID, TOOL_BARRACKS, TOOL_RADAR, TOOL_TOWER, TOOL_BUNKER, TOOL_DEPOT, TOOL_DOME, TOOL_POW, TOOL_POWER,
    TOOL_WAREHOUSE, TOOL_HELIPAD, TOOL_MG, TOOL_CANNON, TOOL_ROCKET, TOOL_HEAVY, TOOL_TWIN, TOOL_MORTAR };
static const struct { const char *name; const uint8_t *tools; int count; } categories[] = {
    { "GROUND", ground_tools, sizeof ground_tools },
    { "ROADS", road_tools, sizeof road_tools },
    { "NATURE", nature_tools, sizeof nature_tools },
    { "BASE", base_tools, sizeof base_tools }
};
enum { BAR_MAP, BAR_AREA, BAR_TIME, BAR_WEATHER, BAR_DETAILS, BAR_COUNT };
static const int bar_edge[] = { 0, 32, 104, 165, 256, WIDTH };

static int ed_tools(uint8_t *list)
{
    int i, n = 0;
    for (i = 0; i < categories[ed_category].count; i++) {
        int tool = categories[ed_category].tools[i];
        if (tool < TERRAIN_BRUSHES && !map_surface_allowed((Biome)ed_world,
            ed_world == BIOME_CLASSIC || (game.map.custom_bases && game.map.mode == MAP_MODE_CLASSIC), (TileType)ed_brushes[tool].kind)) continue;
        if (tool == TOOL_GARAGE && !game.map.custom_bases) continue;
        if (tool == TOOL_FLAG && game.map.custom_bases && !map_mode_has_flags(game.map.mode)) continue;
        list[n++] = (uint8_t)tool;
    }
    return n;
}

static int ed_tool(void)
{
    uint8_t list[sizeof base_tools];
    int n = ed_tools(list);
    return list[ed_brush < n ? ed_brush : 0];
}

static void ed_select_brush(int brush)
{
    ed_brush = brush;
    if (brush < ed_first_brush) ed_first_brush = brush;
    if (brush >= ed_first_brush + BRUSH_VISIBLE) ed_first_brush = brush - BRUSH_VISIBLE + 1;
}

static void ed_switch_category(int delta)
{
    ed_category = (ed_category + CAT_COUNT + delta) % CAT_COUNT;
    ed_brush = ed_first_brush = 0;
}

static void ed_scroll_brushes(int delta)
{
    uint8_t list[sizeof base_tools];
    int max = ed_tools(list) - BRUSH_VISIBLE;
    ed_first_brush += delta;
    if (ed_first_brush > max) ed_first_brush = max;
    if (ed_first_brush < 0) ed_first_brush = 0;
}

static int in_a_base(int x, int y)
{
    int k;
    for (k = 0; k < 2; k++)
        if (x >= game.bases[k].x0 && x <= game.bases[k].x1 && y >= game.bases[k].y0 && y <= game.bases[k].y1) return 1;
    return 0;
}

static void ed_clamp_camera(void)
{
    int max_x = game.map.w * TILE_SIZE - EDITOR_WIDTH, max_y = game.map.h * TILE_SIZE - EDITOR_FIELD_H;
    if (cam_x > max_x) cam_x = max_x;
    if (cam_y > max_y) cam_y = max_y;
    if (cam_x < 0) cam_x = 0;
    if (cam_y < 0) cam_y = 0;
}

static void ed_center(void)
{
    cam_x = ed_x * TILE_SIZE + TILE_SIZE / 2 - EDITOR_WIDTH / 2;
    cam_y = ed_y * TILE_SIZE + TILE_SIZE / 2 - EDITOR_FIELD_H / 2;
    ed_clamp_camera();
}

void editor_camera(int *x, int *y)
{
    *x = cam_x;
    *y = cam_y;
}

static void ed_prepare_map(void)
{
    const Map *m = &game.map;
    game.num_tanks = 0;                                  /* (nothing moving: just the map) */
    game.num_props = 0;
    memset(game.men, 0, sizeof game.men);
    memset(game.helis, 0, sizeof game.helis);
    memset(game.mines, 0, sizeof game.mines);
    memset(game.effects, 0, sizeof game.effects);
    game.drop.active = 0;
    game.biome = (uint8_t)ed_world;
    memset(game.pickups, 0, sizeof game.pickups);
    map_normalize_surfaces(&game.map, (Biome)ed_world, ed_world == BIOME_CLASSIC || (game.map.custom_bases && game.map.mode == MAP_MODE_CLASSIC));
    original_look = ed_world == BIOME_CLASSIC;
    game.weather = WEATHER_CLEAR;
    if (ed_x < 1 || ed_x >= m->w - 1) ed_x = m->w / 2;
    if (ed_y < 1 || ed_y >= m->h - 1) ed_y = m->h / 2;
    ed_center();
    reset_match_view();
}
static int ed_weather_allowed(void)
{
    return map_editor_allows_weather(game.map.custom_bases ? game.map.mode : -1, (Biome)ed_world);
}

static int ed_option_weather_allowed(void)
{
    int mode = game.map.custom_bases || ed_option_size != ed_size ? ed_option_mode : -1;
    return map_editor_allows_weather(mode, (Biome)ed_option_world);
}

static int ed_menu_row_enabled(int row)
{
    return ed_page != ED_OPTIONS || ed_actions[ED_OPTIONS][row] != ED_WEATHER || ed_option_weather_allowed();
}

/* An empty map carries spawn positions in the file so it can be test-run. */
static void ed_new(int empty)
{
    Map *m = &game.map;
    int i, time = m->time, music = m->music, weather = m->weather;
    int players = m->custom_bases ? m->players : 2, mode = m->custom_bases ? m->mode : MAP_MODE_CLASSIC;
    MapDetails details = m->details;
    map_set_next_size(ed_size);
    if (empty) {
        memset(&game, 0, sizeof game);
        game.winner = -1;
        map_fill(m, TILE_SAND);
        map_custom_begin(m, game.bases, players, mode);
        for (i = 0; i < m->w; i++) {
            map_set(m, i, 0, TILE_ROCK);
            map_set(m, i, m->h - 1, TILE_ROCK);
        }
        for (i = 1; i < m->h - 1; i++) {
            map_set(m, 0, i, TILE_ROCK);
            map_set(m, m->w - 1, i, TILE_ROCK);
        }
    } else {
        worldgen_set_camp_garages(4);
        game_init(&game, ed_seed++, (Biome)ed_world);
        map_custom_clear_generated_bases(m, game.bases, players, mode);
        game.enhanced_rules = ed_world != BIOME_CLASSIC && mode != MAP_MODE_CLASSIC;
        game_prepare_surfaces(&game, ed_seed - 1, 1);
    }
    map_set_next_size(SIZE_LARGE);
    m->details = details;
    m->time = (uint8_t)time;
    m->music = (uint8_t)music;
    m->has_weather = ed_weather_allowed();
    m->weather = m->has_weather && weather_allowed((Biome)ed_world, weather) ? (uint8_t)weather : WEATHER_CLEAR;
    ed_x = m->w / 2;
    ed_y = m->h / 2;
    ed_saved = ed_linked = 0;
    ed_brush = ed_first_brush = 0;
    ed_prepare_map();
}

static int ed_save(void)                              /* (0: saved) */
{
    FILE *f;
    const char *error = map_custom_error(&game.map, game.bases);
    BPTR l;
    if (error) { snprintf(ed_note, sizeof ed_note, "%s", error); return -1; }
    l = Lock((CONST_STRPTR)"PROGDIR:maps", ACCESS_READ);
    if (!l) l = CreateDir((CONST_STRPTR)"PROGDIR:maps");
    if (l) UnLock(l);
    if (!mapfile_save(&game.map, game.bases, (Biome)ed_world, ed_name, ed_text, sizeof ed_text)) { strcpy(ed_note, "CAN'T SAVE THIS MAP"); return -1; }
    if (!(f = fopen(MY_MAP_TEMP, "w"))) { strcpy(ed_note, "CAN'T WRITE PROGDIR:MAPS"); return -1; }
    {
        int failed = fputs(ed_text, f) == EOF;
        if (fclose(f) || failed) goto save_failed;
    }
    l = Lock((CONST_STRPTR)MY_MAP_PATH, ACCESS_READ);
    if (l) {
        UnLock(l);
        remove(MY_MAP_BACKUP);
        if (rename(MY_MAP_PATH, MY_MAP_BACKUP)) goto save_failed;
    }
    if (rename(MY_MAP_TEMP, MY_MAP_PATH)) {
        rename(MY_MAP_BACKUP, MY_MAP_PATH);
        goto save_failed;
    }
    remove(MY_MAP_BACKUP);
    my_map_size = ed_size;
    my_map_biome = ed_world;
    my_map_arena = game.map.arena;
    my_map_mode = game.map.custom_bases ? game.map.mode : -1;
    ed_saved = ed_linked = 1;
    recovery_clear(ED_DRAFT);
    strcpy(ed_note, "SAVED: LOCAL GAME, MAP: MY MAP");
    return 0;

save_failed:
    remove(MY_MAP_TEMP);
    ed_saved = 0;
    strcpy(ed_note, "CAN'T SAVE THIS MAP");
    return -1;
}

void editor_cancel_mouse(void)
{
    panning = painting = ed_area.active = ed_area_keyboard = 0;
}

static void ed_activate(void)
{
    ULONG events = RAW_EVENTS | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    editor_on = 1;
    solo = 1;
    memset(keys, 0, sizeof keys);
    memset(ed_pressed, 0, sizeof ed_pressed);
    editor_cancel_mouse();
    ed_checking = ed_menu = ed_zoom = ed_select = ed_row = ed_wait_fire = ed_details = 0;
    ed_detail_edit = -1;
    ed_page = ED_MAIN;
    ed_confirm = -1;
    ed_joy = player_joystick(0);
    ed_test_joy = 0;
    ed_next_move = 0;
    ClearPointer(window);
    window->Flags |= WFLG_RMBTRAP;
    ReportMouse(TRUE, window);
#ifndef OFP_ART_PREVIEW
    if (((struct Library *)IntuitionBase)->lib_Version >= 47) events |= IDCMP_EXTENDEDMOUSE;
#endif
    ModifyIDCMP(window, events);
    local_draw = net_ms();
}

void editor_start(void)
{
    game.map.custom_bases = 0;
    ed_world = BIOME_CLASSIC;
    ed_size = SIZE_LARGE;
    ed_brush = ed_first_brush = ed_category = ed_linked = 0;
    ed_area_team = 0;
    ed_show_time = ed_show_weather = 1;
    strcpy(ed_name, "My map");
    memset(&game.map.details, 0, sizeof game.map.details);
    game.map.time = TIME_DAY;
    game.map.weather = WEATHER_CLEAR;
    game.map.music = 0;
    ed_note[0] = 0;
    ed_new(1);
    ed_saved = 1;
    ed_activate();
    ed_autosave_at = net_ms();
    ed_recover = recovery_read(ED_DRAFT, ed_text, sizeof ed_text);
    if (ed_recover) ed_recovery_draw();
}

static void ed_leave(void)
{
    editor_on = 0;
    ReportMouse(FALSE, window);
    window->Flags &= ~WFLG_RMBTRAP;
    if (blank_pointer) SetPointer(window, blank_pointer, 1, 16, 0, 0);
    online_set_playing(0);
    menu_change(MENU_MAIN);
}

int editor_test_running(void) { return ed_test_map != NULL; }

int editor_return_from_test(void)
{
    if (!ed_test_map) return 0;
    game.map = *ed_test_map;
    memcpy(game.bases, ed_test_bases, sizeof ed_test_bases);
    free(ed_test_map);
    ed_test_map = NULL;
    ed_prepare_map();
    cam_x = ed_test_cam_x;
    cam_y = ed_test_cam_y;
    ed_activate();
    ed_test_joy = ed_joy;
    ed_wait_fire = 1;
    strcpy(ed_note, "BACK FROM TEST RUN");
    return 1;
}

/* Editor chrome occupies the bands outside the map viewport. */
void draw_editor_panels(void)
{
    char text[40];
    fill(PANEL, 0, 0, WIDTH - 1, TOP - 1);
    label(4, 11, GOLD, "MAP EDITOR");
    snprintf(text, sizeof text, "%s / %s", ed_world == BIOME_CLASSIC ? "CLASSIC" : world_names[ed_world], map_size_name[ed_size]);
    label(120, 11, WHITE, text);
    {
        uint8_t list[sizeof base_tools];
        int i, n = ed_tools(list);
        fill(PANEL, EDITOR_WIDTH, TOP, WIDTH - 1, EDITOR_BOTTOM - 1);
        fill(GOLD, EDITOR_WIDTH, TOP, EDITOR_WIDTH, EDITOR_BOTTOM - 1);
        label(245, 29, GOLD, "<");
        local_center(280, 29, WHITE, categories[ed_category].name);
        label(309, 29, GOLD, ">");
        if (ed_first_brush) local_center(280, 43, GOLD, "^");
        for (i = 0; i < BRUSH_VISIBLE && ed_first_brush + i < n; i++) {
            int brush = ed_first_brush + i;
            int y = BRUSH_TOP + i * BRUSH_ROW;
            if (brush == ed_brush) fill(INK, 242, y, WIDTH - 2, y + BRUSH_ROW - 2);
            label(246, y + 12, brush == ed_brush ? GOLD : WHITE, ed_brushes[list[brush]].name);
        }
        if (ed_first_brush + BRUSH_VISIBLE < n)
            local_center(280, 205, GOLD, "MORE v");
    }

    fill(INK, 0, EDITOR_BOTTOM, WIDTH - 1, HEIGHT - 1);
    if (ed_note[0]) label(4, EDITOR_BOTTOM + 9, GOLD, ed_note);
    else if (game.map.custom_bases && ed_category == CAT_BASE) {
        snprintf(text, sizeof text, "TEAM 1 %d/%d     TEAM 2 %d/%d",
                 map_custom_count(&game.map, 0, ST_GARAGE), game.map.players / 2,
                 map_custom_count(&game.map, 1, ST_GARAGE), game.map.players / 2);
        label(4, EDITOR_BOTTOM + 9, WHITE, text);
    }
    {
        const char *const labels[] = { "MAP", "SET AREA", "TIME", "WEATHER", "DETAILS" };
        int i;
        for (i = 0; i < BAR_COUNT; i++) {
            int center = (bar_edge[i] + bar_edge[i + 1]) / 2;
            int enabled = i == BAR_TIME ? ed_show_time : i == BAR_WEATHER ? ed_show_weather && ed_weather_allowed() :
                          i != BAR_AREA || game.map.custom_bases;
            inset(bar_edge[i] + 1, 236, bar_edge[i + 1] - bar_edge[i] - 2, 17, enabled ? GOLD : TRACK);
            local_center(center, 247, enabled ? GOLD : WHITE, labels[i]);
        }
    }

}

void editor_cursor(const View *view)          /* the tile under the cursor: a gold square */
{
    int x = ed_x * TILE_SIZE, y = ed_y * TILE_SIZE;
    const StructInfo *object = ed_tool() < TERRAIN_BRUSHES ? NULL : &struct_info[ed_brushes[ed_tool()].kind];
    int right = x + TILE_SIZE * (object ? object->w : 1) - 1;
    int bottom = y + TILE_SIZE * (object ? object->h : 1) - 1;
    if (game.map.custom_bases && ed_category == CAT_BASE) {
        int team;
        for (team = 0; team < 2; team++) {
            const Base *base = &game.bases[team];
            int l = base->x0 * TILE_SIZE, t = base->y0 * TILE_SIZE;
            int r = (base->x1 + 1) * TILE_SIZE - 1, b = (base->y1 + 1) * TILE_SIZE - 1;
            int pen = team == TEAM_PLAYER ? TEAM_ONE : TEAM_TWO;
            box(view, l, t, r, t, pen); box(view, l, b, r, b, pen);
            box(view, l, t, l, b, pen); box(view, r, t, r, b, pen);
        }

    }
    box(view, x, y, right, y + 1, GOLD); box(view, x, bottom - 1, right, bottom, GOLD);
    box(view, x, y, x + 1, bottom, GOLD); box(view, right - 1, y, right, bottom, GOLD);
}

static int ed_spawn_tile(int x, int y)
{
    const Map *m = &game.map;
    int i;
    for (i = 0; i < m->num_spawns; i++)
        if (m->spawn_tx[i] == x && m->spawn_ty[i] == y) return 1;
    return 0;
}

static void ed_paint_tile(TileType tile)
{
    Map *m = &game.map;
    unsigned sid;
    if (ed_x <= 0 || ed_y <= 0 || ed_x >= m->w - 1 || ed_y >= m->h - 1 ||
        (!m->custom_bases && in_a_base(ed_x, ed_y)) || (ed_spawn_tile(ed_x, ed_y) && (tile_info[tile].flags & TF_SOLID))) return;
    sid = m->sid[ed_y][ed_x];
    if (sid) {
        Structure *st = &m->st[sid - 1];
        const StructInfo *info = &struct_info[st->type];
        int x, y;
        if (tile != TILE_SAND) return;
        for (y = st->y; y < st->y + info->h; y++)
            for (x = st->x; x < st->x + info->w; x++)
                if (m->sid[y][x] == sid) {
                    map_set(m, x, y, TILE_SAND);
                    refresh_art(x, y);
                }
        st->type = ST_NONE;
    } else {
        if (m->type[ed_y][ed_x] == tile) return;
        map_set(m, ed_x, ed_y, tile);
        refresh_art(ed_x, ed_y);
    }
    ed_saved = 0;
}

static void ed_place(void)
{
    Map *m = &game.map;
    int tool = ed_tool(), kind = ed_brushes[tool].kind;
    const StructInfo *info;
    int owner;
    int x, y, k, old_count = m->num_st, result;
    if (tool < TERRAIN_BRUSHES) {
        ed_paint_tile((TileType)kind);
        return;
    }
    owner = map_editor_owner(m, game.bases, kind, ed_x, ed_y);
    if (owner < 0) { strcpy(ed_note, "PLACE INSIDE A TEAM AREA"); return; }
    if (m->custom_bases && (kind == ST_GARAGE || kind == ST_FLAGHOUSE)) {
        if (kind == ST_GARAGE && map_custom_count(m, owner, kind) >= m->players / 2) {
            strcpy(ed_note, "THIS TEAM HAS ALL ITS GARAGES");
            return;
        }
        if (kind == ST_FLAGHOUSE && (!map_mode_has_flags(m->mode) || map_custom_count(m, owner, kind))) {
            strcpy(ed_note, "FLAG BASE UNAVAILABLE");
            return;
        }
    }
    info = &struct_info[kind];
    if (ed_x <= 0 || ed_y <= 0 || ed_x + info->w >= m->w || ed_y + info->h >= m->h) {
        strcpy(ed_note, "THE OBJECT NEEDS MORE ROOM");
        return;
    }
    for (k = 0; k < 2 && !m->arena && !m->custom_bases; k++) {
        const Base *base = &game.bases[k];
        int overlaps = game.bases[0].num_garages == 1 && game.bases[1].num_garages == 1 ?
            ed_x <= base->spawn_tx && ed_x + info->w > base->spawn_tx &&
            ed_y <= base->spawn_ty + 2 && ed_y + info->h > base->spawn_ty + 2 :
            ed_x + info->w > base->x0 - 2 && ed_x < base->x1 + 3 &&
            ed_y + info->h > base->y0 - 2 && ed_y < base->y1 + 7;
        if (overlaps) { strcpy(ed_note, "KEEP THE BASE EXIT CLEAR"); return; }
    }
    for (y = ed_y; y < ed_y + info->h; y++)
        for (x = ed_x; x < ed_x + info->w; x++)
            if (m->sid[y][x] || m->type[y][x] == TILE_WATER || m->type[y][x] == TILE_CRATER || m->type[y][x] == TILE_ROCK ||
                m->type[y][x] == TILE_WALL || (!m->custom_bases && in_a_base(x, y)) || ed_spawn_tile(x, y)) {
                strcpy(ed_note, "SOMETHING IS IN THE WAY");
                return;
            }
    /* Reuse a deleted slot without moving the map's other structure IDs. */
    for (k = 0; k < old_count && m->st[k].type != ST_NONE; k++) {}
    m->num_st = k;
    result = map_place_structure(m, (StructType)kind, ed_x, ed_y, owner);
    if (m->num_st < old_count) m->num_st = old_count;
    if (result < 0) { strcpy(ed_note, "THE MAP IS FULL"); return; }
    for (y = ed_y; y < ed_y + info->h; y++)
        for (x = ed_x; x < ed_x + info->w; x++) refresh_art(x, y);
    ed_note[0] = 0;
    ed_saved = 0;
}

static int on_map(int x, int y)
{
    return x >= 0 && x < EDITOR_WIDTH && y >= TOP && y < EDITOR_BOTTOM;
}

static void ed_hover(int x, int y)
{
    ed_x = (cam_x + x) / TILE_SIZE;
    ed_y = (cam_y + y - TOP) / TILE_SIZE;
}

static void ed_toggle_zoom(void)
{
    ed_zoom = !ed_zoom;
    ed_zoom_x = ed_x;
    ed_zoom_y = ed_y;
    ed_zoom_new = ed_dirty = 1;
    ed_next_move = 0;
    editor_cancel_mouse();
    local_draw = net_ms();
}

static void ed_zoom_jump(void)
{
    ed_x = ed_zoom_x;
    ed_y = ed_zoom_y;
    ed_center();
    ed_wait_fire = 1;
    ed_toggle_zoom();
}

static void ed_render_zoom(void)
{
    static UBYTE pixels[DEPTH][OVERVIEW_SIZE][OVERVIEW_SIZE / 8];
    const Map *m = &game.map;
    static const UBYTE pens[] = {
        [TILE_SAND] = GOLD, [TILE_ROAD] = 4, [TILE_WATER] = 7, [TILE_ICE] = WHITE, [TILE_OIL] = INK, [TILE_CRATER] = INK, [TILE_ROCK] = 5,
        [TILE_TREE] = 14, [TILE_CANOPY] = 14, [TILE_BUILDING] = 3, [TILE_WALL] = WHITE,
        [TILE_RUBBLE] = 5
    };
    int x, y, plane;
    if (ed_zoom_new) {
        memset(pixels, 0, sizeof pixels);
        for (y = 0; y < OVERVIEW_SIZE; y++)
            for (x = 0; x < OVERVIEW_SIZE; x++) {
                unsigned tile = m->type[y * m->h / OVERVIEW_SIZE][x * m->w / OVERVIEW_SIZE];
                unsigned pen = tile < sizeof pens && pens[tile] ? pens[tile] : TRACK;
                for (plane = 0; plane < DEPTH; plane++)
                    if (pen & (1 << plane)) pixels[plane][y][x / 8] |= 0x80 >> (x & 7);
            }
        ed_zoom_new = 0;
    }
    editor_menu_begin();
    local_center(WIDTH / 2, 32, GOLD, ed_zoom == 2 ? "SET TEAM AREAS" : "MAP OVERVIEW");
    if (ed_zoom == 2) {
        inset(8, 54, 65, 18, ed_area_team == 0 ? GOLD : TRACK);
        inset(8, 76, 65, 18, ed_area_team == 1 ? GOLD : TRACK);
        label(15, 66, TEAM_ONE, "TEAM 1");
        label(15, 88, TEAM_TWO, "TEAM 2");
    }
    WaitBlit();
    for (plane = 0; plane < DEPTH; plane++)
        for (y = 0; y < OVERVIEW_SIZE; y++)
            memcpy(frame->Planes[plane] + (OVERVIEW_Y + y) * frame->BytesPerRow + OVERVIEW_X / 8,
                   pixels[plane][y], sizeof pixels[plane][y]);
    for (plane = 0; plane < 2 && m->custom_bases; plane++) {
        const Base *b = ed_area.active && ed_area.team == plane ? &ed_area.bounds : &game.bases[plane];
        int l = OVERVIEW_X + b->x0 * OVERVIEW_SIZE / m->w;
        int r = OVERVIEW_X + b->x1 * OVERVIEW_SIZE / m->w;
        int t = OVERVIEW_Y + b->y0 * OVERVIEW_SIZE / m->h;
        int y2 = OVERVIEW_Y + b->y1 * OVERVIEW_SIZE / m->h;
        int pen = plane == TEAM_PLAYER ? TEAM_ONE : TEAM_TWO;
        fill(pen, l, t, r, t); fill(pen, l, y2, r, y2);
        fill(pen, l, t, l, y2); fill(pen, r, t, r, y2);
        if (ed_zoom == 2) {
            fill(pen, l - 1, t - 1, l + 2, t + 2); fill(pen, r - 1, t - 1, r + 2, t + 2);
            fill(pen, l - 1, y2 - 1, l + 2, y2 + 2); fill(pen, r - 1, y2 - 1, r + 2, y2 + 2);
        }
    }
    x = OVERVIEW_X + ed_zoom_x * OVERVIEW_SIZE / m->w;
    y = OVERVIEW_Y + ed_zoom_y * OVERVIEW_SIZE / m->h;
    if (!ed_area.active || ed_area_keyboard) {
        fill(INK, x - 4, y - 1, x + 4, y + 1);
        fill(INK, x - 1, y - 4, x + 1, y + 4);
        fill(WHITE, x - 3, y, x + 3, y);
        fill(WHITE, x, y - 3, x, y + 3);
    }
    local_center(WIDTH / 2, 224, WHITE, ed_zoom == 1 ? "CLICK OR FIRE TO JUMP" : "DRAG AREA OR EDGE TO RESIZE");
    local_center(WIDTH / 2, 241, GOLD, ed_zoom == 1 ? "F7 / MINE / ESC BACK" :
                 ed_note[0] ? ed_note : "TAB TEAM  FIRE APPLY  ESC BACK");
    editor_menu_present();
}

static int ed_can_jump(void)
{
    return ed_check.count && map_issue_has_location(&game.map, &ed_check.issues[ed_check_row]);
}

static void ed_select_issue(int row)
{
    if (!ed_check.count) return;
    ed_check_row = row < 0 ? 0 : row >= ed_check.count ? ed_check.count - 1 : row;
    if (ed_check_row < ed_check_scroll) ed_check_scroll = ed_check_row;
    if (ed_check_row >= ed_check_scroll + MAP_CHECK_ROWS) ed_check_scroll = ed_check_row - MAP_CHECK_ROWS + 1;
    ed_dirty = 1;
}

static void ed_jump_issue(void)
{
    const MapIssue *issue = &ed_check.issues[ed_check_row];
    if (!ed_can_jump()) return;
    ed_x = issue->x;
    ed_y = issue->y;
    ed_center();
    editor_toggle_menu();
    ed_menu = 0;
    strcpy(ed_note, issue->text);
}

static void ed_render_check(void)
{
    enum { X = 112, Y = 41, S = 96 };
    static UBYTE preview[DEPTH][S][S / 8];
    const Map *m = &game.map;
    int x, y, i;
    memset(preview, 0, sizeof preview);
    for (y = 0; y < S; y++)
        for (x = 0; x < S; x++) {
            unsigned mask = 0;
            int tx = x * m->w / S, ty = y * m->h / S, sx, sy, plane, pen;
            int tx2 = ((x + 1) * m->w + S - 1) / S, ty2 = ((y + 1) * m->h + S - 1) / S;
            for (sy = ty; sy < ty2; sy++)
                for (sx = tx; sx < tx2; sx++) mask |= ed_check.route[sy][sx];
            if (!(mask & 3) && ((x + y) & 1)) mask = 0;
            pen = (mask & 5) && (mask & 10) ? WHITE : mask & 5 ? TEAM_ONE : mask & 10 ? TEAM_TWO :
                  m->type[ty][tx] == TILE_WATER ? 7 :
                  m->type[ty][tx] == TILE_ROAD ? 4 : (tile_info[m->type[ty][tx]].flags & TF_SOLID) ? 5 : TRACK;
            for (plane = 0; plane < DEPTH; plane++)
                if (pen & (1 << plane)) preview[plane][y][x / 8] |= 0x80 >> (x & 7);
        }
    editor_menu_begin();
    label(19, 28, GOLD, "VALIDATE MAP");
    label(16, 63, TEAM_ONE, "TEAM 1");
    label(16, 78, TEAM_TWO, "TEAM 2");
    label(16, 100, WHITE, "DOTTED");
    label(16, 114, WHITE, "BREACH");
    label(220, 64, GOLD, ed_check.errors ? "BLOCKED" : ed_check.count ? "WARNINGS" : "CLEAR");
    WaitBlit();
    for (i = 0; i < DEPTH; i++)
        for (y = 0; y < S; y++)
            memcpy(frame->Planes[i] + (Y + y) * frame->BytesPerRow + X / 8, preview[i][y], S / 8);
    for (i = 0; i < ed_check.count; i++) {
        const MapIssue *p = &ed_check.issues[i];
        if (map_issue_has_location(m, p)) {
            x = X + p->x * S / m->w; y = Y + p->y * S / m->h;
            fill(INK, x - 2, y - 2, x + 2, y + 2);
            fill(i == ed_check_row ? GOLD : WHITE, x - 1, y - 1, x + 1, y + 1);
        }
    }
    for (i = ed_check_scroll; i < ed_check.count && i < ed_check_scroll + MAP_CHECK_ROWS; i++) {
        int top = CHECK_TOP + CHECK_ROW_H * (i - ed_check_scroll);
        if (i == ed_check_row) fill(INK, 13, top, 305, top + CHECK_ROW_H - 1);
        label(16, top + 9, i < ed_check.errors ? GOLD : WHITE, ed_check.issues[i].text);
    }
    menu_option_row(0, 16, 127, 242, "", NULL);
    label(23, 242, ed_can_jump() ? GOLD : 5, "JUMP TO ISSUE");
    menu_option_row(0, 245, 303, 242, "BACK", NULL);
    if (ed_check.count) label(137, 242, WHITE, ed_can_jump() ? "FIRE JUMP" : "NO LOCATION");
    editor_menu_present();
}

static void ed_start_area(int x, int y)
{
    int team, radius = (3 * game.map.w + OVERVIEW_SIZE - 1) / OVERVIEW_SIZE;
    for (team = 0; team < 2; team++) {
        const Base *b = &game.bases[team];
        if (x >= b->x0 - radius && y >= b->y0 - radius && x <= b->x1 + radius && y <= b->y1 + radius) {
            ed_area_team = team;
            break;
        }
    }
    map_area_start(&ed_area, game.bases, ed_area_team, x, y, radius);
    ed_note[0] = 0;
}

static void ed_finish_area(void)
{
    int changed = map_area_finish(&ed_area, &game.map, game.bases);
    if (changed > 0) {
        map_editor_update_turrets(&game.map, game.bases);
        ed_saved = 0;
        strcpy(ed_note, "TEAM AREA UPDATED");
    }
    if (changed < 0) strcpy(ed_note, "AREA CONFLICT OR GARAGE / FLAG OUTSIDE");
    ed_area_keyboard = 0;
}

static const int details_y[] = { 34, 65, 135, 166, 201 };
static char *ed_detail_field(int row, size_t *cap)
{
    MapDetails *d = &game.map.details;
    switch (row) {
    case 0: *cap = 41; return ed_name;
    case 1: *cap = sizeof d->description; return d->description;
    case 2: *cap = sizeof d->author; return d->author;
    default: *cap = sizeof d->contact; return d->contact;
    }
}

static void ed_detail_choose(int row)
{
    size_t cap;
    if (ed_detail_edit >= 0) {
        char *field = ed_detail_field(ed_detail_edit, &cap);
        if (strcmp(field, ed_detail_draft)) { snprintf(field, cap, "%s", ed_detail_draft); ed_saved = 0; }
        ed_detail_edit = -1;
    } else if (row == 4) {
        ed_details = 0;
        ed_wait_fire = 1;
    } else {
        ed_detail_edit = row;
        snprintf(ed_detail_draft, sizeof ed_detail_draft, "%s", ed_detail_field(row, &cap));
    }
    ed_dirty = 1;
}

static void ed_render_details(void)
{
    static const char *const names[] = { "NAME", "DESCRIPTION", "AUTHOR", "CONTACT" };
    int i;
    editor_menu_begin();
    label(18, 23, GOLD, "MAP DETAILS");
    for (i = 0; i < 4; i++) {
        size_t cap, len;
        int y = details_y[i], lines = i == 1 ? 5 : 1, line;
        const char *value = i == ed_detail_edit ? ed_detail_draft : ed_detail_field(i, &cap);
        len = strlen(value);
        if (lines == 1 && len > 35) value += len - 35;
        inset(14, y, 292, details_y[i + 1] - y - 3, i == ed_detail_row ? GOLD : TRACK);
        label(20, y + 10, i == ed_detail_row ? GOLD : WHITE, names[i]);
        for (line = 0; line < lines && *value; line++) {
            char chunk[36];
            size_t n = strlen(value);
            if (n > sizeof chunk - 1) n = sizeof chunk - 1;
            memcpy(chunk, value, n); chunk[n] = 0;
            label(20, y + 21 + 10 * line, WHITE, chunk);
            value += n;
        }
    }
    inset(14, details_y[4], 292, 20, ed_detail_row == 4 ? GOLD : TRACK);
    label(20, 215, WHITE, ed_detail_edit >= 0 ? "APPLY FIELD" : "DONE");
    label(16, 237, WHITE, ed_detail_edit >= 0 ? "ENTER APPLY   ESC CANCEL" : "CONTACT IS PUBLIC WHEN UPLOADED");
    editor_menu_present();
}

int editor_text_input(UWORD code, UWORD qualifier)
{
    if (!ed_details || ed_detail_edit < 0) return 0;
    if (code == 0x44 || code == 0x43) ed_detail_choose(ed_detail_edit);
    else if (code == 0x45) ed_detail_edit = -1;
    else {
        size_t n = strlen(ed_detail_draft), cap;
        ed_detail_field(ed_detail_edit, &cap);
        if (code == 0x41 && n) ed_detail_draft[n - 1] = 0;
        else if (KeymapBase && n + 1 < cap) {
            struct InputEvent event = {0};
            char raw[8];
            event.ie_Class = IECLASS_RAWKEY;
            event.ie_Code = code;
            event.ie_Qualifier = qualifier;
            if (MapRawKey(&event, raw, sizeof raw, NULL) == 1 && raw[0] >= ' ' && raw[0] <= '~') {
                ed_detail_draft[n] = raw[0];
                ed_detail_draft[n + 1] = 0;
            }
        }
    }
    ed_dirty = 1;
    return 1;
}

void editor_mouse(ULONG type, UWORD code, int x, int y)
{
    if (ed_recover) return;
    if (ed_checking) {
        if (type == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) {
            if (y >= CHECK_BUTTON_TOP && y <= 246) {
                if (x >= 16 && x <= 127) ed_jump_issue();
                else if (x >= 245 && x <= 303) editor_toggle_menu();
            } else if (x >= 13 && x <= 305 && y >= CHECK_TOP && y < CHECK_TOP + CHECK_ROW_H * MAP_CHECK_ROWS) {
                int hit = ed_check_scroll + (y - CHECK_TOP) / CHECK_ROW_H;
                if (hit < ed_check.count) ed_select_issue(hit);
            }
        }
        return;
    }
    if (ed_details) {
        if (type == IDCMP_MOUSEBUTTONS && code == SELECTDOWN && x >= 14 && x < 306 && y >= details_y[0] && y < 221) {
            int row = 4;
            while (y < details_y[row]) row--;
            if (ed_detail_edit < 0 || row == 4) { ed_detail_row = row; ed_detail_choose(row); }
        }
        return;
    }
    if (ed_zoom) {
        int hit = x >= OVERVIEW_X && x < OVERVIEW_X + OVERVIEW_SIZE && y >= OVERVIEW_Y && y < OVERVIEW_Y + OVERVIEW_SIZE;
        int tx = (x - OVERVIEW_X) * game.map.w / OVERVIEW_SIZE, ty = (y - OVERVIEW_Y) * game.map.h / OVERVIEW_SIZE;
        if (ed_zoom == 2 && ed_area.active && !ed_area_keyboard) {
            map_area_move(&ed_area, &game.map, tx, ty);
            if (type == IDCMP_MOUSEBUTTONS && code == SELECTUP) ed_finish_area();
            ed_dirty = 1;
        } else if (type == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) {
            if (ed_zoom == 2 && !ed_area.active && x >= 8 && x < 73 && y >= 54 && y < 94) {
                ed_area_team = y >= 76;
                ed_dirty = 1;
            } else if (ed_zoom == 2 && hit && !ed_area.active) {
                ed_start_area(tx, ty);
                ed_dirty = 1;
            } else if (ed_zoom == 1 && hit) {
                ed_zoom_x = tx; ed_zoom_y = ty;
                ed_zoom_jump();
            }
        }
        return;
    }
    if (ed_menu) {
        int spacing = ed_page == ED_OPTIONS ? 19 : 24;
        int row = (y - 44) / spacing;
        if (type != IDCMP_MOUSEBUTTONS || code != SELECTDOWN || x < 20 || x >= 300) return;
        if (ed_confirm >= 0) {
            if (y >= 123 && y < 141) { ed_yes = x >= 160; ed_select = 1; }
        } else if (y >= 44 && row < ed_rows[ed_page] && ed_menu_row_enabled(row)) {
            ed_row = row;
            ed_select = 1;
        }
        return;
    }
    if (type == IDCMP_MOUSEMOVE) {
        if (panning) {
            int dx = x - pan_x, dy = y - pan_y;
            pan_distance += abs(dx) + abs(dy);
            if (pan_distance >= 3) {
                cam_x -= dx;
                cam_y -= dy;
                ed_clamp_camera();
                pan_x = x;
                pan_y = y;
            }
        }
        if (on_map(x, y)) {
            int old_x = ed_x, old_y = ed_y, target_x, target_y;
            ed_hover(x, y);
            target_x = ed_x;
            target_y = ed_y;
            if (painting && !panning) {
                ed_x = old_x; ed_y = old_y;
                while (ed_x != target_x || ed_y != target_y) {
                    ed_x += (target_x > ed_x) - (target_x < ed_x);
                    ed_y += (target_y > ed_y) - (target_y < ed_y);
                    ed_paint_tile((TileType)ed_brushes[ed_tool()].kind);
                }
            }
        }
    } else if (code == MENUDOWN && on_map(x, y)) {
        panning = 1; pan_x = x; pan_y = y; pan_distance = 0;
    } else if (code == MENUUP) {
        if (panning && pan_distance < 3 && abs(x - pan_x) + abs(y - pan_y) < 3 && on_map(x, y)) {
            ed_hover(x, y);
            ed_paint_tile(TILE_SAND);
        }
        panning = 0;
    } else if (code == SELECTDOWN) {
        if (x >= 0 && x < WIDTH && y >= 236 && y < 253) {
            int button = 0;
            while (x >= bar_edge[button + 1]) button++;
            if (button == BAR_MAP) ed_toggle_zoom();
            else if (button == BAR_AREA) {
                if (game.map.custom_bases) { ed_toggle_zoom(); ed_zoom = 2; ed_note[0] = 0; }
                else strcpy(ed_note, "IMPORTED MAP AREAS ARE FIXED");
            } else if (button == BAR_TIME) ed_show_time ^= 1;
            else if (button == BAR_WEATHER) {
                if (ed_weather_allowed()) ed_show_weather ^= 1;
            }
            else { ed_details = ed_dirty = 1; ed_detail_row = 0; }
        }
        else if (on_map(x, y) && !panning) {
            ed_hover(x, y);
            painting = ed_tool() < TERRAIN_BRUSHES;
            ed_place();
        } else if (x >= EDITOR_WIDTH && y >= TOP && y < EDITOR_BOTTOM) {
            if (y < 34) {
                if (x < 260 || x >= 300) ed_switch_category(x < 260 ? -1 : 1);
            } else if (y < BRUSH_TOP) ed_scroll_brushes(-BRUSH_VISIBLE);
            else if (y >= BRUSH_TOP + BRUSH_VISIBLE * BRUSH_ROW) ed_scroll_brushes(BRUSH_VISIBLE);
            else {
                uint8_t list[sizeof base_tools];
                int row = ed_first_brush + (y - BRUSH_TOP) / BRUSH_ROW;
                if (row < ed_tools(list)) ed_select_brush(row);
            }
        }
    } else if (code == SELECTUP) painting = 0;
}

int editor_time_preview(void) { return ed_show_time ? game.map.time : TIME_DAY; }

void editor_raw_key(UWORD code) { ed_pressed[code & 0x7f] = 1; }

void editor_wheel(int x, int y, int delta)
{
    if (ed_checking) ed_select_issue(ed_check_row + delta);
    else if (!ed_details && !ed_menu && !ed_zoom && x >= EDITOR_WIDTH && y >= TOP && y < EDITOR_BOTTOM) ed_scroll_brushes(delta);
}

static void ed_show_page(int page)
{
    ed_page = page;
    ed_row = 0;
    ed_dirty = 1;
}

void editor_toggle_menu(void)
{
    if (ed_recover) return;
    if (ed_checking) { ed_checking = 0; ed_wait_fire = 1; }
    else if (ed_details) { ed_details = 0; ed_wait_fire = 1; }
    else if (ed_zoom) ed_toggle_zoom();
    else if (ed_confirm >= 0) ed_confirm = -1;
    else if (ed_menu && ed_page != ED_MAIN) ed_show_page(ED_MAIN);
    else {
        ed_menu = !ed_menu;
        if (ed_menu) ed_show_page(ED_MAIN);
    }
    ed_dirty = 1;
    editor_cancel_mouse();
    ed_next_move = 0;
    local_draw = net_ms();
}

static void ed_render_menu(void)
{
    static const char *const headings[] = { "MAP EDITOR", "FILE", "OPTIONS", "NETWORK" };
    static const char *const names[][9] = {
        { "FILE  >", "OPTIONS  >", "NETWORK  >", "TEST RUN MAP", "VALIDATE MAP", "BACK TO EDITOR", "BACK TO MAIN MENU" },
        { "OPEN", "SAVE", "RANDOM MAP", "ERASE MAP", "BACK" },
        { "SET WORLD", "SET WEATHER", "SET TIME OF DAY", "SOUNDTRACK", "SET SIZE", "GAME MODE", "PLAYERS", "APPLY", "BACK" },
        { "UPLOAD MAP TO GALLERY", "BACK" }
    };
    int i;
    editor_menu_begin();
    if (ed_confirm >= 0) {
        const char *warning = ed_confirm == ED_REPLACE_SAVE ? "REPLACE THE SAVED MY MAP?" :
                              ed_confirm == ED_APPLY ? "RESIZING ERASES THE CURRENT MAP." :
                              ed_confirm == ED_EXIT ? "EXIT AND DISCARD UNSAVED CHANGES?" :
                              "REPLACE THE CURRENT MAP?";
        label(22, 32, GOLD, "CONFIRM");
        label(22, 83, WHITE, warning);
        if (ed_confirm != ED_REPLACE_SAVE && !ed_saved) label(22, 101, WHITE, "UNSAVED CHANGES WILL BE LOST.");
        menu_option_row(!ed_yes, 20, 153, 135, "CANCEL", NULL);
        menu_option_row(ed_yes, 163, 299, 135, "CONTINUE", NULL);
    } else {
        char players[8];
        const char *values[9] = { NULL };
        if (ed_page == ED_OPTIONS) {
            values[0] = world_names[ed_option_world];
            if (ed_option_weather_allowed()) values[1] = weather_label(ed_option_weather);
            values[2] = time_name(ed_option_time);
            values[3] = soundtrack_title(soundtrack_for_map(ed_option_world, ed_option_music));
            values[4] = map_size_name[ed_option_size];
            values[5] = (game.map.custom_bases || ed_option_size != ed_size) ? map_mode_names[ed_option_mode] : "IMPORTED";
            snprintf(players, sizeof players, "%d", ed_option_players);
            values[6] = (game.map.custom_bases || ed_option_size != ed_size) ? players : "IMPORTED";
        }
        label(22, 32, GOLD, headings[ed_page]);
        for (i = 0; i < ed_rows[ed_page]; i++) {
            int y = 53 + i * (ed_page == ED_OPTIONS ? 19 : 24);
            if (ed_menu_row_enabled(i)) menu_option_row(i == ed_row, 20, 299, y, names[ed_page][i], values[i]);
            else {
                menu_option_row(0, 20, 299, y, "", NULL);
                label(28, y, TRACK, names[ed_page][i]);
            }
        }
        label(22, 237, GOLD, ed_note[0] ? ed_note :
              ed_page == ED_OPTIONS && ed_option_auto_mode && (game.map.custom_bases || ed_option_size != ed_size) ?
                  mode_auto_notice : "FIRE SELECT   MINE BACK");
    }
    editor_menu_present();
}

static int ed_load_file(const char *path)
{
    Map *map;
    Base bases[2];
    Biome biome;
    char name[sizeof ed_name];
    size_t size;
    FILE *file = fopen(path, "r");
    if (!file) return 0;
    size = fread(ed_text, 1, sizeof ed_text - 1, file);
    if (ferror(file) || fgetc(file) != EOF) size = 0;
    fclose(file);
    if (!size || !(map = malloc(sizeof *map))) return 0;
    ed_text[size] = 0;
    memset(map, 0, sizeof *map);
    map_set_next_size(SIZE_LARGE);
    if (mapfile_load(map, bases, &biome, name, sizeof name, ed_text, ed_seed) != 0) {
        free(map);
        return 0;
    }
    game.map = *map;
    snprintf(ed_name, sizeof ed_name, "%s", name);
    memcpy(game.bases, bases, sizeof bases);
    free(map);
    ed_world = biome == BIOME_SCIFI ? BIOME_MOON : biome;
    game.map.weather = (uint8_t)weather_visual((Biome)ed_world, game.map.weather);
    ed_size = game.map.size;
    ed_brush = ed_first_brush = 0;
    ed_saved = 1;
    ed_linked = !strcmp(path, MY_MAP_PATH);
    ed_x = game.map.w / 2;
    ed_y = game.map.h / 2;
    ed_prepare_map();
    return 1;
}

static void ed_open(void)
{
#ifndef OFP_ART_PREVIEW
    AslBase = OpenLibrary((CONST_STRPTR)"asl.library", 38);
    if (AslBase) {
        struct FileRequester *request = AllocAslRequestTags(ASL_FileRequest,
            ASLFR_Window, (ULONG)window, ASLFR_SleepWindow, TRUE, ASLFR_TitleText, (ULONG)"Open map",
            ASLFR_InitialDrawer, (ULONG)"PROGDIR:maps", ASLFR_AcceptPattern, (ULONG)"#?.ofpmap", TAG_DONE);
        if (request && AslRequestTags(request, TAG_DONE)) {
            char path[512];
            snprintf(path, sizeof path, "%s", request->fr_Drawer);
            strcpy(ed_note, AddPart((STRPTR)path, request->fr_File, sizeof path) &&
                   ed_load_file(path) ? "MAP OPENED" : "CAN'T OPEN THIS MAP");
        }
        if (request) FreeAslRequest(request);
        else strcpy(ed_note, "CAN'T OPEN FILE REQUESTER");
        CloseLibrary(AslBase);
        AslBase = NULL;
        memset(keys, 0, sizeof keys);
        editor_cancel_mouse();
        return;
    }
#endif
    strcpy(ed_note, ed_load_file(MY_MAP_PATH) ? "MY MAP OPENED" : "CAN'T OPEN MY MAP");
}

static void ed_test(void)
{
    MatchSetup setup = {0};
    const char *error = map_custom_error(&game.map, game.bases);
    if (error) { snprintf(ed_note, sizeof ed_note, "%s", error); return; }
    if (!mapfile_save(&game.map, game.bases, (Biome)ed_world, ed_name, ed_text, sizeof ed_text) ||
        !(ed_test_map = malloc(sizeof *ed_test_map))) {
        strcpy(ed_note, "CAN'T PREPARE TEST MAP");
        return;
    }
    *ed_test_map = game.map;
    memcpy(ed_test_bases, game.bases, sizeof ed_test_bases);
    ed_test_cam_x = cam_x;
    ed_test_cam_y = cam_y;
    setup.seed = ed_seed;
    setup.versus = 1;
    setup.players = 2;
    setup.relaxed_pace = 1;
    setup.cls[0] = setup.cls[1] = TANK_MEDIUM;
    setup.team[1] = TEAM_ENEMY;
    setup.steer[0] = menu_steer;
    if (game.map.arena) setup.extras = EXTRA_ANNIHILATION;
    setup.bot_commands = 1;
    if (!light_local_ai()) setup.bot[1] = AI_NORMAL;
    if (match_start(&game, &setup, ed_text) != 0) {
        editor_return_from_test();
        strcpy(ed_note, "CAN'T START TEST MAP");
        return;
    }
    ed_leave();
    solo = 1;
    original_look = game.biome == BIOME_CLASSIC;
    recorded = 1;
    local_playing = 1;
    local_tick = local_draw = net_ms();
    reset_field_bots();
    reset_match_view();
}

static void ed_action(int action)
{
    switch (action) {
    case ED_SHOW_FILE: ed_show_page(ED_FILE); break;
    case ED_SHOW_OPTIONS:
        ed_note[0] = 0;
        ed_option_auto_mode = 0;
        ed_option_world = ed_world;
        ed_option_weather = ed_weather_allowed() && weather_allowed((Biome)ed_world, game.map.weather) ?
                            game.map.weather : WEATHER_CLEAR;
        ed_option_time = game.map.time;
        ed_option_music = game.map.music;
        ed_option_size = ed_size;
        ed_option_mode = game.map.custom_bases ? game.map.mode : MAP_MODE_CLASSIC;
        ed_option_players = game.map.custom_bases ? game.map.players : map_custom_players(ed_size, 2, 0);
        ed_show_page(ED_OPTIONS);
        break;
    case ED_SHOW_NETWORK: ed_show_page(ED_NETWORK); break;
    case ED_BACK: ed_show_page(ED_MAIN); break;
    case ED_OPEN: ed_open(); break;
    case ED_RANDOM: ed_new(0); strcpy(ed_note, "NEW RANDOM MAP"); break;
    case ED_RESET: ed_new(1); strcpy(ed_note, "MAP ERASED"); break;
    case ED_APPLY: {
        int allow_weather = ed_option_weather_allowed();
        int weather = allow_weather && weather_allowed((Biome)ed_option_world, ed_option_weather) ?
                      ed_option_weather : WEATHER_CLEAR;
        if (game.map.custom_bases && ed_option_size == ed_size && !map_mode_has_flags(ed_option_mode) &&
            (map_custom_count(&game.map, 0, ST_FLAGHOUSE) || map_custom_count(&game.map, 1, ST_FLAGHOUSE))) {
            strcpy(ed_note, "REMOVE FLAG BUILDINGS FIRST");
            break;
        }
        if (ed_option_world != ed_world || ed_option_time != game.map.time ||
            weather != game.map.weather || ed_option_music != game.map.music || ed_option_size != ed_size ||
            (game.map.custom_bases && (ed_option_mode != game.map.mode || ed_option_players != game.map.players))) {
            ed_world = ed_option_world;
            game.map.time = (uint8_t)ed_option_time;
            if (game.map.custom_bases) {
                game.map.players = (uint8_t)ed_option_players;
                game.map.mode = (uint8_t)ed_option_mode;
            }
            if (ed_option_size != ed_size) {
                ed_size = ed_option_size;
                ed_new(1);
                game.map.players = (uint8_t)ed_option_players;
                game.map.mode = (uint8_t)ed_option_mode;
            } else ed_prepare_map();
            game.map.music = (uint8_t)ed_option_music;
            game.map.weather = (uint8_t)weather;
            game.map.has_weather = allow_weather;
            ed_brush = ed_first_brush = 0;
            ed_saved = 0;
            strcpy(ed_note, ed_option_auto_mode && game.map.custom_bases ? mode_auto_notice : "MAP OPTIONS APPLIED");
        }
        ed_show_page(ED_MAIN);
        break;
    }
    case ED_SAVE: case ED_REPLACE_SAVE: ed_save(); break;
    case ED_TEST: ed_test(); break;
    case ED_VALIDATE:
        map_check_playability(&game, &ed_check, game.map.num_points ? MAP_MODE_CONQUEST : MAP_MODE_CLASSIC);
        ed_check_row = ed_check_scroll = 0;
        ed_checking = 1;
        break;
    case ED_UPLOAD:
        if (share_job.pending) strcpy(ed_note, "UPLOAD IN PROGRESS");
        else if (!ed_saved || !ed_linked) strcpy(ed_note, "SAVE THIS MAP BEFORE UPLOADING");
        else if (amiga_share_start(&share_job, MY_MAP_PATH, settings.profile[settings.current].name) == 0)
            strcpy(ed_note, "UPLOADING TO THE GALLERY...");
        else strcpy(ed_note, "CAN'T START UPLOAD");
        break;
    case ED_RESUME: editor_toggle_menu(); break;
    case ED_EXIT: recovery_clear(ED_DRAFT); ed_leave(); break;
    }
    ed_dirty = 1;
}

static void ed_choose(int action)
{
    if ((!ed_saved && (action == ED_OPEN || action == ED_RANDOM || action == ED_RESET || action == ED_EXIT)) ||
        (action == ED_APPLY && ed_option_size != ed_size)) {
        ed_confirm = action;
    } else if (action == ED_SAVE && !ed_linked) {
        BPTR existing = Lock((CONST_STRPTR)MY_MAP_PATH, ACCESS_READ);
        if (!existing) existing = Lock((CONST_STRPTR)MY_MAP_BACKUP, ACCESS_READ);
        if (existing) {
            UnLock(existing);
            ed_confirm = ED_REPLACE_SAVE;
        } else ed_action(action);
    } else ed_action(action);
    ed_yes = 0;
    ed_dirty = 1;
}

static void ed_option_step(int action, int direction)
{
    switch (action) {
    case ED_WORLD: {
        int mode = ed_option_mode;
        ed_option_world = biome_step((Biome)ed_option_world, direction);
        ed_option_music = 0;
        ed_option_mode = map_editor_mode_for_world(ed_option_mode, (Biome)ed_option_world);
        if (mode != ed_option_mode) ed_option_auto_mode = ed_option_mode == MAP_MODE_FLAG;
        if (!ed_option_weather_allowed() || !weather_allowed((Biome)ed_option_world, ed_option_weather))
            ed_option_weather = WEATHER_CLEAR;
        break;
    }
    case ED_WEATHER:
        if (ed_option_weather_allowed())
            ed_option_weather = weather_step((Biome)ed_option_world, ed_option_weather, direction, 0);
        break;
    case ED_MUSIC: ed_option_music = soundtrack_step(ed_option_world, ed_option_music, direction); break;
    case ED_TIME: ed_option_time = (ed_option_time + TIME_COUNT + direction) % TIME_COUNT; break;
    case ED_GAME_MODE:
        if (game.map.custom_bases || ed_option_size != ed_size) ed_option_mode = (ed_option_mode + MAP_MODE_COUNT + direction) % MAP_MODE_COUNT;
        ed_option_auto_mode = 0;
        break;
    case ED_PLAYERS:
        if (game.map.custom_bases || ed_option_size != ed_size) ed_option_players = map_custom_players(ed_option_size, ed_option_players, direction);
        break;
    case ED_MAP_SIZE:
        do ed_option_size = (ed_option_size + MAP_SIZES + direction) % MAP_SIZES;
        while (!map_size_fits(ed_option_size));
        ed_option_players = map_custom_players(ed_option_size, ed_option_players, 0);
        break;
    }
    ed_dirty = 1;
}

void editor_step(uint32_t now)
{
    InputBits input = active ? player_input(0) : 0;
    InputBits held_joy = active && input_mode[0] != AMIGA_INPUT_KEYS ? player_joystick(0) : 0;
    InputBits pressed = held_joy & ~ed_joy;
    int dx, dy, k;
    ed_joy = held_joy;
    ed_test_joy &= held_joy;
    input &= ~ed_test_joy;
    for (k = 0; k < 128; k++) if (ed_pressed[k]) {
        InputBits action = player_key_action(0, (UWORD)k);
        if (!action && (k == 0x44 || k == 0x43)) action = IN_FIRE;
        if (!ed_recover && !ed_menu && !ed_details && k == 0x42) {
            if (ed_zoom == 2 && !ed_area.active) { ed_area_team ^= 1; ed_dirty = 1; }
            else if (!ed_zoom) ed_switch_category(1);
            continue;
        }
        if (!ed_recover && !action && !ed_menu && !ed_zoom && !ed_details) {
            if (k >= 0x01 && k < 0x01 + TERRAIN_BRUSHES) {
                static const uint8_t slots[][2] = {
                    { CAT_GROUND, 0 }, { CAT_ROADS, 0 }, { CAT_GROUND, 1 }, { CAT_NATURE, 0 },
                    { CAT_NATURE, 1 }, { CAT_NATURE, 2 }, { CAT_BASE, 0 }
                };
                if (k - 1 >= (int)(sizeof slots / sizeof slots[0])) continue;
                ed_category = slots[k - 1][0];
                ed_first_brush = 0;
                ed_select_brush(slots[k - 1][1]);
            } else if (k == 0x52 || k == 0x53) {
                uint8_t list[sizeof base_tools];
                int count = ed_tools(list);
                ed_select_brush((ed_brush + count + (k == 0x52 ? -1 : 1)) % count);
            } else if (k == 0x54 || k == 0x55) ed_switch_category(k == 0x54 ? -1 : 1);
        }
        pressed |= action;
    }
    /* Mouse menu clicks use a separate selection from a remapped Return. */
    if (!ed_recover && !ed_menu && !ed_details && ed_pressed[0x56]) {
        memset(ed_pressed, 0, sizeof ed_pressed);
        ed_toggle_zoom();
        return;
    }
    if (ed_select) pressed |= IN_FIRE;
    ed_select = 0;
    memset(ed_pressed, 0, sizeof ed_pressed);
    if (ed_recover) {
        if (pressed & IN_FIRE) {
            ed_recover = 0;
            Biome biome = BIOME_CLASSIC;
            if (recovery_read(ED_DRAFT, ed_text, sizeof ed_text) &&
                !mapfile_load_draft(&game.map, game.bases, &biome, ed_name, sizeof ed_name, ed_text, ed_seed)) {
                ed_world = biome; ed_size = game.map.size;
                ed_saved = ed_linked = 0; ed_prepare_map();
                strcpy(ed_note, "DRAFT RESTORED - SAVE WHEN READY");
            }
            ed_wait_fire = 1; ed_dirty = 1;
        } else if (pressed & IN_MINE) {
            ed_recover = 0; recovery_clear(ED_DRAFT); ed_dirty = 1;
        }
        return;
    }
    if ((uint32_t)(now - ed_autosave_at) >= 30000) {
        BPTR directory;
        ed_autosave_at = now;
        directory = CreateDir((CONST_STRPTR)"PROGDIR:maps"); if (directory) UnLock(directory);
        if (!ed_saved && mapfile_save_draft(&game.map, game.bases, (Biome)ed_world, ed_name, ed_text, sizeof ed_text) &&
            recovery_write(ED_DRAFT, ed_text)) strcpy(ed_note, "COULD NOT SAVE RECOVERY DRAFT");
    }
    if (pressed & IN_MINE) {
        editor_toggle_menu();
        return;
    }
    if (share_job.pending) {
        int result = amiga_share_poll(&share_job);
        if (result) {
            strcpy(ed_note, result == 1 ? "UPLOADED TO THE GALLERY" :
                result == -2 ? "DAILY UPLOAD LIMIT REACHED" : "UPLOAD FAILED");
            ed_dirty = 1;
        }
    }
    dx = !!(input & IN_RIGHT) - !!(input & IN_LEFT);
    dy = !!(input & IN_DOWN) - !!(input & IN_UP);
    if (ed_checking) {
        if (pressed & (IN_UP | IN_DOWN)) ed_select_issue(ed_check_row + ((pressed & IN_DOWN) ? 1 : -1));
        if (pressed & IN_FIRE) {
            if (!ed_check.count) editor_toggle_menu();
            else ed_jump_issue();
            if (!ed_checking) return;
        }
        if (ed_dirty) { ed_render_check(); ed_dirty = 0; }
        return;
    }
    if (ed_details) {
        if (ed_detail_edit < 0) {
            if (pressed & (IN_UP | IN_DOWN)) {
                ed_detail_row = (ed_detail_row + ((pressed & IN_UP) ? 4 : 1)) % 5;
                ed_dirty = 1;
            }
            if (pressed & IN_FIRE) ed_detail_choose(ed_detail_row);
        }
        if (ed_details && ed_dirty) { ed_render_details(); ed_dirty = 0; }
        return;
    }
    if (ed_menu) {
        if (ed_confirm >= 0) {
            if (pressed & (IN_UP | IN_DOWN)) ed_yes ^= 1;
            if (pressed & (IN_LEFT | IN_RIGHT)) ed_yes = !!(pressed & IN_RIGHT);
            if (pressed) ed_dirty = 1;
            if (pressed & IN_FIRE) {
                int action = ed_confirm;
                ed_confirm = -1;
                if (ed_yes) ed_action(action);
            }
        } else {
            int action;
            if (pressed & (IN_UP | IN_DOWN)) {
                int rows = ed_rows[ed_page];
                int direction = pressed & IN_UP ? -1 : 1;
                do ed_row = (ed_row + rows + direction) % rows;
                while (!ed_menu_row_enabled(ed_row));
                ed_dirty = 1;
            }
            action = ed_actions[ed_page][ed_row];
            if (ed_page == ED_OPTIONS && action >= ED_WORLD && action <= ED_PLAYERS) {
                if (pressed & (IN_FIRE | IN_LEFT | IN_RIGHT)) ed_option_step(action, (pressed & IN_LEFT) ? -1 : 1);
            } else if (pressed & IN_FIRE) ed_choose(action);
        }
        if (editor_on && ed_menu && !ed_checking && ed_dirty) {
            ed_render_menu();
            ed_dirty = 0;
        }
        return;
    }
    if (!dx) dx = !!(pressed & IN_RIGHT) - !!(pressed & IN_LEFT);
    if (!dy) dy = !!(pressed & IN_DOWN) - !!(pressed & IN_UP);
    if (ed_zoom) {
        if (pressed & IN_FIRE) {
            if (ed_zoom == 1) { ed_zoom_jump(); return; }
            if (ed_area_keyboard) ed_finish_area();
            else if (!ed_area.active) { ed_start_area(ed_zoom_x, ed_zoom_y); ed_area_keyboard = 1; }
            ed_dirty = 1;
        }
        if ((dx || dy) && (!ed_next_move || (int32_t)(now - ed_next_move) >= 0)) {
            int step = ed_zoom == 2 ? 1 : 4;
            ed_zoom_x += dx * step;
            ed_zoom_y += dy * step;
            if (ed_zoom_x < 0) ed_zoom_x = 0;
            if (ed_zoom_y < 0) ed_zoom_y = 0;
            if (ed_zoom_x >= game.map.w) ed_zoom_x = game.map.w - 1;
            if (ed_zoom_y >= game.map.h) ed_zoom_y = game.map.h - 1;
            if (ed_area_keyboard) map_area_move(&ed_area, &game.map, ed_zoom_x, ed_zoom_y);
            ed_next_move = now + (ed_next_move ? 80 : 220);
            ed_dirty = 1;
        }
        if (!dx && !dy) ed_next_move = 0;
        if (ed_dirty) { ed_render_zoom(); ed_dirty = 0; }
        return;
    }
    if ((dx || dy) && !panning && (!ed_next_move || (int32_t)(now - ed_next_move) >= 0)) {
        ed_x += dx; ed_y += dy;
        if (ed_x < 1) ed_x = 1;
        if (ed_y < 1) ed_y = 1;
        if (ed_x > game.map.w - 2) ed_x = game.map.w - 2;
        if (ed_y > game.map.h - 2) ed_y = game.map.h - 2;
        ed_center();
        ed_next_move = now + (ed_next_move ? 70 : 220);
    }
    if (!dx && !dy) ed_next_move = 0;
    if (ed_wait_fire && !(input & IN_FIRE)) ed_wait_fire = 0;
    if (!ed_wait_fire && !panning && ((ed_tool() < TERRAIN_BRUSHES ? input | pressed : pressed) & IN_FIRE)) ed_place();
    if ((int32_t)(now - local_draw) >= 0) {
        game.weather = ed_show_weather && game.map.has_weather && ed_weather_allowed() &&
                       weather_allowed((Biome)ed_world, game.map.weather) ?
            (uint8_t)weather_preview((Biome)ed_world, game.map.weather, now) : WEATHER_CLEAR;
        draw_frame();
        local_draw = now + ONLINE_DRAW_MS;
    }
}

/* LOCAL GAME's MY MAP: the editor's last saved map */
const char *my_map_text(void)
{
    static char text[MAPFILE_MAX];
    FILE *f = fopen(MY_MAP_PATH, "r");
    size_t n;
    if (!f) f = fopen(MY_MAP_BACKUP, "r");
    if (!f) return NULL;
    n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    return n ? text : NULL;
}

void read_my_map_metadata(void)
{
    const char *text = my_map_text(), *value;
    int i;
    if (!text) return;
    my_map_size = SIZE_LARGE;
    my_map_arena = strstr(text, "\narena=yes\n") != NULL;
    value = strstr(text, "\nmode=");
    my_map_mode = value && strstr(text, "\nbases=custom\n") ? map_mode_from_name(value + 6) : -1;
    value = strstr(text, "\nsize=");
    if (value) {
        value += 6;
        for (i = 0; i < MAP_SIZES; i++)
            if (!strncmp(value, map_size_name[i], strlen(map_size_name[i]))) { my_map_size = i; break; }
    }
    value = strstr(text, "\nbiome=");
    if (value) {
        char name[20];
        size_t len;
        value += 7;
        len = strcspn(value, "\r\n");
        if (len < sizeof name) {
            memcpy(name, value, len);
            name[len] = 0;
            my_map_biome = biome_from_name(name);
        }
    }
}

/* Explicit diagnostic: uses its own draft names, never the user's recovery slots. */
int editor_recovery_test(void)
{
    static const char prefix[] = "PROGDIR:recovery-check";
    Biome biome = BIOME_CLASSIC; int ok = 1, i; FILE *f;
    map_set_next_size(SIZE_LARGE);
    memset(&game, 0, sizeof game);
    map_fill(&game.map, TILE_SAND);
    map_custom_begin(&game.map, game.bases, 4, MAP_MODE_CLASSIC);
    game.map.type[60][60] = TILE_WALL;
    recovery_clear(prefix);
    for (i = 0; i < 4; i++) {
        char name[24]; snprintf(name, sizeof name, "RECOVERY CHECK %d", i);
        ok &= !!mapfile_save_draft(&game.map, game.bases, BIOME_CLASSIC, name, ed_text, sizeof ed_text);
        ok &= recovery_write(prefix, ed_text) == 0;
    }
    f = fopen("PROGDIR:recovery-check.1.draft", "wb");
    if (f) { fputs("incomplete write", f); fclose(f); } else ok = 0;
    ok &= recovery_read(prefix, ed_text, sizeof ed_text);
    ok &= strstr(ed_text, "RECOVERY CHECK 2") != NULL;
    ok &= mapfile_load(&game.map, game.bases, &biome, NULL, 0, ed_text, 77) != 0;
    ok &= mapfile_load_draft(&game.map, game.bases, &biome, NULL, 0, ed_text, 77) == 0;
    ok &= game.map.type[60][60] == TILE_WALL && map_custom_error(&game.map, game.bases) != NULL;
    recovery_clear(prefix);
    printf("editor recovery: %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : -1;
}
