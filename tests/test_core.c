/* SPDX-License-Identifier: MIT
 * Core tests: no SDL, runs anywhere (also a good first target on 68k). */
#include <stdio.h>
#include <string.h>
#include "core/game.h"
#include "core/match.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static Game a, b, c;

static InputBits script(uint32_t tick)
{
    InputBits in = IN_UP;
    if ((tick / 60) % 3 == 1) in |= IN_RIGHT;
    if ((tick / 90) % 4 == 2) in = IN_DOWN | IN_LEFT;
    if ((tick / 35) % 2)      in |= IN_FIRE;
    return in;
}

static uint32_t run(Game *g, uint32_t seed, int ticks)
{
    int i;
    game_init(g, seed, BIOME_CLASSIC);
    game_add_tank_at_base(g, TANK_MEDIUM, TEAM_PLAYER);
    for (i = 0; i < ticks; i++) {
        InputBits in[MAX_TANKS] = { 0 };
        in[0] = script(g->tick);
        game_tick(g, in);
    }
    return game_checksum(g);
}

static void test_trig(void)
{
    CHECK(fx_sin(ANGLE_NORTH) == 0);
    CHECK(fx_sin(ANGLE_EAST) == FX_ONE);
    CHECK(fx_cos(ANGLE_NORTH) == FX_ONE);
    CHECK(fx_cos(ANGLE_SOUTH) == -FX_ONE);
}

static void test_biomes(void)
{
    int bi;
    for (bi = 0; bi < BIOME_COUNT; bi++) {           /* every biome generates and plays */
        int i;
        game_init(&a, 7, (Biome)bi);
        game_add_tank_at_base(&a, TANK_LIGHT, TEAM_PLAYER);
        CHECK(!map_box_solid(&a.map, a.tanks[0].x, a.tanks[0].y, TANK_HALF));
        for (i = 0; i < 500; i++) {
            InputBits in[MAX_TANKS] = { 0 };
            in[0] = script(a.tick);
            game_tick(&a, in);
        }
        CHECK((int)biome_from_name(biome_name((Biome)bi)) == bi);
    }
    game_init(&a, 7, BIOME_CLASSIC);
    game_init(&c, 7, BIOME_JUNGLE);
    CHECK(game_checksum(&a) != game_checksum(&c));
}

static void test_determinism(void)
{
    uint32_t h1 = run(&a, 42, 3000);
    uint32_t h2 = run(&b, 42, 3000);
    uint32_t h3 = run(&b, 43, 3000);
    CHECK(h1 == h2);
    CHECK(h1 != h3);
    printf("  seed 42 checksum after 3000 ticks: %08x\n", (unsigned)h1);
}

/* tank at tile (10,10) facing east, target at (12,10) */
static void setup_range(Game *g, TileType target)
{
    game_init(g, 1, BIOME_CLASSIC);
    map_fill(&g->map, TILE_SAND); g->num_props = 0;
    map_set(&g->map, 12, 10, target);
    game_add_tank(g, TANK_MEDIUM, 0, FX(10 * 16 + 8), FX(10 * 16 + 8));
    g->tanks[0].angle = g->tanks[0].prev_angle = ANGLE_EAST;
}

static void fire_for(Game *g, int ticks, InputBits in0)
{
    int i;
    for (i = 0; i < ticks; i++) {
        InputBits in[MAX_TANKS] = { 0 };
        in[0] = in0;
        game_tick(g, in);
    }
}

static void test_destruction(void)
{
    setup_range(&a, TILE_WALL);
    fire_for(&a, 15, IN_FIRE);                       /* first shot only */
    CHECK(map_get(&a.map, 12, 10) == TILE_WALL);
    CHECK(a.map.hp[10][12] == tile_info[TILE_WALL].hp - 1);
    fire_for(&a, 200, IN_FIRE);
    CHECK(map_get(&a.map, 12, 10) == TILE_RUBBLE);

    setup_range(&a, TILE_BUILDING);
    fire_for(&a, 400, IN_FIRE);
    CHECK(map_get(&a.map, 12, 10) == TILE_RUINS);

    setup_range(&a, TILE_ROCK);                      /* indestructible */
    fire_for(&a, 400, IN_FIRE);
    CHECK(map_get(&a.map, 12, 10) == TILE_ROCK);

    setup_range(&a, TILE_WATER);                     /* shots fly over water */
    fire_for(&a, 3, IN_FIRE);
    fire_for(&a, 40, 0);
    CHECK(map_get(&a.map, 12, 10) == TILE_WATER);
}

static void test_structures(void)
{
    int idx, x, y, ruins = 0;
    /* a 3x2 barracks right in front of the tank: shoot ONE tile of it */
    game_init(&a, 1, BIOME_CLASSIC);
    map_fill(&a.map, TILE_SAND); a.num_props = 0;
    idx = map_place_structure(&a.map, ST_BARRACKS, 12, 9, TEAM_ENEMY);
    CHECK(idx == 0);
    CHECK(map_get(&a.map, 14, 10) == TILE_BUILDING && a.map.sid[10][14] == 1);
    game_add_tank(&a, TANK_MEDIUM, 0, FX(10 * 16 + 8), FX(10 * 16 + 8));
    a.tanks[0].angle = a.tanks[0].prev_angle = ANGLE_EAST;
    fire_for(&a, 16, IN_FIRE);
    CHECK(a.map.st[0].hp == struct_info[ST_BARRACKS].hp - 1);
    CHECK(map_get(&a.map, 12, 10) == TILE_BUILDING);        /* still standing */
    fire_for(&a, 400, IN_FIRE);
    CHECK(a.map.st[0].hp == 0);
    for (y = 9; y < 11; y++)                                /* whole footprint is gone */
        for (x = 12; x < 15; x++)
            ruins += map_get(&a.map, x, y) == TILE_RUINS && !a.map.sid[y][x];
    CHECK(ruins == 6);
    CHECK(a.tanks[0].score >= struct_info[ST_BARRACKS].score * 5u);    /* plus any runners hit */
    {   /* the barracks' men ran out and scattered */
        int i, fleeing = 0;
        for (i = 0; i < MAX_MEN; i++) fleeing += a.men[i].kind == MAN_SOLDIER;
        CHECK(fleeing + (int)a.tanks[0].kills >= 3);
    }

    /* generated maps contain several kinds of structure */
    game_init(&a, 42, BIOME_CLASSIC);
    {
        int seen[ST_TYPE_COUNT] = { 0 }, i, kinds = 0;
        for (i = 0; i < a.map.num_st; i++) seen[a.map.st[i].type]++;
        for (i = 1; i < ST_TYPE_COUNT; i++) kinds += seen[i] > 0;
        CHECK(kinds >= 5);
        printf("  map has %d structures, %d kinds\n", a.map.num_st, kinds);
    }
}

static void test_atan2(void)
{
    CHECK(fx_atan2(0, -FX(10)) == ANGLE_NORTH);
    CHECK(fx_atan2(FX(10), 0) == ANGLE_EAST);
    CHECK(fx_atan2(0, FX(10)) == ANGLE_SOUTH);
    CHECK(fx_atan2(-FX(10), 0) == ANGLE_WEST);
    {   /* every direction round-trips within half a degree */
        int an, worst = 0;
        for (an = 0; an < 65536; an += 97) {
            angle16 dir = (angle16)(an & 0xFF00);       /* the sine table has 256 steps */
            angle16 got = fx_atan2(fx_mul(FX(50), fx_sin(dir)), -fx_mul(FX(50), fx_cos(dir)));
            int d = (int16_t)(got - dir);
            if (d < 0) d = -d;
            if (d > worst) worst = d;
        }
        CHECK(worst < 64);      /* < 0.35 degrees */
    }
}

static void test_garage_start(void)
{
    const Base *base;
    int i;
    game_init(&a, 42, BIOME_CLASSIC);
    base = &a.bases[TEAM_PLAYER];
    i = game_add_tank_at_base(&a, TANK_MEDIUM, TEAM_PLAYER);
    CHECK(base->num_garages == 4);                                     /* one per tank, and a spare */
    CHECK(a.bases[TEAM_ENEMY].num_garages == 4);                       /* (the fortress too: 4 vs 4) */
    for (i = 0; i < 4; i++) CHECK(a.map.st[a.bases[TEAM_ENEMY].garages[i]].type == ST_GARAGE);
    worldgen_set_camp_garages(3);                                      /* (the original's: three each) */
    game_init(&b, 42, BIOME_CLASSIC);
    CHECK(b.bases[TEAM_PLAYER].num_garages == 3 && b.bases[TEAM_ENEMY].num_garages == 3);
    worldgen_set_camp_garages(4);
    for (i = 0; i < 4; i++) {
        CHECK(a.map.st[base->garages[i]].type == ST_GARAGE);
        CHECK(a.map.sid[base->class_ty[i]][base->class_tx[i]] == base->garages[i] + 1);
        CHECK(map_get(&a.map, base->class_tx[i], base->class_ty[i]) == TILE_FLOOR);
    }
    i = 0;
    CHECK(a.tanks[i].angle == ANGLE_SOUTH);
    CHECK(fx_to_int(a.tanks[i].x) / 16 == base->class_tx[TANK_MEDIUM]);  /* the medium tank's garage */
    fire_for(&a, 1, IN_FIRE);                                             /* garages can't be hurt */
    fire_for(&a, 150, IN_UP);                                             /* straight out of the gate */
    CHECK(fx_to_int(a.tanks[i].y) / 16 > base->y1);
    CHECK(a.map.st[a.bases[TEAM_ENEMY].garage].type == ST_GARAGE);
    for (i = 0; i < 4; i++) {             /* every garage's tank gets out and through the gate */
        int k, j;
        game_init(&a, 42 + i, BIOME_CLASSIC);
        base = &a.bases[TEAM_PLAYER];
        j = game_add_tank_in_garage(&a, (TankClass)(i % 3), TEAM_PLAYER, i);
        fire_for(&a, 40, IN_UP);                                       /* out of the door */
        CHECK(fx_to_int(a.tanks[j].y) / 16 > base->class_ty[i] + 1);
        for (k = 0; k < 900 && fx_to_int(a.tanks[j].y) / 16 <= base->y1; k++) {
            int tx = fx_to_int(a.tanks[j].x) / 16, gate = base->x0 + 16;
            InputBits in = IN_UP;
            angle16 want = tx < gate ? 0x6000 : tx > gate + 1 ? 0xA000 : ANGLE_SOUTH;   /* steer to the gate */
            int16_t d = (int16_t)(want - a.tanks[j].angle);
            if (d > 0x200) in |= IN_RIGHT; else if (d < -0x200) in |= IN_LEFT;
            fire_for(&a, 1, in);
        }
        CHECK(fx_to_int(a.tanks[j].y) / 16 > base->y1);
    }
}

static void test_turrets(void)
{
    int t, k;
    /* an enemy MG nest next to the tank: it turns, shoots, the tank loses armour */
    game_init(&a, 1, BIOME_CLASSIC);
    map_fill(&a.map, TILE_SAND); a.num_props = 0;
    t = game_add_tank(&a, TANK_LIGHT, TEAM_PLAYER, FX(40 * 16 + 8), FX(40 * 16 + 8));
    map_place_structure(&a.map, ST_T_MG, 46, 40, TEAM_ENEMY);
    fire_for(&a, 60, 0);
    CHECK(a.tanks[t].armor < tank_stats[TANK_LIGHT].armor);
    for (k = 0; k < 2000 && a.tanks[t].respawn == 0; k++)
        fire_for(&a, 1, 0);
    CHECK(a.tanks[t].respawn > 0);                                         /* wrecked */
    CHECK(a.tanks[t].lives == tank_stats[TANK_LIGHT].lives - 1);
    fire_for(&a, RESPAWN_TICKS + 1, 0);
    CHECK(a.tanks[t].respawn == 0 && a.tanks[t].armor == tank_stats[TANK_LIGHT].armor);
    CHECK(fx_to_int(a.tanks[t].x) / 16 == a.bases[TEAM_PLAYER].class_tx[TANK_LIGHT]);  /* back in its garage */

    /* our own turrets ignore us; shooting an enemy turret destroys it */
    game_init(&a, 1, BIOME_CLASSIC);
    map_fill(&a.map, TILE_SAND); a.num_props = 0;
    t = game_add_tank(&a, TANK_HEAVY, TEAM_PLAYER, FX(10 * 16 + 8), FX(10 * 16 + 8));
    a.tanks[t].angle = a.tanks[t].prev_angle = ANGLE_EAST;
    map_place_structure(&a.map, ST_T_CANNON, 10, 14, TEAM_PLAYER);
    map_place_structure(&a.map, ST_T_CANNON, 13, 10, TEAM_ENEMY);
    fire_for(&a, 400, IN_FIRE);
    CHECK(a.map.st[1].hp == 0);
    CHECK(a.map.st[0].hp > 0);
    CHECK(a.tanks[t].lives == tank_stats[TANK_HEAVY].lives);
}

static int count_men(const Game *g, int kind)
{
    int i, n = 0;
    for (i = 0; i < MAX_MEN; i++) n += g->men[i].kind == kind;
    return n;
}

static void test_pows(void)
{
    int t, k, before;
    /* blow open a POW camp: prisoners come out, run to the stopped tank, climb in */
    game_init(&a, 3, BIOME_CLASSIC);
    map_fill(&a.map, TILE_SAND); a.num_props = 0;
    memset(a.men, 0, sizeof a.men);
    t = game_add_tank(&a, TANK_HEAVY, TEAM_PLAYER, FX(10 * 16 + 8), FX(10 * 16 + 8));
    a.tanks[t].angle = a.tanks[t].prev_angle = ANGLE_EAST;
    map_place_structure(&a.map, ST_POWCAMP, 13, 9, TEAM_ENEMY);
    for (k = 0; k < 600 && a.map.st[0].hp; k++) fire_for(&a, 1, IN_FIRE);
    CHECK(a.map.st[0].hp == 0);
    before = count_men(&a, MAN_POW);
    CHECK(before >= 5);
    fire_for(&a, 400, 0);
    CHECK(a.tanks[t].pows == before);
    CHECK(count_men(&a, MAN_POW) == 0);

    /* drive them to our first aid, stop, and they get out; 15 earns a tank */
    map_place_structure(&a.map, ST_FIRSTAID, 30, 9, TEAM_PLAYER);
    a.tanks[t].x = FX(31 * 16); a.tanks[t].y = FX(11 * 16 + 8);
    a.tanks[t].saved = MEN_PER_LIFE - 2;
    before = a.tanks[t].lives;
    fire_for(&a, 200, 0);
    CHECK(a.tanks[t].pows == 0);
    CHECK(a.tanks[t].lives == before + 1);
    CHECK(a.tanks[t].msg == MSG_EXTRA_LIFE || a.tanks[t].msg == MSG_SAVED || a.tanks[t].msg == MSG_HELI);

    /* drive into them instead and they die */
    game_init(&a, 3, BIOME_CLASSIC);
    map_fill(&a.map, TILE_SAND); a.num_props = 0;
    memset(a.men, 0, sizeof a.men);
    t = game_add_tank(&a, TANK_LIGHT, TEAM_PLAYER, FX(10 * 16 + 8), FX(10 * 16 + 8));
    a.tanks[t].angle = a.tanks[t].prev_angle = ANGLE_EAST;
    a.men[0].kind = MAN_POW; a.men[0].x = a.men[0].prev_x = FX(14 * 16); a.men[0].y = a.men[0].prev_y = FX(10 * 16 + 8);
    fire_for(&a, 60, IN_UP);
    CHECK(count_men(&a, MAN_POW) == 0 && a.tanks[t].pows == 0);
}

static void test_helicopters_flag_fuel(void)
{
    int t, k, helis = 0, cap;
    /* standing still out in the open brings helicopters */
    game_init(&a, 5, BIOME_CLASSIC);
    map_fill(&a.map, TILE_SAND); a.num_props = 0;
    t = game_add_tank(&a, TANK_HEAVY, TEAM_PLAYER, FX(60 * 16), FX(60 * 16));
    fire_for(&a, CAMP_TICKS - 5, 0);
    for (k = 0; k < MAX_HELIS; k++) helis += a.helis[k].active;
    CHECK(helis == 0);
    fire_for(&a, 10, 0);
    for (k = 0, helis = 0; k < MAX_HELIS; k++) helis += a.helis[k].active;
    CHECK(helis == 1);
    fire_for(&a, 600, 0);
    CHECK(a.tanks[t].armor < tank_stats[TANK_HEAVY].armor);              /* they shoot */

    /* the enemy flag: open its bunker, drive over it, bring it home = win */
    game_init(&a, 42, BIOME_CLASSIC);
    t = game_add_tank_at_base(&a, TANK_LIGHT, TEAM_PLAYER);
    CHECK(a.flags[TEAM_ENEMY].state == FLAG_HIDDEN);
    {
        int fh = a.bases[TEAM_ENEMY].flag_house;
        Structure *st = &a.map.st[fh];
        int dummy;
        CHECK(fh >= 0 && st->type == ST_FLAGHOUSE);
        st->hp = 1;
        a.map.sid[st->y][st->x] = (uint16_t)(fh + 1);
        CHECK(map_damage(&a.map, st->x, st->y, 1, &dummy) == DMG_STRUCT);
    }
    /* what a shot would do on top: */
    a.flags[TEAM_ENEMY].state = FLAG_GROUND;
    a.tanks[t].x = a.flags[TEAM_ENEMY].x; a.tanks[t].y = a.flags[TEAM_ENEMY].y;
    fire_for(&a, 1, 0);
    CHECK(a.tanks[t].has_flag && a.flags[TEAM_ENEMY].state == FLAG_CARRIED);
    CHECK(a.winner < 0);
    a.tanks[t].x = FX(a.bases[TEAM_PLAYER].spawn_tx * 16 + 8);
    a.tanks[t].y = FX(a.bases[TEAM_PLAYER].spawn_ty * 16 + 8);
    fire_for(&a, 1, 0);
    CHECK(a.winner == TEAM_PLAYER);

    /* running dry strands the tank and costs a life */
    game_init(&a, 5, BIOME_CLASSIC);
    map_fill(&a.map, TILE_SAND); a.num_props = 0;
    t = game_add_tank(&a, TANK_MEDIUM, TEAM_PLAYER, FX(60 * 16), FX(60 * 16));
    cap = a.tanks[t].lives;
    a.tanks[t].fuel = 3;
    fire_for(&a, 5, IN_LEFT | IN_UP);
    CHECK(a.tanks[t].lives == cap - 1 && a.tanks[t].respawn > 0 && a.tanks[t].msg == MSG_NO_FUEL);
}

static void test_flee_and_helipads(void)
{
    int t, k, i, fled = 0;
    fx x0[MAX_MEN];
    /* soldiers pour out of a destroyed barracks and run away from the tank */
    game_init(&a, 11, BIOME_CLASSIC);
    map_fill(&a.map, TILE_SAND); a.num_props = 0;
    memset(a.men, 0, sizeof a.men);
    t = game_add_tank(&a, TANK_HEAVY, TEAM_PLAYER, FX(10 * 16 + 8), FX(10 * 16 + 8));
    a.tanks[t].angle = a.tanks[t].prev_angle = ANGLE_EAST;
    map_place_structure(&a.map, ST_BARRACKS, 13, 9, TEAM_ENEMY);
    for (k = 0; k < 800 && a.map.st[0].hp; k++) fire_for(&a, 1, IN_FIRE);
    CHECK(a.map.st[0].hp == 0);
    for (i = 0; i < MAX_MEN; i++) x0[i] = a.men[i].x;
    fire_for(&a, 40, 0);
    for (i = 0; i < MAX_MEN; i++)
        if (a.men[i].kind == MAN_SOLDIER && a.men[i].x > x0[i]) fled++;   /* east, away from us */
    CHECK(fled >= 2);

    /* camping near a helipad: the parked helicopter takes off from it */
    game_init(&a, 11, BIOME_CLASSIC);
    map_fill(&a.map, TILE_SAND); a.num_props = 0;
    k = map_place_structure(&a.map, ST_HELIPAD, 40, 40, TEAM_ENEMY);
    CHECK(map_get(&a.map, 41, 41) == TILE_FLOOR && a.map.st[k].aim == 0);
    t = game_add_tank(&a, TANK_HEAVY, TEAM_PLAYER, FX(55 * 16), FX(41 * 16));
    fire_for(&a, CAMP_TICKS + 2, 0);
    CHECK(a.helis[0].active && a.helis[0].pad == k && a.map.st[k].aim == PAD_AWAY);
}

static void test_turret_types(void)
{
    int t, k, shots, y;
    /* an MG nest needs a good while to wear down even the light tank */
    game_init(&a, 21, BIOME_CLASSIC);
    map_fill(&a.map, TILE_SAND); a.num_props = 0;
    t = game_add_tank(&a, TANK_LIGHT, TEAM_PLAYER, FX(40 * 16 + 8), FX(40 * 16 + 8));
    map_place_structure(&a.map, ST_T_MG, 46, 40, TEAM_ENEMY);
    for (k = 0; k < 3000 && !a.tanks[t].respawn; k++) {
        fire_for(&a, 1, (k / 25) % 2 ? IN_LEFT : IN_RIGHT);    /* jiggle so no helicopters come */
        a.tanks[t].still = 0;
    }
    CHECK(k > 5 * TICK_RATE);
    printf("  MG nest needs %d ticks for a light tank\n", k);

    /* the twin auto-cannon fires in bursts */
    game_init(&a, 21, BIOME_CLASSIC);
    map_fill(&a.map, TILE_SAND); a.num_props = 0;
    memset(a.shots, 0, sizeof a.shots);
    t = game_add_tank(&a, TANK_HEAVY, TEAM_PLAYER, FX(40 * 16 + 8), FX(40 * 16 + 8));
    map_place_structure(&a.map, ST_T_TWIN, 46, 40, TEAM_ENEMY);
    for (k = 0, shots = 0; k < 40; k++) {
        int i, n = 0;
        fire_for(&a, 1, 0);
        for (i = 0; i < MAX_SHOTS; i++) n += a.shots[i].active && a.shots[i].team == TEAM_ENEMY;
        if (n > shots) shots = n;
    }
    CHECK(shots >= 3);

    /* the mortar lobs over a wall and its shell bursts next to the tank */
    game_init(&a, 21, BIOME_CLASSIC);
    map_fill(&a.map, TILE_SAND); a.num_props = 0;
    t = game_add_tank(&a, TANK_HEAVY, TEAM_PLAYER, FX(40 * 16 + 8), FX(40 * 16 + 8));
    for (y = 34; y < 47; y++) map_set(&a.map, 44, y, TILE_WALL);   /* cover */
    map_place_structure(&a.map, ST_T_MORTAR, 48, 40, TEAM_ENEMY);
    fire_for(&a, 250, 0);
    CHECK(a.tanks[t].armor < tank_stats[TANK_HEAVY].armor);
    CHECK(map_get(&a.map, 44, 40) == TILE_WALL);                   /* untouched: it went over */
}

static void test_tank_steering(void)
{
    int t;
    fx x0, y0;
    angle16 a0;
    /* tank controls: left/right only turn the hull on the spot, up/down only drive */
    game_init(&a, 32, BIOME_CLASSIC);
    map_fill(&a.map, TILE_SAND); a.num_props = 0;
    memset(a.mines, 0, sizeof a.mines);
    t = game_add_tank(&a, TANK_MEDIUM, TEAM_PLAYER, FX(60 * 16), FX(60 * 16));
    CHECK(a.tanks[t].steer == STEER_TANK);
    x0 = a.tanks[t].x; y0 = a.tanks[t].y; a0 = a.tanks[t].angle;
    fire_for(&a, 40, IN_LEFT);
    CHECK(a.tanks[t].x == x0 && a.tanks[t].y == y0 && a.tanks[t].angle != a0);
    a0 = a.tanks[t].angle;
    fire_for(&a, 40, IN_UP);
    CHECK(a.tanks[t].angle == a0 && (a.tanks[t].x != x0 || a.tanks[t].y != y0));
}

static void test_direct_steering(void)
{
    int t, k;
    /* the original's controls: push left and the tank turns to face west and goes */
    game_init(&a, 31, BIOME_CLASSIC);
    map_fill(&a.map, TILE_SAND); a.num_props = 0;
    t = game_add_tank(&a, TANK_MEDIUM, TEAM_PLAYER, FX(60 * 16), FX(60 * 16));
    a.tanks[t].steer = STEER_DIRECT;
    a.tanks[t].angle = a.tanks[t].prev_angle = ANGLE_NORTH;
    fire_for(&a, 60, IN_LEFT);
    CHECK(a.tanks[t].angle == ANGLE_WEST);
    CHECK(fx_to_int(a.tanks[t].x) < 60 * 16 - 20);
    /* pull the other way: it backs up instead of turning round */
    for (k = 0; k < 30; k++) fire_for(&a, 1, 0);
    fire_for(&a, 40, IN_RIGHT);
    CHECK(a.tanks[t].angle == ANGLE_WEST && a.tanks[t].speed < 0);
    /* stop, pull down and fire: a mine */
    for (k = 0; k < 60 && a.tanks[t].speed; k++) fire_for(&a, 1, 0);
    fire_for(&a, 1, IN_DOWN | IN_FIRE);
    CHECK(a.tanks[t].mines == tank_stats[TANK_MEDIUM].mines - 1);
}

static void test_grass_own_buildings_fire_on_move(void)
{
    int t, k, i;
    fx x0;
    /* bushes don't stop a tank; they get flattened */
    game_init(&a, 41, BIOME_CLASSIC);
    map_fill(&a.map, TILE_SAND); a.num_props = 0;
    memset(a.mines, 0, sizeof a.mines);
    t = game_add_tank(&a, TANK_MEDIUM, TEAM_PLAYER, FX(20 * 16 + 8), FX(20 * 16 + 8));
    a.tanks[t].angle = a.tanks[t].prev_angle = ANGLE_EAST;
    for (k = 22; k < 26; k++) map_set(&a.map, k, 20, TILE_TREE);
    fire_for(&a, 100, IN_UP);
    CHECK(fx_to_int(a.tanks[t].x) / 16 > 26);
    CHECK(map_get(&a.map, 23, 20) == TILE_STUMP);

    /* driving and shooting at the same time, also backwards */
    x0 = a.tanks[t].x;
    for (i = 0; i < MAX_SHOTS; i++) a.shots[i].active = 0;
    fire_for(&a, 30, IN_UP | IN_FIRE);
    CHECK(a.tanks[t].x > x0);
    for (k = 0, i = 0; i < MAX_SHOTS; i++) k += a.shots[i].active;
    CHECK(k >= 1);
    for (k = 0; k < 80 && a.tanks[t].speed; k++) fire_for(&a, 1, 0);
    x0 = a.tanks[t].x;
    fire_for(&a, 30, IN_DOWN | IN_FIRE);                       /* from a standstill */
    CHECK(a.tanks[t].x < x0 && a.tanks[t].mines == tank_stats[TANK_MEDIUM].mines);

    /* our own buildings can be knocked down too (no points for it) */
    game_init(&a, 41, BIOME_CLASSIC);
    map_fill(&a.map, TILE_SAND); a.num_props = 0;
    memset(a.mines, 0, sizeof a.mines);
    t = game_add_tank(&a, TANK_HEAVY, TEAM_PLAYER, FX(10 * 16 + 8), FX(10 * 16 + 8));
    a.tanks[t].angle = a.tanks[t].prev_angle = ANGLE_EAST;
    map_place_structure(&a.map, ST_BUNKER, 13, 9, TEAM_PLAYER);
    for (k = 0; k < 600 && a.map.st[0].hp; k++) fire_for(&a, 1, IN_FIRE);
    CHECK(a.map.st[0].hp == 0 && a.tanks[t].score == 0);

    /* a gun behind a wall holds its fire */
    game_init(&a, 41, BIOME_CLASSIC);
    map_fill(&a.map, TILE_SAND); a.num_props = 0;
    memset(a.mines, 0, sizeof a.mines);
    memset(a.men, 0, sizeof a.men);                           /* no stray soldiers either */
    t = game_add_tank(&a, TANK_LIGHT, TEAM_PLAYER, FX(40 * 16 + 8), FX(40 * 16 + 8));
    for (k = 36; k < 45; k++) map_set(&a.map, 43, k, TILE_WALL);
    map_place_structure(&a.map, ST_T_CANNON, 46, 40, TEAM_ENEMY);
    for (k = 0; k < 200; k++) { fire_for(&a, 1, 0); a.tanks[t].still = 0; }
    for (k = 0, i = 0; i < MAX_SHOTS; i++) k += a.shots[i].active && a.shots[i].team == TEAM_ENEMY;
    CHECK(k == 0 && a.tanks[t].armor == tank_stats[TANK_LIGHT].armor);
    map_set(&a.map, 43, 40, TILE_RUBBLE);                     /* open a hole: now it shoots */
    for (k = 0; k < 200; k++) { fire_for(&a, 1, 0); a.tanks[t].still = 0; }
    CHECK(a.tanks[t].armor < tank_stats[TANK_LIGHT].armor);

    /* driving onto an enemy mine sets it off at once and it is gone */
    game_init(&a, 41, BIOME_CLASSIC);
    map_fill(&a.map, TILE_SAND); a.num_props = 0;
    memset(a.mines, 0, sizeof a.mines);
    t = game_add_tank(&a, TANK_HEAVY, TEAM_PLAYER, FX(30 * 16 + 8), FX(30 * 16 + 8));
    a.tanks[t].angle = a.tanks[t].prev_angle = ANGLE_EAST;
    a.mines[0].active = 1; a.mines[0].team = TEAM_ENEMY; a.mines[0].arm = 0;
    a.mines[0].x = FX(33 * 16 + 8); a.mines[0].y = FX(30 * 16 + 8);
    fire_for(&a, 90, IN_UP);
    CHECK(!a.mines[0].active && a.tanks[t].armor < tank_stats[TANK_HEAVY].armor);
}

static void test_vehicles(void)
{
    int t, k, men = 0;
    /* maps come with parked vehicles, in both camps and out on the roads */
    game_init(&a, 51, BIOME_CLASSIC);
    CHECK(a.num_props > 10);
    /* a truck blocks the way, takes 3 hits, leaves a wreck, and its crew runs */
    map_fill(&a.map, TILE_SAND); a.num_props = 0;
    memset(a.mines, 0, sizeof a.mines);
    memset(a.men, 0, sizeof a.men);
    t = game_add_tank(&a, TANK_MEDIUM, TEAM_PLAYER, FX(20 * 16 + 8), FX(20 * 16 + 8));
    a.tanks[t].angle = a.tanks[t].prev_angle = ANGLE_EAST;
    a.props[0].x = FX(26 * 16); a.props[0].y = FX(20 * 16 + 8); a.props[0].angle = ANGLE_NORTH;
    a.props[0].kind = PROP_TRUCK; a.props[0].hp = prop_info[PROP_TRUCK].hp;
    a.props[0].team = TEAM_ENEMY; a.props[0].alive = a.props[0].used = 1; a.num_props = 1;
    fire_for(&a, 120, IN_UP);
    CHECK(fx_to_int(a.tanks[t].x) < 26 * 16 - 8);             /* stopped by the truck */
    for (k = 0; k < 300 && a.props[0].alive; k++) fire_for(&a, 1, IN_FIRE);
    CHECK(!a.props[0].alive && a.props[0].used);
    for (k = 0; k < MAX_MEN; k++) men += a.men[k].kind == MAN_SOLDIER;
    CHECK(men + (int)a.tanks[t].kills >= 1);
    fire_for(&a, 120, IN_UP);                                  /* the wreck doesn't block */
    CHECK(fx_to_int(a.tanks[t].x) > 26 * 16);
}

static void put_man(Game *g, int i, int team, int tx, int ty)
{
    Man *m = &g->men[i];
    memset(m, 0, sizeof *m);
    m->kind = MAN_SOLDIER; m->team = (uint8_t)team; m->home = 255;
    m->x = m->prev_x = m->tx = FX(tx * 16 + 8);
    m->y = m->prev_y = m->ty = FX(ty * 16 + 8);
}

static void test_no_strangers_at_home(void)
{
    int seed, i;
    for (seed = 1; seed <= 12; seed++) {           /* nothing half built over, nobody hostile inside */
        const Base *home;
        game_init(&a, (uint32_t)seed, (Biome)(seed % 4));
        home = &a.bases[TEAM_PLAYER];
        for (i = 0; i < a.map.num_st; i++) {
            const Structure *st = &a.map.st[i];
            if (st->type != ST_NONE) CHECK(a.map.sid[st->y][st->x] == i + 1);
        }
        for (i = 0; i < MAX_MEN; i++) {
            const Man *m = &a.men[i];
            int x = fx_to_int(m->x) / 16, y = fx_to_int(m->y) / 16;
            if (m->kind == MAN_SOLDIER && x >= home->x0 && x <= home->x1 && y >= home->y0 && y <= home->y1)
                CHECK(m->team == TEAM_PLAYER);
        }
    }
}

static void test_soldier_sides(void)
{
    int t, k, i, ours = 0, theirs = 0;
    /* both camps have their own soldiers */
    game_init(&a, 61, BIOME_CLASSIC);
    for (i = 0; i < MAX_MEN; i++)
        if (a.men[i].kind == MAN_SOLDIER) { ours += a.men[i].team == TEAM_PLAYER; theirs += a.men[i].team == TEAM_ENEMY; }
    CHECK(ours > 0 && theirs > 0);

    /* ours never shoot our tank, and our tank doesn't run them over */
    map_fill(&a.map, TILE_SAND); a.num_props = 0;
    memset(a.mines, 0, sizeof a.mines);
    memset(a.men, 0, sizeof a.men);
    memset(a.shots, 0, sizeof a.shots);
    t = game_add_tank(&a, TANK_LIGHT, TEAM_PLAYER, FX(40 * 16 + 8), FX(40 * 16 + 8));
    for (i = 0; i < 4; i++) put_man(&a, i, TEAM_PLAYER, 42 + i, 40);
    for (k = 0; k < 600; k++) { fire_for(&a, 1, 0); a.tanks[t].still = 0; }
    CHECK(a.tanks[t].armor == tank_stats[TANK_LIGHT].armor);
    a.tanks[t].angle = a.tanks[t].prev_angle = ANGLE_EAST;
    fire_for(&a, 60, IN_UP);
    for (i = 0, k = 0; i < MAX_MEN; i++) k += a.men[i].kind == MAN_SOLDIER;
    CHECK(k == 4);

    /* theirs do shoot, and the two sides fight each other */
    memset(a.men, 0, sizeof a.men);
    put_man(&a, 0, TEAM_PLAYER, 60, 60);
    put_man(&a, 1, TEAM_ENEMY, 66, 60);
    for (k = 0; k < 1500 && a.men[0].kind && a.men[1].kind; k++) fire_for(&a, 1, 0);
    CHECK(!a.men[0].kind || !a.men[1].kind);
}

static void test_two_players(void)
{
    int p1, p2, k;
    InputBits idle[MAX_TANKS] = { 0 };
    /* together: player 2 gets a garage of their own even with the same tank */
    game_init(&a, 71, BIOME_CLASSIC);
    p1 = game_add_tank_at_base(&a, TANK_MEDIUM, TEAM_PLAYER);
    p2 = game_add_tank_in_garage(&a, TANK_MEDIUM, TEAM_PLAYER, (TANK_MEDIUM + 1) % 3);
    CHECK(a.tanks[p1].x != a.tanks[p2].x);
    a.tanks[p2].armor = 0;                                   /* and respawns there */
    a.tanks[p2].respawn = 1;
    game_tick(&a, idle);
    CHECK(fx_to_int(a.tanks[p2].x) / 16 == a.bases[TEAM_PLAYER].class_tx[(TANK_MEDIUM + 1) % 3]);

    /* versus: player 2 is the enemy side; no helicopter hunts; bringing our flag home wins for them */
    game_init(&a, 72, BIOME_CLASSIC);
    a.versus = 1;
    p1 = game_add_tank_at_base(&a, TANK_MEDIUM, TEAM_PLAYER);
    p2 = game_add_tank_at_base(&a, TANK_HEAVY, TEAM_ENEMY);
    a.tanks[p1].x = FX(64 * 16); a.tanks[p1].y = FX(64 * 16);  /* out in the open, sitting still */
    for (k = 0; k < 15 * TICK_RATE; k++) game_tick(&a, idle);
    for (k = 0; k < MAX_HELIS; k++) CHECK(!a.helis[k].active);
    a.flags[TEAM_PLAYER].state = FLAG_CARRIED; a.flags[TEAM_PLAYER].carrier = (uint8_t)p2;
    a.tanks[p2].has_flag = 1;
    game_tick(&a, idle);                                      /* p2 is still in its own garage: home */
    CHECK(a.winner == TEAM_ENEMY);

#if MAX_TANKS >= 5
    /* four players and the computer on one map */
    game_init(&a, 73, BIOME_CLASSIC);
    for (k = 0; k < 4; k++)
        CHECK(game_add_tank_in_garage(&a, (TankClass)(k % 3), k & 1 ? TEAM_ENEMY : TEAM_PLAYER, k & 1 ? 255 : k) == k);
    k = game_add_tank_at_base(&a, TANK_MEDIUM, TEAM_ENEMY);
    CHECK(k == 4);
    game_set_ai(&a, k, AI_NORMAL);
    for (k = 0; k < 500; k++) game_tick(&a, idle);
    CHECK(a.num_tanks == 5 && a.tanks[4].active);
#endif
}

static char maptext[MAPFILE_MAX];

static void test_mapfile(void)
{
    static Game b2;
    unsigned n;
    int x, y, same = 0, st_a = 0, st_b = 0, i;
    Biome bio;
    char name[32];
    /* a generated map survives a save and load */
    game_init(&a, 81, BIOME_JUNGLE);
    n = mapfile_save(&a.map, a.bases, BIOME_JUNGLE, "Test map", maptext, sizeof maptext);
    CHECK(n > (unsigned)(a.map.w * a.map.h));
    CHECK(a.map.size == SIZE_LARGE && a.map.w == 128);             /* (a random map: LARGE unless asked) */
    CHECK(!strstr(maptext, "\nsize="));                             /* (LARGE: no size= line, as before) */
    {   /* the sizes: a SMALL and a HUGE map survive a save and load; rock outside a small one */
        int zs[2] = { SIZE_SMALL, SIZE_HUGE }, zi;
        for (zi = 0; zi < 2; zi++) {
            map_set_next_size(zs[zi]);
            game_init(&b2, 83, BIOME_DESERT);
            map_set_next_size(SIZE_LARGE);
            CHECK(b2.map.size == zs[zi] && b2.map.w == map_size_tiles[zs[zi]] && b2.map.h == b2.map.w);
            CHECK(b2.bases[0].x1 < b2.map.w && b2.bases[1].y1 < b2.map.h);   /* (both bases inside it) */
            n = mapfile_save(&b2.map, b2.bases, BIOME_DESERT, "Sized", maptext, sizeof maptext);
            CHECK(n > 0 && strstr(maptext, zi ? "\nsize=huge" : "\nsize=small"));
            CHECK(game_init_map(&c, 83, maptext) == 0 && c.map.size == zs[zi] && c.map.w == b2.map.w);
            if (zs[zi] == SIZE_SMALL) CHECK(c.map.type[100][100] == TILE_ROCK && c.map.type[10][70] == TILE_ROCK);
        }
        n = mapfile_save(&a.map, a.bases, BIOME_JUNGLE, "Test map", maptext, sizeof maptext);   /* (back to the large one) */
    }
    CHECK(game_init_map(&b2, 81, maptext) == 0);
    CHECK(b2.biome == BIOME_JUNGLE);
    CHECK(b2.bases[0].x0 == a.bases[0].x0 && b2.bases[1].y0 == a.bases[1].y0);
    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++) same += a.map.type[y][x] == b2.map.type[y][x];
    CHECK(same > MAP_W * MAP_H * 98 / 100);
    for (i = 0; i < a.map.num_st; i++)        /* standing, and not built over */
        st_a += a.map.st[i].hp > 0 && a.map.sid[a.map.st[i].y][a.map.st[i].x] == i + 1;
    for (i = 0; i < b2.map.num_st; i++)
        st_b += b2.map.st[i].hp > 0 && b2.map.sid[b2.map.st[i].y][b2.map.st[i].x] == i + 1;
    printf("  map file: %d of %d buildings back\n", st_b, st_a);
    CHECK(st_b >= st_a * 9 / 10 && st_b <= st_a + 2);
    CHECK(mapfile_load(&b2.map, b2.bases, &bio, name, sizeof name, maptext, 1) == 0 && !strcmp(name, "Test map"));
    /* and it plays: tanks, the computer, a win */
    game_init_map(&a, 5, maptext);
    x = game_add_tank_at_base(&a, TANK_MEDIUM, TEAM_PLAYER);
    y = game_add_tank_at_base(&a, TANK_MEDIUM, TEAM_ENEMY);
    game_set_ai(&a, y, AI_HARD);
    for (i = 0; i < 25000 && a.winner < 0; i++) { InputBits in[MAX_TANKS] = { 0 }; a.tanks[x].still = 0; game_tick(&a, in); }
    CHECK(a.winner == TEAM_ENEMY);
    /* a gun written right in front of our garages (as an original level may have it) stays out */
    {
        char *q = strstr(maptext, "\nend\n");
        int gx = a.bases[TEAM_PLAYER].x0 + 16, gy = a.bases[TEAM_PLAYER].y0 + 14, k, found = 0;
        if (q) sprintf(q, "\nstruct=cannon enemy %d %d\nend\n", gx, gy);
        CHECK(game_init_map(&b2, 81, maptext) == 0);
        for (k = 0; k < b2.map.num_st; k++)
            found |= b2.map.st[k].type == ST_T_CANNON && b2.map.st[k].x == gx && b2.map.st[k].y == gy;
        CHECK(!found);
    }
    /* garbage is refused */
    CHECK(game_init_map(&b2, 1, "hello") != 0);
}

static void test_ai(void)
{
    int p, e, k, start_armor;
    InputBits idle[MAX_TANKS] = { 0 };
    /* left alone, the computer raids our camp, takes the flag and wins */
    game_init(&a, 1, BIOME_CLASSIC);
    p = game_add_tank_at_base(&a, TANK_MEDIUM, TEAM_PLAYER);
    e = game_add_tank_at_base(&a, TANK_MEDIUM, TEAM_ENEMY);
    game_set_ai(&a, e, AI_NORMAL);
    for (k = 0; k < 25000 && a.winner < 0; k++) { a.tanks[p].still = 0; game_tick(&a, idle); }
    CHECK(a.winner == TEAM_ENEMY);
    CHECK(a.tanks[p].msg == MSG_LOST);
    printf("  computer took our flag home after %d s\n", k / TICK_RATE);

    /* out in the open it turns on you and shoots */
    game_init(&a, 2, BIOME_CLASSIC);
    map_fill(&a.map, TILE_SAND); a.num_props = 0;
    memset(a.mines, 0, sizeof a.mines);
    memset(a.men, 0, sizeof a.men);
    p = game_add_tank(&a, TANK_HEAVY, TEAM_PLAYER, FX(60 * 16), FX(60 * 16));
    e = game_add_tank(&a, TANK_MEDIUM, TEAM_ENEMY, FX(60 * 16), FX(52 * 16));
    a.tanks[e].angle = a.tanks[e].prev_angle = ANGLE_EAST;
    game_set_ai(&a, e, AI_HARD);
    start_armor = a.tanks[p].armor;
    for (k = 0; k < 400; k++) { a.tanks[p].still = 0; game_tick(&a, idle); }
    CHECK(a.tanks[p].armor < start_armor || a.tanks[p].lost > 0);

    /* we grab its flag: it comes after us */
    game_init(&a, 3, BIOME_CLASSIC);
    p = game_add_tank_at_base(&a, TANK_MEDIUM, TEAM_PLAYER);
    e = game_add_tank_at_base(&a, TANK_MEDIUM, TEAM_ENEMY);
    game_set_ai(&a, e, AI_NORMAL);
    a.flags[TEAM_ENEMY].state = FLAG_CARRIED; a.flags[TEAM_ENEMY].carrier = (uint8_t)p; a.tanks[p].has_flag = 1;
    game_tick(&a, idle);
    CHECK(a.ai[e].mode == AIM_CHASE);

    /* badly hurt, it goes home to its garage and gets patched up */
    a.flags[TEAM_ENEMY].state = FLAG_HIDDEN; a.tanks[p].has_flag = 0;
    a.tanks[e].armor = 5;
    for (k = 0; k < 1500; k++) { a.tanks[p].still = 0; game_tick(&a, idle); }
    CHECK(a.tanks[e].armor > 5);

    /* our flag dropped in the field: drive over it and it goes back home */
    game_init(&a, 4, BIOME_CLASSIC);
    p = game_add_tank_at_base(&a, TANK_MEDIUM, TEAM_PLAYER);
    a.flags[TEAM_PLAYER].state = FLAG_GROUND;
    a.flags[TEAM_PLAYER].x = a.tanks[p].x; a.flags[TEAM_PLAYER].y = a.tanks[p].y;
    game_tick(&a, idle);
    CHECK(a.flags[TEAM_PLAYER].x == a.flags[TEAM_PLAYER].home_x && a.tanks[p].msg == MSG_FLAG_BACK);
}

static void test_mines(void)
{
    int t, e;
    game_init(&a, 9, BIOME_CLASSIC);
    map_fill(&a.map, TILE_SAND); a.num_props = 0;
    t = game_add_tank(&a, TANK_HEAVY, TEAM_PLAYER, FX(20 * 16), FX(20 * 16));
    e = game_add_tank(&a, TANK_LIGHT, TEAM_ENEMY, FX(40 * 16), FX(20 * 16));
    fire_for(&a, 1, IN_MINE);
    CHECK(a.tanks[t].mines == tank_stats[TANK_HEAVY].mines - 1);
    fire_for(&a, 10, 0);
    CHECK(a.mines[0].active);                                       /* we're still beside it: no bang */
    a.tanks[e].x = a.mines[0].x;  a.tanks[e].y = a.mines[0].y;     /* an enemy drives onto it */
    fire_for(&a, 1, 0);
    CHECK(!a.mines[0].active && a.tanks[e].armor < tank_stats[TANK_LIGHT].armor);
}

static void test_collision(void)
{
    setup_range(&a, TILE_ROCK);
    fire_for(&a, 300, IN_UP);
    CHECK(fx_to_int(a.tanks[0].x) + 6 <= 12 * 16);  /* stopped at the rock */
    CHECK(a.tanks[0].speed == 0);

    setup_range(&a, TILE_WALL);                      /* blast through, then drive over rubble */
    fire_for(&a, 300, IN_FIRE);
    fire_for(&a, 300, IN_UP);
    CHECK(fx_to_int(a.tanks[0].x) > 13 * 16);
}

static void test_relaxed_pace(void)
{
    int i;
    setup_range(&a, TILE_SAND);
    memset(a.men, 0, sizeof a.men);
    a.men[0] = (Man){ .kind = MAN_SOLDIER, .state = MAN_FLEE, .timer = 1100,
                     .x = FX(320), .y = FX(320), .dir = ANGLE_EAST };
    b = a;
    b.relaxed_pace = 1;
    fire_for(&a, 40, IN_UP);
    fire_for(&b, 40, IN_UP);
    CHECK(a.tanks[0].speed == tank_stats[TANK_MEDIUM].max_fwd);
    CHECK(b.tanks[0].speed == a.tanks[0].speed * 7 / 8);
    CHECK(b.men[0].x - FX(320) == 40 * (FX_FRAC(13, 10) * 3 / 4));
    CHECK(b.men[0].x < a.men[0].x);
    for (i = 0; i < 2; i++) {
        Game *g = i ? &b : &a;
        fire_for(g, 50, IN_DOWN);
        CHECK(g->tanks[0].speed == -tank_stats[TANK_MEDIUM].max_rev * (i ? 7 : 8) / 8);
    }
}

static void test_rocket_tank(void)
{
    const int tx = 12, ty = 10;
    setup_range(&a, TILE_WALL);
    game_set_tank_class(&a, 0, TANK_HEAVY);
    b = a;
    a.tanks[0].rocket_launcher = 1;
    fire_for(&a, 1, IN_FIRE);
    fire_for(&b, 1, IN_FIRE);
    CHECK(a.shots[0].kind == SHOT_ROCKET && a.shots[0].damage > b.shots[0].damage);
    CHECK(a.tanks[0].reload > b.tanks[0].reload);
    fire_for(&a, 12, 0);
    fire_for(&b, 12, 0);
    CHECK(a.map.hp[ty][tx] == tile_info[TILE_WALL].hp - 2);
    CHECK(b.map.hp[ty][tx] == tile_info[TILE_WALL].hp - 1);
}

static void test_match_player_limit(void)
{
    static Game g;
    MatchSetup ms = { 0 };
    ms.seed = 2026;
    ms.ai_level = AI_NORMAL;
    ms.players = MATCH_PLAYERS - 1;
    CHECK(match_start(&g, &ms, NULL) == 0);
    CHECK(g.num_tanks == MATCH_PLAYERS && g.tanks[MATCH_PLAYERS - 1].ai == AI_NORMAL);
    ms.players = MATCH_PLAYERS;
    CHECK(match_start(&g, &ms, NULL) == 0);
    CHECK(g.num_tanks == MATCH_PLAYERS);
#if MAX_TANKS > MATCH_PLAYERS
    CHECK(match_start_recorded(&g, &ms, NULL, MATCH_PLAYERS + 1) == 0);
    CHECK(g.num_tanks == MATCH_PLAYERS + 1 && g.tanks[MATCH_PLAYERS].ai == AI_NORMAL);
#endif
}

/* changing weather: clear to begin with, then a storm rolls in, then it clears again */
static void test_changing_weather(void)
{
    static Game g;
    MatchSetup ms;
    InputBits in[MAX_TANKS] = { 0 };
    uint32_t t;
    int storms = 0, clears = 0, was;
    memset(&ms, 0, sizeof ms);
    ms.seed = 7; ms.players = 1; ms.weather = WEATHER_CHANGING; ms.biome = BIOME_DESERT;
    CHECK(!weather_allowed(BIOME_WINTER, WEATHER_SANDSTORM) && !weather_allowed(BIOME_MOON, WEATHER_SNOW));
    CHECK(!weather_allowed(BIOME_WINTER, WEATHER_RAIN) && !weather_allowed(BIOME_WINTER, WEATHER_STORM));
    CHECK(weather_allowed(BIOME_WINTER, WEATHER_SNOWSTORM) && !weather_allowed(BIOME_DESERT, WEATHER_SNOWSTORM));
    CHECK(weather_from_name(weather_name(WEATHER_SNOWSTORM)) == WEATHER_SNOWSTORM);
    CHECK(weather_preview(BIOME_WINTER, WEATHER_CHANGING, 4000) == WEATHER_SNOWSTORM);
    CHECK(weather_allowed(BIOME_VOLCANIC, WEATHER_ERUPTION) && !weather_allowed(BIOME_DESERT, WEATHER_ERUPTION));
    CHECK(weather_step(BIOME_WINTER, WEATHER_SNOW, 1, 0) == WEATHER_FOG);
    CHECK(weather_step(BIOME_MOON, WEATHER_CLEAR, 1, 0) == WEATHER_METEOR);
    CHECK(weather_step(BIOME_MOON, WEATHER_METEOR, 1, 1) == WEATHER_COUNT);
    CHECK(match_start(&g, &ms, NULL) == 0);
    CHECK(g.weather == WEATHER_CLEAR && g.weather_changing);
    was = g.weather;
    for (t = 0; t < 7u * 60u * TICK_RATE; t++) {
        game_tick(&g, in);
        if (g.weather != was) { if (g.weather == WEATHER_STORM) storms++; else clears++; was = g.weather; }
    }
    printf("changing weather, 7 minutes: %d storm(s) came, %d cleared\n", storms, clears);
    CHECK(storms >= 1 && clears >= 1);
}

/* patrols: some of the enemy's jeeps and trucks drive the roads, and keep to them */
static void test_patrols(void)
{
    static Game g;
    InputBits in[MAX_TANKS] = { 0 };
    fx x0[MAX_PROPS], y0[MAX_PROPS];
    int i, t, moving = 0, moved = 0, on_road = 0;
    game_init(&g, 51, BIOME_CLASSIC);
    for (i = 0; i < g.num_props; i++) { x0[i] = g.props[i].x; y0[i] = g.props[i].y; moving += g.props[i].moving; }
    for (t = 0; t < 30 * TICK_RATE; t++) game_tick(&g, in);
    for (i = 0; i < g.num_props; i++) {
        int tx = fx_to_int(g.props[i].x) >> TILE_SHIFT, ty = fx_to_int(g.props[i].y) >> TILE_SHIFT;
        if (!g.props[i].moving) continue;
        moved += g.props[i].x != x0[i] || g.props[i].y != y0[i];
        on_road += g.map.type[ty][tx] == TILE_ROAD;
    }
    printf("patrols: %d driving, %d moved in 30 s, %d on the road\n", moving, moved, on_road);
    CHECK(moving >= 1 && moved >= 1 && on_road == moving);
}

int main(void)
{
    test_trig();
    test_determinism();
    test_biomes();
    test_destruction();
    test_collision();
    test_relaxed_pace();
    test_rocket_tank();
    test_structures();
    test_atan2();
    test_garage_start();
    test_turrets();
    test_pows();
    test_helicopters_flag_fuel();
    test_mines();
    test_flee_and_helipads();
    test_turret_types();
    test_tank_steering();
    test_direct_steering();
    test_grass_own_buildings_fire_on_move();
    test_vehicles();
    test_ai();
    test_soldier_sides();
    test_no_strangers_at_home();
    test_two_players();
    test_match_player_limit();
    test_mapfile();
    test_changing_weather();
    test_patrols();
    printf(failures ? "%d test(s) FAILED\n" : "all core tests passed\n", failures);
    return failures ? 1 : 0;
}
