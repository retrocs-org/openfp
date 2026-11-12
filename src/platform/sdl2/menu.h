/* SPDX-License-Identifier: MIT
 * The game-type screen, like the original's: what to play, against whom,
 * where, and the options (controls, view, sound, screen, keys). */
#ifndef OFP_SDL2_MENU_H
#define OFP_SDL2_MENU_H

#include <SDL.h>
#include "core/game.h"
#include "platform/sdl2/theme.h"
#include "platform/sdl2/settings.h"
#include "platform/sdl2/maps.h"

enum { PAGE_MAIN, PAGE_OPTIONS, PAGE_KEYS, PAGE_NET, PAGE_HOSTSET, PAGE_LOCAL, PAGE_DIRECT, PAGE_ABOUT, PAGE_TOOLS, PAGE_GAME_OPTIONS, PAGE_GRAPHICS, PAGE_AUDIO, PAGE_CHALLENGES, PAGE_CONTROLLERS };   /* (DIRECT: a window over NET) */   /* (MAIN: the first page) */
void menu_show_map_row(void *menu);
void menu_show_local(void *menu);    /* the LOCAL GAME page, on START (back from a local game) */  /* the LOCAL GAME page, on its MAP row (back from the gallery) */
enum { MENU_NOTHING, MENU_START, MENU_QUIT, MENU_CHANGED, MENU_EDIT, MENU_IMPORT, MENU_HOST, MENU_JOIN, MENU_FIND,
       MENU_REPLAYS, MENU_QUICK, MENU_TRAINING, MENU_GALLERY, MENU_DAILY, MENU_BENCHMARK, MENU_OPERATION, MENU_CHALLENGE_RESCUE, MENU_CHALLENGE_BRIDGE, MENU_CHALLENGE_RAID };

typedef struct {                 /* what there is to choose from */
    const Style *styles;
    int          num_styles;
    const MapEntry *maps;
    int          num_maps;
    const Game  *preview;           /* the chosen map (or the random one you'd get), for the picture */
} MenuEnv;

typedef struct {
    int       page, row;
    int       capture;           /* waiting for a key for this row (keys page), -1 = no */
    int       solo;              /* the TRAINING row: 0 training, 1 the daily challenge */
    int       diagnostic_pad;
    int       keypair;           /* the KEYS page: 0 players 1 and 2's sets, 1 players 3 and 4's */
    uint32_t  credits_start;
    uint32_t  frame;
    InputBits prev;
    char      notice[64];        /* a line of news at the bottom (e.g. maps imported) */
    uint32_t  notice_until;      /* SDL ticks */
    char      benchmark[64];     /* result, kept on the Benchmark row */
} Menu;

void menu_notice(Menu *m, const char *text);
/* typing (the server's address on the network page); returns 1 if it took it */
int  menu_text(Menu *m, Settings *s, const char *utf8);
int  menu_typing(Menu *m, Settings *s);       /* on a text row: keys are letters, not steering */

void menu_reset(Menu *m);
/* Shared steel sections and title backdrop for menu pages. */
void menu_panel(SDL_Renderer *r, int x, int y, int w, int h);
void menu_background(SDL_Renderer *r, int w, int h);
/* the row under the cursor types into a setting (name, server, password, listing)? 1 = yes */
int  menu_text_field(const Menu *m, Settings *s, char **buf, size_t *cap, const char **title, int *secret);
/* nav: joystick/arrow bits this frame; key: a scancode pressed this frame
 * (or 0); back / ok: Esc / Enter (or a pad's View / A). */
int  menu_update(Menu *m, Settings *s, const MenuEnv *env, InputBits nav, int key, int back, int ok);
/* the local game's players: n (signed in), keeping the kind of game if it can; a world for them */
void menu_fit_players(Settings *s, const MenuEnv *e, int n);
void menu_draw(SDL_Renderer *r, const Theme *t, const Menu *m, const Settings *s, const MenuEnv *env,
               int w, int h);

#endif
