/* SPDX-License-Identifier: MIT
 * Internals of game.c shared with ai.c. Not part of the public API. */
#ifndef OFP_GAME_INTERNAL_H
#define OFP_GAME_INTERNAL_H

#include "core/game.h"

int  gi_range(Game *g, int lo, int hi);                         /* deterministic dice */
fx   gi_dist(fx ax, fx ay, fx bx, fx by);                      /* cheap distance */
int  gi_in_base(const Game *g, const Tank *t, int team);
int  gi_clear_shot(const Game *g, fx ax, fx ay, fx bx, fx by, int own_sid);
void gi_struct_centre(const Map *m, int idx, fx *x, fx *y);

#endif
