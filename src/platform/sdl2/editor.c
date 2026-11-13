/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/sdl2/editor.h"
#include "common/recovery.h"
#include "common/soundtrack.h"
#include "platform/sdl2/input.h"
#include "platform/sdl2/maps.h"
#include "platform/sdl2/render.h"
#include "platform/sdl2/ui.h"

enum { K_TILE, K_STRUCT, K_ERASE };
enum { TOOL_SAND, TOOL_ROAD, TOOL_WATER, TOOL_ROCK, TOOL_BUSH, TOOL_WALL, TOOL_RUBBLE,
       TOOL_TOWER, TOOL_BUNKER, TOOL_DEPOT, TOOL_BARRACKS, TOOL_DOME, TOOL_HQ, TOOL_AID, TOOL_POW,
       TOOL_RADAR, TOOL_POWER, TOOL_WAREHOUSE, TOOL_HELIPAD, TOOL_MG, TOOL_TWIN, TOOL_CANNON,
       TOOL_ROCKET, TOOL_HEAVY, TOOL_MORTAR, TOOL_CANOPY, TOOL_GATE_H,
       TOOL_GATE_V, TOOL_FLAG, TOOL_GARAGE, TOOL_ICE, TOOL_OIL, TOOL_CRATER, TOOL_ERASE };
static const struct { const char *name; int kind, v; } tools[] = {
    { "SAND", K_TILE, TILE_SAND }, { "ROAD", K_TILE, TILE_ROAD }, { "WATER", K_TILE, TILE_WATER },
    { "ROCK", K_TILE, TILE_ROCK }, { "BUSHES", K_TILE, TILE_TREE }, { "WALL", K_TILE, TILE_WALL },
    { "RUBBLE", K_TILE, TILE_RUBBLE },
    { "TOWER", K_STRUCT, ST_TOWER }, { "BUNKER", K_STRUCT, ST_BUNKER }, { "DEPOT", K_STRUCT, ST_DEPOT },
    { "BARRACKS", K_STRUCT, ST_BARRACKS }, { "BIO-DOME", K_STRUCT, ST_DOME }, { "HQ", K_STRUCT, ST_HQ },
    { "FIRST AID", K_STRUCT, ST_FIRSTAID }, { "POW CAMP", K_STRUCT, ST_POWCAMP }, { "RADAR", K_STRUCT, ST_RADAR },
    { "POWER", K_STRUCT, ST_POWER }, { "WAREHOUSE", K_STRUCT, ST_WAREHOUSE }, { "HELIPAD", K_STRUCT, ST_HELIPAD },
    { "MG NEST", K_STRUCT, ST_T_MG }, { "TWIN GUN", K_STRUCT, ST_T_TWIN }, { "CANNON", K_STRUCT, ST_T_CANNON },
    { "ROCKETS", K_STRUCT, ST_T_ROCKET }, { "HEAVY GUN", K_STRUCT, ST_T_HEAVY }, { "MORTAR", K_STRUCT, ST_T_MORTAR },
    { "TREE TOP", K_TILE, TILE_CANOPY }, { "GATE H", K_STRUCT, ST_GATE_H },
    { "GATE V", K_STRUCT, ST_GATE_V }, { "FLAG BASE", K_STRUCT, ST_FLAGHOUSE }, { "GARAGE", K_STRUCT, ST_GARAGE }, { "ICE", K_TILE, TILE_ICE }, { "OIL SPILL", K_TILE, TILE_OIL }, { "CRATER", K_TILE, TILE_CRATER }, { "ERASE", K_ERASE, 0 },
};
enum { TOOL_TOP = 46, TOOL_ROW = 11, TOOLS_VISIBLE = 10, SIDEBAR_X = 289, BAR_Y = 199 };
enum { CAT_GROUND, CAT_ROADS, CAT_NATURE, CAT_BASE, CATEGORY_COUNT };
enum { BAR_MAP, BAR_AREA, BAR_TIME, BAR_WEATHER, BAR_DETAILS, BAR_COUNT };
static const uint8_t ground[] = { TOOL_SAND, TOOL_WATER, TOOL_ICE, TOOL_CRATER, TOOL_ERASE };
static const uint8_t roads[] = { TOOL_ROAD, TOOL_RUBBLE, TOOL_OIL };
static const uint8_t nature[] = { TOOL_ROCK, TOOL_BUSH, TOOL_CANOPY };
static const uint8_t base[] = { TOOL_WALL, TOOL_GATE_H, TOOL_GATE_V, TOOL_GARAGE, TOOL_FLAG, TOOL_HQ,
    TOOL_AID, TOOL_BARRACKS, TOOL_RADAR, TOOL_TOWER, TOOL_BUNKER, TOOL_DEPOT, TOOL_DOME, TOOL_POW,
    TOOL_POWER, TOOL_WAREHOUSE, TOOL_HELIPAD, TOOL_MG, TOOL_TWIN, TOOL_CANNON, TOOL_ROCKET, TOOL_HEAVY, TOOL_MORTAR };
static const struct { const char *name; const uint8_t *tools; int count; } categories[] = {
    { "GROUND", ground, sizeof ground }, { "ROADS", roads, sizeof roads },
    { "NATURE", nature, sizeof nature }, { "BASE", base, sizeof base }
};
static const SDL_Rect bar[BAR_COUNT] = {
    {8, BAR_Y, 48, 14}, {59, BAR_Y, 72, 14}, {134, BAR_Y, 66, 14}, {203, BAR_Y, 91, 14}, {297, BAR_Y, 80, 14}
};
static int n_undo, n_redo;

static int category_tools(const Editor *e, uint8_t *list)
{
    int i, n = 0;
    for (i = 0; i < categories[e->category].count; i++) {
        int tool = categories[e->category].tools[i];
        if (tools[tool].kind == K_TILE && !map_surface_allowed((Biome)e->g->biome,
            e->g->biome == BIOME_CLASSIC || (e->g->map.custom_bases && e->g->map.mode == MAP_MODE_CLASSIC), (TileType)tools[tool].v)) continue;
        if (tool == TOOL_GARAGE && !e->g->map.custom_bases) continue;
        if (tool == TOOL_FLAG && e->g->map.custom_bases && !map_mode_has_flags(e->g->map.mode)) continue;
        list[n++] = (uint8_t)tool;
    }
    return n;
}

static void select_tool(Editor *e, int index)
{
    uint8_t list[sizeof base];
    int n = category_tools(e, list);
    if (index < 0 || index >= n) return;
    e->tool = list[index];
    if (index < e->first_tool) e->first_tool = index;
    if (index >= e->first_tool + TOOLS_VISIBLE) e->first_tool = index - TOOLS_VISIBLE + 1;
}

static void switch_category(Editor *e, int delta)
{
    e->category = (e->category + delta + CATEGORY_COUNT) % CATEGORY_COUNT;
    e->first_tool = 0;
    select_tool(e, 0);
}

static void scroll_tools(Editor *e, int delta)
{
    uint8_t list[sizeof base];
    int max = category_tools(e, list) - TOOLS_VISIBLE;
    e->first_tool += delta;
    if (e->first_tool > max) e->first_tool = max;
    if (e->first_tool < 0) e->first_tool = 0;
}

static void step_tool(Editor *e, int delta)
{
    uint8_t list[sizeof base];
    int i, n = category_tools(e, list);
    for (i = 0; i < n && list[i] != e->tool; i++) {}
    select_tool(e, (i + n + delta) % n);
}

static const SDL_Rect VIEW = { 5, 3, 280, 193 };
static const SDL_Rect OVERVIEW = { 106, 22, 172, 172 };
static char text_buf[MAPFILE_MAX];

static void say(Editor *e, const char *m)
{
    snprintf(e->msg, sizeof e->msg, "%s", m);
    e->msg_t = 120;
}

/* only the map: no vehicles, soldiers, mines or helicopters while editing */
static void tidy(Game *g)
{
    g->num_props = 0;
    memset(g->men, 0, sizeof g->men);
    memset(g->mines, 0, sizeof g->mines);
    memset(g->helis, 0, sizeof g->helis);
    memset(g->effects, 0, sizeof g->effects);
    g->num_tanks = 1;                                    /* tank 0 is only the camera */
    memset(&g->tanks[0], 0, sizeof g->tanks[0]);
}

/* the view is the camera "tank" (tank 0); keep its centre inside the map */
static void set_view(Editor *e)
{
    Tank *t = &e->g->tanks[0];
    if (e->view_x < VIEW.w / 2) e->view_x = VIEW.w / 2;
    if (e->view_y < VIEW.h / 2) e->view_y = VIEW.h / 2;
    if (e->view_x > (e->g->map.w * TILE_SIZE) - VIEW.w / 2) e->view_x = (e->g->map.w * TILE_SIZE) - VIEW.w / 2;
    if (e->view_y > (e->g->map.h * TILE_SIZE) - VIEW.h / 2) e->view_y = (e->g->map.h * TILE_SIZE) - VIEW.h / 2;
    t->x = t->prev_x = FX(e->view_x);
    t->y = t->prev_y = FX(e->view_y);
}

static void place_camera(Editor *e)                   /* centre the view on the cursor */
{
    e->view_x = e->cx * TILE_SIZE + TILE_SIZE / 2;
    e->view_y = e->cy * TILE_SIZE + TILE_SIZE / 2;
    set_view(e);
}

static void keep_cursor_in_view(Editor *e)            /* nudge the view when the keys walk off it */
{
    int sx = e->cx * TILE_SIZE + TILE_SIZE / 2 - e->view_x, sy = e->cy * TILE_SIZE + TILE_SIZE / 2 - e->view_y;
    int mx = VIEW.w / 2 - 32, my = VIEW.h / 2 - 32;
    if (sx < -mx) e->view_x += sx + mx;
    if (sx > mx)  e->view_x += sx - mx;
    if (sy < -my) e->view_y += sy + my;
    if (sy > my)  e->view_y += sy - my;
    set_view(e);
}

/* L: the sets of modes a map can be made for (the map file's modes= line; the menu offers it for those) */
static const char *const ed_modes[6] = { NULL, "flag", "conquest", "deathmatch,warlord", "annihilation", "annihilation,deathmatch,warlord" };
static const char *const ed_modes_say[6] = { "MODES: ANY (CAMPS)", "MODES: CAPTURE THE FLAG", "MODES: CONQUEST",
                                             "MODES: TEAM DEATHMATCH, WARLORD", "MODES: ANNIHILATION", "MODES: EVERY ARENA FIGHT" };

/* the map as text, with its modes= line (the menu's: not the game's) */
static unsigned map_text(Editor *e, char *buf, unsigned cap)
{
    unsigned n = mapfile_save_draft(&e->g->map, e->g->bases, (Biome)e->g->biome, e->name, buf, cap);
    char *nl;
    if (!n || e->g->map.custom_bases || !e->modes || !(nl = strchr(buf, '\n')) || !(nl = strchr(nl + 1, '\n'))) return n;
    {
        char line[64];
        unsigned len = (unsigned)snprintf(line, sizeof line, "modes=%s\n", ed_modes[e->modes]);
        if (n + len >= cap) return n;
        memmove(nl + 1 + len, nl + 1, strlen(nl + 1) + 1);
        memcpy(nl + 1, line, len);
        return n + len;
    }
}

/* the map again from its text (the camps built, or not: an arena) */
static void rebuild(Editor *e)
{
    if (!map_text(e, text_buf, sizeof text_buf)) return;
    game_init_map(e->g, e->seed, text_buf);
    tidy(e->g);
}

/* P, O: a point at the cursor, or none if there's one there already; how many now (-1: full) */
static int toggle_point(int16_t *xs, int16_t *ys, uint8_t *n, int most, int cx, int cy)
{
    int k;
    for (k = 0; k < *n; k++)
        if (abs(xs[k] - cx) <= 1 && abs(ys[k] - cy) <= 1) {   /* there: it goes */
            for (; k + 1 < *n; k++) { xs[k] = xs[k + 1]; ys[k] = ys[k + 1]; }
            (*n)--;
            return 0;
        }
    if (*n >= most) return -1;
    xs[*n] = (int16_t)cx; ys[*n] = (int16_t)cy; (*n)++;
    return 1;
}

static int in_base(const Game *g, int x, int y);         /* (below) */

/* New maps start with bare ground and editable team areas. */
static void make_empty(Editor *e, int size)
{
    Game *g = e->g;
    Map *m = &g->map;
    int x, y, time = m->time, music = m->music, weather = m->weather, has_weather = m->has_weather;
    int players = m->custom_bases ? m->players : 2, mode = m->custom_bases ? m->mode : MAP_MODE_CLASSIC;
    MapDetails details = m->details;
    memset(m, 0, sizeof *m);
    m->details = details;
    map_set_size(m, size);
    m->time = (uint8_t)time;
    m->music = (uint8_t)music;
    m->weather = (uint8_t)weather;
    m->has_weather = (uint8_t)has_weather;
    for (y = 0; y < m->h; y++)
        for (x = 0; x < m->w; x++)
            map_set(m, x, y, x && y && x < m->w - 1 && y < m->h - 1 ? TILE_SAND : TILE_ROCK);
    map_custom_begin(m, g->bases, players, mode);
    e->modes = 0;
    e->mark = 0;
    e->cx = m->w / 2;
    e->cy = m->h / 2;
    tidy(g);
    place_camera(e);
}

static const char *draft_prefix(void)
{
    static char prefix[1100];
    snprintf(prefix, sizeof prefix, "%srecovery", maps_user_dir());
    return prefix;
}

static int editor_load_game(Game *g, const char *text, uint32_t seed, int draft)
{
    Biome biome = BIOME_CLASSIC;
    if (!draft) return game_init_map(g, seed, text);
    memset(g, 0, sizeof *g);
    if (mapfile_load_draft(&g->map, g->bases, &biome, NULL, 0, text, seed)) return -1;
    g->biome = (uint8_t)biome;
    return 0;
}
static void editor_open_impl(Editor *e, Game *g, const char *text, const char *path, int user, uint32_t seed, int biome, int draft)
{
    char saved_path[sizeof e->path];
    snprintf(saved_path, sizeof saved_path, "%s", path ? path : "");
    memset(e, 0, sizeof *e);
    n_undo = n_redo = 0;
    e->g = g;
    e->seed = seed;
    e->brush = 1;
    e->show_time = e->show_weather = 1;
    e->detail_edit = -1;
    if (!text || editor_load_game(g, text, seed, draft) != 0) {
        memset(g, 0, sizeof *g);
        g->biome = (uint8_t)biome;
        snprintf(e->name, sizeof e->name, "New map");
        make_empty(e, SIZE_LARGE);
        text = NULL;
    } else {
        if (draft) mapfile_load_draft(&g->map, g->bases, NULL, e->name, sizeof e->name, text, seed);
        else mapfile_load(&g->map, g->bases, NULL, e->name, sizeof e->name, text, seed);
        g->map.weather = (uint8_t)weather_visual((Biome)g->biome, g->map.weather);
        {   /* (its modes= line: which of L's sets) */
            const char *ml = strstr(text, "\nmodes=");
            int k;
            if (ml) {
                char v[64];
                int n = 0;
                ml += 7;
                while (ml[n] && ml[n] != '\n' && n < 63) { v[n] = ml[n]; n++; }
                v[n] = 0;
                for (k = 1; k < 6; k++) if (!strcmp(v, ed_modes[k])) e->modes = k;
                if (!e->modes) e->modes = strstr(v, "annihilation") ? 4 : strstr(v, "conquest") ? 2 : strstr(v, "deathmatch") ? 3 : 1;
            }
        }
        if (user && saved_path[0]) snprintf(e->path, sizeof e->path, "%s", saved_path);
        e->user = user && saved_path[0];
        if (g->map.arena) {
            e->cx = g->map.spawn_tx[0];
            e->cy = g->map.spawn_ty[0];
        } else {
            e->cx = (g->bases[TEAM_PLAYER].x0 + g->bases[TEAM_PLAYER].x1) / 2;
            e->cy = g->bases[TEAM_PLAYER].y0 - 4 > 2 ? g->bases[TEAM_PLAYER].y0 - 4 : g->bases[TEAM_PLAYER].y1 + 8;
        }
    }
    e->autosave_at = SDL_GetTicks();
    if (!text) e->recovery_pending = recovery_read(draft_prefix(), text_buf, sizeof text_buf);
    map_normalize_surfaces(&g->map, (Biome)g->biome, g->biome == BIOME_CLASSIC || (g->map.custom_bases && g->map.mode == MAP_MODE_CLASSIC));
    tidy(g);
    place_camera(e);

}

void editor_open(Editor *e, Game *g, const char *text, const char *path, int user, uint32_t seed, int biome)
{ editor_open_impl(e, g, text, path, user, seed, biome, 0); }

static int in_base(const Game *g, int x, int y)
{
    int k;
    for (k = 0; k < 2; k++) {
        const Base *b = &g->bases[k];
        if (x >= b->x0 - 1 && x <= b->x1 + 1 && y >= b->y0 - 1 && y <= b->y1 + 5) return 1;
    }
    return 0;
}

static void remove_structure(Map *m, int sid)
{
    Structure *st = &m->st[sid - 1];
    const StructInfo *si = &struct_info[st->type];
    int x, y;
    for (y = st->y; y < st->y + si->h; y++)
        for (x = st->x; x < st->x + si->w; x++)
            if (m->sid[y][x] == sid) map_set(m, x, y, TILE_SAND);
    st->type = ST_NONE;
}

static void paint_tile(Editor *e, int x, int y, TileType t)
{
    Map *m = &e->g->map;
    if (x < 1 || y < 1 || x >= m->w - 1 || y >= m->h - 1 || (!m->custom_bases && in_base(e->g, x, y))) return;
    if (m->sid[y][x]) { remove_structure(m, m->sid[y][x]); e->dirty = 1; }
    if (m->type[y][x] != (uint8_t)t) { map_set(m, x, y, t); e->dirty = 1; }
}

static int place(Editor *e, int type)
{
    Map *m = &e->g->map;
    const StructInfo *si = &struct_info[type];
    int x, y, x0 = e->cx, y0 = e->cy;
    int owner = map_editor_owner(m, e->g->bases, type, x0, y0);
    if (owner < 0) { say(e, "PLACE INSIDE A TEAM AREA"); return 0; }
    if (m->custom_bases && (type == ST_GARAGE || type == ST_FLAGHOUSE)) {
        if (type == ST_GARAGE && map_custom_count(m, owner, type) >= m->players / 2) {
            say(e, "THIS TEAM HAS ALL ITS GARAGES"); return 0;
        }
        if (type == ST_FLAGHOUSE && (!map_mode_has_flags(m->mode) || map_custom_count(m, owner, type))) {
            say(e, "FLAG BASE UNAVAILABLE"); return 0;
        }
    }
    for (y = y0; y < y0 + si->h; y++)
        for (x = x0; x < x0 + si->w; x++) {
            if (x < 1 || y < 1 || x >= e->g->map.w - 1 || y >= e->g->map.h - 1 || (!m->custom_bases && in_base(e->g, x, y))) {
                say(e, "NOT ON A BASE OR THE EDGE");
                return 0;
            }
            if (m->sid[y][x] || m->type[y][x] == TILE_WATER || m->type[y][x] == TILE_CRATER || m->type[y][x] == TILE_ROCK || m->type[y][x] == TILE_WALL) {
                say(e, "SOMETHING IS IN THE WAY");
                return 0;
            }
        }
    {
        int i, old_count = m->num_st, placed;
        for (i = 0; i < old_count && m->st[i].type != ST_NONE; i++) {}
        m->num_st = i;
        placed = map_place_structure(m, (StructType)type, x0, y0, owner);
        if (m->num_st < old_count) m->num_st = old_count;
        if (placed < 0) { say(e, "THE MAP IS FULL"); return 0; }
    }
    e->dirty = 1;
    return 1;
}

typedef struct { Map map; Base bases[2]; uint8_t biome; int modes; char name[64]; } Snap;
static Snap undo_s[32], redo_s[32];
static void snap_take(const Editor *e, Snap *s) { s->map = e->g->map; s->bases[0] = e->g->bases[0]; s->bases[1] = e->g->bases[1]; s->biome = e->g->biome; s->modes = e->modes; memcpy(s->name, e->name, sizeof s->name); }
static void snap_put(Editor *e, const Snap *s) { e->g->map = s->map; e->g->bases[0] = s->bases[0]; e->g->bases[1] = s->bases[1]; e->g->biome = s->biome; e->modes = s->modes; memcpy(e->name, s->name, sizeof e->name); e->dirty = 1; switch_category(e, 0); }

static void undo_push(Editor *e)
{
    if (n_undo == 32) { memmove(&undo_s[0], &undo_s[1], sizeof undo_s[0] * 31); n_undo = 31; }
    snap_take(e, &undo_s[n_undo++]);
    n_redo = 0;                                          /* (a new change: nothing to redo any more) */
}

static void undo(Editor *e, int redo)
{
    Snap *from = redo ? redo_s : undo_s, *to = redo ? undo_s : redo_s;
    int *nf = redo ? &n_redo : &n_undo, *nt = redo ? &n_undo : &n_redo;
    if (!*nf) { say(e, redo ? "NOTHING TO REDO" : "NOTHING TO UNDO"); return; }
    if (*nt == 32) { memmove(&to[0], &to[1], sizeof to[0] * 31); *nt = 31; }
    snap_take(e, &to[(*nt)++]);
    snap_put(e, &from[--*nf]);
    say(e, redo ? "REDONE" : "UNDONE");
}

/* ---- the fill and the clipboard ------------------------------------------------------------ */
static void fill(Editor *e, TileType t)               /* the ground of one kind, from the cursor out */
{
    static uint16_t q[MAP_W * MAP_H];
    static uint8_t seen[MAP_H][MAP_W];
    Map *m = &e->g->map;
    int head = 0, tail = 0, n = 0;
    uint8_t from = m->type[e->cy][e->cx];
    if (from == (uint8_t)t || m->sid[e->cy][e->cx]) return;
    memset(seen, 0, sizeof seen);
    q[tail++] = (uint16_t)(e->cy * MAP_W + e->cx);
    seen[e->cy][e->cx] = 1;
    while (head < tail) {
        int c = q[head++], x = c % MAP_W, y = c / MAP_W, d;
        static const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
        paint_tile(e, x, y, t);
        n++;
        for (d = 0; d < 4; d++) {
            int nx = x + dx[d], ny = y + dy[d];
            if (nx < 1 || ny < 1 || nx >= e->g->map.w - 1 || ny >= e->g->map.h - 1 || seen[ny][nx]) continue;
            if (m->type[ny][nx] != from || m->sid[ny][nx]) continue;
            seen[ny][nx] = 1;
            q[tail++] = (uint16_t)(ny * MAP_W + nx);
        }
    }
    { char b[48]; snprintf(b, sizeof b, "FILLED %d TILES", n); say(e, b); }
}

static struct { int w, h, n; uint8_t t[64][64]; struct { uint8_t type, dx, dy; } st[256]; } clip;

static void copy_rect(Editor *e, int x0, int y0, int x1, int y1)
{
    const Map *m = &e->g->map;
    int x, y, i;
    if (x1 < x0) { int k = x0; x0 = x1; x1 = k; }
    if (y1 < y0) { int k = y0; y0 = y1; y1 = k; }
    if (x1 - x0 >= 64) x1 = x0 + 63;
    if (y1 - y0 >= 64) y1 = y0 + 63;
    clip.w = x1 - x0 + 1; clip.h = y1 - y0 + 1; clip.n = 0;
    for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++) clip.t[y - y0][x - x0] = m->type[y][x];
    for (i = 0; i < m->num_st && clip.n < 256; i++) {   /* structures with their top-left corner inside */
        const Structure *st = &m->st[i];
        if (!st->type || st->x < x0 || st->x > x1 || st->y < y0 || st->y > y1) continue;
        clip.st[clip.n].type = st->type;
        clip.st[clip.n].dx = (uint8_t)(st->x - x0); clip.st[clip.n].dy = (uint8_t)(st->y - y0);
        clip.n++;
    }
    { char b[64]; snprintf(b, sizeof b, "COPIED %dX%d (%d BUILDINGS)", clip.w, clip.h, clip.n); say(e, b); }
}

static void paste(Editor *e)
{
    int x, y, i, cx = e->cx, cy = e->cy;
    if (!clip.w) { say(e, "NOTHING COPIED: M MARKS TWO CORNERS"); return; }
    undo_push(e);
    for (y = 0; y < clip.h; y++) for (x = 0; x < clip.w; x++) {
        uint8_t t = clip.t[y][x];
        if (t == TILE_BUILDING || t == TILE_FLOOR) t = TILE_SAND;   /* (the buildings come next, whole) */
        paint_tile(e, cx + x, cy + y, (TileType)t);
    }
    for (i = 0; i < clip.n; i++) {
        e->cx = cx + clip.st[i].dx; e->cy = cy + clip.st[i].dy;
        place(e, clip.st[i].type);
    }
    e->cx = cx; e->cy = cy;
    say(e, "PASTED");
}

static void apply(Editor *e, int pressed)
{
    int k = tools[e->tool].kind, v = tools[e->tool].v, x, y, r = e->brush - 1;
    if (pressed) undo_push(e);                           /* (each stroke can be undone) */
    if (k == K_TILE) {
        for (y = e->cy - r / 2 - (r & 1); y <= e->cy + r / 2; y++)
            for (x = e->cx - r / 2 - (r & 1); x <= e->cx + r / 2; x++)
                paint_tile(e, x, y, (TileType)v);
    } else if (k == K_ERASE) {
        Map *m = &e->g->map;
        if (m->sid[e->cy][e->cx] && (m->custom_bases || !in_base(e->g, e->cx, e->cy))) { remove_structure(m, m->sid[e->cy][e->cx]); e->dirty = 1; }
        else paint_tile(e, e->cx, e->cy, TILE_SAND);
    } else if (pressed && k == K_STRUCT) {
        place(e, v);
    }
}

const char *editor_test_map(Editor *e)
{
    const char *error = map_custom_error(&e->g->map, e->g->bases);
    if (error) { say(e, error); return NULL; }
    return map_text(e, text_buf, sizeof text_buf) ? text_buf : NULL;
}

int editor_save(Editor *e)
{
    if (!editor_test_map(e)) return -1;
    if (!e->user || !e->path[0]) {                       /* a new file of your own */
        int i;
        for (i = 1; i < 1000; i++) {
            FILE *f;
            snprintf(e->path, sizeof e->path, "%smymap_%03d.ofpmap", maps_user_dir(), i);
            if ((f = fopen(e->path, "rb")) == NULL) break;
            fclose(f);
        }
        if (!strcmp(e->name, "New map") || !e->name[0]) {
            snprintf(e->name, sizeof e->name, "My map %d", i);
            map_text(e, text_buf, sizeof text_buf);
        }
        e->user = 1;
    }
    if (maps_write(e->path, text_buf) != 0) { say(e, "COULD NOT SAVE"); return -1; }
    e->dirty = 0;
    recovery_clear(draft_prefix());
    say(e, "SAVED");
    return 0;
}

void editor_notice(Editor *e, const char *msg) { say(e, msg); }

void editor_resume_test(Editor *e, InputBits nav, int mx, int my, int buttons)
{
    e->blocked_nav = nav;
    e->blocked_buttons = buttons;
    e->prev = 0;
    e->next_step = 0;
    e->drag = e->mouse_tile = e->last_buttons = 0;
    e->last_mx = mx;
    e->last_my = my;
    say(e, "BACK FROM TEST RUN");
}

enum { CHECK_TOP = 137, CHECK_ROW_H = 9 };
static const SDL_Rect CHECK_JUMP = { 13, 194, 129, 13 }, CHECK_BACK = { 299, 194, 72, 13 };
static const char mode_auto_notice[] = "MODE SET TO CAPTURE THE FLAG";

int editor_check(Editor *e)
{
    int mode = e->modes == 2 || (!e->modes && e->g->map.num_points) ? MAP_MODE_CONQUEST : MAP_MODE_CLASSIC;
    map_check_playability(e->g, &e->check, mode);
    e->check_row = e->check_scroll = 0;
    return e->check.errors;
}

enum { E_CLOSED, E_MAIN, E_FILE, E_OPTIONS, E_NETWORK, E_OPEN };
enum { E_SHOW_FILE = 1, E_SHOW_OPTIONS, E_SHOW_NETWORK, E_SHOW_OPEN, E_BACK, E_RESUME, E_EXIT, E_APPLY, E_LOAD,
       E_WORLD, E_WEATHER, E_TIME, E_MUSIC, E_SIZE, E_MODE, E_PLAYERS };
enum { MENU_X = 42, MENU_Y = 25, MENU_W = 300, MENU_TOP = 52, MENU_ROW = 15, OPEN_VISIBLE = 8 };
static const struct { const char *name; int action; } menu_rows[][9] = {
    [E_MAIN] = { { "FILE  >", E_SHOW_FILE }, { "OPTIONS  >", E_SHOW_OPTIONS }, { "NETWORK  >", E_SHOW_NETWORK },
                 { "TEST RUN MAP", SDLK_F9 }, { "VALIDATE MAP", SDLK_F7 }, { "BACK TO EDITOR", E_RESUME }, { "BACK TO MAIN MENU", E_EXIT } },
    [E_FILE] = { { "OPEN  >", E_SHOW_OPEN }, { "SAVE", SDLK_F2 }, { "RANDOM MAP", SDLK_g },
                 { "ERASE MAP", SDLK_c }, { "BACK", E_BACK } },
    [E_OPTIONS] = { { "SET WORLD", E_WORLD }, { "SET WEATHER", E_WEATHER }, { "SET TIME OF DAY", E_TIME },
                    { "SOUNDTRACK", E_MUSIC }, { "SET SIZE", E_SIZE }, { "GAME MODE", E_MODE }, { "PLAYERS", E_PLAYERS },
                    { "APPLY", E_APPLY }, { "BACK", E_BACK } },
    [E_NETWORK] = { { "UPLOAD MAP TO GALLERY", SDLK_F8 }, { "BACK", E_BACK } }
};
static const int menu_counts[] = { 0, 7, 5, 9, 2 };
static MapEntry open_maps[MAPS_MAX];
static int open_count;

static void editor_page(Editor *e, int page)
{
    e->menu = page;
    e->msel = 0;
    e->drag = e->overview = 0;
}

static int check_jump_available(const Editor *e)
{
    return e->check.count && map_issue_has_location(&e->g->map, &e->check.issues[e->check_row]);
}

static void check_input(Editor *e, InputBits pressed, int key, int click, int mx, int my, int wheel)
{
    SDL_Point p = { mx, my };
    int row = e->check_row + !!((pressed & IN_DOWN) || key == SDLK_DOWN)
                          - !!((pressed & IN_UP) || key == SDLK_UP) - wheel;
    int accept = (pressed & IN_FIRE) || key == SDLK_RETURN || key == SDLK_KP_ENTER;
    if (key == SDLK_F7 || key == SDLK_ESCAPE || key == ED_KEY_MENU || (pressed & IN_MINE) ||
        (click && SDL_PointInRect(&p, &CHECK_BACK))) {
        e->checking = 0;
        e->wait_fire = 1;
        return;
    }
    if (click && mx >= 13 && mx < 371 && my >= CHECK_TOP && my < CHECK_TOP + CHECK_ROW_H * MAP_CHECK_ROWS) {
        int hit = e->check_scroll + (my - CHECK_TOP) / CHECK_ROW_H;
        if (hit < e->check.count) row = hit;
    }
    if (!e->check.count) {
        if (accept) e->checking = 0;
        return;
    }
    e->check_row = row < 0 ? 0 : row >= e->check.count ? e->check.count - 1 : row;
    if (e->check_row < e->check_scroll) e->check_scroll = e->check_row;
    if (e->check_row >= e->check_scroll + MAP_CHECK_ROWS) e->check_scroll = e->check_row - MAP_CHECK_ROWS + 1;
    if ((accept || (click && SDL_PointInRect(&p, &CHECK_JUMP))) && check_jump_available(e)) {
        const MapIssue *issue = &e->check.issues[e->check_row];
        e->cx = issue->x;
        e->cy = issue->y;
        place_camera(e);
        e->checking = 0;
        editor_page(e, E_CLOSED);
        say(e, issue->text);
    }
    e->wait_fire = 1;
}

static int editor_weather_allowed(const Editor *e)
{
    const Map *m = &e->g->map;
    return map_editor_allows_weather(m->custom_bases ? m->mode : -1, (Biome)e->g->biome);
}

static int option_weather_allowed(const Editor *e)
{
    const Map *m = &e->g->map;
    int mode = m->custom_bases || e->option_size != m->size ? e->option_mode : -1;
    return map_editor_allows_weather(mode, (Biome)e->option_world);
}

static int menu_row_enabled(const Editor *e, int row)
{
    return e->menu != E_OPTIONS || menu_rows[E_OPTIONS][row].action != E_WEATHER || option_weather_allowed(e);
}

static void option_change(Editor *e, int action, int delta)
{
    switch (action) {
    case E_WORLD: {
        int mode = e->option_mode;
        e->option_world = biome_step((Biome)e->option_world, delta);
        e->option_music = 0;
        e->option_mode = map_editor_mode_for_world(e->option_mode, (Biome)e->option_world);
        if (mode != e->option_mode) e->option_auto_mode = e->option_mode == MAP_MODE_FLAG;
        if (!option_weather_allowed(e) ||
            (e->option_weather && !weather_allowed((Biome)e->option_world, e->option_weather - 1)))
            e->option_weather = 0;
        break;
    }
    case E_WEATHER:
        if (option_weather_allowed(e)) {
            int current = e->option_weather ? e->option_weather - 1 : WEATHER_COUNT;
            int next = weather_step((Biome)e->option_world, current, delta, 1);
            e->option_weather = next == WEATHER_COUNT ? 0 : next + 1;
        }
        break;
    case E_MUSIC: e->option_music = soundtrack_step(e->option_world, e->option_music, delta); break;
    case E_TIME: e->option_time = (e->option_time + delta + TIME_COUNT) % TIME_COUNT; break;
    case E_SIZE:
        e->option_size = (e->option_size + delta + MAP_SIZES) % MAP_SIZES;
        e->option_players = map_custom_players(e->option_size, e->option_players, 0);
        break;
    case E_MODE:
        e->option_mode = (e->option_mode + delta + MAP_MODE_COUNT) % MAP_MODE_COUNT;
        e->option_auto_mode = 0;
        break;
    case E_PLAYERS: e->option_players = map_custom_players(e->option_size, e->option_players, delta); break;
    }
}

static void apply_options(Editor *e)
{
    Map *m = &e->g->map;
    if (m->custom_bases && e->option_size == m->size && !map_mode_has_flags(e->option_mode) &&
        (map_custom_count(m, 0, ST_FLAGHOUSE) || map_custom_count(m, 1, ST_FLAGHOUSE))) {
        say(e, "REMOVE FLAG BUILDINGS BEFORE CHANGING MODE");
        return;
    }
    undo_push(e);
    e->g->biome = (uint8_t)e->option_world;
    m->time = (uint8_t)e->option_time;
    m->has_weather = option_weather_allowed(e) && e->option_weather &&
                     weather_allowed((Biome)e->option_world, e->option_weather - 1);
    m->weather = m->has_weather ? (uint8_t)(e->option_weather - 1) : WEATHER_CLEAR;
    if (m->custom_bases) {
        m->mode = (uint8_t)e->option_mode;
        m->players = (uint8_t)e->option_players;
    }
    if (e->option_size != m->size) {
        make_empty(e, e->option_size);
        m->mode = (uint8_t)e->option_mode;
        m->players = (uint8_t)e->option_players;
    }
    m->music = (uint8_t)e->option_music;
    map_normalize_surfaces(m, (Biome)e->g->biome, e->g->biome == BIOME_CLASSIC || (m->custom_bases && m->mode == MAP_MODE_CLASSIC));
    e->g->weather = m->weather;
    switch_category(e, 0);
    e->dirty = 1;
    editor_page(e, E_MAIN);
    say(e, e->option_auto_mode && m->custom_bases ? mode_auto_notice : "MAP OPTIONS APPLIED");
}

static void open_map(Editor *e)
{
    MapEntry *entry = &open_maps[e->open_index];
    char *text = maps_read(entry->path);
    Map *map = malloc(sizeof *map);
    Base bases[2];
    map_set_next_size(SIZE_LARGE);
    if (text && map && mapfile_load(map, bases, NULL, NULL, 0, text, e->seed) == 0)
        editor_open(e, e->g, text, entry->path, entry->user, e->seed, BIOME_CLASSIC);
    else say(e, "COULD NOT OPEN MAP");
    free(map);
    free(text);
}

static int editor_menu_input(Editor *e, InputBits pressed, int key, int mx, int my, int click)
{
    Map *m = &e->g->map;
    int action = 0, ok = (pressed & IN_FIRE) || key == SDLK_RETURN || key == SDLK_KP_ENTER;
    int back = (pressed & IN_MINE) || key == SDLK_ESCAPE || key == ED_KEY_MENU;
    int dx = !!(pressed & IN_RIGHT) - !!(pressed & IN_LEFT);
    int dy = !!(pressed & IN_DOWN) - !!(pressed & IN_UP);
    if (e->confirm) {
        if (back) { e->confirm = 0; return 0; }
        if (dx || dy) e->confirm_yes = dx ? dx > 0 : !e->confirm_yes;
        if (click && my >= 111 && my < 131 && mx >= MENU_X + 8 && mx < MENU_X + MENU_W - 8) {
            e->confirm_yes = mx >= MENU_X + MENU_W / 2;
            ok = 1;
        }
        if (!ok) return 0;
        action = e->confirm_yes ? e->confirm : 0;
        e->confirm = 0;
    } else {
        int n = e->menu == E_OPEN ? open_count + 1 : menu_counts[e->menu];
        if (back) {
            editor_page(e, e->menu == E_MAIN ? E_CLOSED : e->menu == E_OPEN ? E_FILE : E_MAIN);
            return 0;
        }
        if (dy)
            do e->msel = (e->msel + n + dy) % n;
            while (!menu_row_enabled(e, e->msel));
        if (click && mx >= MENU_X && mx < MENU_X + MENU_W && my >= MENU_TOP) {
            int row = (my - MENU_TOP) / MENU_ROW;
            if (row < (e->menu == E_OPEN ? OPEN_VISIBLE : n)) {
                int first = e->menu == E_OPEN && e->msel >= OPEN_VISIBLE ? e->msel - OPEN_VISIBLE + 1 : 0;
                if (first + row < n && menu_row_enabled(e, first + row)) {
                    e->msel = first + row; ok = 1;
                }
            }
        }
        if (e->menu == E_OPEN) action = e->msel == open_count ? E_BACK : E_LOAD;
        else action = menu_rows[e->menu][e->msel].action;
        if (action >= E_WORLD && action <= E_PLAYERS) {
            if ((action != E_MODE && action != E_PLAYERS) || m->custom_bases || e->option_size != m->size)
                if (dx || ok) option_change(e, action, dx ? dx : 1);
            return 0;
        }
        if (!ok) return 0;
        if (action == E_LOAD) e->open_index = e->msel;
        if ((action == E_APPLY && e->option_size != m->size) ||
            (e->dirty && (action == E_LOAD || action == E_EXIT || action == SDLK_c || action == SDLK_g))) {
            e->confirm = action;
            e->confirm_yes = 0;
            return 0;
        }
    }
    switch (action) {
    case E_SHOW_FILE: editor_page(e, E_FILE); return 0;
    case E_SHOW_OPTIONS:
        e->option_auto_mode = 0;
        e->option_world = e->g->biome; e->option_time = m->time;
        e->option_music = m->music;
        e->option_weather = editor_weather_allowed(e) && m->has_weather &&
                            weather_allowed((Biome)e->g->biome, m->weather) ? m->weather + 1 : 0;
        e->option_size = m->size;
        e->option_mode = m->custom_bases ? m->mode : MAP_MODE_CLASSIC;
        e->option_players = m->custom_bases ? m->players : map_custom_players(m->size, 2, 0);
        editor_page(e, E_OPTIONS);
        return 0;
    case E_SHOW_NETWORK: editor_page(e, E_NETWORK); return 0;
    case E_SHOW_OPEN: open_count = maps_scan(open_maps, MAPS_MAX); editor_page(e, E_OPEN); return 0;
    case E_BACK: editor_page(e, e->menu == E_OPEN ? E_FILE : E_MAIN); return 0;
    case E_RESUME: editor_page(e, E_CLOSED); return 0;
    case E_APPLY: apply_options(e); return 0;
    case E_LOAD: open_map(e); return 0;
    }
    return action;
}

static void toggle_overview(Editor *e)
{
    e->overview = !e->overview;
    e->area.active = e->area_keyboard = 0;
    e->overview_x = e->cx; e->overview_y = e->cy;
    e->drag = e->help = e->checking = 0;
    e->next_step = 0;
}

static void overview_move(Editor *e, int dx, int dy)
{
    e->overview_x += dx; e->overview_y += dy;
    if (e->overview_x < 0) e->overview_x = 0;
    if (e->overview_y < 0) e->overview_y = 0;
    if (e->overview_x >= e->g->map.w) e->overview_x = e->g->map.w - 1;
    if (e->overview_y >= e->g->map.h) e->overview_y = e->g->map.h - 1;
}

static const char *const detail_labels[] = { "NAME", "DESCRIPTION", "AUTHOR", "CONTACT" };
static const int detail_y[] = { 39, 65, 124, 150, 178 };

static char *detail_field(Editor *e, int row, size_t *cap)
{
    MapDetails *d = &e->g->map.details;
    switch (row) {
    case 0: *cap = 41; return e->name;
    case 1: *cap = sizeof d->description; return d->description;
    case 2: *cap = sizeof d->author; return d->author;
    default: *cap = sizeof d->contact; return d->contact;
    }
}

void editor_text(Editor *e, const char *text)
{
    size_t cap, n;
    if (!e->details || e->detail_edit < 0) return;
    detail_field(e, e->detail_edit, &cap);
    n = strlen(e->detail_draft);
    while (*text && n + 1 < cap) {
        unsigned char ch = (unsigned char)*text++;
        if (ch >= ' ' && ch <= '~') e->detail_draft[n++] = (char)ch;
    }
    e->detail_draft[n] = 0;
}

static void details_input(Editor *e, int key, InputBits pressed, int click, int x, int y)
{
    if (e->detail_edit >= 0) {
        size_t len = strlen(e->detail_draft);
        if (key == SDLK_BACKSPACE && len) e->detail_draft[len - 1] = 0;
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER || (click && x >= 32 && x < 352 && y >= detail_y[4] && y < 194)) {
            size_t cap;
            char *dst = detail_field(e, e->detail_edit, &cap);
            if (strcmp(dst, e->detail_draft)) {
                undo_push(e);
                snprintf(dst, cap, "%s", e->detail_draft);
                e->dirty = 1;
            }
        } else if (key != SDLK_ESCAPE) return;
        SDL_StopTextInput();
        e->detail_edit = -1;
        return;
    }
    if (key == SDLK_ESCAPE || key == ED_KEY_MENU || (pressed & IN_MINE)) { e->details = 0; return; }
    if (pressed & IN_UP) e->detail_row = (e->detail_row + 4) % 5;
    if (pressed & IN_DOWN) e->detail_row = (e->detail_row + 1) % 5;
    if (click) {
        if (x < 32 || x >= 352 || y < detail_y[0] || y >= 194) return;
        for (e->detail_row = 4; y < detail_y[e->detail_row]; e->detail_row--) {}
    }
    if (click || (pressed & IN_FIRE) || key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        size_t cap;
        if (e->detail_row == 4) { e->details = 0; return; }
        e->detail_edit = e->detail_row;
        snprintf(e->detail_draft, sizeof e->detail_draft, "%s", detail_field(e, e->detail_row, &cap));
        SDL_StartTextInput();
    }
}

static void area_commit(Editor *e)
{
    Base next[2] = { e->g->bases[0], e->g->bases[1] };
    int changed = map_area_finish(&e->area, &e->g->map, next);
    if (changed > 0) {
        undo_push(e);
        e->g->bases[e->area.team] = next[e->area.team];
        map_editor_update_turrets(&e->g->map, e->g->bases);
        e->dirty = 1;
        say(e, "TEAM AREA UPDATED");
    } else if (changed < 0) say(e, "AREA CONFLICT OR A GARAGE / FLAG IS OUTSIDE");
}

static void area_select(Editor *e, int x, int y)
{
    int k, radius = (3 * e->g->map.w + OVERVIEW.w - 1) / OVERVIEW.w;
    for (k = 0; k < 2; k++) {
        const Base *b = &e->g->bases[k];
        if (x >= b->x0 - radius && y >= b->y0 - radius && x <= b->x1 + radius && y <= b->y1 + radius) {
            e->area_team = k;
            break;
        }
    }
    map_area_start(&e->area, e->g->bases, e->area_team, x, y, radius);
}

void editor_scroll(Editor *e, int dx, int dy)
{
    if (e->details || e->checking) return;
    if (e->overview) {
        overview_move(e, dx, dy);
        if (e->area_keyboard) map_area_move(&e->area, &e->g->map, e->overview_x, e->overview_y);
        return;
    }
    e->cx += dx; e->cy += dy;
    if (e->cx < 0) e->cx = 0;
    if (e->cy < 0) e->cy = 0;
    if (e->cx > e->g->map.w - 1) e->cx = e->g->map.w - 1;
    if (e->cy > e->g->map.h - 1) e->cy = e->g->map.h - 1;
    place_camera(e);
}

int editor_update(Editor *e, InputBits nav, int key, int mx, int my, int buttons, int wheel)
{
    InputBits pressed;
    int dx = 0, dy = 0, cam_x, cam_y;
    int click;
    int menu_action = e->menu != E_CLOSED;
    int right_panel = mx >= SIDEBAR_X && mx < 378 && my >= 4 && my < BAR_Y;
    e->blocked_nav &= nav;
    e->blocked_buttons &= buttons;
    nav &= ~e->blocked_nav;
    buttons &= ~e->blocked_buttons;
    click = (buttons & ~e->last_buttons) & SDL_BUTTON(SDL_BUTTON_LEFT);
    pressed = (InputBits)(nav & ~e->prev);
    e->prev = nav;
    e->frame++;
    if (e->msg_t) e->msg_t--;
    if (!(nav & IN_FIRE)) e->wait_fire = 0;
    if (e->recovery_pending) {
        if (pressed & (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT)) e->recovery_choice ^= 1;
        click = click && my >= 122 && my < 146 && mx >= 24 && mx < 356;
        if (click) e->recovery_choice = mx >= 192;
        if ((pressed & (IN_FIRE | IN_MINE)) || key == SDLK_RETURN || key == SDLK_ESCAPE || click) {
            int restore = !(pressed & IN_MINE) && key != SDLK_ESCAPE && !e->recovery_choice;
            e->recovery_pending = 0;
            if (restore && recovery_read(draft_prefix(), text_buf, sizeof text_buf)) {
                editor_open_impl(e, e->g, text_buf, NULL, 0, e->seed, BIOME_CLASSIC, 1);
                e->dirty = 1;
                say(e, "DRAFT RESTORED - SAVE AS A NEW MAP");
            } else recovery_clear(draft_prefix());
            e->blocked_nav = nav;
        }
        return ED_NOTHING;
    }
    if ((uint32_t)(SDL_GetTicks() - e->autosave_at) >= 30000) {
        e->autosave_at = SDL_GetTicks();
        if (e->dirty && map_text(e, text_buf, sizeof text_buf) && recovery_write(draft_prefix(), text_buf))
            say(e, "DRAFT COULD NOT BE SAVED");
    }
    if (e->checking) {
        check_input(e, pressed, key, click, mx, my, wheel);
        if (!e->checking) {
            e->blocked_nav |= nav;
            e->blocked_buttons |= buttons;
        }
        e->last_buttons = e->checking ? buttons : 0;
        e->last_mx = mx;
        e->last_my = my;
        return ED_NOTHING;
    }
    if (e->details) {
        details_input(e, key, pressed, click, mx, my);
        e->last_buttons = buttons;
        return ED_NOTHING;
    }
    if (!e->menu && !e->overview && click) {
        int button;
        for (button = 0; button < BAR_COUNT; button++)
            if (SDL_PointInRect(&(SDL_Point){ mx, my }, &bar[button])) break;
        if (button < BAR_COUNT) {
            if (button == BAR_MAP) toggle_overview(e);
            else if (button == BAR_AREA) {
                if (e->g->map.custom_bases) { toggle_overview(e); e->overview = 2; e->msg_t = 0; }
                else say(e, "IMPORTED MAP AREAS ARE FIXED");
            } else if (button == BAR_TIME) e->show_time ^= 1;
            else if (button == BAR_WEATHER) {
                if (editor_weather_allowed(e)) e->show_weather ^= 1;
            }
            else { e->details = 1; e->detail_row = 0; }
            e->last_buttons = buttons;
            return ED_NOTHING;
        }
    }
    if (!e->menu && key == SDLK_F6) {
        toggle_overview(e);
        e->last_buttons = buttons;
        return ED_NOTHING;
    }
    if (e->overview) {
        int hit = SDL_PointInRect(&(SDL_Point){ mx, my }, &OVERVIEW);
        int accept = (pressed & IN_FIRE) || key == SDLK_RETURN || key == SDLK_KP_ENTER;
        int x = (mx - OVERVIEW.x) * e->g->map.w / OVERVIEW.w;
        int y = (my - OVERVIEW.y) * e->g->map.h / OVERVIEW.h;
        if (key == SDLK_ESCAPE || key == ED_KEY_MENU || (pressed & IN_MINE)) toggle_overview(e);
        else if (e->overview == 2 && key == SDLK_TAB && !e->area.active) e->area_team ^= 1;
        else if (e->overview == 2 && click && !e->area.active && my >= 28 && my < 60 && mx >= 15 && mx < 95) {
            e->area_team = my >= 44;
        } else if (e->overview == 2 && click && hit && !e->area.active) {
            area_select(e, x, y);
            e->area_keyboard = 0;
        } else if (e->overview == 2 && e->area.active && !e->area_keyboard) {
            map_area_move(&e->area, &e->g->map, x, y);
            if (!(buttons & SDL_BUTTON(SDL_BUTTON_LEFT))) area_commit(e);
        } else if (e->overview == 2 && accept) {
            if (e->area.active) { area_commit(e); e->area_keyboard = 0; }
            else { area_select(e, e->overview_x, e->overview_y); e->area_keyboard = 1; }
        } else if (e->overview == 1 && ((click && hit) || accept)) {
            if (click && hit) { e->overview_x = x; e->overview_y = y; }
            e->cx = e->overview_x; e->cy = e->overview_y;
            place_camera(e);
            toggle_overview(e);
            e->wait_fire = 1;
        } else {
            int directions = IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT;
            uint32_t now = SDL_GetTicks();
            if (!(nav & directions)) e->next_step = 0;
            else if ((pressed & directions) || (e->next_step && (int32_t)(now - e->next_step) >= 0)) {
                int step = e->overview == 2 ? 1 : 4;
                overview_move(e, step * (!!(nav & IN_RIGHT) - !!(nav & IN_LEFT)),
                              step * (!!(nav & IN_DOWN) - !!(nav & IN_UP)));
                if (e->area_keyboard) map_area_move(&e->area, &e->g->map, e->overview_x, e->overview_y);
                e->next_step = now + (e->next_step ? 80 : 220);
            }
        }
        e->last_buttons = buttons;
        e->last_mx = mx; e->last_my = my;
        return ED_NOTHING;
    }
    if (!e->menu && (key == ED_KEY_MENU || key == SDLK_ESCAPE)) {
        editor_page(e, E_MAIN);
        return ED_NOTHING;
    }
    if (e->menu) {
        e->last_buttons = buttons;
        key = editor_menu_input(e, pressed, key, mx, my, click);
        nav = pressed = buttons = wheel = 0;
        e->wait_fire = 1;
        if (!key) return ED_NOTHING;
    }
    if (!menu_action && !(SDL_GetModState() & KMOD_CTRL) &&
        (key == SDLK_z || (e->dirty && (key == SDLK_g || key == SDLK_c)))) {
        editor_page(e, E_MAIN);
        if (key == SDLK_z) {
            editor_menu_input(e, IN_DOWN, 0, -1, -1, 0);
            editor_menu_input(e, IN_FIRE, 0, -1, -1, 0);
            option_change(e, E_SIZE, 1);
            e->confirm = E_APPLY;
        } else e->confirm = key;
        e->confirm_yes = 0;
        return ED_NOTHING;
    }
    if (right_panel) {
        if (wheel) scroll_tools(e, -wheel);
        if (click && my >= 33 && my < 43 && (mx < 309 || mx >= 358)) switch_category(e, mx < 309 ? -1 : 1);
        else if (click && my >= 43 && my < TOOL_TOP) scroll_tools(e, -TOOLS_VISIBLE);
        else if (click && my >= TOOL_TOP && my < TOOL_TOP + TOOLS_VISIBLE * TOOL_ROW)
            select_tool(e, e->first_tool + (my - TOOL_TOP) / TOOL_ROW);
        else if (click && my >= TOOL_TOP + TOOLS_VISIBLE * TOOL_ROW && my < 166)
            scroll_tools(e, TOOLS_VISIBLE);
    }

    if ((SDL_GetModState() & KMOD_CTRL) && (key == SDLK_z || key == SDLK_y || key == SDLK_c || key == SDLK_v)) {
        if (key == SDLK_z) undo(e, (SDL_GetModState() & KMOD_SHIFT) != 0);
        else if (key == SDLK_y) undo(e, 1);
        else if (key == SDLK_v) paste(e);
        else if (e->mark) { copy_rect(e, e->mark_x, e->mark_y, e->cx, e->cy); e->mark = 0; }
        else say(e, "M MARKS THE FIRST CORNER, CTRL+C THE OTHER");
        key = 0;
    }
    switch (key) {
    case SDLK_F1: e->help = !e->help; e->checking = 0; break;
    case SDLK_F7:                                         /* is it playable? */
        editor_check(e);
        e->checking = 1;
        e->help = 0;
        return ED_NOTHING;
    case SDLK_u: undo(e, 0); break;                      /* (and Ctrl+Z / Ctrl+Y, Ctrl+Shift+Z) */
    case SDLK_r: undo(e, 1); break;
    case SDLK_m:                                         /* copy: M here, M (or Ctrl+C) at the other corner */
        if (!e->mark) { e->mark = 1; e->mark_x = e->cx; e->mark_y = e->cy; say(e, "FIRST CORNER: M AT THE OTHER"); }
        else { copy_rect(e, e->mark_x, e->mark_y, e->cx, e->cy); e->mark = 0; }
        break;
    case SDLK_v: paste(e); break;
    case SDLK_f:                                         /* fill the area of one ground kind */
        if (tools[e->tool].kind == K_TILE) { undo_push(e); fill(e, (TileType)tools[e->tool].v); }
        else say(e, "FILL: PICK A GROUND TOOL FIRST");
        break;
    case SDLK_F10: return editor_test_map(e) ? ED_PLAY_HERE : ED_NOTHING;   /* try it, from the cursor */
    case SDLK_q: case SDLK_PAGEUP: step_tool(e, -1); break;
    case SDLK_e: case SDLK_PAGEDOWN: step_tool(e, 1); break;
    case SDLK_LEFTBRACKET: switch_category(e, -1); break;
    case SDLK_RIGHTBRACKET: switch_category(e, 1); break;
    case SDLK_TAB: switch_category(e, (SDL_GetModState() & KMOD_SHIFT) ? -1 : 1); break;
    case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4: case SDLK_5: e->brush = key - SDLK_0; break;
    case SDLK_F2:
        if (editor_save(e) != 0) return ED_NOTHING;
        if (editor_check(e)) say(e, "SAVED, BUT IT HAS PROBLEMS: F7");
        else if (e->check.count) say(e, "SAVED (A WARNING OR TWO: F7)");
        return ED_SAVED;
    case SDLK_F9: return editor_test_map(e) ? ED_PLAY : ED_NOTHING;
    case SDLK_F8:
        if (e->dirty || !e->user || !e->path[0]) { say(e, "SAVE THE MAP BEFORE UPLOADING"); return ED_NOTHING; }
        return ED_SHARE;
    case E_EXIT: recovery_clear(draft_prefix()); return ED_QUIT;
    case SDLK_g: {
        int players = e->g->map.custom_bases ? e->g->map.players : 2;
        int mode = e->g->map.custom_bases ? e->g->map.mode : MAP_MODE_CLASSIC;
        int music = e->g->map.music;
        MapDetails details = e->g->map.details;
        undo_push(e);                            /* a fresh random map to start from (this size) */
        e->seed = e->seed * 1103515245u + 12345u;
        map_set_next_size(e->g->map.size);
        game_init(e->g, e->seed, (Biome)e->g->biome);
        map_custom_clear_generated_bases(&e->g->map, e->g->bases, players, mode);
        e->g->enhanced_rules = e->g->biome != BIOME_CLASSIC && mode != MAP_MODE_CLASSIC;
        game_prepare_surfaces(e->g, e->seed, 1);
        e->g->map.details = details;
        e->g->map.music = (uint8_t)music;
        map_set_next_size(SIZE_LARGE);
        tidy(e->g);
        switch_category(e, 0);
        e->dirty = 1;
        say(e, "A NEW RANDOM MAP");
        break;
    }
    case SDLK_c: { undo_push(e);
        make_empty(e, e->g->map.size);
        switch_category(e, 0);
        e->dirty = 1;
        say(e, "CLEARED");
        break;
    }
    case SDLK_b: {
        Map *m = &e->g->map;
        int mode = m->mode;
        undo_push(e);
        e->g->biome = (uint8_t)biome_step((Biome)e->g->biome, 1);
        m->music = 0;
        map_normalize_surfaces(&e->g->map, (Biome)e->g->biome, e->g->biome == BIOME_CLASSIC || (e->g->map.custom_bases && e->g->map.mode == MAP_MODE_CLASSIC));
        switch_category(e, 0);
        if (m->custom_bases)
            m->mode = (uint8_t)map_editor_mode_for_world(m->mode, (Biome)e->g->biome);
        if (!editor_weather_allowed(e) || !weather_allowed((Biome)e->g->biome, m->weather)) {
            m->has_weather = 0;
            e->g->weather = m->weather = WEATHER_CLEAR;
        }
        e->dirty = 1;
        say(e, m->mode != mode && m->mode == MAP_MODE_FLAG ? mode_auto_notice : "TERRAIN TYPE CHANGED");
        break;
    }
    case SDLK_t: {                                        /* when the battle is fought */
        static const char *msgs[TIME_COUNT] = { "FOUGHT BY DAY", "FOUGHT AT DUSK", "FOUGHT AT NIGHT" };
        e->g->map.time = (uint8_t)((e->g->map.time + 1) % TIME_COUNT);
        e->dirty = 1;
        say(e, msgs[e->g->map.time]);
        break;
    }
    case SDLK_k: {                                        /* the map's weather (sKy; W moves) */
        Map *m = &e->g->map;
        if (!editor_weather_allowed(e)) { say(e, "WEATHER DISABLED FOR THIS WORLD OR MODE"); break; }
        undo_push(e);
        int weather = weather_step((Biome)e->g->biome, m->has_weather ? m->weather : WEATHER_COUNT, 1, 1);
        m->has_weather = weather != WEATHER_COUNT;
        e->g->weather = m->weather = m->has_weather ? (uint8_t)weather : WEATHER_CLEAR;
        e->dirty = 1;
        say(e, m->has_weather ? weather_label(m->weather) : "WEATHER: AS THE SERVER SAYS");
        break;
    }
    case SDLK_p: {                                        /* conquest: a capture point at the cursor (or not) */
        Map *m = &e->g->map;
        int r2;
        undo_push(e);
        r2 = toggle_point(m->point_tx, m->point_ty, &m->num_points, 3, e->cx, e->cy);
        e->dirty = 1;
        if (r2 < 0) say(e, "THREE CAPTURE POINTS ALREADY: P ON ONE TAKES IT AWAY");
        else { char b2[48]; snprintf(b2, sizeof b2, r2 ? "CAPTURE POINT %c (%d OF 3)" : "CAPTURE POINT GONE (%d LEFT)", r2 ? 'A' + m->num_points - 1 : m->num_points, m->num_points); say(e, b2); }
        break;
    }
    case SDLK_o: {                                        /* an arena: a spawn point at the cursor (or not) */
        Map *m = &e->g->map;
        int r2;
        undo_push(e);
        if (m->custom_bases) { say(e, "GARAGES ARE THE STARTING POSITIONS"); break; }
        r2 = toggle_point(m->spawn_tx, m->spawn_ty, &m->num_spawns, 8, e->cx, e->cy);
        if (m->arena && m->num_spawns < 2) { m->arena = 0; rebuild(e); }   /* (an arena needs two at least) */
        e->dirty = 1;
        if (r2 < 0) say(e, "EIGHT SPAWN POINTS ALREADY: O ON ONE TAKES IT AWAY");
        else { char b2[48]; snprintf(b2, sizeof b2, r2 ? "SPAWN POINT %d OF 8" : "SPAWN POINT GONE (%d LEFT)", m->num_spawns); say(e, b2); }
        break;
    }
    case SDLK_n: {                                        /* an arena (No camps), or camps again */
        Map *m = &e->g->map;
        if (m->custom_bases) { say(e, "CHOOSE THE GAME MODE IN MAP OPTIONS"); break; }
        if (!m->arena && m->num_spawns < 2) { say(e, "AN ARENA NEEDS SPAWN POINTS: O, TWO AT LEAST"); break; }
        undo_push(e);
        m->arena = !m->arena;
        if (!m->arena && e->g->bases[TEAM_PLAYER].x0 < 0) {   /* (camps again: where they'd go on a new map) */
            e->g->bases[TEAM_PLAYER].x0 = 10;  e->g->bases[TEAM_PLAYER].y0 = 100;
            e->g->bases[TEAM_ENEMY].x0 = 100;  e->g->bases[TEAM_ENEMY].y0 = 10;
        }
        rebuild(e);
        e->dirty = 1;
        say(e, m->arena ? "AN ARENA: NO CAMPS, THE SPAWN POINTS" : "CAMPS AGAIN");
        break;
    }
    case SDLK_l: {                                        /* the modes it's made for */
        undo_push(e);
        if (e->g->map.custom_bases) { say(e, "CHOOSE THE GAME MODE IN MAP OPTIONS"); break; }
        e->modes = (e->modes + 1) % 6;
        e->dirty = 1;
        say(e, ed_modes_say[e->modes]);
        break;
    }
    case SDLK_x: {                                        /* the extras: not said, none, pickups, the hill, both */
        Map *m = &e->g->map;
        static const char *xs[4] = { "EXTRAS: NONE", "EXTRAS: PICKUPS", "EXTRAS: THE HILL", "EXTRAS: PICKUPS AND THE HILL" };
        undo_push(e);
        if (!m->has_extras) { m->has_extras = 1; m->extras = 0; }
        else if (m->extras < 3) m->extras++;
        else m->has_extras = 0;
        e->dirty = 1;
        say(e, m->has_extras ? xs[m->extras & 3] : "EXTRAS: AS THE SERVER SAYS");
        break;
    }
    case SDLK_DELETE: case SDLK_BACKSPACE: {
        int t = e->tool;
        e->tool = TOOL_ERASE;
        apply(e, 1);
        e->tool = t;
        break;
    }
    default: break;
    }
    if (menu_action) return ED_NOTHING;
    if (wheel && !right_panel) step_tool(e, wheel > 0 ? -1 : 1);

    /* the cursor on the keys: one step, then after a short pause a steady
     * walk of about 14 tiles a second, whatever the frame rate */
    {
        uint32_t now = SDL_GetTicks();
        if (!(nav & (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT))) e->next_step = 0;
        else if (pressed & (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT)) e->next_step = now + 280;
        else if (e->next_step && now >= e->next_step) {
            e->next_step = now + 70;
            pressed |= nav;
        }
        if (pressed & IN_LEFT) dx = -1;
        if (pressed & IN_RIGHT) dx = 1;
        if (pressed & IN_UP) dy = -1;
        if (pressed & IN_DOWN) dy = 1;
    }
    if (dx || dy) {
        e->cx += dx; e->cy += dy;
        if (e->cx < 0) e->cx = 0;
        if (e->cy < 0) e->cy = 0;
        if (e->cx >= e->g->map.w) e->cx = e->g->map.w - 1;
        if (e->cy >= e->g->map.h) e->cy = e->g->map.h - 1;
        keep_cursor_in_view(e);
    }

    /* the mouse: hold the right (or middle) button and drag to scroll, like an
     * RTS; a right click without dragging erases; the left button paints */
    {
        int pan = SDL_BUTTON(SDL_BUTTON_RIGHT) | SDL_BUTTON(SDL_BUTTON_MIDDLE);
        int down = buttons & ~e->last_buttons, up = e->last_buttons & ~buttons;
        if (down & pan) { e->drag = 1; e->drag_moved = 0; }
        if (e->drag && (buttons & pan)) {
            int ddx = mx - e->last_mx, ddy = my - e->last_my;
            if (ddx || ddy) {
                e->view_x -= ddx; e->view_y -= ddy;
                e->drag_moved += (ddx < 0 ? -ddx : ddx) + (ddy < 0 ? -ddy : ddy);
                set_view(e);
            }
        }
        game_camera(e->g, 0, VIEW.w, VIEW.h, 256, &cam_x, &cam_y);
        e->mouse_tile = mx >= VIEW.x && my >= VIEW.y && mx < VIEW.x + VIEW.w && my < VIEW.y + VIEW.h;
        if (e->mouse_tile && !e->drag && (mx != e->last_mx || my != e->last_my || buttons)) {
            e->cx = (mx - VIEW.x + cam_x) / TILE_SIZE;           /* the cursor follows the mouse */
            e->cy = (my - VIEW.y + cam_y) / TILE_SIZE;
        }
        if (e->drag && (up & pan)) {
            if ((up & SDL_BUTTON(SDL_BUTTON_RIGHT)) && e->drag_moved < 4 && e->mouse_tile) {
                int t = e->tool;                                  /* a plain right click: erase */
                e->tool = TOOL_ERASE;
                apply(e, 1);
                e->tool = t;
            }
            if (!(buttons & pan)) e->drag = 0;
        }
        if (!e->drag && (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) && e->mouse_tile)
            apply(e, (down & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0);
        e->last_mx = mx; e->last_my = my; e->last_buttons = buttons;
    }
    if (!e->wait_fire && (nav & IN_FIRE)) apply(e, (pressed & IN_FIRE) != 0);
    else if (nav & IN_MINE) {                            /* B on a pad: erase (as a right click) */
        int t = e->tool;
        e->tool = TOOL_ERASE;
        apply(e, (pressed & IN_MINE) != 0);
        e->tool = t;
    }
    return ED_NOTHING;
}

static void editor_menu_draw(SDL_Renderer *r, const HudColors *c, const Editor *e)
{
    static const char *const titles[] = { "", "MAP EDITOR", "FILE", "OPTIONS", "NETWORK", "OPEN MAP" };
    int i, n = e->menu == E_OPEN ? open_count + 1 : menu_counts[e->menu];
    int first = e->menu == E_OPEN && e->msel >= OPEN_VISIBLE ? e->msel - OPEN_VISIBLE + 1 : 0;
    ui_panel(r, MENU_X, MENU_Y, MENU_W, e->menu == E_OPTIONS ? 181 : 166);
    ui_text_bold(r, MENU_X + 11, 34, 1, c->value, e->confirm ? "CONFIRM" : titles[e->menu]);
    if (e->confirm) {
        const char *msg = e->confirm == E_APPLY ? "RESIZING ERASES THE CURRENT MAP." :
                          e->confirm == E_EXIT ? "DISCARD CHANGES AND EXIT?" : "REPLACE THE CURRENT MAP?";
        ui_text(r, MENU_X + 11, 72, 1, UI_WHITE, msg);
        if (e->dirty) ui_text(r, MENU_X + 11, 89, 1, UI_WHITE, "UNSAVED CHANGES WILL BE LOST.");
        for (i = 0; i < 2; i++) {
            int x = MENU_X + 8 + i * (MENU_W / 2);
            ui_fill(r, x, 111, MENU_W / 2 - 16, 20, e->confirm_yes == i ? c->value : (Rgb){ 16, 16, 12 });
            ui_text_bold(r, x + 6, 117, 1, e->confirm_yes == i ? (Rgb){ 20, 18, 10 } : UI_WHITE, i ? "CONTINUE" : "CANCEL");
        }
        return;
    }
    for (i = first; i < n && i < first + (e->menu == E_OPEN ? OPEN_VISIBLE : n); i++) {
        int row = i - first, y = MENU_TOP + row * MENU_ROW;
        int enabled = menu_row_enabled(e, i), on = enabled && e->msel == i;
        const char *name, *value = NULL;
        char label[34], count[8];
        Rgb fg = on ? (Rgb){ 20, 18, 10 } : enabled ? UI_WHITE : UI_GREY;
        if (e->menu == E_OPEN) {
            snprintf(label, sizeof label, "%.33s", i == open_count ? "BACK" : open_maps[i].name);
            name = label;
        } else name = menu_rows[e->menu][i].name;
        if (e->menu == E_OPTIONS) {
            switch (menu_rows[E_OPTIONS][i].action) {
            case E_WORLD: value = biome_name((Biome)e->option_world); break;
            case E_WEATHER:
                if (enabled) value = e->option_weather ? weather_label(e->option_weather - 1) : "ANY";
                break;
            case E_TIME: value = time_name(e->option_time); break;
            case E_MUSIC: value = soundtrack_title(soundtrack_for_map(e->option_world, e->option_music)); break;
            case E_SIZE: value = map_size_name[e->option_size]; break;
            case E_MODE: value = (e->g->map.custom_bases || e->option_size != e->g->map.size) ? map_mode_names[e->option_mode] : "IMPORTED"; break;
            case E_PLAYERS:
                snprintf(count, sizeof count, "%d", e->option_players);
                value = (e->g->map.custom_bases || e->option_size != e->g->map.size) ? count : "IMPORTED";
                break;
            }
        }
        ui_fill(r, MENU_X + 7, y, MENU_W - 14, MENU_ROW - 2, on ? c->value : (Rgb){ 17, 18, 15 });
        ui_text_bold(r, MENU_X + 12, y + 3, 1, fg, name);
        if (value) ui_text(r, MENU_X + MENU_W - 12 - ui_text_width(value, 1), y + 3, 1, on ? fg : c->value, value);
    }
    if (e->menu == E_OPTIONS && e->option_auto_mode && !e->msg_t &&
        (e->g->map.custom_bases || e->option_size != e->g->map.size))
        ui_text(r, MENU_X + 11, 198, 1, c->value, mode_auto_notice);
    else
        ui_text(r, MENU_X + 11, e->menu == E_OPTIONS ? 198 : 177, 1, e->msg_t ? c->value : UI_GREY, e->msg_t ? e->msg : "FIRE SELECT   MINE / ESC BACK");
}

static void details_draw(SDL_Renderer *r, const HudColors *c, Editor *e)
{
    int i;
    ui_backdrop(r, 0, 0, 384, 216);
    ui_panel(r, 22, 8, 340, 200);
    ui_text_bold(r, 35, 20, 1, c->value, "MAP DETAILS");
    for (i = 0; i < 4; i++) {
        size_t cap, len;
        const char *value = i == e->detail_edit ? e->detail_draft : detail_field(e, i, &cap);
        int line, lines = i == 1 ? 5 : 1;
        len = strlen(value);
        if (lines == 1 && len > 38) value += len - 38;
        ui_box(r, 32, detail_y[i], 320, detail_y[i + 1] - detail_y[i] - 3);
        ui_text(r, 38, detail_y[i] + 3, 1, i == e->detail_row ? c->value : UI_GREY, detail_labels[i]);
        for (line = 0; line < lines && *value; line++) {
            char chunk[39];
            size_t n = strlen(value);
            if (n > sizeof chunk - 1) n = sizeof chunk - 1;
            memcpy(chunk, value, n); chunk[n] = 0;
            ui_text(r, 38, detail_y[i] + 12 + 8 * line, 1, UI_WHITE, chunk);
            value += n;
        }
    }
    ui_box(r, 32, detail_y[4], 320, 16);
    ui_text(r, 38, detail_y[4] + 4, 1, e->detail_row == 4 ? c->value : UI_WHITE,
            e->detail_edit >= 0 ? "APPLY FIELD" : "DONE");
    ui_text(r, 34, 195, 1, UI_GREY, e->detail_edit >= 0 ? "ENTER APPLY   ESC CANCEL" : "CONTACT IS PUBLIC WHEN UPLOADED");
}

static void editor_overview_draw(SDL_Renderer *r, const HudColors *c, const Editor *e)
{
    const Map *m = &e->g->map;
    int x = OVERVIEW.x + e->overview_x * OVERVIEW.w / m->w;
    int y = OVERVIEW.y + e->overview_y * OVERVIEW.h / m->h;
    int team;
    ui_backdrop(r, 0, 0, 384, 216);
    ui_frame(r, OVERVIEW.x - 3, OVERVIEW.y - 3, OVERVIEW.w + 6, OVERVIEW.h + 6);
    ui_text_bold(r, OVERVIEW.x, 8, 1, c->value, e->overview == 2 ? "SET TEAM AREAS" : "MAP OVERVIEW");
    if (e->overview == 2) {
        ui_box(r, 15, 28, 80, 15); ui_box(r, 15, 44, 80, 15);
        ui_text(r, 22, 32, 1, e->area_team == 0 ? c->value : UI_WHITE, "TEAM 1");
        ui_text(r, 22, 48, 1, e->area_team == 1 ? c->value : UI_WHITE, "TEAM 2");
        ui_text(r, 15, 71, 1, UI_GREY, "DRAG TO DRAW");
        ui_text(r, 15, 84, 1, UI_GREY, "DRAG TO MOVE");
        ui_text(r, 15, 97, 1, UI_GREY, "EDGES RESIZE");
    }
    render_map_preview(r, e->g, &OVERVIEW, e->frame);
    for (team = 0; team < 2 && m->custom_bases; team++) {
        const Base *b = e->area.active && e->area.team == team ? &e->area.bounds : &e->g->bases[team];
        SDL_Rect zone = { OVERVIEW.x + b->x0 * OVERVIEW.w / m->w, OVERVIEW.y + b->y0 * OVERVIEW.h / m->h,
                          (b->x1 - b->x0 + 1) * OVERVIEW.w / m->w, (b->y1 - b->y0 + 1) * OVERVIEW.h / m->h };
        Rgb color = render_side_rgb(team);
        SDL_SetRenderDrawColor(r, color.r, color.g, color.b, 255);
        SDL_RenderDrawRect(r, &zone);
        if (e->overview == 2) {
            int right = zone.x + zone.w - 1, bottom = zone.y + zone.h - 1;
            ui_fill(r, zone.x - 1, zone.y - 1, 4, 4, color);
            ui_fill(r, right - 2, zone.y - 1, 4, 4, color);
            ui_fill(r, zone.x - 1, bottom - 2, 4, 4, color);
            ui_fill(r, right - 2, bottom - 2, 4, 4, color);
        }
    }
    if (e->overview == 1 || e->area_keyboard || !e->area.active) {
        ui_fill(r, x - 4, y - 1, 9, 3, (Rgb){0, 0, 0});
        ui_fill(r, x - 1, y - 4, 3, 9, (Rgb){0, 0, 0});
        ui_fill(r, x - 3, y, 7, 1, UI_WHITE);
        ui_fill(r, x, y - 3, 1, 7, UI_WHITE);
    }
    ui_text(r, 15, 202, 1, UI_WHITE, e->overview == 1 ? "CLICK OR FIRE TO JUMP    ESC BACK" :
            e->msg_t ? e->msg : "TAB TEAM   FIRE START / APPLY   ESC BACK");
}

static void editor_check_draw(SDL_Renderer *r, const HudColors *c, const Editor *e)
{
    const SDL_Rect preview = { 142, 31, 100, 100 };
    const Map *m = &e->g->map;
    Rgb bad = {255, 110, 90};
    int x, y, i;
    ui_backdrop(r, 0, 0, 384, 216);
    ui_panel(r, 8, 7, 368, 203);
    ui_text_bold(r, 16, 15, 1, c->value, "VALIDATE MAP");
    ui_text(r, 16, 39, 1, render_side_rgb(0), "TEAM 1 ROUTE");
    ui_text(r, 16, 52, 1, render_side_rgb(1), "TEAM 2 ROUTE");
    ui_text(r, 16, 67, 1, UI_GREY, "DOTTED: BREACH");
    render_map_preview(r, e->g, &preview, 0);
    for (y = 0; y < m->h; y++)
        for (x = 0; x < m->w; x++) {
            unsigned mask = e->check.route[y][x];
            int px = preview.x + x * preview.w / m->w, py = preview.y + y * preview.h / m->h;
            if (!mask || (!(mask & 3) && ((px + py) & 1))) continue;
            ui_fill(r, px, py, 1, 1, (mask & 5) && (mask & 10) ? UI_WHITE : render_side_rgb((mask & 10) != 0));
        }
    for (i = 0; i < e->check.count; i++) {
        const MapIssue *p = &e->check.issues[i];
        if (map_issue_has_location(m, p)) {
            x = preview.x + p->x * preview.w / m->w;
            y = preview.y + p->y * preview.h / m->h;
            ui_fill(r, x - 1, y - 1, 3, 3, i < e->check.errors ? bad : c->value);
            if (i == e->check_row) ui_frame(r, x - 3, y - 3, 7, 7);
        }
    }
    ui_text(r, 251, 39, 1, e->check.errors ? bad : c->value,
            e->check.errors ? "BLOCKED ROUTES" : e->check.count ? "HAS WARNINGS" : "ROUTES CLEAR");
    for (i = e->check_scroll; i < e->check.count && i < e->check_scroll + MAP_CHECK_ROWS; i++) {
        int top = CHECK_TOP + (i - e->check_scroll) * CHECK_ROW_H;
        if (i == e->check_row) ui_fill(r, 13, top, 358, CHECK_ROW_H, c->frame_dark);
        ui_text(r, 16, top + 2, 1, i < e->check.errors ? bad : UI_WHITE, e->check.issues[i].text);
    }
    ui_box(r, CHECK_JUMP.x, CHECK_JUMP.y, CHECK_JUMP.w, CHECK_JUMP.h);
    ui_text(r, CHECK_JUMP.x + 5, CHECK_JUMP.y + 3, 1, check_jump_available(e) ? c->value : UI_GREY, "JUMP TO ISSUE");
    ui_box(r, CHECK_BACK.x, CHECK_BACK.y, CHECK_BACK.w, CHECK_BACK.h);
    ui_text(r, CHECK_BACK.x + 5, CHECK_BACK.y + 3, 1, UI_WHITE, "BACK");
    if (e->check.count)
        ui_text(r, 150, CHECK_JUMP.y + 3, 1, UI_GREY,
                check_jump_available(e) ? "FIRE JUMP  ESC BACK" : "NO TILE FOR THIS ISSUE");
}

void editor_draw(SDL_Renderer *r, const Theme *t, const Style *st, Editor *e)
{
    if (e->recovery_pending) {
        ui_fill(r, 0, 0, 384, 216, (Rgb){16,16,32});
        ui_text(r, 28, 62, 1, (Rgb){255,220,64}, "RECOVER EDITOR DRAFT?");
        ui_text(r, 28, 87, 1, (Rgb){220,220,210}, "YOUR SAVED MAP IS UNCHANGED.");
        ui_fill(r, e->recovery_choice ? 200 : 24, 122, 156, 24, (Rgb){100,80,35});
        ui_text(r, 32, 130, 1, (Rgb){255,220,64}, "RESTORE DRAFT");
        ui_text(r, 208, 130, 1, (Rgb){255,220,64}, "DISCARD DRAFT");
        return;
    }
    const HudColors *c = &t->hud;
    int cam_x, cam_y, k = tools[e->tool].kind, v = tools[e->tool].v, w = 1, h = 1, i, y, blink = ui_blink(800);
    char buf[64];
    SDL_Rect q;
    if (e->checking) { editor_check_draw(r, c, e); return; }
    if (e->details) { details_draw(r, c, e); return; }
    if (e->overview) { editor_overview_draw(r, c, e); return; }
    ui_backdrop(r, 0, 0, 384, 216);
    ui_frame(r, VIEW.x - 2, VIEW.y - 2, VIEW.w + 4, VIEW.h + 4);
    render_set_night(!e->show_time);
    render_view(r, t, st, e->g, &VIEW, 0, 256, -1);
    render_set_night(0);
    if (e->show_weather && e->g->map.has_weather && editor_weather_allowed(e) &&
        weather_allowed((Biome)e->g->biome, e->g->map.weather)) {
        uint32_t now = SDL_GetTicks();
        render_weather(r, &VIEW, now / 20, weather_preview((Biome)e->g->biome, e->g->map.weather, now),
                       e->show_time ? e->g->map.time : TIME_DAY, 1);
    }
    game_camera(e->g, 0, VIEW.w, VIEW.h, 256, &cam_x, &cam_y);
    {   /* conquest's points (A B C) and an arena's spawn points, on the map */
        const Map *mm2 = &e->g->map;
        int k2;
        for (k2 = 0; k2 < mm2->num_points; k2++) {
            int px = VIEW.x + mm2->point_tx[k2] * TILE_SIZE - cam_x, py = VIEW.y + mm2->point_ty[k2] * TILE_SIZE - cam_y;
            char lb[2] = { (char)('A' + k2), 0 };
            SDL_Rect pr = { px - CP_RADIUS * TILE_SIZE + TILE_SIZE / 2, py - CP_RADIUS * TILE_SIZE + TILE_SIZE / 2,
                            CP_RADIUS * TILE_SIZE * 2, CP_RADIUS * TILE_SIZE * 2 };
            SDL_SetRenderDrawColor(r, 240, 230, 200, 200);
            SDL_RenderDrawRect(r, &pr);
            ui_text_shadow(r, px + 5, py + 4, 1, (Rgb){ 240, 230, 200 }, lb);
        }
        for (k2 = 0; k2 < mm2->num_spawns; k2++) {
            int px = VIEW.x + mm2->spawn_tx[k2] * TILE_SIZE - cam_x, py = VIEW.y + mm2->spawn_ty[k2] * TILE_SIZE - cam_y;
            int camp_side = (mm2->spawn_tx[k2] + mm2->spawn_ty[k2]) < e->g->map.w;   /* (team modes: the half it's in) */
            Rgb sc = render_side_rgb(camp_side ? TEAM_PLAYER : TEAM_ENEMY);
            char lb[4];
            snprintf(lb, sizeof lb, "%d", k2 + 1);
            ui_fill(r, px + 2, py + 2, TILE_SIZE - 4, TILE_SIZE - 4, sc);
            ui_text_shadow(r, px + 5, py + 4, 1, UI_WHITE, lb);
        }
    }

    if (k == K_TILE) w = h = e->brush;
    if (k == K_STRUCT) { w = struct_info[v].w; h = struct_info[v].h; }
    q.x = VIEW.x + (k == K_TILE ? e->cx - (w - 1) / 2 - ((w - 1) & 1) : e->cx) * TILE_SIZE - cam_x;
    q.y = VIEW.y + (k == K_TILE ? e->cy - (h - 1) / 2 - ((h - 1) & 1) : e->cy) * TILE_SIZE - cam_y;
    q.w = w * TILE_SIZE; q.h = h * TILE_SIZE;
    SDL_RenderSetClipRect(r, &VIEW);
    if (e->g->map.custom_bases && e->category == CAT_BASE) {
        int team;
        for (team = 0; team < 2; team++) {
            const Base *b = &e->g->bases[team];
            Rgb color = render_side_rgb(team);
            SDL_Rect zone = { VIEW.x + b->x0 * TILE_SIZE - cam_x, VIEW.y + b->y0 * TILE_SIZE - cam_y,
                              (b->x1 - b->x0 + 1) * TILE_SIZE, (b->y1 - b->y0 + 1) * TILE_SIZE };
            SDL_SetRenderDrawColor(r, color.r, color.g, color.b, 255);
            SDL_RenderDrawRect(r, &zone);
        }
    }
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    { SDL_Rect o = { q.x - 1, q.y - 1, q.w + 2, q.h + 2 }; SDL_RenderDrawRect(r, &o); }
    SDL_SetRenderDrawColor(r, blink ? 255 : 255, blink ? 230 : 120, 40, 255);
    SDL_RenderDrawRect(r, &q);
    if (k == K_STRUCT) {                                  /* whose it will be */
        int owner = map_editor_owner(&e->g->map, e->g->bases, v, e->cx, e->cy);
        Rgb color = owner < 0 ? (Rgb){ 255, 230, 40 } : render_side_rgb(owner);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, color.r, color.g, color.b, 90);
        SDL_RenderFillRect(r, &q);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }
    SDL_RenderSetClipRect(r, NULL);
    if (e->msg_t)
        ui_text_shadow(r, VIEW.x + (VIEW.w - ui_text_width(e->msg, 1)) / 2, VIEW.y + 6, 1, UI_WHITE, e->msg);

    /* the panel */
    ui_panel(r, 289, 3, 88, 193);
    ui_box(r, 294, 10, 78, 12);
    ui_text_bold(r, 294 + (78 - ui_text_width("EDITOR", 1)) / 2, 12, 1, c->label, "EDITOR");
    snprintf(buf, sizeof buf, "%.10s", e->name);
    ui_text(r, 294 + (78 - ui_text_width(buf, 1)) / 2, 26, 1, e->dirty ? UI_WHITE : UI_GREY, buf);
    ui_box(r, 294, 32, 78, 12);
    ui_text(r, 297, 35, 1, c->value, "<");
    ui_text(r, 366, 35, 1, c->value, ">");
    ui_text(r, 333 - ui_text_width(categories[e->category].name, 1) / 2, 35, 1, UI_WHITE, categories[e->category].name);
    {
        uint8_t list[sizeof base];
        int n = category_tools(e, list);
        ui_box(r, 294, 45, 78, 113);
        if (e->first_tool) ui_text(r, 364, 44, 1, c->value, "^");
        for (i = e->first_tool, y = TOOL_TOP + 2; i < n && i < e->first_tool + TOOLS_VISIBLE; i++, y += TOOL_ROW) {
            int ti = list[i];
            if (ti == e->tool) ui_fill(r, 296, y - 1, 74, 10, (Rgb){ 60, 50, 30 });
            ui_text(r, 299, y, 1, ti == e->tool ? c->value : UI_WHITE, tools[ti].name);
        }
        if (i < n) ui_text(r, 296, 157, 1, c->value, "MORE v");
    }
    if (e->g->map.custom_bases && e->category == CAT_BASE) {
        int team;
        for (team = 0; team < 2; team++) {
            snprintf(buf, sizeof buf, "TEAM %d %d/%d", team + 1,
                     map_custom_count(&e->g->map, team, ST_GARAGE), e->g->map.players / 2);
            ui_text(r, 296, 173 + team * 10, 1, render_side_rgb(team), buf);
        }
    } else if (k == K_TILE) {
        snprintf(buf, sizeof buf, "BRUSH %d", e->brush);
        ui_text(r, 296, 173, 1, c->value, buf);
    }
    {
        int allow_weather = editor_weather_allowed(e);
        const char *const labels[] = { "MAP", "SET AREA", e->show_time ? "TIME ON" : "TIME OFF",
                                      !allow_weather ? "WEATHER" : e->show_weather ? "WEATHER ON" : "WEATHER OFF", "DETAILS" };
        for (i = 0; i < BAR_COUNT; i++) {
            ui_box(r, bar[i].x, bar[i].y, bar[i].w, bar[i].h);
            ui_text(r, bar[i].x + (bar[i].w - ui_text_width(labels[i], 1)) / 2, bar[i].y + 4, 1,
                    (i == BAR_AREA && !e->g->map.custom_bases) || (i == BAR_WEATHER && !allow_weather) ?
                    UI_GREY : c->value, labels[i]);
        }
    }
    if (e->help) {                                       /* F1: every key, over the map */
        static const char *keys[] = {
            "ARROWS / MOUSE   MOVE THE CURSOR", "FIRE / LEFT      PAINT OR PLACE", "RIGHT CLICK      ERASE",
            "Q E: TOOL        TAB: CATEGORY", "SET AREA         DRAW OR MOVE TEAM AREAS", "1 TO 5           BRUSH SIZE",
            "F                FILL AN AREA", "U  R             UNDO  REDO (CTRL+Z, CTRL+Y)",
            "M ... M          COPY BETWEEN TWO CORNERS", "V                PASTE AT THE CURSOR",
            "G  C             A NEW MAP, CLEAR IT", "B  T             WORLD, TIME OF DAY",
            "K  X             WEATHER, EXTRAS OF THIS MAP",
            "P                CONQUEST: A CAPTURE POINT (3)", "O  N             ARENA: A SPAWN POINT (8), NO CAMPS",
            "L                THE MODES IT'S MADE FOR",
            "F2  F9  F10      SAVE, PLAY, PLAY FROM HERE", "F8               SHARE ON THE WEB SITE",
            "F7               IS IT PLAYABLE? (A CHECK)",
            "F6               FULL MAP AND JUMP", "ESC              EDITOR MENU",
            "F1               CLOSE THIS",
        };
        int kk, n = (int)(sizeof keys / sizeof keys[0]);   /* (the whole width: the lines are long) */
        ui_fill(r, 6, 6, 372, 10 + n * 9, (Rgb){ 14, 14, 10 });   /* (9 apart: they all fit) */
        ui_frame(r, 6, 6, 372, 10 + n * 9);
        for (kk = 0; kk < n; kk++) ui_text(r, 14, 11 + kk * 9, 1, kk == n - 1 ? UI_GREY : UI_WHITE, keys[kk]);
    }
    if (e->menu) editor_menu_draw(r, c, e);
}
