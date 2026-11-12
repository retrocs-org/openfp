/* SPDX-License-Identifier: MIT */
#ifndef OFP_OPERATION_H
#define OFP_OPERATION_H

#include "core/match.h"

enum { OP_INACTIVE, OP_GUN, OP_PRISON, OP_PICKUP, OP_RETURN, OP_CAPTURE, OP_WON, OP_LOST };
enum { CHALLENGE_RESCUE, CHALLENGE_BRIDGE, CHALLENGE_RAID, CHALLENGE_COUNT };
enum { OP_RESCUE_GOAL = 4 };
typedef struct {
    int phase, gun, prison, medic, saved, aboard;
    int challenge; /* zero: the deferred co-op operation; otherwise challenge index + 1 */
    int guns[3], gun_count, destroyed;
} DesertOperation;

int operation_start(Game *g, DesertOperation *op, const MatchSetup *crew);
int operation_challenge_start(Game *g, DesertOperation *op, const MatchSetup *crew, int challenge);
const char *operation_name(const DesertOperation *op);
void operation_step(Game *g, DesertOperation *op);
int operation_target(const Game *g, const DesertOperation *op, int player, fx *x, fx *y);
const char *operation_prompt(const DesertOperation *op);

#endif
