/* SPDX-License-Identifier: MIT
 * Achievements: the same list and the same rules for the game (yours, at this machine)
 * and for servers (per player there). After each match, achieve_update() takes what a
 * player did and their totals so far, and says which ones they've just earned. */
#ifndef OFP_ACHIEVE_H
#define OFP_ACHIEVE_H

#include <stdint.h>

enum { ACH_VICTORY, ACH_UNTOUCHABLE, ACH_TANK_BUSTER, ACH_RESCUER, ACH_LIFESAVER, ACH_HELI_HUNTER,
       ACH_CENTURION, ACH_NIGHT_OWL, ACH_WORLD_TOUR, ACH_VETERAN, ACH_COUNT };

typedef struct {                       /* one player in one finished match */
    int won, lost, saved, tank_kills, kills, helis, biome, night, played_against_people;
} AchieveMatch;

typedef struct {                       /* what's kept between matches */
    uint32_t earned;                   /* bit per ACH_* */
    int      matches, wins, saved, kills;
    uint32_t worlds_won;               /* bit per Biome */
} AchieveTotals;

uint32_t    achieve_update(AchieveTotals *t, const AchieveMatch *m);   /* returns the bits just earned */
const char *achieve_name(int a);       /* "TANK BUSTER" */
const char *achieve_text(int a);       /* "Destroy three enemy tanks in one match" */
int         achieve_count(uint32_t earned);

#endif
