/* SPDX-License-Identifier: MIT */
#include <string.h>
#include "common/rules.h"
#include "core/match.h"
#include "core/game.h"

static Game g;                                       /* (static: big) */

static uint32_t play(const MatchSetup *ms, const char *map, int ticks)
{
    MatchSetup setup = *ms;
    InputBits in[MAX_TANKS] = {0};
    int t;
    setup.bot_commands = 1;           /* Fingerprint the shared simulation without running server AI. */
    if (match_start(&g, &setup, map) != 0) return 0xDEADu;
    for (t = 0; t < ticks; t++) {
        int k;
        for (k = 0; k < g.num_tanks; k++)             /* scripted commands for human and bot tanks */
            in[k] = (InputBits)(((uint32_t)t * 2654435761u >> (8 + k)) & (IN_UP | IN_LEFT | IN_FIRE));
        game_tick(&g, in);
    }
    return game_checksum(&g) ^ (uint32_t)g.tick << 20;
}

uint32_t net_rules(void)
{
    static uint32_t rules;
    static char map[MAPFILE_MAX + 64];
    MatchSetup ms;
    uint32_t h = 2166136261u;
    if (rules) return rules;
    /* together against the computer, a storm, pickups and the hill, a computer helper */
    memset(&ms, 0, sizeof ms);
    ms.seed = 1987; ms.biome = BIOME_WINTER; ms.camp_garages = 4; ms.players = 2; ms.ai_level = AI_HARD;
    ms.cls[0] = TANK_LIGHT; ms.cls[1] = TANK_HEAVY; ms.bot[1] = AI_NORMAL;
    ms.weather = WEATHER_STORM; ms.extras = EXTRA_PICKUPS | EXTRA_HILL; ms.time_limit = 600;
    h = (h ^ play(&ms, NULL, 600)) * 16777619u;
    /* versus: computer players on both sides, the adaptive enemy's numbers aside */
    memset(&ms, 0, sizeof ms);
    ms.seed = 2026; ms.biome = BIOME_DESERT; ms.camp_garages = 3; ms.players = 4; ms.versus = 1;
    for (int p = 0; p < 4; p++) { ms.cls[p] = (uint8_t)(p % 3); ms.team[p] = (uint8_t)(p & 1); ms.bot[p] = (uint8_t)(p < 2 ? AI_HARD : 0); }
    h = (h ^ play(&ms, NULL, 400)) * 16777619u;
    /* a map file, read and played (the map's own weather and extras) */
    game_init(&g, 4242, BIOME_SCIFI);
    {
        unsigned n = mapfile_save(&g.map, g.bases, BIOME_SCIFI, "Rules", map, sizeof map - 64);
        char *tiles = n ? strstr(map, "\ntiles\n") : NULL;
        if (tiles) {
            static const char add[] = "\nweather=storm\nextras=hill";
            memmove(tiles + strlen(add), tiles, strlen(tiles) + 1);
            memcpy(tiles, add, strlen(add));
        }
    }
    memset(&ms, 0, sizeof ms);
    ms.seed = 99; ms.camp_garages = 4; ms.players = 1; ms.cls[0] = TANK_MEDIUM; ms.ai_level = AI_ADAPTIVE + 1;
    h = (h ^ play(&ms, map, 300)) * 16777619u;
    /* conquest, annihilation (every tank for itself) and team deathmatch: computer players two a side */
    {
        int mode;
        static const uint8_t modes[4] = { EXTRA_CONQUEST, EXTRA_ANNIHILATION, EXTRA_TDM, EXTRA_WARLORD };
        for (mode = 0; mode < 4; mode++) {
            int k;
            memset(&ms, 0, sizeof ms);
            ms.seed = 5150u + (uint32_t)mode; ms.biome = BIOME_JUNGLE; ms.camp_garages = 4; ms.players = 4; ms.versus = 1;
            ms.extras = modes[mode];
            for (k = 0; k < 4; k++) { ms.cls[k] = (uint8_t)(k % 3); ms.team[k] = (uint8_t)(k & 1); ms.bot[k] = AI_HARD; }
            h = (h ^ play(&ms, NULL, 500)) * 16777619u;
        }
    }
    /* the newer worlds' own rules: the swamp's mud, the moon's slide */
    {
        static const uint8_t worlds[3] = { BIOME_SWAMP, BIOME_MOON, BIOME_VOLCANIC };
        int w2, k;
        for (w2 = 0; w2 < 3; w2++) {
            memset(&ms, 0, sizeof ms);
            ms.seed = 777u + (uint32_t)w2; ms.biome = worlds[w2]; ms.camp_garages = 4; ms.players = 2; ms.versus = 1;
            ms.lives = w2 == 2 ? MATCH_UNLIMITED_LIVES : 3;
            for (k = 0; k < 2; k++) { ms.cls[k] = (uint8_t)k; ms.team[k] = (uint8_t)k; ms.bot[k] = AI_HARD; }
            h = (h ^ play(&ms, NULL, 600)) * 16777619u;
        }
    }
    rules = h ? h : 1;
    return rules;
}
