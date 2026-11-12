/* SPDX-License-Identifier: MIT */
#ifndef OFP_MAPCHECK_H
#define OFP_MAPCHECK_H

#include "core/game.h"

enum { MAP_CHECK_MAX = 20, MAP_CHECK_ROWS = 6 };
typedef struct {
    char text[40];
    int16_t x, y;                      /* negative if this issue has no position */
} MapIssue;

typedef struct {
    MapIssue issues[MAP_CHECK_MAX];    /* errors first */
    int count, errors;
    uint8_t route[MAP_H][MAP_W];       /* team bits 0–1, breach-required route bits 2–3 */
} MapCheck;

void map_check_playability(const Game *g, MapCheck *check, int legacy_mode);
int map_issue_has_location(const Map *map, const MapIssue *issue);

#endif
