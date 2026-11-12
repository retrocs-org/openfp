/* SPDX-License-Identifier: MIT
 * When two machines get out of step: the whole game state at a tick, as text, one thing a
 * line ("tank 0 x=... y=..."), so two dumps can be compared line by line and the first
 * difference says where the games went apart. */
#ifndef OFP_DUMP_H
#define OFP_DUMP_H

#include <stdio.h>
#include "core/game.h"
#include "common/replay.h"

void game_dump(const Game *g, FILE *f);
/* play a replay to that tick (scratch: a Game to use) and dump it there; 0 = done */
int  replay_dump_at(const Replay *r, uint32_t tick, Game *scratch, FILE *f);
/* the first differences between two dumps (at most max lines of them); returns how many
 * lines differ in all, 0 = the same */
int  dump_diff(const char *path_a, const char *path_b, FILE *out, int max);

#endif
