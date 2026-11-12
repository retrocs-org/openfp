/* SPDX-License-Identifier: MIT */
#include <string.h>
#include <time.h>
#include "common/daily.h"
#include "core/game.h"

uint32_t daily_today(void)
{
    time_t now = time(NULL);
    struct tm *u = gmtime(&now);
    return u ? (uint32_t)((u->tm_year + 1900) * 10000 + (u->tm_mon + 1) * 100 + u->tm_mday) : 20260101u;
}

void daily_setup(uint32_t day, MatchSetup *ms)
{
    uint32_t seed = (day * 2654435761u) ^ 0x000FDA11u;   /* (the web site: the same, in inc/daily.php) */
    memset(ms, 0, sizeof *ms);
    ms->seed = seed;
    ms->biome = (uint8_t)((seed >> 3) % BIOME_COUNT);
    ms->time = (uint8_t)((seed >> 7) % TIME_COUNT);
    ms->weather = (uint8_t)((seed >> 11) % 4 == 0 ? WEATHER_STORM : WEATHER_CLEAR);
    ms->extras = (uint8_t)((seed >> 13) % 3 == 0 ? EXTRA_PICKUPS : 0);
    ms->camp_garages = 4;
    ms->ai_level = AI_NORMAL;
    ms->players = 1;
    ms->cls[0] = TANK_MEDIUM;
    ms->team[0] = TEAM_PLAYER;
}
