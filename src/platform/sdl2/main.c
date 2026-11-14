/* SPDX-License-Identifier: MIT
 * SDL2 frontend: window, screens, fixed-timestep loop, input sampling.
 *
 *   openfirepower [--seed N] [--style NAME] [--theme NAME] [--smoke]
 *                 [--shot file.bmp] [--shot-select file.bmp]
 *
 * --smoke runs the selection screen briefly, then 500 scripted game ticks,
 * and exits (CI / headless, use with SDL_VIDEODRIVER=dummy). --shot saves
 * the last game frame, --shot-select the last selection-screen frame. */
#include "platform/sdl2/pref_path.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <SDL.h>
#include "common/fx_budget.h"
#include <dirent.h>                         /* (MinGW has them too) */
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(d, mode) _mkdir(d)              /* (Windows: no permissions to give) */
#endif
#include "core/game.h"
#include "platform/sdl2/render.h"
#include "platform/sdl2/input.h"
#include "platform/sdl2/theme.h"
#include "platform/sdl2/select.h"
#include "platform/sdl2/settings.h"
#include "platform/sdl2/menu.h"
#include "platform/sdl2/maps.h"
#include "platform/sdl2/editor.h"
#include "platform/sdl2/original.h"
#include "core/match.h"
#include "common/replay.h"
#include "common/training.h"
#include "common/operation.h"
#include "common/server.h"
#include "common/client.h"
#include "common/sha256.h"
#include "common/http.h"
#include "common/achieve.h"
#include "common/rules.h"
#include "common/dump.h"
#include "common/crash.h"
#include "common/daily.h"
#include <signal.h>
#include "platform/sdl2/netui.h"
#include "platform/sdl2/ui.h"
#include "platform/sdl2/fx.h"
#include "platform/sdl2/audio.h"
#include "platform/sdl2/music.h"
#include "platform/sdl2/osk.h"
#include "platform/sdl2/gallery.h"
#include "platform/sdl2/profiles.h"
#include "platform/sdl2/teams.h"
#include "common/botnames.h"
#include "platform/sdl2/select.h"

#define LOGICAL_W 384      /* 16:9 low-res, integer scaled */
#define LOGICAL_H 216
#define MAX_CATCHUP 5      /* ticks per frame before we drop time */

typedef enum { SCREEN_MENU, SCREEN_SELECT, SCREEN_PLAY, SCREEN_EDITOR, SCREEN_TITLE, SCREEN_LOBBY,
               SCREEN_BROWSER, SCREEN_REPLAYS, SCREEN_GALLERY, SCREEN_TEAMS } Screen;

/* ---- the title screen, with a battle going on behind it (attract mode) ---------- */
static Game demo;
static double demo_acc;
static int demo_follow;
#define ATTRACT_AFTER_MS 60000              /* the menu left alone this long: back to the title */

static void demo_start(int biome)
{
    uint32_t seed = (uint32_t)SDL_GetPerformanceCounter() * 2654435761u | 1u;
    int a, b;
    game_init(&demo, seed, (Biome)biome);
    demo.enhanced_rules = biome != BIOME_CLASSIC;
    demo.vehicle_world = (uint8_t)biome;
    game_prepare_surfaces(&demo, seed, 1);
    demo.map.time = (uint8_t)((seed >> 4) % 5 == 0 ? TIME_DUSK : (seed >> 4) % 5 == 1 ? TIME_NIGHT : TIME_DAY);
    a = game_add_tank_at_base(&demo, (TankClass)(seed % 3), TEAM_PLAYER);   /* two computer tanks */
    b = game_add_tank_at_base(&demo, (TankClass)((seed >> 8) % 3), TEAM_ENEMY);
    game_set_ai(&demo, a, AI_HARD);
    game_set_ai(&demo, b, AI_HARD);
    demo_acc = 0;
    demo_follow = 0;
    fx_reset();
}

static void demo_run(double dt, int biome)
{
    InputBits none[MAX_TANKS] = { 0 };
    int n = 0;
    demo_acc += dt;
    while (demo_acc >= 1.0 / TICK_RATE && n++ < 8) {
        game_tick(&demo, none);
        demo_acc -= 1.0 / TICK_RATE;
    }
    if (n >= 8) demo_acc = 0;
    if (demo.tick % (20 * TICK_RATE) == 0) demo_follow = 1 - demo_follow;   /* watch the other side */
    if (demo.winner >= 0 || demo.tick > 6 * 60 * TICK_RATE) demo_start(biome);  /* and start over */
    fx_update(&demo, (float)dt);
}

static Game game;          /* ~33 KB, keep it off the stack */

static InputBits smoke_script(uint32_t tick)
{
    InputBits in = IN_UP;
    if ((tick / 60) % 3 == 1) in |= IN_RIGHT;
    if ((tick / 35) % 2)      in |= IN_FIRE;
    return in;
}

static void set_fullscreen(SDL_Window *win, SDL_Renderer *ren, Settings *cfg, int fullscreen)
{
    int current = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0;
    if (current != fullscreen) {
        if (SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) == 0)
            current = fullscreen;
        ui_screen_target(ren);
    }
    cfg->fullscreen = current;
}

static void save_shot(SDL_Renderer *ren, const char *path)
{
    int w, h;
    SDL_Surface *surf;
    SDL_GetRendererOutputSize(ren, &w, &h);
    surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surf && SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch) == 0)
        SDL_SaveBMP(surf, path);
    SDL_FreeSurface(surf);
}

static Style styles[STYLE_MAX];
static int num_styles;

static int steer_mode;
static int ai_level = AI_NORMAL;            /* the enemy tank: off, easy, normal, hard (F7) */
static const char *ai_names[] = { "none", "easy", "normal", "hard" };

static int play_mode = MODE_SOLO;           /* MODE_*: what the game being played is */
static int cur_style;                       /* the world chosen (index into styles) */

/* a view's frame: the theme's, and inside it a line of the side's colour (the tank's own, as it is
 * on the minimap and the scores) */
static void side_frame(SDL_Renderer *r, int x, int y, int w, int h, int tank)
{
    ui_frame(r, x, y, w, h);
    if (tank >= 0 && tank < game.num_tanks) {
        Rgb c = render_side_rgb(game.tanks[tank].team);
        SDL_Rect q = { x + 1, y + 1, w - 2, h - 2 };
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
        SDL_RenderDrawRect(r, &q);
    }
}
static int random_time;                     /* TIME_* for random maps */
static char play_map[1024];                 /* map file to play on; "" = generate one */
static MapEntry maps[MAPS_MAX];
static int num_maps;
static Game preview;                        /* the map shown on the game-type screen */
static uint32_t preview_seed = 1;           /* the random battlefield you'd get, shown in advance */
static char preview_of[1200];               /* what the preview was made from */

/* keep the game-type screen's picture up to date with the chosen world and map */
/* a random battlefield's time of day and weather: its own, from its seed (a map file has its own) */
static void random_sky(uint32_t seed, int *tod, int *weather)
{
    *tod = (int)((seed >> 7) % TIME_COUNT);
    *weather = (seed >> 11) % 5 == 0 ? WEATHER_STORM : (seed >> 11) % 5 == 1 ? WEATHER_CHANGING : WEATHER_CLEAR;   /* (a storm now and then; or one coming) */
}

static int classic_rules(const Settings *cfg, const Style *style, const Game *map)
{
    return style->faithful || style->biome == BIOME_CLASSIC || cfg->local_type == TEAMS_CLASSIC ||
           (cfg->map[0] && map->map.custom_bases && map->map.mode == MAP_MODE_CLASSIC);
}

static void update_preview(const Settings *cfg, int style)
{
    char key[1200];
    int classic = styles[style].faithful || cfg->local_type == TEAMS_CLASSIC;
    snprintf(key, sizeof key, "%s|%d|%u|%d|%d", cfg->map, style, cfg->map[0] ? 0u : preview_seed, cfg->local_type, cfg->map_size);
    if (!strcmp(key, preview_of)) return;
    snprintf(preview_of, sizeof preview_of, "%s", key);
    if (cfg->map[0]) {
        char *text = maps_read(cfg->map);
        map_set_next_size(SIZE_LARGE);                   /* (a map file: its own size) */
        if (text && game_init_map(&preview, 1, text) == 0) {
            preview.enhanced_rules = !classic && preview.biome != BIOME_CLASSIC &&
                !(preview.map.custom_bases && preview.map.mode == MAP_MODE_CLASSIC);
            game_prepare_surfaces(&preview, preview_seed, 0);
            preview.weather = (uint8_t)match_weather(&preview, classic, WEATHER_CLEAR);
            free(text);
            return;
        }
        free(text);
    }
    map_set_next_size(cfg->map_size == SIZE_ANY ? SIZE_LARGE : cfg->map_size);                     /* (a random battlefield: the size chosen) */
    game_init(&preview, preview_seed, (Biome)styles[style].biome);
    map_set_next_size(SIZE_LARGE);
    if ((cfg->local_type == TEAMS_ANNIHILATION || cfg->local_type == TEAMS_TDM || cfg->local_type == TEAMS_WARLORD || cfg->local_type == TEAMS_KILLCONF) && !styles[style].faithful)
        game_make_arena(&preview);                       /* (deathmatches: an arena, as it'll be played) */
    preview.enhanced_rules = !classic && preview.biome != BIOME_CLASSIC;
    game_prepare_surfaces(&preview, preview_seed, 1);
    {   /* (the time and weather it'll be played in: the panel says so) */
        int tod = TIME_DAY, w = WEATHER_CLEAR;
        if (!styles[style].faithful) random_sky(preview_seed, &tod, &w);
        preview.map.time = (uint8_t)tod;
        preview.weather = (uint8_t)match_weather(&preview, classic, w);
    }
}
static int humans = 1;                      /* players at this machine: tanks 0 .. humans-1 */

/* network play (docs/NETWORK.md): this machine plays one tank of a match the
 * server referees; hosting runs that server right here, in the same loop */
static int local_time_limit;                /* --time-limit MINUTES for games here, in seconds */
static int net_on, hosting, me_tank;
static Osk osk;                             /* the on-screen keyboard (typing with a pad) */
static Gallery gallery;                     /* the web site's map gallery, in the game */
static int osk_for;                         /* what it types: OSK_FOR_* */
static int osk_hold;
static int pending_browser_join;            /* the pad's keyboard finished a server's password: join it */                        /* (after it closes: until A is let go, the menu doesn't see it) */
enum { OSK_FOR_SETTING, OSK_FOR_LOBBY, OSK_FOR_MATCH, OSK_FOR_SERVER_PW, OSK_FOR_TEAM_NAME };
static int noted_join;
static Teams teams;                         /* the local game's sides (the team screen) */
static int custom_teams;                    /* this local match comes from it */
static int desync_written;                  /* this match's out-of-step report is written */
static Uint32 menu_quiet_since;             /* from the title: the menu waits for every key and button to be let go */
static int from_title;                      /* the first profile window, from the title: backing out goes back there */

/* the menus are player 1's: an event from another player's pad or keys isn't for them
 * (nobody signed in: everyone's, as ever; player 1 on keys: those and the keyboard's own) */
static int menu_owner_ok(const SDL_Event *ev)
{
    int dev, p1;
    if (signin_count() == 0) return 1;
    p1 = signin_device(0);
    dev = input_device_of_event(ev);
    if (dev >= 0) return dev == p1;
    if (ev->type == SDL_KEYDOWN || ev->type == SDL_KEYUP || ev->type == SDL_TEXTINPUT) return p1 < DEV_PAD1;   /* (Enter, Esc...) */
    return ev->type != SDL_CONTROLLERBUTTONDOWN && ev->type != SDL_CONTROLLERBUTTONUP && ev->type != SDL_CONTROLLERAXISMOTION;
}

static int menu_with_sound(Menu *m, Settings *settings, const MenuEnv *env, InputBits nav, int key, int back, int ok)
{
    int row = m->row, page = m->page;
    int confirm = m->capture < 0 && !back && (ok || ((nav & ~m->prev) & IN_FIRE));
    int result = menu_update(m, settings, env, nav, key, back, ok);
    if (confirm || m->row != row || m->page != page) audio_menu_feedback(confirm);
    return result;
}

static InputBits menu_input(void)            /* what moves the menus: player 1's, if anyone's signed in */
{
    if (signin_count() == 0) return input_player1_menu();
    return input_device_menu(signin_device(0));
}

static int menu_quiet(void)                 /* 1 while the key that left the title is still down */
{
    const Uint8 *kb;
    int n, i;
    if (!menu_quiet_since) return 0;
    kb = SDL_GetKeyboardState(&n);
    for (i = 0; i < n; i++) if (kb[i]) return 1;
    if (input_player1() || SDL_GetMouseState(NULL, NULL)) return 1;
    if (SDL_GetTicks() - menu_quiet_since < 150) return 1;
    menu_quiet_since = 0;
    return 0;
}
static void crash_save_game(const char *path);   /* (below: it needs the recording) */
static Game live_ring[4];                   /* the live game at the last few checks (for an out-of-step report) */
static uint32_t live_tick[4];               /* the check each holds (+1: 0 = empty) */

static void keep_live(void)                 /* after a tick: if it was a check, a copy of the game as it is */
{
    if (game.tick % NET_CHECK_EVERY == 0) {
        int k = (int)(game.tick / NET_CHECK_EVERY) % 4;
        memcpy(&live_ring[k], &game, sizeof game);
        live_tick[k] = game.tick;          /* (= the check's tick + 1) */
    }
}                      /* this server went into "joined lately" */
static NetClient netx[3];                   /* players 2, 3 and 4 at this machine, in a network game */
static int net_extra;                       /* how many of them (0..3) */
static int me_tankx[3] = { -1, -1, -1 };    /* their tanks in the match (-1: none) */
static uint32_t net_nextx[3];               /* (each hears the same ticks: how far) */
#define netc2 netx[0]                       /* (the second, where two is all it is) */
#define net_two (net_extra > 0)
#define me_tank2 me_tankx[0]
static uint32_t net_next2;
static int spec_view;                       /* watching: whose tank the camera follows */
static int spec_free, spec_overview;        /* watching (a match or a replay): a free camera; the whole map */
static int spec_tally = 1;                  /* watching: every tank's kills and losses along the bottom (K) */
static int spec_director;                   /* watching: D, the camera picks where the action is */
static int spec_cx, spec_cy;                /* the free camera's middle, world pixels */

/* watching: F a free camera (from where you are), O the whole map; returns 1 if it was one of those */
static int spec_keys(SDL_Keycode k, int following)
{
    if (k == SDLK_f) {
        spec_free = !spec_free;
        if (spec_free && following >= 0 && following < game.num_tanks) {
            spec_cx = fx_to_int(game.tanks[following].x); spec_cy = fx_to_int(game.tanks[following].y);
        }
        return 1;
    }
    if (k == SDLK_o) { spec_overview = !spec_overview; return 1; }
    if (k == SDLK_k) { spec_tally = !spec_tally; return 1; }
    if (k == SDLK_d && !spec_free) { spec_director = !spec_director; spec_overview = 0; return 1; }   /* (free camera: D moves it) */
    return 0;
}

/* each frame, watching: W A S D (and a held left mouse button) move the free camera */
static void spec_move(void)
{
    const Uint8 *kb = SDL_GetKeyboardState(NULL);
    static int last_mx, last_my, dragging;
    int mx, my;
    Uint32 mb;
    if (!spec_free) return;
    if (kb[SDL_SCANCODE_W]) spec_cy -= 6;
    if (kb[SDL_SCANCODE_S]) spec_cy += 6;
    if (kb[SDL_SCANCODE_A]) spec_cx -= 6;
    if (kb[SDL_SCANCODE_D]) spec_cx += 6;
    mb = SDL_GetMouseState(&mx, &my);
    if (mb & SDL_BUTTON_LMASK) {                         /* (window pixels: about 3 to a world pixel) */
        if (dragging) { spec_cx -= (mx - last_mx) / 2; spec_cy -= (my - last_my) / 2; }
        dragging = 1;
    } else dragging = 0;
    last_mx = mx; last_my = my;
    if (spec_cx < 0) spec_cx = 0;
    if (spec_cy < 0) spec_cy = 0;
    if (spec_cx > game.map.w * TILE_SIZE) spec_cx = game.map.w * TILE_SIZE;   /* (the map's own edge) */
    if (spec_cy > game.map.h * TILE_SIZE) spec_cy = game.map.h * TILE_SIZE;
}

/* rumble: local player p's pad, when its tank is hit, fires, is destroyed (watched as the views are drawn) */
static int player_dev(int p)
{
    if (signin_count() > p) return signin_device(p);
    return p == 0 && input_pad_count() ? DEV_PAD1 : p > 0 && input_pad_count() > p ? DEV_PAD1 + p : -1;
}

static void rumble_watch(int tank, int p)
{
    static uint8_t prev_armor[MAX_TANKS];
    static uint16_t prev_respawn[MAX_TANKS], prev_shot[MAX_TANKS];
    static uint32_t prev_tick;
    const Tank *t;
    int dev = player_dev(p);
    if (tank < 0 || tank >= game.num_tanks) return;
    t = &game.tanks[tank];
    if (game.tick < prev_tick) memset(prev_armor, 0, sizeof prev_armor);   /* (a new match) */
    prev_tick = game.tick;
    if (dev >= 0 && t->active) {
        if (t->respawn && !prev_respawn[tank]) input_rumble_dev(dev, 100, 450);   /* destroyed */
        else if (!t->respawn && t->armor < prev_armor[tank] && prev_armor[tank])
            input_rumble_dev(dev, 25 + (prev_armor[tank] - t->armor) * 3, 140);   /* hit: the harder, the more */
        else if (t->reload > prev_shot[tank] && !t->respawn) input_rumble_dev(dev, 12, 50);   /* fired (its reload starts) */
    }
    prev_armor[tank] = t->armor;
    prev_respawn[tank] = t->respawn;
    prev_shot[tank] = t->reload;
}

static void draw_view(SDL_Renderer *ren, const Theme *th, const Style *st, const SDL_Rect *view, int tank, int alpha);

/* a watched view: the whole map, or the free camera's, or the tank's */
/* watching (a match from the server, or a replay): who destroyed whom, the last few for a few seconds, and
 * every tank's tally under the note at the top (K hides and shows it). From the game itself: a tank's losses going
 * up, and on the same tick the destroyer's kills. */
#define FEED_LINES 4
static Replay view_rep;                     /* the replay being watched */
static int replay_view;                     /* the play screen is showing a replay */
static struct { char text[48]; int team; uint32_t at; } feed[FEED_LINES];
static int feed_n;
static uint32_t feed_tick = (uint32_t)-1;
static uint16_t feed_lost[MAX_TANKS];
static uint8_t feed_kills[MAX_TANKS];
static void feed_name(char *out, size_t cap, int t)
{
    const char *nm = replay_view ? view_rep.names[t] : "";
    if (nm[0]) snprintf(out, cap, "%.10s", nm);
    else snprintf(out, cap, "P%d", t + 1);
    for (; *out; out++) if (*out >= 'a' && *out <= 'z') *out = (char)(*out - 32);
}
static void feed_update(void)
{
    int i, j, n = game.num_tanks < MAX_TANKS ? game.num_tanks : MAX_TANKS;
    if (game.tick == feed_tick) return;
    if (feed_tick == (uint32_t)-1 || game.tick < feed_tick) {   /* (a new match, or a replay wound back) */
        feed_n = 0;
        for (i = 0; i < n; i++) { feed_lost[i] = game.tanks[i].lost; feed_kills[i] = game.tanks[i].tank_kills; }
        feed_tick = game.tick;
        return;
    }
    feed_tick = game.tick;
    for (i = 0; i < n; i++) {
        if (game.tanks[i].lost > feed_lost[i]) {
            char victim[16], killer[16];
            int by = -1, k;
            for (j = 0; j < n; j++) if (j != i && game.tanks[j].tank_kills > feed_kills[j]) { by = j; break; }
            feed_name(victim, sizeof victim, i);
            if (by >= 0) feed_name(killer, sizeof killer, by);
            for (k = FEED_LINES - 1; k > 0; k--) feed[k] = feed[k - 1];
            if (by >= 0) snprintf(feed[0].text, sizeof feed[0].text, "%s DESTROYED %s", killer, victim);
            else snprintf(feed[0].text, sizeof feed[0].text, "%s IS DOWN", victim);
            feed[0].team = by >= 0 ? game.tanks[by].team & 1 : 2;
            feed[0].at = SDL_GetTicks();
            if (feed_n < FEED_LINES) feed_n++;
        }
    }
    for (i = 0; i < n; i++) { feed_lost[i] = game.tanks[i].lost; feed_kills[i] = game.tanks[i].tank_kills; }
}
static void draw_spectator_overlay(SDL_Renderer *ren, const SDL_Rect *view)
{
    static const Rgb side[3] = { { 240, 160, 80 }, { 130, 200, 100 }, { 200, 200, 200 } };
    uint32_t now = SDL_GetTicks();
    int i, y = view->y + 26, n = game.num_tanks < MAX_TANKS ? game.num_tanks : MAX_TANKS;
    feed_update();
    for (i = 0; i < feed_n; i++) {
        if (now - feed[i].at > 6000) continue;
        ui_fill(ren, view->x + view->w - 6 - ui_text_width(feed[i].text, 1), y - 1, ui_text_width(feed[i].text, 1) + 4, 9, (Rgb){ 0, 0, 0 });
        ui_text(ren, view->x + view->w - 4 - ui_text_width(feed[i].text, 1), y, 1, side[feed[i].team], feed[i].text);
        y += 10;
    }
    if (spec_tally && n > 0) {                       /* the tally: every tank, kills / losses */
        char buf[40];
        int x = view->x + 3, by = view->y + 13;    /* (under the note at the top: the bottom has the timeline) */
        for (i = 0; i < n; i++) {
            char nm[16];
            if (!game.tanks[i].active && !game.tanks[i].lost) continue;
            feed_name(nm, sizeof nm, i);
            snprintf(buf, sizeof buf, "%s %d/%d", nm, game.tanks[i].tank_kills, game.tanks[i].lost);
            if (x + ui_text_width(buf, 1) > view->x + view->w - 60) break;   /* (room for the round trip, right) */
            ui_fill(ren, x - 2, by - 1, ui_text_width(buf, 1) + 4, 9, (Rgb){ 0, 0, 0 });
            ui_text(ren, x, by, 1, side[game.tanks[i].team & 1], buf);
            x += ui_text_width(buf, 1) + 8;
        }
    }
}

/* the director (D while watching): which tank to show. Each tank scores what's happening around it (the
 * flag on it, enemies near, a kill just now, being hit); the camera moves to a clearly better one, but not
 * more often than every four seconds unless the one it shows is out of the match */
static int director_pick(int current)
{
    static uint32_t moved_at;
    static int16_t last_armor[MAX_TANKS];
    static uint8_t last_kills[MAX_TANKS];
    static uint32_t busy_until[MAX_TANKS];
    int i, j, best = current, n = game.num_tanks < MAX_TANKS ? game.num_tanks : MAX_TANKS, score[MAX_TANKS];
    uint32_t now = SDL_GetTicks();
    if (n <= 0) return 0;
    if (current < 0 || current >= n) current = 0;
    for (i = 0; i < n; i++) {
        const Tank *t = &game.tanks[i];
        if (game.tanks[i].tank_kills != last_kills[i] || (int)t->armor < last_armor[i]) busy_until[i] = now + 3000;
        last_kills[i] = game.tanks[i].tank_kills;
        last_armor[i] = (int16_t)t->armor;
        score[i] = -100000;
        if (!t->active) continue;
        score[i] = t->has_flag ? 1000 : 0;
        if ((int32_t)(busy_until[i] - now) > 0) score[i] += 300;
        for (j = 0; j < n; j++) {                        /* (enemies within 160 pixels) */
            const Tank *u = &game.tanks[j];
            int dx, dy;
            if (j == i || !u->active || (u->team & 1) == (t->team & 1)) continue;
            dx = fx_to_int(u->x) - fx_to_int(t->x); dy = fx_to_int(u->y) - fx_to_int(t->y);
            if (dx * dx + dy * dy < 160 * 160) score[i] += 120;
        }
    }
    for (i = 0; i < n; i++) if (score[i] > score[best]) best = i;
    if (best != current && (score[current] < -1000 || (score[best] > score[current] + 100 && now - moved_at > 4000))) {
        moved_at = now;
        return best;
    }
    return current;
}

static void draw_watched(SDL_Renderer *ren, const Theme *th, const Style *st, const SDL_Rect *view, int tank, int alpha, uint32_t frame)
{
    if (spec_overview) render_overview(ren, &game, view, frame);
    else {
        render_free_camera(spec_free, spec_cx, spec_cy);
        draw_view(ren, th, st, view, tank, alpha);
        render_free_camera(0, 0, 0);
    }
    draw_spectator_overlay(ren, view);
}
static int chat_typing;                     /* typing a chat line during a network match (T) */
static char chat_line[NET_CHAT];
static uint32_t chat_seen_at[40], chat_seen_seq[40];   /* when each chat line arrived (the in-match overlay) */
static char net_host[64], net_pass[64];     /* where we joined, for rejoining after a dropout */
static int join_port;
static uint32_t net_lost_at;                /* when the connection dropped mid-match (0 = it hasn't) */
static NetClient netc;
static Server host_srv;
static uint32_t net_next;
static double net_acc;
static double net_play;                     /* network play: time towards the next official tick, at a steady pace */
#define NET_BUFFER 2                        /* official ticks kept in hand against uneven arrival (40 ms) */
static Browser browser;                     /* FIND SERVERS */
static int browser_due;                     /* fetch the list on the next frame (after drawing "looking") */
static LobbyUI lobby_ui;
static int lobby_rename_side;
static char lobby_name[NET_TEAM_NAME];
static NetClient *lobby_pilot(void)
{
    int seat = lobby_ui.seat;
    return seat > 0 && seat <= net_extra && netx[seat - 1].state == NC_LOBBY ? &netx[seat - 1] : &netc;
}

static Replay rec;                          /* every match is recorded: last.ofprep beside the settings */
static int recording;
static void crash_save_game(const char *path) { if (recording) replay_save(&rec, path); }

static void replay_path(char *out, size_t cap, const char *name)
{
    const char *sp = settings_path(), *slash = strrchr(sp, '/');
#ifdef _WIN32
    const char *bs = strrchr(sp, '\\');
    if (bs && (!slash || bs > slash)) slash = bs;
#endif
    snprintf(out, cap, "%.*s%s", slash ? (int)(slash - sp + 1) : 0, sp, name);
}

static int keep_replays = 1;                /* test runs don't write over the player's replay */
static int smoke_replay;                    /* --smoke --replay: play the end, then stop */
static int smoke_pad;
static int smoke_operation;
static DesertOperation operation;
static int selected_challenge = -1;
static int smoke_training;                  /* --smoke-training: the first steps of TRAINING, scripted */                       /* --smoke-pad: a virtual game pad goes through the menus, lobby, chat */
static SDL_Joystick *test_pad;
static int forced_menu;
static Uint32 last_input;                   /* when the last input came (power save: idle menus wait for the next) */
static int play_here_x = -1, play_here_y = -1;  /* the editor's F10: the tank starts at this tile */                     /* (the pad test: a menu action, as if chosen) */

/* the pad test's script: press a button now and let it go next frame */
static void pad_press(int button)
{
    static int down = -1;
    if (!test_pad) return;
    if (down >= 0) SDL_JoystickSetVirtualButton(test_pad, down, 0);
    down = button;
    if (button >= 0) SDL_JoystickSetVirtualButton(test_pad, button, 1);
}

static void test_key(SDL_Keycode key, const char *text)
{
    SDL_Event ev = { 0 };
    if (key) {
        ev.type = SDL_KEYDOWN;
        ev.key.keysym.sym = key;
        ev.key.keysym.scancode = SDL_GetScancodeFromKey(key);
        SDL_PushEvent(&ev);
    }
    if (text) {
        ev.type = SDL_TEXTINPUT;
        snprintf(ev.text.text, sizeof ev.text.text, "%s", text);
        SDL_PushEvent(&ev);
    }
}

static void keep_in_replays(const Replay *r);

static void end_recording(void)
{
    char path[1200];
    const char *to = getenv("OFP_REPLAY_OUT");      /* (for tests: write it here, even in a test run) */
    if (!recording) return;
    if (to) snprintf(path, sizeof path, "%s", to);
    else replay_path(path, sizeof path, "last.ofprep");
    if (rec.ticks > 0 && (to || keep_replays)) replay_save(&rec, path);
    if (rec.ticks > 50 * TICK_RATE && keep_replays && !to) keep_in_replays(&rec);   /* (a real match: kept) */
    replay_free(&rec);
    recording = 0;
}

/* ---- training: a first mission, step by step ------------------------------------------------ */
static int training, train_step, train_mark, train_saved_ai = -1;
static fx train_x0, train_y0;

/* a step's three lines, for how you play: your pad's layout or the keys; tank or the original's steering */
static void train_lines(int step, char out[3][64])
{
    int pad = signin_count() > 0 ? signin_device(0) >= DEV_PAD1 : input_pad_count() > 0;
    int lay = signin_count() > 0 ? signin_pad_layout(0) : -2, direct = (signin_count() > 0 ? signin_steer(0) : steer_mode) == STEER_DIRECT;
    if (lay < 0) lay = PAD_TRIGGERS;
    out[0][0] = out[1][0] = out[2][0] = 0;
    switch (step) {
    case TR_DRIVE:
        snprintf(out[0], 64, "1 OF 6: DRIVE");
        snprintf(out[1], 64, "%s", direct ? (pad ? "PUSH THE STICK THE WAY YOU WANT TO GO." : "PUSH THE WAY YOU WANT TO GO.")
                               : pad && lay == PAD_TRIGGERS ? "RT TO GO, LT BACK, THE STICK TURNS."
                               : pad ? "THE STICK: UP TO GO, LEFT AND RIGHT TURN." : "UP TO GO, DOWN BACK, LEFT AND RIGHT TURN.");
        snprintf(out[2], 64, "DRIVE OUT OF THE GARAGE.");
        break;
    case TR_SHOOT:
        snprintf(out[0], 64, "2 OF 6: SHOOT");
        snprintf(out[1], 64, "%s", pad ? "FIRE: A (CROSS ON A PLAYSTATION PAD)." : "FIRE: SPACE.");
        snprintf(out[2], 64, "SHOOT SOMETHING TO BITS.");
        break;
    case TR_MINE:
        snprintf(out[0], 64, "3 OF 6: MINES");
        snprintf(out[1], 64, "%s", pad ? "LAY A MINE: X (SQUARE ON A PLAYSTATION PAD)." : "LAY A MINE: SHIFT OR E.");
        snprintf(out[2], 64, "IT WAITS THERE FOR AN ENEMY TANK.");
        break;
    case TR_TREES:
        snprintf(out[0], 64, "4 OF 6: TREES");
        snprintf(out[1], 64, "DRIVE UNDER THE TREES (THE ARROW):");
        snprintf(out[2], 64, "THE ENEMY CAN'T SEE YOU THERE.");
        break;
    case TR_RESCUE:
        snprintf(out[0], 64, "5 OF 6: RESCUE");
        snprintf(out[1], 64, "SHOOT OPEN A PRISON CAMP, PICK UP");
        snprintf(out[2], 64, "THE MEN, BRING THEM TO FIRST AID.");
        break;
    case TR_FLAG:
        snprintf(out[0], 64, "6 OF 6: THE FLAG");
        snprintf(out[1], 64, "THE ENEMY'S FLAG IS IN THEIR FORTRESS:");
        snprintf(out[2], 64, "TAKE IT AND BRING IT HOME.");
        break;
    default:
        snprintf(out[0], 64, "WELL DONE!");
        snprintf(out[1], 64, "YOU'RE READY FOR THE REAL THING.");
        snprintf(out[2], 64, "%s", pad ? "BACK (B): TO THE MENU." : "BACK (ESC): TO THE MENU.");
        break;
    }
}

/* the nearest trees to the tank (0: the map has none) */

/* the training ground: a small map, your camp and their fortress, nothing that fights back (no guns,
 * no helicopters, no barracks), a few buildings to shoot, a grove beside your camp, a prison camp on
 * the way; the same every time */
static char train_map_text[MAPFILE_MAX];
static const char *train_map;                       /* (set: start_game's map, once) */
static char *text_copy(const char *t)               /* (a copy the caller frees: C99 has no strdup) */
{
    size_t n = strlen(t) + 1;
    char *c = (char *)malloc(n);
    if (c) memcpy(c, t, n);
    return c;
}

static void build_training_ground(void)               /* (common/training.c's: shared with the Amiga) */
{
    snprintf(train_map_text, sizeof train_map_text, "%s", training_ground());
}

/* the match started: nothing that fights back (no guns, helipads, the enemy's barracks or its soldiers),
 * three of their plain buildings to shoot, one prison camp halfway between the bases */
static void train_clean_up(void) { training_clean_up(&game); }

/* where the arrow points, for this step (0 = nowhere) */
static int train_target(fx *tx, fx *ty)
{
    Training tr = { train_step, train_mark, train_x0, train_y0 };
    return training_target(&game, &tr, tx, ty);
}

/* each frame: has this step been done? */
static void train_check(void)
{
    Training tr = { train_step, train_mark, train_x0, train_y0 };
    training_check(&game, &tr);
    train_step = tr.step; train_mark = tr.mark;
}

static void draw_guide_arrow(SDL_Renderer *ren, const SDL_Rect *v, const Tank *t, fx tx, fx ty)
{
    double ang = atan2((double)(ty - t->y), (double)(tx - t->x));
    int cx = v->x + v->w / 2, cy = v->y + v->h / 2 + 11, r = 44, k, pass;
    int ax = cx + (int)(cos(ang) * r), ay = cy + (int)(sin(ang) * r);
    for (pass = 0; pass < 2; pass++) {               /* a dark edge, then the arrow */
        if (pass == 0) SDL_SetRenderDrawColor(ren, 30, 20, 0, 255); else SDL_SetRenderDrawColor(ren, 255, 214, 90, 255);
        for (k = 0; k < 12; k++) {                   /* the head: wider as it goes back */
            double w = k * 0.75 + (pass == 0 ? 1.0 : 0.0);
            int bx = ax - (int)(cos(ang) * k * 1.3), by = ay - (int)(sin(ang) * k * 1.3);
            int px = (int)(-sin(ang) * w), py = (int)(cos(ang) * w);
            SDL_RenderDrawLine(ren, bx - px, by - py, bx + px, by + py);
        }
        for (k = 12; k < 22; k++) {                  /* and a short tail */
            int bx = ax - (int)(cos(ang) * k * 1.3), by = ay - (int)(sin(ang) * k * 1.3);
            int px = (int)(-sin(ang) * (pass == 0 ? 3 : 2)), py = (int)(cos(ang) * (pass == 0 ? 3 : 2));
            SDL_RenderDrawLine(ren, bx - px, by - py, bx + px, by + py);
        }
    }
}

static void draw_training(SDL_Renderer *ren, const SDL_Rect *v)
{
    fx tx, ty;
    char tx2[3][64];
    train_lines(train_step > TR_DONE ? TR_DONE : train_step, tx2);
    ui_fill(ren, v->x, v->y, v->w, 31, (Rgb){ 14, 14, 10 });
    ui_text_bold(ren, v->x + 4, v->y + 3, 1, ui_colors()->value, tx2[0]);
    ui_text(ren, v->x + 4, v->y + 13, 1, UI_WHITE, tx2[1]);
    ui_text(ren, v->x + 4, v->y + 22, 1, UI_WHITE, tx2[2]);
    if (train_target(&tx, &ty)) draw_guide_arrow(ren, v, &game.tanks[0], tx, ty);
}

static void draw_operation(SDL_Renderer *ren, const SDL_Rect *v, int p)
{
    char counter[48];
    fx tx, ty;
    unsigned remaining = game.tick >= game.time_limit ? 0 : (unsigned)((game.time_limit - game.tick) / TICK_RATE);
    ui_fill(ren, v->x, v->y, v->w, 27, (Rgb){14, 14, 10});
    if (selected_challenge == CHALLENGE_BRIDGE)
        snprintf(counter, sizeof counter, "%s %u:%02u", operation_name(&operation), remaining / 60, remaining % 60);
    else snprintf(counter, sizeof counter, "%s %d/%d %u:%02u", operation_name(&operation),
             selected_challenge == CHALLENGE_RAID ? operation.destroyed : operation.saved,
             selected_challenge == CHALLENGE_RAID ? operation.gun_count : OP_RESCUE_GOAL, remaining / 60, remaining % 60);
    ui_text_bold(ren, v->x + 4, v->y + 3, 1, ui_colors()->value, counter);
    ui_text(ren, v->x + 4, v->y + 15, 1, UI_WHITE, operation_prompt(&operation));
    if (operation_target(&game, &operation, p, &tx, &ty)) draw_guide_arrow(ren, v, &game.tanks[p], tx, ty);
    else ui_text(ren, v->x + 4, v->y + v->h - 10, 1, UI_WHITE, "F1 / A RETRY   ESC / B BACK");
}

/* ---- the server browser's lists: favourites, joined lately, played with -------------------- */
static void list_front(char (*list)[72], int *n, int max, const char *item)   /* to the front, once */
{
    int i, j;
    for (i = 0; i < *n; i++) if (!strcmp(list[i], item)) break;
    if (i == *n) { if (*n < max) (*n)++; i = *n - 1; }
    for (j = i; j > 0; j--) memcpy(list[j], list[j - 1], 72);
    snprintf(list[0], 72, "%s", item);
}

static void note_friend(Settings *c, const char *name)
{
    int i, j;
    if (!name[0] || !strcmp(name, c->player_name)) return;
    for (i = 0; i < c->nfriends; i++) if (!SDL_strcasecmp(c->friends[i], name)) break;
    if (i == c->nfriends) { if (c->nfriends < 24) c->nfriends++; i = c->nfriends - 1; }
    for (j = i; j > 0; j--) memcpy(c->friends[j], c->friends[j - 1], 16);
    snprintf(c->friends[0], 16, "%s", name);
}

static void browser_lists(Browser *b, const Settings *c)
{
    int i;
    b->nfavs = c->nfavs; b->nrecent = c->nrecent; b->nfriends = c->nfriends;
    for (i = 0; i < c->nfavs; i++) snprintf(b->favs[i], 72, "%s", c->favs[i]);
    for (i = 0; i < c->nrecent; i++) snprintf(b->recent[i], 72, "%s", c->recent[i]);
    for (i = 0; i < c->nfriends; i++) snprintf(b->friends[i], 16, "%s", c->friends[i]);
}

/* ---- achievements at this machine --------------------------------------------------------- */
static AchieveTotals my_ach;
static uint32_t ach_new;                    /* earned in the match just over (for the report) */
static int ach_loaded;

static void ach_path(char *out, size_t cap) { replay_path(out, cap, "achievements.ini"); }

static void ach_load(void)
{
    char path[1100];
    FILE *f;
    unsigned e = 0, w = 0;
    ach_loaded = 1;
    ach_path(path, sizeof path);
    memset(&my_ach, 0, sizeof my_ach);
    if ((f = fopen(path, "r")) != NULL) {
        if (fscanf(f, "earned=%u matches=%d wins=%d saved=%d kills=%d worlds=%u", &e, &my_ach.matches, &my_ach.wins,
                   &my_ach.saved, &my_ach.kills, &w) >= 1) { my_ach.earned = e; my_ach.worlds_won = w; }
        fclose(f);
    }
}

static void ach_save(void)
{
    char path[1100];
    FILE *f;
    ach_path(path, sizeof path);
    if ((f = fopen(path, "w")) != NULL) {
        fprintf(f, "earned=%u matches=%d wins=%d saved=%d kills=%d worlds=%u\n", (unsigned)my_ach.earned, my_ach.matches,
                my_ach.wins, my_ach.saved, my_ach.kills, (unsigned)my_ach.worlds_won);
        fclose(f);
    }
}

/* the match is over: what did the player(s) here earn? */
/* a match over: each signed-in player's profile gets its stats (tanks[n]: player n's tank, -1: none) */
/* the KEYS page, signed in: whose controls it shows and changes (a key set: the profile on those keys; the
 * pad's rows: player 1's if on a pad, else the first on one) */
static int keys_owner(int set) { int n; for (n = 0; n < signin_count(); n++) if (signin_device(n) == DEV_KEYS1 + set) return n; return -1; }
static int pad_owner(void) { int n; for (n = 0; n < signin_count(); n++) if (signin_device(n) >= DEV_PAD1) return n; return -1; }

static void keys_page_from_profiles(Settings *c)   /* (while it's open: theirs; the keys only, a pad's in its profile window) */
{
    int set;
    for (set = 0; set < 4; set++) if (keys_owner(set) >= 0) signin_get_controls(keys_owner(set), NULL, NULL, NULL, c->keys[set]);
}

static void keys_page_to_profiles(const Settings *c, const Menu *m)   /* (a change: to its owner) */
{
    int set, po = pad_owner();
    if (m->row < 2 * ACT_COUNT) {                        /* a key: that set's profile */
        set = (m->keypair ? 2 : 0) + m->row / ACT_COUNT;
        if (keys_owner(set) >= 0) signin_set_keys(keys_owner(set), (const int (*)[2])c->keys[set]);
    }
    (void)po;                                            /* (a pad's settings: in its player's profile window) */
}

static void stats_match_over(const int *tanks, int n)
{
    int k, mode = game.extras & (EXTRA_CONQUEST | EXTRA_HARDPOINT) ? 1 : game.extras & EXTRA_ANNIHILATION ? 2
                : game.extras & (EXTRA_TDM | EXTRA_KILLCONF) ? 3 : game.extras & EXTRA_WARLORD ? 4 : 0;   /* (the profile's five) */
    for (k = 0; k < n && k < signin_count(); k++) {
        const Tank *t;
        int won;
        if (tanks[k] < 0 || tanks[k] >= game.num_tanks) continue;
        t = &game.tanks[tanks[k]];
        won = game.ffa_winner >= 0 ? game.ffa_winner == tanks[k] : game.winner >= 0 && game.winner == (t->team & 1);
        signin_record(k, won, t->tank_kills, t->lost, mode, t->cls);
    }
}

static void ach_match_over(int tank, int tank2)
{
    int k;
    if (!ach_loaded) ach_load();
    ach_new = 0;
    for (k = 0; k < 2; k++) {
        int t = k ? tank2 : tank;
        AchieveMatch m;
        const Tank *tk;
        if (t < 0 || t >= game.num_tanks) continue;
        tk = &game.tanks[t];
        m.won = game.winner >= 0 && game.winner == (tk->team & 1);
        m.lost = tk->lost; m.saved = tk->saved; m.tank_kills = tk->tank_kills; m.kills = tk->kills; m.helis = tk->helis_down;
        m.biome = game.biome; m.night = game.map.time == TIME_NIGHT; m.played_against_people = game.versus;
        ach_new |= achieve_update(&my_ach, &m);
    }
    ach_save();
}

/* ---- is there a newer version? (asked once, in the background) -------------------------- */
static char update_url[300], update_note[120];
static SDL_atomic_t update_done;

static int newer(const char *a, const char *b)       /* is version a newer than b ("0.3.0" > "0.2.9") */
{
    int x[3] = { 0, 0, 0 }, y[3] = { 0, 0, 0 }, i;
    sscanf(a, "%d.%d.%d", &x[0], &x[1], &x[2]);
    sscanf(b, "%d.%d.%d", &y[0], &y[1], &y[2]);
    for (i = 0; i < 3; i++) if (x[i] != y[i]) return x[i] > y[i];
    return 0;
}

static int update_thread(void *arg)
{
    char resp[400], ver[32] = "", url[300] = "";
    (void)arg;
    if (http_request(update_url, NULL, resp, sizeof resp, 4000) == 200 && sscanf(resp, "%31[^\t]\t%299s", ver, url) >= 1 &&
        newer(ver, OFP_VERSION)) {
        const char *u = url;
        if (!strncmp(u, "https://", 8)) u += 8; else if (!strncmp(u, "http://", 7)) u += 7;
        snprintf(update_note, sizeof update_note, "OPENFP %s IS OUT: %.60s", ver, u);
        for (char *q = update_note; *q; q++) if (*q >= 'a' && *q <= 'z') *q = (char)(*q - 32);
    }
    SDL_AtomicSet(&update_done, 1);
    return 0;
}

static void check_for_update(const Settings *c)
{
    SDL_Thread *t;
    if (!c->check_updates || !c->listing[0]) return;
    snprintf(update_url, sizeof update_url, "%.250s%saction=version", c->listing, strchr(c->listing, '?') ? "&" : "?");
    t = SDL_CreateThread(update_thread, "update check", NULL);
    if (t) SDL_DetachThread(t);
}

/* ---- watching replays ---------------------------------------------------------------- */
static uint32_t rv_tick;                    /* ticks played back so far */
static int rv_speed = 1, rv_tank, rv_style; /* speed (0 = paused), whose tank, which look */
static char rv_name[96];

#define RV_MAX 64
static struct { char path[1024], label[64], when[24]; long size; } rv_files[RV_MAX];
static int rv_count, rv_sel, rv_top, rv_awards;   /* rv_awards: the awards page instead of the list */
static char rv_notice[80];
enum { RV_LIST, RV_WATCH, RV_AWARDS, RV_BACK, RV_VISIBLE = 9 };
static int rv_focus;

static void rv_dir(char *out, size_t cap) { replay_path(out, cap, "replays"); }

/* every replay we can find: the last match, the ones kept, and downloads */
static void rv_scan(void)
{
    char dir[1100];
    rv_count = 0;
    {
        const char *home = getenv("HOME") ? getenv("HOME") : getenv("USERPROFILE");   /* (Windows: USERPROFILE) */
        char dl[1100];
        const char *dirs[3];
        int d;
        rv_dir(dir, sizeof dir);
        snprintf(dl, sizeof dl, "%s/Downloads", home ? home : ".");
        dirs[0] = dir; dirs[1] = dl; dirs[2] = NULL;
        for (d = 0; dirs[d]; d++) {
            DIR *dp = opendir(dirs[d]);
            struct dirent *e;
            if (!dp) continue;
            while ((e = readdir(dp)) != NULL && rv_count < RV_MAX) {
                size_t l = strlen(e->d_name);
                struct stat stt;
                if (l < 8 || strcmp(e->d_name + l - 7, ".ofprep")) continue;
                snprintf(rv_files[rv_count].path, sizeof rv_files[0].path, "%.700s/%.300s", dirs[d], e->d_name);
                if (stat(rv_files[rv_count].path, &stt) != 0) continue;
                {   /* a readable name: "SCIFI MATCH" for ours, "WEB 12 NEON GRID" for the site's */
                    char nm[64], *q;
                    unsigned yy, hh;
                    char world[24] = "";
                    snprintf(nm, sizeof nm, "%.*s", (int)(l - 7 > 60 ? 60 : l - 7), e->d_name);
                    if (sscanf(nm, "%8u-%6u-%23s", &yy, &hh, world) == 3)
                        snprintf(rv_files[rv_count].label, sizeof rv_files[0].label, "%s MATCH", world);
                    else if (!strncmp(nm, "openfirepower-", 14))
                        snprintf(rv_files[rv_count].label, sizeof rv_files[0].label, "WEB %s", nm + 14);
                    else snprintf(rv_files[rv_count].label, sizeof rv_files[0].label, "%s", nm);
                    for (q = rv_files[rv_count].label; *q; q++) if (*q == '-' || *q == '_') *q = ' ';
                }
                strftime(rv_files[rv_count].when, sizeof rv_files[0].when, "%m-%d %H:%M", localtime(&stt.st_mtime));
                rv_files[rv_count].size = (long)stt.st_size;
                rv_count++;
            }
            closedir(dp);
        }
        {   /* newest first */
            int i, j;
            for (i = 1; i < rv_count; i++)
                for (j = i; j > 0 && strcmp(rv_files[j].when, rv_files[j - 1].when) > 0; j--) {
                    char tmp[sizeof rv_files[0]];
                    memcpy(tmp, &rv_files[j], sizeof tmp); memcpy(&rv_files[j], &rv_files[j - 1], sizeof tmp); memcpy(&rv_files[j - 1], tmp, sizeof tmp);
                }
        }
    }
    replay_path(dir, sizeof dir, "last.ofprep");          /* the last match, at the top */
    {
        FILE *f = fopen(dir, "rb");
        if (f && rv_count < RV_MAX) {
            memmove(&rv_files[1], &rv_files[0], sizeof rv_files[0] * (size_t)rv_count);
            snprintf(rv_files[0].path, sizeof rv_files[0].path, "%.1000s", dir);
            snprintf(rv_files[0].label, sizeof rv_files[0].label, "THE LAST MATCH");
            fseek(f, 0, SEEK_END);
            rv_files[0].size = ftell(f);
            rv_files[0].when[0] = 0;
            rv_count++;
        }
        if (f) fclose(f);
    }
    if (rv_sel >= rv_count) rv_sel = rv_count ? rv_count - 1 : 0;
}

/* FIND SERVERS, L: a server's last match with a replay, from the web site, into replays/ (as WEB ...);
 * 0 = ok, its path in out; else a word why in out */
static int fetch_server_replay(const char *listing, const BrowserEntry *e, char *out, size_t cap)
{
    char url[700], q[160] = "", where[100], dir[1100];
    char *json = malloc(65536), *rep = NULL;
    int id = 0, st, k;
    FILE *f;
    if (!json) { snprintf(out, cap, "NO MEMORY"); return -1; }
    snprintf(where, sizeof where, "%.70s:%d", e->host, e->port);
    http_form(q, sizeof q, "server", where);
    snprintf(url, sizeof url, "%.380s%saction=matches&limit=20&%s", listing, strchr(listing, '?') ? "&" : "?", q);
    st = http_request(url, NULL, json, 65536, 5000);
    if (st == 200) id = browser_replay_id(json);
    free(json);
    if (st != 200) { snprintf(out, cap, "THE WEB SITE DOESN'T ANSWER"); return -1; }
    if (!id) { snprintf(out, cap, "NO MATCH WITH A REPLAY THERE YET"); return -1; }
    if (!(rep = malloc(3200000))) { snprintf(out, cap, "NO MEMORY"); return -1; }
    snprintf(url, sizeof url, "%.380s%saction=replay&id=%d", listing, strchr(listing, '?') ? "&" : "?", id);
    st = http_request(url, NULL, rep, 3200000, 15000);
    if (st != 200 || (strncmp(rep, "OFPREPLAY 1", 11) && strncmp(rep, "OFPREPLAY 2", 11))) { free(rep); snprintf(out, cap, "THAT REPLAY DIDN'T COME"); return -1; }
    rv_dir(dir, sizeof dir);
    mkdir(dir, 0700);
    snprintf(where, sizeof where, "%.60s", e->host);
    for (k = 0; where[k]; k++) if (!((where[k] >= 'a' && where[k] <= 'z') || (where[k] >= '0' && where[k] <= '9'))) where[k] = '-';
    snprintf(out, cap, "%.1000s/openfirepower-%s-%d.ofprep", dir, where, id);
    if (!(f = fopen(out, "wb")) || fwrite(rep, 1, strlen(rep), f) != strlen(rep)) { if (f) fclose(f); free(rep); snprintf(out, cap, "COULDN'T SAVE IT"); return -1; }
    fclose(f);
    free(rep);
    return 0;
}

/* every match also kept in replays/, the 30 newest */
static void keep_in_replays(const Replay *r)
{
    char dir[1100], path[1300], stamp[32];
    time_t now = time(NULL);
    rv_dir(dir, sizeof dir);
    mkdir(dir, 0700);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
    snprintf(path, sizeof path, "%.1100s/%s-%s.ofprep", dir, stamp, biome_name((Biome)r->setup.biome));
    replay_save(r, path);
    {
        DIR *dp = opendir(dir);
        struct dirent *e;
        char names[200][64];
        int n = 0, i, j;
        if (!dp) return;
        while ((e = readdir(dp)) != NULL && n < 200)
            if (strlen(e->d_name) < 64 && strstr(e->d_name, ".ofprep")) snprintf(names[n++], 64, "%s", e->d_name);
        closedir(dp);
        for (i = 1; i < n; i++)                          /* oldest first (names start with the date) */
            for (j = i; j > 0 && strcmp(names[j], names[j - 1]) < 0; j--) {
                char t[64];
                memcpy(t, names[j], 64); memcpy(names[j], names[j - 1], 64); memcpy(names[j - 1], t, 64);
            }
        for (i = 0; i < n - 30; i++) { snprintf(path, sizeof path, "%.1100s/%.63s", dir, names[i]); remove(path); }
    }
}

static void rv_feed(uint32_t t, InputBits *in)
{
    int k;
    memset(in, 0, sizeof(InputBits) * MAX_TANKS);
    for (k = 0; k < view_rep.tanks && k < MAX_TANKS; k++) in[k] = view_rep.inputs[(size_t)t * (size_t)view_rep.tanks + (size_t)k];
}

/* open one to watch: 0 = ok */
static int rv_open(const char *path)
{
    int i;
    const char *base = strrchr(path, '/');
    replay_free(&view_rep);
    if (replay_load(&view_rep, path) != 0 || match_start_recorded(&game, &view_rep.setup, view_rep.map, view_rep.tanks) != 0) {
        snprintf(rv_notice, sizeof rv_notice, "THAT ISN'T A REPLAY THIS GAME CAN READ");
        replay_free(&view_rep);
        return -1;
    }
    rv_style = 0;                                        /* the look of its world (not the faithful one) */
    for (i = 0; i < num_styles; i++)
        if (styles[i].biome == (game.biome == BIOME_SCIFI ? BIOME_MOON : game.biome)) { rv_style = i; break; }
    snprintf(rv_name, sizeof rv_name, "%.60s", base ? base + 1 : path);
    for (i = 0; rv_name[i]; i++) if (rv_name[i] >= 'a' && rv_name[i] <= 'z') rv_name[i] = (char)(rv_name[i] - 32);
    rv_tick = 0;
    rv_speed = 1;
    rv_tank = 0;
    replay_view = 1;
    fx_reset();
    audio_reset();
    return 0;
}

/* to a given tick: from the start, quietly (there's no going backwards in a simulation) */
static void rv_seek(uint32_t target)
{
    InputBits in[MAX_TANKS];
    if (target > view_rep.ticks) target = view_rep.ticks;
    if (target < rv_tick) {
        match_start_recorded(&game, &view_rep.setup, view_rep.map, view_rep.tanks);
        rv_tick = 0;
    }
    for (; rv_tick < target; rv_tick++) { rv_feed(rv_tick, in); game_tick(&game, in); }
    fx_reset();
}

static void end_training(void)
{
    if (!training) return;
    training = 0;
    if (train_saved_ai >= 0) ai_level = train_saved_ai;
    train_saved_ai = -1;
}

static int rejoin_done;                     /* (left the match on purpose, or it ended: the main loop forgets it) */
static void forget_rejoin(void) { rejoin_done = 1; }

/* six letters and digits, no dot: a server's join code (tools/listing: ofp_join_code), not a host name */
static int is_join_code(const char *s)
{
    int i;
    for (i = 0; s[i]; i++)
        if (!((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9'))) return 0;
    return i == 6 && strpbrk(s, "0123456789") && strpbrk(s, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
}

/* hosting here: the game known to the listing by a join code (not on its list), and players let in through
 * our router (NAT traversal). The listing is asked from a thread, so the game never waits for it; the
 * thread and the game share only what's below, under the lock. */
static SDL_mutex *host_lock;
static SDL_Thread *host_thread;
static SDL_atomic_t host_run;
static struct { char listing[256], name[48], stun[80], code[8]; int port, players, max_players; NetAddr punch[8]; int npunch; } host_share;

static int host_announcer(void *unused)
{
    uint32_t announced = 0, polled = 0;
    (void)unused;
    while (SDL_AtomicGet(&host_run)) {
        uint32_t now = SDL_GetTicks();
        char body[512] = "", resp[1024], num[16], listing[256], stun[80];
        SDL_LockMutex(host_lock);
        snprintf(listing, sizeof listing, "%s", host_share.listing);
        snprintf(stun, sizeof stun, "%s", host_share.stun);
        SDL_UnlockMutex(host_lock);
        if (stun[0] && (!announced || now - announced > 30000)) {   /* (once we know how we're seen) */
            announced = now ? now : 1;
            SDL_LockMutex(host_lock);
            http_form(body, sizeof body, "action", "announce");
            snprintf(num, sizeof num, "%d", host_share.port); http_form(body, sizeof body, "port", num);
            http_form(body, sizeof body, "name", host_share.name);
            snprintf(num, sizeof num, "%d", host_share.players); http_form(body, sizeof body, "players", num);
            snprintf(num, sizeof num, "%d", host_share.max_players); http_form(body, sizeof body, "max_players", num);
            snprintf(num, sizeof num, "%u", (unsigned)net_build_id()); http_form(body, sizeof body, "build", num);
            http_form(body, sizeof body, "version", OFP_VERSION);
            http_form(body, sizeof body, "hidden", "1");
            http_form(body, sizeof body, "stun", stun);
            SDL_UnlockMutex(host_lock);
            if (http_request(listing, body, resp, sizeof resp, 5000) == 200 && strstr(resp, " code ")) {
                SDL_LockMutex(host_lock);
                sscanf(strstr(resp, " code ") + 6, "%7s", host_share.code);
                SDL_UnlockMutex(host_lock);
            }
        }
        if (stun[0] && (!polled || now - polled > 3000)) {   /* who wants in */
            char *line;
            polled = now ? now : 1;
            body[0] = 0;
            http_form(body, sizeof body, "action", "punches");
            SDL_LockMutex(host_lock);
            snprintf(num, sizeof num, "%d", host_share.port);
            SDL_UnlockMutex(host_lock);
            http_form(body, sizeof body, "port", num);
            if (http_request(listing, body, resp, sizeof resp, 3000) == 200)
                for (line = strtok(resp, "\n"); line; line = strtok(NULL, "\n")) {
                    char ip[64];
                    int pt = 0;
                    NetAddr to;
                    if (sscanf(line, "%63s %d", ip, &pt) != 2 || pt <= 0 || net_resolve(ip, (uint16_t)pt, &to) != 0) continue;
                    SDL_LockMutex(host_lock);
                    if (host_share.npunch < 8) host_share.punch[host_share.npunch++] = to;
                    SDL_UnlockMutex(host_lock);
                }
        }
        SDL_Delay(200);
    }
    return 0;
}

/* each frame while hosting: STUN now and then, the punches the thread found, the code for the lobby */
static void host_traversal(const char *listing, const char *name)
{
    static uint32_t asked;
    uint32_t now = SDL_GetTicks();
    int k;
    if (!listing[0] || getenv("OFP_NO_TRAVERSAL")) return;
    if (!host_lock && !(host_lock = SDL_CreateMutex())) return;
    if (!host_thread) {
        SDL_LockMutex(host_lock);
        memset(&host_share, 0, sizeof host_share);
        snprintf(host_share.listing, sizeof host_share.listing, "%s", listing);
        snprintf(host_share.name, sizeof host_share.name, "%.40s", name);
        host_share.port = NET_PORT;
        SDL_UnlockMutex(host_lock);
        SDL_AtomicSet(&host_run, 1);
        host_thread = SDL_CreateThread(host_announcer, "host announcer", NULL);
        asked = 0;
    }
    if (!asked || now - asked > 60000) {                 /* (the answer comes in with the game's packets) */
        char h[128];
        int pt = 3478;
        NetAddr a;
        asked = now ? now : 1;
        net_split(NET_STUN_DEFAULT, h, sizeof h, &pt);
        if (net_resolve(h, (uint16_t)pt, &a) == 0) server_stun(&host_srv, &a);
    }
    SDL_LockMutex(host_lock);
    if (host_srv.have_public) {
        char where[64], hh[64];
        int pt = 0;
        net_addr_str(&host_srv.public_addr, where, sizeof where);
        net_split(where, hh, sizeof hh, &pt);
        snprintf(host_share.stun, sizeof host_share.stun, "%s %d", hh, pt);
    }
    host_share.players = server_people(&host_srv);
    host_share.max_players = host_srv.cfg.max_players;
    for (k = 0; k < host_share.npunch; k++) { server_punch(&host_srv, &host_share.punch[k]); server_punch(&host_srv, &host_share.punch[k]); }
    host_share.npunch = 0;
    snprintf(lobby_ui.join_code, sizeof lobby_ui.join_code, "%s", host_share.code);
    SDL_UnlockMutex(host_lock);
}

static void host_traversal_stop(void)
{
    if (host_thread) {
        SDL_AtomicSet(&host_run, 0);
        SDL_WaitThread(host_thread, NULL);               /* (a request at most: a few seconds) */
        host_thread = NULL;
    }
    lobby_ui.join_code[0] = 0;
}

static void leave_net(void)
{
    forget_rejoin();                                     /* (left on purpose: nothing to join again) */
    net_lost_at = 0;
    noted_join = 0;
    desync_written = 0;
    {
        int k;
        for (k = 0; k < net_extra; k++) netc_close(&netx[k]);
        net_extra = 0;
        for (k = 0; k < 3; k++) me_tankx[k] = -1;
    }
    chat_typing = 0;
    if (osk.open && (osk_for == OSK_FOR_TEAM_NAME || osk_for == OSK_FOR_LOBBY)) osk.open = 0;
    if (net_on || netc.socket_open) netc_close(&netc);
    if (hosting) {
        char path[1200];
        host_traversal_stop();
        replay_path(path, sizeof path, "hosted.ofprep");
        if (host_srv.replay.ticks) replay_save(&host_srv.replay, path);   /* the referee's own record */
        server_close(&host_srv);
    }
    net_on = hosting = 0;
}

static void lobby_join_match(void)
{
    int i;
    if (netc.server_state != SRV_RUNNING || netc.places_free <= 0) return;
    netc_join(&netc, 1);
    for (i = 0; i < net_extra; i++)
        if (netx[i].places_free > 0) netc_join(&netx[i], 1);
}

static int lobby_activate(NetClient *pilot, int action)
{
    lobby_ui.typing = 0;
    switch (action) {
    case LB_CAMP: case LB_FORTRESS: {
        int side = action == LB_CAMP ? SIDE_CAMP : SIDE_FORTRESS;
        if (!netc_lobby_can_rename_side(pilot, side)) break;
        lobby_rename_side = side;
        lobby_name[0] = 0;
        osk_open(&osk, lobby_name, sizeof lobby_name, side == SIDE_CAMP ? "RENAME CAMP" : "RENAME FORTRESS", 0);
        osk_for = OSK_FOR_TEAM_NAME;
        break;
    }
    case LB_READY:
        if (netc_lobby_can_start(pilot)) netc_chat(pilot, "/start");
        else if (netc.server_state == SRV_RUNNING && netc.places_free > 0) lobby_join_match();
        else if (pilot->state == NC_LOBBY && !pilot->eliminated) netc_ready(pilot, !pilot->ready, pilot->want_cls);
        break;
    case LB_WATCH:
        if (netc.server_state == SRV_RUNNING) netc_watch(&netc, 1);
        break;
    case LB_STATUS: lobby_ui.diagnostics = 1; lobby_ui.diagnostic_button = 0; lobby_ui.diagnostic_saved[0] = 0; break;
    case LB_SCORES: lobby_ui.show_scores ^= 1; break;
    case LB_PAGE: {
        LobbyRoster roster;
        netc_lobby_roster(&netc, &roster);
        lobby_ui.page = (lobby_ui.page + 1) % roster.pages;
        break;
    }
    case LB_CHAT: lobby_ui.typing = pilot->state == NC_LOBBY; break;
    case LB_KEYBOARD:
        if (pilot->state != NC_LOBBY) break;
        lobby_ui.action = LB_KEYBOARD;
        osk_open(&osk, lobby_ui.line, sizeof lobby_ui.line, "CHAT", 0);
        osk_for = OSK_FOR_LOBBY;
        break;
    case LB_EXIT: return 1;
    }
    return 0;
}

static int lobby_keypad(int seat, const SDL_KeyboardEvent *ev)
{
    int dev = signin_count() > seat ? signin_device(seat) : seat ? DEV_KEYS2 : DEV_KEYS1;
    InputBits key = input_device_key(dev, ev->keysym.scancode);
    switch (ev->keysym.sym) {
    case SDLK_UP: return PAD_UP;
    case SDLK_DOWN: return PAD_DOWN;
    case SDLK_LEFT: return PAD_LEFT;
    case SDLK_RIGHT: return PAD_RIGHT;
    case SDLK_RETURN: case SDLK_KP_ENTER: return PAD_A;
    case SDLK_ESCAPE: return PAD_BACK;
    }
    if (key & IN_FIRE) return PAD_A;
    if (key & IN_MINE) return PAD_BACK;
    if (key & IN_UP) return PAD_UP;
    if (key & IN_DOWN) return PAD_DOWN;
    if (key & IN_LEFT) return PAD_LEFT;
    if (key & IN_RIGHT) return PAD_RIGHT;
    return PAD_NONE;
}

static void lobby_move(NetClient *player, int nav)
{
    int focus = lobby_ui.action;
    if (focus >= LB_CHAT && nav == PAD_UP) lobby_ui.action = LB_NONE;
    else if (focus && nav == PAD_DOWN) lobby_ui.action = LB_CHAT;
    else if (focus && (nav == PAD_LEFT || nav == PAD_RIGHT))
        lobby_ui.action = lobby_action_step(&netc, player, &lobby_ui, nav == PAD_LEFT ? -1 : 1);
    else if (nav == PAD_UP || nav == PAD_DOWN)
        netc_lobby_tank_step(player, nav == PAD_UP ? -1 : 1);
    else if (nav == PAD_LEFT || nav == PAD_RIGHT)
        netc_lobby_team_step(player, nav == PAD_LEFT ? -1 : 1);
}

static void append_input(char *line, size_t cap, const char *text)
{
    size_t n = strlen(line);
    for (; *text && n + 1 < cap; text++) if (*text >= 32 && *text < 127) line[n++] = *text;
    line[n] = 0;
}

static void lobby_send_chat(void)
{
    if (lobby_ui.line[0]) netc_chat(lobby_pilot(), lobby_ui.line);
    lobby_ui.line[0] = 0;
    lobby_ui.typing = 0;
}

static Screen return_to_lobby(void)
{
    int i;
    forget_rejoin();
    net_lost_at = 0;
    netc_leave_match(&netc);
    for (i = 0; i < net_extra; i++) netc_leave_match(&netx[i]);
    chat_typing = 0;
    chat_line[0] = 0;
    net_on = 0;
    lobby_ui.page = lobby_ui.show_scores = lobby_ui.action = lobby_ui.typing = 0;
    return SCREEN_LOBBY;
}

static MatchSetup last_setup;              /* how the game being played began */
static Settings *cfg_now;                   /* the settings (for the match set-up) */
static char *last_map_text;                 /* its map file, if any (kept for the replay) */

static void start_game(uint32_t seed, const int cls[4], int mode, int biome)
{
    MatchSetup ms;
    int p;
    char *text = train_map && train_map[0] ? text_copy(train_map) : play_map[0] ? maps_read(play_map) : NULL;   /* (the training ground: its own) */
    memset(&ms, 0, sizeof ms);
    ms.seed = seed;
    ms.biome = (uint8_t)biome;
    ms.time = (uint8_t)random_time;
    int classic = training || classic_rules(cfg_now, &styles[cur_style], &preview);
    if (!text && !classic) {                           /* a random battlefield: its own time and weather */
        int tod, w;
        random_sky(seed, &tod, &w);
        ms.time = (uint8_t)tod;
        random_time = tod;
        ms.weather = (uint8_t)weather_visual((Biome)biome, w);
    }
    ms.camp_garages = (uint8_t)(classic ? 3 : 4);
    ms.map_size = (uint8_t)(text ? 0 : (cfg_now->map_size == SIZE_ANY ? SIZE_LARGE : cfg_now->map_size) + 1);   /* (a random battlefield's size; a map file: its own) */
    ms.versus = (uint8_t)game_modes[mode].versus;
    ms.ai_level = (uint8_t)(ai_level == AI_ADAPTIVE ? AI_ADAPTIVE + (cfg_now->adapt_skill + 50) / 100 : ai_level);
    ms.lives = (uint8_t)cfg_now->match_lives;
    ms.no_powerups = !cfg_now->powerups;
    ms.time_limit = (uint16_t)(local_time_limit > 0 ? local_time_limit : cfg_now->host_time * 60);          /* --time-limit (seconds; 0 = none) */
    ms.extras = (uint8_t)(classic ? 0 : cfg_now->extras);
    ms.players = (uint8_t)game_modes[mode].players;
    for (p = 0; p < ms.players && p < MATCH_PLAYERS; p++) {
        ms.cls[p] = (uint8_t)tank_chassis(cls[p]);
        if (!classic && cls[p] == TANK_RHINO) ms.rocket_tanks |= (uint8_t)(1u << p);
        ms.team[p] = (uint8_t)mode_team(mode, p);
        ms.steer[p] = (uint8_t)(p < signin_count() ? signin_steer(p) : steer_mode);   /* (each player's own: their profile's) */
    }
    if (custom_teams) {                                  /* the team screen: its sides, tanks and computer players */
        ms.extras = 0;                                   /* (the team screen's match: its mode's own, no left-over extras) */
        ms.rocket_tanks = 0;
        teams_setup(&teams, &ms);
        for (p = 0; p < ms.players && p < MATCH_PLAYERS; p++) ms.steer[p] = (uint8_t)(p < signin_count() ? signin_steer(p) : steer_mode);
    }
    if (classic) ms.rocket_tanks = 0;
    for (p = 0; p < (custom_teams ? teams.humans : ms.players) && p < signin_count(); p++) {
        int pick = custom_teams ? teams.cls[p] : cls[p];
        if (!(classic && pick == TANK_HEAVY && signin_tank(p) == TANK_RHINO)) signin_set_tank(p, pick);
    }
    if (match_start(&game, &ms, text) != 0) {             /* a map file, or a new battlefield */
        fprintf(stderr, "map %s could not be read, making one instead\n", play_map);
        free(text);
        text = NULL;
        match_start(&game, &ms, NULL);
    }
    if (getenv("OFP_TEST_CANOPY") && game.num_tanks >= 2) {   /* (pictures: tank 1 under the trees, tank 2 hidden beside it) */
        int k, dx, dy, tx, ty;
        for (k = 0; k < 2; k++) {
            if (k == 1) { game.tanks[1].x = game.tanks[1].prev_x = game.tanks[0].x + FX(5 * TILE_SIZE);
                          game.tanks[1].y = game.tanks[1].prev_y = game.tanks[0].y; }
            tx = fx_to_int(game.tanks[k].x) >> TILE_SHIFT; ty = fx_to_int(game.tanks[k].y) >> TILE_SHIFT;
            for (dy = -1; dy <= 1; dy++) for (dx = -1; dx <= 1; dx++) map_set(&game.map, tx + dx, ty + dy, TILE_CANOPY);
        }
    }
    free(last_map_text);
    last_map_text = text;
    last_setup = ms;
    end_recording();
    replay_begin(&rec, &ms, text, game.num_tanks);
    if (cfg_now) snprintf(rec.names[0], sizeof rec.names[0], "%s", cfg_now->player_name);
    for (p = 0; p < signin_count() && p < ms.players && p < MATCH_PLAYERS; p++) snprintf(rec.names[p], sizeof rec.names[p], "%.15s", signin_name(p));
    recording = 1;
    play_mode = mode;
    humans = custom_teams ? teams.humans : ms.players;   /* (the computer's players aren't on the screen) */
    fx_reset();
    audio_reset();
}

static int start_desert_operation(void)
{
    MatchSetup crew = {0};
    int p;
    end_recording();
    crew.players = (uint8_t)(signin_count() >= 2 || smoke_operation ? 2 : 1);
    for (p = 0; p < crew.players && p < MATCH_PLAYERS; p++)
        crew.steer[p] = (uint8_t)(p < signin_count() ? signin_steer(p) : steer_mode);
    if ((selected_challenge < 0 ? operation_start(&game, &operation, &crew) :
         operation_challenge_start(&game, &operation, &crew, selected_challenge)) != 0) return -1;
    for (p = 0; p < num_styles; p++) if (styles[p].biome == game.biome) { cur_style = p; break; }
    custom_teams = 0;
    humans = crew.players;
    fx_reset();
    audio_reset();
    return 0;
}

/* ---- the daily challenge: today's battlefield, the same for everyone ---------------------- */
static int daily;                           /* playing it now */
static uint32_t daily_day;                  /* which day's */
static char daily_result[96];               /* what the web site said (for the report) */

static void start_daily(void)
{
    MatchSetup ms;
    int k;
    daily_day = daily_today();
    daily_setup(daily_day, &ms);
    ms.steer[0] = (uint8_t)(signin_count() > 0 ? signin_steer(0) : steer_mode);
    for (k = 0; k < num_styles; k++) if (styles[k].biome == (ms.biome == BIOME_SCIFI ? BIOME_MOON : ms.biome)) { cur_style = k; break; }
    match_start(&game, &ms, NULL);
    free(last_map_text);
    last_map_text = NULL;
    last_setup = ms;
    end_recording();
    replay_begin(&rec, &ms, NULL, game.num_tanks);
    if (cfg_now) snprintf(rec.names[0], sizeof rec.names[0], "%s", cfg_now->player_name);
    recording = 1;
    play_mode = MODE_SOLO;
    humans = 1;
    daily = 1;
    daily_result[0] = 0;
    fx_reset();
    audio_reset();
}

/* a win: the replay to the web site, which checks it and says where it puts you */
static void daily_send(void)
{
    char path[1100], dayb[16], url[500], resp[200];
    char *text, *body = NULL;
    size_t cap = 0;
    int st;
    if (!cfg_now || !cfg_now->listing[0]) return;
    replay_path(path, sizeof path, "daily.ofprep");
    if (replay_save(&rec, path) != 0 || !(text = maps_read(path))) { snprintf(daily_result, sizeof daily_result, "DAILY: COULDN'T KEEP THE REPLAY"); return; }
    snprintf(dayb, sizeof dayb, "%u", (unsigned)daily_day);
    http_form_grow(&body, &cap, "action", "daily_submit");
    http_form_grow(&body, &cap, "day", dayb);
    http_form_grow(&body, &cap, "name", cfg_now->player_name);
    http_form_grow(&body, &cap, "replay", text);
    snprintf(url, sizeof url, "%.490s", cfg_now->listing);
    st = body ? http_request(url, body, resp, sizeof resp, 10000) : -1;
    if (st == 200 && !strncmp(resp, "ok place ", 9)) {   /* "ok place 4 of 17" */
        int place = 0, of = 0;
        sscanf(resp + 9, "%d of %d", &place, &of);
        snprintf(daily_result, sizeof daily_result, "DAILY: PLACE %d OF %d%s", place, of,
                 strstr(resp, "being checked") ? " (BEING CHECKED)" : " TODAY");
    } else if (st == 200 && !strncmp(resp, "ok kept", 7))
        snprintf(daily_result, sizeof daily_result, "DAILY: YOUR EARLIER WIN WAS FASTER");
    else snprintf(daily_result, sizeof daily_result, st < 0 ? "DAILY: THE WEB SITE DOESN'T ANSWER" : "DAILY: THE WEB SITE DIDN'T TAKE IT");
    free(text);
    free(body);
}

static int team_out(int team);
static int game_finished(void);

/* the faithful Classic world plays plain: no tracks in the ground (lighting,
 * night and animated water are left out by the renderer for it) */
static void apply_world_rules(const Style *st)
{
    fx_set_tracks(!st->faithful);
}
static int zoom;                    /* 1 = the original's close-up framing (2x) */
static int no_save;                 /* test runs leave the player's settings alone */
static void save_cfg(const Settings *c) { if (!no_save) settings_save(c); }

/* the menu changed something: make it so now, and remember it */
static void apply_settings(SDL_Window *win, SDL_Renderer *ren, Settings *cfg, int *steer, SelectScreen *sel)
{
    int i;
    *steer = cfg->steer;
    zoom = cfg->zoom;
    fx_set_shake(cfg->effects);
    audio_set_near_engines(cfg->effects);
    fx_set_details(cfg->explosions, cfg->ground_fires);
    render_set_ground_fires(cfg->ground_fires);
    ai_level = cfg->ai_level;
    render_set_radar_zoom(cfg->radar);
    audio_set_volume(cfg->volume);
    music_volume(cfg->music);
    render_set_sides(cfg->sides);
    input_set_keys(cfg->keys);
    input_set_pad_buttons(-1, -1);                       /* (a pad nobody signed in with: the defaults; */
    input_set_pad_layout(PAD_TRIGGERS);                  /*  each profile has its own, in its window) */
    signin_assign_inputs();                              /* (and the signed-in profiles' own controls on top) */
    set_fullscreen(win, ren, cfg, cfg->fullscreen);
    for (i = 0; i < num_styles; i++)
        if (!strcmp(styles[i].name, cfg->style)) sel->style = i;
    sel->mode = cfg->mode;
    snprintf(play_map, sizeof play_map, "%s", cfg->map);
    random_time = cfg->night;
    save_cfg(cfg);
}

static void start_from_menu(const Settings *cfg, SelectScreen *sel, Screen *screen)
{
    int i;
    snprintf(play_map, sizeof play_map, "%s", cfg->map);
    random_time = cfg->night;
    select_reset(sel, sel->choice, sel->style, num_styles);
    sel->mode = cfg->mode;
    for (i = 0; i < signin_count() && i < 4; i++) sel->choices[i] = tank_chassis(signin_tank(i));
    signin_assign_inputs();                              /* each with their own keys or pad */
    for (i = 0; i < num_styles; i++)
        if (!strcmp(styles[i].name, cfg->style)) sel->style = i;
    *screen = SCREEN_SELECT;
    custom_teams = 0;
    if (!no_save) {                                      /* the team screen: sides, tanks, ready (test runs: as before) */
        int devs[4], avs[4], cl[4], n = signin_count() > 0 ? signin_count() : 1;
        update_preview(cfg, sel->style);
        int faithful = classic_rules(cfg, &styles[sel->style], &preview);
        if (faithful && n > 2) n = 2;
        char names[4][16];
        for (i = 0; i < n && i < 4; i++) {
            devs[i] = signin_count() > 0 ? signin_device(i) : -1;
            avs[i] = signin_count() > 0 ? signin_avatar(i) : -1;
            cl[i] = signin_count() > 0 ? signin_tank(i) : sel->choices[i];
            snprintf(names[i], sizeof names[i], "%.15s", signin_count() > 0 ? signin_name(i) : cfg->player_name);
        }
        {
            int caps[2], room, cap0, cap1, mode = faithful ? TEAMS_CLASSIC : cfg->local_type;
            room = match_sides(&preview, mode == TEAMS_ANNIHILATION ? EXTRA_ANNIHILATION : 0, caps);   /* (team modes: even sides) */
            if (faithful) { caps[0] = caps[1] = 1; room = 2; }   /* (the Original: 1 vs 1) */
            cap0 = caps[0]; cap1 = caps[1];
            int nb[2] = { cfg->bots_side[0] == BOTS_FILL ? room - n : cfg->bots_side[0], 0 };   /* (FILL: up to what it holds) */
            if (faithful) nb[0] = n >= 2 ? 0 : 1;            /* (the Original: 1 vs 1) */
            if (nb[0] < 0) nb[0] = 0;
            if (nb[0] + n > room) nb[0] = room - n > 0 ? room - n : 0;
            if (mode != TEAMS_ANNIHILATION && (n + nb[0]) % 2 && nb[0] + n + 1 <= room) nb[0]++;   /* (even sides: one more) */
            else if (mode != TEAMS_ANNIHILATION && (n + nb[0]) % 2 && nb[0] > 0) nb[0]--;             /* (or one fewer) */
        teams_set_map(&preview);                             /* (the map, with where everyone starts) */
        teams_begin(&teams, faithful ? TEAMS_CLASSIC : cfg->local_type, n, devs, names, avs, cl, nb,
                    cfg->ai_level >= AI_EASY && cfg->ai_level <= AI_ADAPTIVE ? cfg->ai_level : AI_NORMAL,
                    cap0, cap1, room < MATCH_PLAYERS ? room : MATCH_PLAYERS);   /* (the Original: two, as in 1987; a map made
                                                                   for so many: those) */
        }
        if (getenv("OFP_TEST_HOST_BUTTONS")) { teams.host_buttons = 1; teams.ready[0] = 1; teams.host_sel = HB_START; }   /* (pictures) */
        if (getenv("OFP_TEST_STATS")) { teams.stat_cls = 2; teams.stat_until = SDL_GetTicks() + 60000; }   /* (pictures: a tank's stats) */
        *screen = SCREEN_TEAMS;
    }
}

/* Layout like the original: play field and riveted panel on a backdrop */
#define REPORT_DELAY 150      /* frames of watching the wreck before the report */

/* the report's buttons: PLAY AGAIN, NEW TEAMS (or TANKS), MENU; left/right chooses, FIRE (A) presses */
static int report_sel;
static void draw_report_buttons(SDL_Renderer *ren, int x, int y, int w)
{
    const char *lbl[3] = { "PLAY AGAIN", custom_teams ? "NEW TEAMS" : "NEW TANKS", "MENU" };
    char rm[32];
    int k, nb = 3, bw;
    if (net_on) {                                        /* online: REMATCH (the tally so far), LOBBY */
        snprintf(rm, sizeof rm, netc.my_rematch ? "REMATCH %d/%d *" : "REMATCH %d/%d", netc.rematch_yes, netc.rematch_of);
        lbl[0] = rm; lbl[1] = "LOBBY"; nb = 2;
        if (report_sel > 1) report_sel = 1;
    }
    bw = (w - 4 * (nb - 1)) / nb;
    for (k = 0; k < nb; k++) {
        int bx = x + k * (bw + 4), on = report_sel == k;
        ui_fill(ren, bx, y, bw, 13, on ? ui_colors()->value : (Rgb){ 30, 28, 22 });
        ui_frame(ren, bx, y, bw, 13);
        ui_text_bold(ren, bx + (bw - ui_text_width(lbl[k], 1)) / 2, y + 3, 1, on ? (Rgb){ 20, 18, 10 } : UI_WHITE, lbl[k]);
    }
}

/* the original's end-of-game sheet */
/* a match's new achievements, along the top of the report */
static void draw_new_awards(SDL_Renderer *ren, const SDL_Rect *v)
{
    if (daily && daily_result[0]) {                      /* the daily challenge: where it put you */
        ui_fill(ren, v->x, v->y + v->h - 24, v->w, 12, (Rgb){ 10, 18, 26 });
        ui_text_bold(ren, v->x + (v->w - ui_text_width(daily_result, 1)) / 2, v->y + v->h - 22, 1, (Rgb){ 140, 200, 255 }, daily_result);
    }
    char buf[96] = "";
    int a, n = 0;
    if (!ach_new || replay_view) return;
    for (a = 0; a < ACH_COUNT; a++)
        if ((ach_new >> a) & 1) {
            size_t l = strlen(buf);
            if (n < 2) snprintf(buf + l, sizeof buf - l, "%s%s", n ? ", " : "NEW: ", achieve_name(a));
            n++;
        }
    if (n > 2) { size_t l = strlen(buf); snprintf(buf + l, sizeof buf - l, " +%d", n - 2); }
    ui_fill(ren, v->x, v->y + v->h - 12, v->w, 12, (Rgb){ 20, 16, 10 });
    ui_text_bold(ren, v->x + (v->w - ui_text_width(buf, 1)) / 2, v->y + v->h - 10, 1, (Rgb){ 255, 214, 90 }, buf);
}

static void draw_report(SDL_Renderer *ren, const SDL_Rect *v, int tank)
{
    const Tank *t = &game.tanks[tank];
    const HudColors *c = ui_colors();
    Rgb paper = { 140, 86, 38 }, ink = c->value;
    char buf[48];
    int x = v->x + 10, y = v->y + 6, cx = v->x + v->w / 2;
    int won = game.winner == t->team;
#define LINE(str, dx) do { ui_text_bold(ren, x + (dx), y, 1, ink, (str)); y += 10; } while (0)
#define CENTRE(str)   do { ui_text_bold(ren, cx - (ui_text_width((str), 1) + 1) / 2, y, 1, ink, (str)); y += 10; } while (0)
    ui_fill(ren, v->x, v->y, v->w, v->h, paper);
    ui_frame(ren, v->x - 2, v->y - 2, v->w + 4, v->h + 4);
    if (game.humans_out && game.versus && won) { LINE("NO PLAYERS LEFT: YOUR", 0); LINE(" SIDE HAS THE MORE POINTS!", 0); }
    else if (game.humans_out && game.versus && game.winner >= 0) { LINE("NO PLAYERS LEFT: THE", 0); LINE(" OTHER SIDE HAS MORE POINTS", 0); }
    else if (game.humans_out && game.versus) { LINE("NO PLAYERS LEFT, LEVEL", 0); LINE(" ON POINTS: A DRAW", 0); }
    else if (game.humans_out)           { LINE("NO PLAYERS LEFT: ONLY", 0); LINE(" THE COMPUTER IS STILL IN", 0); }
    else if (game.time_up && won)       { LINE("TIME IS UP: YOUR SIDE", 0); LINE(" HAS THE MORE POINTS!", 0); }
    else if (game.time_up && game.winner >= 0) { LINE("TIME IS UP: THE ENEMY", 0); LINE(" HAS THE MORE POINTS", 0); }
    else if (game.time_up)              { LINE("TIME IS UP: NEITHER", 0); LINE(" SIDE HAS WON. A DRAW", 0); }
    else if (won)                       { LINE("THE ENEMY FLAG IS IN", 0); LINE(" YOUR HANDS, COMMANDER!", 0); }
    else if (game.winner >= 0)          { LINE("THE ENEMY HAS TAKEN", 0);  LINE(" OUR FLAG, COMMANDER", 0); }
    else                                { LINE("ALL YOUR TANKS HAVE", 0);  LINE(" BEEN DESTROYED IN BATTLE", 0); }
    y += 6;
    CENTRE(won ? "MISSION COMPLETE" : game.winner >= 0 ? "MISSION FAILED" : game.time_up ? "A DRAW" : "GAME OVER");
    CENTRE("***** FINAL REPORT *****");
    y += 4;
    LINE("CASUALTIES:", 60);
    snprintf(buf, sizeof buf, " TANKS : %u", t->lost);      LINE(buf, 60);
    snprintf(buf, sizeof buf, " MEN   : %u", t->pows_lost); LINE(buf, 60);
    LINE("RESCUED:", 60);
    snprintf(buf, sizeof buf, " MEN   : %u", t->saved);     LINE(buf, 60);
    LINE("INFLICTED:", 60);
    snprintf(buf, sizeof buf, " TANKS : %u", t->tank_kills); LINE(buf, 60);
    snprintf(buf, sizeof buf, " HELI  : %u", t->helis_down); LINE(buf, 60);
    snprintf(buf, sizeof buf, " MEN   : %u", t->kills);     LINE(buf, 60);
    snprintf(buf, sizeof buf, " SCORE : %u", (unsigned)t->score); LINE(buf, 60);
    y += 6;
    draw_report_buttons(ren, v->x + 8, y, v->w - 16);   /* (left/right, FIRE; B: the menu) */
#undef LINE
#undef CENTRE
    draw_new_awards(ren, v);
}

/* two players: one sheet over the whole screen, a column each */
static void draw_report2(SDL_Renderer *ren)
{
    const HudColors *c = ui_colors();
    Rgb paper = { 140, 86, 38 }, ink = c->value;
    char buf[48];
    const char *head;
    int p, y;
    int w = game.winner;
    if (w < 0 && last_setup.versus)                          /* out of tanks: the other one wins */
        w = team_out(TEAM_PLAYER) ? TEAM_ENEMY : team_out(TEAM_ENEMY) ? TEAM_PLAYER : -1;
    if (game.ffa_winner >= 0) {                          /* every tank for itself: the one left */
        static char who[40];
        int fw = game.ffa_winner;
        if (custom_teams && fw < teams.humans) snprintf(who, sizeof who, "%.15s WINS", teams.name[fw]);
        else if (game.tanks[fw].ai) snprintf(who, sizeof who, "%.20s WINS", bot_name(custom_teams ? teams.name_seed : last_setup.seed, fw));
        else snprintf(who, sizeof who, "PLAYER %d WINS", fw + 1);
        for (char *q = who; *q; q++) if (*q >= 'a' && *q <= 'z') *q = (char)(*q - 32);
        head = who;
    } else
    head = last_setup.versus ? (w == TEAM_PLAYER ? (custom_teams ? "THE CAMP WINS" : "PLAYER 1 WINS")
                                : w == TEAM_ENEMY ? (custom_teams ? "THE FORTRESS WINS" : "PLAYER 2 WINS") : "DRAW")
         : w == TEAM_PLAYER ? "MISSION COMPLETE" : w == TEAM_ENEMY ? "MISSION FAILED" : "GAME OVER";
    ui_fill(ren, 6, 6, LOGICAL_W - 12, LOGICAL_H - 12, paper);
    ui_frame(ren, 4, 4, LOGICAL_W - 8, LOGICAL_H - 8);
    ui_text_shadow(ren, (LOGICAL_W - ui_text_width(head, 2) - 2) / 2, 12, 2, ink, head);
    ui_text_bold(ren, (LOGICAL_W - ui_text_width("***** FINAL REPORT *****", 1)) / 2, 34, 1, ink, "***** FINAL REPORT *****");
    for (p = 0; p < 2; p++) {
        const Tank *t = &game.tanks[p];
        int x = 40 + p * 180;
        y = 52;
#define LINE(str) do { ui_text_bold(ren, x, y, 1, ink, (str)); y += 10; } while (0)
        LINE(p ? "PLAYER 2" : "PLAYER 1");
        y += 4;
        LINE("CASUALTIES:");
        snprintf(buf, sizeof buf, " TANKS : %u", t->lost);       LINE(buf);
        snprintf(buf, sizeof buf, " MEN   : %u", t->pows_lost);  LINE(buf);
        LINE("RESCUED:");
        snprintf(buf, sizeof buf, " MEN   : %u", t->saved);      LINE(buf);
        LINE("INFLICTED:");
        snprintf(buf, sizeof buf, " TANKS : %u", t->tank_kills); LINE(buf);
        snprintf(buf, sizeof buf, " HELI  : %u", t->helis_down); LINE(buf);
        snprintf(buf, sizeof buf, " MEN   : %u", t->kills);      LINE(buf);
        snprintf(buf, sizeof buf, " SCORE : %u", (unsigned)t->score); LINE(buf);
#undef LINE
    }
    (void)ink;
    draw_report_buttons(ren, 40, 186, LOGICAL_W - 80);   /* what next */
    {
        SDL_Rect whole = { 6, 6, LOGICAL_W - 12, LOGICAL_H - 12 };
        draw_new_awards(ren, &whole);
    }
}

static int paused_now;

/* the pause menu: START (player 1's) or Escape in a match. A local match stands still behind it; a
 * network one goes on (player 1's tank waits). RESUME, the sound, the music, the view,
 * QUIT MATCH (to the menu). Changes are saved at once. */
enum { PM_RESUME, PM_VOLUME, PM_MUSIC, PM_VIEW, PM_ORDERS, PM_QUIT, PM_COUNT };
static int pmenu, pmenu_sel, orders_open, order_row = ORDER_ATTACK, order_seat;
static NetClient *order_client(void) { return order_seat ? &netx[order_seat - 1] : &netc; }
static int order_slot(void) { return net_on ? order_client()->slot : order_seat; }
static int orders_available(void) {
    int slot = order_slot();
    if (!game_orders_allowed(&game) || (unsigned)slot >= (unsigned)game.num_tanks) return 0;
    return net_on ? order_client()->can_order : game.team_leader[game.tanks[slot].team & 1] == slot && !game.tanks[slot].ai;
}
static void draw_orders(SDL_Renderer *ren) {
    int k; char title[64];
    ui_fill(ren, 62, 49, 260, 125, (Rgb){16,16,12}); ui_frame(ren, 62, 49, 260, 125);
    snprintf(title, sizeof title, "P%d TEAM ORDERS", order_seat + 1);
    ui_text_bold(ren, 74, 58, 1, ui_colors()->value, title);
    for (k = 0; k < ORDER_COUNT; k++) {
        if (order_row == k) ui_fill(ren, 70, 76 + k * 17, 244, 14, ui_colors()->value);
        ui_text(ren, 76, 79 + k * 17, 1, order_row == k ? (Rgb){20,18,10} : UI_WHITE, game_order_name(k));
    }
}
static void draw_pause_menu(SDL_Renderer *ren, const Settings *c, int zoomed, int online)
{
    const char *lbl[PM_COUNT] = { "RESUME", "EFFECT VOLUME", "MUSIC VOLUME", "VIEW", "TEAM ORDERS", online ? "BACK TO SERVER LOBBY" : "QUIT MATCH" };
    const char *view = zoomed ? "CLOSE-UP" : "NORMAL";
    int k, w = 220, h = 22 + PM_COUNT * 13 + 16, x = (LOGICAL_W - w) / 2, y = (LOGICAL_H - h) / 2;
    ui_fill(ren, x, y, w, h, (Rgb){ 16, 16, 12 });
    ui_frame(ren, x, y, w, h);
    ui_text_bold(ren, x + (w - ui_text_width(online ? "MENU (THE MATCH GOES ON)" : "PAUSED", 1)) / 2, y + 6, 1, ui_colors()->value,
                 online ? "MENU (THE MATCH GOES ON)" : "PAUSED");
    for (k = 0; k < PM_COUNT; k++) {
        int yy, on = k == pmenu_sel;
        if (k == PM_ORDERS && !game_orders_allowed(&game)) continue;
        yy = y + 20 + (k - (k > PM_ORDERS && !game_orders_allowed(&game))) * 13;
        if (on) ui_fill(ren, x + 4, yy - 2, w - 8, 12, ui_colors()->value);
        ui_text_bold(ren, x + 10, yy, 1, on ? (Rgb){ 20, 18, 10 } : UI_WHITE, lbl[k]);
        if (k == PM_MUSIC || k == PM_VOLUME) {
            int level = k == PM_MUSIC ? c->music : c->volume;
            ui_volume_meter(ren, x + w - 94, yy - 1, 84, level, ui_colors()->value);
        } else if (k == PM_ORDERS) {
            char b[32]; snprintf(b, sizeof b, "P%d %s", order_seat + 1, orders_available() ? ">" : "LOCKED");
            ui_text(ren, x + w - 10 - ui_text_width(b, 1), yy, 1, on ? (Rgb){20,18,10} : ui_colors()->value, b);
        } else if (k == PM_VIEW) {
            char b[16];
            snprintf(b, sizeof b, on ? "< %s >" : "%s", view);
            ui_text(ren, x + w - 10 - ui_text_width(b, 1), yy, 1, on ? (Rgb){ 20, 18, 10 } : ui_colors()->value, b);
        }
    }
    {
        const char *hint = input_pad_count() ? "A: OK   B: BACK" : "ENTER: OK   ESC: BACK";
        ui_text(ren, x + (w - ui_text_width(hint, 1)) / 2, y + h - 11, 1, UI_GREY, hint);
    }
}
static SDL_Texture *zoom_tex;

/* one player's window on the battlefield, zoomed or not */
static void draw_view(SDL_Renderer *ren, const Theme *th, const Style *st, const SDL_Rect *view, int tank, int alpha)
{
    if (zoom) {
        int w = 0, h = 0;
        if (zoom_tex) SDL_QueryTexture(zoom_tex, NULL, NULL, &w, &h);
        if (!zoom_tex || w != view->w / 2 || h != view->h / 2) {
            if (zoom_tex) SDL_DestroyTexture(zoom_tex);
            zoom_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
                                         view->w / 2, view->h / 2);
            if (zoom_tex) SDL_SetTextureScaleMode(zoom_tex, SDL_ScaleModeNearest);
            else zoom = 0;
        }
    }
    if (zoom) {
        /* half the world, every pixel doubled: about as much ground on screen
         * as the Amiga showed, with sprites the same size relative to it */
        SDL_Rect half = { 0, 0, view->w / 2, view->h / 2 };
        SDL_SetRenderTarget(ren, zoom_tex);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        render_view(ren, th, st, &game, &half, tank, alpha, cfg_now->weather_effects ? cfg_now->effects : -1);
        ui_screen_target(ren);                      /* (back to the screen, its scaling and all) */
        SDL_RenderCopy(ren, zoom_tex, NULL, view);
    } else {
        render_view(ren, th, st, &game, view, tank, alpha, cfg_now->weather_effects ? cfg_now->effects : -1);
    }
}

/* the words over a view: big news in the middle, events at the top */
static void draw_overlay(SDL_Renderer *ren, const Theme *theme, const SDL_Rect *view, int tank)
{
    static const char *events[] = { NULL, "ENEMY FLAG TAKEN! BRING IT HOME", "15 MEN SAVED: EXTRA TANK!",
                                     "OUT OF FUEL!", "TANK FULL: TO THE FIRST AID", NULL,
                                     "HELICOPTERS! KEEP MOVING", "MEN DELIVERED",
                                     "THE ENEMY HAS OUR FLAG! STOP HIM", "FLAG RECOVERED",
                                     "ENEMY TANK DESTROYED", "THE ENEMY HAS NO TANKS LEFT", NULL,
                                     "SUPPLY DROP! ON THE RADAR" };
    static const char *short_events[] = { NULL, "FLAG TAKEN! GO HOME", "EXTRA TANK!", "OUT OF FUEL!",
                                          "FULL: TO FIRST AID", NULL, "HELICOPTERS!", "MEN DELIVERED",
                                          "THEY HAVE OUR FLAG!", "FLAG RECOVERED", "ENEMY TANK DOWN",
                                          "NO ENEMY TANKS LEFT", NULL, "SUPPLY DROP!" };
    const Tank *t = &game.tanks[tank];
    if (game.extras & EXTRA_HILL) {                  /* the hill: each side's time on it, of the two minutes */
        char hb[40];
        Rgb a = render_side_rgb(TEAM_PLAYER), b = render_side_rgb(TEAM_ENEMY);
        int w = 60, x0 = view->x + (view->w - 2 * w - 6) / 2, y0 = view->y + (game.time_limit ? 14 : 4);
        int fa = (int)((long)game.hill[0] * w / HILL_WIN), fb = (int)((long)game.hill[1] * w / HILL_WIN);
        ui_fill(ren, x0 - 1, y0 - 1, 2 * w + 8, 7, (Rgb){ 10, 10, 8 });
        ui_fill(ren, x0, y0, fa, 5, a);
        ui_fill(ren, x0 + w + 6 + (w - fb), y0, fb, 5, b);
        snprintf(hb, sizeof hb, "%s", game.hill_holder == (t->team & 1) ? "YOU HOLD THE HILL" : game.hill_holder >= 0 ? "THE ENEMY HOLDS THE HILL" : "");
        if (hb[0]) ui_text_shadow(ren, view->x + (view->w - ui_text_width(hb, 1)) / 2, y0 + 8, 1,
                                  game.hill_holder == (t->team & 1) ? a : b, hb);
    }
    if (game.time_limit) {                           /* the clock, top middle: what's left */
        uint32_t left = game.tick < game.time_limit ? (game.time_limit - game.tick + TICK_RATE - 1) / TICK_RATE : 0;
        char clk[16];
        Rgb col = left <= 60 && (SDL_GetTicks() / 500) % 2 ? (Rgb){ 255, 120, 80 } : left <= 60 ? (Rgb){ 255, 200, 90 } : UI_WHITE;
        snprintf(clk, sizeof clk, "%u:%02u", (unsigned)(left / 60), (unsigned)(left % 60));
        ui_text_shadow(ren, view->x + (view->w - ui_text_width(clk, 1)) / 2, view->y + 3, 1, col, clk);
    }
    int cx = view->x + view->w / 2, cy = view->y + view->h / 2, narrow = view->w < 220;
    const char *big = game.winner == t->team ? "MISSION COMPLETE"
                    : game.winner >= 0 ? "MISSION FAILED" : t->out ? "GAME OVER"
                    : t->respawn ? "TANK LOST" : NULL;
    int scale = narrow && big && ui_text_width(big, 2) > view->w - 8 ? 1 : 2;
    if (paused_now) {
        ui_text_shadow(ren, cx - (ui_text_width("PAUSED", 2) + 2) / 2, cy - 8, 2, ui_colors()->value, "PAUSED");
        if (tank == 0)
            ui_text_shadow(ren, cx - (ui_text_width("P / START TO GO ON", 1) + 1) / 2, cy + 12, 1, UI_WHITE,
                           "P / START TO GO ON");
        return;
    }
    if (theme->skull && t->respawn && (t->out ||
        (RESPAWN_TICKS - t->respawn >= 28 && RESPAWN_TICKS - t->respawn < 118))) {
        int elapsed = RESPAWN_TICKS - t->respawn;
        int phase = t->out ? (int)(SDL_GetTicks() / 105) : elapsed / 6;
        SDL_Rect src = { (phase & 3) * 32, 0, 32, 32 };
        SDL_Rect dst = { cx - 32, cy - 40, 64, 64 };
        char lives[24];
        if (t->lives == MATCH_UNLIMITED_LIVES) strcpy(lives, "UNLIMITED TANKS");
        else snprintf(lives, sizeof lives, t->lives ? "%u TANK%s LEFT" : "GAME OVER",
                 (unsigned)t->lives, t->lives == 1 ? "" : "S");
        ui_fill(ren, cx - 78, cy - 44, 156, 83, (Rgb){ 12, 13, 12 });
        ui_frame(ren, cx - 78, cy - 44, 156, 83);
        SDL_RenderCopy(ren, theme->skull, &src, &dst);
        ui_text(ren, cx - ui_text_width(lives, 1) / 2, cy + 26, 1, ui_colors()->value, lives);
        if (!replay_view && (!net_on || netc.slot != 255)) audio_skull_laugh(tank, t->lost);
        return;
    }
    if (big && (SDL_GetTicks() % 2400) < 1900) {      /* steady, with a short break */
        ui_text_shadow(ren, cx - (ui_text_width(big, scale) + scale) / 2, cy - 16, scale, ui_colors()->value, big);
        if (game.winner < 0 && !t->out) {
            char buf[32];
            snprintf(buf, sizeof buf, "%u LEFT", t->lives);
            ui_text_shadow(ren, cx - (ui_text_width(buf, 1) + 1) / 2, cy + 6, 1, UI_WHITE, buf);
        }
    }
    if (t->msg_t && t->msg < sizeof events / sizeof events[0] && events[t->msg]) {
        const char *m = narrow ? short_events[t->msg] : events[t->msg];
        ui_text_shadow(ren, cx - (ui_text_width(m, 1) + 1) / 2, view->y + 8, 1, UI_WHITE, m);
    }
}

static void rv_keep_visible(void)
{
    if (rv_sel < rv_top) rv_top = rv_sel;
    if (rv_sel >= rv_top + RV_VISIBLE) rv_top = rv_sel - RV_VISIBLE + 1;
}

static void draw_replays(SDL_Renderer *ren)
{
    int i, y;
    char buf[96];
    const Rgb label = { 204, 196, 170 }, value = { 250, 196, 64 }, dim = { 104, 100, 86 };
    menu_background(ren, LOGICAL_W, LOGICAL_H);
    menu_panel(ren, 8, 48, LOGICAL_W - 16, 135);
    ui_text_bold(ren, 20, 57, 1, value, rv_awards ? "AWARDS" : "REPLAYS");
    ui_fill(ren, 18, 68, LOGICAL_W - 36, 1, (Rgb){ 54, 54, 46 });
    if (rv_awards) {
        snprintf(buf, sizeof buf, "%d OF %d   %d MATCHES   %d WON", achieve_count(my_ach.earned), ACH_COUNT,
                 my_ach.matches, my_ach.wins);
        ui_text(ren, LOGICAL_W - 20 - ui_text_width(buf, 1), 57, 1, dim, buf);
        for (i = 0, y = 75; i < ACH_COUNT; i++, y += 10) {
            int got = (my_ach.earned >> i) & 1;
            ui_fill(ren, 20, y, 6, 6, got ? value : (Rgb){ 60, 58, 50 });
            ui_text(ren, 32, y, 1, got ? value : dim, achieve_name(i));
            snprintf(buf, sizeof buf, "%s", achieve_text(i));
            for (char *q = buf; *q; q++) if (*q >= 'a' && *q <= 'z') *q = (char)(*q - 32);
            ui_text(ren, 132, y, 1, got ? label : dim, buf);
        }
    } else {
        ui_text(ren, LOGICAL_W - 20 - ui_text_width("WHEN          SIZE", 1), 57, 1, dim, "WHEN          SIZE");
        if (!rv_count) ui_text(ren, 20, 80, 1, label, "NO REPLAYS YET");
        for (i = rv_top, y = 75; i < rv_count && i < rv_top + RV_VISIBLE; i++, y += 11) {
            int selected = i == rv_sel && rv_focus == RV_LIST;
            Rgb ink = selected ? (Rgb){ 22, 16, 8 } : label;
            ui_fill(ren, 18, y - 2, LOGICAL_W - 36, 10, selected ? (Rgb){ 232, 176, 48 } : (Rgb){ 18, 20, 17 });
            snprintf(buf, sizeof buf, "%.22s", rv_files[i].label);
            for (char *q = buf; *q; q++) if (*q >= 'a' && *q <= 'z') *q = (char)(*q - 32);
            ui_text(ren, 20, y, 1, ink, buf);
            snprintf(buf, sizeof buf, "%s  %4ldK", rv_files[i].when, (rv_files[i].size + 1023) / 1024);
            ui_text(ren, LOGICAL_W - 20 - ui_text_width(buf, 1), y, 1, selected ? ink : dim, buf);
        }
        if (rv_notice[0]) ui_text(ren, 20, 173, 1, (Rgb){ 255, 120, 80 }, rv_notice);
    }
    menu_panel(ren, 8, 188, LOGICAL_W - 16, 23);
    for (i = RV_WATCH; i <= RV_BACK; i++) {
        int x = 18 + (i - RV_WATCH) * 120, selected = rv_focus == i;
        const char *text = i == RV_WATCH ? (rv_awards ? "REPLAYS" : "WATCH") : i == RV_AWARDS ? "AWARDS" : "BACK";
        ui_fill(ren, x, 194, 108, 11, selected ? (Rgb){ 232, 176, 48 } : (Rgb){ 18, 20, 17 });
        ui_text(ren, x + (108 - ui_text_width(text, 1)) / 2, 196, 1,
                selected ? (Rgb){ 22, 16, 8 } : i == RV_WATCH && !rv_awards && !rv_count ? dim : value, text);
    }
}

static void draw_play(SDL_Renderer *ren, const Theme *th, const Style *st, int alpha, uint32_t frame,
                      int report)
{
    if (replay_view) {                               /* a replay: one tank's view, and the controls */
        SDL_Rect view = { 8, 6, 274, 204 };
        char bar[96];
        uint32_t now_s = rv_tick / TICK_RATE, all_s = view_rep.ticks / TICK_RATE;
        if (game.num_tanks) rv_tank %= game.num_tanks;
        if (spec_director) rv_tank = director_pick(rv_tank);   /* (D: where the action is) */
        ui_backdrop(ren, 0, 0, LOGICAL_W, LOGICAL_H);
        side_frame(ren, view.x - 2, view.y - 2, view.w + 4, view.h + 4, rv_tank);
        draw_watched(ren, th, st, &view, rv_tank, alpha, frame);
        render_hud(ren, th, &game, rv_tank, 289, 4, frame);
        render_player_tag(ren, view.x + 3, view.y + view.h - 26, rv_tank, game.tanks[rv_tank].team);
        if (report) draw_report(ren, &view, rv_tank);
        else draw_overlay(ren, th, &view, rv_tank);
        ui_fill(ren, view.x, view.y + view.h - 11, view.w, 11, (Rgb){ 0, 0, 0 });
        {   /* the timeline: click or drag along it to go anywhere in the match */
            int tw = view_rep.ticks ? (int)((long)view.w * rv_tick / view_rep.ticks) : 0;
            ui_fill(ren, view.x, view.y + view.h - 16, view.w, 5, (Rgb){ 30, 28, 22 });
            ui_fill(ren, view.x, view.y + view.h - 16, tw, 5, ui_colors()->value);
            ui_fill(ren, view.x + tw - 1, view.y + view.h - 18, 3, 9, UI_WHITE);
        }
        snprintf(bar, sizeof bar, "%s %u:%02u/%u:%02u %s", rv_speed ? "PLAY" : "PAUSE", (unsigned)(now_s / 60),
                 (unsigned)(now_s % 60), (unsigned)(all_s / 60), (unsigned)(all_s % 60),
                 rv_speed == 1 ? "1X" : rv_speed == 2 ? "2X" : rv_speed == 4 ? "4X" : rv_speed == 8 ? "8X" : rv_speed == 16 ? "16X" : "");
        ui_text(ren, view.x + 3, view.y + view.h - 10, 1, ui_colors()->value, bar);
        if (rv_tank < MATCH_PLAYERS && view_rep.names[rv_tank][0]) {   /* whose tank: by name, if the replay knows */
            int q;
            snprintf(bar, sizeof bar, "%.14s", view_rep.names[rv_tank]);
            for (q = 0; bar[q]; q++) if (bar[q] >= 'a' && bar[q] <= 'z') bar[q] = (char)(bar[q] - 32);
        } else if (rv_tank >= view_rep.setup.players) snprintf(bar, sizeof bar, "COMPUTER");
        else snprintf(bar, sizeof bar, "P%d OF %d", rv_tank + 1, game.num_tanks);
        ui_text(ren, view.x + view.w - 3 - ui_text_width(bar, 1), view.y + view.h - 10, 1, UI_GREY, bar);
        if (rv_name[0]) ui_text_shadow(ren, view.x + 3, view.y + 3, 1, UI_GREY, rv_name);
        return;
    }
    if (net_on) {                                    /* a network game: my own tank, full screen (or two halves) */
        SDL_Rect view = { 8, 6, 274, 204 };
        char pbuf[48];
        int watching = netc.slot == 255, i;
        if (watching && spec_director) spec_view = director_pick(spec_view % (game.num_tanks ? game.num_tanks : 1));
        if (watching) me_tank = spec_view % (game.num_tanks ? game.num_tanks : 1);
        ui_backdrop(ren, 0, 0, LOGICAL_W, LOGICAL_H);
        if (net_extra >= 2 && !watching) {           /* three or four of us here: a quarter each, as local split screen */
            int p;
            ui_backdrop(ren, 0, 0, LOGICAL_W * 2, LOGICAL_H * 2);
            for (p = 0; p < 4; p++) {
                int qx = (p % 2) * LOGICAL_W, qy = (p / 2) * LOGICAL_H, t = p == 0 ? me_tank : p - 1 < 3 ? me_tankx[p - 1] : -1;
                SDL_Rect v = { qx + 8, qy + 6, 274, 204 };
                side_frame(ren, v.x - 2, v.y - 2, v.w + 4, v.h + 4, t);
                if (p > net_extra || t < 0) {               /* a free quarter (or one of us out): the whole battlefield */
                    render_overview(ren, &game, &v, frame);
                    continue;
                }
                draw_view(ren, th, st, &v, t, alpha);
                rumble_watch(t, p);
                render_hud(ren, th, &game, t, qx + 289, qy + 4, frame);
                if (report) draw_report(ren, &v, t);
                else draw_overlay(ren, th, &v, t);
            }
            return;
        }
        if (me_tank2 >= 0) {                         /* two of us here: side by side */
            int p;
            for (p = 0; p < 2; p++) {
                int t = p ? me_tank2 : me_tank;
                SDL_Rect v = { 4 + p * 192, 4, 184, 144 };
                side_frame(ren, v.x - 2, v.y - 2, v.w + 4, v.h + 4, t);
                draw_view(ren, th, st, &v, t, alpha);
                render_hud_small(ren, th, &game, t, 2 + p * 192, 150, frame, p ? "P2" : "P1");
                if (report) draw_report(ren, &v, t);
                else draw_overlay(ren, th, &v, t);
            }
            view.x = 4; view.y = 4; view.w = 376; view.h = 144;
        } else {
            side_frame(ren, view.x - 2, view.y - 2, view.w + 4, view.h + 4, me_tank);
            if (watching) draw_watched(ren, th, st, &view, me_tank, alpha, frame);
            else { draw_view(ren, th, st, &view, me_tank, alpha); if (!watching) rumble_watch(me_tank, 0); }
            render_hud(ren, th, &game, me_tank, 289, 4, frame);
            render_player_tag(ren, view.x + 3, view.y + view.h - 15, me_tank, game.tanks[me_tank].team);
            if (report) draw_report(ren, &view, me_tank);
            else draw_overlay(ren, th, &view, me_tank);
        }
        if (watching) {                              /* a spectator's note */
            int pd = input_pad_count() > 0;               /* (a pad's buttons when there's a pad) */
            if (spec_overview) snprintf(pbuf, sizeof pbuf, pd ? "THE WHOLE MAP   Y: BACK   B: LOBBY" : "THE WHOLE MAP   O: BACK   ESC: LOBBY");
            else if (spec_free) snprintf(pbuf, sizeof pbuf, pd ? "FREE: STICK  X: P%d  Y: MAP" : "FREE: W A S D  F: P%d  O: MAP", me_tank + 1);
            else snprintf(pbuf, sizeof pbuf, pd ? "P%d  RB: NEXT  X: FREE  Y: MAP  B: TALLY" : "P%d  TAB: NEXT  F: FREE  O: MAP  K: TALLY  D: DIRECTOR", me_tank + 1);
            ui_fill(ren, view.x, view.y, view.w, 10, (Rgb){ 0, 0, 0 });
            ui_text(ren, view.x + 3, view.y + 1, 1, ui_colors()->value, pbuf);
        }
        snprintf(pbuf, sizeof pbuf, "%d MS", netc.rtt);  /* the round trip, top right of the view */
        ui_text(ren, view.x + view.w - 3 - ui_text_width(pbuf, 1), view.y + (watching ? 12 : 2), 1,
                netc.rtt > 150 ? (Rgb){ 240, 100, 80 } : (Rgb){ 140, 220, 120 }, pbuf);
        {   /* the chat: the last lines for a few seconds, and the one being typed */
            int shown = 0;
            uint32_t q, now = SDL_GetTicks();
            for (q = netc.chat_seq; q > 0 && q + 4 > netc.chat_seq; q--) {
                const ClientChat *l = &netc.chat[q % 40];
                if (l->seq != q || !l->text[0]) continue;
                if (chat_seen_seq[q % 40] != q) { chat_seen_seq[q % 40] = q; chat_seen_at[q % 40] = now; }
                if (now - chat_seen_at[q % 40] > 10000 && !chat_typing) continue;
                if (l->from < 0) snprintf(pbuf, sizeof pbuf, "* %.40s", l->text);
                else snprintf(pbuf, sizeof pbuf, "%s: %.36s", l->name, l->text);
                int k, mention = netc_chat_mentions(&netc, l, netc.id);
                for (k = 0; !mention && k < net_extra; k++)
                    if (netx[k].state == NC_RUNNING) mention = netc_chat_mentions(&netc, l, netx[k].id);
                ui_text_shadow(ren, view.x + 4, view.y + view.h - 24 - shown * 9, 1,
                               mention ? ui_colors()->value : l->from < 0 ? UI_GREY : UI_WHITE, pbuf);
                shown++;
            }
            if (chat_typing) {
                snprintf(pbuf, sizeof pbuf, "SAY: %.36s_", chat_line);
                ui_fill(ren, view.x, view.y + view.h - 12, view.w, 11, (Rgb){ 0, 0, 0 });
                ui_text(ren, view.x + 4, view.y + view.h - 11, 1, ui_colors()->value, pbuf);
            }
        }
        (void)i;
        if (net_lost_at || netc_behind(&netc) > 20) {    /* rejoining, or catching up */
            const char *msg = net_lost_at ? "RECONNECTING..." : "CATCHING UP...";
            ui_fill(ren, view.x, view.y + view.h / 2 - 12, view.w, 24, (Rgb){ 0, 0, 0 });
            ui_text_shadow(ren, view.x + (view.w - ui_text_width(msg, 2)) / 2, view.y + view.h / 2 - 8, 2,
                           ui_colors()->value, msg);
        }
        return;
    }
    if (humans >= 3) {
        /* three or four players: the screen is twice as big each way and
         * every player gets a full one-player layout in a quarter of it */
        int p;
        ui_backdrop(ren, 0, 0, LOGICAL_W * 2, LOGICAL_H * 2);
        for (p = 0; p < 4; p++) {
            int qx = (p % 2) * LOGICAL_W, qy = (p / 2) * LOGICAL_H;
            SDL_Rect view = { qx + 8, qy + 6, 274, 204 };
            ui_frame(ren, view.x - 2, view.y - 2, view.w + 4, view.h + 4);
            if (p >= humans) {                         /* a free quarter: the whole battlefield */
                render_overview(ren, &game, &view, frame);
                ui_text_shadow(ren, qx + 289 + (88 - ui_text_width("MAP", 2) - 2) / 2, qy + 96, 2,
                               ui_colors()->value, "MAP");
                continue;
            }
            draw_view(ren, th, st, &view, p, alpha);
            rumble_watch(p, p);
            render_hud(ren, th, &game, p, qx + 289, qy + 4, frame);
            render_player_tag(ren, view.x + 3, view.y + view.h - 15, p, game.tanks[p].team);
            if (report) draw_report(ren, &view, p);
            else draw_overlay(ren, th, &view, p);
        }
        return;
    }
    ui_backdrop(ren, 0, 0, LOGICAL_W, LOGICAL_H);
    if (humans == 1) {
        SDL_Rect view = { 8, 6, 274, 204 };
        ui_frame(ren, view.x - 2, view.y - 2, view.w + 4, view.h + 4);
        draw_view(ren, th, st, &view, 0, alpha);
        if (!replay_view) rumble_watch(0, 0);
        render_hud(ren, th, &game, 0, 289, 4, frame);
        if (operation.phase) draw_operation(ren, &view, 0);
        else if (report) draw_report(ren, &view, 0);
        else draw_overlay(ren, th, &view, 0);
        if (training && !report) draw_training(ren, &view);
        return;
    }
    {   /* split screen: a window and a panel each, player 1 on the left */
        int p;
        for (p = 0; p < 2; p++) {
            SDL_Rect view = { 4 + p * 192, 4, 184, 144 };
            ui_frame(ren, view.x - 2, view.y - 2, view.w + 4, view.h + 4);
            draw_view(ren, th, st, &view, p, alpha);
            render_hud_small(ren, th, &game, p, 2 + p * 192, 150, frame, p ? "P2" : "P1");
            if (operation.phase) draw_operation(ren, &view, p);
            else if (!report) draw_overlay(ren, th, &view, p);
        }
        if (report && !operation.phase) draw_report2(ren);
    }
}

/* over when a flag got home, or the players are out of tanks (in versus,
 * either one; together, both) */
static int team_out(int team)              /* all of a side's players out of tanks? */
{
    int p, any = 0;
    for (p = 0; p < humans; p++)
        if (game.tanks[p].team == team) { if (!game.tanks[p].out) return 0; any = 1; }
    return any;
}

static int game_finished(void)
{
    if (game.winner >= 0 || game.time_up) return 1;
    return team_out(TEAM_PLAYER) || team_out(TEAM_ENEMY);
}

static int run_benchmark(SDL_Renderer *ren, const Theme *theme, char result[64])
{
    SDL_Rect view = { 0, 0, LOGICAL_W, LOGICAL_H };
    const Style *style = &styles[0];
    Uint64 start, sim_time, draw_time, frequency = SDL_GetPerformanceFrequency();
    int i;
    if (match_benchmark_start(&demo, MATCH_PLAYERS / 2) != 0) {
        snprintf(result, 64, "COULD NOT START BENCHMARK");
        return -1;
    }
    start = SDL_GetPerformanceCounter();
    match_benchmark_step(&demo, BENCH_TICKS);
    sim_time = SDL_GetPerformanceCounter() - start;
    if (match_benchmark_start(&demo, MATCH_PLAYERS / 2) != 0) {
        snprintf(result, 64, "COULD NOT START DISPLAY TEST");
        return -1;
    }
    fx_reset();
    for (i = 0; i < num_styles; i++) {
        if (styles[i].biome == BIOME_DESERT) {
            style = &styles[i];
            break;
        }
    }
    render_set_style_teams(style);
    ui_screen_target(ren);
    start = SDL_GetPerformanceCounter();
    for (i = 0; i < BENCH_FRAMES; i++) {
        match_benchmark_step(&demo, BENCH_TICKS_PER_FRAME);
        render_view(ren, theme, style, &demo, &view, 0, 256, -1);
        SDL_RenderPresent(ren);
    }
    draw_time = SDL_GetPerformanceCounter() - start;
    snprintf(result, 64, "%.0f TICKS/S   %.1f FPS", BENCH_TICKS * (double)frequency / (sim_time ? sim_time : 1),
             BENCH_FRAMES * (double)frequency / (draw_time ? draw_time : 1));
    return 0;
}

int main(int argc, char **argv)
{
    SDL_Window   *win;
    SDL_Renderer *ren;
    Theme theme;
    const char *theme_name = "default", *style_name = "classic", *shot = NULL, *shot_select = NULL;
    int mute = 0, steer = STEER_TANK, start_mode = MODE_SOLO, want_versus = 0, want_coop = 0, want_players = 0;
    int big_screen = 0;
    uint32_t seed = 1987, frame = 0;
    int smoke = 0, running = 1, i, frames = 0, over_frames = 0, fixed_seed = 0, paused = 0;
    int benchmark_cli = 0, exit_status = 0, text_input_before = -1;
    int last_armor[4] = { -1, -1, -1, -1 }, last_lives[4] = { -1, -1, -1, -1 };
    double frame_dt = 0.0;
    Screen screen = SCREEN_MENU;
    SelectScreen sel;
    Settings cfg;
    Menu menu;
    MenuEnv menv;
    int skip_menu = 0, net_cli = 0;
    char join_addr[64] = "", cli_password[64] = "", cli_name[16] = "";
    int two_here = 0;
    char cli_replay[1024] = "";
    int cli_list = 0, cli_sides = -1, cli_storm = 0, cli_adaptive = 0, cli_osk = 0, cli_extras = -1;
    Uint32 idle_since = 0;
    int menu_r = MENU_NOTHING, frame_drawn = 0;
    Editor ed;
    static Game editor_game;
    int editor_test_active = 0, editor_old_style = 0;
    int mouse_x = 0, mouse_y = 0, mouse_b = 0, wheel = 0, ed_key = 0;
    int ed_stick_wait = 0;
    Uint64 freq, last, title_timer;
    double acc = 0.0;
    const double tick_s = 1.0 / TICK_RATE;

    /* last time's settings first; the command line can override them */
    settings_defaults(&cfg);
    for (i = 1; i < argc && strncmp(argv[i], "--smoke", 7); i++) {}
    if (i == argc) settings_load(&cfg);                     /* test runs start from the defaults */
    input_set_rumble(cfg.rumble);
    cfg_now = &cfg;
    signin_init();                                       /* the profiles (nobody signed in yet) */
    if (getenv("OFP_TEST_SIGNIN")) signin_test(getenv("OFP_TEST_SIGNIN"));
    if (getenv("OFP_TEST_OWN") && signin_count() > 0) signin_open_own(signin_device(0), 0);   /* (pictures: your own window) */
    if (getenv("OFP_TEST_PICKER")) signin_open(DEV_KEYS1 + 1);   /* (pictures: the profile window, open) */
    {   /* a crash: a report beside the settings, and the match so far */
        char *pref = ofp_pref_path();
        crash_install("OpenFP", pref ? pref : "", crash_save_game);
        SDL_free(pref);
    }
    style_name = !strcmp(cfg.style, "classic") ? "desert" : cfg.style;
    steer = cfg.steer;
    zoom = cfg.zoom;
    ai_level = cfg.ai_level;
    start_mode = cfg.mode;
    input_set_keys(cfg.keys);
    input_set_pad_buttons(-1, -1);                       /* (a pad nobody signed in with: the defaults) */
    input_set_pad_layout(PAD_TRIGGERS);
    render_set_red_marks(cfg.red_marks);
    fx_set_shake(cfg.effects); audio_set_near_engines(cfg.effects);
    fx_set_details(cfg.explosions, cfg.ground_fires);
    render_set_ground_fires(cfg.ground_fires);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc)             { seed = (uint32_t)strtoul(argv[++i], NULL, 10); fixed_seed = 1; }
        else if (!strcmp(argv[i], "--theme") && i + 1 < argc)       theme_name = argv[++i];
        else if (!strcmp(argv[i], "--style") && i + 1 < argc)       style_name = argv[++i];
        else if (!strcmp(argv[i], "--smoke"))                       smoke = 1;
        else if (!strcmp(argv[i], "--benchmark"))                   benchmark_cli = 1;
        else if (!strcmp(argv[i], "--skip-menu"))                   skip_menu = 1;
        else if (!strcmp(argv[i], "--zoom"))                        zoom = 1;
        else if (!strcmp(argv[i], "--night"))                       cfg.night = TIME_NIGHT;   /* random maps */
        else if (!strcmp(argv[i], "--versus"))                      want_versus = 1;
        else if (!strcmp(argv[i], "--coop"))                        want_coop = 1;
        else if (!strcmp(argv[i], "--players") && i + 1 < argc)     want_players = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mute"))                        mute = 1;
        else if (!strcmp(argv[i], "--classic-controls"))            steer = STEER_DIRECT;
        else if (!strcmp(argv[i], "--enemy") && i + 1 < argc) {
            const char *v = argv[++i];
            ai_level = !strcmp(v, "none") || !strcmp(v, "off") ? AI_OFF : !strcmp(v, "easy") ? AI_EASY
                     : !strcmp(v, "hard") ? AI_HARD : AI_NORMAL;
        }
        else if (!strcmp(argv[i], "--smoke-report"))                smoke = 2;   /* ends on the report */
        else if (!strcmp(argv[i], "--smoke-menu"))                  smoke = 3;   /* through the menu first */
        else if (!strcmp(argv[i], "--smoke-editor"))                smoke = 4;   /* make a map, then play it */
        else if (!strcmp(argv[i], "--music-preview") && i + 1 < argc) {   /* the songs as WAV files, to listen to */
            int song, k;
            music_init();
            {
                char *pref = ofp_pref_path();
                music_load(theme_data_dir(), pref);
                SDL_free(pref);
            }
            music_volume(6);
            for (song = 0; song < MUSIC_SONGS; song++) {
                char path[1100];
                FILE *wf;
                int n = 22050 * 30;
                uint32_t v32;
                uint16_t v16;
                snprintf(path, sizeof path, "%.1000s/openfirepower-%s.wav", argv[i + 1], music_name(song));
                if (!(wf = fopen(path, "wb"))) { fprintf(stderr, "can't write %s\n", path); return 1; }
                music_play(song);
                for (k = 0; k < 22050 * 3; k++) music_sample();          /* (the fade from the one before) */
                fwrite("RIFF", 1, 4, wf); v32 = 36 + (uint32_t)n * 2; fwrite(&v32, 4, 1, wf); fwrite("WAVEfmt ", 1, 8, wf);
                v32 = 16; fwrite(&v32, 4, 1, wf); v16 = 1; fwrite(&v16, 2, 1, wf); fwrite(&v16, 2, 1, wf);
                v32 = 22050; fwrite(&v32, 4, 1, wf); v32 = 44100; fwrite(&v32, 4, 1, wf); v16 = 2; fwrite(&v16, 2, 1, wf);
                v16 = 16; fwrite(&v16, 2, 1, wf); fwrite("data", 1, 4, wf); v32 = (uint32_t)n * 2; fwrite(&v32, 4, 1, wf);
                for (k = 0; k < n; k++) {
                    int v = music_sample() * 3;                               /* (louder than in the game, alone) */
                    int16_t smp = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
                    fwrite(&smp, 2, 1, wf);
                }
                fclose(wf);
                printf("%s\n", path);
            }
            music_close();
            return 0;
        }
        else if (!strcmp(argv[i], "--dump") && i + 3 < argc) {   /* --dump REPLAY TICK OUT: the state there, as text */
            static Replay dr;
            static Game dg;
            FILE *df;
            if (replay_load(&dr, argv[i + 1]) != 0) { fprintf(stderr, "%s: not a replay\n", argv[i + 1]); return 1; }
            if (!(df = fopen(argv[i + 3], "w"))) { fprintf(stderr, "can't write %s\n", argv[i + 3]); return 1; }
            replay_dump_at(&dr, (uint32_t)strtoul(argv[i + 2], NULL, 10), &dg, df);
            fclose(df);
            printf("%s: the state at tick %s\n", argv[i + 3], argv[i + 2]);
            return 0;
        }
        else if (!strcmp(argv[i], "--version")) {
            printf("OpenFP %s, network protocol %d (oldest %d), rules %08x\n", OFP_VERSION, NET_PROTOCOL,
                   NET_PROTOCOL_OLDEST, (unsigned)net_rules());
            return 0;
        }
        else if (!strcmp(argv[i], "--smoke-operation")) smoke_operation = 1;
        else if (!strcmp(argv[i], "--smoke-training"))              smoke_training = 1;
        else if (!strcmp(argv[i], "--smoke-pad"))                   smoke_pad = 1;   /* a virtual pad, scripted */
        else if (!strcmp(argv[i], "--smoke-title"))                 smoke = 5;   /* watch the title battle */
        else if (!strcmp(argv[i], "--osk"))                         cli_osk = 1;   /* (tests: the on-screen keyboard) */
        else if (!strcmp(argv[i], "--extras") && i + 1 < argc)    cli_extras = atoi(argv[++i]);   /* 1 pickups, 2 hill, 3 both */
        else if (!strcmp(argv[i], "--storm"))                       cli_storm = 1;   /* random maps: a storm */
        else if (!strcmp(argv[i], "--adaptive"))                    cli_adaptive = 1;
        else if (!strcmp(argv[i], "--two"))                         two_here = 2;   /* two players here, online */
        else if (!strcmp(argv[i], "--here") && i + 1 < argc) {      /* 1..4 players here, online */
            two_here = atoi(argv[++i]);
            if (two_here < 1) two_here = 1;
            if (two_here > 4) two_here = 4;
        }
        else if (!strcmp(argv[i], "--replay") && i + 1 < argc)    snprintf(cli_replay, sizeof cli_replay, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--replays"))                      cli_list = i + 1 < argc && !strcmp(argv[i + 1], "awards") ? (i++, 2) : 1;
        else if (!strcmp(argv[i], "--sides") && i + 1 < argc)        cli_sides = !strcmp(argv[++i], "orange") ? 1 : 0;
        else if (!strcmp(argv[i], "--time-limit") && i + 1 < argc) {
            int minutes = atoi(argv[++i]);
            local_time_limit = (minutes < 1 ? 20 : minutes > 30 ? 30 : minutes) * 60;
        }
        else if (!strcmp(argv[i], "--password") && i + 1 < argc) snprintf(cli_password, sizeof cli_password, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) snprintf(cli_name, sizeof cli_name, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--join") && i + 1 < argc) {      /* join one: --join HOST[:PORT] */
            net_cli = MENU_JOIN;
            snprintf(join_addr, sizeof join_addr, "%s", argv[++i]);
        }
        else if (!strcmp(argv[i], "--map") && i + 1 < argc)         snprintf(cfg.map, sizeof cfg.map, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc)        shot = argv[++i];
        else if (!strcmp(argv[i], "--shot-select") && i + 1 < argc) shot_select = argv[++i];
    }

    if (((smoke && smoke != 3 && smoke != 4 && smoke != 5) || want_versus || want_coop || want_players > 1) && !net_cli)
        skip_menu = 1;
    no_save = smoke != 0 || benchmark_cli;
    if (cli_sides >= 0) { cfg.sides = cli_sides; render_set_sides(cli_sides); }
    if (cli_storm) cfg.weather = WEATHER_STORM;
    if (cli_extras >= 0) cfg.extras = cli_extras & EXTRA_ALL;   /* (tests: every mode's bit too) */
    if (cli_adaptive) cfg.ai_level = AI_ADAPTIVE;
    if (smoke && getenv("OFP_TEST_HOST"))                   /* (tests: HOST SETTINGS as "min,rule,start,watch,maps,bots") */
        sscanf(getenv("OFP_TEST_HOST"), "%d,%d,%d,%d,%d,%d", &cfg.host_time, &cfg.host_rule, &cfg.host_start,
               &cfg.host_watch, &cfg.host_maps, &cfg.host_bots);
    keep_replays = !no_save;
    if (want_versus || want_coop || want_players > 1) {      /* --players N with --versus / --coop */
        int n = want_players > 1 ? (want_players > 4 ? 4 : want_players) : 2, m;
        if (n > OFP_MAX_LOCAL_PLAYERS) {
            fprintf(stderr, "this build splits the screen for at most %d players\n", OFP_MAX_LOCAL_PLAYERS);
            n = OFP_MAX_LOCAL_PLAYERS;
        }
        for (m = 0; m < MODE_COUNT; m++)
            if (game_modes[m].players == n && game_modes[m].versus == !want_coop && mode_available(m)) start_mode = m;
    }
    input_hints();
#ifdef OFP_TVOS
    /* The game supplies a controller-driven keyboard. UIKit's keyboard steals focus. */
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");
    SDL_SetHint(SDL_HINT_APPLE_TV_CONTROLLER_UI_EVENTS, "0");
    if (smoke_pad) SDL_SetHint(SDL_HINT_JOYSTICK_MFI, "0");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    win = SDL_CreateWindow(OFP_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           LOGICAL_W * 3, LOGICAL_H * 3,
                           SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
#ifdef OFP_TVOS
                           | SDL_WINDOW_HIDDEN
#endif
                           );
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
#ifdef OFP_TVOS
    {
        extern void ofp_tvos_attach_window(SDL_Window *window);
        ofp_tvos_attach_window(win);
        SDL_ShowWindow(win);
    }
#endif

    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }
    ui_set_logical(ren, LOGICAL_W, LOGICAL_H, 1);

    memset(&theme, 0, sizeof theme);
    if (theme_load(ren, &theme, theme_name) != 0 && theme_load(ren, &theme, "default") != 0) {
        fprintf(stderr, "no usable graphics theme found in %s/themes (set OFP_DATA?)\n", theme_data_dir());
        return 1;
    }
    signin_set_theme(&theme);                            /* (the profile window's tank cards: the same theme) */
    ui_set_theme(&theme);
    render_set_radar_zoom(cfg.radar);
    if (cfg.fullscreen && !smoke) set_fullscreen(win, ren, &cfg, 1);
    if (render_init(ren) != 0 || ui_init(ren) != 0) {
        fprintf(stderr, "render init: %s\n", SDL_GetError());
        return 1;
    }
    num_styles = styles_load(ren, styles, STYLE_MAX);
    if (num_styles == 0) {
        fprintf(stderr, "no map styles found in %s/styles (run tools/gen_styles.py?)\n", theme_data_dir());
        return 1;
    }
    {
        int start_style = 0;
        if (!strcmp(style_name, "classic")) style_name = "retro";
        if (!strcmp(style_name, "remaster")) style_name = "desert";
        if (!strcmp(style_name, "scifi")) style_name = "moon";
        for (i = 0; i < num_styles; i++)
            if (!strcmp(styles[i].name, style_name)) start_style = i;
        {
            char db[1200];
            snprintf(db, sizeof db, "%s/gamecontrollerdb.txt", theme_data_dir());
            input_init(db);
            if (smoke_pad) {                             /* a virtual game controller, for the test */
                SDL_VirtualJoystickDesc d;
                int vi;
                SDL_zero(d);
                d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
                d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
                d.naxes = SDL_CONTROLLER_AXIS_MAX;
                d.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
                vi = SDL_JoystickAttachVirtualEx(&d);
                test_pad = vi >= 0 ? SDL_JoystickOpen(vi) : NULL;
                if (!test_pad) { fprintf(stderr, "no virtual pad: %s\n", SDL_GetError()); return 1; }
            }
        }
        select_reset(&sel, TANK_MEDIUM, start_style, num_styles);
        sel.mode = start_mode;
        for (i = 1; i < 4; i++) sel.choices[i] = (TANK_MEDIUM + i) % 3;
        if (audio_init(cfg.volume, cfg.music) != 0)
            fprintf(stderr, "no sound: %s\n", SDL_GetError());
        if (mute) audio_toggle_mute();
    }
    /* what the menu shows is what this run uses */
    cfg.steer = steer; cfg.zoom = zoom; cfg.ai_level = ai_level; cfg.mode = start_mode;
    snprintf(cfg.style, sizeof cfg.style, "%s", styles[sel.style].name);
    num_maps = maps_scan(maps, MAPS_MAX);
    if (cfg.map[0]) {                                       /* gone since? play a new one then */
        char *t = maps_read(cfg.map);
        if (!t) cfg.map[0] = 0;
        free(t);
    }
    snprintf(play_map, sizeof play_map, "%s", cfg.map);
    menv.styles = styles; menv.num_styles = num_styles;
    menv.maps = maps; menv.num_maps = num_maps;
    menv.preview = &preview;
    random_time = cfg.night;
    preview_seed = fixed_seed ? seed : ((uint32_t)SDL_GetPerformanceCounter() * 2654435761u) | 1u;
    menu_reset(&menu);
    screen = skip_menu ? SCREEN_SELECT : (smoke && smoke != 5) || net_cli ? SCREEN_MENU : SCREEN_TITLE;
    if (benchmark_cli) {
        exit_status = run_benchmark(ren, &theme, menu.benchmark) != 0;
        puts(menu.benchmark);
        running = 0;
    }
    if (!net_cli && !smoke && !no_save && cfg.rejoin[0] && cfg.rejoin_until > (long)time(NULL) &&
        cfg.rejoin_until - (long)time(NULL) <= 15 * 60) {   /* the game closed in a match: back into it */
        char note[120];
        snprintf(cfg.server, sizeof cfg.server, "%s", cfg.rejoin);
        snprintf(note, sizeof note, "BACK INTO YOUR MATCH ON %.40s", cfg.rejoin);
        screen = SCREEN_MENU;
        menu.page = PAGE_NET;
        forced_menu = MENU_JOIN;                            /* (as JOIN would: your place is kept for you) */
        menu_notice(&menu, note);
    } else if (cfg.rejoin[0] && !net_cli && !smoke) forget_rejoin();   /* (too long ago: over by now) */
    if (net_cli) {                                          /* straight into a network game */
        menu_r = net_cli;
        if (want_versus || want_coop || want_players > 1) cfg.mode = start_mode;
        if (join_addr[0]) snprintf(cfg.server, sizeof cfg.server, "%s", join_addr);
        if (cli_password[0]) snprintf(cfg.server_password, sizeof cfg.server_password, "%s", cli_password);
        if (cli_name[0]) snprintf(cfg.player_name, sizeof cfg.player_name, "%s", cli_name);
        if (two_here) cfg.net_locals = two_here;
    }
    if (smoke_operation) { forced_menu = MENU_OPERATION; screen = SCREEN_MENU; }
    if (smoke_training) {
        forced_menu = MENU_TRAINING;
        screen = SCREEN_MENU;
    }
    if (cli_osk) {                                          /* --osk: the keyboard over the network page */
        static const int keys[] = { PAD_DOWN, PAD_RIGHT, PAD_RIGHT, PAD_A, PAD_X, PAD_A, PAD_A };
        size_t q;
        screen = SCREEN_MENU;
        menu.page = getenv("OFP_TEST_PAGE") && !strcmp(getenv("OFP_TEST_PAGE"), "keys") ? PAGE_KEYS
                  : getenv("OFP_TEST_PAGE") && !strcmp(getenv("OFP_TEST_PAGE"), "main") ? PAGE_MAIN
                  : getenv("OFP_TEST_PAGE") && !strcmp(getenv("OFP_TEST_PAGE"), "local") ? PAGE_LOCAL
                  : getenv("OFP_TEST_PAGE") && !strcmp(getenv("OFP_TEST_PAGE"), "options") ? PAGE_OPTIONS : PAGE_NET;   /* (pictures) */
        if (getenv("OFP_TEST_PAGE") && !strcmp(getenv("OFP_TEST_PAGE"), "editor")) {    /* (pictures: the editor, a new map) */
            editor_open(&ed, &editor_game, NULL, NULL, 0, preview_seed, styles[sel.style].biome);
            if (getenv("OFP_TEST_WORLD_KEY")) editor_update(&ed, 0, SDLK_b, -1, -1, 0, 0);   /* (B: the next world) */
            screen = SCREEN_EDITOR;
        }
        if (getenv("OFP_TEST_PAGE") && !strcmp(getenv("OFP_TEST_PAGE"), "browser")) {   /* (pictures: the server list, from a file) */
            static char lst[8192];
            FILE *lf = getenv("OFP_TEST_LISTING") ? fopen(getenv("OFP_TEST_LISTING"), "rb") : NULL;
            size_t ln = lf ? fread(lst, 1, sizeof lst - 1, lf) : 0;
            if (lf) fclose(lf);
            lst[ln] = 0;
            browser_parse(&browser, lst);
            browser.fetched = 1;
            browser.nmine = cfg.n_mine; memcpy(browser.mine, cfg.mine, sizeof browser.mine); memcpy(browser.mine_pw, cfg.mine_pw, sizeof browser.mine_pw);
            browser.tab = getenv("OFP_TEST_TAB") ? atoi(getenv("OFP_TEST_TAB")) : 0;
            screen = SCREEN_BROWSER;
        }
        if (getenv("OFP_TEST_PAGE") && !strcmp(getenv("OFP_TEST_PAGE"), "teams")) {     /* (tests: the team screen) */
            update_preview(&cfg, sel.style);
            start_from_menu(&cfg, &sel, &screen);
        }
        if (getenv("OFP_TEST_PAGE") && !strcmp(getenv("OFP_TEST_PAGE"), "gallery")) {   /* (tests: the gallery) */
            gallery_fetch(&gallery, cfg.listing);
            gallery.sel = gallery.n > 1 ? 1 : 0;
            screen = SCREEN_GALLERY;
            if (getenv("OFP_TEST_GALLERY_SAVE") && gallery_key(&gallery, SDL_SCANCODE_RETURN, cfg.listing) == GAL_SAVED) {
                num_maps = maps_scan(maps, MAPS_MAX);
                printf("gallery: saved %s; %s\n", gallery.saved_path, maps_find(maps, num_maps, gallery.saved_path) >= 0 ? "it's in the map list" : "NOT IN THE MAP LIST");
            }
        }
        if (!getenv("OFP_TEST_NO_OSK")) {                   /* (the menu page alone, for a picture of it) */
            snprintf(cfg.player_name, sizeof cfg.player_name, "Tom");
            osk_open(&osk, cfg.player_name, sizeof cfg.player_name, "YOUR NAME", 0);
            for (q = 0; q < sizeof keys / sizeof keys[0]; q++) osk_pad(&osk, keys[q]);
        }
        if (smoke) { smoke = 0; smoke_replay = 2; }
    }
    if (cli_list) {                                         /* --replays: the list (--replays awards: that page) */
        rv_scan();
        if (cli_list == 2) { rv_awards = 1; ach_load(); }
        screen = SCREEN_REPLAYS;
        if (smoke) { smoke = 0; smoke_replay = 2; }
    }
    if (cli_replay[0]) {                                    /* --replay FILE: watch it */
        if (rv_open(cli_replay) != 0) { fprintf(stderr, "%s: can't play that replay\n", cli_replay); return 1; }
        humans = 1;
        apply_world_rules(&styles[rv_style]);
        screen = SCREEN_PLAY;
        if (smoke) {                                        /* a test run: all of it, then a picture */
            rv_seek(view_rep.ticks > 60 ? view_rep.ticks - 60 : 0);
            rv_tank = view_rep.setup.players > 1 ? 1 : 0;
            smoke = 0;
            smoke_replay = 1;
        }
    }
    if (screen == SCREEN_TITLE && !benchmark_cli) demo_start(styles[sel.style].biome);
    http_init();                                         /* (before the update check's thread)  */
    if (!smoke && !no_save) check_for_update(&cfg);

    freq = SDL_GetPerformanceFrequency();
    last = title_timer = SDL_GetPerformanceCounter();

    while (running) {
        Uint64 fx_started = SDL_GetPerformanceCounter();
        input_set_deadzone(cfg.stick_deadzone);
        SDL_Event ev, test_text = { 0 };
        Uint64 now;
        int ticks = 0, alpha = 256, confirm = 0;

        if (smoke_pad) {                                 /* the pad test, step by step */
            static int phase, at, chat_sent, pad_ticks, good = 1, wait_for_lobby;
            static char name0[16];
            int f = ++pad_ticks - at;
            switch (phase) {
            case 0:                                      /* the network page, NAME: A opens the keyboard */
                if (f == 1) {
                    screen = SCREEN_MENU;
                    menu_reset(&menu);
                    menu.page = PAGE_NET;
                    menu.row = 0;
                    snprintf(name0, sizeof name0, "%s", cfg.player_name);
                }
                if (f == 5) pad_press(SDL_CONTROLLER_BUTTON_A);
                if (f == 7) pad_press(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
                if (f == 9) pad_press(SDL_CONTROLLER_BUTTON_A);
                if (f == 11) pad_press(SDL_CONTROLLER_BUTTON_X);
                if (f == 13) pad_press(SDL_CONTROLLER_BUTTON_A);
                if (f == 15) pad_press(SDL_CONTROLLER_BUTTON_START);
                if (f == 17) pad_press(-1);
                if (f == 20) {
                    printf("pad: keyboard %s, name \"%s\" -> \"%s\"\n", osk.open ? "still open!" : "closed", name0, cfg.player_name);
                    {
                        char want[24];
                        snprintf(want, sizeof want, "%.12swW", name0);
                        if (osk.open || strcmp(cfg.player_name, want)) good = 0;
                    }
                    SDL_StopTextInput();                 /* closing a profile editor leaves SDL text input off */
                    forced_menu = MENU_HOST;             /* (a game hosted in here: the test's server only;
                                                            players join dedicated servers) */
                    phase = 1; at = pad_ticks;
                }
                break;
            case 1:                                      /* lobby controls, rename and explicit chat focus */
                if (screen != SCREEN_LOBBY || netc.state != NC_LOBBY) { at = pad_ticks; if (f > 400) { printf("pad: no lobby\n"); running = 0; } break; }
                if (f < 10) break;
                if (f == 12) {
                    int keys[6][2];
                    memcpy(keys, input_default_keys[DEV_KEYS1], sizeof keys);
                    keys[4][0] = SDL_SCANCODE_A;
                    input_set_keyset(DEV_KEYS1, (const int (*)[2])keys);
                    if (SDL_IsTextInputActive()) { puts("pad: lobby text input starts active"); good = 0; }
                    pad_press(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
                }
                if (f == 15) pad_press(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
                if (f == 19) {
                    if (!netc_lobby_can_rename_side(&netc, SIDE_CAMP) && wait_for_lobby++ < 90) { at++; break; }
                    wait_for_lobby = 0;
                    test_key(SDLK_F3, NULL);
                }
                if (f == 21) test_key(SDLK_RIGHT, NULL);
                if (f == 23) test_key(SDLK_DOWN, NULL);
                if (f == 25) test_key(SDLK_a, "a");
                if (f == 27) {
                    if (!osk.open || strcmp(lobby_name, "s")) { puts("pad: rename keyboard navigation failed"); good = 0; }
                    test_key(SDLK_RETURN, NULL);
                }
                if (f == 34) {
                    if (!osk.open && strcmp(netc_lobby_team_name(&netc, SIDE_CAMP), "s") && wait_for_lobby++ < 90) { at++; break; }
                    if (osk.open || strcmp(netc_lobby_team_name(&netc, SIDE_CAMP), "s")) { puts("pad: team rename didn't arrive"); good = 0; }
                    test_key(0, "stray");
                    test_key(SDLK_TAB, NULL);
                }
                if (f == 36) test_key(SDLK_DOWN, NULL);
                if (f == 38) test_key(SDLK_a, "a");
                if (f == 41) {
                    if (lobby_ui.action != LB_CHAT || !lobby_ui.typing || lobby_ui.line[0] || !SDL_IsTextInputActive()) {
                        puts("pad: chat focus or Fire binding failed"); good = 0;
                    }
                    test_key(SDLK_a, "alpha");
                }
                if (f == 43) test_key(SDLK_RETURN, NULL);
                if (f == 48) {
                    if (lobby_ui.typing || SDL_IsTextInputActive()) { puts("pad: chat retained text focus"); good = 0; }
                    pad_press(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
                }
                if (f == 50) pad_press(SDL_CONTROLLER_BUTTON_A);
                if (f == 53) {
                    if (!osk.open || osk_for != OSK_FOR_LOBBY) { puts("pad: chat keyboard button failed"); good = 0; }
                    pad_press(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
                }
                if (f == 55) pad_press(SDL_CONTROLLER_BUTTON_A);
                if (f == 57) pad_press(SDL_CONTROLLER_BUTTON_START);
                if (f == 61) pad_press(SDL_CONTROLLER_BUTTON_B);
                if (f == 64) pad_press(SDL_CONTROLLER_BUTTON_A);
                if (f == 82) {
                    int typed = 0, virtual_key = 0;
                    uint32_t q;
                    for (q = netc.chat_seq; q && netc.chat_seq - q < 40; q--) {
                        const ClientChat *line = &netc.chat[q % 40];
                        if (line->seq != q) continue;
                        typed |= !strcmp(line->text, "alpha");
                        virtual_key |= !strcmp(line->text, "w");
                    }
                    if (!typed || !virtual_key) { puts("pad: lobby chat didn't arrive"); good = 0; }
                    input_set_keyset(DEV_KEYS1, input_default_keys[DEV_KEYS1]);
                    pad_press(-1);
                    printf("pad: lobby tank %d, side %d, ready %d\n", netc.want_cls, netc.side, netc.ready);
                    if (netc.want_cls != (TANK_MEDIUM + 1) % TANK_CLASS_COUNT || netc.side != SIDE_CAMP || !netc.ready) good = 0;
                    phase = 2; at = pad_ticks;
                }
                break;
            case 2:                                      /* the match: LB chat, A types, Start sends */
                if (!(net_on && screen == SCREEN_PLAY)) { at = pad_ticks; break; }
                if (f < 30) break;
                if (f == 32) pad_press(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
                if (f == 35) pad_press(SDL_CONTROLLER_BUTTON_A);
                if (f == 38) pad_press(SDL_CONTROLLER_BUTTON_START);
                if (f == 40) pad_press(-1);
                if (f > 40 && !chat_sent) {
                    uint32_t q;
                    for (q = 1; q <= netc.chat_seq; q++)
                        if (netc.chat[q % 40].seq == q && !strcmp(netc.chat[q % 40].text, "q")) chat_sent = 1;
                }
                if (f == 160) {
                    printf("pad: in the match, chat %s\n", chat_sent ? "\"q\" came back from the server" : "didn't arrive");
                    if (!chat_sent) good = 0;
                    leave_net();                         /* then: the profile window, with the stick */
                    screen = SCREEN_MENU;
                    menu_reset(&menu);
                    phase = 3; at = pad_ticks;
                }
                break;
            case 3:                                      /* the profile window: the stick moves it, not the menu behind */
                if (f == 5) signin_open(DEV_PAD1);
                if (f == 10 || f == 18) SDL_JoystickSetVirtualAxis(test_pad, SDL_CONTROLLER_AXIS_LEFTY, 30000);
                if (f == 26) SDL_JoystickSetVirtualAxis(test_pad, SDL_CONTROLLER_AXIS_LEFTY, -30000);
                if (f == 14 || f == 22 || f == 30) SDL_JoystickSetVirtualAxis(test_pad, SDL_CONTROLLER_AXIS_LEFTY, 0);
                if (f == 40) {
                    printf("pad: the profile window %s, the menu behind on row %d\n", signin_busy() ? "open" : "CLOSED", menu.row);
                    if (!signin_busy() || menu.row != 0) good = 0;
                    {   /* closed again (Escape), or it takes phase 4's buttons for itself */
                        SDL_Event esc;
                        SDL_zero(esc);
                        esc.type = SDL_KEYDOWN; esc.key.keysym.sym = SDLK_ESCAPE; esc.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
                        SDL_PushEvent(&esc);
                    }
                    phase = 4; at = pad_ticks;
                }
                break;
            case 4: {                                    /* a local match's report: A again, X tanks, B the menu */
                static int again, tanks;
                int cls4[4] = { 1, 1, 1, 1 };
                if (f == 1 || f == 20 || f == 40) {      /* (a match, over: its report up) */
                    custom_teams = 0;
                    start_game(4040u, cls4, MODE_SOLO, BIOME_CLASSIC);
                    screen = SCREEN_PLAY;
                    over_frames = REPORT_DELAY + 1;
                }
                if (f == 5) pad_press(SDL_CONTROLLER_BUTTON_A);
                if (f == 8) { pad_press(-1); again = over_frames <= REPORT_DELAY && screen == SCREEN_PLAY; }
                if (f == 25) pad_press(SDL_CONTROLLER_BUTTON_X);
                if (f == 28) { pad_press(-1); tanks = screen == SCREEN_SELECT; }
                if (f == 45) pad_press(SDL_CONTROLLER_BUTTON_B);
                if (f == 48) {
                    pad_press(-1);
                    printf("pad: the report: A %s, X %s, B %s\n", again ? "played it again" : "DIDN'T", tanks ? "the tanks" : "DIDN'T",
                           screen == SCREEN_MENU && menu.page == PAGE_LOCAL ? "LOCAL GAME" : "DIDN'T");
                    if (!again || !tanks || screen != SCREEN_MENU || menu.page != PAGE_LOCAL) good = 0;
                    phase = 5; at = pad_ticks;
                }
                break;
            }
            case 5:                                      /* replay buttons and awards with no keyboard */
                if (f == 1) { rv_scan(); rv_focus = RV_LIST; rv_awards = 0; screen = SCREEN_REPLAYS; }
                if (f == 5) pad_press(SDL_CONTROLLER_BUTTON_X);
                if (f == 9 && !rv_awards) { puts("pad: awards didn't open"); good = 0; }
                if (f == 10 || f == 14) pad_press(-1);
                if (f == 12) pad_press(SDL_CONTROLLER_BUTTON_X);
                if (f == 16) pad_press(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
                if (f == 20 && rv_focus != RV_WATCH) { puts("pad: replay buttons didn't focus"); good = 0; }
                if (f == 24) pad_press(SDL_CONTROLLER_BUTTON_B);
                if (f == 28) {
                    if (screen != SCREEN_MENU || menu.page != PAGE_TOOLS) { puts("pad: replay Back failed"); good = 0; }
                    phase = 6; at = pad_ticks;
                }
                break;
            case 6: {                                    /* editor, Start menu, cursor and reconnect */
                if (f == 1) { editor_open(&ed, &editor_game, NULL, NULL, 0, 2121, BIOME_CLASSIC); screen = SCREEN_EDITOR; }
                if (f == 6) pad_press(SDL_CONTROLLER_BUTTON_START);
                if (f == 12) pad_press(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
                if (f == 18 && (!ed.menu || ed.msel != 1)) { puts("pad: editor menu navigation failed"); good = 0; }
                if (f == 22) pad_press(SDL_CONTROLLER_BUTTON_B);
                if (f == 28 && ed.menu) { puts("pad: editor Back failed"); good = 0; }
                if (f == 32) {
                    int d; pad_press(-1);
                    for (d = 0; d < SDL_NumJoysticks(); d++)
                        if (SDL_JoystickGetDeviceInstanceID(d) == SDL_JoystickInstanceID(test_pad)) { SDL_JoystickDetachVirtual(d); break; }
                    SDL_JoystickClose(test_pad); test_pad = NULL;
                }
                if (f == 36) {
                    int d = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 1);
                    test_pad = d >= 0 ? SDL_JoystickOpen(d) : NULL;
                    if (!test_pad) { puts("pad: reconnect failed"); good = 0; }
                    screen = SCREEN_MENU; menu_reset(&menu);
                }
                if (f == 40) {
                    printf(good ? "pad ok\n" : "pad: FAILED\n");
                    running = 0;
                }
                break;
            }
            }
        }
        if (SDL_AtomicGet(&update_done) == 1 && screen == SCREEN_MENU) {   /* a newer version: say so, once */
            SDL_AtomicSet(&update_done, 2);
            if (update_note[0]) menu_notice(&menu, update_note);
        }
        {   /* the music: the world's tune in a match, the menus' elsewhere */
            int want = -1;
            if (screen == SCREEN_PLAY) {
                want = (replay_view && rv_speed != 1) || smoke ? -1 : soundtrack_for_map(game.biome, game.map.music);
            } else if (screen != SCREEN_EDITOR) want = screen == SCREEN_MENU && menu.page == PAGE_ABOUT ? SOUNDTRACK_CREDITS : MUSIC_MENU;
            music_play(want);
        }
        if (hosting) {                                  /* the referee, if it's us; and a join code, through routers */
            server_step(&host_srv);
            if (!smoke && !no_save) host_traversal(cfg.listing, cfg.player_name);
        }
        if (screen == SCREEN_LOBBY && signin_busy()) {
            int before = signin_count();
            signin_frame();
            if (signin_count() > before && before == net_extra + 1 && before < MAX_SIGNED) {
                NetClient *extra = &netx[before - 1];
                if (netc_open(extra, net_host, (uint16_t)join_port, signin_name(before), net_pass,
                              signin_key(before), tank_chassis(signin_tank(before)), signin_steer(before)) == 0) {
                    extra->portrait = signin_avatar(before) + 1;
                    extra->wait_for_place = 1;
                    snprintf(extra->cache_dir, sizeof extra->cache_dir, "%s", netc.cache_dir);
                    net_extra = before;
                    lobby_ui.seat = before;
                    lobby_ui.action = LB_NONE;
                } else menu_notice(&menu, extra->status);
            }
        }
        if (screen == SCREEN_LOBBY || net_on) netc_step(&netc);
        if (training && screen == SCREEN_PLAY && !net_on) train_check();
        if (training && screen != SCREEN_PLAY && screen != SCREEN_SELECT) end_training();
        if (daily && screen != SCREEN_PLAY) daily = 0;
        if (operation.phase && screen != SCREEN_PLAY) operation.phase = OP_INACTIVE;
        if (screen == SCREEN_LOBBY && netc.state == NC_LOBBY && !noted_join && !hosting && !no_save) {
            char key[72], *colon;                        /* joined: into the browser's "lately" */
            noted_join = 1;
            {   /* (as host:port, an IPv6 one in brackets) */
                char h[64];
                int pt = NET_PORT;
                net_split(cfg.server, h, sizeof h, &pt);
                snprintf(key, sizeof key, strchr(h, ':') ? "[%.56s]:%d" : "%.56s:%d", h, pt);
                (void)colon;
            }
            list_front(cfg.recent, &cfg.nrecent, 10, key);
            save_cfg(&cfg);
        }
        if ((screen == SCREEN_LOBBY || net_on) && net_two) {
            int kx;
            for (kx = 1; kx < net_extra; kx++) {         /* players 3 and 4: the same as the second */
                netc_step(&netx[kx]);
                if (netx[kx].state == NC_FAILED) { menu_notice(&menu, netx[kx].status); me_tankx[kx] = -1; }
            }
            netc_step(&netc2);
            if (netc2.state == NC_FAILED) {             /* the second player couldn't come (or dropped out) */
                menu_notice(&menu, netc2.status);
                for (kx = 0; kx < net_extra; kx++) { netc_close(&netx[kx]); me_tankx[kx] = -1; }
                net_extra = 0;
            }
        }
        if (screen == SCREEN_LOBBY && netc.state == NC_RUNNING) {   /* our match is on: play */
            int m;
            if (osk.open && (osk_for == OSK_FOR_TEAM_NAME || osk_for == OSK_FOR_LOBBY)) osk.open = 0;
            lobby_ui.typing = 0;
            if (match_start(&game, &netc.ms, netc.map_len ? netc.map : NULL) == 0) {
                net_on = 1;
                spec_view = 0;
                me_tank = netc.slot == 255 ? 0 : netc.slot;     /* watching: follow the first tank */
                if (!no_save && !smoke && !hosting && netc.slot != 255) {   /* (the game closes in it: join again next time) */
                    snprintf(cfg.rejoin, sizeof cfg.rejoin, strchr(net_host, ':') ? "[%s]:%d" : "%s:%d", net_host, join_port);
                    cfg.rejoin_until = (long)time(NULL) + 15 * 60;
                    save_cfg(&cfg);
                }
                if (!no_save && netc.slot != 255) {             /* the people in this match: friends */
                    int k;
                    for (k = 0; k < netc.nlobby; k++)
                        if (netc.lobby[k].playing == 1 && netc.lobby[k].id != netc.id) note_friend(&cfg, netc.lobby[k].name);
                    save_cfg(&cfg);
                }
                {
                    int kx;
                    for (kx = 0; kx < 3; kx++)
                        me_tankx[kx] = kx < net_extra && netx[kx].slot >= 0 && netx[kx].slot < 255 && netx[kx].state >= NC_LOADING
                                       ? netx[kx].slot : -1;
                    for (kx = 0; kx < 3; kx++) net_nextx[kx] = 0;
                }
                net_next2 = 0;
                humans = netc.ms.players;
                net_next = 0;
                net_acc = 0;
                net_play = 0;
                play_mode = MODE_SOLO;
                for (m = 0; m < MODE_COUNT; m++)
                    if (game_modes[m].players == humans && game_modes[m].versus == netc.ms.versus) play_mode = m;
                for (m = 0; m < num_styles; m++)
                    if (styles[m].biome == (game.biome == BIOME_SCIFI ? BIOME_MOON : game.biome)) {
                        cur_style = m;
                        break;
                    }
                apply_world_rules(&styles[cur_style]);
                end_recording();
                replay_begin(&rec, &netc.ms, netc.map_len ? netc.map : NULL, game.num_tanks);
                if (netc.slot >= 0 && netc.slot < MATCH_PLAYERS)   /* (my name; the server's replay has everyone's) */
                    snprintf(rec.names[netc.slot], sizeof rec.names[0], "%s", cfg.player_name);
                recording = 1;
                fx_reset();
                audio_reset();
                over_frames = 0;
                screen = SCREEN_PLAY;
            }
        }
        if (rejoin_done) {                                  /* (nothing to join again any more) */
            rejoin_done = 0;
            if (cfg.rejoin[0]) { cfg.rejoin[0] = 0; cfg.rejoin_until = 0; if (!no_save) save_cfg(&cfg); }
        }
        if (net_on && screen == SCREEN_PLAY && netc.state == NC_ENDED && over_frames <= REPORT_DELAY) {
            over_frames = REPORT_DELAY + 1;                 /* the server says it's over: the report */
            forget_rejoin();
        }
        if (net_on && screen == SCREEN_PLAY && netc.state == NC_FAILED && netc_can_retry(&netc) &&
            (!net_lost_at || SDL_GetTicks() - net_lost_at < 90000)) {   /* a dropout: call again, keep the place */
            static uint32_t last_try;
            uint8_t key[32];
                int portrait = netc.portrait;
            if (!net_lost_at) net_lost_at = SDL_GetTicks();
            if (SDL_GetTicks() - last_try > 2000) {
                last_try = SDL_GetTicks();
                memcpy(key, netc.key, 32);
                netc_close(&netc);
                netc_open(&netc, net_host, (uint16_t)join_port, cfg.player_name, net_pass, key, netc.want_cls, steer);
                netc.portrait = portrait;
            }
        }
        if (net_on && screen == SCREEN_PLAY && net_lost_at && netc.state == NC_LOBBY &&
            netc.server_state == SRV_LOBBY && netc.nlobby > 0) {      /* back, but the match is over: the lobby */
            screen = SCREEN_LOBBY;
            net_on = 0;
            net_lost_at = 0;
        }
        if (net_on && screen == SCREEN_PLAY && net_lost_at && netc.state == NC_RUNNING) {   /* back: from the start, fast */
            match_start(&game, &netc.ms, netc.map_len ? netc.map : NULL);
            net_next = 0;
            net_lost_at = 0;
            fx_reset();
        }
        if ((screen == SCREEN_LOBBY || (net_on && screen == SCREEN_PLAY)) && netc.state == NC_FAILED &&
            !(net_on && netc_can_retry(&netc) && net_lost_at && SDL_GetTicks() - net_lost_at < 90000)) {
            screen = SCREEN_MENU;
            menu_reset(&menu);
            menu.page = PAGE_NET;
            menu_notice(&menu, netc.status);
        }
        if (screen == SCREEN_BROWSER && !browser.typing_pw) {      /* a pad's stick and A button in the list */
            static InputBits prev_nav;
            InputBits nv = input_player1_menu(), pr = (InputBits)(nv & ~prev_nav);
            prev_nav = nv;
            if (pending_browser_join || (pr & (IN_UP | IN_DOWN | IN_FIRE))) {
                int r = pending_browser_join ? BR_JOIN : browser_input(&browser, pr, 0, NULL);
                pending_browser_join = 0;
                if (r == BR_JOIN && browser.sel < browser.n) {
                    const BrowserEntry *e = &browser.e[browser.sel];
                    snprintf(cfg.server, sizeof cfg.server, "%.50s:%d", e->host, e->port);
                    snprintf(cfg.server_password, sizeof cfg.server_password, "%s", e->password ? browser.pw : "");
                    screen = SCREEN_MENU; menu_reset(&menu); menu.page = PAGE_NET; menu_r = MENU_JOIN;
                } else if (r == BR_JOIN_MINE && browser.mine_sel < browser.nmine) {
                    snprintf(cfg.server, sizeof cfg.server, "%s", browser.mine[browser.mine_sel]);
                    snprintf(cfg.server_password, sizeof cfg.server_password, "%s", browser.mine_pw[browser.mine_sel]);
                    screen = SCREEN_MENU; menu_reset(&menu); menu.page = PAGE_NET; menu_r = MENU_JOIN;
                }
            }
        }
        if (screen == SCREEN_BROWSER && browser_due && frame_drawn) {   /* ("looking..." is on screen now) */
            browser_due = 0;
            browser_lists(&browser, &cfg);
                browser.need = signin_count() > 0 ? signin_count() : cfg.net_locals;   /* (room for everyone here) */
            browser_fetch(&browser, cfg.listing);
        }
        if (screen != SCREEN_PLAY && screen != SCREEN_LOBBY && (net_on || hosting || netc.socket_open)) leave_net();
        if (screen == SCREEN_LOBBY) net_on = 0;
        if (screen != SCREEN_PLAY && recording) end_recording();
        /* the ORIGINAL world's camp has the original's three garages; the others four */
        if (!operation.phase && !editor_test_active && !net_on) cur_style = sel.style;
        signin_set_tanks(styles[sel.style].tanks, screen == SCREEN_LOBBY || net_on ||
                         classic_rules(&cfg, &styles[sel.style], &preview));
        worldgen_set_camp_garages(styles[sel.style].faithful || cfg.local_type == TEAMS_CLASSIC ? 3 : 4);   /* (the 1987 rules: three) */
        {   /* (tests: OFP_TEST_KEYS="FRAME:KEY,..." types keys, e.g. "20:Return,40:Down"; held 3 frames) */
            static char tk[256];
            static int tk_init, tk_loop;                 /* (its own count: the frame count restarts with the menu) */
            const char *p;
            if (!tk_init) { tk_init = 1; if (getenv("OFP_TEST_KEYS")) snprintf(tk, sizeof tk, "%s", getenv("OFP_TEST_KEYS")); }
            tk_loop++;
            for (p = tk; *p; ) {
                int f = atoi(p);
                char name[32];
                const char *c2 = strchr(p, ':'), *e2;
                if (!c2) break;
                e2 = strchr(c2, ',');
                snprintf(name, sizeof name, "%.*s", (int)(e2 ? e2 - c2 - 1 : (long)strlen(c2 + 1)), c2 + 1);
                if (name[0] == '@') {                        /* "@TEXT": typed text */
                    if (tk_loop == f) {
                        test_text.type = SDL_TEXTINPUT;
                        snprintf(test_text.text.text, sizeof test_text.text.text, "%s", name + 1);
                    }
                } else {
                    char *star = strchr(name, '*');
                    int hold = star ? atoi(star + 1) : 3;
                    if (star) *star = 0;
                    if (tk_loop >= f && tk_loop <= f + hold) {
                    SDL_Event ke;
                    memset(&ke, 0, sizeof ke);
                    ke.type = tk_loop < f + hold ? SDL_KEYDOWN : SDL_KEYUP;
                    ke.key.state = tk_loop < f + hold ? SDL_PRESSED : SDL_RELEASED;
                    ke.key.repeat = tk_loop > f && tk_loop < f + hold;     /* (held: the repeats a real key sends) */
                    ke.key.keysym.scancode = SDL_GetScancodeFromName(name);
                    ke.key.keysym.sym = SDL_GetKeyFromScancode(ke.key.keysym.scancode);
                    SDL_PushEvent(&ke);
                    }
                }
                if (!e2) break;
                p = e2 + 1;
            }
        }
        for (;;) {
            int pad;
            int text_mode = -1;
            if (screen == SCREEN_LOBBY)
                text_mode = signin_busy() || lobby_ui.typing || (osk.open && (osk_for == OSK_FOR_TEAM_NAME || osk_for == OSK_FOR_LOBBY));
            else if (screen == SCREEN_PLAY && net_on && chat_typing) text_mode = 1;
            if (text_mode >= 0) {
                if (text_input_before < 0) text_input_before = SDL_IsTextInputActive();
                if (text_mode && !SDL_IsTextInputActive()) SDL_StartTextInput();
                else if (!text_mode && SDL_IsTextInputActive()) SDL_StopTextInput();
            } else if (text_input_before >= 0) {
                if (text_input_before) SDL_StartTextInput();
                else SDL_StopTextInput();
                text_input_before = -1;
            }
            if (!SDL_PollEvent(&ev)) {
                if (!test_text.type) break;
                ev = test_text;
                test_text.type = 0;
            }
            if (ev.type == SDL_MOUSEMOTION) ui_mouse_position(win, &ev.motion.x, &ev.motion.y);
            if (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP)
                ui_mouse_position(win, &ev.button.x, &ev.button.y);
            if (ev.type == SDL_APP_WILLENTERBACKGROUND) {
                if (screen == SCREEN_PLAY && !net_on && !replay_view) { paused = pmenu = 1; pmenu_sel = PM_RESUME; }
                audio_quiet(); music_play(-1);
                if (!no_save) save_cfg(&cfg);
                continue;
            }
            pad = input_handle_event(&ev);
            if (screen == SCREEN_TEAMS && teams_name_event(&teams, &ev)) continue;
            if (ev.type != SDL_USEREVENT) last_input = SDL_GetTicks();   /* (power save: something happened) */
            if (editor_test_active && screen == SCREEN_PLAY &&
                (pad == PAD_PAUSE || pad == PAD_BACK || (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE))) {
                editor_test_active = 0;
                cur_style = editor_old_style;
                apply_world_rules(&styles[cur_style]);
                screen = SCREEN_EDITOR;
                over_frames = 0;
                paused = pmenu = 0;
                editor_resume_test(&ed, menu_input(), mouse_x, mouse_y, mouse_b);
                ed_stick_wait = 1;
                continue;
            }
            if (editor_test_active && screen == SCREEN_PLAY && over_frames > REPORT_DELAY) continue;
            if (screen == SCREEN_PLAY && signin_busy() && signin_event(&ev, pad)) continue;
            if (pad == PAD_PAUSE &&
                ((screen == SCREEN_MENU && !osk.open) || screen == SCREEN_TEAMS ||
                 (screen == SCREEN_PLAY && !net_on && !replay_view && !pmenu &&
                  over_frames <= REPORT_DELAY))) {
                int dev = input_device_of_event(&ev);
                if (dev >= DEV_PAD1 && signin_is_in(dev) &&
                    (screen != SCREEN_PLAY || dev != signin_device(0))) {
                    signin_open_own(dev, screen == SCREEN_PLAY);
                    continue;
                }
                if (dev >= DEV_PAD1 && screen == SCREEN_MENU) {
                    signin_open(dev);
                    continue;
                }
            }
            if (screen == SCREEN_LOBBY && !osk.open && !lobby_ui.typing &&
                (signin_busy() || signin_count() > 0) && signin_event(&ev, pad)) continue;
            if (screen == SCREEN_TEAMS) {
                int dev = input_device_of_event(&ev);
                if (signin_busy() && signin_event(&ev, pad)) continue;
                if (!signin_busy() && ev.type == SDL_KEYDOWN && !ev.key.repeat &&
                    (ev.key.keysym.mod & KMOD_SHIFT) && ev.key.keysym.sym >= SDLK_F1 && ev.key.keysym.sym <= SDLK_F4) {
                    int p = ev.key.keysym.sym - SDLK_F1;
                    if (p < teams.humans) signin_open_own(teams.dev[p], 0);
                    continue;
                }
                if (!signin_busy() && ev.type == SDL_KEYDOWN && !ev.key.repeat &&
                    ev.key.keysym.sym >= SDLK_F1 && ev.key.keysym.sym <= SDLK_F4 &&
                    teams_rename(&teams, ev.key.keysym.sym - SDLK_F1)) continue;
                if (signin_count() == teams.humans && teams.humans < 4 &&
                    teams.humans < teams.max_total && dev >= 0 && !signin_is_in(dev) &&
                    input_device_is_fire(dev, &ev)) {
                    signin_open(dev);
                    continue;
                }
            }
            if (screen == SCREEN_MENU && !osk.open && menu.capture < 0 &&
                (menu.page == PAGE_MAIN || signin_busy() || signin_count() > 0) &&
                signin_event(&ev, pad)) {                     /* (FIRE on an unused device: join; the picker; a name) */
                menu.prev = 0xFF;                             /* (that press chose nothing in the menu) */
                continue;
            }
            if (osk.open && ev.type != SDL_QUIT &&
                (pad != PAD_NONE || osk_for == OSK_FOR_TEAM_NAME || osk_for == OSK_FOR_LOBBY)) {
                int r = OSK_TYPING;
                if (pad != PAD_NONE) r = osk_pad(&osk, pad);
                else if (ev.type == SDL_TEXTINPUT) append_input(osk.target, osk.cap, ev.text.text);
                else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT)
                    r = osk_click(&osk, LOGICAL_W, LOGICAL_H, ev.button.x, ev.button.y);
                else if (ev.type == SDL_KEYDOWN) {
                    SDL_Keycode key = ev.key.keysym.sym;
                    int nav = lobby_keypad(lobby_pilot() == &netc ? 0 : lobby_ui.seat, &ev.key);
                    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) nav = PAD_PAUSE;
                    else if (key == SDLK_BACKSPACE) nav = PAD_B;
                    else if (key >= SDLK_SPACE && key < SDLK_DELETE && (ev.key.keysym.mod & KMOD_SHIFT)) nav = PAD_NONE;
                    if (nav != PAD_NONE) {
                        if (key >= SDLK_SPACE && key < SDLK_DELETE) SDL_FlushEvent(SDL_TEXTINPUT);
                        if (!ev.key.repeat || nav == PAD_B || nav >= PAD_UP) r = osk_pad(&osk, nav);
                    }
                }
                if (r != OSK_TYPING) osk_hold = 1;
                if (r == OSK_DONE) {
                    if (osk_for == OSK_FOR_SETTING) save_cfg(&cfg);
                    else if (osk_for == OSK_FOR_LOBBY) lobby_send_chat();
                    else if (osk_for == OSK_FOR_TEAM_NAME) netc_lobby_rename(lobby_pilot(), lobby_rename_side, lobby_name);
                    else if (osk_for == OSK_FOR_MATCH && chat_line[0]) { netc_chat(&netc, chat_line); chat_line[0] = 0; chat_typing = 0; }
                    else if (osk_for == OSK_FOR_SERVER_PW) {
                        SDL_Event wake;                      /* (the browser picks it up at once) */
                        browser.typing_pw = 0;
                        pending_browser_join = 1;
                        memset(&wake, 0, sizeof wake);
                        wake.type = SDL_USEREVENT;
                        SDL_PushEvent(&wake);
                    }
                }
                if (r == OSK_CANCEL && osk_for == OSK_FOR_MATCH) chat_typing = 0;
                if (r == OSK_CANCEL && osk_for == OSK_FOR_SERVER_PW) browser.typing_pw = 0;
                continue;
            }
            if (screen == SCREEN_MENU && pad == PAD_A) {   /* A on a text row: the on-screen keyboard */
                char *fb;
                size_t fc;
                const char *ft;
                int sec;
                if (menu_text_field(&menu, &cfg, &fb, &fc, &ft, &sec)) {
                    osk_open(&osk, fb, fc, ft, sec);
                    osk_for = OSK_FOR_SETTING;
                    continue;
                }
            }
            if (screen == SCREEN_BROWSER && browser.typing_pw && pad == PAD_A) {   /* a password, with the pad */
                osk_open(&osk, browser.pw, sizeof browser.pw, "PASSWORD", 1);
                osk_for = OSK_FOR_SERVER_PW;
                continue;
            }
            if (net_on && screen == SCREEN_PLAY && pad == PAD_LB && over_frames <= REPORT_DELAY) {   /* chat, with the pad */
                chat_line[0] = 0;
                chat_typing = 1;
                osk_open(&osk, chat_line, sizeof chat_line, "SAY", 0);
                osk_for = OSK_FOR_MATCH;
                continue;
            }
            if (screen == SCREEN_GALLERY && (ev.type == SDL_KEYDOWN || pad != PAD_NONE)) {   /* the gallery */
                SDL_Scancode sc = ev.type == SDL_KEYDOWN ? ev.key.keysym.scancode
                                : pad == PAD_UP ? SDL_SCANCODE_UP : pad == PAD_DOWN ? SDL_SCANCODE_DOWN
                                : pad == PAD_A ? SDL_SCANCODE_RETURN : pad == PAD_B || pad == PAD_BACK ? SDL_SCANCODE_ESCAPE
                                : pad == PAD_ZOOM ? SDL_SCANCODE_R : SDL_SCANCODE_UNKNOWN;   /* (Y: the listing again) */
                int gr = gallery_key(&gallery, sc, cfg.listing);
                if (gr == GAL_BACK) { screen = SCREEN_MENU; menu_reset(&menu); menu_show_map_row(&menu); }   /* (back on MAP) */
                if (gr == GAL_SAVED) {                      /* into the list, and picked */
                    num_maps = maps_scan(maps, MAPS_MAX);
                    menv.num_maps = num_maps;
                    snprintf(cfg.map, sizeof cfg.map, "%.1023s", gallery.saved_path);
                    save_cfg(&cfg);
                }
                continue;
            }

            if (screen == SCREEN_LOBBY && lobby_ui.diagnostics) {
                int nav = pad;
                if (ev.type == SDL_KEYDOWN) nav = lobby_keypad(0, &ev.key);
                if (nav == PAD_LEFT || nav == PAD_RIGHT) lobby_ui.diagnostic_button ^= 1;
                if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT && ev.button.y >= LOGICAL_H - 29) {
                    lobby_ui.diagnostic_button = ev.button.x >= LOGICAL_W / 2;
                    nav = PAD_A;
                }
                if (nav == PAD_B || nav == PAD_BACK) lobby_ui.diagnostics = 0;
                if (nav == PAD_A) {
                    if (lobby_ui.diagnostic_button) lobby_ui.diagnostics = 0;
                    else {
                        char path[1200]; char *pref = ofp_pref_path();
                        snprintf(path, sizeof path, "%snetwork-report.txt", pref ? pref : "");
                        snprintf(lobby_ui.diagnostic_saved, sizeof lobby_ui.diagnostic_saved, "%s",
                                 netc_save_diagnostics(&netc, path) == 0 ? "REPORT SAVED" : "COULD NOT SAVE");
                        SDL_free(pref);
                    }
                }
                if (ev.type == SDL_QUIT) running = 0;
                continue;
            }
            if (screen == SCREEN_LOBBY && pad != PAD_NONE) {   /* the lobby with a pad (a Steam Deck, the sofa) */
                int seat, dev = input_device_of_event(&ev);
                NetClient *player;
                if (lobby_ui.typing && pad != PAD_LB) {
                    if (pad == PAD_B || pad == PAD_BACK) lobby_ui.typing = 0;
                    continue;
                }
                for (seat = 0; seat <= net_extra && seat < signin_count(); seat++)
                    if (signin_device(seat) == dev) break;
                if (seat > net_extra || seat >= signin_count()) {
                    if (net_extra || signin_count() > 0) continue;
                    seat = 0;
                }
                player = seat ? &netx[seat - 1] : &netc;
                if (player->state == NC_LOBBY) lobby_ui.seat = seat;
                if (pad == PAD_RB)
                    lobby_ui.action = lobby_action_step(&netc, player, &lobby_ui, 1);
                else if (pad == PAD_A && lobby_ui.action) {
                    int action = lobby_ui.action == LB_CHAT ? LB_KEYBOARD : lobby_ui.action;
                    if (lobby_activate(player, action)) { screen = SCREEN_MENU; menu_reset(&menu); menu.page = PAGE_NET; }
                } else if (pad == PAD_B && lobby_ui.action) lobby_ui.action = LB_NONE;
                else if (pad == PAD_PAUSE && netc_lobby_can_start(player)) netc_chat(player, "/start");
                else if (pad == PAD_A || (pad == PAD_PAUSE && netc.server_state != SRV_RUNNING)) {
                    if (player->state == NC_LOBBY) netc_ready(player, !player->ready, player->want_cls);
                } else if (pad == PAD_X) netc_lobby_tank_step(player, 1);
                else if (pad >= PAD_UP) lobby_move(player, pad);
                else if (pad == PAD_ZOOM && netc.nballot) {   /* Y: my vote, to the next map on the ballot */
                    int bi, cur = -1;
                    char vb[24];
                    for (bi = 0; bi < netc.nballot; bi++) if (netc.ballot[bi] == netc.my_vote) cur = bi;
                    netc.my_vote = netc.ballot[(cur + 1) % netc.nballot];
                    snprintf(vb, sizeof vb, "/vote %d", netc.my_vote);
                    netc_chat(&netc, vb);
                }
                else if (pad == PAD_LB) lobby_activate(player, LB_KEYBOARD);
                else if (pad == PAD_B) lobby_ui.show_scores ^= 1;
                else if (pad == PAD_ZOOM && netc.server_state == SRV_RUNNING) netc_watch(&netc, 1);
                else if (pad == PAD_PAUSE && netc.server_state == SRV_RUNNING) lobby_join_match();
                else if (pad == PAD_BACK) { screen = SCREEN_MENU; menu_reset(&menu); menu.page = PAGE_NET; }
                continue;
            }
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_TEXTINPUT && screen == SCREEN_MENU && menu_text(&menu, &cfg, ev.text.text)) continue;
            if (ev.type == SDL_KEYDOWN && screen == SCREEN_MENU && menu_typing(&menu, &cfg)) {   /* typing: no hotkeys */
                SDL_Keycode k = ev.key.keysym.sym;
                int r;
                if (k == SDLK_ESCAPE || k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_BACKSPACE ||
                    k == SDLK_UP || k == SDLK_DOWN) {
                    r = menu_update(&menu, &cfg, &menv, 0, ev.key.keysym.scancode, k == SDLK_ESCAPE,
                                    k == SDLK_RETURN || k == SDLK_KP_ENTER);
                    if (r != MENU_NOTHING) menu_r = r;
                }
                continue;
            }
            if (ev.type == SDL_DROPFILE) {                     /* a disk image dropped on the window */
                char msg[96];
                const char *f = ev.drop.file;
                size_t fl = f ? strlen(f) : 0;
                if (fl > 7 && !strcmp(f + fl - 7, ".ofprep") && !net_on && rv_open(f) == 0) {   /* a replay: watch it */
                    humans = 1;
                    apply_world_rules(&styles[rv_style]);
                    over_frames = 0;
                    screen = SCREEN_PLAY;
                } else if (fl > 4 && (!strcmp(f + fl - 4, ".adf") || !strcmp(f + fl - 4, ".ADF"))) {
                    original_import(f, msg, sizeof msg);
                    num_maps = maps_scan(maps, MAPS_MAX);
                    menv.num_maps = num_maps;
                    if (screen != SCREEN_MENU) { screen = SCREEN_MENU; menu_reset(&menu); }
                    menu_notice(&menu, msg);
                }
                SDL_free(ev.drop.file);
                continue;
            }
            if (ev.type == SDL_KEYDOWN || ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_CONTROLLERBUTTONDOWN ||
                ev.type == SDL_MOUSEMOTION || ev.type == SDL_CONTROLLERAXISMOTION)
                idle_since = SDL_GetTicks();
            if (screen == SCREEN_TITLE) {                       /* any key, click or button: to the menu */
                if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                    if (ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
                    else if (ev.key.keysym.sym == SDLK_F11) set_fullscreen(win, ren, &cfg, !cfg.fullscreen);
                    else {                                  /* who are you? (then the menu) */
                        int dev = input_device_of_event(&ev);
                        screen = SCREEN_MENU; menu_reset(&menu); fx_reset(); menu_quiet_since = SDL_GetTicks() | 1;
                        if (signin_count() == 0) {
                            int enter = ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_KP_ENTER;
                            signin_open(enter || dev < 0 ? DEV_KEYS1 : dev);
                            from_title = 1;
                        }
                    }
                } else if (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_CONTROLLERBUTTONDOWN) {
                    int dev = input_device_of_event(&ev);
                    screen = SCREEN_MENU; menu_reset(&menu); fx_reset(); menu_quiet_since = SDL_GetTicks() | 1;
                    if (signin_count() == 0) { signin_open(dev >= 0 ? dev : DEV_KEYS1); from_title = 1; }
                }
                continue;
            }
            if (ev.type == SDL_MOUSEMOTION) { mouse_x = ev.motion.x; mouse_y = ev.motion.y; }
            if (ev.type == SDL_MOUSEBUTTONDOWN) { mouse_b |= SDL_BUTTON(ev.button.button); mouse_x = ev.button.x; mouse_y = ev.button.y; }
            if (ev.type == SDL_MOUSEBUTTONUP) mouse_b &= ~SDL_BUTTON(ev.button.button);
            if (ev.type == SDL_MOUSEWHEEL) wheel += ev.wheel.y;
            if (screen == SCREEN_EDITOR) {                      /* the editor has its own keys */
                if (ev.type == SDL_TEXTINPUT) editor_text(&ed, ev.text.text);
                if (ev.type == SDL_KEYDOWN) {
                    SDL_Keycode k = ev.key.keysym.sym;
                    if (k == SDLK_F11) set_fullscreen(win, ren, &cfg, !cfg.fullscreen);
                    else if (!ev.key.repeat || k == SDLK_q || k == SDLK_e || (ed.details && k == SDLK_BACKSPACE)) ed_key = (int)k;
                }
                if (pad == PAD_BACK) ed_key = SDLK_ESCAPE;
                if (pad == PAD_PAUSE) ed_key = ED_KEY_MENU;      /* (START: the map's menu) */
                continue;
            }
            if (screen == SCREEN_MENU) {                        /* the game-type screen takes it all */
                int key = 0, back = 0, ok = 0, r;
                int escape_back = menu.page != PAGE_MAIN && ev.type == SDL_KEYDOWN &&
                                  !ev.key.repeat && ev.key.keysym.sym == SDLK_ESCAPE;
                if (!escape_back && !(menu.capture >= 0 && ev.type == SDL_KEYDOWN) &&
                    !menu_owner_ok(&ev)) continue;
                if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                    SDL_Keycode k = ev.key.keysym.sym;
                    if (k == SDLK_F11 ||
                        (k == SDLK_RETURN && (ev.key.keysym.mod & (KMOD_ALT | KMOD_GUI)))) {
                        set_fullscreen(win, ren, &cfg, !cfg.fullscreen);
                        continue;
                    }
                    if (k == SDLK_ESCAPE) back = 1;
                    else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && menu.capture < 0) ok = 1;
                    else key = (int)ev.key.keysym.scancode;
                }
                if (pad == PAD_BACK || (pad == PAD_B && !osk.open && menu.page != PAGE_MAIN)) back = 1;   /* (B: back, as on any
                                                                pad; not on the first page: there it would quit, and held it signs out) */
                if (!key && !back && !ok) continue;
                if (!escape_back && menu_quiet()) continue;  /* (the key that left the title: not a choice) */
                r = menu_with_sound(&menu, &cfg, &menv, menu_input(), key, back, ok);
                if (r != MENU_NOTHING) menu_r = r;             /* dealt with below, once a frame */
                continue;
            }
            if (screen == SCREEN_PLAY && operation.phase >= OP_WON && !pmenu) {
                SDL_Keycode k = ev.type == SDL_KEYDOWN && !ev.key.repeat ? ev.key.keysym.sym : 0;
                if (k == SDLK_ESCAPE || pad == PAD_B || pad == PAD_BACK) {
                    operation.phase = OP_INACTIVE;
                    screen = SCREEN_MENU;
                    menu_reset(&menu);
                } else if (k == SDLK_F1 || pad == PAD_A) {
                    start_desert_operation();
                    over_frames = 0;
                    acc = 0.0;
                }
                continue;
            }
            if (screen == SCREEN_PLAY && !replay_view && over_frames <= REPORT_DELAY && !chat_typing &&
                (pmenu || pad == PAD_PAUSE || (ev.type == SDL_KEYDOWN && !ev.key.repeat && ev.key.keysym.sym == SDLK_ESCAPE)) &&
                (ev.type == SDL_KEYDOWN || pad != PAD_NONE)) {   /* the pause menu: player 1's (keys, or their pad) */
                SDL_Keycode k = ev.type == SDL_KEYDOWN ? ev.key.keysym.sym : 0;
                int dev = signin_count() ? signin_device(0) : DEV_KEYS1;
                InputBits key = ev.type == SDL_KEYDOWN ? input_device_key(dev, ev.key.keysym.scancode) : 0;
                if (k != SDLK_ESCAPE && !menu_owner_ok(&ev)) continue;
                if (!pmenu) {                                /* open it (a local match: paused) */
                    pmenu = 1; pmenu_sel = PM_RESUME; orders_open = 0; order_seat = 0;
                    if (!net_on) paused = 1;
                    continue;
                }
                if (ev.type == SDL_KEYDOWN && ev.key.repeat && !(key & (IN_UP | IN_DOWN))) continue;
                if (orders_open) {
                    if (pad == PAD_UP || (key & IN_UP)) order_row = (order_row + ORDER_COUNT - 1) % ORDER_COUNT;
                    else if (pad == PAD_DOWN || (key & IN_DOWN)) order_row = (order_row + 1) % ORDER_COUNT;
                    else if (pad == PAD_B || pad == PAD_BACK || k == SDLK_ESCAPE || (key & IN_MINE)) orders_open = 0;
                    else if ((pad == PAD_A || k == SDLK_RETURN || (key & IN_FIRE)) && orders_available()) {
                        if (net_on) netc_team_order(order_client(), order_row);
                        else game_team_order(&game, order_slot(), order_row);
                        orders_open = pmenu = paused = 0;
                    }
                    continue;
                }
                if (pad == PAD_UP || (key & IN_UP)) pmenu_sel = (pmenu_sel + PM_COUNT - 1) % PM_COUNT;
                else if (pad == PAD_DOWN || (key & IN_DOWN)) pmenu_sel = (pmenu_sel + 1) % PM_COUNT;
                else if (pad == PAD_B || pad == PAD_PAUSE || pad == PAD_BACK || k == SDLK_ESCAPE || (key & IN_MINE)) { pmenu = 0; paused = 0; }
                else {
                    int dir = pad == PAD_LEFT || (key & IN_LEFT) ? -1 : pad == PAD_RIGHT || (key & IN_RIGHT) ? 1 : 0;
                    int ok = pad == PAD_A || k == SDLK_RETURN || k == SDLK_KP_ENTER || (key & IN_FIRE);
                    if (pmenu_sel == PM_RESUME && ok) { pmenu = 0; paused = 0; }
                    else if (pmenu_sel == PM_ORDERS) {
                        if (dir) order_seat = (order_seat + signin_count() + dir) % (signin_count() ? signin_count() : 1);
                        if (ok && orders_available()) { orders_open = 1; order_row = ORDER_ATTACK; }
                    }
                    else if (pmenu_sel == PM_VOLUME && (dir || ok)) {
                        int next = volume_step(cfg.volume, dir ? dir : 1);
                        if (next != cfg.volume) {
                            cfg.volume = next;
                            audio_set_volume(next);
                            save_cfg(&cfg);
                        }
                    } else if (pmenu_sel == PM_MUSIC && (dir || ok)) {
                        int next = volume_step(cfg.music, dir ? dir : 1);
                        if (next != cfg.music) {
                            cfg.music = next;
                            music_volume(next);
                            save_cfg(&cfg);
                        }
                    } else if (pmenu_sel == PM_VIEW && (dir || ok)) {
                        zoom = !zoom; cfg.zoom = zoom; save_cfg(&cfg);
                    } else if (pmenu_sel == PM_QUIT && ok) {     /* the match: left, to the menu */
                        pmenu = 0; paused = 0;
                        if (net_on) screen = return_to_lobby();
                        else {
                            screen = SCREEN_MENU; menu_reset(&menu);
                            if (!training && !daily && !operation.phase) menu_show_local(&menu);
                        }
                    }
                }
                if (pmenu_sel == PM_ORDERS && !game_orders_allowed(&game)) pmenu_sel = (pad == PAD_UP || (key & IN_UP)) ? PM_VIEW : PM_QUIT;
                continue;
            }
            if (pad == PAD_ZOOM) zoom = !zoom;
            if (pad == PAD_PAUSE) {
                if (screen == SCREEN_PLAY && !net_on && over_frames > REPORT_DELAY) {   /* report: restart */
                    start_game(seed, sel.choices, play_mode, styles[sel.style].biome);
                    over_frames = 0; acc = 0.0;
                } else if (screen == SCREEN_PLAY) {
                    /* (the pause menu has it) */
                } else {
                    confirm = 1;
                }
            }
            if ((pad == PAD_BACK || pad == PAD_B) && screen == SCREEN_SELECT) { screen = SCREEN_MENU; menu_reset(&menu); menu_show_local(&menu); }
            else if (pad == PAD_BACK && screen == SCREEN_PLAY && !net_on && !replay_view) {
                screen = SCREEN_SELECT;
                paused = 0;
                select_reset(&sel, game.tanks[0].cls, sel.style, num_styles);
            }
            if (net_on && screen == SCREEN_PLAY && chat_typing) {   /* typing a chat line in the match */
                if (ev.type == SDL_TEXTINPUT) {
                    append_input(chat_line, sizeof chat_line, ev.text.text);
                } else if (ev.type == SDL_KEYDOWN) {
                    SDL_Keycode k = ev.key.keysym.sym;
                    if (k == SDLK_BACKSPACE && chat_line[0]) chat_line[strlen(chat_line) - 1] = 0;
                    else if (k == SDLK_TAB && !ev.key.repeat) netc_chat_complete(&netc, chat_line, sizeof chat_line);
                    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) { if (chat_line[0]) netc_chat(&netc, chat_line); chat_typing = 0; }
                    else if (k == SDLK_ESCAPE) chat_typing = 0;
                }
                continue;
            }
            if (replay_view && screen == SCREEN_PLAY && (ev.type == SDL_KEYDOWN || pad != PAD_NONE ||
                                                         ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEMOTION)) {   /* watching a replay */
                SDL_Keycode k = ev.type == SDL_KEYDOWN ? ev.key.keysym.sym : 0;
                static const int speeds[] = { 1, 2, 4, 8, 16 };
                int si = 0;
                while (si < 4 && speeds[si] < rv_speed) si++;
                if (k == SDLK_ESCAPE || pad == PAD_BACK) {
                    replay_view = 0;
                    over_frames = 0;
                    audio_quiet();
                    screen = SCREEN_REPLAYS;
                } else if (k == SDLK_SPACE || k == SDLK_p || pad == PAD_PAUSE) rv_speed = rv_speed ? 0 : 1;
                else if (k == SDLK_UP) rv_speed = speeds[rv_speed ? (si < 4 ? si + 1 : 4) : 0];
                else if (k == SDLK_DOWN) rv_speed = rv_speed ? speeds[si > 0 ? si - 1 : 0] : 1;
                else if (spec_keys(k, rv_tank)) {}                       /* F free camera, O the whole map */
                else if (k == SDLK_TAB || pad == PAD_RB) { rv_tank++; spec_free = 0; spec_director = 0; }    /* the next tank (and following it) */
                else if (pad == PAD_LB) { rv_tank += game.num_tanks ? game.num_tanks - 1 : 0; spec_free = 0; }   /* (a pad: the one before) */
                else if (pad == PAD_X) spec_keys(SDLK_f, rv_tank);       /* (a pad: X the free camera, Y the whole map) */
                else if (pad == PAD_ZOOM) spec_keys(SDLK_o, rv_tank);
                else if (pad == PAD_UP) rv_speed = speeds[rv_speed ? (si < 4 ? si + 1 : 4) : 0];
                else if (pad == PAD_DOWN) rv_speed = rv_speed ? speeds[si > 0 ? si - 1 : 0] : 1;
                else if (pad == PAD_RIGHT) { rv_seek(rv_tick + 10 * TICK_RATE); over_frames = 0; }   /* (a pad: ten seconds on, */
                else if (pad == PAD_LEFT) { rv_seek(rv_tick > 10 * TICK_RATE ? rv_tick - 10 * TICK_RATE : 0); over_frames = 0; }   /* back) */
                else if (pad == PAD_B) spec_keys(SDLK_k, rv_tank);       /* (B: the tally on and off) */
                else if (k >= SDLK_1 && k <= SDLK_6 && k - SDLK_1 < game.num_tanks) rv_tank = k - SDLK_1;   /* that player's */
                else if (ev.type == SDL_MOUSEBUTTONDOWN || (ev.type == SDL_MOUSEMOTION && (ev.motion.state & SDL_BUTTON_LMASK))) {
                    int mx = ev.type == SDL_MOUSEBUTTONDOWN ? ev.button.x : ev.motion.x;
                    int my = ev.type == SDL_MOUSEBUTTONDOWN ? ev.button.y : ev.motion.y;
                    const int vx = 8, vy = 6, vw = 274, vh = 204;    /* (the replay's view, as drawn) */
                    if (mx >= vx && mx < vx + vw && my >= vy + vh - 20 && my < vy + vh && view_rep.ticks) {
                        rv_seek((uint32_t)((uint64_t)view_rep.ticks * (uint32_t)(mx - vx) / (uint32_t)vw));
                        over_frames = 0;
                    }
                }
                else if (k == SDLK_RIGHT) { rv_seek(rv_tick + 10 * TICK_RATE); over_frames = 0; }
                else if (k == SDLK_LEFT) {
                    rv_seek(rv_tick > 10 * TICK_RATE ? rv_tick - 10 * TICK_RATE : 0);
                    over_frames = 0;
                } else if (k == SDLK_HOME) { rv_seek(0); over_frames = 0; }
                else if (k == SDLK_r) {                          /* radar near or full map, as in a match */
                    render_set_radar_zoom(render_radar_zoom() == 2 ? 1 : 2);
                } else if (k == SDLK_z) { zoom = !zoom; }
                continue;
            }
            if (screen == SCREEN_REPLAYS) {
                int nav = pad, activate = 0, old_sel = rv_sel, old_focus = rv_focus, old_awards = rv_awards;
                SDL_Keycode key = ev.type == SDL_KEYDOWN ? ev.key.keysym.sym : 0;
                if ((ev.type == SDL_KEYDOWN || pad != PAD_NONE) && !menu_owner_ok(&ev)) continue;
                if (ev.type == SDL_KEYDOWN) nav = lobby_keypad(0, &ev.key);
                if (key == SDLK_SPACE) nav = PAD_A;
                if (key == SDLK_TAB || nav == PAD_X) {
                    rv_awards = !rv_awards;
                    rv_focus = rv_awards ? RV_WATCH : RV_LIST;
                    if (!ach_loaded) ach_load();
                } else if (nav == PAD_BACK || nav == PAD_B) {
                    screen = SCREEN_MENU;
                    menu_reset(&menu);
                    menu.page = PAGE_TOOLS;
                    menu.row = 1;
                } else if (nav == PAD_UP) {
                    if (rv_focus != RV_LIST && !rv_awards && rv_count) rv_focus = RV_LIST;
                    else if (rv_sel > 0) rv_sel--;
                } else if (nav == PAD_DOWN) {
                    if (rv_focus == RV_LIST && !rv_awards && rv_sel + 1 < rv_count) rv_sel++;
                    else if (rv_focus == RV_LIST) rv_focus = RV_WATCH;
                } else if (nav == PAD_LEFT || nav == PAD_RIGHT) {
                    if (rv_focus == RV_LIST) rv_focus = RV_WATCH;
                    else rv_focus = RV_WATCH + (rv_focus - RV_WATCH + (nav == PAD_RIGHT ? 1 : 2)) % 3;
                } else if (nav == PAD_A) activate = 1;
                else if (ev.type == SDL_MOUSEWHEEL && !rv_awards && rv_count) {
                    rv_sel -= ev.wheel.y;
                    if (rv_sel < 0) rv_sel = 0;
                    if (rv_sel >= rv_count) rv_sel = rv_count - 1;
                    rv_focus = RV_LIST;
                } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                    int x = ev.button.x, y = ev.button.y;
                    if (!rv_awards && x >= 18 && x < LOGICAL_W - 18 && y >= 73 && y < 73 + RV_VISIBLE * 11) {
                        int row = rv_top + (y - 73) / 11;
                        if (row < rv_count) { rv_sel = row; rv_focus = RV_LIST; activate = ev.button.clicks >= 2; }
                    } else if (x >= 18 && x < 366 && y >= 194 && y < 205 && (x - 18) % 120 < 108) {
                        rv_focus = RV_WATCH + (x - 18) / 120;
                        activate = 1;
                    }
                }
                if (activate) {
                    if (rv_focus == RV_BACK) { screen = SCREEN_MENU; menu_reset(&menu); menu.page = PAGE_TOOLS; menu.row = 1; }
                    else if (rv_focus == RV_AWARDS) { rv_awards = 1; if (!ach_loaded) ach_load(); }
                    else if (rv_awards) { rv_awards = 0; rv_focus = RV_LIST; }
                    else if (rv_count) {
                        rv_notice[0] = 0;
                        if (rv_open(rv_files[rv_sel].path) == 0) {
                            humans = 1;
                            apply_world_rules(&styles[rv_style]);
                            over_frames = 0;
                            acc = 0.0;
                            screen = SCREEN_PLAY;
                        }
                    }
                }
                if (activate || old_sel != rv_sel || old_focus != rv_focus || old_awards != rv_awards)
                    audio_menu_feedback(activate);
                if (screen == SCREEN_MENU) menu_quiet_since = SDL_GetTicks() | 1;
                rv_keep_visible();
                continue;
            }
            if (net_on && screen == SCREEN_PLAY && ev.type == SDL_KEYDOWN && !ev.key.repeat &&
                (ev.key.keysym.sym == SDLK_t || ev.key.keysym.sym == SDLK_RETURN) && over_frames <= REPORT_DELAY) {
                chat_typing = 1;                            /* T (or Enter): say something */
                chat_line[0] = 0;
                SDL_FlushEvent(SDL_TEXTINPUT);
                continue;
            }
            if (net_on && screen == SCREEN_PLAY && netc.slot == 255 &&
                ((ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_TAB) || pad == PAD_RB)) { spec_view++; spec_free = 0; spec_director = 0; continue; }   /* watching: the next tank */
            if (net_on && screen == SCREEN_PLAY && netc.slot == 255 && over_frames <= REPORT_DELAY &&
                (pad == PAD_X || pad == PAD_ZOOM || pad == PAD_B)) {   /* (a pad: X free, Y the map, B the tally) */
                spec_keys(pad == PAD_X ? SDLK_f : pad == PAD_ZOOM ? SDLK_o : SDLK_k, spec_view % (game.num_tanks ? game.num_tanks : 1));
                continue;
            }
            if (net_on && screen == SCREEN_PLAY && netc.slot == 255 && ev.type == SDL_KEYDOWN &&
                spec_keys(ev.key.keysym.sym, spec_view % (game.num_tanks ? game.num_tanks : 1))) continue;   /* F, O */
            if (net_on && screen == SCREEN_PLAY) {         /* a network match: ESC leaves, after the report: the lobby */
                int out = pad == PAD_BACK, report = over_frames > REPORT_DELAY, back = 0;
                if (ev.type == SDL_KEYDOWN && !ev.key.repeat && netc.slot == 255 && ev.key.keysym.sym == SDLK_ESCAPE) {
                    screen = return_to_lobby();
                    continue;
                }
                if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                    SDL_Keycode k = ev.key.keysym.sym;
                    out |= k == SDLK_ESCAPE && !report;
                    if (report && (k == SDLK_LEFT || k == SDLK_RIGHT)) report_sel = !report_sel;   /* REMATCH, LOBBY */
                    if (report && (k == SDLK_RETURN || k == SDLK_SPACE) && report_sel == 0) netc_rematch(&netc, 1);
                    back = report && (k == SDLK_RETURN || k == SDLK_ESCAPE || k == SDLK_SPACE || k == SDLK_F1);
                    if (k == SDLK_F11) set_fullscreen(win, ren, &cfg, !cfg.fullscreen);
                }
                if (report && (pad == PAD_LEFT || pad == PAD_RIGHT)) report_sel = !report_sel;
                if (report && pad == PAD_A) { if (report_sel == 0) netc_rematch(&netc, 1); back = 1; }
                if (report && pad == PAD_B) back = 1;
                if (report && pad == PAD_PAUSE) back = 1;
                if (back || out) { pmenu = paused = 0; screen = return_to_lobby(); }
                continue;
            }
            if (screen == SCREEN_LOBBY) {                  /* chat, get ready, pick a tank */
                NetClient *pilot = lobby_pilot();
                if (ev.type == SDL_KEYDOWN && !lobby_ui.typing && signin_count() > 0) {
                    int seat, dev = input_device_of_event(&ev);
                    for (seat = 0; seat <= net_extra && seat < signin_count(); seat++)
                        if (signin_device(seat) == dev) break;
                    if (seat <= net_extra && seat < signin_count()) {
                        lobby_ui.seat = seat;
                        pilot = seat ? &netx[seat - 1] : &netc;
                    } else if (dev >= 0) continue;
                }
                int action = ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT ?
                    lobby_action_at(&netc, pilot, &lobby_ui, LOGICAL_W, ev.button.x, ev.button.y) : LB_NONE;
                if (action) {
                    lobby_ui.slap_click_id = 0;
                    lobby_ui.action = action;
                    if (lobby_activate(pilot, action)) { screen = SCREEN_MENU; menu_reset(&menu); menu.page = PAGE_NET; }
                } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                    lobby_ui.typing = 0;
                    lobby_ui.action = LB_NONE;
                    if (netc.lobby_social && !lobby_ui.show_scores) {
                        const LobbyPlayer *target = lobby_player_at(&netc, &lobby_ui, LOGICAL_W, ev.button.x, ev.button.y);
                        int id = target && target->id != pilot->id ? target->id : 0;
                        if (ev.button.clicks == 2 && id && id == lobby_ui.slap_click_id) {
                            char cmd[NET_NAME + 6];
                            snprintf(cmd, sizeof cmd, "/slap %s", target->name);
                            netc_chat(pilot, cmd);
                        }
                        lobby_ui.slap_click_id = id;
                    }
                } else if (ev.type == SDL_TEXTINPUT) {
                    if (lobby_ui.typing) append_input(lobby_ui.line, sizeof lobby_ui.line, ev.text.text);
                } else if (ev.type == SDL_KEYDOWN) {
                    SDL_Keycode k = ev.key.keysym.sym;
                    if (lobby_ui.typing) {
                        if (k == SDLK_BACKSPACE && lobby_ui.line[0]) lobby_ui.line[strlen(lobby_ui.line) - 1] = 0;
                        else if (k == SDLK_TAB && !ev.key.repeat) netc_chat_complete(pilot, lobby_ui.line, sizeof lobby_ui.line);
                        else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && !ev.key.repeat) lobby_send_chat();
                        else if (k == SDLK_ESCAPE) lobby_ui.typing = 0;
                    } else if (!ev.key.repeat) {
                        int seat = pilot == &netc ? 0 : lobby_ui.seat;
                        int nav = lobby_keypad(seat, &ev.key);
                        if (nav == PAD_A) {
                            SDL_FlushEvent(SDL_TEXTINPUT);
                            if (lobby_activate(pilot, lobby_ui.action ? lobby_ui.action : LB_READY)) { screen = SCREEN_MENU; menu_reset(&menu); menu.page = PAGE_NET; }
                        } else if (nav >= PAD_UP) lobby_move(pilot, nav);
                        else if (k == SDLK_F1 && pilot->state == NC_LOBBY)
                            netc_ready(pilot, !pilot->ready, pilot->want_cls);
                        else if (k == SDLK_TAB && net_extra && (ev.key.keysym.mod & KMOD_SHIFT)) {
                            lobby_ui.seat = (seat + 1) % (net_extra + 1);
                            lobby_ui.action = LB_NONE;
                        } else if (k == SDLK_TAB)
                            lobby_ui.action = lobby_action_step(&netc, pilot, &lobby_ui, 1);
                        else if (k == SDLK_PAGEUP || k == SDLK_PAGEDOWN) {
                            LobbyRoster roster;
                            netc_lobby_roster(&netc, &roster);
                            if (lobby_ui.page >= roster.pages) lobby_ui.page = roster.pages - 1;
                            lobby_ui.page = (lobby_ui.page + roster.pages + (k == SDLK_PAGEUP ? -1 : 1)) % roster.pages;
                        }
                        else if (k == SDLK_F3)
                            lobby_activate(pilot, pilot->side == SIDE_FORTRESS ? LB_FORTRESS : LB_CAMP);
                        else if (k == SDLK_F7 && netc_lobby_can_start(pilot)) netc_chat(pilot, "/start");
                        else if (k == SDLK_F4 && pilot->state == NC_LOBBY && netc_lobby_teams(pilot))
                            netc_side(pilot, (pilot->side + 1) % 3);
                        else if (k == SDLK_F6) lobby_ui.show_scores ^= 1;
                        else if (k == SDLK_F2 && netc.server_state == SRV_RUNNING) netc_watch(&netc, 1);   /* watch */
                        else if (k == SDLK_F5) lobby_join_match();
                        else if (nav == PAD_BACK && lobby_ui.action) lobby_ui.action = LB_NONE;
                        else if (nav == PAD_BACK) { screen = SCREEN_MENU; menu_reset(&menu); menu.page = PAGE_NET; }
                        else if (k == SDLK_F11) set_fullscreen(win, ren, &cfg, !cfg.fullscreen);
                    }
                }

                continue;
            }
            if (screen == SCREEN_BROWSER) {                /* the server list */
                int r = BR_NOTHING;
                if (ev.type == SDL_TEXTINPUT) r = browser_input(&browser, 0, 0, ev.text.text);
                else if (ev.type == SDL_KEYDOWN) {
                    int sc = ev.key.keysym.scancode;
                    InputBits nb = (InputBits)(sc == SDL_SCANCODE_UP ? IN_UP : sc == SDL_SCANCODE_DOWN ? IN_DOWN : 0);
                    r = browser_input(&browser, nb, sc, NULL);
                } else if (pad == PAD_X || pad == PAD_ZOOM || pad == PAD_B || pad == PAD_BACK || pad == PAD_LB)   /* a pad: favourite,
                                                                                     refresh, back, LB its last match */
                    r = browser_input(&browser, 0, pad == PAD_X ? SDL_SCANCODE_F : pad == PAD_ZOOM ? SDL_SCANCODE_R
                                                   : pad == PAD_LB ? SDL_SCANCODE_L : SDL_SCANCODE_ESCAPE, NULL);
                else if (pad == PAD_LB || pad == PAD_RB) r = browser_input(&browser, pad == PAD_LB ? IN_LEFT : IN_RIGHT, 0, NULL);   /* the tabs */
                if (r == BR_DROP_MINE) {                     /* MY SERVERS: that one goes */
                    settings_drop_mine(&cfg, browser.mine_sel);
                    save_cfg(&cfg);
                    browser.nmine = cfg.n_mine; memcpy(browser.mine, cfg.mine, sizeof browser.mine); memcpy(browser.mine_pw, cfg.mine_pw, sizeof browser.mine_pw);
                    if (browser.mine_sel >= browser.nmine && browser.mine_sel > 0) browser.mine_sel--;
                    r = BR_NOTHING;
                }
                if (r == BR_JOIN_MINE && browser.mine_sel < browser.nmine) {   /* MY SERVERS: there, as DIRECT CONNECT */
                    snprintf(cfg.server, sizeof cfg.server, "%s", browser.mine[browser.mine_sel]);
                    snprintf(cfg.server_password, sizeof cfg.server_password, "%s", browser.mine_pw[browser.mine_sel]);
                    save_cfg(&cfg);
                    screen = SCREEN_MENU; menu_reset(&menu); menu.page = PAGE_NET; menu_r = MENU_JOIN;
                    r = BR_NOTHING;
                }
                if (pad == PAD_BACK) r = BR_BACK;
                if (r == BR_BACK) { screen = SCREEN_MENU; menu_reset(&menu); menu.page = PAGE_NET; }
                if (r == BR_REFRESH) { browser.fetched = 0; browser.n = 0; browser_due = 1; frame_drawn = 0; }
                if (r == BR_WATCH && browser.sel < browser.n) {   /* L: its last match, watched */
                    char got[1300];
                    snprintf(browser.status, sizeof browser.status, "fetching its last match...");
                    if (fetch_server_replay(cfg.listing, &browser.e[browser.sel], got, sizeof got) != 0)
                        snprintf(browser.status, sizeof browser.status, "%.120s", got);
                    else if (rv_open(got) == 0) {
                        humans = 1;
                        apply_world_rules(&styles[rv_style]);
                        over_frames = 0;
                        screen = SCREEN_PLAY;
                    } else snprintf(browser.status, sizeof browser.status, "THAT ISN'T A REPLAY THIS GAME CAN READ");
                    r = BR_NOTHING;
                }
                if (r == BR_FAVOURITE && browser.sel < browser.n) {   /* F: a favourite, or not any more */
                    char key[72];
                    int k, was = -1;
                    snprintf(key, sizeof key, "%.50s:%d", browser.e[browser.sel].host, browser.e[browser.sel].port);
                    for (k = 0; k < cfg.nfavs; k++) if (!strcmp(cfg.favs[k], key)) was = k;
                    if (was >= 0) { for (k = was; k + 1 < cfg.nfavs; k++) memcpy(cfg.favs[k], cfg.favs[k + 1], 72); cfg.nfavs--; }
                    else list_front(cfg.favs, &cfg.nfavs, 16, key);
                    save_cfg(&cfg);
                    browser_lists(&browser, &cfg);
                browser.need = signin_count() > 0 ? signin_count() : cfg.net_locals;   /* (room for everyone here) */
                    browser_mark(&browser);
                    for (k = 0; k < browser.n; k++) {            /* (the same one stays selected) */
                        char here[72];
                        snprintf(here, sizeof here, "%.50s:%d", browser.e[k].host, browser.e[k].port);
                        if (!strcmp(here, key)) { browser.sel = k; break; }
                    }
                }
                if (r == BR_JOIN && browser.sel < browser.n) {
                    const BrowserEntry *e = &browser.e[browser.sel];
                    snprintf(cfg.server, sizeof cfg.server, "%.50s:%d", e->host, e->port);
                    snprintf(cfg.server_password, sizeof cfg.server_password, "%s", e->password ? browser.pw : "");
                    screen = SCREEN_MENU;                   /* and join it, as from the menu */
                    menu_reset(&menu);
                    menu.page = PAGE_NET;
                    menu_r = MENU_JOIN;
                }
                continue;
            }
            {   /* the report: again (F1, A), the tanks (F2, X), the menu (F3, B); a pad's or the keys' */
                int report = screen == SCREEN_PLAY && over_frames > REPORT_DELAY && !net_on && !replay_view, act = 0;
                if (report && ev.type == SDL_KEYDOWN && !ev.key.repeat) {   /* the buttons: left/right, FIRE; F1..F3 too */
                    SDL_Keycode k = ev.key.keysym.sym;
                    if (k == SDLK_LEFT || k == SDLK_a) report_sel = (report_sel + 2) % 3;
                    if (k == SDLK_RIGHT || k == SDLK_d) report_sel = (report_sel + 1) % 3;
                    act = k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE || k == SDLK_LCTRL || k == SDLK_RCTRL ? report_sel + 1
                        : k == SDLK_F1 ? 1 : k == SDLK_F2 ? 2 : k == SDLK_F3 ? 3 : 0;
                }
                if (report && pad) {                         /* a pad: the d-pad or stick chooses, A presses, B: the menu */
                    if (pad == PAD_LEFT) report_sel = (report_sel + 2) % 3;
                    if (pad == PAD_RIGHT) report_sel = (report_sel + 1) % 3;
                    act = pad == PAD_A ? report_sel + 1 : pad == PAD_X ? 2 : pad == PAD_B || pad == PAD_BACK ? 3 : 0;
                }
                if (act) report_sel = 0;
                if (act == 1) {                                  /* the same again */
                    if (daily) start_daily();                   /* (today's challenge: today's again) */
                    else start_game(seed, sel.choices, play_mode, styles[sel.style].biome);   /* same map again */
                    over_frames = 0;
                    acc = 0.0;
                    continue;
                }
                if (act == 2) {                                  /* repick tanks (and sides) */
                    if (custom_teams) {
                        int q;
                        screen = SCREEN_TEAMS;
                        for (q = 0; q < teams.humans; q++) { teams.ready[q] = 0; teams.prev[q] = 0xFF; }
                    } else {
                        screen = SCREEN_SELECT;
                        select_reset(&sel, game.tanks[0].cls, sel.style, num_styles);
                    }
                    continue;
                }
                if (act == 3) {                                  /* back to the menu: LOCAL GAME */
                    screen = SCREEN_MENU;
                    menu_reset(&menu);
                    if (!training && !daily) menu_show_local(&menu);
                    continue;
                }
            }
            if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                SDL_Keycode k = ev.key.keysym.sym;
                if (k == SDLK_ESCAPE) {                           /* one step back: play, tanks, menu */
                    if (screen == SCREEN_PLAY && custom_teams) {   /* (the team screen again, nobody ready) */
                        int q;
                        screen = SCREEN_TEAMS;
                        for (q = 0; q < teams.humans; q++) { teams.ready[q] = 0; teams.prev[q] = 0xFF; }
                    }
                    else if (screen == SCREEN_PLAY) { screen = SCREEN_SELECT; select_reset(&sel, game.tanks[0].cls, sel.style, num_styles); }
                    else { screen = SCREEN_MENU; menu_reset(&menu); if (!training && !daily) menu_show_local(&menu); }   /* (back where it began) */
                    continue;
                }
                if (k == SDLK_F11 || (k == SDLK_RETURN && (ev.key.keysym.mod & (KMOD_ALT | KMOD_GUI))))
                    set_fullscreen(win, ren, &cfg, !cfg.fullscreen);
                else if (k == SDLK_RETURN || k == SDLK_KP_ENTER)
                    confirm = 1;
                if (k == SDLK_z) { zoom = !zoom; cfg.zoom = zoom; save_cfg(&cfg); }   /* close-up view */
                if (k == SDLK_TAB && screen == SCREEN_SELECT) { select_next_mode(&sel); cfg.mode = sel.mode; }
                if ((k == SDLK_p || k == SDLK_PAUSE) && screen == SCREEN_PLAY && !net_on) paused = !paused;
                if (k == SDLK_r && screen == SCREEN_PLAY) {        /* radar: near or full map */
                    render_set_radar_zoom(render_radar_zoom() == 2 ? 1 : 2);
                    cfg.radar = render_radar_zoom();
                    save_cfg(&cfg);
                }
                if (k == SDLK_F6) audio_toggle_mute();
                if (k == SDLK_F7) { ai_level = (ai_level + 1) % 4; cfg.ai_level = ai_level; save_cfg(&cfg); }
                /* (the controls are chosen under OPTIONS only: a stray key mid-battle
                 * used to switch the steering without anyone noticing) */
            }
        }

        now = SDL_GetPerformanceCounter();
        frame_dt = (double)(now - last) / (double)freq;
        acc += frame_dt;
        last = now;

        if (screen == SCREEN_SELECT) {
            InputBits in = input_player1(), ins[4] = { 0, 0, 0, 0 };
            int go, np = game_modes[sel.mode].players, p;
            for (p = 0; p < np && p < MAX_SIGNED && np > 1; p++) ins[p] = input_player(p, np);
            if (smoke) { in = ins[0] = (frame == 10 || frame == 20) ? IN_RIGHT : 0; ins[1] = frame == 15 ? IN_LEFT : 0; }
            go = sel.mode == MODE_SOLO ? select_update(&sel, in, confirm || (smoke && frame == 40))
                                       : select_update_n(&sel, ins, confirm || (smoke && frame == 40));
            if (go) {
                if (shot_select) save_shot(ren, shot_select);
                if (!fixed_seed && !play_map[0] && !smoke)          /* the random map you saw in the menu */
                    seed = preview_seed;
                else if (!fixed_seed)                               /* a new battlefield each time */
                    seed = (uint32_t)SDL_GetPerformanceCounter() ^ (uint32_t)SDL_GetTicks() * 2654435761u;
                preview_seed = (uint32_t)SDL_GetPerformanceCounter() * 2654435761u ^ seed;   /* next time, another */
                start_game(seed, sel.choices, sel.mode, styles[sel.style].biome);
                apply_world_rules(&styles[sel.style]);
                if (play_here_x >= 0) {                     /* the editor's F10: start at the cursor, if a tank fits there */
                    int ok = 1, ddx, ddy;
                    for (ddy = -1; ddy <= 1; ddy++) for (ddx = -1; ddx <= 1; ddx++) {
                        int qx = play_here_x + ddx, qy = play_here_y + ddy;
                        if (qx < 0 || qy < 0 || qx >= MAP_W || qy >= MAP_H || (tile_info[game.map.type[qy][qx]].flags & TF_SOLID) ||
                            game.map.sid[qy][qx]) ok = 0;
                    }
                    if (ok) {
                        Tank *t0 = &game.tanks[0];
                        t0->x = t0->prev_x = FX(play_here_x * TILE_SIZE + TILE_SIZE / 2);
                        t0->y = t0->prev_y = FX(play_here_y * TILE_SIZE + TILE_SIZE / 2);
                        t0->speed = 0;
                    } else menu_notice(&menu, "NO ROOM FOR A TANK AT THE CURSOR: FROM THE GARAGE");
                    play_here_x = play_here_y = -1;
                }
                cfg.mode = sel.mode;
                snprintf(cfg.style, sizeof cfg.style, "%s", styles[sel.style].name);
                save_cfg(&cfg);
                screen = SCREEN_PLAY;
                over_frames = 0;
                acc = 0.0;
            }
        } else if (screen == SCREEN_TEAMS) {                /* the sides: everyone with their own keys or pad */
            int tr = TEAMS_NOTHING, p, picking = signin_busy();
            for (p = 0; p < teams.humans && !picking; p++)
                picking = teams.dev[p] >= 0 && !input_device_present(teams.dev[p]);
            if (picking) signin_frame();
            for (p = teams.humans - 1; p >= 0; p--) {
                int dev = teams.dev[p];
                if (dev >= 0 && !signin_is_in(dev)) teams_remove_player(&teams, p);
                else if (picking) teams.prev[p] = 0xff;
            }
            while (teams.humans < signin_count()) {
                int next = teams.humans;
                if (!teams_add_player(&teams, signin_device(next), signin_name(next), signin_avatar(next), signin_tank(next))) break;
            }
            if (picking) for (p = 0; p < teams.humans && p < signin_count(); p++) {
                if (teams.dev[p] != signin_device(p)) continue;
                snprintf(teams.name[p], sizeof teams.name[p], "%s", signin_name(p));
                teams.avatar[p] = signin_avatar(p);
            }
            if (!teams.humans) {
                screen = SCREEN_TITLE;
                demo_start(styles[sel.style].biome);
            }
            else if (!signin_busy()) tr = teams_frame(&teams);
            if (tr == TEAMS_BACK) { screen = SCREEN_MENU; menu_reset(&menu); menu_show_local(&menu); }
            if (tr == TEAMS_START) {
                int m, mode = MODE_SOLO;
                cfg.bots_side[0] = teams.bots[0] + teams.bots[1];   /* (the AI bots: as many next time) */
                cfg.ai_level = ai_level = teams.bot_level;           /* (and as good: the team screen's LEVEL) */
                save_cfg(&cfg);
                for (m = 0; m < MODE_COUNT; m++)           /* (the nearest old game type: the screen's layout) */
                    if (game_modes[m].players == teams.humans) { mode = m; if (game_modes[m].versus) break; }
                if (!fixed_seed && !play_map[0]) seed = preview_seed;
                else if (!fixed_seed) seed = (uint32_t)SDL_GetPerformanceCounter() ^ (uint32_t)SDL_GetTicks() * 2654435761u;
                preview_seed = (uint32_t)SDL_GetPerformanceCounter() * 2654435761u ^ seed;
                custom_teams = 1;
                sel.mode = mode;
                start_game(seed, sel.choices, mode, styles[sel.style].biome);
                apply_world_rules(&styles[sel.style]);
                screen = SCREEN_PLAY;
                over_frames = 0;
                acc = 0.0;
            }
        } else if (screen != SCREEN_PLAY) {
            acc = 0.0;                                      /* menus, editor, title: the game waits */
        } else if (net_on) {
            InputBits set[MAX_TANKS];
            int ready = netc_waiting(&netc, net_next);    /* official ticks here, not shown yet */
            uint32_t front = net_next + (uint32_t)ready;   /* the first one not here */
            net_acc += frame_dt;                        /* my buttons, once a tick, a few ticks ahead */
            if (smoke && !getenv("OFP_NET_STEADY")) net_acc += tick_s;   /* a test run: as fast as the server allows */
            while (net_acc >= tick_s) {
                net_acc -= tick_s;
                /* (timed against the newest tick here, not the one on screen: the buffer
                 * below shows a little behind, and the server shouldn't wait for that) */
                if (netc.slot != 255 && netc.mine_hi <= front + (uint32_t)netc.delay)
                    netc_input(&netc, netc.mine_hi, smoke ? smoke_script(netc.mine_hi + 37u * (uint32_t)me_tank)
                                                          : chat_typing || pmenu ? 0 : input_player(0, net_extra + 1));
                {   /* players 2, 3 and 4 here: their own keys or pads */
                    int kx;
                    for (kx = 0; kx < net_extra; kx++)
                        if (me_tankx[kx] >= 0 && netx[kx].mine_hi <= net_next + (uint32_t)netx[kx].delay)
                            netc_input(&netx[kx], netx[kx].mine_hi, chat_typing ? 0 : input_player(kx + 1, net_extra + 1));
                }
            }
            if (net_acc > 0.25) net_acc = 0;
            static int steady_test = -1, st_frames, st_stalls, st_most, st_lo = 99, st_hi;
            if (steady_test < 0) steady_test = getenv("OFP_NET_STEADY") != NULL;   /* (tests: the normal pace, measured) */
            if ((smoke && !steady_test) || netc_behind(&netc) > 20 || ready > NET_BUFFER + 25) {
                /* catching up (joining, back after a dropout, a test run): all at once */
                while (ticks < (netc_behind(&netc) > 20 ? 3000 : MAX_CATCHUP + 25) && !(smoke && game.tick >= 500) &&
                       netc_tick(&netc, net_next, set)) {
                    static long nudge_at2 = -2;
                    if (nudge_at2 == -2) nudge_at2 = getenv("OFP_TEST_DESYNC") ? atol(getenv("OFP_TEST_DESYNC")) : -1;
                    game_tick(&game, set);
                    if (nudge_at2 >= 0 && (long)game.tick == nudge_at2 && !desync_written) game.tanks[0].score += 7;
                    keep_live();
                    if (recording) replay_tick(&rec, set, &game);
                    if ((net_next + 1) % NET_CHECK_EVERY == 0 && netc.slot != 255) netc_check(&netc, net_next, game_checksum(&game));
                    net_next++;
                    ticks++;
                }
                net_play = 0;
                alpha = 256;
            } else {
                /* a steady 50 a second, a couple of ticks behind the newest: uneven arrival
                 * doesn't show; a little faster when too many wait, a little slower when few do */
                double rate = ready > NET_BUFFER + 2 ? 1.06 : ready < NET_BUFFER ? 0.94 : 1.0;
                net_play += frame_dt * rate;
                while (net_play >= tick_s && ticks < MAX_CATCHUP && netc_tick(&netc, net_next, set)) {
                    static long nudge_at = -2;                 /* (tests: OFP_TEST_DESYNC=T nudges this game at tick T) */
                    if (nudge_at == -2) nudge_at = getenv("OFP_TEST_DESYNC") ? atol(getenv("OFP_TEST_DESYNC")) : -1;
                    game_tick(&game, set);
                    if (nudge_at >= 0 && (long)game.tick == nudge_at && !desync_written) game.tanks[0].score += 7;
                    keep_live();
                    if (recording) replay_tick(&rec, set, &game);
                    if ((net_next + 1) % NET_CHECK_EVERY == 0 && netc.slot != 255) netc_check(&netc, net_next, game_checksum(&game));
                    net_next++;
                    ticks++;
                    net_play -= tick_s;
                }
                if (net_play > tick_s) net_play = tick_s;   /* (none here yet: wait on the last one, don't save up) */
                if (steady_test && game.tick > 100) {        /* (the test's tally: is it even?) */
                    st_frames++;
                    if (ticks == 0 && net_play >= tick_s) st_stalls++;   /* due, and nothing here */
                    if (ticks > st_most) st_most = ticks;
                    if (ready < st_lo) st_lo = ready;
                    if (ready > st_hi) st_hi = ready;
                    if (game.tick >= 480 && st_frames > 1) {
                        printf("steady: %d frames, %d stalls, at most %d ticks in a frame, %d to %d waiting\n",
                               st_frames, st_stalls, st_most, st_lo, st_hi);
                        st_frames = -100000;                 /* (once) */
                    }
                }
                alpha = (int)(net_play / tick_s * 256.0);  /* smooth in between, as in local play */
                if (alpha > 256) alpha = 256;
                if (alpha < 0) alpha = 0;
            }
            if (net_two) {                              /* the other connections hear the same ticks: keep them current */
                InputBits drop[MAX_TANKS];
                int kx;
                for (kx = 0; kx < net_extra; kx++)
                    while (net_nextx[kx] < net_next && netc_tick(&netx[kx], net_nextx[kx], drop)) net_nextx[kx]++;
            }
            if (netc_take_restart(&netc)) {             /* out of step: the match again from the start */
                if (netc.desync_tick && recording && !desync_written) {   /* first, what we had there: a report */
                    static Game scratch;
                    char path[1300], stamp[32];
                    time_t when = time(NULL);
                    FILE *df;
                    desync_written = 1;
                    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&when));
                    replay_path(path, sizeof path, "");
                    snprintf(path + strlen(path), sizeof path - strlen(path), "desync-%s-tick%u-game.txt", stamp, (unsigned)netc.desync_tick);
                    if ((df = fopen(path, "w")) != NULL) {   /* this build's rules, from the start: what we compute */
                        replay_dump_at(&rec, netc.desync_tick + 1, &scratch, df);
                        fclose(df);
                        fprintf(stderr, "out of step at tick %u: our state there is in %s; the server keeps its own\n",
                                (unsigned)netc.desync_tick, path);
                    }
                    {   /* and as it really was in this run (catches what a replay wouldn't: a one-off) */
                        int k;
                        for (k = 0; k < 4; k++)
                            if (live_tick[k] == netc.desync_tick + 1) {
                                char *dot = strstr(path, "-game.txt");
                                if (dot) snprintf(dot, (size_t)(path + sizeof path - dot), "-game-live.txt");
                                if ((df = fopen(path, "w")) != NULL) { game_dump(&live_ring[k], df); fclose(df); }
                            }
                    }
                }
                match_start(&game, &netc.ms, netc.map_len ? netc.map : NULL);
                net_next = 0;
                fx_reset();
            }
            if (game_finished() && over_frames <= REPORT_DELAY) {
                if (over_frames == 0 && !smoke) {
                    int mine[2] = { me_tank, me_tank2 };
                    ach_match_over(me_tank, me_tank2);   /* (a network match: ours) */
                    stats_match_over(mine, 2);
                }
                over_frames++;
            }
            if (smoke && game.tick >= 500) running = 0;
        } else if (smoke) {
            InputBits in[MAX_TANKS] = { 0 };
            in[0] = smoke_script(game.tick);
            for (i = 1; i < humans; i++) in[i] = smoke_script(game.tick + 37u * (uint32_t)i);
            game_tick(&game, in);
            if (getenv("OFP_TEST_GATE")) {               /* (pictures: tank 1 that many tiles inside the camp's east gate) */
                const Base *cb = &game.bases[TEAM_PLAYER];
                int gd = atoi(getenv("OFP_TEST_GATE"));
                game.tanks[0].x = game.tanks[0].prev_x = FX((cb->x1 - gd) * TILE_SIZE + TILE_SIZE / 2);
                game.tanks[0].y = game.tanks[0].prev_y = FX((cb->y0 + 21) * TILE_SIZE + TILE_SIZE);
            }
            if (getenv("OFP_TEST_PAUSE_MENU")) { pmenu = 1; pmenu_sel = PM_MUSIC; }   /* (pictures: the pause menu) */
            if (getenv("OFP_TEST_CANOPY")) {             /* (pictures: trees over the first two tanks, wherever they are) */
                int k2, dx, dy;
                for (k2 = 0; k2 < 2 && k2 < game.num_tanks; k2++) {
                    int tx = fx_to_int(game.tanks[k2].x) >> TILE_SHIFT, ty = fx_to_int(game.tanks[k2].y) >> TILE_SHIFT;
                    for (dy = -1; dy <= 1; dy++) for (dx = -2; dx <= 2; dx++) map_set(&game.map, tx + dx, ty + dy, TILE_CANOPY);
                }
            }
            if (recording) replay_tick(&rec, in, &game);
            if (smoke == 2 && game.tick == 300) {              /* pretend the last tank just died */
                game.tanks[0].lives = 0; game.tanks[0].out = 1; game.tanks[0].lost = 5;
                game.tanks[0].respawn = 1; over_frames = REPORT_DELAY + 1;
            }
            if (game.tick >= 500) running = 0;
        } else if (replay_view && screen == SCREEN_PLAY) {   /* watching a replay */
            InputBits in[MAX_TANKS];
            double step = tick_s / (rv_speed ? rv_speed : 1);
            if (!rv_speed) acc = 0.0;
            while (rv_speed && acc >= step && rv_tick < view_rep.ticks && ticks < MAX_CATCHUP * 16) {
                rv_feed(rv_tick, in);
                game_tick(&game, in);
                rv_tick++;
                acc -= step;
                ticks++;
            }
            if (rv_tick >= view_rep.ticks) {
                acc = 0.0;
                if (smoke_replay && running) {
                    printf("replay ok: %u ticks, checksum %08x, %s\n", (unsigned)rv_tick, (unsigned)game_checksum(&game),
                           game.winner == TEAM_PLAYER ? "the camp won" : game.winner == TEAM_ENEMY ? "the fortress won" :
                           game.time_up ? "time up, a draw" : "no winner");
                    running = 0;
                }
            }
            alpha = rv_speed ? (int)(acc / step * 256.0) : 256;
            if (alpha > 256) alpha = 256;
            if ((game_finished() || rv_tick >= view_rep.ticks) && over_frames <= REPORT_DELAY) over_frames++;
        } else if (screen == SCREEN_PLAY && signin_busy()) {
            signin_frame();
            acc = 0.0;
        } else if (paused || (screen == SCREEN_PLAY && over_frames > REPORT_DELAY)) {
            acc = 0.0;                                   /* (paused; or the report's up: the match stands still, as it ended) */
        } else {
            while (acc >= tick_s && ticks < MAX_CATCHUP) {
                InputBits in[MAX_TANKS] = { 0 };
                int p;
                if (humans >= 2) for (p = 0; p < humans && p < MAX_TANKS; p++) in[p] = input_player(p, humans);
                else in[0] = input_player(0, 1);  /* sampled once per tick, as in network play */
                if (pmenu) in[0] = 0;                        /* (the menu's open: player 1's tank waits) */
                if (smoke_training) {                       /* (the test: drive out, fire, lay a mine) */
                    uint32_t tk = game.tick;
                    in[0] = (InputBits)(tk < 150 ? IN_UP : tk < 400 ? (IN_FIRE | ((tk / 40) % 2 ? IN_LEFT : 0)) : tk < 420 ? IN_MINE : 0);
                    if (tk == 600) {
                        fx ax, ay;
                        char tl[3][64];
                        int aims = train_target(&ax, &ay);
                        printf("training: step %d of 6 reached (%s); the arrow %s\n", train_step + 1, (train_lines(train_step, tl), tl[0]),
                               aims ? "points the way" : "has nothing to point at");
                        {   /* the training ground: nothing that fights back */
                            int q, guns = 0, pads = 0, pows = 0;
                            for (q = 0; q < game.map.num_st; q++) {
                                const Structure *st = &game.map.st[q];
                                guns += (struct_info[st->type].flags & SF_TURRET) != 0;
                                pads += st->type == ST_HELIPAD;
                                pows += st->type == ST_POWCAMP;
                            }
                            int helis = 0;
                            for (q = 0; q < MAX_HELIS; q++) helis += game.helis[q].active;
                            printf("training ground: %dx%d, %d guns, %d helipads, %d helicopters, %d prison camp(s), %d enemy tanks\n",
                                   game.map.w, game.map.h, guns, pads, helis, pows, game.num_tanks - 1);
                            aims = aims && !guns && !pads && !helis && pows == 1;
                        }
                        printf(train_step >= TR_TREES && (train_step != TR_TREES || aims) ? "training ok\n" : "training: FAILED\n");   /* (to the trees: the arrow shows them) */
                        running = 0;
                    }
                }
                game_tick(&game, in);
                if (operation.phase) operation_step(&game, &operation);
                if (recording) replay_tick(&rec, in, &game);
                acc -= tick_s;
                ticks++;
            }
            if (ticks == MAX_CATCHUP) acc = 0.0;  /* fell behind (debugger, sleep): don't spiral */
            alpha = (int)(acc / tick_s * 256.0);
            if (alpha > 256) alpha = 256;
            if (game_finished() && over_frames <= REPORT_DELAY) {
                if (over_frames == 0 && !smoke && !editor_test_active && !operation.phase) {
                    int mine[4] = { 0, 1, 2, 3 };
                    ach_match_over(0, humans > 1 ? 1 : -1);   /* (the players here) */
                    if (!training && !daily) stats_match_over(mine, humans);
                }
                if (over_frames == 0 && daily && game.winner == TEAM_PLAYER && !no_save) daily_send();
                if (over_frames == 0 && daily && game.winner != TEAM_PLAYER) snprintf(daily_result, sizeof daily_result, "DAILY: NOT THIS TIME. F1: AGAIN");
                if (over_frames == 0 && game.adapt && !no_save) { cfg.adapt_skill = game.adapt_skill; save_cfg(&cfg); }
                over_frames++;                                      /* then the final report */
            }
        }
        steer_mode = steer;
        if (screen == SCREEN_PLAY && !paused && !signin_busy()) {
            int p;
            fx_update(&game, smoke ? (float)tick_s : (float)frame_dt);
            if (replay_view) { if (rv_speed == 1) audio_update(&game, rv_tank); else audio_quiet(); }
            else if (net_on) audio_update(&game, me_tank);
            else audio_update_n(&game, humans);
            for (p = 0; p < humans && !net_on && !replay_view; p++) {   /* feel it: rumble when hit, a big one when the tank goes */
                const Tank *me = &game.tanks[p];
                if (last_lives[p] >= 0 && me->lives < last_lives[p]) input_rumble_player(p, humans, 60000, 60000, 600);
                else if (last_armor[p] >= 0 && me->armor < last_armor[p] && !me->respawn)
                    input_rumble_player(p, humans, 12000 + (last_armor[p] - me->armor) * 3000,
                                        20000 + (last_armor[p] - me->armor) * 4000, 120);
                last_armor[p] = me->armor;
                last_lives[p] = me->lives;
            }
        } else {
            audio_quiet();
            for (i = 0; i < 4; i++) last_armor[i] = last_lives[i] = -1;
        }

        {   /* three or four players get a screen twice the size each way */
            int want = screen == SCREEN_PLAY && ((humans >= 3 && !net_on && !replay_view) ||
                                                 (net_on && net_extra >= 2 && netc.slot != 255));   /* (3 or 4 of us here, online) */   /* (split screen here only:
                                                     a network match's or a replay's players aren't all at this machine) */
            if (want != big_screen) {
                big_screen = want;
                ui_set_logical(ren, LOGICAL_W * (want ? 2 : 1), LOGICAL_H * (want ? 2 : 1), !want);
            }
        }
        ui_set_backdrop(styles[sel.style].backdrop);      /* terrain sets the mood */
        if (screen == SCREEN_MENU && menu.page != PAGE_ABOUT && !smoke && SDL_GetTicks() - idle_since > ATTRACT_AFTER_MS) {
            screen = SCREEN_TITLE;                          /* nobody here: show the battle */
            demo_start(styles[sel.style].biome);
        }
        if (screen == SCREEN_MENU) {                       /* signing in and out (the first page); who's in plays */
            static int was_in;
            if (menu.page == PAGE_MAIN || signin_busy()) signin_frame();
            if (was_in > 0 && signin_count() == 0 && !signin_busy()) {   /* everyone signed out: the title screen again */
                menu_reset(&menu);
                screen = SCREEN_TITLE;
                demo_start(styles[sel.style].biome);
            }
            was_in = signin_count();
            if (from_title && !signin_busy()) {          /* the first window closed: in, or back to the title */
                from_title = 0;
                if (signin_count() == 0) screen = SCREEN_TITLE;
            }
            if (signin_count() > 0 && game_modes[cfg.mode].players != signin_count()) menu_fit_players(&cfg, &menv, signin_count());
        }
        if (screen == SCREEN_PLAY && (replay_view || (net_on && netc.slot == 255))) spec_move();   /* watching */
        else spec_free = spec_overview = 0;
        ui_screen_target(ren);                          /* each frame: the screen, at its scaling, cleared */
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        if (num_styles) {                               /* the sides in the world's tanks' colours (a match: its own world) */
            int ws = screen == SCREEN_PLAY ? cur_style : sel.style;
            render_set_style_teams(&styles[ws >= 0 && ws < num_styles ? ws : 0]);
        }
        if (screen == SCREEN_GALLERY) {                 /* the web site's maps */
            gallery_draw(ren, &gallery, cfg.listing, LOGICAL_W, LOGICAL_H, frame);
        } else if (screen == SCREEN_LOBBY) {            /* the server's lobby */
            lobby_draw(ren, &netc, netx, net_extra, &lobby_ui, styles[sel.style].tanks, hosting, LOGICAL_W, LOGICAL_H);
            signin_draw_picker(ren, LOGICAL_W, LOGICAL_H);
        } else if (screen == SCREEN_BROWSER) {
            browser_draw(ren, &browser, cfg.listing, LOGICAL_W, LOGICAL_H);
            frame_drawn = 1;
        } else if (screen == SCREEN_TITLE) {
            SDL_Rect full = { 0, 0, LOGICAL_W, LOGICAL_H };
            int pulse = (int)(160 + 95 * SDL_sinf((float)SDL_GetTicks() / 900.0f));   /* a slow glow, not a blink */
            demo_run(smoke ? 1.0 / 50 : frame_dt, styles[sel.style].biome);
            if (smoke == 5 && demo.tick >= 3000) running = 0;   /* a minute of battle, then stop */
            render_view(ren, &theme, &styles[sel.style], &demo, &full, demo_follow, 256, -1);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            {   /* darken the top and bottom for the words */
                SDL_Rect band = { 0, 0, LOGICAL_W, 62 };
                SDL_SetRenderDrawColor(ren, 0, 0, 0, 170);
                SDL_RenderFillRect(ren, &band);
                band.y = LOGICAL_H - 50; band.h = 50;
                SDL_RenderFillRect(ren, &band);
            }
            ui_text_shadow(ren, (LOGICAL_W - ui_text_width("OPENFP", 3) - 3) / 2, 10, 3,
                           (Rgb){ 250, 196, 64 }, "OPENFP");
            ui_text(ren, (LOGICAL_W - ui_text_width("AN OPEN REMAKE OF THE 1987 CLASSIC", 1)) / 2, 44, 1,
                    (Rgb){ 204, 196, 170 }, "AN OPEN REMAKE OF THE 1987 CLASSIC");
            ui_text_shadow(ren, (LOGICAL_W - ui_text_width("PRESS FIRE", 2) - 2) / 2, LOGICAL_H - 43, 2,
                           (Rgb){ (Uint8)pulse, (Uint8)(pulse * 3 / 4), 40 }, "PRESS FIRE");
            ui_text(ren, (LOGICAL_W - ui_text_width("ENTER OR CONTROLLER A TO SIGN IN", 1)) / 2, LOGICAL_H - 23, 1,
                    UI_WHITE, "ENTER OR CONTROLLER A TO SIGN IN");
            ui_text(ren, (LOGICAL_W - ui_text_width("ORIGINAL GAME BY SILENT SOFTWARE, 1987", 1)) / 2, LOGICAL_H - 12, 1,
                    (Rgb){ 120, 116, 100 }, "ORIGINAL GAME BY SILENT SOFTWARE, 1987");
        } else if (screen == SCREEN_EDITOR) {
            int r;
            InputBits ed_nav = menu_input();
            if (smoke == 4) {
                unsigned step = ed.frame;
                if (ed.g->map.custom_bases) {
                    if (step < 3) ed_key = SDLK_TAB;
                    if (step >= 3 && step <= 5) ed_key = SDLK_e;
                    if (step == 6 || step == 8 || step == 13 || step == 15) {
                        const Base *b = &ed.g->bases[step < 10 ? TEAM_PLAYER : TEAM_ENEMY];
                        ed.cx = b->x0 + ((step == 6 || step == 13) ? 5 : 12);
                        ed.cy = b->y0 + 5;
                        ed_nav = IN_FIRE;
                    }
                }
                if (step == 40) ed_key = SDLK_F9;
            }
            {   /* a pad: the triggers step through the tools; the right stick scrolls the map */
                static int lt0, rt0, scroll_t;
                int rx, ry, lt, rt;
                input_pads_extra(&rx, &ry, &lt, &rt);
                if (!ed_key && rt && !rt0) ed_key = SDLK_e;          /* RT: the next one */
                else if (!ed_key && lt && !lt0) ed_key = SDLK_q;     /* LT: the one before */
                lt0 = lt; rt0 = rt;
                if (abs(rx) <= 12000 && abs(ry) <= 12000) ed_stick_wait = 0;
                if (!ed_stick_wait && !ed.menu && (abs(rx) > 12000 || abs(ry) > 12000) && ++scroll_t >= 2) {
                    int sp = abs(rx) > 28000 || abs(ry) > 28000 ? 3 : 1;
                    editor_scroll(&ed, abs(rx) > 12000 ? (rx > 0 ? sp : -sp) : 0, abs(ry) > 12000 ? (ry > 0 ? sp : -sp) : 0);
                    scroll_t = 0;
                }
            }
            r = editor_update(&ed, ed_nav, ed_key, mouse_x, mouse_y, mouse_b, wheel);   /* (a pad's: the menus' way) */
            ed_key = 0; wheel = 0;
            if (r == ED_SAVED) {                           /* saved: it's on the list now */
                num_maps = maps_scan(maps, MAPS_MAX);
                menv.num_maps = num_maps;
                snprintf(cfg.map, sizeof cfg.map, "%s", ed.path);
                save_cfg(&cfg);
            }
            if (r == ED_QUIT) { screen = SCREEN_MENU; menu_reset(&menu); }
            if (r == ED_PLAY || r == ED_PLAY_HERE) {
                MatchSetup setup = {0};
                const char *text = editor_test_map(&ed);
                int k;
                setup.seed = ed.seed;
                setup.versus = 1;
                setup.players = 2;
                setup.cls[0] = setup.cls[1] = TANK_MEDIUM;
                setup.team[1] = TEAM_ENEMY;
                setup.steer[0] = signin_count() ? signin_steer(0) : steer_mode;
                setup.bot[1] = AI_NORMAL;
                if (ed.g->map.arena) setup.extras = EXTRA_ANNIHILATION;
                if (text && match_start(&game, &setup, text) == 0) {
                    editor_old_style = cur_style;
                    for (k = 0; k < num_styles; k++) if (styles[k].biome == game.biome) { cur_style = k; break; }
                    apply_world_rules(&styles[cur_style]);
                    if (r == ED_PLAY_HERE) {
                        int dx, dy, clear = 1;
                        for (dy = -1; dy <= 1; dy++)
                            for (dx = -1; dx <= 1; dx++) {
                                int x = ed.cx + dx, y = ed.cy + dy;
                                if ((tile_info[map_get(&game.map, x, y)].flags & TF_SOLID) ||
                                    (x >= 0 && y >= 0 && x < game.map.w && y < game.map.h && game.map.sid[y][x])) clear = 0;
                            }
                        if (clear) {
                            Tank *t = &game.tanks[0];
                            t->x = t->prev_x = FX(ed.cx * TILE_SIZE + TILE_SIZE / 2);
                            t->y = t->prev_y = FX(ed.cy * TILE_SIZE + TILE_SIZE / 2);
                        }
                    }
                    editor_test_active = 1;
                    ed.menu = 0;
                    screen = SCREEN_PLAY;
                    humans = 1;
                    play_mode = MODE_SOLO;
                    custom_teams = training = daily = 0;
                    frame = over_frames = 0;
                    paused = pmenu = 0;
                    acc = 0.0;
                    fx_reset();
                    audio_reset();
                } else editor_notice(&ed, "CAN'T START TEST MAP");
            }
            if (r == ED_SHARE) {                            /* F8: to the web site's map gallery */
                char *text = maps_read(ed.path), *body = NULL, resp[200], url[400];
                size_t cap = 0;
                int st;
                snprintf(url, sizeof url, "%.380s", cfg.listing);
                http_form_grow(&body, &cap, "action", "map_upload");
                http_form_grow(&body, &cap, "author", cfg.player_name);
                if (text) http_form_grow(&body, &cap, "map", text);
                st = text && body ? http_request(url, body, resp, sizeof resp, 8000) : -1;
                editor_notice(&ed, st == 200 ? "SHARED: SEE MAPS ON THE WEB SITE" : st == 429 ? "TEN MAPS A DAY, PLEASE"
                                 : st < 0 ? "THE WEB SITE DOESN'T ANSWER" : "THE WEB SITE DIDN'T TAKE IT");
                free(text);
                free(body);
            }
            if (screen == SCREEN_EDITOR) {               /* (drawn in its world's own style: changing the world changes the look) */
                int es = sel.style, k3;
                if (num_styles && styles[es].biome != ed.g->biome)
                    for (k3 = 0; k3 < num_styles; k3++) if (styles[k3].biome == ed.g->biome) { es = k3; break; }
                editor_draw(ren, &theme, &styles[es], &ed);
            }
            else if (screen == SCREEN_SELECT) select_draw(ren, &theme, styles, &sel, LOGICAL_W * (big_screen ? 2 : 1), LOGICAL_H * (big_screen ? 2 : 1));
            else menu_draw(ren, &theme, &menu, &cfg, &menv, LOGICAL_W, LOGICAL_H);
        } else if (screen == SCREEN_MENU && smoke == 4) {  /* straight into the editor (tests: a map of OFP_TEST_EDIT_MAP's) */
            static char et_text[MAPFILE_MAX];
            const char *et_path = getenv("OFP_TEST_EDIT_MAP");
            FILE *ef = et_path ? fopen(et_path, "rb") : NULL;
            size_t en = ef ? fread(et_text, 1, sizeof et_text - 1, ef) : 0;
            if (ef) fclose(ef);
            et_text[en] = 0;
            editor_open(&ed, &editor_game, en ? et_text : NULL, NULL, 0, 99, BIOME_CLASSIC);
            screen = SCREEN_EDITOR;
            menu_draw(ren, &theme, &menu, &cfg, &menv, LOGICAL_W, LOGICAL_H);
        } else if (screen == SCREEN_MENU && smoke && menu_r != MENU_HOST && menu_r != MENU_JOIN) {   /* a scripted walk through the menu */
            static const struct { int at, ok, back; InputBits nav; } walk[] = {
                /* the first page: down to OPTIONS, the close-up view, back */
                { 4, 0, 0, IN_DOWN }, { 5, 0, 0, IN_DOWN }, { 6, 0, 0, IN_DOWN }, { 7, 0, 0, IN_DOWN }, { 8, 0, 0, IN_DOWN },
                { 10, 1, 0, 0 }, { 12, 0, 0, IN_DOWN }, { 14, 0, 0, IN_RIGHT }, { 16, 0, 1, 0 },
                /* up to LOCAL GAME, in (on START), up to TYPE, then down to TIME: dusk, then START */
                { 17, 0, 0, IN_UP }, { 18, 0, 0, IN_UP }, { 19, 0, 0, IN_UP }, { 20, 0, 0, IN_UP }, { 21, 0, 0, IN_UP },
                { 23, 1, 0, 0 },
                { 24, 0, 0, IN_UP }, { 25, 0, 0, IN_UP }, { 26, 0, 0, IN_UP }, { 27, 0, 0, IN_UP }, { 28, 0, 0, IN_UP },
                { 29, 0, 0, IN_UP },
                { 31, 0, 0, IN_RIGHT },
                { 32, 0, 0, IN_DOWN }, { 33, 0, 0, IN_DOWN }, { 34, 0, 0, IN_DOWN }, { 35, 0, 0, IN_DOWN },
                { 36, 0, 0, IN_RIGHT }, { 37, 0, 0, IN_DOWN }, { 38, 0, 0, IN_DOWN },
                { 41, 1, 0, 0 },
            };
            size_t w;
            for (w = 0; w < sizeof walk / sizeof walk[0]; w++)
                if ((int)frame == walk[w].at) {
                    int rr;
                    menu_update(&menu, &cfg, &menv, 0, 0, 0, 0);
                    rr = menu_update(&menu, &cfg, &menv, walk[w].nav, 0, walk[w].back, walk[w].ok);
                    if (rr == MENU_CHANGED) apply_settings(win, ren, &cfg, &steer, &sel);
                    if (rr == MENU_START) { start_from_menu(&cfg, &sel, &screen); frame = 0; }
                }
            update_preview(&cfg, sel.style);
            if (screen == SCREEN_MENU) {
                menu_draw(ren, &theme, &menu, &cfg, &menv, LOGICAL_W, LOGICAL_H);
                if (menu.page == PAGE_MAIN) signin_draw_bar(ren, 24, 184, 336);   /* who's in */
                signin_draw_picker(ren, LOGICAL_W, LOGICAL_H);
            }
            else select_draw(ren, &theme, styles, &sel, LOGICAL_W * (big_screen ? 2 : 1), LOGICAL_H * (big_screen ? 2 : 1));
        } else if (screen == SCREEN_MENU) {
            if (!menu.frame) {                              /* (re)entering: pick up new map files */
                num_maps = maps_scan(maps, MAPS_MAX);
                menv.num_maps = num_maps;
                menu_update(&menu, &cfg, &menv, 0, 0, 0, 0);
            }
            else if (menu_quiet() || signin_busy()) {        /* wait for release even on frames without a key press */
                menu.prev = 0xFF;                            /* (and what's held when the picker closes isn't the menu's) */
            }
            else {                                           /* the pad's stick and A button too */
                InputBits mi = menu_input();                 /* (player 1's) */
                int r2;
                if (osk_hold && !(mi & IN_FIRE)) osk_hold = 0;
                if (osk.open || osk_hold) mi = 0;           /* (the on-screen keyboard's A isn't the menu's) */
                r2 = menu_with_sound(&menu, &cfg, &menv, mi, 0, 0, 0);
                if (menu_r == MENU_NOTHING) menu_r = r2;
                if (forced_menu) { menu_r = forced_menu; forced_menu = 0; }
            }
            if (menu_r == MENU_QUIT) running = 0;
            if (menu_r == MENU_CHANGED) {
                apply_settings(win, ren, &cfg, &steer, &sel);
                if (menu.page == PAGE_KEYS && signin_count()) keys_page_to_profiles(&cfg, &menu);   /* (theirs, saved) */
            }
            {   /* the KEYS page, opened while signed in: it shows theirs */
                static int keys_open;
                if (screen == SCREEN_MENU && menu.page == PAGE_KEYS) {
                    if (!keys_open && signin_count()) keys_page_from_profiles(&cfg);
                    keys_open = 1;
                } else keys_open = 0;
            }
            if (menu_r == MENU_START) start_from_menu(&cfg, &sel, &screen);
            if (menu_r == MENU_BENCHMARK) {
                snprintf(menu.benchmark, sizeof menu.benchmark, "RUNNING BENCHMARK...");
                menu_draw(ren, &theme, &menu, &cfg, &menv, LOGICAL_W, LOGICAL_H);
                SDL_RenderPresent(ren);
                run_benchmark(ren, &theme, menu.benchmark);
                idle_since = SDL_GetTicks();
                last = SDL_GetPerformanceCounter();
                acc = 0;
            }
            if (menu_r == MENU_QUICK) {                     /* QUICK MATCH: find the best open game, and join */
                int pick;
                memset(&browser, 0, sizeof browser);
                browser_lists(&browser, &cfg);
                browser.need = signin_count() > 0 ? signin_count() : cfg.net_locals;   /* (room for everyone here) */
                snprintf(browser.want_goal, sizeof browser.want_goal, "%s", quick_goal_key[cfg.quick_goal % QUICK_GOALS]);
                menu_notice(&menu, "LOOKING FOR A GAME...");
                menu_draw(ren, &theme, &menu, &cfg, &menv, LOGICAL_W, LOGICAL_H);
                SDL_RenderPresent(ren);
                browser.my_rating = browser_my_rating(cfg.listing, cfg.player_name);   /* (rated: a close game first) */
                if (browser_fetch(&browser, cfg.listing) != 0) menu_notice(&menu, "THE SERVER LIST DOESN'T ANSWER");
                else if ((pick = browser_pick(&browser)) < 0) menu_notice(&menu, "NO COMPATIBLE OPEN GAME WITH ROOM");
                else {
                    if (browser.want_goal[0] && strcmp(browser.e[pick].goal, browser.want_goal))   /* (none in that mode: the best other) */
                        menu_notice(&menu, "NONE IN THAT MODE: THE BEST OTHER GAME");
                    snprintf(cfg.server, sizeof cfg.server, "%.50s:%d", browser.e[pick].host, browser.e[pick].port);
                    cfg.server_password[0] = 0;
                    menu_r = MENU_JOIN;                     /* (handled just below, as from the menu) */
                }
            }
            if (menu_r == MENU_FIND) {                      /* the server list */
                memset(&browser, 0, sizeof browser);
                snprintf(browser.status, sizeof browser.status, "asking the listing");
                browser_due = 1;
                frame_drawn = 0;
                save_cfg(&cfg);
                browser.nmine = cfg.n_mine;                 /* (MY SERVERS: the settings') */
                memcpy(browser.mine, cfg.mine, sizeof browser.mine); memcpy(browser.mine_pw, cfg.mine_pw, sizeof browser.mine_pw);
                if (browser.mine_sel >= browser.nmine) browser.mine_sel = 0;
                screen = SCREEN_BROWSER;
            }
            if (menu_r == MENU_HOST || menu_r == MENU_JOIN) {   /* network: host here, or join someone */
                char host[64], *colon;
                char join_code[8] = "";
                int port = NET_PORT;
                leave_net();
                int cls = sel.choices[0];
                lobby_ui.line[0] = 0;
                lobby_ui.seat = lobby_ui.show_scores = lobby_ui.slap_click_id = lobby_ui.page = lobby_ui.action = lobby_ui.typing = 0;
                if (menu_r == MENU_HOST) {                  /* a server right here, for the game on the main page */
                    ServerConfig sc;
                    int mp = cfg.mode;
                    char *map = cfg.map[0] ? maps_read(cfg.map) : NULL;
                    int mi = maps_find(maps, num_maps, cfg.map);
                    server_default_config(&sc);
                    snprintf(sc.name, sizeof sc.name, "%.20s's game", cfg.player_name);
                    snprintf(sc.motd, sizeof sc.motd, "Hosted in the game. Press F1 when you're ready.");
                    sc.max_players = game_modes[mp].players;
                    sc.min_players = game_modes[mp].players;
                    sc.countdown_ms = 3000;
                    sc.bots = cfg.host_bots;                    /* computer players in the free places */
                    sc.bot_level = cfg.ai_level ? cfg.ai_level : AI_NORMAL;
                    sc.base.versus = smoke_pad || game_modes[mp].versus;
                    sc.base.ai_level = (uint8_t)cfg.ai_level;
                    sc.base.biome = (uint8_t)styles[sel.style].biome;
                    sc.base.time = (uint8_t)cfg.night;
                    sc.base.camp_garages = (uint8_t)(styles[sel.style].faithful ? 3 : 4);
                    sc.base.seed = cfg.map[0] ? 0 : preview_seed;
                    sc.map = map;
                    if (mi >= 0) snprintf(sc.map_name, sizeof sc.map_name, "%.31s", maps[mi].name);
                    /* HOST SETTINGS */
                    sc.base.time_limit = (uint16_t)(cfg.host_time * 60);
                    sc.base.lives = (uint8_t)cfg.match_lives;
                    sc.base.no_powerups = !cfg.powerups;
                    sc.base.weather = (uint8_t)cfg.weather;
                    sc.base.extras = (uint8_t)cfg.extras;
                    sc.weather_mode = cfg.weather;
                    sc.base.time_rule = (uint8_t)cfg.host_rule;
                    sc.start_after_ms = cfg.host_start * 1000;
                    if (cfg.host_start) sc.min_players = 1;      /* (then whoever's ready plays) */
                    sc.allow_watching = cfg.host_watch != 2;
                    sc.spectator_delay_s = cfg.host_watch == 1 ? 30 : 0;
                    if (cfg.host_maps) {                         /* a rotation: the world's maps */
                        const Style *hs = &styles[sel.style];
                        int k;
                        sc.rotation = cfg.host_maps == 2 ? ROT_RANDOM : cfg.host_maps == 3 ? ROT_VOTE : ROT_ORDER;
                        for (k = 0; k < num_maps && sc.nmaps < NET_MAPS; k++) {
                            char *t;
                            if (maps[k].biome != hs->biome || (hs->faithful && !maps[k].original)) continue;
                            if ((t = maps_read(maps[k].path)) == NULL) continue;
                            sc.maps[sc.nmaps] = t;
                            snprintf(sc.map_names[sc.nmaps], sizeof sc.map_names[0], "%.31s", maps[k].name);
                            sc.nmaps++;
                        }
                    }
                    if (server_open(&host_srv, NET_PORT, &sc, NULL) == 0) {
                        hosting = 1;
                        snprintf(host, sizeof host, "127.0.0.1");
                    } else {
                        menu_notice(&menu, "PORT 29870 IS IN USE");
                        host[0] = 0;
                    }
                    { int k; for (k = 0; k < sc.nmaps; k++) free((char *)sc.maps[k]); }   /* (the server has its own copies) */
                    free(map);
                } else {
                    snprintf(host, sizeof host, "%s", cfg.server);
                    { char split[64]; net_split(cfg.server, split, sizeof split, &port); snprintf(host, sizeof host, "%s", split); (void)colon; }
                    if (is_join_code(host) && cfg.listing[0]) {   /* a join code: the listing says where that server is */
                        char url[400], resp[200], h2[130], h3[130];
                        int p2 = 0, p3 = 0;
                        snprintf(url, sizeof url, "%.300s%saction=join&code=%s", cfg.listing, strchr(cfg.listing, '?') ? "&" : "?", host);
                        if (http_request(url, NULL, resp, sizeof resp, 4000) == 200 && sscanf(resp, "%129s %d %129s %d", h2, &p2, h3, &p3) >= 2 && p2 > 0) {
                            if (p3 > 0) {
                                snprintf(join_code, sizeof join_code, "%.6s", host);
                                snprintf(host, sizeof host, "%.63s", h3);
                                port = p3;                         /* how the internet sees it */
                            }
                            else { snprintf(host, sizeof host, "%.63s", h2); port = p2; }
                        } else {
                            menu_notice(&menu, "NO SERVER WITH THAT JOIN CODE");
                            host[0] = 0;
                        }
                    }
                    save_cfg(&cfg);
                }
                if (host[0]) {
                    if (!cfg.have_key) {                        /* my key for servers: made once, kept */
                        net_random(cfg.player_key, 32);
                        cfg.have_key = 1;
                        save_cfg(&cfg);
                    }
                    snprintf(net_host, sizeof net_host, "%s", host);
                    join_port = port;
                    snprintf(net_pass, sizeof net_pass, "%s", hosting ? "" : cfg.server_password);
                    if (signin_count() > 0) {                       /* signed in: everyone in plays, as themselves */
                        cfg.net_locals = signin_count();
                        cls = tank_chassis(signin_tank(0));
                        signin_assign_inputs();
                    }
                    if (netc_open(&netc, host, (uint16_t)port, signin_count() > 0 ? signin_name(0) : cfg.player_name, net_pass,
                                  smoke ? NULL : signin_count() > 0 ? signin_key(0) : cfg.player_key, cls,
                                  signin_count() > 0 ? signin_steer(0) : steer) == 0) {   /* (the steering: player 1's own) */
                        if (signin_count() > 0) netc.portrait = signin_avatar(0) + 1;
                        netc.wait_for_place = 1;                 /* (a full server: wait in its queue; Esc gives up) */
                        snprintf(netc.cache_dir, sizeof netc.cache_dir, "%.500s", maps_user_dir());   /* keep downloaded maps */
                        if (netc.cache_dir[0] && netc.cache_dir[strlen(netc.cache_dir) - 1] == '/')
                            netc.cache_dir[strlen(netc.cache_dir) - 1] = 0;
                        {   /* players 2, 3, 4 here: a connection each, a key each (from mine), a name each */
                            uint8_t keyx[32];
                            int kx;
                            net_extra = 0;
                            memcpy(keyx, cfg.player_key, 32);
                            for (kx = 0; kx + 1 < cfg.net_locals && kx < 3; kx++) {
                                char namex[NET_NAME];
                                sha256(keyx, 32, keyx);
                                snprintf(namex, sizeof namex, "%.12s+%d", cfg.player_name, kx + 2);
                                int extra_cls = (cls + kx + 1) % TANK_CLASS_COUNT;
                                if (kx + 1 < signin_count()) {             /* (signed in: their own name, key, tank) */
                                    snprintf(namex, sizeof namex, "%.15s", signin_name(kx + 1));
                                    memcpy(keyx, signin_key(kx + 1), 32);
                                    extra_cls = tank_chassis(signin_tank(kx + 1));
                                }
                                if (netc_open(&netx[kx], host, (uint16_t)port, namex, net_pass, smoke ? NULL : keyx,
                                              extra_cls, kx + 1 < signin_count() ? signin_steer(kx + 1) : steer) != 0) break;
                                if (kx + 1 < signin_count()) netx[kx].portrait = signin_avatar(kx + 1) + 1;
                                net_extra = kx + 1;
                            }
                        }
                        if (join_code[0]) {
                            NetClient *clients[4] = { &netc, &netx[0], &netx[1], &netx[2] };
                            netc_punch(clients, 1 + net_extra, cfg.listing, join_code, NET_STUN_DEFAULT, 1500);
                        }
                        screen = SCREEN_LOBBY;
                        if (smoke && !smoke_pad) {                  /* a test run: ready at once */
                            netc_ready(&netc, 1, netc.want_cls);
                            { int kx; for (kx = 0; kx < net_extra; kx++) netc_ready(&netx[kx], 1, netx[kx].want_cls); }
                        }
                    } else { menu_notice(&menu, netc.status); leave_net(); }
                }
            }
            if (menu_r == MENU_IMPORT) {                    /* look for the disk image */
                char msg[96];
                original_import_search(msg, sizeof msg);
                num_maps = maps_scan(maps, MAPS_MAX);
                menv.num_maps = num_maps;
                menu_notice(&menu, msg);
            }
            if (menu_r == MENU_DAILY) {                     /* today's challenge */
                start_daily();
                apply_world_rules(&styles[cur_style]);
                sel.style = cur_style;
                screen = SCREEN_PLAY;
                over_frames = 0;
                acc = 0.0;
            }
            if (menu_r == MENU_GALLERY) {                   /* the web site's maps */
                menu_notice(&menu, "ASKING THE WEB SITE...");
                menu_draw(ren, &theme, &menu, &cfg, &menv, LOGICAL_W, LOGICAL_H);
                SDL_RenderPresent(ren);
                gallery_fetch(&gallery, cfg.listing);
                screen = SCREEN_GALLERY;
            }
            if (menu_r == MENU_OPERATION || (menu_r >= MENU_CHALLENGE_RESCUE && menu_r <= MENU_CHALLENGE_RAID)) {
                selected_challenge = menu_r == MENU_OPERATION ? -1 : menu_r - MENU_CHALLENGE_RESCUE;
                if (start_desert_operation() == 0) {
                    screen = SCREEN_PLAY;
                    over_frames = 0;
                    acc = 0.0;
                    if (smoke_operation) {
                        exit_status = game.biome != BIOME_DESERT || humans != 2 || game.num_tanks != 2 || operation.phase != OP_GUN;
                        puts(exit_status ? "operation menu FAILED" : "operation menu ok");
                        running = 0;
                    }
                } else {
                    menu_notice(&menu, "COULD NOT START SAND BRIDGE");
                    if (smoke_operation) { puts("operation menu FAILED"); running = 0; exit_status = 1; }
                }
            }
            if (menu_r == MENU_TRAINING) {                  /* the first mission: no enemy tank, a guide */
                int cls[4] = { TANK_MEDIUM, TANK_MEDIUM, TANK_MEDIUM, TANK_MEDIUM }, k;
                train_saved_ai = ai_level;
                ai_level = AI_OFF;
                for (k = 0; k < num_styles; k++) if (styles[k].biome == BIOME_DESERT) { cur_style = sel.style = k; break; }
                play_map[0] = 0;
                custom_teams = 0;                         /* (the training is its own) */
                training = 1;                             /* (before the match: its clear sky, no extras) */
                build_training_ground();
                train_map = train_map_text;
                start_game(2026u, cls, MODE_SOLO, BIOME_CLASSIC);
                train_map = NULL;
                apply_world_rules(&styles[sel.style]);
                for (k = 0; k < game.num_props; k++) game.props[k].moving = 0;   /* (no patrols either) */
                train_clean_up();
                train_step = TR_DRIVE;
                train_x0 = game.tanks[0].x; train_y0 = game.tanks[0].y;
                humans = 1;
                screen = SCREEN_PLAY;
                over_frames = 0;
                acc = 0.0;
            }
            if (menu_r == MENU_REPLAYS) {                   /* the replays: the list */
                rv_scan();
                rv_notice[0] = 0;
                rv_focus = RV_LIST;
                rv_awards = 0;
                rv_keep_visible();
                screen = SCREEN_REPLAYS;
            }
            if (menu_r == MENU_EDIT) {
                editor_open(&ed, &editor_game, NULL, NULL, 0, preview_seed, BIOME_CLASSIC);
                screen = SCREEN_EDITOR;
            }
            menu_r = MENU_NOTHING;
            update_preview(&cfg, sel.style);
            if (preview.map.arena && preview.map.num_spawns)   /* an arena: its spawn points, half a side */
                select_set_caps((preview.map.num_spawns + 1) / 2, preview.map.num_spawns / 2);
            else select_set_caps(preview.bases[TEAM_PLAYER].num_garages, preview.bases[TEAM_ENEMY].num_garages);
            select_set_max_players(classic_rules(&cfg, &styles[sel.style], &preview) ? 2 : 4);
            if (!mode_available(cfg.mode)) {                /* too many players for this map's garages */
                int m, best = MODE_SOLO;
                char note[64];
                for (m = 0; m < MODE_COUNT; m++)
                    if (mode_available(m) && game_modes[m].versus == game_modes[cfg.mode].versus &&
                        game_modes[m].players > game_modes[best].players) best = m;
                if (styles[sel.style].faithful) snprintf(note, sizeof note, "CLASSIC: ONE OR TWO PLAYERS, AS IN 1987");
                else snprintf(note, sizeof note, "ROOM FOR %d + %d TANKS ON THIS MAP",
                              preview.bases[TEAM_PLAYER].num_garages, preview.bases[TEAM_ENEMY].num_garages);
                cfg.mode = sel.mode = best;
                menu_notice(&menu, note);
                save_cfg(&cfg);
            }
            menu_draw(ren, &theme, &menu, &cfg, &menv, LOGICAL_W, LOGICAL_H);
            if (menu.page == PAGE_MAIN) signin_draw_bar(ren, 24, 184, 336);   /* who's in */
            signin_draw_picker(ren, LOGICAL_W, LOGICAL_H);                  /* (over it all, when open) */
        } else if (screen == SCREEN_TEAMS) {
            teams_set_tanks(num_styles && styles[sel.style < num_styles ? sel.style : 0].tanks
                            ? styles[sel.style < num_styles ? sel.style : 0].tanks : theme.tanks);   /* (the world's tanks) */
            teams_draw(ren, &theme, &teams, LOGICAL_W, LOGICAL_H);
            signin_draw_picker(ren, LOGICAL_W, LOGICAL_H);
        } else if (screen == SCREEN_SELECT)
            select_draw(ren, &theme, styles, &sel, LOGICAL_W * (big_screen ? 2 : 1), LOGICAL_H * (big_screen ? 2 : 1));
        else if (screen == SCREEN_REPLAYS)
            draw_replays(ren);
        else {
            paused_now = paused && !pmenu;                 /* (the menu says PAUSED itself) */
            draw_play(ren, &theme, &styles[replay_view ? rv_style : net_on || editor_test_active || operation.phase ? cur_style : sel.style],
                      alpha, frame, over_frames > REPORT_DELAY && !editor_test_active);
            if (editor_test_active) {
                const char *hint = input_pad_count() ? "START / ESC BACK TO EDITOR" : "ESC BACK TO EDITOR";
                ui_fill(ren, 8, LOGICAL_H - 18, ui_text_width(hint, 1) + 10, 12, (Rgb){ 16, 16, 12 });
                ui_text(ren, 13, LOGICAL_H - 15, 1, theme.hud.value, hint);
            }
            if (pmenu && !replay_view) draw_pause_menu(ren, &cfg, zoom, net_on);   /* (over it all) */
            signin_draw_picker(ren, LOGICAL_W, LOGICAL_H);
        }
        static int rv_test;                              /* (the test's own count of passes) */
        if (smoke_replay == 1 && replay_view) rv_test++;
        if (smoke_replay == 1 && replay_view && rv_test == 3) rv_speed = 0;   /* (paused, so the click's result shows) */
        if (smoke_replay == 1 && replay_view && rv_test == 2 && getenv("OFP_TEST_SPEC")) {   /* (tests: the watching views) */
            if (!strcmp(getenv("OFP_TEST_SPEC"), "overview")) spec_overview = 1;
            else { spec_keys(SDLK_f, rv_tank); spec_cx += 300; spec_cy -= 200; }
        }
        if (smoke_replay == 1 && replay_view && rv_test == 4) {   /* (the test: a click halfway along the timeline) */
            SDL_Event click;
            memset(&click, 0, sizeof click);
            click.type = SDL_MOUSEBUTTONDOWN;
            click.button.button = SDL_BUTTON_LEFT;
            click.button.x = 8 + 274 / 2;
            click.button.y = 6 + 204 - 14;
            SDL_PushEvent(&click);
        }
        if (smoke_replay == 1 && replay_view && rv_test == 8)
            printf("timeline: a click halfway went to tick %u of %u; following %s\n", (unsigned)rv_tick, (unsigned)view_rep.ticks,
                   view_rep.names[0][0] ? view_rep.names[0] : "(no name)");
        {   /* a module just started: its title, author and licence, for a few seconds (not over a match) */
            static char now_credit[130];
            static uint32_t credit_until;
            int started = music_started();
            if (started >= 0 && music_credit(started)[0]) {
                snprintf(now_credit, sizeof now_credit, "MUSIC: %s", music_credit(started));
                for (char *q = now_credit; *q; q++) if (*q >= 'a' && *q <= 'z') *q = (char)(*q - 32);
                credit_until = SDL_GetTicks() + 6000;
            }
            if (SDL_GetTicks() < credit_until && screen != SCREEN_PLAY && screen != SCREEN_EDITOR) {
                char l1[130], l2[130];                   /* two lines: the title; who and the licence */
                const char *by = strstr(now_credit, " BY ");
                int w1, w2;
                if (by) { snprintf(l1, sizeof l1, "%.*s", (int)(by - now_credit), now_credit); snprintf(l2, sizeof l2, "%.46s", by + 1); }
                else { snprintf(l1, sizeof l1, "%.46s", now_credit); l2[0] = 0; }
                l1[46] = 0;
                w1 = ui_text_width(l1, 1); w2 = ui_text_width(l2, 1);
                ui_fill(ren, 0, LOGICAL_H - (l2[0] ? 19 : 10), (w1 > w2 ? w1 : w2) + 8, l2[0] ? 19 : 10, (Rgb){ 10, 10, 8 });
                ui_text(ren, 4, LOGICAL_H - (l2[0] ? 18 : 9), 1, ui_colors()->value, l1);
                if (l2[0]) ui_text(ren, 4, LOGICAL_H - 9, 1, UI_GREY, l2);
            }
        }
        if (screen == SCREEN_PLAY && getenv("OFP_TEST_CRASH") && game.tick == 200) raise(SIGSEGV);   /* (tests) */
        if (orders_open && pmenu) draw_orders(ren);
        if (net_on && netc.state == NC_RUNNING && game_orders_allowed(&game)) {
            int p;
            for (p = 0; p <= net_extra && p < MAX_SIGNED; p++) {
                NetClient *nc = p ? &netx[p - 1] : &netc;
                if (nc->slot >= 0 && nc->slot < game.num_tanks) {
                    int team = game.tanks[nc->slot].team & 1;
                    game.team_order[team] = nc->team_order;
                    if (nc->team_order.kind) game.team_leader[team] = (int8_t)nc->team_order.issuer;
                }
            }
        }
        if (osk.open) osk_draw(ren, &osk, LOGICAL_W, LOGICAL_H);   /* the on-screen keyboard, over it all */
        if (smoke_replay == 2 && frames > 3) running = 0;   /* (a test run of the list: a few frames) */
        if (shot && !running) save_shot(ren, shot);
        fx_budget_frame((unsigned)((SDL_GetPerformanceCounter() - fx_started) * 1000 / freq), 20,
                        cfg.auto_effects && screen == SCREEN_PLAY && !paused && !pmenu);
        SDL_RenderPresent(ren);
        if (cfg.power_save && !smoke && !smoke_pad && !smoke_training) {   /* not a match: no need for 120 a second */
            static Uint64 last_frame;
            Uint32 wf = SDL_GetWindowFlags(win);
            int away = (wf & SDL_WINDOW_MINIMIZED) || !(wf & SDL_WINDOW_INPUT_FOCUS);
            int cap = screen == SCREEN_PLAY ? (away && !net_on ? 10 : 0) : (away ? 10 : 30);   /* (0: as fast as the screen) */
            if (cap && screen != SCREEN_TITLE && screen != SCREEN_SELECT && !(screen == SCREEN_MENU && menu.page == PAGE_ABOUT) && !net_on && !hosting && !osk.open &&
                SDL_GetTicks() - last_input > 2000) {
                /* a menu nobody's touching (nothing moves there but the cursor's blink): no frame until something
                 * happens, or a quarter of a second has gone (the menus still update: a server list, a notice) */
                SDL_WaitEventTimeout(NULL, 250);
            } else if (cap) {
                Uint64 now_c = SDL_GetPerformanceCounter(), per_s = SDL_GetPerformanceFrequency();
                double spent = last_frame ? (double)(now_c - last_frame) / (double)per_s : 1.0, want = 1.0 / cap;
                if (spent < want) SDL_Delay((Uint32)((want - spent) * 1000.0));
            }
            last_frame = SDL_GetPerformanceCounter();
        }
        frames++;
        frame++;

        now = SDL_GetPerformanceCounter();
        if (!smoke && now - title_timer >= freq) {
            char title[160];
            snprintf(title, sizeof title, "%s  |  %d fps  |  %s world  |  enemy tank: %s (F7)  |  %s controls%s%s%s",
                     OFP_TITLE, frames, styles[screen == SCREEN_PLAY ? replay_view ? rv_style : cur_style : sel.style].title, ai_names[ai_level],
                     steer == STEER_DIRECT ? "original" : "tank",
                     audio_muted() ? "  |  muted (F6)" : "",
                     input_pad_name() ? "  |  pad: " : "", input_pad_name() ? input_pad_name() : "");
            SDL_SetWindowTitle(win, title);
            frames = 0;
            title_timer = now;
        }
    }

    if (smoke)
        printf("smoke ok: %u ticks, checksum %08x, %s terrain, spawn %d,%d\n", (unsigned)game.tick,
               (unsigned)game_checksum(&game), biome_name((Biome)game.biome), game.spawn_tx, game.spawn_ty);
    if (smoke && hosting)                                   /* (what the server here was set to) */
        printf("hosted: %d maps (%s), time limit %d s, start after %d s, watching %s, %d bots\n", host_srv.nmaps,
               host_srv.cfg.rotation == ROT_VOTE ? "vote" : host_srv.cfg.rotation == ROT_RANDOM ? "random" : "in order",
               host_srv.cfg.base.time_limit, host_srv.cfg.start_after_ms / 1000,
               !host_srv.cfg.allow_watching ? "off" : host_srv.cfg.spectator_delay_s ? "30 s behind" : "live", host_srv.cfg.bots);

    save_cfg(&cfg);
    signin_shutdown();
    input_shutdown();
    ui_shutdown();
    render_shutdown();
    if (zoom_tex) SDL_DestroyTexture(zoom_tex);
    audio_shutdown();
    theme_free(&theme);
    styles_free(styles, num_styles);
    end_recording();                                        /* the match being played, if any */
    leave_net();
    if (getenv("OFP_TEST_FPS")) printf("frames drawn: %d\n", frames);   /* (tests: is the power saving on?) */
    if (getenv("OFP_TEST_KEYS")) {
        int q;
        printf("at the end: screen %d, menu page %d, row %d; signed in %d:", (int)screen, menu.page, menu.row, signin_count());
        for (q = 0; q < signin_count(); q++) printf(" %s (%s)", signin_name(q), input_device_name(signin_device(q)));
        printf("; local game: %d players\n", game_modes[cfg.mode].players);
    }
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return exit_status;
}
