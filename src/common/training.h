/* SPDX-License-Identifier: MIT
 * TRAINING, shared by every frontend: the training ground (a small map, nothing that fights back), its
 * six steps (drive, shoot, a mine, the trees, a rescue, the flag), where a step's arrow points, and
 * whether it's done. The frontend shows the prompts (its own buttons) and the arrow. */
#ifndef OFP_TRAINING_H
#define OFP_TRAINING_H
#include "core/match.h"

enum { TR_DRIVE, TR_SHOOT, TR_MINE, TR_TREES, TR_RESCUE, TR_FLAG, TR_DONE };
typedef struct { int step, mark; fx x0, y0; } Training;

const char *training_ground(void);              /* the ground's map text (the same every time); "" if it failed */
void training_setup(MatchSetup *ms);            /* a setup for it: one player, no computer tank, clear skies */
void training_clean_up(Game *g);                /* the match started: nothing that fights back, one prison camp */
void training_begin(Training *tr, const Game *g);   /* step one, from tank 0's place */
int  training_target(const Game *g, const Training *tr, fx *tx, fx *ty);   /* where the arrow points (0: nowhere) */
void training_check(const Game *g, Training *tr);  /* each frame: a step done? on to the next */
#endif
