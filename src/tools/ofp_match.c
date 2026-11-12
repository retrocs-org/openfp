/* SPDX-License-Identifier: MIT
 * ofp_match: the headless match runner. No graphics, no sound, just the core.
 *
 *   ofp_match bots   [--seed N] [--world NAME] [--map FILE] [--extras N] [--storm] [--ticks N] [--out FILE]
 *                    the computer against itself, to a win (or N ticks), saved as a replay
 *   ofp_match verify FILE
 *                    play a replay back and check every recorded checksum
 *   ofp_match dump   FILE TICK [OUT]
 *                    the whole game state at that tick, as text (to compare two machines)
 *   ofp_match diff   DUMP_A DUMP_B
 *                    where two dumps first differ: the thing that went apart
 *
 * It is the heart of the match server (see docs/NETWORK.md), and a test bench:
 * a replay that stops matching its checksums means the simulation changed.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#endif
#include "core/match.h"
#include "common/replay.h"
#include "common/dump.h"

static Game g;

static char *read_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = (char *)malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) == (size_t)n) b[n] = 0; else { free(b); b = NULL; }
    fclose(f);
    return b;
}

static int bots(int argc, char **argv)
{
    MatchSetup ms;
    Replay r;
    const char *out = "match.ofprep", *mapf = NULL;
    char *map = NULL;
    long limit = 30L * 60 * TICK_RATE;
    int i, t;
    memset(&ms, 0, sizeof ms);
    ms.seed = 1; ms.camp_garages = 4; ms.players = 1; ms.cls[0] = TANK_MEDIUM; ms.team[0] = TEAM_PLAYER;
    ms.bot[0] = AI_HARD; ms.ai_level = AI_HARD;
    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) ms.seed = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--world") && i + 1 < argc) ms.biome = (uint8_t)biome_from_name(argv[++i]);
        else if (!strcmp(argv[i], "--extras") && i + 1 < argc) ms.extras = (uint8_t)(atoi(argv[++i]) & EXTRA_ALL);   /* 1 pickups, 2 hill, 4 conquest, 8 annihilation */
        else if (!strcmp(argv[i], "--storm")) ms.weather = WEATHER_STORM;
        else if (!strcmp(argv[i], "--changing")) ms.weather = WEATHER_CHANGING;   /* (a storm comes and goes) */
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) {   /* a random battlefield's size */
            const char *z = argv[++i];
            ms.map_size = (uint8_t)(1 + (!strcmp(z, "small") ? SIZE_SMALL : !strcmp(z, "medium") ? SIZE_MEDIUM : !strcmp(z, "huge") ? SIZE_HUGE : SIZE_LARGE));
        }
        else if (!strcmp(argv[i], "--map") && i + 1 < argc) mapf = argv[++i];
        else if (!strcmp(argv[i], "--ticks") && i + 1 < argc) limit = atol(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
    }
    if (mapf && !(map = read_text(mapf))) { fprintf(stderr, "cannot read %s\n", mapf); return 1; }
    if (match_start(&g, &ms, map) != 0) { fprintf(stderr, "bad map\n"); return 1; }
    replay_begin(&r, &ms, map, g.num_tanks);
    for (t = 0; t < limit && g.winner < 0; t++) {
        InputBits in[MAX_TANKS] = { 0 };          /* nobody presses anything: the computer drives */
        game_tick(&g, in);
        replay_tick(&r, in, &g);
    }
    printf("%s after %d s (%u ticks), checksum %08x\n",
           g.winner == TEAM_PLAYER ? "our side won" : g.winner == TEAM_ENEMY ? "the enemy won" : "no winner",
           t / TICK_RATE, (unsigned)r.ticks, (unsigned)game_checksum(&g));
    if (replay_save(&r, out) != 0) { fprintf(stderr, "cannot write %s\n", out); return 1; }
    printf("replay written to %s\n", out);
    replay_free(&r);
    free(map);
    return 0;
}

static int check_map(const char *path)
{
    static char text[200001];
    MatchSetup ms = {0};
    FILE *f = fopen(path, "rb");
    size_t n;
    int read_error;
    if (!f) return 1;
    n = fread(text, 1, sizeof text, f);
    read_error = ferror(f);
    fclose(f);
    if (read_error) return 1;
    ms.seed = 1;
    ms.players = 2;
    ms.camp_garages = 4;
    if (n == sizeof text || memchr(text, 0, n) || match_start(&g, &ms, text) != 0) {
        puts("bad map");
        return 2;
    }
    puts("map ok");
    return 0;
}

static int verify(const char *path)
{
    Replay r;
    long bad;
    if (replay_load(&r, path) != 0) { fprintf(stderr, "cannot read replay %s\n", path); return 1; }
    bad = replay_verify(&r, &g);
    if (bad >= 0) {
        printf("DESYNC: %s differs from its recording at tick %ld\n", path, bad);
        replay_free(&r);
        return 2;
    }
    printf("%s: %u ticks, all %u checksums match, %s\n", path, (unsigned)r.ticks, (unsigned)r.nchecks,
           g.winner == TEAM_PLAYER ? "our side won" : g.winner == TEAM_ENEMY ? "the enemy won" : "no winner yet");
    replay_free(&r);
    return 0;
}

static Game dump_game;

static int dump_cmd(int argc, char **argv)
{
    static Replay r;
    FILE *out = stdout;
    uint32_t tick = (uint32_t)strtoul(argv[3], NULL, 10);
    if (replay_load(&r, argv[2]) != 0) { fprintf(stderr, "%s: not a replay this build can read\n", argv[2]); return 1; }
    if (argc > 4 && !(out = fopen(argv[4], "w"))) { fprintf(stderr, "can't write %s\n", argv[4]); return 1; }
    if (tick > r.ticks) fprintf(stderr, "(the replay ends at tick %u: dumped there)\n", (unsigned)r.ticks);
    if (replay_dump_at(&r, tick, &dump_game, out) != 0) { fprintf(stderr, "the replay's match wouldn't start\n"); return 1; }
    if (out != stdout) { fclose(out); printf("%s: the state at tick %u\n", argv[4], (unsigned)(tick > r.ticks ? r.ticks : tick)); }
    replay_free(&r);
    return 0;
}

static int diff_cmd(const char *a, const char *b)
{
    int n = dump_diff(a, b, stdout, 12);
    if (n < 0) { fprintf(stderr, "can't read both files\n"); return 2; }
    if (!n) printf("the same: these two games agree at this tick\n"
                   "(a -game.txt the same as the server's, but its -game-live.txt not: that machine computes the same game,\n"
                   " and what went wrong was a one-off in that run, not the rules)\n");
    else printf("%d lines differ: the first is where they went apart (look at what it names)\n", n);
    return n ? 1 : 0;
}

/* the balance test: equal sides, every tank the computer's at one level, on many maps; each side's wins
 * (is one side favoured by the maps, the rules or the computer players?). --jobs N: N matches at a time,
 * each in a process of its own (the results are the same: a match is its map and its seed) */
static const struct { const char *name; uint8_t extras; } bal_goals[] = {
    { "flag", 0 }, { "conquest", EXTRA_CONQUEST }, { "hill", EXTRA_HILL }, { "annihilation", EXTRA_ANNIHILATION },
    { "deathmatch", EXTRA_TDM }, { "warlord", EXTRA_WARLORD }, { "killconfirmed", EXTRA_KILLCONF }, { "hardpoint", EXTRA_HARDPOINT } };

typedef struct { int map; uint32_t seed; uint8_t biome; } BalJob;
static int bal_size = -1;                                 /* --size: a random battlefield's (SIZE_SMALL ...; -1: large) */   /* map: its index, -1 a random battlefield */
typedef struct { int winner, secs, half; } BalResult;               /* winner: 0 camp, 1 fortress, -1 none */

static BalResult bal_play(char **maptext, const BalJob *j, int gi, int level, int side, long limit)
{
    MatchSetup ms;
    BalResult res = { -1, 0, 0 };
    const char *map = j->map >= 0 ? maptext[j->map] : NULL;
    int room[2] = { side, side }, half, a, t;
    memset(&ms, 0, sizeof ms);
    ms.seed = j->seed;
    ms.extras = bal_goals[gi].extras;
    ms.ai_level = (uint8_t)level;
    ms.versus = 1;                                       /* (side against side, as the team screen and the server set it:
                                                            not, and the computer adds its own enemy tank, one more for the fortress) */
    ms.camp_garages = 4;
    ms.biome = j->biome;
    if (bal_size >= 0 && !map) ms.map_size = (uint8_t)(bal_size + 1);   /* (a random battlefield's size) */
    ms.players = 2;                                      /* (to ask the map's room) */
    if (match_room_sides(&ms, map, room) <= 0) room[0] = room[1] = 1;
    half = side;
    if (half > room[0]) half = room[0];
    if (half > room[1]) half = room[1];
    if (half < 1) half = 1;
    ms.players = (uint8_t)(2 * half);
    for (a = 0; a < ms.players; a++) {                  /* equal sides: the same tanks, all the computer's */
        ms.team[a] = (uint8_t)(a < half ? TEAM_PLAYER : TEAM_ENEMY);
        ms.cls[a] = (uint8_t)((a % half) % TANK_CLASS_COUNT);
        ms.bot[a] = (uint8_t)level;
    }
    res.half = half;
    if (match_start(&g, &ms, map) != 0) { res.secs = -1; return res; }
    for (t = 0; t < limit && g.winner < 0; t++) {
        InputBits in[MAX_TANKS] = { 0 };
        game_tick(&g, in);
    }
    res.secs = t / TICK_RATE;
    res.winner = g.winner == TEAM_PLAYER ? 0 : g.winner == TEAM_ENEMY ? 1 : -1;
    return res;
}

static int balance(int argc, char **argv)
{
    const char *goal = "flag", *csvp = NULL, *world = NULL;
    int level = AI_NORMAL, side = 4, seeds = 4, randoms = 0, jobs = 1, i, k, gi = 0, nmaps = 0, nj = 0, ngood = 0;
    long limit = 20L * 60 * TICK_RATE;
    long tw[2] = { 0, 0 }, tdraw = 0, tsec = 0, tn = 0;
    static const char *maps[512];
    static char *maptext[512], label[512 + 64][48];
    static int map_ok[512];
    BalJob *job;
    BalResult *res;
    FILE *csv = NULL;
    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--goal") && i + 1 < argc) goal = argv[++i];
        else if (!strcmp(argv[i], "--level") && i + 1 < argc) {
            const char *l = argv[++i];
            level = !strcmp(l, "easy") ? AI_EASY : !strcmp(l, "hard") ? AI_HARD : AI_NORMAL;
        }
        else if (!strcmp(argv[i], "--side") && i + 1 < argc) side = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seeds") && i + 1 < argc) seeds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--random") && i + 1 < argc) randoms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--world") && i + 1 < argc) world = argv[++i];
        else if (!strcmp(argv[i], "--minutes") && i + 1 < argc) limit = atol(argv[++i]) * 60L * TICK_RATE;
        else if (!strcmp(argv[i], "--csv") && i + 1 < argc) csvp = argv[++i];
        else if (!strcmp(argv[i], "--jobs") && i + 1 < argc) jobs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) {
            const char *z = argv[++i];
            bal_size = !strcmp(z, "small") ? SIZE_SMALL : !strcmp(z, "medium") ? SIZE_MEDIUM : !strcmp(z, "huge") ? SIZE_HUGE : SIZE_LARGE;
        }
        else if (argv[i][0] != '-' && nmaps < 512) maps[nmaps++] = argv[i];
    }
    for (k = 0; k < (int)(sizeof bal_goals / sizeof bal_goals[0]); k++) if (!strcmp(bal_goals[k].name, goal)) gi = k;
    if (side < 1) side = 1;
    if (side > MATCH_PLAYERS / 2) side = MATCH_PLAYERS / 2;
    if (jobs < 1) jobs = 1;
    if (jobs > 64) jobs = 64;
#ifdef _WIN32
    jobs = 1;                                            /* (no fork: one at a time) */
#endif
    if (csvp && !(csv = fopen(csvp, "w"))) { fprintf(stderr, "cannot write %s\n", csvp); return 1; }
    if (csv) fprintf(csv, "map,seed,side,winner,seconds\n");
    printf("balance: %s, %d a side (at most), level %s, %d seed(s) a map, %d random battlefield(s), up to %ld min a match, %d at a time\n\n",
           bal_goals[gi].name, side, level == AI_EASY ? "easy" : level == AI_HARD ? "hard" : "normal", seeds, randoms, limit / 60 / TICK_RATE, jobs);
    for (k = 0; k < nmaps; k++) {                        /* the maps: read, the ones for other modes skipped */
        const char *base = strrchr(maps[k], '/'), *ml;
        snprintf(label[k], sizeof label[k], "%.40s", base ? base + 1 : maps[k]);
        if (!(maptext[k] = read_text(maps[k]))) { printf("%-28s can't read it\n", label[k]); continue; }
        if ((ml = strstr(maptext[k], "\nmodes=")) != NULL) {
            char v[80];
            int q = 0;
            ml += 7;
            while (ml[q] && ml[q] != '\n' && q < 79) { v[q] = ml[q]; q++; }
            v[q] = 0;
            {   /* (kill confirmed: team deathmatch's arenas; hardpoint: conquest's maps) */
                const char *want = !strcmp(bal_goals[gi].name, "killconfirmed") ? "deathmatch" : !strcmp(bal_goals[gi].name, "hardpoint") ? "conquest" : bal_goals[gi].name;
                if (!strstr(v, want) && !strstr(v, bal_goals[gi].name)) { printf("%-28s skipped (made for %s)\n", label[k], v); continue; }
            }
        }
        map_ok[k] = 1; ngood++;
    }
    for (k = 0; k < randoms; k++) snprintf(label[nmaps + k], sizeof label[0], "random %d%s%s", k + 1, world ? " " : "", world ? world : "");
    job = (BalJob *)calloc((size_t)(ngood * (seeds > 0 ? seeds : 1) + randoms + 1), sizeof *job);
    res = (BalResult *)calloc((size_t)(ngood * (seeds > 0 ? seeds : 1) + randoms + 1), sizeof *res);
    if (!job || !res) { fprintf(stderr, "out of memory\n"); return 1; }
    for (k = 0; k < nmaps; k++)                          /* the list of matches: each map's seeds, then the random ones */
        if (map_ok[k]) for (i = 1; i <= seeds; i++) { job[nj].map = k; job[nj].seed = (uint32_t)i * 7919u; job[nj].biome = (uint8_t)(world ? (int)biome_from_name(world) : 0); nj++; }
    for (k = 0; k < randoms; k++) {
        job[nj].map = -1 - k; job[nj].seed = (uint32_t)(k + 1) * 104729u;
        job[nj].biome = (uint8_t)(world ? (int)biome_from_name(world) : k % (int)BIOME_COUNT);
        nj++;
    }
#ifndef _WIN32
    if (jobs > 1 && nj > 1) {                            /* the workers: each its share, results back down a pipe */
        int fd[2], w;
        if (pipe(fd) != 0) { perror("pipe"); return 1; }
        fflush(stdout);
        for (w = 0; w < jobs; w++) {
            pid_t pid = fork();
            if (pid < 0) { perror("fork"); return 1; }
            if (pid == 0) {
                close(fd[0]);
                for (i = w; i < nj; i += jobs) {
                    BalJob jj = job[i];
                    int rec[4];
                    BalResult r;
                    if (jj.map < 0) jj.map = -1;
                    r = bal_play(maptext, &jj, gi, level, side, limit);
                    rec[0] = i; rec[1] = r.winner; rec[2] = r.secs; rec[3] = r.half;
                    if (write(fd[1], rec, sizeof rec) != (ssize_t)sizeof rec) _exit(1);
                }
                _exit(0);
            }
        }
        close(fd[1]);
        {
            int rec[4], got = 0;
            while (read(fd[0], rec, sizeof rec) == (ssize_t)sizeof rec) {
                if (rec[0] >= 0 && rec[0] < nj) { res[rec[0]].winner = rec[1]; res[rec[0]].secs = rec[2]; res[rec[0]].half = rec[3]; got++; }
                if (got % 20 == 0) { fprintf(stderr, "\r%d of %d matches played", got, nj); fflush(stderr); }
            }
            fprintf(stderr, "\r%d of %d matches played\n", got, nj);
        }
        close(fd[0]);
        while (wait(NULL) > 0) {}
    } else
#endif
    for (i = 0; i < nj; i++) {                           /* (one at a time) */
        BalJob jj = job[i];
        if (jj.map < 0) jj.map = -1;
        res[i] = bal_play(maptext, &jj, gi, level, side, limit);
        if (i % 10 == 9) { fprintf(stderr, "\r%d of %d matches played", i + 1, nj); fflush(stderr); }
    }
    if (nj >= 10) fprintf(stderr, "\n");
    for (i = 0; i < nj; ) {                              /* each map's line, in order; and the totals */
        int lk = job[i].map >= 0 ? job[i].map : nmaps + (-1 - job[i].map), j2 = i;
        long w[2] = { 0, 0 }, draw = 0, sec = 0, n = 0;
        while (j2 < nj && (job[j2].map >= 0 ? job[j2].map : nmaps + (-1 - job[j2].map)) == lk) {
            const BalResult *r = &res[j2];
            if (r->secs < 0) { printf("%-28s won't start\n", label[lk]); j2++; continue; }
            n++; sec += r->secs;
            if (r->winner == 0) w[0]++; else if (r->winner == 1) w[1]++; else draw++;
            if (csv) fprintf(csv, "%s,%u,%d,%s,%d\n", label[lk], (unsigned)job[j2].seed, r->half,
                             r->winner == 0 ? "camp" : r->winner == 1 ? "fortress" : "none", r->secs);
            j2++;
        }
        i = j2;
        if (!n) continue;
        printf("%-28s camp %ld, fortress %ld, no winner %ld   (%ld s a match)\n", label[lk], w[0], w[1], draw, sec / n);
        tw[0] += w[0]; tw[1] += w[1]; tdraw += draw; tsec += sec; tn += n;
    }
    if (csv) fclose(csv);
    for (k = 0; k < nmaps; k++) free(maptext[k]);
    free(job); free(res);
    if (!tn) { printf("no matches played\n"); return 1; }
    {
        long decided = tw[0] + tw[1], diff = tw[0] - tw[1], sd2 = 0;
        while ((sd2 + 1) * (sd2 + 1) <= decided) sd2++;  /* (the square root, near enough) */
        printf("\n%ld matches: camp won %ld (%ld%%), the fortress %ld (%ld%%), no winner %ld; %ld s a match\n", tn,
               tw[0], decided ? tw[0] * 100 / decided : 0, tw[1], decided ? tw[1] * 100 / decided : 0, tdraw, tsec / tn);
        /* (even sides: the difference in wins is within about 2 x sqrt(decided) by chance alone) */
        printf("%s\n", decided < 20 ? "too few decided matches to say: play more (--seeds, --random)"
                     : labs(diff) > 2 * sd2 ? (diff > 0 ? "the camp is favoured: more than chance would give" : "the fortress is favoured: more than chance would give")
                     : "even: the difference is within what chance gives");
    }
    return 0;
}

/* Observation stays in this tool: no counters or timing enter the simulation. */
typedef struct {
    uint32_t alive, home, enemy, repair, patrol, attack, carrying, pickups, shots, low_progress;
    int first_enemy;
} EvalTeam;

static unsigned eval_number(const char *s, unsigned low, unsigned high)
{
    char *end;
    unsigned long n;
    if (!s[0] || s[0] == '-') return 0;
    errno = 0;
    n = strtoul(s, &end, 10);
    return !errno && !*end && n >= low && n <= high ? (unsigned)n : 0;
}

static int eval_inside(const Base *b, const Tank *t)
{
    int x = fx_to_int(t->x) >> TILE_SHIFT, y = fx_to_int(t->y) >> TILE_SHIFT;
    return x >= b->x0 && x <= b->x1 && y >= b->y0 && y <= b->y1;
}

static int evaluate(int argc, char **argv)
{
    MatchSetup ms = {0};
    EvalTeam stats[2] = {{0}, {0}};
    Replay replay;
    const char *map_path = NULL, *out = NULL, *profile = "server", *goal = "flag";
    char *map;
    unsigned seed = 1, side = 4, minutes = 20;
    int level = AI_NORMAL, gi = -1, i, k, half, room[2] = {4,4};
    fx sample_x[MAX_TANKS] = {0}, sample_y[MAX_TANKS] = {0};
    unsigned sample_live[MAX_TANKS] = {0};
    for (i = 2; i < argc; i++) {
        const char *opt = argv[i], *value;
        if (i + 1 == argc) { fprintf(stderr, "missing value for %s\n", opt); return 2; }
        value = argv[++i];
        if (!strcmp(opt, "--map")) map_path = value;
        else if (!strcmp(opt, "--out")) out = value;
        else if (!strcmp(opt, "--profile")) profile = value;
        else if (!strcmp(opt, "--goal")) goal = value;
        else if (!strcmp(opt, "--seed")) seed = eval_number(value, 1, UINT32_MAX);
        else if (!strcmp(opt, "--side")) side = eval_number(value, 1, MATCH_PLAYERS / 2);
        else if (!strcmp(opt, "--minutes")) minutes = eval_number(value, 1, 30);
        else if (!strcmp(opt, "--level")) {
            level = !strcmp(value,"easy") ? AI_EASY : !strcmp(value,"normal") ? AI_NORMAL : !strcmp(value,"hard") ? AI_HARD : AI_OFF;
        } else { fprintf(stderr, "unknown evaluation option %s\n", opt); return 2; }
    }
    for (i = 0; i < (int)(sizeof bal_goals / sizeof bal_goals[0]); i++)
        if (!strcmp(goal, bal_goals[i].name)) gi = i;
    if (!map_path || !seed || !side || !minutes || !level || gi < 0 ||
        (strcmp(profile,"server") && strcmp(profile,"local"))) {
        fprintf(stderr, "evaluate requires --map FILE; valid --seed, --side 1..4, --minutes 1..30, --level easy|normal|hard, --profile server|local and --goal\n");
        return 2;
    }
    map = read_text(map_path);
    if (!map) { fprintf(stderr,"cannot read %s\n",map_path); return 1; }
    ms.seed = seed; ms.players = 2; ms.camp_garages = 4; ms.versus = 1;
    ms.ai_level = (uint8_t)level; ms.extras = bal_goals[gi].extras;
    ms.bot_commands = 1; ms.time_limit = (uint16_t)(minutes * 60);
    if (match_room_sides(&ms,map,room) < 2 || room[0] < 1 || room[1] < 1) {
        fprintf(stderr,"map has no room for opposing teams\n"); free(map); return 1;
    }
    half = (int)side;
    if (half > room[0]) half = room[0];
    if (half > room[1]) half = room[1];
    ms.players = (uint8_t)(2 * half);
    for (i = 0; i < ms.players; i++) {
        k = i % half;
        ms.team[i] = (uint8_t)(i < half ? TEAM_PLAYER : TEAM_ENEMY);
        ms.cls[i] = (uint8_t)((k + seed % TANK_CLASS_COUNT) % TANK_CLASS_COUNT);
        ms.bot[i] = (uint8_t)level;
        /* Local all-bot CTF sides leave one opening defender; Classic never gets these flags. */
        if (!strcmp(profile,"local") && gi == 0 && (half == 1 || k > 0))
            ms.bot[i] |= MATCH_BOT_RAIDER | MATCH_BOT_SIEGE;
    }
    if (match_start(&g,&ms,map)) { fprintf(stderr,"bad map\n"); free(map); return 1; }
    if (g.biome == BIOME_CLASSIC || !g.enhanced_rules) {
        for (i = 0; i < ms.players; i++) {
            ms.bot[i] = (uint8_t)level;
            ms.cls[i] = (uint8_t)(((i % half) + seed % 3) % 3);
        }
        if (match_start(&g,&ms,map)) { free(map); return 1; }
    }
    if ((g.extras & ~(EXTRA_PICKUPS)) != bal_goals[gi].extras) {
        fprintf(stderr,"map overrides the requested game mode; choose its authored mode\n");
        free(map); return 2;
    }
    stats[0].first_enemy = stats[1].first_enemy = -1;
    if (out) replay_begin(&replay,&ms,map,g.num_tanks);
    while (g.winner < 0 && !g.time_up && g.tick < minutes * 60u * TICK_RATE) {
        InputBits input[MAX_TANKS] = {0};
        uint8_t flag[MAX_TANKS], reload[MAX_TANKS];
        for (i = 0; i < g.num_tanks; i++) {
            const Tank *t = &g.tanks[i];
            flag[i] = t->has_flag; reload[i] = t->reload;
            if (g.tick % TICK_RATE == 0) { sample_x[i] = t->x; sample_y[i] = t->y; sample_live[i] = !t->out && !t->respawn && t->active; }
        }
        game_tick_server(&g,input);
        if (out) replay_tick(&replay,input,&g);
        for (i = 0; i < g.num_tanks; i++) {
            const Tank *t = &g.tanks[i];
            EvalTeam *s = &stats[t->team & 1];
            int mode = g.ai[i].mode;
            if (!flag[i] && t->has_flag) s->pickups++;
            if (!reload[i] && t->reload) s->shots++;
            if (!t->active || t->out || t->respawn) { sample_live[i] = 0; continue; }
            s->alive++;
            if (!g.map.arena && eval_inside(&g.bases[t->team & 1],t)) s->home++;
            if (!g.map.arena && eval_inside(&g.bases[1 - (t->team & 1)],t)) {
                s->enemy++;
                if (s->first_enemy < 0) s->first_enemy = (int)g.tick;
            }
            s->repair += mode == AIM_REPAIR;
            s->patrol += mode == AIM_PATROL;
            s->attack += mode == AIM_RAID || mode == AIM_FETCH || mode == AIM_HUNT || mode == AIM_POINT || mode == AIM_HILL;
            s->carrying += t->has_flag != 0;
            if (g.tick % TICK_RATE == 0 && sample_live[i] &&
                (mode == AIM_RAID || mode == AIM_FETCH || mode == AIM_HOME || mode == AIM_HUNT || mode == AIM_CHASE || mode == AIM_RECOVER) &&
                llabs((long long)t->x - sample_x[i]) < FX(2) && llabs((long long)t->y - sample_y[i]) < FX(2)) s->low_progress++;
        }
    }
    if (out) {
        int saved = replay.stopped ? -1 : replay_save(&replay,out);
        replay_free(&replay);
        if (saved) { fprintf(stderr,"cannot save complete replay\n"); free(map); return 1; }
    }
    printf("{\"schema\":1,\"seed\":%u,\"goal\":\"%s\",\"profile\":\"%s\",\"world\":\"%s\",\"side\":%d,\"ticks\":%u,\"tick_rate\":%d,\"winner\":%d,\"time_up\":%s,\"checksum\":\"%08x\",\"teams\":[",
           seed,goal,profile,biome_name((Biome)g.biome),half,(unsigned)g.tick,TICK_RATE,(int)g.winner,g.time_up ? "true" : "false",(unsigned)game_checksum(&g));
    for (k = 0; k < 2; k++) {
        EvalTeam *s = &stats[k];
        unsigned score = 0, kills = 0, lost = 0, saved = 0, objective = 0;
        for (i = 0; i < g.num_tanks; i++) if ((g.tanks[i].team & 1) == k) {
            score += g.tanks[i].score; kills += g.tanks[i].tank_kills; lost += g.tanks[i].lost; saved += g.tanks[i].saved;
        }
        if (g.extras & EXTRA_CONQUEST) objective = g.cp_score[k];
        else if (g.extras & EXTRA_HARDPOINT) objective = g.hp_score[k];
        else if (g.extras & EXTRA_KILLCONF) objective = g.kc_score[k];
        else if (g.extras & EXTRA_WARLORD) objective = g.jug_score[k];
        else if (g.extras & EXTRA_HILL) objective = g.hill[k] / TICK_RATE;
        printf("%s{\"alive_ticks\":%u,\"home_ticks\":%u,\"enemy_base_ticks\":%u,\"repair_ticks\":%u,\"patrol_ticks\":%u,\"attack_ticks\":%u,\"carrying_ticks\":%u,\"flag_pickups\":%u,\"shots\":%u,\"low_progress_seconds\":%u,\"first_enemy_base_tick\":%d,\"score\":%u,\"tank_kills\":%u,\"deaths\":%u,\"rescued\":%u,\"objective_score\":%u}",
               k ? "," : "",s->alive,s->home,s->enemy,s->repair,s->patrol,s->attack,s->carrying,s->pickups,s->shots,s->low_progress,s->first_enemy,score,kills,lost,saved,objective);
    }
    puts("]}"); free(map); return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "bots")) return bots(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "balance")) return balance(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "evaluate")) return evaluate(argc, argv);
    if (argc == 3 && !strcmp(argv[1], "check-map")) return check_map(argv[2]);
    if (argc >= 3 && !strcmp(argv[1], "verify")) return verify(argv[2]);
    if (argc >= 4 && !strcmp(argv[1], "dump")) return dump_cmd(argc, argv);
    if (argc >= 4 && !strcmp(argv[1], "diff")) return diff_cmd(argv[2], argv[3]);
    fprintf(stderr, "usage: ofp_match bots [--seed N] [--world NAME] [--map FILE] [--extras N] [--storm] [--ticks N] [--out FILE]\n"
                    "       ofp_match evaluate --map FILE [--goal flag|conquest|hill|annihilation|deathmatch|warlord|killconfirmed|hardpoint]\n"
                    "                          [--profile server|local] [--level easy|normal|hard] [--side 1..4] [--seed N] [--minutes 1..30] [--out REPLAY]\n"
                    "       ofp_match verify FILE.ofprep\n"
                    "       ofp_match check-map FILE.ofpmap\n"
                    "       ofp_match balance [--goal flag|conquest|hill|annihilation|deathmatch|warlord] [--level easy|normal|hard]\n"
                    "                         [--side N] [--seeds N] [--random N] [--world NAME] [--minutes M] [--jobs N] [--size small|medium|large|huge] [--csv FILE] [MAP.ofpmap ...]\n");
    return 1;
}
