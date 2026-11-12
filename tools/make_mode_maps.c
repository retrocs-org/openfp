/* SPDX-License-Identifier: MIT
 * The bundled maps made for a mode (data/maps/):
 *   annihilation:     arenas: no camps at all, eight spawn points (every tank for itself)
 *   team deathmatch:  arenas too, each side's four spawn points in its own half
 *   conquest:         camps, and three capture points written in
 *   capture the flag: long maps, the camps in far corners
 * Built with the game's own generator, then checked by loading them back. Build and run
 * from the source tree:
 *   cc -std=c99 -Isrc tools/make_mode_maps.c build/libofp_core.a -lm -o /tmp/mm && /tmp/mm data/maps */
#include <stdio.h>
#include <string.h>
#include "core/game.h"
#include "core/world.h"

static Game g, back;
static char text[MAPFILE_MAX + 512], out[MAPFILE_MAX + 512];

/* a line after the name= line: modes= (the menu's, not the game's) */
static void add_line(char *t, const char *line)
{
    char *nl = strchr(t, '\n');                          /* (after "OFPMAP 1") */
    if (nl) nl = strchr(nl + 1, '\n');                   /* (after name=...) */
    if (!nl) return;
    memmove(nl + 1 + strlen(line), nl + 1, strlen(nl + 1) + 1);
    memcpy(nl + 1, line, strlen(line));
}

static int write_map(const char *dir, const char *file, const char *t)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, file);
    if (!(f = fopen(path, "w"))) return -1;
    fputs(t, f);
    fclose(f);
    return 0;
}

/* a map with camps, the camps where asked (loaded back: they're built there, whatever's in the way goes) */
static int map_players;                              /* (the next map: made for so many; 0: as many as fit) */

static int camps_map(const char *dir, const char *file, const char *name, int biome, int tod, unsigned seed,
                     int px, int py, int ex, int ey, const char *modes, int conquest)
{
    char *bp, line[64];
    game_init(&g, seed, (Biome)biome);
    g.map.time = (uint8_t)tod;
    if (!mapfile_save(&g.map, g.bases, (Biome)biome, name, text, sizeof text)) return -1;
    if (px >= 0) {
        if ((bp = strstr(text, "base=player "))) { char *e = strchr(bp, '\n'); snprintf(line, sizeof line, "base=player %d %d", px, py);
            memmove(bp + strlen(line), e, strlen(e) + 1); memcpy(bp, line, strlen(line)); }
        if ((bp = strstr(text, "base=enemy "))) { char *e = strchr(bp, '\n'); snprintf(line, sizeof line, "base=enemy %d %d", ex, ey);
            memmove(bp + strlen(line), e, strlen(e) + 1); memcpy(bp, line, strlen(line)); }
    }
    if (game_init_map(&back, 1, text) != 0) { fprintf(stderr, "%s: doesn't load\n", file); return -1; }
    if (conquest) {                                      /* conquest: the game's own points, written in */
        int k;
        game_setup_extras(&back, EXTRA_CONQUEST);
        back.map.num_points = back.num_cp;
        for (k = 0; k < back.num_cp; k++) { back.map.point_tx[k] = back.cp_tx[k]; back.map.point_ty[k] = back.cp_ty[k]; }
    }
    back.enhanced_rules = biome != BIOME_CLASSIC;
    game_prepare_surfaces(&back, seed, 1);
    back.map.players = (uint8_t)map_players;
    if (!mapfile_save(&back.map, back.bases, (Biome)biome, name, out, sizeof out)) return -1;
    snprintf(line, sizeof line, "modes=%s\n", modes);
    add_line(out, line);
    if (write_map(dir, file, out) != 0) return -1;
    printf("%-32s %-14s %s, camps (%d,%d) and (%d,%d)%s\n", file, name, modes, back.bases[0].x0, back.bases[0].y0,
           back.bases[1].x0, back.bases[1].y0, conquest ? ", three points" : "");
    return 0;
}

/* an arena: the battlefield, its camps cleared, eight spawn points (four each half) */
static int arena_map(const char *dir, const char *file, const char *name, int biome, int tod, unsigned seed, const char *modes)
{
    char line[64];
    game_init(&g, seed, (Biome)biome);
    game_make_arena(&g);
    g.map.time = (uint8_t)tod;
    g.enhanced_rules = biome != BIOME_CLASSIC;
    game_prepare_surfaces(&g, seed, 1);
    g.map.players = (uint8_t)map_players;
    if (!mapfile_save(&g.map, g.bases, (Biome)biome, name, out, sizeof out)) return -1;
    snprintf(line, sizeof line, "modes=%s\n", modes);
    add_line(out, line);
    if (game_init_map(&back, 1, out) != 0 || !back.map.arena || back.map.num_spawns != 8) { fprintf(stderr, "%s: no arena\n", file); return -1; }
    if (write_map(dir, file, out) != 0) return -1;
    printf("%-32s %-14s %s, an arena, 8 spawn points\n", file, name, modes);
    return 0;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "data/maps";
    static const struct { const char *file, *name; int biome, tod; unsigned seed; } ffa[8] = {
        { "arena-scrapyard.ofpmap",    "Scrapyard",    BIOME_CLASSIC, TIME_DUSK,  3111 },
        { "arena-salt-pan.ofpmap",     "Salt Pan",     BIOME_DESERT,  TIME_DAY,   3222 },
        { "arena-ice-floe.ofpmap",     "Ice Floe",     BIOME_WINTER,  TIME_NIGHT, 3333 },
        { "arena-jungle-pit.ofpmap",   "Jungle Pit",   BIOME_JUNGLE,  TIME_DAY,   3444 },
        { "arena-rust-belt.ofpmap",    "Rust Belt",    BIOME_CLASSIC, TIME_NIGHT, 3555 },
        { "arena-crater-field.ofpmap", "Crater Field", BIOME_DESERT,  TIME_DUSK,  3666 },
        { "arena-glacier-bowl.ofpmap", "Glacier Bowl", BIOME_WINTER,  TIME_DAY,   3777 },
        { "arena-neon-pit.ofpmap",     "Neon Pit",     BIOME_SCIFI,   TIME_NIGHT, 3888 } };
    static const struct { const char *file, *name; int biome, tod; unsigned seed; } tdm[6] = {
        { "dm-close-quarters.ofpmap",  "Close Quarters", BIOME_CLASSIC, TIME_DAY,   2111 },
        { "dm-sandtrap.ofpmap",        "Sandtrap",       BIOME_DESERT,  TIME_DUSK,  2222 },
        { "dm-reactor-core.ofpmap",    "Reactor Core",   BIOME_SCIFI,   TIME_NIGHT, 2333 },
        { "dm-mangrove.ofpmap",        "Mangrove",       BIOME_JUNGLE,  TIME_DUSK,  2444 },
        { "dm-snowblind.ofpmap",       "Snowblind",      BIOME_WINTER,  TIME_NIGHT, 2555 },
        { "dm-foundry.ofpmap",         "Foundry",        BIOME_CLASSIC, TIME_DUSK,  2666 } };
    static const struct { const char *file, *name; int biome, tod; unsigned seed; } cq[3] = {
        { "conquest-highlands.ofpmap",     "Highlands",     BIOME_CLASSIC, TIME_DAY,   4111 },
        { "conquest-data-nodes.ofpmap",    "Data Nodes",    BIOME_SCIFI,   TIME_NIGHT, 4222 },
        { "conquest-monsoon-front.ofpmap", "Monsoon Front", BIOME_JUNGLE,  TIME_DUSK,  4333 } };
    static const struct { const char *file, *name; int biome, tod; unsigned seed; int px, py, ex, ey; } ctf[3] = {
        { "ctf-long-haul.ofpmap",   "Long Haul",   BIOME_DESERT, TIME_DAY,   5111, 8, 104, 100, 6 },
        { "ctf-border-run.ofpmap",  "Border Run",  BIOME_WINTER, TIME_DUSK,  5222, 6, 100, 102, 8 },
        { "ctf-deep-jungle.ofpmap", "Deep Jungle", BIOME_JUNGLE, TIME_NIGHT, 5333, 10, 102, 98, 10 } };
    /* the newer worlds: one map of each kind apiece */
    static const struct { const char *file, *name; int biome, tod; unsigned seed; const char *modes; int kind; } worlds[16] = {
        { "arena-caldera.ofpmap",       "Caldera",       BIOME_VOLCANIC,  TIME_NIGHT, 6101, "annihilation", 0 },
        { "dm-magma-run.ofpmap",        "Magma Run",     BIOME_VOLCANIC,  TIME_DUSK,  6102, "deathmatch,warlord", 0 },
        { "conquest-ashfall.ofpmap",    "Ashfall",       BIOME_VOLCANIC,  TIME_DAY,   6103, "conquest", 1 },
        { "ctf-obsidian-pass.ofpmap",   "Obsidian Pass", BIOME_VOLCANIC,  TIME_DUSK,  6104, "flag", 2 },
        { "arena-junkyard.ofpmap",      "Junkyard",      BIOME_WASTELAND, TIME_DAY,   6201, "annihilation", 0 },
        { "dm-dead-mall.ofpmap",        "Dead Mall",     BIOME_WASTELAND, TIME_NIGHT, 6202, "deathmatch,warlord", 0 },
        { "conquest-fallout.ofpmap",    "Fallout",       BIOME_WASTELAND, TIME_DUSK,  6203, "conquest", 1 },
        { "ctf-rust-road.ofpmap",       "Rust Road",     BIOME_WASTELAND, TIME_DAY,   6204, "flag", 2 },
        { "arena-crater-rim.ofpmap",    "Crater Rim",    BIOME_MOON,      TIME_NIGHT, 6301, "annihilation", 0 },
        { "dm-dome-six.ofpmap",         "Dome Six",      BIOME_MOON,      TIME_DAY,   6302, "deathmatch,warlord", 0 },
        { "conquest-tranquility.ofpmap","Tranquility",   BIOME_MOON,      TIME_NIGHT, 6303, "conquest", 1 },
        { "ctf-far-side.ofpmap",        "Far Side",      BIOME_MOON,      TIME_DUSK,  6304, "flag", 2 },
        { "arena-bog-hole.ofpmap",      "Bog Hole",      BIOME_SWAMP,     TIME_DUSK,  6401, "annihilation", 0 },
        { "dm-mire.ofpmap",             "Mire",          BIOME_SWAMP,     TIME_NIGHT, 6402, "deathmatch,warlord", 0 },
        { "conquest-bayou.ofpmap",      "Bayou",         BIOME_SWAMP,     TIME_DAY,   6403, "conquest", 1 },
        { "ctf-long-marsh.ofpmap",      "Long Marsh",    BIOME_SWAMP,     TIME_DUSK,  6404, "flag", 2 } };
    /* smaller matches: maps made for 2 (1 vs 1), 4 and 6 */
    static const struct { const char *file, *name; int biome, tod; unsigned seed; const char *modes; int kind, players; } small[13] = {
        { "ctf-duel-at-dawn.ofpmap",   "Duel at Dawn",  BIOME_CLASSIC,   TIME_DUSK,  7101, "flag", 3, 2 },
        { "ctf-two-dunes.ofpmap",      "Two Dunes",     BIOME_DESERT,    TIME_DAY,   7102, "flag", 3, 2 },
        { "ctf-cold-standoff.ofpmap",  "Cold Standoff", BIOME_WINTER,    TIME_NIGHT, 7103, "flag", 3, 2 },
        { "ctf-small-hours.ofpmap",    "Small Hours",   BIOME_JUNGLE,    TIME_NIGHT, 7104, "flag", 3, 4 },
        { "ctf-rustwater.ofpmap",      "Rustwater",     BIOME_WASTELAND, TIME_DUSK,  7105, "flag", 3, 4 },
        { "conquest-outposts.ofpmap",  "Outposts",      BIOME_SCIFI,     TIME_NIGHT, 7201, "conquest", 1, 4 },
        { "conquest-three-wells.ofpmap","Three Wells",  BIOME_DESERT,    TIME_DUSK,  7202, "conquest", 1, 4 },
        { "dm-pit-stop.ofpmap",        "Pit Stop",      BIOME_CLASSIC,   TIME_DAY,   7301, "deathmatch,warlord", 0, 4 },
        { "dm-ice-box.ofpmap",         "Ice Box",       BIOME_WINTER,    TIME_DUSK,  7302, "deathmatch,warlord", 0, 4 },
        { "dm-half-moon.ofpmap",       "Half Moon",     BIOME_MOON,      TIME_NIGHT, 7303, "deathmatch,warlord", 0, 6 },
        { "arena-duel-ring.ofpmap",    "Duel Ring",     BIOME_SCIFI,     TIME_NIGHT, 7401, "annihilation", 0, 2 },
        { "arena-four-corners.ofpmap", "Four Corners",  BIOME_JUNGLE,    TIME_DAY,   7402, "annihilation", 0, 4 },
        { "arena-ember-ring.ofpmap",   "Ember Ring",    BIOME_VOLCANIC,  TIME_NIGHT, 7403, "annihilation", 0, 4 } };
    int i;
    for (i = 0; i < 13; i++) {
        const char *f = small[i].file;
        map_players = small[i].players;
        if (small[i].kind == 0) { if (arena_map(dir, f, small[i].name, small[i].biome, small[i].tod, small[i].seed, small[i].modes)) return 1; }
        else if (small[i].kind == 1) { if (camps_map(dir, f, small[i].name, small[i].biome, small[i].tod, small[i].seed, -1, 0, 0, 0, "conquest", 1)) return 1; }
        else if (camps_map(dir, f, small[i].name, small[i].biome, small[i].tod, small[i].seed, 32, 68, 74, 38, "flag", 0)) return 1;   /* (closer: a small match) */
    }
    map_players = 0;
    for (i = 0; i < 16; i++) {
        const char *f = worlds[i].file;
        if (worlds[i].kind == 0) { if (arena_map(dir, f, worlds[i].name, worlds[i].biome, worlds[i].tod, worlds[i].seed, worlds[i].modes)) return 1; }
        else if (worlds[i].kind == 1) { if (camps_map(dir, f, worlds[i].name, worlds[i].biome, worlds[i].tod, worlds[i].seed, -1, 0, 0, 0, "conquest", 1)) return 1; }
        else if (camps_map(dir, f, worlds[i].name, worlds[i].biome, worlds[i].tod, worlds[i].seed, 8, 102, 98, 8, "flag", 0)) return 1;
    }
    for (i = 0; i < 8; i++) if (arena_map(dir, ffa[i].file, ffa[i].name, ffa[i].biome, ffa[i].tod, ffa[i].seed, "annihilation")) return 1;
    for (i = 0; i < 6; i++) if (arena_map(dir, tdm[i].file, tdm[i].name, tdm[i].biome, tdm[i].tod, tdm[i].seed, "deathmatch,warlord")) return 1;
    for (i = 0; i < 3; i++) if (camps_map(dir, cq[i].file, cq[i].name, cq[i].biome, cq[i].tod, cq[i].seed, -1, 0, 0, 0, "conquest", 1)) return 1;
    for (i = 0; i < 3; i++) if (camps_map(dir, ctf[i].file, ctf[i].name, ctf[i].biome, ctf[i].tod, ctf[i].seed,
                                          ctf[i].px, ctf[i].py, ctf[i].ex, ctf[i].ey, "flag", 0)) return 1;
    return 0;
}
