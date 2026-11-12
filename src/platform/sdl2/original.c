/* SPDX-License-Identifier: MIT */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/world.h"
#include "platform/sdl2/original.h"
#include "platform/sdl2/maps.h"
#include "platform/sdl2/theme.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

/* ---- reading an AmigaDOS disk image (OFS and FFS) --------------------------------- */

#define BS 512
typedef struct { const unsigned char *d; long n; int ffs; } Adf;

static unsigned long u32(const unsigned char *b, int i)
{
    b += i * 4;
    return ((unsigned long)b[0] << 24) | ((unsigned long)b[1] << 16) | ((unsigned long)b[2] << 8) | b[3];
}
static const unsigned char *blk(const Adf *a, unsigned long n)
{
    return n > 0 && (long)((n + 1) * BS) <= a->n ? a->d + n * BS : NULL;
}
static void name_of(const unsigned char *b, char *out)
{
    int n = b[BS - 80] > 30 ? 30 : b[BS - 80];
    memcpy(out, b + BS - 79, (size_t)n);
    out[n] = 0;
}

/* read a file whose header block is h into out (cap bytes); returns its size or -1 */
static long read_file(const Adf *a, unsigned long h, unsigned char *out, long cap)
{
    const unsigned char *e = blk(a, h), *b;
    long size, got = 0;
    int guard = 0;
    if (!e) return -1;
    size = (long)u32(e, BS / 4 - 47);
    if (size > cap) return -1;
    if (a->ffs) {
        for (b = e; b && guard++ < 64; ) {
            int i;
            for (i = BS / 4 - 51; i > 5 && got < size; i--) {
                const unsigned char *db = blk(a, u32(b, i));
                long take;
                if (!u32(b, i)) continue;
                if (!db) return -1;
                take = size - got < BS ? size - got : BS;
                memcpy(out + got, db, (size_t)take);
                got += take;
            }
            b = u32(b, BS / 4 - 2) ? blk(a, u32(b, BS / 4 - 2)) : NULL;
        }
    } else {
        unsigned long p = u32(e, 4);
        while (p && guard++ < 4000 && got < size) {
            const unsigned char *db = blk(a, p);
            long take;
            if (!db) return -1;
            take = (long)u32(db, 3);
            if (take > size - got) take = size - got;
            memcpy(out + got, db + 24, (size_t)take);
            got += take;
            p = u32(db, 4);
        }
    }
    return got == size ? size : -1;
}

/* ---- the conversion (as tools/ofp_maps.py) ---------------------------------------- */

#define MAP_N 64
/* Only street blocks supply asphalt. See tools/ofp_maps.py. */
static const char *const CELLS[] = {
    [2] = "PPPP", [3] = ".T..", [4] = "..P.",
    [5] = ".===", [6] = "====", [7] = "=.==", [8] = "====", [10] = "====",
    [11] = "==.=", [12] = "====", [13] = "===.", [15] = "====", [16] = "====",
};
static const struct { int block; const char *name; } STRUCTS[] = {
    { 9, "cannon" }, { 29, "cannon" }, { 31, "cannon" }, { 33, "cannon" }, { 37, "cannon" }, { 38, "cannon" },
    { 35, "heavy" }, { 17, "warehouse" }, { 18, "depot" }, { 19, "powcamp" },
    { 23, "barracks" }, { 25, "firstaid" }, { 27, "radar" }, { 14, "helipad" },
};
static int is_rubble(int i) { return i == 22 || i == 28 || i == 30 || i == 39 || i == 40 || i == 44 || i == 45 || i == 46 || i == 49; }
static int is_ruins(int i) { return i == 24; }
/* the concrete barriers walling the compounds: these blocks only ever turn up in
 * the closed rings round them (50, the corner pieces, has no picture) */
static int is_wall(int i) { return i == 21 || i == 34 || i == 36 || i == 41 || i == 42 || i == 43 || i == 47 || i == 48 || i == 50; }
#define GARAGE_BLOCK 20                    /* the player's own garage: once per map */
static const char *struct_at(int i)
{
    size_t k;
    for (k = 0; k < sizeof STRUCTS / sizeof STRUCTS[0]; k++) if (STRUCTS[k].block == i) return STRUCTS[k].name;
    return NULL;
}

#define ORIG_W 128                                  /* an original level: 64 x 64 blocks, 128 x 128 tiles: LARGE */
#define ORIG_H 128
static char tiles[ORIG_H][ORIG_W];

static unsigned char occupied[ORIG_H][ORIG_W];

static int distance_sq(int x, int y, int bx, int by)
{
    int dx = x - bx, dy = y - by;
    return dx * dx + dy * dy;
}

static int garage_score(int x, int y)
{
    int tx, ty, score = 100 * occupied[y + 3][x + 1];
    for (ty = y; ty < y + 3; ty++)
        for (tx = x; tx < x + 3; tx++)
            score += 100 * occupied[ty][tx] + 10 * (tiles[ty][tx] != '.');
    if (tiles[y + 3][x + 1] != '=' && tiles[y + 3][x + 1] != '.') score += 10;
    if (tiles[y + 3][x + 1] != '=') score++;
    return score;
}

static int convert(const unsigned char *raw, const char *title, const char *path)
{
    int bx, by, k, y, best = 0x7fffffff, cp[2][2];
    FILE *f;
    memset(occupied, 0, sizeof occupied);
    for (by = 0; by < MAP_N; by++)
        for (bx = 0; bx < MAP_N; bx++) {
            int i = raw[bx * MAP_N + by] & 63;                  /* stored column by column */
            const char *c = i < (int)(sizeof CELLS / sizeof CELLS[0]) ? CELLS[i] : NULL;
            for (k = 0; k < 4; k++) {
                char ch = is_wall(i) ? '#' : is_rubble(i) ? ':' : is_ruins(i) ? 'x' : c ? c[k] : '.';
                tiles[by * 2 + (k >> 1)][bx * 2 + (k & 1)] = ch;
            }
            if (struct_at(i)) {
                int tx, ty;
                for (ty = by * 2; ty < by * 2 + 4 && ty < ORIG_H; ty++)
                    for (tx = bx * 2; tx < bx * 2 + 4 && tx < ORIG_W; tx++) occupied[ty][tx] = 1;
            }
        }
    {
        int px = 3, py = 96, ex = 3, ey = 3, x0, y0, found = 0;
        for (by = 0; by < MAP_N && !found; by++)
            for (bx = 0; bx < MAP_N && !found; bx++)
                if ((raw[bx * MAP_N + by] & 63) == GARAGE_BLOCK) {
                    px = bx * 2; py = by * 2; found = 1;
                }
        if (px < 1) px = 1;
        if (px > ORIG_W - 4) px = ORIG_W - 4;
        if (py < 1) py = 1;
        if (py > ORIG_H - 5) py = ORIG_H - 5;
        for (y0 = 8; y0 < ORIG_H - 8; y0++)
            for (x0 = 8; x0 < ORIG_W - 8; x0++) {
                int score;
                if (distance_sq(x0, y0, px, py) < 60 * 60) continue;
                score = garage_score(x0, y0);
                if (score < best) { best = score; ex = x0; ey = y0; }
            }
        cp[0][0] = px; cp[0][1] = py; cp[1][0] = ex; cp[1][1] = ey;
    }
    if ((f = fopen(path, "wb")) == NULL) return -1;
    fprintf(f, "OFPMAP 1\nname=%s\nmodes=flag\nbiome=classic\nambient=off\nplayers=2\nbases=garages\nbase=player %d %d\nbase=enemy %d %d\ntiles\n", title,
            cp[0][0], cp[0][1], cp[1][0], cp[1][1]);
    for (y = 0; y < ORIG_H; y++) { fwrite(tiles[y], 1, ORIG_W, f); fputc('\n', f); }
    for (by = 0; by < MAP_N; by++)
        for (bx = 0; bx < MAP_N; bx++) {
            const char *s = struct_at(raw[bx * MAP_N + by] & 63);
            if (s) {
                int tx = bx * 2, ty = by * 2;
                int friendly = !strcmp(s, "firstaid") &&
                               distance_sq(tx, ty, cp[0][0], cp[0][1]) < distance_sq(tx, ty, cp[1][0], cp[1][1]);
                fprintf(f, "struct=%s %s %d %d\n", s, friendly ? "player" : "enemy", tx, ty);
            }
        }
    fputs("end\n", f);
    return fclose(f) == 0 ? 0 : -1;
}

int original_import(const char *adf_path, char *msg, size_t cap)
{
    Adf a;
    unsigned char *img = NULL, maps[16][MAP_N * MAP_N];
    FILE *f = fopen(adf_path, "rb");
    long n;
    int i, written = 0, found = 0;
    const unsigned char *root;
    snprintf(msg, cap, "NO COMPATIBLE MAPS ON THAT DISK");
    if (!f) { snprintf(msg, cap, "COULD NOT OPEN THE DISK IMAGE"); return 0; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 100 * BS || n > 4 * 1024 * 1024 || (img = (unsigned char *)malloc((size_t)n)) == NULL ||
        fread(img, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(img); return 0; }
    fclose(f);
    if (memcmp(img, "DOS", 3)) { free(img); snprintf(msg, cap, "NOT AN AMIGADOS DISK IMAGE"); return 0; }
    a.d = img; a.n = n; a.ffs = img[3] & 1;
    if ((root = blk(&a, (unsigned long)(n / BS / 2))) == NULL) { free(img); return 0; }
    /* MapP1 ... MapP3 directories, each with t1 ... t5 */
    for (i = 6; i < 6 + 72; i++) {
        unsigned long h = u32(root, i);
        int guard = 0;
        while (h && guard++ < 100) {
            const unsigned char *e = blk(&a, h);
            char nm[32];
            if (!e) break;
            name_of(e, nm);
            if (!strncmp(nm, "Map", 3) && (long)u32(e, BS / 4 - 1) == 2) {       /* a directory */
                int j;
                for (j = 6; j < 6 + 72; j++) {
                    unsigned long fh = u32(e, j);
                    int g2 = 0;
                    while (fh && g2++ < 100) {
                        const unsigned char *fe = blk(&a, fh);
                        char fn[32], title[64], path[1200];
                        int k, dup = 0;
                        if (!fe) break;
                        name_of(fe, fn);
                        if (found < 16 && read_file(&a, fh, maps[found], MAP_N * MAP_N) == MAP_N * MAP_N) {
                            for (k = 0; k < found; k++) dup |= !memcmp(maps[k], maps[found], MAP_N * MAP_N);
                            if (!dup) {                          /* several levels share one map */
                                snprintf(title, sizeof title, "Original %.20s %.20s", nm + 3, fn);
                                snprintf(path, sizeof path, "%soriginal-%s-%s.ofpmap", maps_user_dir(), nm + 3, fn);
                                if (convert(maps[found], title, path) == 0) written++;
                                found++;
                            }
                        }
                        fh = u32(fe, BS / 4 - 4);
                    }
                }
            }
            h = u32(e, BS / 4 - 4);
        }
    }
    free(img);
    if (written) snprintf(msg, cap, "%d ORIGINAL MAPS IMPORTED", written);
    return written;
}

/* the usual places a disk image lands */
static int try_dir(const char *dir, char *msg, size_t cap)
{
    int n = 0;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    char pat[1100];
    HANDLE h;
    snprintf(pat, sizeof pat, "%s\\*.adf", dir);
    h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            char p[1200];
            snprintf(p, sizeof p, "%s\\%s", dir, fd.cFileName);
            n = original_import(p, msg, cap);
        } while (!n && FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) return 0;
    while (!n && (e = readdir(d)) != NULL) {
        size_t l = strlen(e->d_name);
        char p[1200];
        if (l < 5 || (strcmp(e->d_name + l - 4, ".adf") && strcmp(e->d_name + l - 4, ".ADF"))) continue;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        n = original_import(p, msg, cap);
    }
    closedir(d);
#endif
    return n;
}

int original_import_search(char *msg, size_t cap)
{
    const char *home = getenv("HOME");
    char dir[1100];
    int n;
#ifdef _WIN32
    if (!home) home = getenv("USERPROFILE");
#endif
    snprintf(msg, cap, "NO .ADF FOUND: DROP IT ON THE WINDOW");
    if ((n = try_dir(".", msg, cap)) > 0) return n;
    snprintf(dir, sizeof dir, "%s", theme_data_dir());
    if ((n = try_dir(dir, msg, cap)) > 0) return n;
    snprintf(dir, sizeof dir, "%s/..", theme_data_dir());
    if ((n = try_dir(dir, msg, cap)) > 0) return n;
    snprintf(dir, sizeof dir, "%s/../..", theme_data_dir());
    if ((n = try_dir(dir, msg, cap)) > 0) return n;
    if (home) {
        static const char *sub[] = { "Downloads", "Desktop", "Documents", "" };
        size_t i;
        for (i = 0; i < sizeof sub / sizeof sub[0]; i++) {
            snprintf(dir, sizeof dir, "%s/%s", home, sub[i]);
            if ((n = try_dir(dir, msg, cap)) > 0) return n;
        }
    }
    if (!strstr(msg, "IMPORTED") && !strstr(msg, "DROP")) {}
    return 0;
}
