/* SPDX-License-Identifier: MIT */
#include "common/achieve.h"

static const char *names[ACH_COUNT] = {
    "VICTORY", "UNTOUCHABLE", "TANK BUSTER", "RESCUER", "LIFESAVER", "HELI HUNTER",
    "CENTURION", "NIGHT OWL", "WORLD TOUR", "VETERAN"
};
static const char *texts[ACH_COUNT] = {
    "Win a match",
    "Win, losing no tank",
    "3 tanks in one match",
    "5 men saved in a match",
    "100 men saved in all",
    "Shoot down a helicopter",
    "100 soldiers in all",
    "Win at night",
    "Win in all five worlds",
    "Play 25 matches"
};

const char *achieve_name(int a) { return a >= 0 && a < ACH_COUNT ? names[a] : ""; }
const char *achieve_text(int a) { return a >= 0 && a < ACH_COUNT ? texts[a] : ""; }

int achieve_count(uint32_t earned)
{
    int n = 0, i;
    for (i = 0; i < ACH_COUNT; i++) n += (earned >> i) & 1;
    return n;
}

uint32_t achieve_update(AchieveTotals *t, const AchieveMatch *m)
{
    uint32_t now = 0, before = t->earned;
    t->matches++;
    t->wins += m->won != 0;
    t->saved += m->saved;
    t->kills += m->kills;
    if (m->won && m->biome >= 0 && m->biome < 32) t->worlds_won |= 1u << m->biome;
    if (m->won) now |= 1u << ACH_VICTORY;
    if (m->won && m->lost == 0) now |= 1u << ACH_UNTOUCHABLE;
    if (m->tank_kills >= 3) now |= 1u << ACH_TANK_BUSTER;
    if (m->saved >= 5) now |= 1u << ACH_RESCUER;
    if (t->saved >= 100) now |= 1u << ACH_LIFESAVER;
    if (m->helis > 0) now |= 1u << ACH_HELI_HUNTER;
    if (t->kills >= 100) now |= 1u << ACH_CENTURION;
    if (m->won && m->night) now |= 1u << ACH_NIGHT_OWL;
    if ((t->worlds_won & 0x1Fu) == 0x1Fu) now |= 1u << ACH_WORLD_TOUR;
    if (t->matches >= 25) now |= 1u << ACH_VETERAN;
    t->earned |= now;
    return t->earned & ~before;
}
