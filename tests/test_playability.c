/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include "common/mapcheck.h"
#include "common/operation.h"
#include "platform/amiga/local/pacing.h"

static Game game;
static MapCheck report;
static DesertOperation operation;

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "%d: %s\n", __LINE__, #condition); return 1; } } while (0)

static int check_routes(void)
{
    Map *m = &game.map;
    int x = 30, y;
    map_set_next_size(SIZE_SMALL);
    map_fill(m, TILE_SAND);
    map_custom_begin(m, game.bases, 2, MAP_MODE_FLAG);
    map_place_structure(m, ST_GARAGE, 10, 9, TEAM_PLAYER);
    map_place_structure(m, ST_GARAGE, 43, 42, TEAM_ENEMY);
    for (y = 1; y < m->h - 1; y++) map_set(m, x, y, TILE_WATER);
    map_check_playability(&game, &report, MAP_MODE_FLAG);
    CHECK(report.errors == 2);
    CHECK(strstr(report.issues[0].text, "ENEMY FLAG: NO ROUTE"));

    for (y = 20; y < 24; y++) map_set(m, x, y, TILE_ROAD);
    map_check_playability(&game, &report, MAP_MODE_FLAG);
    CHECK(report.errors == 0);
    CHECK(report.route[20][x] | report.route[21][x] | report.route[22][x] | report.route[23][x]);

    map_place_structure(m, ST_GATE_V, x, 20, TEAM_PLAYER);
    map_check_playability(&game, &report, MAP_MODE_FLAG);
    CHECK(report.errors == 0);
    CHECK((report.route[20][x] | report.route[21][x] | report.route[22][x] | report.route[23][x]) == 9);

    m->mode = MAP_MODE_CONQUEST;
    m->num_points = 1;
    m->point_tx[0] = 30;
    m->point_ty[0] = 32;
    for (y = 25; y < 40; y++)
        for (x = 23; x < 38; x++) map_set(m, x, y, TILE_WATER);
    map_check_playability(&game, &report, MAP_MODE_CLASSIC);
    CHECK(report.errors == 2);
    CHECK(strstr(report.issues[0].text, "CHECKPOINT: NO ROUTE"));
    return 0;
}

static void tick(InputBits player_one)
{
    InputBits inputs[MAX_TANKS] = {0};
    inputs[0] = player_one;
    game_tick(&game, inputs);
    operation_step(&game, &operation);
}

static void position(Tank *tank, fx x, fx y)
{
    tank->x = tank->prev_x = x;
    tank->y = tank->prev_y = y;
    tank->speed = 0;
}

static int check_mission(int players)
{
    MatchSetup crew = {0};
    fx x, y;
    int i, j;
    crew.players = (uint8_t)players;
    CHECK(operation_start(&game, &operation, &crew) == 0);
    CHECK(game.biome == BIOME_DESERT && game.num_tanks == players && operation.phase == OP_GUN);
    CHECK(game.map.st[operation.gun].team == TEAM_INDEPENDENT);
    CHECK(game.flags[TEAM_ENEMY].state == FLAG_HIDDEN && game.tanks[0].lives == 3);
    if (players == 2) CHECK(game.tanks[1].team == TEAM_PLAYER && game.tanks[1].x != game.tanks[0].x);
    for (i = OP_GUN; i <= OP_PRISON; i++) {
        CHECK(operation.phase == i && operation_target(&game, &operation, 0, &x, &y));
        position(&game.tanks[0], x, y + FX(42));
        game.tanks[0].angle = 0;
        game.tanks[0].armor = 255;
        for (j = 0; j < 240 && operation.phase == i; j++) tick(IN_FIRE);
        CHECK(operation.phase == i + 1);
    }
    for (i = 0; i < OP_RESCUE_GOAL && operation.phase == OP_PICKUP; i++) {
        Tank *tank = &game.tanks[i % players];
        int carried = tank->pows;
        if (players == 2) position(&game.tanks[(i + 1) % players], FX(280), FX(480));
        CHECK(operation_target(&game, &operation, i % players, &x, &y));
        position(tank, x, y + FX(11));
        for (j = 0; j < 15 && tank->pows == carried; j++) tick(0);
        CHECK(tank->pows > carried);
    }
    CHECK(operation.phase == OP_RETURN);
    for (i = 0; i < players; i++) {
        CHECK(operation_target(&game, &operation, i, &x, &y));
        position(&game.tanks[i], x + FX(i * 15 - 8), y + FX(27));
    }
    for (i = 0; i < 55 && operation.phase == OP_RETURN; i++) tick(0);
    CHECK(operation.phase == OP_WON && operation.saved >= OP_RESCUE_GOAL && game.winner == TEAM_PLAYER);
    if (players == 2) CHECK(game.tanks[0].saved > 0 && game.tanks[1].saved > 0);

    CHECK(operation_start(&game, &operation, &crew) == 0);
    for (i = 0; i < players; i++) game.tanks[i].out = 1;
    tick(0);
    CHECK(operation.phase == OP_LOST && game.winner == TEAM_ENEMY);
    return 0;
}

int main(void)
{
    CHECK(frame_deadline(100, 110, 130, 40) == 140);
    CHECK(frame_deadline(100, 110, 175, 40) == 175);
    CHECK(frame_deadline(UINT32_MAX - 9u, 2, 13, 40) == 30);
    if (check_routes() || check_mission(1) || check_mission(2)) return 1;
    puts("map routes, frame deadlines and solo/co-op rescue passed");
    return 0;
}
