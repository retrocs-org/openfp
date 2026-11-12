/* SPDX-License-Identifier: MIT
 * Fuzz testing the files strangers hand the game: maps (the web site's gallery, a server's map download, a
 * map dropped on the window) and replays (the web site's, a friend's). Seeded with every real one in
 * data/maps and tests/replays, and mutated the way files go wrong: lines lost, doubled, swapped or cut
 * short, numbers made huge, negative or nonsense, keys misspelt, garbage and long lines added. Each result
 * goes through mapfile_load and match_start (and a few ticks of play), or replay_load and replay_verify.
 *
 *   fuzz_files [iterations] [seed]     ctest runs a short one; build with -DOFP_FUZZ=ON for address and
 *                                      undefined-behaviour checks (a crash names the input it kept)
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L                  /* (popen: listing the seeds) */
#else
#define popen _popen
#define pclose _pclose
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/replay.h"
#include "core/match.h"
#include "core/game.h"
#include "core/world.h"

#ifndef OFP_DATA_DIR
#define OFP_DATA_DIR "data"
#endif
#ifndef OFP_TESTS_DIR
#define OFP_TESTS_DIR "tests"
#endif

enum { MAX_SEEDS = 200, MAX_TEXT = 400000 };
static char *seeds[MAX_SEEDS];
static int is_replay[MAX_SEEDS], nseeds;
static uint32_t rnd = 12345;
static uint32_t next(void) { rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5; return rnd; }

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *t;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > MAX_TEXT / 2 || !(t = malloc((size_t)n + 1))) { fclose(f); return NULL; }
    if (fread(t, 1, (size_t)n, f) != (size_t)n) { free(t); fclose(f); return NULL; }
    t[n] = 0;
    fclose(f);
    return t;
}

static void add_seed(const char *path, int replay)
{
    char *t;
    if (nseeds < MAX_SEEDS && (t = slurp(path)) != NULL) { seeds[nseeds] = t; is_replay[nseeds++] = replay; }
}

/* ---- mutations, on lines ---- */
static char *lines[20000];
static int nlines;
static char pool[MAX_TEXT * 2];
static int pool_at;

static char *keep(const char *s, int n)
{
    char *p;
    if (pool_at + n + 1 > (int)sizeof pool) return (char *)"";
    p = pool + pool_at;
    memcpy(p, s, (size_t)n);
    p[n] = 0;
    pool_at += n + 1;
    return p;
}

static void split(const char *t)
{
    nlines = 0; pool_at = 0;
    while (*t && nlines < (int)(sizeof lines / sizeof *lines)) {
        const char *e = strchr(t, '\n');
        int n = e ? (int)(e - t) : (int)strlen(t);
        lines[nlines++] = keep(t, n);
        t += n + (e ? 1 : 0);
    }
}

static const char *odd_number(void)
{
    static const char *n[] = { "0", "-1", "-2147483648", "2147483647", "4294967295", "99999999999999999999",
                               "65536", "255", "256", "-0", "1e9", "0x10", "", "  7", "3.5", "NaN" };
    return n[next() % (sizeof n / sizeof *n)];
}

static void mutate_line(int i)
{
    char buf[700], *s = lines[i];
    int n = (int)strlen(s), at, k;
    switch (next() % 7) {
    case 0: {                                            /* a number in it: replaced by an odd one */
        for (at = 0; at < n && !(s[at] >= '0' && s[at] <= '9'); at++) ;
        if (at == n) { at = n; }
        for (k = at; k < n && s[k] >= '0' && s[k] <= '9'; k++) ;
        snprintf(buf, sizeof buf, "%.*s%s%s", at, s, odd_number(), s + k);
        break;
    }
    case 1: snprintf(buf, sizeof buf, "%.*s", n ? (int)(next() % (unsigned)n) : 0, s); break;   /* cut short */
    case 2: {                                            /* a character flipped */
        snprintf(buf, sizeof buf, "%s", s);
        if (n) buf[next() % (unsigned)n] = (char)(next() % 256 ? next() % 256 : 'x');
        break;
    }
    case 3: snprintf(buf, sizeof buf, "%s%s", s, s); break;   /* doubled: a long line */
    case 4: {                                            /* the key misspelt (or its "=" lost) */
        char *eq = strchr(s, '=');
        snprintf(buf, sizeof buf, "%s", s);
        if (eq) buf[eq - s] = (char)(next() % 2 ? ' ' : ':');
        break;
    }
    case 5: {                                            /* a run of junk */
        int len = 1 + (int)(next() % 600);
        for (k = 0; k < len && k < (int)sizeof buf - 1; k++) buf[k] = (char)(32 + next() % 95);
        buf[k] = 0;
        break;
    }
    default: snprintf(buf, sizeof buf, "%s %s", s, odd_number()); break;   /* one more field */
    }
    lines[i] = keep(buf, (int)strlen(buf));
}

static int join(char *out, int cap)
{
    int i, at = 0;
    for (i = 0; i < nlines; i++) {
        int n = (int)strlen(lines[i]);
        if (at + n + 2 >= cap) break;
        memcpy(out + at, lines[i], (size_t)n);
        at += n;
        out[at++] = '\n';
    }
    out[at] = 0;
    return at;
}

static void mutate(const char *seed, char *out, int cap)
{
    int m, muts = 1 + (int)(next() % 6), i;
    split(seed);
    for (m = 0; m < muts && nlines > 0; m++) {
        int a = (int)(next() % (unsigned)nlines), b = (int)(next() % (unsigned)nlines);
        switch (next() % 8) {
        case 0: memmove(lines + a, lines + a + 1, (size_t)(nlines - a - 1) * sizeof *lines); nlines--; break;   /* lost */
        case 1: if (nlines < 19999) { memmove(lines + a + 1, lines + a, (size_t)(nlines - a) * sizeof *lines); nlines++; } break;   /* doubled */
        case 2: { char *t = lines[a]; lines[a] = lines[b]; lines[b] = t; break; }   /* swapped */
        case 3: nlines = a + 1; break;                    /* the file cut short */
        case 4: for (i = 0; i < 1 + (int)(next() % 20) && nlines < 19999; i++) lines[nlines++] = keep("99999 99999 99999", 17); break;
        default: mutate_line(a); break;
        }
    }
    join(out, cap);
}

/* ---- the readers ---- */
static Game game;
static Map map;

static void try_map(const char *text)
{
    Base bases[2];
    Biome biome;
    char name[64];
    MatchSetup ms;
    if (mapfile_load(&map, bases, &biome, name, sizeof name, text, 7) == 0) {
        memset(&ms, 0, sizeof ms);
        ms.seed = 3; ms.players = 2; ms.camp_garages = 4; ms.biome = (uint8_t)biome;
        if (match_start(&game, &ms, text) == 0) {
            InputBits in[MAX_TANKS];
            int t;
            memset(in, 0, sizeof in);
            for (t = 0; t < 30; t++) { in[0] = (InputBits)(next() & 0xff); game_tick(&game, in); }
        }
    }
}

static void try_replay(const char *text, int n)
{
    static Replay r;
    static Game back;
    FILE *f = fopen("fuzz-files.ofprep.tmp", "wb");
    if (!f) return;
    fwrite(text, 1, (size_t)n, f);
    fclose(f);
    if (replay_load(&r, "fuzz-files.ofprep.tmp") == 0) {
        if (r.ticks < 4000) replay_verify(&r, &back);
        replay_free(&r);
    }
}

int main(int argc, char **argv)
{
    static char text[MAX_TEXT];
    static const char *map_dirs[] = { OFP_DATA_DIR "/maps", OFP_DATA_DIR "/maps/classic", OFP_DATA_DIR "/maps/original" };
    long iters = argc > 1 ? atol(argv[1]) : 2000, i;
    char cmd[600], path[700];
    int d;
    FILE *ls;
    if (argc > 2) rnd = (uint32_t)atol(argv[2]) | 1;
    for (d = 0; d < 3; d++) {                             /* the seeds: every real map and replay */
#ifdef _WIN32
        snprintf(cmd, sizeof cmd, "dir /b \"%s\\*.ofpmap\" 2>nul", map_dirs[d]);
#else
        snprintf(cmd, sizeof cmd, "ls \"%s\" 2>/dev/null", map_dirs[d]);
#endif
        if ((ls = popen(cmd, "r")) != NULL) {
            while (fgets(path, 300, ls)) {
                char full[1000];
                path[strcspn(path, "\r\n")] = 0;
                if (!strstr(path, ".ofpmap")) continue;
                snprintf(full, sizeof full, "%s/%s", map_dirs[d], path);
                add_seed(full, 0);
            }
            pclose(ls);
        }
    }
#ifdef _WIN32
    snprintf(cmd, sizeof cmd, "dir /b \"%s\\replays\\*.ofprep\" 2>nul", OFP_TESTS_DIR);
#else
    snprintf(cmd, sizeof cmd, "ls \"%s/replays\" 2>/dev/null", OFP_TESTS_DIR);
#endif
    if ((ls = popen(cmd, "r")) != NULL) {
        while (fgets(path, 300, ls)) {
            char full[1000];
            path[strcspn(path, "\r\n")] = 0;
            if (!strstr(path, ".ofprep")) continue;
            snprintf(full, sizeof full, "%s/replays/%s", OFP_TESTS_DIR, path);
            add_seed(full, 1);
        }
        pclose(ls);
    }
    if (nseeds < 2) { printf("fuzz_files: no seeds found (maps %d)\n", nseeds); return 1; }
    for (i = 0; i < iters; i++) {
        int s = (int)(next() % (unsigned)nseeds), n;
        mutate(seeds[s], text, sizeof text);
        n = (int)strlen(text);
        if (getenv("FUZZ_KEEP")) {                       /* (to find the input that crashed: kept each time) */
            FILE *k = fopen("fuzz-files-last.txt", "wb");
            if (k) { fwrite(text, 1, (size_t)n, k); fclose(k); }
        }
        if (is_replay[s]) try_replay(text, n);
        else try_map(text);
    }
    remove("fuzz-files.ofprep.tmp");
    printf("fuzz_files: %ld maps and replays (from %d real ones), mutated: no crash\n", iters, nseeds);
    for (i = 0; i < nseeds; i++) free(seeds[i]);
    return 0;
}
