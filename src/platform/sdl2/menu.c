/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include "platform/sdl2/menu.h"
#include "common/soundtrack.h"
#include "common/credits.h"
#include "platform/sdl2/netui.h"
#include "platform/sdl2/fx.h"
#include "core/match.h"
#include "platform/sdl2/teams.h"
#include "platform/sdl2/input.h"
#include "platform/sdl2/profiles.h"
#include "platform/sdl2/select.h"
#include "platform/sdl2/ui.h"
#include "platform/sdl2/audio.h"
#include "platform/sdl2/render.h"

/* the first page: where to go */
enum { G_LOCAL, G_NET, G_TRAINING, G_TOOLS, G_OPTIONS, G_ABOUT, G_QUIT, G_ROWS };
enum { TOOL_EDITOR, TOOL_REPLAYS, TOOL_BENCHMARK, TOOL_CHALLENGES, TOOL_CONTROLLERS, TOOL_BACK, TOOL_ROWS };
enum { SET_GAME, SET_GRAPHICS, SET_AUDIO, SET_KEYS, SET_BACK, SET_ROWS };
/* LOCAL GAME: how many here, what kind of game, where */
enum { M_TERRAIN, M_TYPE, M_SIZE, M_MAP, M_START, M_BACK, M_ROWS, M_ENEMY = 98, M_BOTS0 = 99 };
/* (the world, then what it offers: its modes, a size, its maps of that mode and size; the AI bots'
 * level: the team screen's) */   /* (AI bots: on the team screen now) */

void menu_show_map_row(void *menu) { Menu *m = (Menu *)menu; m->page = PAGE_LOCAL; m->row = M_MAP; }
void menu_show_local(void *menu) { Menu *m = (Menu *)menu; m->page = PAGE_LOCAL; m->row = M_START; }

/* the Original world is the 1987 game's: one or two players */
static int world_takes(const Style *st, int players) { return !st->faithful || players <= 2; }
enum { N_NAME, N_QUICK, N_FIND, N_JOIN, N_BACK, N_ROWS };   /* (JOIN: DIRECT CONNECT, a window of its own) */
enum { D_ADDRESS, D_PASSWORD, D_SAVE, D_CONNECT, D_CANCEL, D_ROWS };
enum { OPT_RADAR, OPT_SIDES, OPT_LIVES, OPT_TIME, OPT_POWERUPS, OPT_BACK, OPT_ROWS };
enum { GRAPH_VIEW, GRAPH_MARKS, GRAPH_EFFECTS, GRAPH_EXPLOSIONS, GRAPH_FIRES, GRAPH_WEATHER, GRAPH_SCREEN, GRAPH_AUTO, GRAPH_BACK, GRAPH_ROWS };
enum { AUDIO_MUSIC, AUDIO_EFFECTS, AUDIO_BACK, AUDIO_ROWS };
/* (the steering, TANK or ORIGINAL: each player's own, in their profile window) */
enum { K_PAIR = 2 * ACT_COUNT, K_DEFAULTS, K_BACK, K_ROWS, K_PAD_LAYOUT = 90, K_PAD_FIRE, K_PAD_MINE };
/* (a pad's layout and buttons: each player's own, in their profile window, START on the pad) */
/* the pad's buttons to choose from (-1: as ever) */
static const int pad_choice[] = { -1, SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X,
                                  SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER };
#define PAD_CHOICES 7

static int pad_next(int now, int dir)
{
    int i;
    for (i = 0; i < PAD_CHOICES && pad_choice[i] != now; i++) {}
    return pad_choice[((i < PAD_CHOICES ? i : 0) + dir + PAD_CHOICES) % PAD_CHOICES];
}


static const char *act_label[ACT_COUNT] = { "UP", "DOWN", "LEFT", "RIGHT", "FIRE", "MINE" };

void menu_notice(Menu *m, const char *text)
{
    snprintf(m->notice, sizeof m->notice, "%s", text);
    m->notice_until = SDL_GetTicks() + 6000;
}

void menu_reset(Menu *m)
{
    memset(m, 0, sizeof *m);
    m->capture = -1;
    m->prev = 0xFF;
}

static int rows_of(int page)
{
    switch (page) {
    case PAGE_MAIN: return G_ROWS;
    case PAGE_LOCAL: return M_ROWS;
    case PAGE_NET: return N_ROWS;
    case PAGE_DIRECT: return D_ROWS;
    case PAGE_OPTIONS: return SET_ROWS;
    case PAGE_GAME_OPTIONS: return OPT_ROWS;
    case PAGE_GRAPHICS: return GRAPH_ROWS;
    case PAGE_AUDIO: return AUDIO_ROWS;
    case PAGE_CONTROLLERS: return 6;
    case PAGE_TOOLS: return TOOL_ROWS;
    case PAGE_CHALLENGES: return 4;
    case PAGE_ABOUT: return 1;
    default: return K_ROWS;
    }
}

/* the text field on this row of the network page, if it is one */
static char *field(Menu *m, Settings *s, size_t *cap, int *kind)
{
    if (m->page == PAGE_DIRECT) {                        /* DIRECT CONNECT: the address, the password */
        if (m->row == D_ADDRESS) { *cap = sizeof s->server; *kind = 0; return s->server; }
        if (m->row == D_PASSWORD) { *cap = sizeof s->server_password; *kind = 2; return s->server_password; }
        return NULL;
    }
    if (m->page != PAGE_NET) return NULL;
    switch (m->row) {
    case N_NAME:     if (signin_count() > 0) return NULL;   /* (signed in: their profiles' names) */
                     *cap = sizeof s->player_name; *kind = 1; return s->player_name;
    default:         return NULL;
    }
}

int menu_typing(Menu *m, Settings *s)
{
    size_t cap;
    int kind;
    return field(m, s, &cap, &kind) != NULL;
}

int menu_text(Menu *m, Settings *s, const char *utf8)
{
    size_t cap, n;
    int kind;
    char *f = field(m, s, &cap, &kind);
    if (!f) return 0;
    n = strlen(f);
    for (; *utf8; utf8++) {                               /* plain ASCII */
        char ch = *utf8;
        int ok = kind == 0 ? ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                              ch == '.' || ch == '-' || ch == ':')
               : kind == 3 ? (ch > 32 && ch < 127)
               : (ch >= 32 && ch < 127);
        if (ok && n + 1 < cap) f[n++] = ch;
    }
    f[n] = 0;
    return 1;
}

/* text centred in a box, shortened (...) if it doesn't fit */
static void fit_centre(SDL_Renderer *r, int x, int w, int y, Rgb col, const char *text)
{
    char t[128];
    size_t n;
    snprintf(t, sizeof t, "%s", text);
    while ((n = strlen(t)) > 3 && ui_text_width(t, 1) > w) { t[n - 4] = '.'; t[n - 3] = '.'; t[n - 2] = 0; }
    ui_text(r, x + (w - ui_text_width(t, 1)) / 2, y, 1, col, t);
}

static int fit_mode = 0;                    /* LOCAL GAME's mode, for the maps made for it */
static int fit_players = 1;                 /* everyone in the match (people and AI bots; FILL: 1, any map) */

/* a map for this world and mode: made for the mode, with room for everyone. The Original world: its
 * authored levels for capture the flag. */
static int fit_size = SIZE_LARGE;                    /* (the size chosen: the maps of it) */
static int map_fits(const MenuEnv *e, int i, const Style *st)
{
    if (e->maps[i].biome != st->biome || (fit_size != SIZE_ANY && e->maps[i].size != fit_size)) return 0;
    if (st->faithful) return e->maps[i].original || (e->maps[i].user && e->maps[i].custom);
    return (e->maps[i].modes & (1 << fit_mode)) && e->maps[i].room >= fit_players;
}
#define MAP_MODE(t) ((t) % TEAMS_MODES == TEAMS_CLASSIC ? TEAMS_FLAG : (t) % TEAMS_MODES == TEAMS_KILLCONF ? TEAMS_TDM \
                     : (t) % TEAMS_MODES == TEAMS_HARDPOINT ? TEAMS_CONQUEST : (t) % TEAMS_MODES)   /* (their maps: TDM's arenas, conquest's points) */   /* (CLASSIC: capture the flag's maps) */

static int maps_for(const MenuEnv *e, const Style *st)
{
    int i, n = 0;
    for (i = 0; i < e->num_maps; i++) n += map_fits(e, i, st);
    return n;
}

static int style_index(const Settings *s, const MenuEnv *e)
{
    int i;
    for (i = 0; i < e->num_styles; i++) if (!strcmp(e->styles[i].name, s->style)) return i;
    return 0;
}

static void fit_original_map(Settings *s, const MenuEnv *e)
{
    int i;
    if (s->map_size != SIZE_ANY) s->map_size = SIZE_LARGE;
    fit_size = s->map_size;
    i = maps_find(e->maps, e->num_maps, s->map);
    if (i >= 0 && map_fits(e, i, &e->styles[style_index(s, e)])) return;
    s->map[0] = 0;
    for (i = 0; i < e->num_maps; i++)
        if (e->maps[i].original) {
            snprintf(s->map, sizeof s->map, "%s", e->maps[i].path);
            break;
        }
}


/* step a value row left (-1) or right (+1); returns 1 if anything changed */
/* QUICK MATCH's modes (netui.h): the server's goal words, and their names (0: any) */
const char *const quick_goal_key[QUICK_GOALS] = { "", "flag", "conquest", "hardpoint", "deathmatch", "killconfirmed", "annihilation", "warlord" };
const char *const quick_goal_name[QUICK_GOALS] = { "ANY MODE", "CAPTURE THE FLAG", "CONQUEST", "HARDPOINT", "TEAM DEATHMATCH",
                                                    "KILL CONFIRMED", "ANNIHILATION", "WARLORD" };

static int step(Menu *m, Settings *s, const MenuEnv *e, int dir)
{
    int i, n;
    if (m->page == PAGE_NET && m->row == N_QUICK) {      /* QUICK MATCH: the mode it looks for first */
        s->quick_goal = (s->quick_goal + dir + QUICK_GOALS) % QUICK_GOALS;
        return 1;
    }
    if (m->page == PAGE_MAIN) {
        if (m->row == G_TRAINING) { m->solo = !m->solo; return 1; }   /* training, or today's challenge */
        return 0;
    }
    if (m->page == PAGE_LOCAL) {
        switch (m->row) {
        case M_TYPE:                                 /* the mode: capture the flag, conquest, annihilation */
            if (e->num_styles && e->styles[style_index(s, e)].faithful) return 0;   /* (the Original: its own game only) */
            {
                int mi3, nm = (s->local_type + dir + TEAMS_MODES) % TEAMS_MODES;
                fit_mode = MAP_MODE(nm);
                mi3 = maps_find(e->maps, e->num_maps, s->map);
                if (mi3 >= 0 && e->num_styles && !map_fits(e, mi3, &e->styles[style_index(s, e)])) s->map[0] = 0;
            }
            s->local_type = (s->local_type + dir + TEAMS_MODES) % TEAMS_MODES;
            return 1;
        case M_BOTS0: {                              /* AI bots: 0 .. 7, then FILL (the team screen evens the sides) */
            int mi4;
            if (e->num_styles && e->styles[style_index(s, e)].faithful) return 0;   /* (the Original: 1 vs 1) */
            s->bots_side[0] = (s->bots_side[0] + dir + MATCH_PLAYERS + 1) % (MATCH_PLAYERS + 1);
            fit_players = signin_count() > 0 ? signin_count() : 1;   /* (the people: the AI bots fill up on the team screen) */
            mi4 = maps_find(e->maps, e->num_maps, s->map);   /* (a map without room for them all: a random one) */
            if (mi4 >= 0 && e->num_styles && !map_fits(e, mi4, &e->styles[style_index(s, e)])) s->map[0] = 0;
            return 1;
        }
        case M_ENEMY:                                /* the computer's level: its tanks and the bots */
            s->ai_level = (s->ai_level + dir + 5) % 5;    /* ... and ADAPTIVE */
            return 1;
        case M_TERRAIN: {                            /* the world; a map of another world is dropped */
            int mi;
            if (e->num_styles < 2) return 0;
            i = style_index(s, e);
            for (n = 0; n < e->num_styles; n++) {           /* (with 3 or 4 here: not the Original) */
                i = (i + dir + e->num_styles) % e->num_styles;
                if (world_takes(&e->styles[i], game_modes[s->mode].players)) break;
            }
            snprintf(s->style, sizeof s->style, "%s", e->styles[i].name);
            mi = maps_find(e->maps, e->num_maps, s->map);
            if (mi >= 0 && !map_fits(e, mi, &e->styles[i])) s->map[0] = 0;
            if (e->styles[i].faithful) fit_original_map(s, e);
            return 1;
        }
        case M_SIZE: {                               /* the size: a random battlefield's, and the maps offered (of it) */
            int mi5;
            if (e->num_styles && e->styles[style_index(s, e)].faithful)
                s->map_size = s->map_size == SIZE_ANY ? SIZE_LARGE : SIZE_ANY;
            else s->map_size = (s->map_size + dir + MAP_SIZES + 1) % (MAP_SIZES + 1);
            fit_size = s->map_size;
            mi5 = maps_find(e->maps, e->num_maps, s->map);   /* (a map of another size: a random one) */
            if (mi5 >= 0 && e->num_styles && !map_fits(e, mi5, &e->styles[style_index(s, e)])) s->map[0] = 0;
            return 1;
        }
        case M_MAP: {                                /* random, then this world's map files */
            const Style *stl = &e->styles[style_index(s, e)];
            int k;
            if (!e->num_styles) return 0;
            i = maps_find(e->maps, e->num_maps, s->map);
            for (k = 0; k <= e->num_maps; k++) {
                i += dir;
                if (i < -1) i = e->num_maps - 1;
                if (i >= e->num_maps) i = -1;
                if (i < 0 && stl->faithful && maps_for(e, stl)) continue;   /* Classic: the levels only */
                if (i < 0 || map_fits(e, i, stl)) break;
            }
            snprintf(s->map, sizeof s->map, "%s", i < 0 ? "" : e->maps[i].path);
            return 1;
        }


        default: return 0;
        }
    }
    if (m->page == PAGE_KEYS) {                          /* the pad's buttons step; the keys are pressed */
        if (m->row == K_PAIR) { m->keypair = !m->keypair; return 0; }
        if (m->row == K_PAD_FIRE) { s->pad_fire = pad_next(s->pad_fire, dir); return 1; }
        if (m->row == K_PAD_MINE) { s->pad_mine = pad_next(s->pad_mine, dir); return 1; }
        if (m->row == K_PAD_LAYOUT) { s->pad_layout = (s->pad_layout + dir + PAD_LAYOUTS) % PAD_LAYOUTS; return 1; }   /* the bumpers, the stick, the triggers */
        return 0;
    }
    if (m->page == PAGE_GAME_OPTIONS) {
        switch (m->row) {
        case OPT_RADAR: s->radar = s->radar == 2 ? 1 : 2; return 1;
        case OPT_SIDES: s->sides = !s->sides; return 1;
        case OPT_LIVES:
            s->match_lives = s->match_lives == 255 ? (dir > 0 ? 1 : 99) :
                s->match_lives + dir < 1 || s->match_lives + dir > 99 ? 255 : s->match_lives + dir;
            return 1;
        case OPT_TIME: s->host_time = (s->host_time - 1 + dir + 30) % 30 + 1; return 1;
        case OPT_POWERUPS: s->powerups = !s->powerups; return 1;
        default: return 0;
        }
    }
    if (m->page == PAGE_GRAPHICS) {
        switch (m->row) {
        case GRAPH_VIEW: s->zoom = !s->zoom; return 1;
        case GRAPH_MARKS: s->red_marks = !s->red_marks; render_set_red_marks(s->red_marks); return 1;
        case GRAPH_EFFECTS: s->effects = !s->effects; fx_set_shake(s->effects); audio_set_near_engines(s->effects); return 1;
        case GRAPH_EXPLOSIONS: s->explosions = (s->explosions + EXPLOSION_STYLES + dir) % EXPLOSION_STYLES; return 1;
        case GRAPH_FIRES: s->ground_fires = !s->ground_fires; return 1;
        case GRAPH_WEATHER: s->weather_effects = !s->weather_effects; return 1;
        case GRAPH_AUTO: s->auto_effects ^= 1; return 1;
        case GRAPH_SCREEN: s->fullscreen = !s->fullscreen; return 1;
        default: return 0;
        }
    }
    if (m->page == PAGE_AUDIO) {
        switch (m->row) {
        case AUDIO_MUSIC:
        case AUDIO_EFFECTS: {
            int *value = m->row == AUDIO_MUSIC ? &s->music : &s->volume;
            int next = volume_step(*value, dir);
            if (*value == next) return 0;
            *value = next;
            return 1;
        }
        default: return 0;
        }
    }
    return 0;
}

void menu_fit_players(Settings *s, const MenuEnv *e, int n)
{
    int versus = game_modes[s->mode].versus, best = -1, k;
    if (n < 1 || n > 4) return;
    for (k = 0; k < MODE_COUNT; k++)
        if (game_modes[k].players == n && (best < 0 || game_modes[k].versus == versus)) best = k;
    if (best < 0) return;
    s->mode = best;
    if (e->num_styles && !world_takes(&e->styles[style_index(s, e)], n)) {   /* the Original: two at most */
        for (k = 0; k < e->num_styles && !world_takes(&e->styles[k], n); k++) {}
        if (k < e->num_styles) { snprintf(s->style, sizeof s->style, "%s", e->styles[k].name); s->map[0] = 0; }
    }
}

static void settings_back(Menu *m)
{
    m->row = m->page == PAGE_KEYS ? SET_KEYS : m->page - PAGE_GAME_OPTIONS;
    m->page = PAGE_OPTIONS;
}

int menu_update(Menu *m, Settings *s, const MenuEnv *e, InputBits nav, int key, int back, int ok)
{
    fit_mode = MAP_MODE(s->local_type);
    if (m->page == PAGE_LOCAL && e->num_styles && e->styles[style_index(s, e)].faithful)
        fit_original_map(s, e);
    fit_size = s->map_size;
    fit_players = signin_count() > 0 ? signin_count() : 1;   /* (the people: the AI bots fill up on the team screen) */
    InputBits pr = (InputBits)(nav & ~m->prev);
    int n = rows_of(m->page);
    m->prev = nav;
    m->frame++;

    if (m->page == PAGE_CONTROLLERS) {
        int dir = pr & IN_LEFT ? -1 : pr & IN_RIGHT ? 1 : 0;
        if (back || ((ok || (pr & IN_FIRE)) && m->row == 5)) {
            m->page = PAGE_TOOLS; m->row = TOOL_CONTROLLERS; return MENU_NOTHING;
        }
        if (pr & IN_UP) m->row = (m->row + 5) % 6;
        if (pr & IN_DOWN) m->row = (m->row + 1) % 6;
        if (m->row == 0 && (dir || ok || (pr & IN_FIRE))) m->diagnostic_pad = (m->diagnostic_pad + 4 + (dir ? dir : 1)) % 4;
        if (m->row == 1 && (dir || ok || (pr & IN_FIRE))) {
            s->stick_deadzone += dir ? dir : 1;
            if (s->stick_deadzone < 12) s->stick_deadzone = 12;
            if (s->stick_deadzone > 50) s->stick_deadzone = 50;
            input_set_deadzone(s->stick_deadzone); return MENU_CHANGED;
        }
        if (ok || (pr & IN_FIRE)) {
            if (m->row == 2) menu_notice(m, input_calibrate(DEV_PAD1 + m->diagnostic_pad) ? "STICK CENTER CALIBRATED" : "RELEASE STICK AND TRY AGAIN");
            if (m->row == 3) input_rumble_dev(DEV_PAD1 + m->diagnostic_pad, 60, 300);
            if (m->row == 4) { input_reset_calibration(); s->stick_deadzone = 37; input_set_deadzone(37); return MENU_CHANGED; }
        }
        return MENU_NOTHING;
    }
    if (m->capture >= 0) {                               /* redefining a key */
        if (back) { m->capture = -1; return MENU_NOTHING; }
        if (key > 0) {
            int p = m->keypair * 2 + m->capture / ACT_COUNT, a = m->capture % ACT_COUNT;
            s->keys[p][a][0] = key;
            s->keys[p][a][1] = 0;
            m->capture = -1;
            m->prev = 0xFF;
            return MENU_CHANGED;
        }
        return MENU_NOTHING;
    }
    {   /* typing into a text row */
        size_t cap;
        int kind;
        char *f = field(m, s, &cap, &kind);
        if (f && key == SDL_SCANCODE_BACKSPACE) {
            size_t l = strlen(f);
            if (l) f[l - 1] = 0;
            return MENU_CHANGED;
        }
        if (f) {                                           /* letters are text here, not steering */
            pr = 0;
            if (key == SDL_SCANCODE_UP) pr = IN_UP;
            if (key == SDL_SCANCODE_DOWN) pr = IN_DOWN;
        }
        (void)cap; (void)kind;
    }
    if (m->page == PAGE_ABOUT) {
        if (back || ok || (pr & IN_FIRE)) { m->page = PAGE_MAIN; m->row = G_ABOUT; }
        return MENU_NOTHING;
    }
    if (back) {
        if (m->page == PAGE_MAIN) return MENU_QUIT;
        if (m->page == PAGE_CHALLENGES) { m->page = PAGE_TOOLS; m->row = TOOL_CHALLENGES; return MENU_NOTHING; }
        if (m->page == PAGE_LOCAL) { m->page = PAGE_MAIN; m->row = G_LOCAL; return MENU_CHANGED; }
        if (m->page == PAGE_NET) { m->page = PAGE_MAIN; m->row = G_NET; return MENU_CHANGED; }
        if (m->page == PAGE_DIRECT) { m->page = PAGE_NET; m->row = N_JOIN; return MENU_CHANGED; }
        if (m->page == PAGE_KEYS || (m->page >= PAGE_GAME_OPTIONS && m->page <= PAGE_AUDIO)) settings_back(m);
        else { m->row = m->page == PAGE_TOOLS ? G_TOOLS : G_OPTIONS; m->page = PAGE_MAIN; }
        return MENU_NOTHING;
    }
    if (pr & IN_UP)   m->row = (m->row + n - 1) % n;
    if (pr & IN_DOWN) m->row = (m->row + 1) % n;
    if ((pr & IN_LEFT) && step(m, s, e, -1))  return MENU_CHANGED;
    if ((pr & IN_RIGHT) && step(m, s, e, +1)) return MENU_CHANGED;
    if (ok || (pr & IN_FIRE)) {
        if (m->page == PAGE_MAIN) {
            if (m->row == G_LOCAL) { m->page = PAGE_LOCAL; m->row = M_START; return MENU_CHANGED; }
            if (m->row == G_TOOLS) { m->page = PAGE_TOOLS; m->row = 0; return MENU_NOTHING; }
            if (m->row == G_TRAINING) return m->solo ? MENU_DAILY : MENU_TRAINING;
            if (m->row == G_ABOUT) { m->page = PAGE_ABOUT; m->row = 0; m->credits_start = SDL_GetTicks(); return MENU_NOTHING; }
            if (m->row == G_QUIT) return MENU_QUIT;
            if (m->row == G_OPTIONS) { m->page = PAGE_OPTIONS; m->row = 0; return MENU_NOTHING; }
            if (m->row == G_NET) { m->page = PAGE_NET; m->row = N_FIND; return MENU_NOTHING; }
            return MENU_NOTHING;
        }
        if (m->page == PAGE_LOCAL) {
            if (m->row == M_START) return MENU_START;
            if (m->row == M_BACK) { m->page = PAGE_MAIN; m->row = G_LOCAL; return MENU_CHANGED; }
            if (m->row == M_MAP) return MENU_GALLERY;         /* (left and right: your maps; FIRE: the web site's) */

            return step(m, s, e, +1) ? MENU_CHANGED : MENU_NOTHING;
        }
        if (m->page == PAGE_NET) {
            if (m->row == N_FIND) return MENU_FIND;
            if (m->row == N_QUICK) return MENU_QUICK;
            if (m->row == N_JOIN) { m->page = PAGE_DIRECT; m->row = D_ADDRESS; return MENU_CHANGED; }   /* the window */
            if (m->row == N_BACK) { m->page = PAGE_MAIN; m->row = G_NET; return MENU_CHANGED; }
            return MENU_NOTHING;
        }
        if (m->page == PAGE_DIRECT) {                    /* DIRECT CONNECT */
            if (m->row == D_ADDRESS) { m->row = D_PASSWORD; return MENU_NOTHING; }   /* (on to the password) */
            if (m->row == D_SAVE) { s->direct_save = !s->direct_save; return MENU_CHANGED; }
            if (m->row == D_CANCEL) { m->page = PAGE_NET; m->row = N_JOIN; return MENU_CHANGED; }
            if (!s->server[0]) { m->row = D_ADDRESS; return MENU_NOTHING; }   /* (an address first) */
            if (s->direct_save) settings_add_mine(s, s->server, s->server_password);
            m->page = PAGE_NET; m->row = N_JOIN;
            return MENU_JOIN;                            /* CONNECT (or Enter on the password) */
        }
        if (m->page == PAGE_CHALLENGES) {
            if (m->row < 3) return MENU_CHALLENGE_RESCUE + m->row;
            m->page = PAGE_TOOLS; m->row = TOOL_CHALLENGES; return MENU_NOTHING;
        }
        if (m->page == PAGE_TOOLS) {
            if (m->row == TOOL_EDITOR) return MENU_EDIT;
            if (m->row == TOOL_REPLAYS) return MENU_REPLAYS;
            if (m->row == TOOL_CONTROLLERS) { m->page = PAGE_CONTROLLERS; m->row = 0; return MENU_NOTHING; }
            if (m->row == TOOL_BENCHMARK) return MENU_BENCHMARK;
            if (m->row == TOOL_CHALLENGES) { m->page = PAGE_CHALLENGES; m->row = 0; return MENU_NOTHING; }
            m->page = PAGE_MAIN; m->row = G_TOOLS; return MENU_NOTHING;
        }
        if (m->page == PAGE_OPTIONS) {
            if (m->row == SET_BACK) { m->page = PAGE_MAIN; m->row = G_OPTIONS; return MENU_NOTHING; }
            m->page = m->row == SET_KEYS ? PAGE_KEYS : PAGE_GAME_OPTIONS + m->row;
            m->row = 0;
            return MENU_NOTHING;
        }
        if (m->page >= PAGE_GAME_OPTIONS && m->page <= PAGE_AUDIO) {
            if (m->row == n - 1) { settings_back(m); return MENU_NOTHING; }
            return step(m, s, e, +1) ? MENU_CHANGED : MENU_NOTHING;
        }
        if (m->row == K_BACK) { settings_back(m); return MENU_NOTHING; }
        if (m->row == K_DEFAULTS) {
            settings_default_keys(s);
            signin_reset_keys();
            return MENU_CHANGED;
        }
        if (m->row == K_PAD_FIRE) { s->pad_fire = pad_next(s->pad_fire, +1); return MENU_CHANGED; }
        if (m->row == K_PAD_MINE) { s->pad_mine = pad_next(s->pad_mine, +1); return MENU_CHANGED; }
        if (m->row == K_PAD_LAYOUT) { s->pad_layout = (s->pad_layout + 1) % PAD_LAYOUTS; return MENU_CHANGED; }
        if (m->row == K_PAIR) { m->keypair = !m->keypair; return MENU_NOTHING; }   /* players 1-2's keys, or 3-4's */
        m->capture = m->row;                             /* the next key pressed */
    }
    return MENU_NOTHING;
}

static const char *key_name(int sc)
{
    static const struct { const char *from, *to; } shorter[] = {
        { "Left Shift", "LSHF" }, { "Right Shift", "RSHF" }, { "Left Ctrl", "LCTL" },   /* (two keys fit a half row) */
        { "Right Ctrl", "RCTL" }, { "Left Alt", "LALT" }, { "Right Alt", "RALT" }, { "Left GUI", "LCMD" },
        { "Right GUI", "RCMD" }, { "Return", "RET" }, { "Space", "SPC" }, { "Backspace", "BKSP" }, { "CapsLock", "CAPS" },
    };
    static char buf[4][24];
    static int k;
    const char *nm = sc > 0 ? SDL_GetScancodeName((SDL_Scancode)sc) : "";
    size_t i;
    if (!nm || !nm[0]) return "-";
    for (i = 0; i < sizeof shorter / sizeof shorter[0]; i++)
        if (!strcmp(nm, shorter[i].from)) return shorter[i].to;
    k = (k + 1) & 3;
    snprintf(buf[k], sizeof buf[k], "%s", nm);
    if (!strncmp(buf[k], "Keypad ", 7)) { buf[k][0] = 'K'; buf[k][1] = 'P'; memmove(buf[k] + 2, buf[k] + 7, strlen(buf[k] + 7) + 1); }
    return buf[k];
}

static void upper(char *s) { for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 32); }

/* one line of the list: label on the left, value on the right, arrows when it can change */
static int no_arrows;                 /* key rows don't change with left/right */

/* the menu's own colours: dark steel plates, khaki lettering, amber for
 * what you can change, an amber bar for where you are */
static const Rgb M_STEEL = { 24, 26, 22 }, M_STEEL_HI = { 58, 60, 52 }, M_BRONZE = { 168, 122, 56 },
                 M_LABEL = { 204, 196, 170 }, M_VALUE = { 250, 196, 64 }, M_DIM = { 104, 100, 86 },
                 M_BAR = { 232, 176, 48 }, M_BAR_DARK = { 150, 104, 24 }, M_INK = { 22, 16, 8 },
                 M_LINE = { 54, 54, 46 }, M_OURS = { 90, 150, 255 }, M_THEIRS = { 255, 80, 60 };

void menu_panel(SDL_Renderer *r, int x, int y, int w, int h)
{
    ui_fill(r, x, y, w, h, (Rgb){ 0, 0, 0 });
    ui_fill(r, x + 1, y + 1, w - 2, h - 2, M_BRONZE);
    ui_fill(r, x + 2, y + 2, w - 4, h - 4, (Rgb){ 0, 0, 0 });
    ui_fill(r, x + 3, y + 3, w - 6, h - 6, M_STEEL);
    ui_fill(r, x + 3, y + 3, w - 6, 1, M_STEEL_HI);            /* light along the top edge */
    ui_fill(r, x + 5, y + 5, 2, 2, M_BRONZE);                   /* rivets */
    ui_fill(r, x + w - 7, y + 5, 2, 2, M_BRONZE);
    ui_fill(r, x + 5, y + h - 7, 2, 2, M_BRONZE);
    ui_fill(r, x + w - 7, y + h - 7, 2, 2, M_BRONZE);
}

static void row(SDL_Renderer *r, const HudColors *c, int x, int y, int w, int selected, int blink,
                const char *label, const char *value, int dim)
{
    (void)c; (void)blink;
    if (selected) {                                     /* an amber bar across the whole row */
        ui_fill(r, x - 6, y - 2, w + 16, 12, M_BAR);
        ui_fill(r, x - 6, y + 9, w + 16, 1, M_BAR_DARK);
        ui_fill(r, x - 6, y - 2, 2, 12, M_BAR_DARK);
    }
    ui_text(r, x, y, 1, selected ? M_INK : dim ? M_DIM : M_LABEL, label);
    if (value) {
        char buf[200];
        int vw, room = w - ui_text_width(label, 1) - 8 - (selected && !dim && !no_arrows ? 10 : 0);
        snprintf(buf, sizeof buf, "%s", value);
        upper(buf);
        vw = ui_text_width(buf, 1);
        if (vw > room && room > 24) {                    /* too long: ".." and the end of it (an address's port) */
            size_t n = strlen(buf);
            const char *tail = buf;
            while (*tail && ui_text_width(tail, 1) + ui_text_width("..", 1) > room) tail++;
            memmove(buf + 2, tail, strlen(tail) + 1);
            buf[0] = buf[1] = '.';
            (void)n;
            vw = ui_text_width(buf, 1);
        }
        ui_text(r, x + w - vw, y, 1, selected ? M_INK : dim ? M_DIM : M_VALUE, buf);
        if (selected && !dim && !no_arrows) {
            ui_icon(r, ICON_LEFT, x + w - vw - 10, y, M_INK);
            ui_icon(r, ICON_RIGHT, x + w + 2, y, M_INK);
        }
    }
}

static void divider(SDL_Renderer *r, int x, int y, int w) { ui_fill(r, x, y, w, 1, M_LINE); }

static void hint(SDL_Renderer *r, int sw, int y, const char *s)
{
    ui_text(r, (sw - ui_text_width(s, 1)) / 2, y, 1, M_DIM, s);
}

static void graphics_preview(SDL_Renderer *r, const Theme *theme, const Style *style,
                             const Menu *m, const Settings *s, int x, int w)
{
    const char *caption = s->zoom ? "CLOSE-UP VIEW" : "WIDE VIEW";
    SDL_Rect field = { x + 9, 78, w - 18, 61 };
    uint32_t now = SDL_GetTicks(), phase = now % 2400;
    int show_combat = m->row == GRAPH_EFFECTS;
    int impact = show_combat && phase < 180;
    int offset = impact && s->effects ? (int)(phase / 30 % 3) - 1 : 0;
    int size = m->row == GRAPH_VIEW && s->zoom ? 2 : 1;
    int cx = field.x + field.w / 2, cy = field.y + field.h / 2;
    int tile = TILE_SIZE * size;

    menu_panel(r, x, 48, w, 114);
    fit_centre(r, x + 4, w - 8, 58, M_VALUE, "PREVIEW");
    ui_fill(r, field.x - 2, field.y - 2, field.w + 4, field.h + 4, M_LINE);
    SDL_RenderSetClipRect(r, &field);
    ui_fill(r, field.x, field.y, field.w, field.h, (Rgb){ 183, 153, 104 });
    if (style && style->tiles) {
        for (int row = -2; row <= 2; row++) {
            for (int col = -1; col * tile < field.w + 2; col++) {
                SDL_Rect src = { ((col + 1 + (row + 2) * 3) & 15) * TILE_SIZE,
                                 (row == 0 ? TILE_ROAD : TILE_SAND) * TILE_SIZE, TILE_SIZE, TILE_SIZE };
                SDL_Rect dst = { field.x + col * tile + offset, cy + row * tile - tile / 2, tile, tile };
                SDL_RenderCopy(r, style->tiles, &src, &dst);
            }
        }
    }
    if (m->row == GRAPH_MARKS) {
        ui_fill(r, cx + 17, cy + 11, 9, 5, (Rgb){ 43, 37, 29 });
        ui_fill(r, cx + 19, cy + 9, 5, 8, (Rgb){ 51, 44, 37 });
        if (s->red_marks) {
            ui_fill(r, cx - 22, cy + 8, 5, 4, (Rgb){ 162, 31, 22 });
            ui_fill(r, cx - 25, cy + 11, 2, 2, (Rgb){ 190, 43, 27 });
            ui_fill(r, cx - 18, cy + 14, 3, 2, (Rgb){ 162, 31, 22 });
        }
        caption = s->red_marks ? "BLOOD + SCORCH" : "SCORCH ONLY";
    }
    render_tank_icon(r, style && style->tanks ? style->tanks : theme->tanks,
                     TANK_MEDIUM, TEAM_PLAYER, cx + offset, cy, 24 * size);
    if (show_combat) {
        if (impact) {
            int spread = 3 + (int)phase / 20;
            ui_fill(r, cx + offset - 2, cy - 19, 4, 4, M_VALUE);
            ui_fill(r, cx + offset + spread, cy - 20, 2, 2, M_VALUE);
            ui_fill(r, cx + offset - spread, cy - 16, 2, 2, M_BAR);
            if (s->effects && phase < 95) {
                SDL_SetRenderDrawColor(r, 255, 244, 208, (Uint8)(64 - phase * 64 / 95));
                SDL_RenderFillRect(r, &field);
            }
        }
        caption = s->effects ? "SHAKE + FLASH" : "CALM CAMERA";
    } else if (m->row == GRAPH_EXPLOSIONS) {
        if (s->explosions != EXPLOSIONS_OFF && phase < 600) {
            int spread = s->explosions == EXPLOSIONS_FULL ? (int)phase / 45 + 2 : 3;
            ui_fill(r, cx + 17 - spread / 2, cy - 10, spread + 3, 6, (Rgb){ 230, 72, 20 });
            ui_fill(r, cx + 17, cy - 12, 3, 6, M_VALUE);
            if (s->explosions == EXPLOSIONS_FULL) {
                for (int i = 0; i < 8; i++) {
                    int ox = (i % 3 - 1) * spread, oy = (i / 3 - 1) * spread;
                    ui_fill(r, cx + 18 + ox, cy - 8 + oy, 2, 2, i & 1 ? M_VALUE : M_DIM);
                }
            }
        }
        caption = s->explosions == EXPLOSIONS_FULL ? "FIRE + DEBRIS" :
                  s->explosions == EXPLOSIONS_SIMPLE ? "SMALL BURST" : "BLASTS OFF";
    } else if (m->row == GRAPH_FIRES) {
        ui_fill(r, cx + 12, cy + 8, 14, 6, (Rgb){ 43, 37, 29 });
        if (s->ground_fires) {
            int flicker = (int)(now / 90 % 3);
            ui_fill(r, cx + 16, cy + 2 - flicker, 4, 9 + flicker, (Rgb){ 230, 72, 20 });
            ui_fill(r, cx + 17, cy + 6, 2, 4, M_VALUE);
            ui_fill(r, cx + 15 + flicker, cy - 5, 6, 5, M_DIM);
        }
        caption = s->ground_fires ? "BURNING RUINS" : "COLD RUINS";
    } else if (m->row == GRAPH_WEATHER) {
        if (s->weather_effects) render_weather(r, &field, now / 20, WEATHER_RAIN, TIME_DAY, 0);
        caption = s->weather_effects ? "RAIN SAMPLE" : "WEATHER OFF";
    } else if (m->row == GRAPH_SCREEN) {
        caption = s->fullscreen ? "FULL DISPLAY" : "WINDOWED";
    }
    SDL_RenderSetClipRect(r, NULL);
    fit_centre(r, x + 4, w - 8, 147, M_LABEL, caption);
}

void menu_background(SDL_Renderer *r, int sw, int sh)
{
    SDL_Rect all = { 0, 0, sw, sh };
    ui_backdrop(r, 0, 0, sw, sh);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);           /* calm the camouflage down */
    SDL_SetRenderDrawColor(r, 0, 0, 0, 150);
    SDL_RenderFillRect(r, &all);

    /* the name plate */
    menu_panel(r, 62, 6, sw - 124, 36);
    ui_text_shadow(r, (sw - ui_text_width("OPENFP", 2) - 2) / 2, 11, 2, M_VALUE, "OPENFP");
    ui_text(r, (sw - ui_text_width("AN OPEN REMAKE", 1)) / 2, 30, 1, M_DIM, "AN OPEN REMAKE");

}

void menu_draw(SDL_Renderer *r, const Theme *t, const Menu *m, const Settings *s, const MenuEnv *e,
               int sw, int sh)
{
    fit_mode = MAP_MODE(s->local_type);
    fit_size = s->map_size;                       /* (the maps made for this mode) */
    fit_players = signin_count() > 0 ? signin_count() : 1;   /* (the people: the AI bots fill up on the team screen) */
    const HudColors *c = &t->hud;
    int blink = ui_blink(1000), px = 84, pw = sw - 168, i, y;
    char buf[64];
    menu_background(r, sw, sh);

    if (m->page == PAGE_ABOUT) {
        int k, top = 58, height = 132;
        int span = height + OFP_CREDIT_LINES * 12 + 60;
        int scroll = (int)((SDL_GetTicks() - m->credits_start) / OFP_CREDITS_STEP_MS % (unsigned)span);
        SDL_Rect clip = { 24, top, sw - 48, height }, previous;
        int clipped = SDL_RenderIsClipEnabled(r);
        SDL_RenderGetClipRect(r, &previous);
        menu_panel(r, 18, 48, sw - 36, 149);
        SDL_RenderSetClipRect(r, &clip);
        for (k = 0; k < OFP_CREDIT_LINES; k++) {
            int cy = top + height + k * 12 - scroll;
            if (cy + 7 < top || cy >= top + height) continue;
            ui_text(r, (sw - ui_text_width(ofp_credits[k].text, 1)) / 2, cy, 1,
                    ofp_credits[k].heading ? M_VALUE : M_LABEL, ofp_credits[k].text);
        }
        SDL_RenderSetClipRect(r, clipped ? &previous : NULL);
        menu_panel(r, 115, 201, sw - 230, 16);
        fit_centre(r, 115, sw - 230, 205, M_VALUE, "BACK");
        return;
    }
    if (m->page == PAGE_MAIN) {                          /* the first page: where to go */
        static const char *labels[G_ROWS] = { "LOCAL GAME", "NETWORK GAME", NULL, "TOOLS", "SETTINGS", "ABOUT", "QUIT" };
        static const char *hints[G_ROWS] = { "1 TO 4 HERE: THE COMPUTER, OR EACH OTHER",
                                             "SERVERS ON THE INTERNET OR YOUR NETWORK", NULL, "MAP EDITOR, REPLAYS AND BENCHMARK",
                                             "GAME OPTIONS, GRAPHICS, AUDIO AND CONTROLS", "SOUNDTRACK CREDITS", "" };
        int i2, px2 = 120, pw2 = 144;
        menu_panel(r, px2 - 16, 58, pw2 + 32, 130);
        for (i2 = 0; i2 < G_ROWS; i2++) {
            const char *lb = i2 == G_TRAINING ? (m->solo ? "DAILY CHALLENGE" : "TRAINING") : labels[i2];
            if (i2 == G_TOOLS || i2 == G_QUIT) divider(r, px2 - 6, 66 + i2 * 14 - 5, pw2 + 12);
            row(r, c, px2, 66 + i2 * 14, pw2, m->row == i2, blink, lb, NULL, 0);
        }
        if (SDL_GetTicks() < m->notice_until) ui_text(r, (sw - ui_text_width(m->notice, 1)) / 2, 201, 1, M_VALUE, m->notice);
        else hint(r, sw, 201, m->row == G_TRAINING ? (m->solo ? "TODAY'S, THE SAME FOR ALL (LEFT/RIGHT)"
                                                              : "FIRST STEPS (LEFT/RIGHT: DAILY CHALLENGE)")
                            : hints[m->row] && hints[m->row][0] ? hints[m->row] : (input_pad_count() ? "D-PAD/STICK AND A   HOLD B: SIGN OUT" : "ARROWS AND FIRE   ESC: QUIT"));
        return;
    }
    if (m->page == PAGE_LOCAL) {                         /* LOCAL GAME: how many, what kind, where */
        const char *terrain = e->num_styles ? e->styles[style_index(s, e)].title : "-";
        int versus = 1, lx = 24, lw = 186;
        int mi = maps_find(e->maps, e->num_maps, s->map);
        int nmaps = e->num_styles ? maps_for(e, &e->styles[style_index(s, e)]) : 0;
        int faithful = e->num_styles && e->styles[style_index(s, e)].faithful;
        char mname[24];
        snprintf(mname, sizeof mname, "%.16s", mi < 0 ? "RANDOM" : e->maps[mi].name);
        menu_panel(r, 8, 46, 222, 152);
        y = 54;
        ui_text_bold(r, lx, y - 1, 1, M_OURS, "LOCAL GAME");                            y += 12;

        row(r, c, lx, y, lw, m->row == M_TERRAIN, blink, "WORLD", terrain, 0);           y += 11;
        {   /* what the world offers: its modes (the Original: CLASSIC only), a size, its maps of them */
            static const char *types[TEAMS_MODES] = { "CAPTURE THE FLAG", "CONQUEST", "ANNIHILATION", "TEAM DEATHMATCH", "WARLORD", "CLASSIC" , "KILL CONFIRMED", "HARDPOINT" };
            static const char *sz[] = { "SMALL", "MEDIUM", "LARGE", "HUGE", "ANY" };
            int orig = e->num_styles && e->styles[style_index(s, e)].faithful;
            int zs = s->map_size;
            row(r, c, lx, y, lw, m->row == M_TYPE, blink, "MODE", orig && e->preview && e->preview->map.custom_bases ? map_mode_names[e->preview->map.mode] :
                types[orig ? TEAMS_CLASSIC : s->local_type % TEAMS_MODES], orig);   y += 11;
            row(r, c, lx, y, lw, m->row == M_SIZE, blink, "SIZE", sz[zs >= 0 && zs <= SIZE_ANY ? zs : SIZE_LARGE], 0);             y += 11;
        }
        row(r, c, lx, y, lw, m->row == M_MAP, blink, "MAP", mname, !nmaps);            y += 11;
        divider(r, lx - 6, y, lw + 12);                                                  y += 5;
        row(r, c, lx, y, lw, m->row == M_START, blink, "START GAME", NULL, 0);           y += 11;
        row(r, c, lx, y, lw, m->row == M_BACK, blink, "BACK", NULL, 0);
        /* the map you'll play on: both camps and flags, and how many maps this world has */
        menu_panel(r, 236, 46, 140, 152);
        if (e->preview) {
            SDL_Rect pv = { 242, 54, 128, 104 };
            render_map_preview(r, e->preview, &pv, m->frame);
            int sides[2], ffa = s->local_type % TEAMS_MODES == TEAMS_ANNIHILATION && !faithful;   /* (every tank for itself: no sides) */
            int room = match_sides(e->preview, ffa ? EXTRA_ANNIHILATION : 0, sides);
            if (faithful || s->local_type % TEAMS_MODES == TEAMS_CLASSIC) sides[0] = sides[1] = 1;   /* (the Original, CLASSIC: 1 vs 1) */
            if (ffa) snprintf(buf, sizeof buf, "%sFOR %d PLAYERS", e->preview->map.arena ? "ARENA, " : "", room);
            else snprintf(buf, sizeof buf, "%s%d VS %d", e->preview->map.arena ? "ARENA, " : "", sides[0], sides[1]);
            fit_centre(r, 238, 136, 160, M_LABEL, buf);
        }
        if (faithful)
            snprintf(buf, sizeof buf, nmaps ? "%d LEVELS" : "NO MAPS OF THIS SIZE", nmaps);   /* (onto the window: the levels) */
        else
            snprintf(buf, sizeof buf, mi < 0 ? "A NEW RANDOM MAP" : "%d %s MAP%s", nmaps,
                     e->num_styles ? biome_name((Biome)e->styles[style_index(s, e)].biome) : "", nmaps == 1 ? "" : "S");
        upper(buf);
        fit_centre(r, 238, 136, 170, M_DIM, buf);
        if (e->preview) {                                /* when, and in what: the map's own (a random one: its own too) */
            static const char *when[TIME_COUNT] = { "BY DAY", "AT DUSK", "BY NIGHT" };
            int tod = e->preview->map.time % TIME_COUNT;
            int weather = weather_visual((Biome)e->preview->biome, e->preview->weather);
            snprintf(buf, sizeof buf, "%s%s%s", when[tod], weather ? ", " : "",
                     weather ? weather_label(weather) : "");
            fit_centre(r, 238, 136, 180, M_LABEL, buf);
        }
        if (signin_count() > 0) {                        /* who's playing, by name */
            int k2;
            buf[0] = 0;
            for (k2 = 0; k2 < signin_count() && k2 < 4; k2++)
                snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "%s%.8s", k2 ? ", " : "", signin_name(k2));
            {   /* and the computer's players, and the kind of game */
                int nb = s->bots_side[0] == BOTS_FILL ? -1 : s->bots_side[0];
                (void)nb;
            }
            upper(buf);
        } else {                                         /* (nobody signed in: the settings' name) */
            int nb = s->bots_side[0] == BOTS_FILL ? -1 : s->bots_side[0];
            snprintf(buf, sizeof buf, "%.12s", s->player_name[0] ? s->player_name : "PLAYER");
            (void)nb;
            upper(buf);
            (void)versus;
        }
        fit_centre(r, lx - 6, lw + 12, y + 16, M_LABEL, buf);   /* (in the left box, under BACK) */
        if (SDL_GetTicks() < m->notice_until) ui_text(r, (sw - ui_text_width(m->notice, 1)) / 2, 201, 1, M_VALUE, m->notice);
        else hint(r, sw, 201, m->row == M_MAP ? "FIRE: MORE MAPS FROM THE WEB SITE"
                            : (input_pad_count() ? "D-PAD/STICK AND A   B: BACK" : "ARROWS AND FIRE   ESC: BACK"));
        return;
    }
    if (m->page == PAGE_NET) {
        char tx[300];
        int y0 = 54, cur = blink;
        px = 92; pw = sw - 184;                          /* (four rows: a box their size) */
        menu_panel(r, px - 22, 44, pw + 44, 108);
        ui_text_bold(r, px, y0, 1, M_VALUE, "NETWORK GAME");
        divider(r, px - 6, y0 + 11, pw + 12);
        no_arrows = 1;                                    /* nothing here steps left/right */
        snprintf(tx, sizeof tx, "%s%s", s->player_name, m->row == N_NAME && cur ? "_" : "");
        if (signin_count() > 0) {                        /* signed in: who joins (as themselves) */
            int k2;
            tx[0] = 0;
            for (k2 = 0; k2 < signin_count() && k2 < 4; k2++)
                snprintf(tx + strlen(tx), sizeof tx - strlen(tx), "%s%.8s", k2 ? ", " : "", signin_name(k2));
            upper(tx);
        }
        row(r, c, px, y0 + 20, pw, m->row == N_NAME, blink, signin_count() > 1 ? "PLAYERS" : "NAME", tx, signin_count() > 0);
        no_arrows = 0;

        no_arrows = 1;
        /* finding a game: the server list */
        no_arrows = 0;
        row(r, c, px, y0 + 40, pw, m->row == N_QUICK, blink, "QUICK MATCH", quick_goal_name[s->quick_goal % QUICK_GOALS], 0);
        no_arrows = 1;
        row(r, c, px, y0 + 51, pw, m->row == N_FIND, blink, "FIND SERVERS", NULL, 0);
        /* or straight to one, by its address (a window: address, password) */
        row(r, c, px, y0 + 62, pw, m->row == N_JOIN, blink, "DIRECT CONNECT", NULL, 0);
        divider(r, px - 6, y0 + 78, pw + 12);
        row(r, c, px, y0 + 83, pw, m->row == N_BACK, blink, "BACK", NULL, 0);
        no_arrows = 0;
        if (SDL_GetTicks() < m->notice_until) ui_text(r, (sw - ui_text_width(m->notice, 1)) / 2, 200, 1, M_VALUE, m->notice);
        else hint(r, sw, 200, m->row == N_FIND ? "PUBLIC SERVERS, AND YOUR OWN"
                       : m->row == N_QUICK ? "THE BEST OPEN GAME (LEFT/RIGHT: A MODE FIRST)"
                       : m->row == N_JOIN ? "A SERVER BY ITS ADDRESS: LISTED OR NOT"
                       : m->row == N_NAME ? (signin_count() > 0 ? "SIGNED IN: THEIR PROFILES' NAMES" : "HOW OTHERS SEE YOU") : "ESC: BACK");
        return;
    }
    if (m->page == PAGE_DIRECT) {                        /* DIRECT CONNECT: a window */
        char tx[96], stars[40];
        size_t si;
        int cur2 = (SDL_GetTicks() / 400) % 2, wx = px - 10, ww = pw + 20;
        menu_panel(r, wx - 12, 56, ww + 24, 128);
        ui_text_bold(r, wx + 4, 64, 1, M_VALUE, "DIRECT CONNECT");
        divider(r, wx, 76, ww);
        snprintf(tx, sizeof tx, "%s%s", s->server[0] ? s->server : (m->row == D_ADDRESS ? "" : "HOST OR HOST:PORT"), m->row == D_ADDRESS && cur2 ? "_" : "");
        row(r, c, px, 82, pw, m->row == D_ADDRESS, blink, "ADDRESS", tx, !s->server[0] && m->row != D_ADDRESS);
        for (si = 0; si < strlen(s->server_password) && si + 1 < sizeof stars; si++) stars[si] = '*';
        stars[si] = 0;
        snprintf(tx, sizeof tx, "%s%s", stars[0] ? stars : (m->row == D_PASSWORD ? "" : "NONE"), m->row == D_PASSWORD && cur2 ? "_" : "");
        row(r, c, px, 94, pw, m->row == D_PASSWORD, blink, "PASSWORD", tx, !stars[0] && m->row != D_PASSWORD);
        row(r, c, px, 106, pw, m->row == D_SAVE, blink, "SAVE TO MY SERVERS", s->direct_save ? "YES" : "NO", 0);
        divider(r, wx, 120, ww);
        row(r, c, px, 126, pw, m->row == D_CONNECT, blink, "CONNECT", NULL, 0);
        row(r, c, px, 138, pw, m->row == D_CANCEL, blink, "CANCEL", NULL, 0);
        hint(r, sw, 200, m->row == D_ADDRESS ? "A NAME OR AN IP ADDRESS, AND :PORT IF NOT 29870"
                        : m->row == D_PASSWORD ? "ONLY FOR A SERVER THAT ASKS FOR ONE" : m->row == D_SAVE ? "IN FIND SERVERS, UNDER MY SERVERS"
                        : input_pad_count() ? "A: CHOOSE   B: CANCEL" : "ENTER: CHOOSE   ESC: CANCEL");
        return;
    }
    if (m->page == PAGE_CHALLENGES) {
        static const char *const names[] = { "TIMED RESCUE", "BRIDGE CAPTURE", "ONE TANK RAID", "BACK" };
        static const char *const detail[] = { "JUNGLE / RESCUE FOUR / 3 MIN", "DESERT / HOLD THE BRIDGE / 2 MIN", "VOLCANO / THREE GUNS / ONE TANK" };
        menu_panel(r, px - 22, 48, pw + 44, 114);
        ui_text_bold(r, px, 58, 1, M_VALUE, "FIELD CHALLENGES");
        divider(r, px - 6, 70, pw + 12);
        for (i = 0; i < 3; i++) row(r, c, px, 81 + i * 25, pw, m->row == i, blink, names[i], NULL, 0);
        menu_panel(r, px - 22, 167, pw + 44, 24);
        row(r, c, px, 175, pw, m->row == 3, blink, names[3], NULL, 0);
        if (m->row < 3) hint(r, sw, 203, detail[m->row]);
        return;
    }
    if (m->page == PAGE_CONTROLLERS) {
        InputSnapshot snapshot; int dev = DEV_PAD1 + m->diagnostic_pad, owner = -1, j;
        input_snapshot(dev, &snapshot);
        for (j = 0; j < signin_count(); j++) if (signin_device(j) == dev) owner = j;
        menu_panel(r, 8, 48, sw - 16, 154);
        ui_text_bold(r, 18, 55, 1, M_VALUE, "CONTROLLER TEST");
        snprintf(buf, sizeof buf, "PAD %d / %s", m->diagnostic_pad + 1, owner < 0 ? "UNASSIGNED" : signin_name(owner));
        row(r, c, 18, 72, sw - 36, m->row == 0, blink, "DEVICE", buf, 0);
        snprintf(buf, sizeof buf, "%d%%", s->stick_deadzone);
        row(r, c, 18, 87, sw - 36, m->row == 1, blink, "DEADZONE", buf, 0);
        row(r, c, 18, 102, sw - 36, m->row == 2, blink, "CALIBRATE CENTER", NULL, 0);
        row(r, c, 18, 117, sw - 36, m->row == 3, blink, "TEST RUMBLE", s->rumble ? "300 MS" : "DISABLED", 0);
        row(r, c, 18, 132, sw - 36, m->row == 4, blink, "RESET CALIBRATION", NULL, 0);
        snprintf(buf, sizeof buf, "LEFT %d,%d  RIGHT %d,%d", snapshot.axis[0], snapshot.axis[1], snapshot.axis[2], snapshot.axis[3]);
        ui_text(r, 18, 148, 1, M_VALUE, buf);
        {
            static const char *const names[] = { "A", "B", "X", "Y", "BK", "GD", "ST", "LS", "RS", "LB", "RB", "UP", "DN", "LT", "RT" };
            for (j = 0; j < 15; j++) {
                int x = 18 + j * 23, on = !!(snapshot.buttons & (1u << j));
                ui_fill(r, x, 161, 21, 10, on ? M_VALUE : UI_GREY);
                ui_text(r, x + 2, 162, 1, on ? M_INK : M_LABEL, names[j]);
            }
        }
        snprintf(buf, sizeof buf, "LT %d RT %d / %.16s", snapshot.axis[4], snapshot.axis[5], input_controller_model(dev));
        ui_text(r, 18, 175, 1, M_VALUE, buf);
        row(r, c, 18, 189, sw - 36, m->row == 5, blink, "BACK", NULL, 0);
        if (SDL_GetTicks() < m->notice_until) hint(r, sw, 207, m->notice);
        return;
    }
    if (m->page == PAGE_OPTIONS || m->page == PAGE_TOOLS) {
        static const char *const settings_items[] = { "GAME OPTIONS", "GRAPHICS", "AUDIO", "CONTROLS", "BACK" };
        static const char *const tool_items[] = { "MAP EDITOR", "REPLAYS & AWARDS", "BENCHMARK", "FIELD CHALLENGES", "CONTROLLER TEST", "BACK" };
        int tools = m->page == PAGE_TOOLS, count = rows_of(m->page);
        const char *const *items = tools ? tool_items : settings_items;
        menu_panel(r, px - 22, 48, pw + 44, 114);
        ui_text_bold(r, px, 58, 1, M_VALUE, tools ? "TOOLS" : "SETTINGS");
        divider(r, px - 6, 70, pw + 12);
        for (i = 0; i < count - 1; i++) row(r, c, px, 79 + i * 15, pw, m->row == i, blink, items[i], NULL, 0);
        menu_panel(r, px - 22, 167, pw + 44, 24);
        row(r, c, px, 175, pw, m->row == count - 1, blink, items[count - 1], NULL, 0);
        if (tools && m->row == TOOL_BENCHMARK)
            hint(r, sw, 203, m->benchmark[0] ? m->benchmark : "MEASURE SIMULATION AND DISPLAY SPEED");
        return;
    }
    if (m->page >= PAGE_GAME_OPTIONS && m->page <= PAGE_AUDIO) {
        int game_options = m->page == PAGE_GAME_OPTIONS;
        int graphics = m->page == PAGE_GRAPHICS;
        int back = rows_of(m->page) - 1;
        if (game_options) { px = 30; pw = sw - 165; }
        if (graphics) { px = 30; pw = sw - 196; }
        menu_panel(r, px - 22, 48, pw + 44, 114);
        ui_text_bold(r, px, 58, 1, M_VALUE, game_options ? "GAME OPTIONS" : graphics ? "GRAPHICS" : "AUDIO");
        divider(r, px - 6, 70, pw + 12);
        if (game_options) {
            int panel_x = sw - 105, panel_w = 99, left = panel_x + (panel_w - RADAR_SIZE) / 2;
            row(r, c, px, 77, pw, m->row == OPT_RADAR, blink, "RADAR", s->radar == 2 ? "TANK DOTS" : "MAP", 0);
            row(r, c, px, 93, pw, m->row == OPT_SIDES, blink, "SIDES", s->sides ? "BLUE/ORANGE + MARKS" : "BLUE/RED", 0);
            snprintf(buf, sizeof buf, "%d", s->match_lives);
            row(r, c, px, 109, pw, m->row == OPT_LIVES, blink, "TANKS / PLAYER", s->match_lives == 255 ? "UNLIMITED" : buf, 0);
            snprintf(buf, sizeof buf, "%d MIN", s->host_time);
            row(r, c, px, 125, pw, m->row == OPT_TIME, blink, "MATCH TIME", buf, 0);
            row(r, c, px, 141, pw, m->row == OPT_POWERUPS, blink, "POWER-UPS", s->powerups ? "ON (ENHANCED)" : "OFF", 0);
            menu_panel(r, panel_x, 48, panel_w, 114);
            fit_centre(r, panel_x, panel_w, 58, M_VALUE, "FIELD RADAR");
            ui_fill(r, left - 2, 77, RADAR_SIZE + 4, RADAR_SIZE + 4, M_LINE);
            if (e->preview && e->preview->map.w > 1 && e->preview->map.h > 1)
                render_draw_radar(r, e->preview, -1, left, 79, m->frame);
            fit_centre(r, panel_x, panel_w, 144, M_LABEL, s->radar == 2 ? "TANK DOTS" : "MAP");
        } else if (graphics) {
            const Style *style = e->num_styles ? &e->styles[style_index(s, e)] : NULL;
            static const char *const explosions[] = { "FULL", "SIMPLE", "OFF" };
            row(r, c, px, 76, pw, m->row == GRAPH_VIEW, blink, "VIEW", s->zoom ? "CLOSE-UP" : "WIDE", 0);
            row(r, c, px, 87, pw, m->row == GRAPH_MARKS, blink, "MARKS", s->red_marks ? "ALL" : "NO BLOOD", 0);
            row(r, c, px, 98, pw, m->row == GRAPH_EFFECTS, blink, "COMBAT FX", s->effects ? "FULL" : "CALM", 0);
            row(r, c, px, 109, pw, m->row == GRAPH_EXPLOSIONS, blink, "EXPLOSIONS", explosions[s->explosions], 0);
            row(r, c, px, 120, pw, m->row == GRAPH_FIRES, blink, "GROUND FIRES", s->ground_fires ? "ON" : "OFF", 0);
            row(r, c, px, 131, pw, m->row == GRAPH_WEATHER, blink, "WEATHER EFFECTS", s->weather_effects ? "ON" : "OFF", 0);
            row(r, c, px, 142, pw, m->row == GRAPH_SCREEN, blink, "SCREEN", s->fullscreen ? "FULLSCREEN" : "WINDOW", 0);
            row(r, c, px, 153, pw, m->row == GRAPH_AUTO, blink, "AUTO EFFECTS", s->auto_effects ? "ON" : "OFF", 0);
            graphics_preview(r, t, style, m, s, sw - 135, 127);
        } else {
            row(r, c, px, 79, pw, m->row == AUDIO_MUSIC, blink, "MUSIC VOLUME", NULL, 0);
            row(r, c, px, 95, pw, m->row == AUDIO_EFFECTS, blink, "EFFECT VOLUME", NULL, 0);
            ui_volume_meter(r, px + pw - 84, 78, 84, s->music, M_VALUE);
            ui_volume_meter(r, px + pw - 84, 94, 84, s->volume, M_VALUE);
        }
        menu_panel(r, 62, 167, sw - 124, 24);
        row(r, c, 84, 175, sw - 168, m->row == back, blink, "BACK", NULL, 0);
        if (graphics) {
            static const char *const help[GRAPH_ROWS] = {
                [GRAPH_VIEW] = "CHANGES HOW MUCH OF THE FIELD YOU SEE",
                [GRAPH_MARKS] = "HIDE BLOOD WHILE KEEPING SCORCH MARKS",
                [GRAPH_EFFECTS] = "SHAKE, FLASHES AND NEARBY TANK ENGINES",
                [GRAPH_EXPLOSIONS] = "FULL PARTICLES, SMALL BURSTS OR NO BLASTS",
                [GRAPH_FIRES] = "LINGERING FIRE, SMOKE AND GLOW FROM RUINS",
                [GRAPH_WEATHER] = "RAIN, SNOW AND OTHER MAP WEATHER",
                [GRAPH_SCREEN] = "WINDOW OR EDGE-TO-EDGE FULLSCREEN",
                [GRAPH_AUTO] = "REDUCE DECORATION WHEN FRAMES ARE SLOW",
                [GRAPH_BACK] = "SAVED AS YOU GO"
            };
            hint(r, sw, 203, help[m->row]);
        } else if (game_options) hint(r, sw, 203, "SAVED AS YOU GO");
        return;
    }
    /* keys page */
    menu_panel(r, px - 46, 48, pw + 92, 150);
    ui_text_bold(r, px - 32, 56, 1, M_OURS, m->keypair ? "PLAYER 3" : "PLAYER 1");
    ui_text_bold(r, px + pw / 2 + 4, 56, 1, M_THEIRS, m->keypair ? "PLAYER 4" : "PLAYER 2");
    divider(r, px - 34, 66, pw + 68);
    for (i = 0; i < 2 * ACT_COUNT; i++) {
        int col = i / ACT_COUNT, p = m->keypair * 2 + col, a = i % ACT_COUNT, x = col ? px + pw / 2 + 4 : px - 32, w = pw / 2 - 2;
        char val[64];
        if (m->capture == i) snprintf(val, sizeof val, "%s", blink ? "PRESS A KEY" : "");
        else if (s->keys[p][a][1])
            snprintf(val, sizeof val, "%s/%s", key_name(s->keys[p][a][0]), key_name(s->keys[p][a][1]));
        else snprintf(val, sizeof val, "%s", key_name(s->keys[p][a][0]));
        no_arrows = 1;
        row(r, c, x, 70 + a * 10, w, m->row == i, blink, act_label[a], val, 0);
        no_arrows = 0;
    }
    divider(r, px - 34, 130, pw + 68);
    row(r, c, px - 32, 134, pw + 64, m->row == K_PAIR, blink, "KEYS FOR", m->keypair ? "PLAYERS 3 AND 4" : "PLAYERS 1 AND 2", 0);
    divider(r, px - 34, 146, pw + 68);
    row(r, c, px, 150, pw, m->row == K_DEFAULTS, blink, "DEFAULTS", NULL, 0);
    row(r, c, px, 161, pw, m->row == K_BACK, blink, "BACK", NULL, 0);
    hint(r, sw, 203, m->capture >= 0 ? "ESC: KEEP THE OLD KEY"
                   : m->row == K_PAIR ? "SETS 3 AND 4: EMPTY UNTIL YOU GIVE THEM KEYS" : "A PAD'S BUTTONS: START, YOUR PROFILE");
}

int menu_text_field(const Menu *m, Settings *s, char **buf, size_t *cap, const char **title, int *secret)
{
    *secret = 0;
    if (m->page == PAGE_DIRECT) {
        if (m->row == D_ADDRESS) { *buf = s->server; *cap = sizeof s->server; *title = "ADDRESS (HOST OR HOST:PORT)"; return 1; }
        if (m->row == D_PASSWORD) { *buf = s->server_password; *cap = sizeof s->server_password; *title = "PASSWORD (IF IT ASKS)"; *secret = 1; return 1; }
        return 0;
    }
    if (m->page != PAGE_NET) return 0;
    if (m->row == N_NAME && signin_count() == 0) { *buf = s->player_name; *cap = sizeof s->player_name; *title = "YOUR NAME"; return 1; }
    return 0;
}
