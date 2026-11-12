/* SPDX-License-Identifier: MIT
 * The local game's team screen, before a match: the camp on the left, the fortress on the
 * right. Every player here, with their own keys or pad, moves left or right to a side,
 * picks a tank (up, down) and says ready (FIRE; MINE: not yet). People and computer
 * players are spread to even the sides; a player who switches swaps with a computer
 * player there. When everyone's ready, player 1 starts. Never more than a side's
 * garages, nor than a match holds. */
#ifndef OFP_SDL2_TEAMS_H
#define OFP_SDL2_TEAMS_H

#include <SDL.h>
#include "core/match.h"
#include "platform/sdl2/theme.h"

enum { TEAMS_FLAG, TEAMS_CONQUEST, TEAMS_ANNIHILATION, TEAMS_TDM, TEAMS_WARLORD, TEAMS_CLASSIC, TEAMS_KILLCONF, TEAMS_HARDPOINT, TEAMS_MODES };
/* (the new ones after CLASSIC: LOCAL GAME's MODE is saved as its number) */
/* (annihilation: every tank for itself; CLASSIC: the 1987 rules: capture the flag 1 vs 1, three garages a
 * side, no pickups, no hill, no storms; capture the flag: ours, with what's been added since) */
enum { TEAMS_NOTHING, TEAMS_START, TEAMS_BACK };

typedef struct {
    int  type, humans;
    int  dev[4], side[4], cls[4], ready[4];
    InputBits prev[4];
    char name[4][16];
    char side_name[2][13], edit_name[13];
    int leader[2], side_order[4], join_order, editing_team;
    int  avatar[4];
    int  bots[2], bot_level;           /* the computer players on each side, and how good */
    int  cap[2], max_total;            /* a side's garages; the most a match holds */
    char note[64];
    Uint32 note_until;
    uint32_t name_seed;              /* the AI bots' names: bot_name(name_seed, its tank) */
    int      focus[4];               /* (no longer used: left, right the side; up, down the tank) */
    int      host_buttons, host_sel; /* player 1 at the buttons (FIRE got there): HB_ADD, HB_REMOVE, HB_START */
    int      stat_cls;               /* the tank someone just chose: its speed, armour and gun shown a moment */
    Uint32   stat_until;
} Teams;
enum { TF_SIDE, TF_TANK, TF_BOTS };
enum { HB_ADD, HB_REMOVE, HB_LEVEL, HB_START, HB_COUNT };   /* (the AI bots' level: easy, normal, hard, adaptive) */

/* names, avatars and devices of the players here (devs: DEV_*; avatar -1: none); bots[0] + bots[1]:
 * how many computer players in all (spread to even the sides) */
int teams_add_player(Teams *t, int dev, const char *name, int avatar, int cls);
void teams_remove_player(Teams *t, int player);
void teams_begin(Teams *t, int type, int humans, const int *devs, char (*names)[16], const int *avatars,
                 const int *cls, const int bots[2], int bot_level, int cap_camp, int cap_fortress, int max_total);
int  teams_rename(Teams *t, int player);
int  teams_name_event(Teams *t, const SDL_Event *event);
int  teams_frame(Teams *t);            /* TEAMS_*: every player's own keys or pad */
void teams_set_map(const Game *g);
void teams_set_tanks(SDL_Texture *tanks);   /* the world's tank sprites: each row's tank in its side's colours */
int  teams_add_bot(Teams *t);        /* player 1's: an AI bot more (1) or not (0: no room; a note says) */
int  teams_remove_bot(Teams *t);   /* the map it'll be (the preview): shown, with where everyone starts */
void teams_draw(SDL_Renderer *r, const Theme *th, const Teams *t, int sw, int sh);
/* the match: the players here first (tanks 0..humans-1), then the computer's; versus or not, the hill */
void teams_setup(const Teams *t, MatchSetup *ms);
int  teams_count(const Teams *t, int side);   /* players and computer players on a side */

#endif
