/* SPDX-License-Identifier: MIT */
#include <string.h>
#include "core/match.h"

static int map_text_fits(const char *t)                /* a map file's size= (none: large): does it fit the grid? */
{
    const char *z = t ? strstr(t, "\nsize=") : NULL;
    int k;
    if (!z) return map_size_fits(SIZE_LARGE);
    z += 6;
    for (k = 0; k < MAP_SIZES; k++)
        if (!strncmp(z, map_size_name[k], strlen(map_size_name[k]))) return map_size_fits(k);
    return 1;
}

int match_weather(const Game *g, int classic, int fallback)
{
    int mode = g->map.custom_bases ? g->map.mode : classic ? MAP_MODE_CLASSIC : -1;
    Biome biome = (Biome)g->biome;
    int weather = g->map.has_weather ? g->map.weather : fallback;
    /* Retain the simulation of old Winter maps and recordings. The clients draw these storms as snow. */
    return map_allows_weather(mode, biome) && weather_allowed(biome, weather_visual(biome, weather)) ? weather : WEATHER_CLEAR;
}

static int match_start_with_limit(Game *g, const MatchSetup *ms, const char *map_text, int tank_limit, int legacy_weather)
{
    if ((!map_text && ms->map_size && !map_size_fits(ms->map_size - 1)) || (map_text && !map_text_fits(map_text)))
        return -2;                                       /* (too big for this build's grid: an Amiga's, say) */
    int p, used[2][MAX_GARAGES];
    worldgen_set_camp_garages(ms->camp_garages ? ms->camp_garages : 4);
    map_set_next_size(SIZE_LARGE);                       /* (a map file: its own size, LARGE if it doesn't say) */
    if (!map_text || game_init_map(g, ms->seed, map_text) != 0) {
        if (map_text) return -1;
        map_set_next_size(ms->map_size ? ms->map_size - 1 : SIZE_LARGE);   /* (a random battlefield: the size asked for) */
        game_init(g, ms->seed, (Biome)(ms->biome < BIOME_COUNT ? ms->biome : 0));
        map_set_next_size(SIZE_LARGE);
        g->map.time = (uint8_t)(ms->time < TIME_COUNT ? ms->time : 0);
    }
    g->versus = ms->versus;
    g->bot_commands = ms->bot_commands;
    g->legacy_arena_spawns = ms->legacy_arena_spawns;
    g->bot_rng = (ms->seed ^ 0x76b9d9edu) | 1u;
    g->relaxed_pace = ms->relaxed_pace;
    g->time_limit = (uint32_t)(ms->legacy_rules ? ms->time_limit :
        !ms->time_limit ? MATCH_DEFAULT_SECONDS : ms->time_limit > MATCH_MAX_SECONDS ? MATCH_MAX_SECONDS : ms->time_limit) * TICK_RATE;
    g->life_limit = ms->legacy_rules ? 0 : ms->lives == MATCH_UNLIMITED_LIVES ? MATCH_UNLIMITED_LIVES :
        !ms->lives ? 5 : ms->lives > MATCH_MAX_LIVES ? MATCH_MAX_LIVES : ms->lives;
    g->vehicle_world = !ms->legacy_rules && ms->camp_garages != 3 &&
        !(g->map.custom_bases && g->map.mode == MAP_MODE_CLASSIC) ? g->biome : BIOME_CLASSIC;
    g->enhanced_rules = !ms->legacy_rules && !ms->legacy_surfaces && g->vehicle_world != BIOME_CLASSIC;
    g->no_powerups = ms->no_powerups || (!ms->legacy_surfaces && g->vehicle_world == BIOME_CLASSIC);
    /* Classic plays clear. Older recordings use the weather they played with. */
    g->weather = (uint8_t)(legacy_weather ? (g->map.has_weather ? g->map.weather : ms->weather) :
                          match_weather(g, ms->camp_garages == 3, ms->weather));
    g->weather_changing = g->weather == WEATHER_CHANGING;
    g->weather_seed = ms->seed ^ 0x5717u;
    if (g->weather_changing) g->weather = WEATHER_CLEAR;   /* (clear to begin with) */
    {
        uint8_t x = g->map.has_extras ? g->map.extras : ms->extras;
        if (g->map.custom_bases) {
            static const uint8_t modes[MAP_MODE_COUNT] = { 0, 0, EXTRA_CONQUEST, EXTRA_TDM, EXTRA_ANNIHILATION, EXTRA_WARLORD, EXTRA_HARDPOINT, EXTRA_KILLCONF };
            x = (x & (EXTRA_PICKUPS | EXTRA_HILL)) | modes[g->map.mode];
        }
        if (!map_text && (x & (EXTRA_ANNIHILATION | EXTRA_TDM | EXTRA_WARLORD | EXTRA_KILLCONF))) game_make_arena(g);   /* deathmatches: arenas */
        if (!ms->legacy_rules && !ms->legacy_surfaces) game_prepare_surfaces(g, ms->seed, !map_text);
        if (g->map.arena) x |= EXTRA_PICKUPS;            /* (an arena: no garage to mend at, so the pickups) */
        if (g->enhanced_rules && !g->no_powerups && !g->map.has_extras) x |= EXTRA_PICKUPS;
        if (g->no_powerups) x &= (uint8_t)~EXTRA_PICKUPS;
        g->ffa_winner = -1;                              /* (every match: nobody's won it alone yet) */
        g->jug[0] = g->jug[1] = -1;
        if (x) game_setup_extras(g, x);
        if (g->map.w <= 64 && !g->map.arena && !(x & EXTRA_CONQUEST)) game_country_split(g);   /* (a small map: each half its own) */
    }
    g->time_rule = ms->time_rule;
    memset(used, 0, sizeof used);
    for (p = 0; p < ms->players && p < MATCH_PLAYERS; p++) {
        int team = ms->team[p] & 1, t, gar, k, ng = g->bases[team].num_garages;
        gar = ms->cls[p] < ng ? ms->cls[p] : 0;
        for (k = 0; k < ng && used[team][gar]; k++) gar = (gar + 1) % ng;
        used[team][gar] = 1;
        t = game_add_tank_in_garage(g, (TankClass)(ms->cls[p] % TANK_CLASS_COUNT), team, gar);
        if (t >= 0) {
            g->tanks[t].steer = ms->steer[p];
            g->tanks[t].rocket_launcher = g->tanks[t].cls == TANK_HEAVY && (ms->rocket_tanks & (1u << p));
            if ((ms->leader_mask & (1u << p)) && !ms->bot[p] &&
                !(g->extras & EXTRA_ANNIHILATION) && g->team_leader[team] < 0)
                g->team_leader[team] = (int8_t)t;
        }
        if (t >= 0 && ms->bot[p]) {
            int level = ms->bot[p] & ~(MATCH_BOT_RAIDER | MATCH_BOT_SIEGE);
            game_set_ai(g, t, level);
            if (level && (ms->bot[p] & MATCH_BOT_RAIDER)) g->raid_bots |= (uint8_t)(1u << t);
            if (level && (ms->bot[p] & MATCH_BOT_SIEGE)) g->siege_bots |= (uint8_t)(1u << t);
        }
    }
    if (!ms->versus && ms->ai_level != AI_OFF && g->num_tanks < tank_limit) {         /* and the computer: light, medium or heavy */
        static const TankClass ai_tank[4] = { TANK_MEDIUM, TANK_LIGHT, TANK_MEDIUM, TANK_HEAVY };
        int lvl = ms->ai_level >= AI_ADAPTIVE ? AI_NORMAL : ms->ai_level;
        int e = game_add_tank_at_base(g, ai_tank[lvl & 3], TEAM_ENEMY);
        if (e >= 0) game_set_ai(g, e, lvl);
        if (ms->ai_level >= AI_ADAPTIVE) {                /* adaptive: from where it was left last time */
            g->adapt = 1;
            g->adapt_skill = (int16_t)((ms->ai_level - AI_ADAPTIVE) * 100);
            if (g->adapt_skill > 200) g->adapt_skill = 200;
        }
    }
    game_extras_tanks(g);                                /* (the extras that need the tanks: now they're in) */
    return 0;
}

int match_start(Game *g, const MatchSetup *ms, const char *map_text)
{
    return match_start_with_limit(g, ms, map_text, MATCH_PLAYERS, 0);
}

int match_benchmark_start(Game *g, int tanks_per_side)
{
    MatchSetup ms = { 0 };
    int i;
    if (tanks_per_side < 1 || tanks_per_side > MATCH_PLAYERS / 2) return -1;
    ms.seed = 2026;
    ms.biome = BIOME_CLASSIC;
    ms.map_size = SIZE_LARGE + 1;
    ms.camp_garages = 4;
    ms.versus = 1;
    ms.players = (uint8_t)(tanks_per_side * 2);
    for (i = 0; i < ms.players; i++) {
        ms.team[i] = i < tanks_per_side ? TEAM_PLAYER : TEAM_ENEMY;
        ms.cls[i] = (i % tanks_per_side) % TANK_CLASS_COUNT;
        ms.bot[i] = AI_NORMAL | MATCH_BOT_RAIDER | MATCH_BOT_SIEGE;
    }
    return match_start(g, &ms, NULL);
}

void match_benchmark_step(Game *g, int ticks)
{
    static const InputBits inputs[MAX_TANKS] = { 0 };
    while (ticks-- > 0) game_tick(g, inputs);
}

int match_start_recorded(Game *g, const MatchSetup *ms, const char *map_text, int recorded_tanks)
{
    /* Older recordings can include the automatic opponent in a full match. */
    int limit = recorded_tanks == MATCH_PLAYERS + 1 ? recorded_tanks : MATCH_PLAYERS;
    return match_start_with_limit(g, ms, map_text, limit, ms->legacy_weather);
}

static uint32_t mix(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

uint32_t match_hash(const MatchSetup *ms, const char *map_text)
{
    uint32_t h = 2166136261u;                        /* field by field: the same on any byte order */
    int i;
    h = mix(h, ms->seed); h = mix(h, ms->biome); h = mix(h, ms->time); h = mix(h, ms->camp_garages);
    if (!ms->legacy_rules) { h = mix(h, 0x574f524cu); h = mix(h, ms->lives); }
    if (!ms->legacy_rules && !ms->legacy_surfaces) { h = mix(h, 0x53555246u); h = mix(h, ms->no_powerups); }
    if (!ms->legacy_arena_spawns) h = mix(h, 0x5350574eu);
    h = mix(h, ms->versus); h = mix(h, ms->ai_level); h = mix(h, ms->players);
    for (i = 0; i < MATCH_PLAYERS; i++) { h = mix(h, ms->cls[i]); h = mix(h, ms->team[i]); h = mix(h, ms->steer[i]); h = mix(h, ms->bot[i]); }
    if (ms->time_limit) { h = mix(h, ms->time_limit); h = mix(h, ms->time_rule); }   /* (unchanged hash without one) */
    if (ms->weather) h = mix(h, 0x57000000u | ms->weather);
    if (ms->extras) h = mix(h, 0x45000000u | ms->extras);
    if (ms->relaxed_pace) h = mix(h, 0x50414345u);
    if (ms->rocket_tanks) h = mix(h, 0x524f4300u | ms->rocket_tanks);
    if (ms->bot_commands) h = mix(h, 0x424f5453u);
    for (; map_text && *map_text; map_text++) h = (h ^ (unsigned char)*map_text) * 16777619u;
    return h;
}

/* a side's room on a map (an arena: half its spawn points; camps: that side's garages; a map made for
 * fewer: half of those), for this mode: the team modes even (each side as many as the smaller holds),
 * every tank for itself the lot. room[0], room[1]: each side's; returns them both together */
int match_sides(const Game *g, uint8_t extras, int room[2])
{
    int c0 = g->map.arena ? (g->map.num_spawns + 1) / 2 : g->bases[0].num_garages;
    int c1 = g->map.arena ? g->map.num_spawns / 2 : g->bases[1].num_garages;
    if (g->map.players) {
        if (c0 > (g->map.players + 1) / 2) c0 = (g->map.players + 1) / 2;
        if (c1 > g->map.players / 2) c1 = g->map.players / 2;
    }
    if (c0 < 1) c0 = 1;
    if (c1 < 1) c1 = 1;
    if (!(extras & EXTRA_ANNIHILATION)) c0 = c1 = c0 < c1 ? c0 : c1;   /* (the team modes: even sides) */
    if (c0 + c1 > MATCH_PLAYERS) { c0 = (MATCH_PLAYERS + 1) / 2; c1 = MATCH_PLAYERS / 2; }
    room[0] = c0; room[1] = c1;
    return c0 + c1;
}

/* how many tanks a map holds for this match (its sides' room, as above; a random battlefield: as the
 * match would make it, an arena in the deathmatches) */
int match_room_sides(const MatchSetup *ms, const char *map_text, int sides[2])
{
    static Game room_g;                                  /* (a big struct: not on the stack) */
    map_set_next_size(SIZE_LARGE);
    if (!map_text || game_init_map(&room_g, ms->seed, map_text) != 0) {
        if (ms->extras & (EXTRA_ANNIHILATION | EXTRA_TDM | EXTRA_WARLORD)) {   /* (a random arena: 8, 4 a side) */
            sides[0] = (MATCH_PLAYERS + 1) / 2; sides[1] = MATCH_PLAYERS / 2;
            return MATCH_PLAYERS;
        }
        worldgen_set_camp_garages(ms->camp_garages ? ms->camp_garages : 4);
        map_set_next_size(ms->map_size ? ms->map_size - 1 : SIZE_LARGE);
        game_init(&room_g, ms->seed, (Biome)(ms->biome < BIOME_COUNT ? ms->biome : 0));
        map_set_next_size(SIZE_LARGE);
    }
    return match_sides(&room_g, ms->extras, sides);
}

int match_room(const MatchSetup *ms, const char *map_text)
{
    int sides[2];
    return match_room_sides(ms, map_text, sides);
}
