/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/replay.h"
#include "common/net.h"

static Game game, played;
static int failures;
#define CHECK(c) do { if (!(c)) { if (failures < 12) fprintf(stderr, "%d: %s\n", __LINE__, #c); failures++; } } while (0)

static MatchSetup setup(int players, unsigned teams, int mode)
{
    MatchSetup ms = {0};
    int p;
    ms.seed = 7919; ms.biome = BIOME_MOON; ms.camp_garages = 4;
    ms.players = (uint8_t)players; ms.versus = 1; ms.extras = (uint8_t)mode;
    ms.bot_commands = 1;
    for (p = 0; p < players; p++) {
        ms.team[p] = (teams >> p) & 1;
        ms.cls[p] = TANK_MEDIUM;
    }
    return ms;
}

static void check_positions(int players, int mode)
{
    int p, q, rank[2] = {0, 0};
    CHECK(game.num_tanks == players && game.map.arena && game.map.num_spawns == 8);
    for (p = 0; p < players; p++) {
        const Tank *t = &game.tanks[p];
        int k, chosen = -1, nth = 0;
        for (q = 0; q < p; q++)
            CHECK(t->x != game.tanks[q].x || t->y != game.tanks[q].y);
        for (k = 0; k < game.map.num_spawns; k++) {
            int own = ((game.map.spawn_tx[k] + game.map.spawn_ty[k]) < game.map.w) == (t->team == TEAM_PLAYER);
            if (!(mode & EXTRA_ANNIHILATION) && !own) continue;
            if (nth++ == ((mode & EXTRA_ANNIHILATION) ? p : rank[t->team])) { chosen = k; break; }
        }
        CHECK(chosen >= 0);
        if (chosen >= 0) {
            CHECK(t->x == FX(game.map.spawn_tx[chosen] * TILE_SIZE + TILE_SIZE / 2));
            CHECK(t->y == FX(game.map.spawn_ty[chosen] * TILE_SIZE + TILE_SIZE / 2));
            CHECK(t->prev_x == t->x && t->prev_y == t->y);
        }
        rank[t->team]++;
    }
}

static void orders(const char *map)
{
    static const int modes[] = { EXTRA_TDM, EXTRA_WARLORD, EXTRA_KILLCONF, EXTRA_ANNIHILATION };
    int players, m;
    for (players = 2; players <= 8; players += 2) {
        unsigned teams;
        for (teams = 0; teams < (1u << players); teams++) {
            unsigned bits = teams; int count = 0;
            for (; bits; bits >>= 1) count += bits & 1;
            if (count != players / 2) continue;
            for (m = 0; m < (int)(sizeof modes / sizeof *modes); m++) {
                MatchSetup ms = setup(players, teams, modes[m]);
                CHECK(match_start(&game, &ms, map) == 0);
                check_positions(players, modes[m]);
            }
        }
    }
}

static void records(const char *map)
{
    MatchSetup ms = setup(8, 0xf0u, EXTRA_TDM), copy;
    int legacy, i;
    uint8_t bytes[512];
    Pack packet;
    copy = ms; copy.legacy_arena_spawns = 1;
    CHECK(match_hash(&copy, map) != match_hash(&ms, map));
    pk_init(&packet, bytes, sizeof bytes); pk_setup(&packet, &ms);
    i = packet.n; pk_init(&packet, bytes, i); uk_setup(&packet, &copy);
    CHECK(!packet.bad && !copy.legacy_arena_spawns);
    copy.extras = ms.extras; /* MATCH carries extras separately from pk_setup. */
    CHECK(match_hash(&copy, map) == match_hash(&ms, map));
    for (legacy = 0; legacy <= 1; legacy++) {
        Replay recorded, loaded;
        const char *path = legacy ? "arena-spawns-legacy.ofprep" : "arena-spawns-new.ofprep";
        ms.legacy_arena_spawns = (uint8_t)legacy;
        CHECK(match_start(&game, &ms, map) == 0);
        if (!legacy) check_positions(8, EXTRA_TDM);
        else {
            for (i = 0; i < 8; i += 2) {
                CHECK(game.tanks[i].x == game.tanks[i + 1].x);
                CHECK(game.tanks[i].y == game.tanks[i + 1].y);
            }
        }
        replay_begin(&recorded, &ms, map, game.num_tanks);
        for (i = 0; i < 500; i++) {
            InputBits in[MAX_TANKS] = { IN_UP | IN_FIRE, IN_RIGHT, 0 };
            game_tick(&game, in); replay_tick(&recorded, in, &game);
        }
        CHECK(replay_save(&recorded, path) == 0);
        CHECK(replay_load(&loaded, path) == 0);
        CHECK(loaded.setup.legacy_arena_spawns == legacy);
        CHECK(match_hash(&loaded.setup, loaded.map) == match_hash(&ms, map));
        CHECK(replay_verify(&loaded, &played) == -1);
        CHECK(game_checksum(&played) == game_checksum(&game));
        replay_free(&loaded); replay_free(&recorded); remove(path);
    }
}

int main(int argc, char **argv)
{
    char *map = NULL;
    FILE *f;
    long n;
    if (argc != 2 || !(f = fopen(argv[1], "rb"))) return 2;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET)) return 2;
    map = malloc((size_t)n + 1);
    if (!map || fread(map, 1, (size_t)n, f) != (size_t)n) return 2;
    fclose(f); map[n] = 0;
    orders(NULL);
    /* The authored arena supplies its team-deathmatch rules. */
    orders(map);
    records(NULL); records(map);
    free(map);
    if (failures) fprintf(stderr, "%d arena spawn checks failed\n", failures);
    return failures ? 1 : 0;
}
