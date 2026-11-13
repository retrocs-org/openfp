/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/soundtrack.h"
#include "common/replay.h"
static Game original, loaded;
static char map[400000], modified[400100];
static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); failures++; } } while (0)
static void roundtrip(int world, int choice)
{
    Biome biome;
    MatchSetup ms = {0};
    Replay record, replay;
    const char *path = "soundtrack-test.ofprep";
    int i;
    game_init(&original, 77, (Biome)world);
    original.map.music = (uint8_t)choice;
    CHECK(mapfile_save(&original.map, original.bases, (Biome)world, "Music test", map, sizeof map) > 0);
    CHECK(!!strstr(map, "\nmusic=") == !!choice);
    CHECK(mapfile_load(&loaded.map, loaded.bases, &biome, NULL, 0, map, 77) == 0);
    CHECK((int)biome == world && loaded.map.music == choice);
    ms.seed = 77; ms.biome = (uint8_t)world; ms.players = 2; ms.versus = 1; ms.team[1] = 1;
    CHECK(match_start(&original, &ms, map) == 0);
    CHECK(original.map.music == choice);
    replay_begin(&record, &ms, map, 2);
    for (i = 0; i < 250; i++) {
        InputBits in[MAX_TANKS] = { IN_UP | IN_FIRE, IN_RIGHT };
        game_tick(&original, in); replay_tick(&record, in, &original);
    }
    CHECK(replay_save(&record, path) == 0);
    CHECK(replay_load(&replay, path) == 0);
    CHECK(replay_verify(&replay, &loaded) == -1);
    CHECK(loaded.map.music == choice);
    replay_free(&record); replay_free(&replay); remove(path);
}
static void fallback(const char *key)
{
    Biome biome;
    game_init(&original, 77, BIOME_WINTER);
    CHECK(mapfile_save(&original.map, original.bases, BIOME_WINTER, "Fallback", map, sizeof map) > 0);
    snprintf(modified, sizeof modified, "OFPMAP 1\nmusic=%s\n%s", key, strchr(map, '\n') + 1);
    CHECK(mapfile_load(&loaded.map, loaded.bases, &biome, NULL, 0, modified, 77) == 0);
    CHECK(biome == BIOME_WINTER && loaded.map.music == 0);
}
int main(void)
{
    int world, choice;
    for (world = 0; world < BIOME_COUNT; world++) {
        static const int counts[BIOME_COUNT] = {1, 3, 3, 2, 1, 2, 3, 2, 1};
        CHECK(soundtrack_count(world) == counts[world]);
        for (choice = 0; choice < soundtrack_count(world); choice++) {
            CHECK(soundtrack_choice(world, soundtrack_key(world, choice)) == choice);
            CHECK(soundtrack_title(soundtrack_for_map(world, choice))[0]);
            CHECK(soundtrack_step(world, choice, 1) < soundtrack_count(world));
            CHECK(soundtrack_step(world, choice, -1) >= 0);
            if (world != BIOME_SCIFI) roundtrip(world, choice);
        }
        CHECK(soundtrack_for_map(world, 255) == world);
    }
    CHECK(soundtrack_for_map(BIOME_WINTER, 1) == SOUNDTRACK_SNOW_THEME);
    CHECK(soundtrack_for_map(BIOME_WASTELAND, 1) == SOUNDTRACK_CANNON_TUBE);
    CHECK(soundtrack_for_map(BIOME_MOON, 1) == SOUNDTRACK_SPACE_ATMOSPHERE);
    CHECK(soundtrack_for_map(BIOME_WASTELAND, 2) == SOUNDTRACK_NEW_BEGINNING);
    CHECK(soundtrack_for_map(BIOME_DESERT, 1) == SOUNDTRACK_LOST_DESERT);
    CHECK(soundtrack_for_map(BIOME_DESERT, 2) == SOUNDTRACK_DESERT_DREAMS);
    CHECK(soundtrack_for_map(BIOME_JUNGLE, 1) == SOUNDTRACK_URBAN_JUNGLE);
    CHECK(soundtrack_for_map(BIOME_JUNGLE, 2) == SOUNDTRACK_JUNGLE_PRO_SENSORY);
    CHECK(soundtrack_for_map(BIOME_VOLCANIC, 1) == SOUNDTRACK_IRON_WASTELAND);
    fallback("cannon-tube"); fallback("unknown-future-track");
    return failures ? 1 : 0;
}
