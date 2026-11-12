/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include "common/replay.h"
#include "common/net.h"
#include "server/config.h"

static Game game, played;
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d: %s\n", __LINE__, #c); failures++; } } while (0)

static MatchSetup setup(int world, int lives)
{
    MatchSetup ms = {0};
    ms.seed = 73; ms.biome = (uint8_t)world; ms.camp_garages = 4;
    ms.versus = 1; ms.players = 2; ms.lives = (uint8_t)lives;
    ms.cls[0] = ms.cls[1] = TANK_MEDIUM; ms.team[1] = TEAM_ENEMY;
    return ms;
}

static void empty_field(void)
{
    map_fill(&game.map, TILE_SAND);
    game.num_props = 0;
    memset(game.men, 0, sizeof game.men);
    memset(game.helis, 0, sizeof game.helis);
    memset(game.mines, 0, sizeof game.mines);
    memset(game.pickups, 0, sizeof game.pickups);
    game.tanks[0].x = game.tanks[0].prev_x = FX(40 * 16 + 8);
    game.tanks[0].y = game.tanks[0].prev_y = FX(40 * 16 + 8);
}

static void run_dry(void)
{
    InputBits in[MAX_TANKS] = { IN_UP | IN_LEFT };
    int i;
    game.tanks[0].respawn = 0; game.tanks[0].fuel = 3;
    for (i = 0; i < 5; i++) game_tick(&game, in);
}

static void limits(void)
{
    MatchSetup ms = setup(BIOME_CLASSIC, 2);
    CHECK(match_start(&game, &ms, NULL) == 0);
    CHECK(game.time_limit == MATCH_DEFAULT_SECONDS * TICK_RATE && game.tanks[0].lives == 2);
    empty_field(); run_dry(); CHECK(game.tanks[0].lives == 1 && !game.tanks[0].out);
    run_dry(); CHECK(!game.tanks[0].lives && game.tanks[0].out);
    ms.lives = MATCH_UNLIMITED_LIVES; ms.time_limit = 65535;
    CHECK(match_start(&game, &ms, NULL) == 0);
    CHECK(game.time_limit == MATCH_MAX_SECONDS * TICK_RATE);
    empty_field(); run_dry(); run_dry();
    CHECK(game.tanks[0].lives == MATCH_UNLIMITED_LIVES && !game.tanks[0].out);
    ms.time_limit = 60; ms.lives = 0;
    CHECK(match_start(&game, &ms, NULL) == 0);
    CHECK(game.time_limit == 60 * TICK_RATE && game.tanks[0].lives == 5);
    game.tick = game.time_limit - 1;
    { InputBits in[MAX_TANKS] = {0}; game_tick(&game, in); }
    CHECK(game.time_up); /* equal points may legitimately finish as a draw */
    ms.extras = EXTRA_TDM; ms.lives = 1;
    CHECK(match_start(&game, &ms, NULL) == 0);
    empty_field(); run_dry(); CHECK(game.tanks[0].out);
}

static void worlds(void)
{
    MatchSetup ms = setup(BIOME_VOLCANIC, 5);
    InputBits in[MAX_TANKS] = {0};
    CHECK(match_start(&game, &ms, NULL) == 0);
    CHECK(!strcmp(world_tank_stats(game.vehicle_world, TANK_MEDIUM)->name, "BASILISK"));
    CHECK(game.tanks[0].armor > tank_stats[TANK_MEDIUM].armor);
    empty_field(); game.extras = EXTRA_PICKUPS; game.tanks[0].mines = 0;
    game.pickups[0].active = 1; game.pickups[0].kind = PICKUP_MINES;
    game.pickups[0].tx = game.pickups[0].ty = 40;
    game_tick(&game, in); CHECK(game.tanks[0].mines == 5);
    ms.biome = BIOME_MOON;
    CHECK(match_start(&game, &ms, NULL) == 0);
    CHECK(game.tanks[0].armor < tank_stats[TANK_MEDIUM].armor);
    empty_field(); game.extras = EXTRA_PICKUPS; game.tanks[0].armor = 20;
    game.pickups[0].active = 1; game.pickups[0].kind = PICKUP_FUEL;
    game.pickups[0].tx = game.pickups[0].ty = 40;
    game_tick(&game, in); CHECK(game.tanks[0].armor == 30);
    in[0] = IN_FIRE; game_tick(&game, in);
    CHECK(game.shots[0].ttl > 45);
    ms.camp_garages = 3;
    CHECK(match_start(&game, &ms, NULL) == 0);
    CHECK(game.vehicle_world == BIOME_CLASSIC && game.tanks[0].armor == tank_stats[TANK_MEDIUM].armor);
}

static void records(void)
{
    MatchSetup ms = setup(BIOME_MOON, MATCH_UNLIMITED_LIVES), copy;
    Replay recorded, loaded;
    uint8_t bytes[512]; Pack packet; int length, i;
    pk_init(&packet, bytes, sizeof bytes); pk_setup(&packet, &ms); length = packet.n;
    pk_init(&packet, bytes, length); uk_setup(&packet, &copy);
    CHECK(!packet.bad && copy.lives == MATCH_UNLIMITED_LIVES && copy.biome == BIOME_MOON);
    CHECK(match_hash(&copy, NULL) == match_hash(&ms, NULL));
    copy.lives = 5; CHECK(match_hash(&copy, NULL) != match_hash(&ms, NULL));
    CHECK(match_start(&game, &ms, NULL) == 0);
    replay_begin(&recorded, &ms, NULL, game.num_tanks);
    for (i = 0; i < 500; i++) {
        InputBits in[MAX_TANKS] = { IN_UP | IN_FIRE, 0 };
        game_tick(&game, in); replay_tick(&recorded, in, &game);
    }
    CHECK(replay_save(&recorded, "world-rules.ofprep") == 0);
    CHECK(replay_load(&loaded, "world-rules.ofprep") == 0);
    CHECK(!loaded.setup.legacy_rules && loaded.setup.lives == MATCH_UNLIMITED_LIVES);
    CHECK(replay_verify(&loaded, &played) == -1 && game_checksum(&played) == game_checksum(&game));
    replay_free(&loaded); replay_free(&recorded);
    {
        FILE *f = fopen("legacy-rules.ofprep", "w");
        CHECK(f != NULL);
        if (f) {
            fputs("OFPREPLAY 1\nseed=73 biome=1 time=0 garages=4 versus=1 ai=0 players=1 tanks=1\nplayer=0 cls=1 team=0 steer=0\nticks\n1 00\nend\n", f);
            fclose(f);
            CHECK(replay_load(&loaded, "legacy-rules.ofprep") == 0);
            CHECK(loaded.setup.legacy_rules == 1);
            CHECK(match_start_recorded(&played, &loaded.setup, NULL, 1) == 0);
            CHECK(!played.life_limit && !played.time_limit && played.vehicle_world == BIOME_CLASSIC);
            replay_free(&loaded);
        }
    }
    remove("legacy-rules.ofprep"); /* keep world-rules.ofprep for the WebAssembly cross-check */
}

static int count_tile(const Map *m, int tile)
{
    int x,y,n=0; for(y=0;y<m->h;y++)for(x=0;x<m->w;x++)n+=m->type[y][x]==tile; return n;
}

static fx brake_distance(int tile, int world)
{
    MatchSetup ms=setup(world,5); InputBits in[MAX_TANKS]={IN_UP}; int i;
    fx start;
    ms.no_powerups=1;
    CHECK(match_start(&game,&ms,NULL)==0); empty_field(); map_fill(&game.map,(TileType)tile);
    for(i=0;i<120;i++)game_tick(&game,in);
    start=game.tanks[0].y; in[0]=0;
    for(i=0;i<40;i++)game_tick(&game,in);
    CHECK(game.tanks[0].active && !game.tanks[0].respawn);
    return start-game.tanks[0].y;
}

static void surfaces(void)
{
    MatchSetup ms=setup(BIOME_MOON,5); int i;
    InputBits in[MAX_TANKS]={0};
    CHECK(match_start(&game,&ms,NULL)==0);
    CHECK(!count_tile(&game.map,TILE_WATER) && !count_tile(&game.map,TILE_ICE));
    CHECK(count_tile(&game.map,TILE_CRATER)>0);
    ms.biome=BIOME_WINTER; CHECK(match_start(&game,&ms,NULL)==0);
    CHECK(count_tile(&game.map,TILE_ICE)>0 && count_tile(&game.map,TILE_WATER)>0);
    ms.biome=BIOME_DESERT; CHECK(match_start(&game,&ms,NULL)==0);
    CHECK(count_tile(&game.map,TILE_OIL)>0);
    CHECK(!map_surface_allowed(BIOME_MOON,0,TILE_WATER));
    CHECK(map_surface_allowed(BIOME_WINTER,0,TILE_ICE));
    CHECK(!map_surface_allowed(BIOME_DESERT,0,TILE_ICE));
    CHECK(!map_surface_allowed(BIOME_WINTER,0,TILE_OIL));
    CHECK(!map_surface_allowed(BIOME_CLASSIC,1,TILE_OIL));
    CHECK(brake_distance(TILE_ICE,BIOME_WINTER)>brake_distance(TILE_ROAD,BIOME_WINTER)*3);
    CHECK(brake_distance(TILE_OIL,BIOME_DESERT)>brake_distance(TILE_ROAD,BIOME_DESERT)*3);
    CHECK(match_start(&game,&ms,NULL)==0); empty_field(); map_fill(&game.map,TILE_ICE);
    game.tanks[0].angle=0; in[0]=IN_UP;
    for(i=0;i<120;i++)game_tick(&game,in);
    in[0]=IN_UP|IN_RIGHT;
    for(i=0;i<15;i++)game_tick(&game,in);
    CHECK(game.tanks[0].drift_x < fx_mul(game.tanks[0].speed,fx_sin(game.tanks[0].angle)));
    /* Sliding still respects solid boundaries. */
    map_set(&game.map,40,40,TILE_ROCK); game.tanks[0].y=FX(41*16+8); game.tanks[0].x=FX(40*16+8);
    game.tanks[0].angle=0; game.tanks[0].drift_x=0; game.tanks[0].drift_y=-FX(2);
    in[0]=IN_UP;for(i=0;i<50;i++)game_tick(&game,in);
    CHECK(game.tanks[0].y>=FX(41*16)+TANK_HALF);
    /* All three new tile types survive map editing/save/load. */
    { static char map[MAPFILE_MAX]; Base bases[2];
      map_set(&game.map,50,50,TILE_ICE);map_set(&game.map,51,50,TILE_OIL);map_set(&game.map,52,50,TILE_CRATER);
      CHECK(mapfile_save(&game.map,game.bases,BIOME_WINTER,"Surfaces",map,sizeof map)>0);
      CHECK(mapfile_load(&played.map,bases,NULL,NULL,0,map,1)==0);
      CHECK(played.map.type[50][50]==TILE_ICE && played.map.type[50][51]==TILE_OIL && played.map.type[50][52]==TILE_CRATER);
    }
    ms.biome=BIOME_CLASSIC;ms.extras=EXTRA_PICKUPS;
    CHECK(match_start(&game,&ms,NULL)==0);
    CHECK(!game.enhanced_rules && game.no_powerups && !(game.extras&EXTRA_PICKUPS));
    CHECK(!count_tile(&game.map,TILE_ICE)&&!count_tile(&game.map,TILE_OIL));
}

static void powerups(void)
{
    MatchSetup ms=setup(BIOME_DESERT,5); InputBits in[MAX_TANKS]={0}; int i,kind[5]={0};
    CHECK(match_start(&game,&ms,NULL)==0);
    for(i=0;i<MAX_PICKUPS;i++)if(game.pickups[i].active)kind[game.pickups[i].kind]++;
    CHECK((game.extras&EXTRA_PICKUPS) && kind[PICKUP_BOOST] && kind[PICKUP_RAPID]);
    empty_field();game.pickups[0].active=1;game.pickups[0].kind=PICKUP_BOOST;game.pickups[0].tx=game.pickups[0].ty=40;
    game_tick(&game,in);CHECK(game.tanks[0].boost_ticks==POWERUP_TICKS);
    game.pickups[1]=game.pickups[0];game.pickups[1].active=1;game.pickups[1].kind=PICKUP_RAPID;
    game_tick(&game,in);CHECK(game.tanks[0].rapid_ticks==POWERUP_TICKS);
    in[0]=IN_UP|IN_FIRE;for(i=0;i<60;i++)game_tick(&game,in);
    CHECK(game.tanks[0].speed>world_tank_stats(game.vehicle_world,TANK_MEDIUM)->max_fwd);
    game.tanks[0].reload=0;game_tick(&game,in);
    CHECK(game.tanks[0].reload<world_tank_stats(game.vehicle_world,TANK_MEDIUM)->reload);
    in[0]=0;for(i=0;i<POWERUP_TICKS;i++)game_tick(&game,in);
    CHECK(!game.tanks[0].boost_ticks && !game.tanks[0].rapid_ticks);
    ms.no_powerups=1;ms.extras=EXTRA_PICKUPS|EXTRA_TDM;
    CHECK(match_start(&game,&ms,NULL)==0 && !(game.extras&EXTRA_PICKUPS));
    for(i=0;i<MAX_PICKUPS;i++)CHECK(!game.pickups[i].active);
    game.drop_wait=65534;game_tick(&game,in);CHECK(!game.drop.active);
    { uint8_t bytes[256];Pack p;MatchSetup copy;
      pk_init(&p,bytes,sizeof bytes);pk_setup(&p,&ms);i=p.n;pk_init(&p,bytes,i);uk_setup(&p,&copy);copy.extras=ms.extras;
      CHECK(!p.bad && copy.no_powerups && match_hash(&ms,NULL)==match_hash(&copy,NULL));
    }
}

static void server_config(void)
{
    ServerSettings settings; ServerConfig config;
    FILE *f = fopen("world-rules.cfg", "w");
    CHECK(f != NULL);
    if (!f) return;
    fputs("time_limit = 999\nlives = unlimited\npickups = no\n", f); fclose(f);
    config_defaults(&settings); CHECK(config_load(&settings, "world-rules.cfg") == 0);
    config_to_server(&settings, &config);
    CHECK(config.base.time_limit == MATCH_MAX_SECONDS && config.base.lives == MATCH_UNLIMITED_LIVES);
    CHECK(config.base.no_powerups);
    f = fopen("world-rules.cfg", "w");
    if (f) { fputs("time_limit = 0\nlives = 8\npickups = yes\n", f); fclose(f); }
    config_defaults(&settings); CHECK(config_load(&settings, "world-rules.cfg") == 0);
    CHECK(settings.time_limit_min == 20 && settings.lives == 8);
    config_to_server(&settings,&config);CHECK(!config.base.no_powerups);
    remove("world-rules.cfg");
}

int main(void)
{
    limits(); worlds(); records(); surfaces(); powerups(); server_config();
    if (!failures) puts("World vehicles, pickups, life limits, time limits, network setup and old/new replays passed.");
    return failures != 0;
}
