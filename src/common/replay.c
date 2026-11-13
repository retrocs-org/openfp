/* SPDX-License-Identifier: MIT */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/replay.h"

void replay_begin(Replay *r, const MatchSetup *ms, const char *map_text, int tanks)
{
    memset(r, 0, sizeof *r);
    r->setup = *ms;
    r->setup.legacy_weather = 0;
    r->tanks = tanks;
    if (map_text) {
        size_t n = strlen(map_text);
        r->map = (char *)malloc(n + 1);
        if (r->map) memcpy(r->map, map_text, n + 1);
        else r->stopped = 1;
    }
}

void replay_free(Replay *r)
{
    free(r->map); free(r->inputs); free(r->checks);
    memset(r, 0, sizeof *r);
}

void replay_tick(Replay *r, const InputBits *in, const Game *after)
{
    if (r->stopped || r->ticks >= REPLAY_MAX_TICKS) return;
    if (r->ticks >= r->cap) {
        uint32_t nc = r->cap ? r->cap * 2 : 3000;
        uint8_t *p;
        if (nc > REPLAY_MAX_TICKS) nc = REPLAY_MAX_TICKS;
        p = (uint8_t *)realloc(r->inputs, (size_t)nc * (size_t)r->tanks);
        if (!p) { r->stopped = 1; return; }
        r->inputs = p; r->cap = nc;
    }
    memcpy(r->inputs + (size_t)r->ticks * (size_t)r->tanks, in, (size_t)r->tanks);
    r->ticks++;
    if (r->ticks % REPLAY_CHECK_EVERY == 0) {
        if (r->nchecks >= r->ccap) {
            uint32_t nc = r->ccap ? r->ccap * 2 : 64;
            uint32_t *p = (uint32_t *)realloc(r->checks, nc * sizeof *p);
            if (!p) { r->stopped = 1; return; }
            r->checks = p; r->ccap = nc;
        }
        r->checks[r->nchecks++] = game_checksum(after);
    }
}

int replay_save(const Replay *r, const char *path)
{
    FILE *f = fopen(path, "w");
    const MatchSetup *ms = &r->setup;
    uint32_t t, run;
    int p;
    if (!f) return -1;
    fprintf(f, "OFPREPLAY %d\nseed=%u biome=%d time=%d garages=%d versus=%d ai=%d players=%d tanks=%d\n",
            ms->bot_commands ? 2 : 1, (unsigned)ms->seed, (int)ms->biome, (int)ms->time, (int)ms->camp_garages, (int)ms->versus, (int)ms->ai_level,
            (int)ms->players, (int)r->tanks);
    for (p = 0; p < ms->players && p < MATCH_PLAYERS; p++)
    {
        char nm[16];
        int q;
        snprintf(nm, sizeof nm, "%s", r->names[p]);
        for (q = 0; nm[q]; q++) if (nm[q] == ' ' || nm[q] == '\n' || nm[q] == '\r') nm[q] = '_';   /* (one word) */
        fprintf(f, "player=%d cls=%d team=%d steer=%d bot=%d%s%s\n", p, (int)ms->cls[p], (int)ms->team[p], (int)ms->steer[p], (int)ms->bot[p],
                nm[0] ? " name=" : "", nm);
    }
    if (!ms->legacy_arena_spawns) fputs("arena_spawns=1\n", f);
    if (!ms->legacy_rules) fprintf(f, "world_rules=1 lives=%u\n", (unsigned)ms->lives);
    if (!ms->legacy_rules && !ms->legacy_surfaces) fprintf(f, "surface_rules=1 powerups=%u\n", (unsigned)!ms->no_powerups);
    if (ms->time_limit) fprintf(f, "time_limit=%u rule=%u\n", (unsigned)ms->time_limit, (unsigned)ms->time_rule);
    if (ms->weather) fprintf(f, "weather=%u\n", (unsigned)ms->weather);
    if (!ms->legacy_weather) fputs("classic_clear=1\n", f);
    if (ms->map_size) fprintf(f, "map_size=%u\n", (unsigned)ms->map_size);   /* (a random battlefield's size + 1: small, medium, huge) */
    if (ms->extras) fprintf(f, "extras=%u\n", (unsigned)ms->extras);
    if (ms->relaxed_pace) fputs("relaxed_pace=1\n", f);
    if (ms->rocket_tanks) fprintf(f, "rocket_tanks=%u\n", (unsigned)ms->rocket_tanks);
    if (ms->leader_mask) fprintf(f, "leaders=%u\n", (unsigned)ms->leader_mask);
    if (r->map) { fprintf(f, "map=%lu\n", (unsigned long)strlen(r->map)); fputs(r->map, f); }
    fputs("ticks\n", f);
    for (t = 0; t < r->ticks; t += run) {           /* runs of identical buttons */
        const uint8_t *cur = r->inputs + (size_t)t * (size_t)r->tanks;
        int k;
        for (run = 1; t + run < r->ticks && !memcmp(cur, cur + (size_t)run * (size_t)r->tanks, (size_t)r->tanks); run++) {
            if ((t + run) % REPLAY_CHECK_EVERY == 0) break; /* keep checks between runs */
        }
        fprintf(f, "%u ", (unsigned)run);
        for (k = 0; k < r->tanks; k++) fprintf(f, "%02x", cur[k]);
        fputc('\n', f);
        if ((t + run) % REPLAY_CHECK_EVERY == 0 && (t + run) / REPLAY_CHECK_EVERY <= r->nchecks)
            fprintf(f, "check %u %08x\n", (unsigned)(t + run), (unsigned)r->checks[(t + run) / REPLAY_CHECK_EVERY - 1]);
    }
    fputs("end\n", f);
    return fclose(f) == 0 ? 0 : -1;
}

int replay_load(Replay *r, const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];
    MatchSetup ms;
    int tanks = 0, seed_ok = 0, have_ticks = 0, ended = 0;
    memset(r, 0, sizeof *r);
    memset(&ms, 0, sizeof ms);
    ms.legacy_weather = 1;
    ms.legacy_rules = 1;
    ms.legacy_surfaces = 1;
    ms.legacy_arena_spawns = 1;
    if (!f) return -1;
    if (!fgets(line, sizeof line, f)) goto fail;
    if (!strcmp(line, "OFPREPLAY 2\n") || !strcmp(line, "OFPREPLAY 2\r\n")) ms.bot_commands = 1;
    else if (strcmp(line, "OFPREPLAY 1\n") && strcmp(line, "OFPREPLAY 1\r\n")) goto fail;
    while (fgets(line, sizeof line, f)) {
        unsigned seed, len;
        int b, tm, ga, vs, ai, pl, tk, p, c, te, st, bt = 0;
        if (sscanf(line, "seed=%u biome=%d time=%d garages=%d versus=%d ai=%d players=%d tanks=%d",
                   &seed, &b, &tm, &ga, &vs, &ai, &pl, &tk) == 8) {
            ms.seed = seed; ms.biome = (uint8_t)b; ms.time = (uint8_t)tm; ms.camp_garages = (uint8_t)ga;
            ms.versus = (uint8_t)vs; ms.ai_level = (uint8_t)ai; ms.players = (uint8_t)pl; tanks = tk;
            seed_ok = 1;
        } else if (sscanf(line, "player=%d cls=%d team=%d steer=%d bot=%d", &p, &c, &te, &st, &bt) >= 4 &&
                   p >= 0 && p < MATCH_PLAYERS) {
            ms.cls[p] = (uint8_t)c; ms.team[p] = (uint8_t)te; ms.steer[p] = (uint8_t)st; ms.bot[p] = (uint8_t)bt;
            {   /* a name, from builds that keep them */
                const char *nm = strstr(line, " name=");
                if (nm) {
                    int q;
                    snprintf(r->names[p], sizeof r->names[p], "%s", nm + 6);
                    for (q = 0; r->names[p][q]; q++)
                        if (r->names[p][q] == '\n' || r->names[p][q] == '\r' || (unsigned char)r->names[p][q] < 32) { r->names[p][q] = 0; break; }
                        else if (r->names[p][q] == '_') r->names[p][q] = ' ';
                }
            }
        } else if (sscanf(line, "time_limit=%u rule=%d", &len, &b) == 2) {
            ms.time_limit = (uint16_t)(len > 65535 ? 65535 : len);
            ms.time_rule = (uint8_t)(b & 1);
        } else if (sscanf(line, "world_rules=1 lives=%u", &len) == 1) {
            ms.legacy_rules = 0;
            ms.lives = (uint8_t)(len == 255 || len <= 99 ? len : 5);
        } else if (sscanf(line, "surface_rules=1 powerups=%u", &len) == 1) {
            ms.legacy_surfaces = 0; ms.no_powerups = !len;
        } else if (sscanf(line, "arena_spawns=%u", &len) == 1) {
            ms.legacy_arena_spawns = len != 1;
        } else if (sscanf(line, "extras=%u", &len) == 1) {
            ms.extras = (uint8_t)(len & EXTRA_ALL);
        } else if (sscanf(line, "relaxed_pace=%u", &len) == 1) {
            ms.relaxed_pace = len == 1;
        } else if (sscanf(line, "rocket_tanks=%u", &len) == 1) {
            ms.rocket_tanks = (uint8_t)(len <= 255 ? len : 0);
        } else if (sscanf(line, "leaders=%u", &len) == 1) {
            ms.leader_mask = (uint8_t)(len <= 255 ? len : 0);
        } else if (sscanf(line, "weather=%u", &len) == 1) {
            ms.weather = (uint8_t)(len < WEATHER_COUNT ? len : WEATHER_CLEAR);
        } else if (sscanf(line, "classic_clear=%u", &len) == 1) {
            ms.legacy_weather = len != 1;
        } else if (sscanf(line, "map_size=%u", &len) == 1) {
            ms.map_size = (uint8_t)(len <= MAP_SIZES ? len : 0);
        } else if (sscanf(line, "map=%u", &len) == 1) {
            if (r->map || !len || len >= 1000000) goto fail;
            r->map = (char *)malloc(len + 1);
            if (!r->map || fread(r->map, 1, len, f) != len || memchr(r->map, 0, len)) goto fail;
            r->map[len] = 0;
        } else if (!strcmp(line, "ticks\n") || !strcmp(line, "ticks\r\n")) {
            have_ticks = 1;
            break;
        }
    }
    if (!seed_ok || !have_ticks || tanks <= 0 || tanks > MAX_TANKS ||
        !ms.players || ms.players > MATCH_PLAYERS || ms.players > tanks) goto fail;
    r->setup = ms;
    r->tanks = tanks;
    while (fgets(line, sizeof line, f)) {
        unsigned tick, sum;
        char extra;
        if (!strcmp(line, "end\n") || !strcmp(line, "end\r\n") || !strcmp(line, "end")) {
            ended = 1;
            break;
        }
        if (sscanf(line, "check %6u %8x %c", &tick, &sum, &extra) == 2) {
            if (tick != r->ticks || tick % REPLAY_CHECK_EVERY ||
                tick / REPLAY_CHECK_EVERY != r->nchecks + 1) goto fail;
            if (r->nchecks >= r->ccap) {
                uint32_t nc = r->ccap ? r->ccap * 2 : 64, *q = (uint32_t *)realloc(r->checks, nc * sizeof *q);
                if (!q) goto fail;
                r->checks = q; r->ccap = nc;
            }
            r->checks[r->nchecks++] = sum;
        } else {
            unsigned long run;
            char *hex;
            uint8_t b[MAX_TANKS];
            uint32_t total;
            int k;
            if (!isdigit((unsigned char)line[0])) goto fail;
            run = strtoul(line, &hex, 10);
            if (!run || run > REPLAY_MAX_TICKS - r->ticks || !isspace((unsigned char)*hex)) goto fail;
            while (isspace((unsigned char)*hex)) hex++;
            if (strspn(hex, "0123456789abcdefABCDEF") != (size_t)tanks * 2) goto fail;
            for (k = 0; k < tanks; k++) {
                unsigned v;
                if (sscanf(hex + k * 2, "%2x", &v) != 1) goto fail;
                b[k] = (uint8_t)v;
            }
            hex += tanks * 2;
            while (isspace((unsigned char)*hex)) hex++;
            if (*hex) goto fail;
            total = r->ticks + (uint32_t)run;
            if (total > r->cap) {
                uint32_t nc = r->cap ? r->cap * 2 : 3000;
                uint8_t *q;
                if (nc < total) nc = total;
                if (nc > REPLAY_MAX_TICKS) nc = REPLAY_MAX_TICKS;
                q = (uint8_t *)realloc(r->inputs, (size_t)nc * (size_t)tanks);
                if (!q) goto fail;
                r->inputs = q; r->cap = nc;
            }
            while (r->ticks < total) {
                memcpy(r->inputs + (size_t)r->ticks * (size_t)tanks, b, (size_t)tanks);
                r->ticks++;
            }
        }
    }
    if (!ended || ferror(f)) goto fail;
    fclose(f);
    return 0;

fail:
    fclose(f);
    replay_free(r);
    return -1;
}

long replay_verify(const Replay *r, Game *g)
{
    uint32_t t;
    if (match_start_recorded(g, &r->setup, r->map, r->tanks) != 0) return 0;
    for (t = 0; t < r->ticks; t++) {
        InputBits in[MAX_TANKS] = { 0 };
        memcpy(in, r->inputs + (size_t)t * (size_t)r->tanks, (size_t)r->tanks);
        game_tick(g, in);
        if ((t + 1) % REPLAY_CHECK_EVERY == 0) {
            uint32_t k = (t + 1) / REPLAY_CHECK_EVERY - 1;
            if (k < r->nchecks && r->checks[k] != game_checksum(g)) return (long)(t + 1);
        }
    }
    return -1;
}
