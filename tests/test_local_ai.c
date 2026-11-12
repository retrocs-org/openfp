/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include "core/match.h"
#include "common/client.h"
static Game live, playback;
static NetClient client;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
int main(void)
{
    MatchSetup ms = {0};
    char report[1800], tiny[8];
    int tick, moving = 0;
    ms.seed = 2121; ms.players = 2; ms.versus = 1; ms.team[1] = TEAM_ENEMY;
    ms.map_size = SIZE_SMALL + 1; ms.biome = BIOME_DESERT; ms.bot_commands = 1;
    ms.bot[0] = ms.bot[1] = AI_NORMAL | MATCH_BOT_RAIDER | MATCH_BOT_SIEGE;
    CHECK(match_start(&live, &ms, NULL) == 0);
    CHECK(match_start(&playback, &ms, NULL) == 0);
    ai_local_reset();
    for (tick = 0; tick < 1200; tick++) {
        InputBits input[MAX_TANKS] = {0};
        int bot;
        for (bot = 0; bot < live.num_tanks; bot++) input[bot] = ai_think_local(&live, bot, 128);
        game_tick(&live, input);
        game_tick(&playback, input); /* replay never computes routes or consumes the bot RNG */
        CHECK(game_checksum(&live) == game_checksum(&playback));
        moving += live.tanks[0].speed != 0 || live.tanks[1].speed != 0;
    }
    CHECK(moving > 100);
    client.state = NC_LOADING; client.map_got = 123; client.map_len = 456;
    strcpy(client.password, "PRIVATE-PASSWORD"); strcpy(client.admin_pw, "PRIVATE-ADMIN");
    memset(client.key, 'Z', sizeof client.key);
    netc_diagnostics(&client, report, sizeof report);
    CHECK(strstr(report, "123 / 456"));
    CHECK(!strstr(report, "PRIVATE-") && !strstr(report, "ZZZZ"));
    memset(tiny, 'X', sizeof tiny); netc_diagnostics(&client, tiny, sizeof tiny);
    CHECK(tiny[sizeof tiny - 1] == 0);
    puts("local AI replay and diagnostics: ok");
    return 0;
}
