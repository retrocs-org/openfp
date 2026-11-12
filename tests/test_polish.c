/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include "common/recovery.h"
#include "common/fx_budget.h"
#include "common/client.h"
static Game game, peer;
static Server server;
static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); failures++; } } while (0)
static void command(int client, unsigned sequence, unsigned assignment, int kind)
{
    uint8_t data[256]; char line[64]; Pack k;
    snprintf(line, sizeof line, "/order %u %d", assignment, kind);
    pk_init(&k, data, sizeof data); pk_header(&k, NM_CHAT); pk_u32(&k, sequence); pk_str(&k, line, NET_CHAT - 1);
    server_fuzz_packet(&server, data, k.n, server.c[client].addr.ip, server.c[client].addr.port);
}
static void test_orders(void)
{
    MatchSetup setup = {0}; Replay replay; int i;
    setup.seed = 88; setup.biome = BIOME_DESERT; setup.map_size = SIZE_SMALL + 1;
    setup.players = 4; setup.versus = 1; setup.camp_garages = 2;
    setup.cls[0] = setup.cls[1] = setup.cls[2] = setup.cls[3] = TANK_MEDIUM;
    setup.team[2] = setup.team[3] = TEAM_ENEMY;
    setup.bot[2] = setup.bot[3] = AI_HARD | MATCH_BOT_RAIDER;
    setup.leader_mask = 1; setup.bot_commands = 1;
    CHECK(!match_start(&game, &setup, NULL));
    CHECK(game_orders_allowed(&game));
    CHECK(!game_team_order(&game, 1, ORDER_ATTACK));
    CHECK(!game_team_order(&game, 255, ORDER_ATTACK));
    CHECK(!game_team_order(&game, 0, ORDER_COUNT));
    CHECK(game_team_order(&game, 0, ORDER_DEFEND));
    CHECK(game_order_active(&game, 0));
    game.tick = game.team_order[0].until;
    CHECK(!game_order_active(&game, 0));
    game.tick = 0; game.team_leader[0] = 1;
    CHECK(!game_order_active(&game, 0));
    game.team_leader[0] = 0;
    /* Both the Classic world and Classic rules in another world refuse commands. */
    game.biome = BIOME_CLASSIC; CHECK(!game_team_order(&game, 0, ORDER_ATTACK));
    game.biome = BIOME_DESERT; game.enhanced_rules = 0; CHECK(!game_team_order(&game, 0, ORDER_ATTACK));
    game.enhanced_rules = 1; game.extras = EXTRA_ANNIHILATION; CHECK(!game_orders_allowed(&game));
    game.extras = 0; game.map.custom_bases = 1; game.map.mode = MAP_MODE_CLASSIC; CHECK(!game_orders_allowed(&game));
    CHECK(!match_start(&game, &setup, NULL));
    memset(&server, 0, sizeof server); server.sock = -1; server.state = SRV_RUNNING;
    server.game = game;
    for (i = 0; i < 2; i++) {
        SrvClient *c = &server.c[i];
        c->used = c->authed = 1; c->id = i + 1; c->slot = i;
        c->join_order = c->side_order = (unsigned)i + 1;
        c->assignment = 42; c->chat_tokens = 20;
        c->addr.ip = 0x7f000001; c->addr.port = (uint16_t)(45000 + i);
        snprintf(c->name, sizeof c->name, "human%d", i);
    }
    command(1, 1, 42, ORDER_ATTACK); CHECK(!server.game.team_order[0].kind);
    command(0, 1, 41, ORDER_ATTACK); CHECK(!server.game.team_order[0].kind);
    command(0, 2, 42, ORDER_ATTACK); CHECK(server.game.team_order[0].kind == ORDER_ATTACK);
    server.game.biome = BIOME_CLASSIC;
    command(0, 3, 42, ORDER_ESCORT); CHECK(server.game.team_order[0].kind == ORDER_ATTACK);
    server.game.biome = BIOME_DESERT; server.c[0].gone = 1;
    command(1, 2, 42, ORDER_REGROUP); CHECK(server.game.team_order[0].kind == ORDER_REGROUP && server.game.team_order[0].issuer == 1);
    /* Resolved bot inputs, including order-driven decisions, replay identically. */
    setup.bot[1] = AI_HARD;
    CHECK(!match_start(&game, &setup, NULL)); CHECK(!match_start(&peer, &setup, NULL));
    replay_begin(&replay, &setup, NULL, game.num_tanks);
    for (i = 0; i < 300; i++) {
        InputBits inputs[MAX_TANKS] = {0};
        if (!i || i == 100) CHECK(game_team_order(&game, 0, i ? ORDER_ESCORT : ORDER_ATTACK));
        game_tick_server(&game, inputs); game_tick(&peer, inputs);
        CHECK(game_checksum(&game) == game_checksum(&peer));
        replay_tick(&replay, inputs, &game);
    }
    CHECK(replay_verify(&replay, &peer) == -1); replay_free(&replay);
    CHECK(game_team_order(&game, 0, ORDER_NONE)); CHECK(!game_order_active(&game, 0));
    CHECK(!match_start(&game, &setup, NULL));
    CHECK(game_team_order(&game, 0, ORDER_REGROUP));
    (void)ai_think(&game, 1);
    CHECK(game.ai[1].goal_tx == game.team_order[0].tx && game.ai[1].goal_ty == game.team_order[0].ty);
}
static void test_recovery(void)
{
    char text[64]; FILE *f;
    recovery_clear("polish-recovery");
    CHECK(!recovery_write("polish-recovery", "first unfinished map"));
    CHECK(!recovery_write("polish-recovery", "second unfinished map"));
    CHECK(!recovery_write("polish-recovery", "third unfinished map"));
    CHECK(!recovery_write("polish-recovery", "fourth unfinished map"));
    f = fopen("polish-recovery.1.draft", "wb"); CHECK(f != NULL);
    if (f) { fputs("OFPDRAFT 1 4 999 00000000\ntruncated", f); fclose(f); }
    CHECK(recovery_read("polish-recovery", text, sizeof text) && !strcmp(text, "third unfinished map"));
    CHECK(!recovery_read("polish-recovery", text, 4));
    recovery_clear("polish-recovery"); CHECK(!recovery_read("polish-recovery", text, sizeof text));
}
static void test_budget(void)
{
    FxBudget budget = {0}; int i;
    for (i = 0; i < 150; i++) fx_budget_step(&budget, 50, 40, 1);
    CHECK(budget.quality == 2);
    for (i = 0; i < 200; i++) fx_budget_step(&budget, 38, 40, 1);
    CHECK(budget.quality == 2); /* no oscillation around the threshold */
    for (i = 0; i < 500; i++) fx_budget_step(&budget, 20, 40, 1);
    CHECK(!budget.quality);
    CHECK(!fx_budget_step(&budget, 90, 40, 0));
}
int main(void) {
    test_recovery(); test_budget(); test_orders();
    printf("recovery, visual budget, Classic restrictions and server orders: %s\n", failures ? "FAILED" : "ok");
    return !!failures;
}
