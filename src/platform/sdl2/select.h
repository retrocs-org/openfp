/* SPDX-License-Identifier: MIT
 * Tank selection screen: one card per tank class. */
#ifndef OFP_SDL2_SELECT_H
#define OFP_SDL2_SELECT_H

#include <SDL.h>
#include "core/game.h"
#include "platform/sdl2/theme.h"

/* how many play, and how. "Teams" modes: players 1 and 3 hold our camp,
 * players 2 and 4 the enemy's. "Team" modes: everyone against the computer. */
enum { MODE_SOLO, MODE_VERSUS, MODE_COOP, MODE_3_TEAMS, MODE_3_COOP, MODE_4_TEAMS, MODE_4_COOP, MODE_COUNT };
typedef struct { const char *name; int players, versus; } GameMode;
extern const GameMode game_modes[MODE_COUNT];

/* Most players this build lets share one screen: 4 on macOS, Linux and
 * Windows; a smaller machine (the planned Amiga port) is built with 2 and
 * plays the bigger games over the network instead. */
#ifndef OFP_MAX_LOCAL_PLAYERS
#define OFP_MAX_LOCAL_PLAYERS 4
#endif
int  mode_available(int mode);                   /* fits this build, and the map's garages? */
/* the chosen map's room: garages in our camp and in the enemy fortress */
void select_set_caps(int ours, int theirs);
void select_set_max_players(int n);              /* the faithful Classic: 2, as in the original */
int  mode_side_players(int mode, int team);      /* players on a side in a mode */
int  mode_team(int mode, int player);            /* TEAM_PLAYER or TEAM_ENEMY */
void select_tank_card(SDL_Renderer *r, const Theme *t, SDL_Texture *tanks, int pick, int choices, int x, int y, int focus);

typedef struct {
    int      choice;        /* TankClass (player 1) */
    int      choices[4];    /* every player's tank; [0] mirrors choice */
    int      mode;          /* MODE_* */
    int      ready[4];      /* several players: all press fire to start */
    InputBits prevs[4];
    int      style;         /* index into the loaded map styles */
    int      num_styles;
    int      slide;         /* card slide-in offset, px */
    uint32_t frame;
    InputBits prev;
} SelectScreen;

void select_reset(SelectScreen *s, int choice, int style, int num_styles);
/* Feed one frame of input. Returns 1 when the player confirmed a tank. */
int  select_update(SelectScreen *s, InputBits in, int confirm_key);
/* several players: each picks a tank, all press fire (or Enter starts) */
int  select_update_n(SelectScreen *s, const InputBits in[4], int confirm_key);
void select_next_mode(SelectScreen *s);
void select_draw(SDL_Renderer *r, const Theme *t, const Style *styles, const SelectScreen *s,
                 int screen_w, int screen_h);

#endif
