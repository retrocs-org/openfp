/* SPDX-License-Identifier: MIT */
#include "common/botnames.h"

static const char *const names[] = {               /* (seven letters at most: a team screen card's room) */
    "Rusty", "Oopsie", "Wobbles", "Clanky", "Dented", "Boomer", "Panic", "Mishap",
    "Grumpy", "Rattles", "Muffin", "Skidz", "Gravy", "Bolts", "Crumpet", "Smokey",
    "Tincan", "Bumper", "Mudpie", "Leaky", "Snoozy", "Wonky", "Diesel", "Pickles",
    "Shelly", "Hank", "Bumble", "Dinky", "Squeak", "Fumble", "Doozy", "Kaboom",
};
#define N_NAMES ((int)(sizeof names / sizeof names[0]))

const char *bot_name(uint32_t seed, int k)
{
    int order[N_NAMES], i;
    uint32_t x = seed * 2654435761u + 0x9E3779B9u;
    for (i = 0; i < N_NAMES; i++) order[i] = i;
    for (i = N_NAMES - 1; i > 0; i--) {                  /* (a shuffle by the seed: the match's own order) */
        int j, t;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        j = (int)(x % (uint32_t)(i + 1));
        t = order[i]; order[i] = order[j]; order[j] = t;
    }
    if (k < 0) k = 0;
    return names[order[k % N_NAMES]];
}
