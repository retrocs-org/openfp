/* SPDX-License-Identifier: MIT
 * The map editor: paint the ground, place buildings and guns for either
 * side, move the two home bases, then save the map and play it. */
#ifndef OFP_SDL2_EDITOR_H
#define OFP_SDL2_EDITOR_H

#include <SDL.h>
#include "common/mapcheck.h"
#include "platform/sdl2/theme.h"

enum { ED_NOTHING, ED_QUIT, ED_SAVED, ED_PLAY, ED_SHARE, ED_PLAY_HERE };   /* PLAY_HERE: from the cursor (cx, cy) */

typedef struct {
    Game     *g;                     /* the map being edited lives in a game struct, for drawing */
    int       cx, cy;                /* cursor tile */
    int       tool, brush;     /* brush: 1..3 tiles across, for ground */
    int       dirty, wait_fire;
    char      name[64], path[1024];  /* path: where it saves ("" = a new file in your maps folder) */
    int       user;                  /* path is in your own maps folder */
    char      msg[64];
    int       msg_t;
    uint32_t  frame, seed;
    InputBits prev;
    InputBits blocked_nav;           /* inputs to ignore until released after a transition */
    int       blocked_buttons;
    uint32_t  next_step;             /* key repeat: when the held direction moves again (ms) */
    int       mouse_tile;            /* the mouse is over the map */
    int       view_x, view_y;        /* the centre of the view, world pixels */
    int       last_mx, last_my, last_buttons;
    int       drag, drag_moved;      /* right (or middle) button held: dragging the map */
    int       mark, mark_x, mark_y;  /* copying: the first corner is marked (M) */
    int       help;                  /* F1: the list of keys is showing */
    int       checking;              /* F7: the map's check is showing */
    MapCheck  check;
    int       check_row, check_scroll;
    int       modes;                 /* L: the modes it's made for (ED_MODES' index; 0: it doesn't say) */
    int       menu, msel, confirm, confirm_yes, open_index;
    int       option_world, option_weather, option_time, option_size, option_mode, option_players;
    int       option_auto_mode;
    int       category, first_tool;
    int       overview, overview_x, overview_y, area_team, area_keyboard;
    uint32_t autosave_at;
    int recovery_pending, recovery_choice;
    MapAreaEdit area;
    int       show_time, show_weather, details, detail_row, detail_edit;
    char      detail_draft[sizeof ((MapDetails *)0)->description];
} Editor;
#define ED_KEY_MENU SDLK_F12         /* (START on a pad: the map's menu) */
void editor_scroll(Editor *e, int dx, int dy);   /* the right stick: the cursor and view, a few tiles at a time */

/* text: an .ofpmap to edit, or NULL for an empty Large map */
void editor_open(Editor *e, Game *g, const char *text, const char *path, int user, uint32_t seed, int biome);
/* nav: arrows / pad; key: SDL keycode pressed this frame (0 = none);
 * mouse in logical screen coordinates, buttons as SDL_BUTTON masks, wheel steps */
int  editor_update(Editor *e, InputBits nav, int key, int mx, int my, int buttons, int wheel);
void editor_resume_test(Editor *e, InputBits nav, int mx, int my, int buttons);
const char *editor_test_map(Editor *e);  /* the current draft, serialized without saving */
void editor_text(Editor *e, const char *text);
int  editor_save(Editor *e);         /* 0 = written; sets path to the file */
void editor_notice(Editor *e, const char *msg);   /* a message along the bottom for a while */
/* is the map playable? fills e->check (and the counts); returns the number of errors */
int  editor_check(Editor *e);
void editor_draw(SDL_Renderer *r, const Theme *t, const Style *st, Editor *e);

#endif
