/* SPDX-License-Identifier: MIT
 * The map editor: conquest's capture points (P), an arena's spawn points (O), an arena (N), the
 * modes it's made for (L): placed, taken away, saved and opened again (the real editor code). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "platform/sdl2/editor.h"
#include "core/match.h"
#include "platform/sdl2/maps.h"
#include "common/recovery.h"
#include "common/soundtrack.h"
static Game g; static Editor e; static char buf[400000];
static void key(int k) { editor_update(&e, 0, k, -1, -1, 0, 0); }
static void at(int x, int y, int k) { e.cx = x; e.cy = y; key(k); }
static void press(InputBits input)
{
    editor_update(&e, input, 0, -1, -1, 0, 0);
    editor_update(&e, 0, 0, -1, -1, 0, 0);
}
static int click(int x, int y)
{
    int result = editor_update(&e, 0, 0, x, y, SDL_BUTTON(SDL_BUTTON_LEFT), 0);
    editor_update(&e, 0, 0, x, y, 0, 0);
    return result;
}
static void drag_zone(int x0, int y0, int x1, int y1)
{
    const int left = SDL_BUTTON(SDL_BUTTON_LEFT);
    editor_update(&e, 0, 0, 106 + x0, 22 + y0, left, 0);
    editor_update(&e, 0, 0, 106 + x1, 22 + y1, left, 0);
    editor_update(&e, 0, 0, 106 + x1, 22 + y1, 0, 0);
}

static void detail(int y, const char *value)
{
    click(50, y);
    while (e.detail_draft[0]) key(SDLK_BACKSPACE);
    editor_text(&e, value);
    key(SDLK_RETURN);
}

static int check_weather(void)
{
    int i, ok;
    editor_open(&e, &g, NULL, NULL, 0, 77, BIOME_CLASSIC);
    key(SDLK_k);
    click(247, 206);
    ok = !g.map.has_weather && e.show_weather;
    key(SDLK_ESCAPE);
    click(62, 70);                          /* Options */
    press(IN_DOWN);                         /* World -> Time of Day, skipping Weather */
    click(62, 70);                          /* Disabled Weather ignores a mouse click. */
    ok &= e.msel == 2 && e.option_weather == 0;
    for (i = 0; i < 4; i++) click(62, 55);   /* Classic -> Moon -> Winter -> Desert -> Jungle */
    ok &= e.option_world == BIOME_JUNGLE && e.option_mode == MAP_MODE_FLAG && e.option_auto_mode;
    click(62, 70); click(62, 70);           /* Any -> Clear -> Thunderstorm */
    click(62, 160);
    ok &= strstr(e.msg, "MODE SET TO CAPTURE THE FLAG") != NULL;
    key(SDLK_ESCAPE);
    ok &= g.biome == BIOME_JUNGLE && g.map.mode == MAP_MODE_FLAG && g.map.has_weather && g.map.weather == WEATHER_STORM;
    key(SDLK_ESCAPE);
    click(62, 70);                          /* Options */
    for (i = 0; i < 4; i++) click(62, 55);   /* Jungle -> Swamp -> Volcano -> Wasteland -> Classic */
    ok &= e.option_world == BIOME_CLASSIC && e.option_mode == MAP_MODE_FLAG && e.option_weather == 0;
    click(62, 70);
    ok &= e.msel == 0 && e.option_weather == 0;
    click(62, 160);
    key(SDLK_ESCAPE);
    ok &= g.biome == BIOME_CLASSIC && !g.map.has_weather && g.map.weather == WEATHER_CLEAR;
    printf("editor world and weather controls: %s\n", ok ? "ok" : "FAILED");
    return ok;
}

static int check_soundtrack(void)
{
    int ok;
    game_init(&g, 77, BIOME_WINTER);
    mapfile_save(&g.map, g.bases, BIOME_WINTER, "Winter music", buf, sizeof buf);
    editor_open(&e, &g, buf, NULL, 0, 77, BIOME_WINTER);
    key(SDLK_ESCAPE); click(62, 70); click(62, 100);
    ok = e.option_music == 1 && g.map.music == 0;
    press(IN_MINE); click(62, 70);
    ok &= e.option_music == 0; /* Cancel leaves the map untouched. */
    click(62, 100); click(62, 160); key(SDLK_ESCAPE);
    ok &= g.map.music == 1;
    key(SDLK_c); /* Erasing terrain keeps the chosen soundtrack. */
    ok &= g.map.music == 1;
    key(SDLK_ESCAPE); click(62, 70); click(62, 115); click(62, 160);
    click(242, 119); key(SDLK_ESCAPE); /* Resize also keeps the choice. */
    ok &= g.map.music == 1;
    key(SDLK_ESCAPE); click(62, 70); click(62, 55);
    ok &= e.option_world == BIOME_DESERT && e.option_music == 0;
    click(62, 160); key(SDLK_ESCAPE);
    ok &= g.biome == BIOME_DESERT && g.map.music == 0;
    printf("editor soundtrack choices: %s\n", ok ? "ok" : "FAILED");
    return ok;
}

static int check_return_and_jump(void)
{
    int ok, tool, x, y, view_x, view_y, left = SDL_BUTTON(SDL_BUTTON_LEFT);
    editor_open(&e, &g, NULL, NULL, 0, 77, BIOME_CLASSIC);
    key(SDLK_TAB);                         /* Roads, so an accidental paint would change the draft. */
    tool = e.tool;
    key(SDLK_F7);
    ok = e.checking && e.check.errors == 2;
    press(IN_DOWN);
    ok &= e.check_row == 1 && map_issue_has_location(&g.map, &e.check.issues[1]);
    x = e.check.issues[1].x;
    y = e.check.issues[1].y;
    click(60, 199);
    ok &= !e.checking && e.cx == x && e.cy == y && e.tool == tool;
    view_x = e.view_x;
    view_y = e.view_y;
    editor_resume_test(&e, IN_DOWN | IN_FIRE | IN_MINE, 100, 100, left);
    editor_update(&e, IN_DOWN | IN_FIRE | IN_MINE, 0, 100, 100, left, 0);
    ok &= e.cx == x && e.cy == y && e.view_x == view_x && e.view_y == view_y && e.tool == tool && !e.dirty;
    editor_update(&e, 0, 0, 100, 100, 0, 0);
    press(IN_FIRE);
    ok &= e.dirty;
    g.map.arena = 1;
    key(SDLK_F7);
    ok &= e.checking && e.check.count && !map_issue_has_location(&g.map, &e.check.issues[0]);
    press(IN_FIRE);
    ok &= e.checking;
    key(SDLK_ESCAPE);
    printf("editor validation jump and test return: %s\n", ok && !e.checking ? "ok" : "FAILED");
    return ok && !e.checking;
}

static int check_custom(void)
{
    static Game battle;
    MatchSetup setup = {0};
    int i, ok, x0, y0;
    const char *saved;
    editor_open(&e, &g, NULL, NULL, 0, 77, BIOME_CLASSIC);
    ok = g.map.size == SIZE_LARGE && g.biome == BIOME_CLASSIC && g.map.custom_bases && g.map.num_st == 0 && g.map.players == 4;
    ok &= map_custom_players(SIZE_SMALL, 2, 1) == 2 &&
          map_custom_players(SIZE_MEDIUM, 2, 1) == 4 && map_custom_players(SIZE_MEDIUM, 4, 1) == 2 &&
          map_custom_players(SIZE_LARGE, 4, 1) == 6 && map_custom_players(SIZE_LARGE, 6, 1) == 4 &&
          map_custom_players(SIZE_HUGE, 8, 1) == 8;
    ok &= editor_test_map(&e) == NULL;
    key(SDLK_F7);
    ok &= e.checking && e.check.errors == 2;
    key(SDLK_ESCAPE);
    ok &= !e.checking;
    key(SDLK_ESCAPE);
    click(62, 70);                          /* Options */
    click(62, 130);                         /* Classic -> Capture the Flag */
    click(62, 115);                         /* Large -> Huge */
    click(62, 145);                         /* Huge stays at 8 */
    click(62, 160);                         /* Apply, then confirm resize */
    click(242, 119);
    key(SDLK_ESCAPE);
    ok &= g.map.size == SIZE_HUGE && g.map.players == 8 && g.map.mode == MAP_MODE_FLAG;
    for (i = 0; i < 3; i++) key(SDLK_TAB);
    x0 = e.cx; y0 = e.cy;
    key(SDLK_F6);
    press(IN_RIGHT);
    press(IN_FIRE);
    ok &= e.cx == x0 + 4 && e.cy == y0 && !e.overview && g.map.num_st == 0;
    click(30, 206);
    click(140, 64);
    ok &= e.cx < x0 && e.cy < y0 && !e.overview && g.map.num_st == 0;
    click(90, 206);
    drag_zone(84, 84, 39, 39);          /* Resize Team 1's lower corner. */
    drag_zone(21, 21, 27, 27);          /* Move the whole area. */
    ok &= g.bases[TEAM_PLAYER].x0 == 9 && g.bases[TEAM_PLAYER].x1 < 55;
    key(SDLK_ESCAPE);
    click(327, 206);
    detail(42, "River Watch");
    detail(75, "Control the crossings.");
    detail(130, "Editor Test");
    detail(157, "maps@example.org");
    click(52, 184);
    ok &= !e.details && !strcmp(g.map.details.contact, "maps@example.org");
    click(164, 206); click(247, 206);
    ok &= !e.show_time && e.show_weather && g.map.time == TIME_DAY;
    key(SDLK_e); key(SDLK_e); key(SDLK_e);    /* Garage */
    e.cx = 75; e.cy = 77; press(IN_FIRE);    /* Outside both areas */
    ok &= g.map.num_st == 0;
    for (i = 0; i < 4; i++) { e.cx = 10 + 7 * i; e.cy = 13; press(IN_FIRE); }
    e.cx = 40; e.cy = 32; press(IN_FIRE);    /* Already at the team's limit */
    ok &= map_custom_count(&g.map, TEAM_PLAYER, ST_GARAGE) == 4 && editor_test_map(&e) == NULL;
    for (i = 0; i < 4; i++) { e.cx = 112 + 7 * i; e.cy = 121; press(IN_FIRE); }
    key(SDLK_e);                            /* Flag base */
    e.cx = 123; e.cy = 130; press(IN_FIRE);
    ok &= map_custom_count(&g.map, TEAM_ENEMY, ST_FLAGHOUSE) == 1;
    key(SDLK_ESCAPE);
    click(62, 70);
    click(62, 130);                         /* Flag -> Conquest */
    click(62, 160);
    ok &= g.map.mode == MAP_MODE_FLAG;      /* Existing flag blocks the mode change */
    key(SDLK_ESCAPE); key(SDLK_ESCAPE);
    key(SDLK_DELETE);
    key(SDLK_ESCAPE);
    click(62, 70); click(62, 130); click(62, 160); key(SDLK_ESCAPE);
    ok &= g.map.mode == MAP_MODE_CONQUEST;
    for (i = 0; i < 16; i++) key(SDLK_e);    /* MG nest (no flag tool in Conquest) */
    e.cx = 76; e.cy = 76; press(IN_FIRE);
    ok &= map_custom_count(&g.map, TEAM_INDEPENDENT, ST_T_MG) == 1;
    saved = editor_test_map(&e);
    if (!saved) return 0;
    snprintf(buf, sizeof buf, "%s", saved);
    setup.seed = 77;
    setup.versus = 1;
    setup.players = 8;
    setup.weather = WEATHER_STORM;           /* Classic artwork stays clear with a host override. */
    for (i = 0; i < 8; i++) setup.team[i] = i / 4;
    ok &= match_start(&battle, &setup, buf) == 0;
    ok &= battle.num_tanks == 8 && (battle.extras & EXTRA_CONQUEST) && battle.weather == WEATHER_CLEAR &&
          battle.bases[0].num_garages == 4 && battle.bases[1].num_garages == 4;
    ok &= map_custom_count(&battle.map, TEAM_INDEPENDENT, ST_T_MG) == 1 &&
          !strcmp(battle.map.details.description, "Control the crossings.") &&
          !strcmp(battle.map.details.author, "Editor Test") &&
          !strcmp(battle.map.details.contact, "maps@example.org") &&
          strstr(buf, "\nname=River Watch\n") != NULL;
    for (i = TEAM_PLAYER; i <= TEAM_ENEMY; i++) {
        const InputBits inputs[MAX_TANKS] = {0};
        int j, tick, target = i == TEAM_PLAYER ? 0 : 4;
        int turret, armor;
        if (match_start(&battle, &setup, buf) != 0) return 0;
        turret = battle.map.sid[76][76] - 1;
        if (turret < 0 || battle.map.st[turret].team != TEAM_INDEPENDENT) return 0;
        for (j = 0; j < battle.num_tanks; j++) battle.tanks[j].active = j == target;
        battle.tanks[target].x = FX(76 * TILE_SIZE + TILE_SIZE / 2);
        battle.tanks[target].y = FX(76 * TILE_SIZE + TILE_SIZE / 2 - 48);
        battle.tanks[target].respawn = 0;
        armor = battle.tanks[target].armor;
        for (tick = 0; tick < 60; tick++) game_tick(&battle, inputs);
        ok &= battle.tanks[target].armor < armor;
    }
    if (editor_save(&e) == 0) {
        char *file = maps_read(e.path);
        ok &= file && strstr(file, "\nsize=huge\n") && strstr(file, "\nplayers=8\n") && strstr(file, "\nmodes=conquest\n");
        free(file);
    } else ok = 0;
    {
        char *garage = strstr(buf, "struct=garage player 10 13");
        if (!garage) return 0;
        memcpy(garage + strlen("struct=garage "), "enemy ", 6);
        ok &= match_start(&battle, &setup, buf) != 0;
    }
    printf("custom areas, overview, flags, and 8-player garages: %s\n", ok ? "ok" : "FAILED");
    return ok;
}
static int garages(void) { int k, n = 0; for (k = 0; k < g.map.num_st; k++) n += g.map.st[k].type == ST_GARAGE; return n; }
static int check_surfaces(void)
{
    int ok=1;
    editor_open(&e,&g,NULL,NULL,0,77,BIOME_CLASSIC);
    g.biome=BIOME_WINTER;g.map.mode=MAP_MODE_FLAG;
    click(366,35);click(366,35);click(366,35);click(366,35); /* return to Ground */
    key(SDLK_PAGEDOWN);e.cx=40;e.cy=40;press(IN_FIRE);ok &= g.map.type[40][40]==TILE_WATER;
    key(SDLK_PAGEDOWN);e.cx=41;e.cy=40;press(IN_FIRE);ok &= g.map.type[40][41]==TILE_ICE;
    g.biome=BIOME_MOON;
    click(366,35);click(366,35);click(366,35);click(366,35);
    key(SDLK_PAGEDOWN);e.cx=42;e.cy=40;press(IN_FIRE);ok &= g.map.type[40][42]==TILE_CRATER;
    g.biome=BIOME_DESERT;
    click(366,35); /* Roads */
    key(SDLK_PAGEDOWN);key(SDLK_PAGEDOWN);e.cx=43;e.cy=40;press(IN_FIRE);ok &= g.map.type[40][43]==TILE_OIL;
    printf("World-filtered editor surfaces: %s\n",ok?"ok":"FAILED");return ok;
}

int main(void) {
    int surfaces_ok=check_surfaces();
    int ok = 1, k; FILE *f; size_t n;
    ok &= check_weather();
    ok &= check_soundtrack();
    ok &= check_return_and_jump();
    ok &= check_custom();
    game_init(&g, 77, BIOME_DESERT);
    mapfile_save(&g.map, g.bases, BIOME_DESERT, "Legacy test", buf, sizeof buf);
    editor_open(&e, &g, buf, NULL, 0, 77, BIOME_DESERT);
    at(40, 40, SDLK_p); at(64, 64, SDLK_p); at(88, 88, SDLK_p); at(100, 30, SDLK_p);
    printf("P at four places: %d points (three at most)\n", g.map.num_points); ok &= g.map.num_points == 3;
    at(40, 40, SDLK_p);
    printf("P on the first again: %d points\n", g.map.num_points); ok &= g.map.num_points == 2;
    at(40, 40, SDLK_p);
    key(SDLK_n);
    printf("N with no spawn points: arena %d (no)\n", g.map.arena); ok &= !g.map.arena;
    at(30, 50, SDLK_o); at(50, 30, SDLK_o); at(80, 98, SDLK_o); at(98, 80, SDLK_o);
    key(SDLK_n);
    printf("four spawn points, N: arena %d, %d spawns, garages %d\n", g.map.arena, g.map.num_spawns, garages()); ok &= g.map.arena && g.map.num_spawns == 4 && garages() == 0;
    for (k = 0; k < 5; k++) key(SDLK_l);
    if (editor_save(&e) != 0) { printf("SAVE FAILED\n"); return 1; }
    f = fopen(e.path, "rb"); n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f);
    printf("saved: modes line \"%s\", arena line %s, spawns line %s, points line %s\n", strstr(buf, "\nmodes=") ? "modes=annihilation,deathmatch,warlord" : "MISSING",
           strstr(buf, "\narena=yes") ? "yes" : "MISSING", strstr(buf, "\nspawns=") ? "yes" : "MISSING", strstr(buf, "\npoints=") ? "yes" : "MISSING");
    ok &= strstr(buf, "\nmodes=annihilation,deathmatch,warlord\n") && strstr(buf, "\narena=yes") && strstr(buf, "\nspawns=") && strstr(buf, "\npoints=");
    editor_open(&e, &g, buf, e.path, 1, 77, BIOME_DESERT);
    printf("opened again: modes %d, arena %d, %d spawns, %d points\n", e.modes, g.map.arena, g.map.num_spawns, g.map.num_points);
    ok &= e.modes == 5 && g.map.arena && g.map.num_spawns == 4 && g.map.num_points == 3;
    key(SDLK_n);
    printf("N again: arena %d, garages %d (camps again)\n", g.map.arena, garages()); ok &= !g.map.arena && garages() >= 6;
    {   /* a pad: B (MINE) erases; START, the map's menu: WORLD changed; B closes; the stick moves up and down */
        int b0, cy0, t0 = e.tool, k2;
        e.cx = 60; e.cy = 60;
        for (k2 = 0; k2 < 40 && g.map.type[60][60] != TILE_WALL; k2++) { e.tool = k2; editor_update(&e, IN_FIRE, 0, -1, -1, 0, 0); editor_update(&e, 0, 0, -1, -1, 0, 0); }
        printf("painted at 60,60: %s\n", g.map.type[60][60] != TILE_SAND ? "yes" : "NO");
        editor_update(&e, IN_MINE, 0, -1, -1, 0, 0); editor_update(&e, 0, 0, -1, -1, 0, 0);
        printf("B there: %s\n", g.map.type[60][60] == TILE_SAND ? "erased" : "STILL THERE"); ok &= g.map.type[60][60] == TILE_SAND;
        e.tool = t0;
        key(ED_KEY_MENU);
        printf("START: the menu %s\n", e.menu ? "open" : "CLOSED"); ok &= e.menu;
        b0 = g.biome;
        press(IN_DOWN); press(IN_FIRE);             /* Options */
        press(IN_RIGHT);                            /* World */
        click(62, 160);                              /* Apply */
        printf("WORLD, right: %d -> %d\n", b0, g.biome); ok &= g.biome != b0;
        press(IN_MINE);
        printf("B: the menu %s\n", e.menu ? "STILL OPEN" : "closed"); ok &= !e.menu;
        cy0 = e.cy;
        for (k2 = 0; k2 < 30; k2++) editor_update(&e, IN_DOWN, 0, -1, -1, 0, 0);
        editor_update(&e, 0, 0, -1, -1, 0, 0);
        printf("the stick down: the cursor %d -> %d\n", cy0, e.cy); ok &= e.cy > cy0;
    }
    {
        char prefix[1200], manual[1200], check[32]; FILE *f;
        snprintf(prefix, sizeof prefix, "%srecovery", maps_user_dir());
        snprintf(manual, sizeof manual, "%srecovery-manual-check.ofpmap", maps_user_dir());
        recovery_clear(prefix);
        f = fopen(manual, "w"); if (f) { fputs("manual-map-unchanged", f); fclose(f); }
        editor_open(&e, &g, NULL, NULL, 0, 77, BIOME_CLASSIC);
        g.map.type[60][60] = TILE_WALL; e.dirty = 1;
        e.autosave_at = SDL_GetTicks() - 30001u; key(0);
        { int got = recovery_read(prefix, buf, sizeof buf); printf("draft written: %d (%s)\n", got, prefix); ok &= got; }
        editor_open(&e, &g, NULL, NULL, 0, 77, BIOME_CLASSIC);
        printf("draft offered: %d\n", e.recovery_pending); ok &= e.recovery_pending;
        press(IN_FIRE);
        printf("draft restored: pending %d dirty %d path %s tile %d expected %d\n", e.recovery_pending, e.dirty, e.path, g.map.type[60][60], TILE_WALL);
        ok &= !e.recovery_pending && e.dirty && !e.path[0] && g.map.type[60][60] == TILE_WALL;
        ok &= editor_test_map(&e) == NULL; /* recovery is not permission to publish an unfinished map */
        f = fopen(manual, "r"); check[0] = 0;
        if (f) { fgets(check, sizeof check, f); fclose(f); }
        printf("manual map: %s\n", check); ok &= !strcmp(check, "manual-map-unchanged");
        editor_open(&e, &g, NULL, NULL, 0, 77, BIOME_CLASSIC);
        press(IN_MINE);
        ok &= !e.recovery_pending && !recovery_read(prefix, buf, sizeof buf);
        remove(manual);
        printf("unfinished draft recovery and manual-map preservation: %s\n", ok ? "ok" : "FAILED");
    }
    ok &= surfaces_ok;
    printf(ok ? "editor: ok\n" : "editor: FAILED\n");
    return !ok;
}
