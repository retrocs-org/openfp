/* SPDX-License-Identifier: MIT */
#include "platform/sdl2/pref_path.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/sdl2/maps.h"
#include "platform/sdl2/theme.h"
#include "core/world.h"
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

const char *maps_user_dir(void)
{
    static char dir[1024];
    if (!dir[0]) {
        char *pref = ofp_pref_path();
#ifdef _WIN32
        snprintf(dir, sizeof dir, "%smaps\\", pref ? pref : ".\\");
        _mkdir(dir);
#else
        snprintf(dir, sizeof dir, "%smaps/", pref ? pref : "./");
        mkdir(dir, 0755);
#endif
        SDL_free(pref);
    }
    return dir;
}

char *maps_read(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *buf;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 4 * 1024 * 1024 || (buf = (char *)malloc((size_t)n + 1)) == NULL) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    buf[n] = 0;
    fclose(f);
    return buf;
}

int maps_write(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    size_t n = strlen(text);
    if (!f) return -1;
    if (fwrite(text, 1, n, f) != n) { fclose(f); return -1; }
    return fclose(f) == 0 ? 0 : -1;
}

/* the name= and biome= lines near the top of a map file */
/* the modes a map's made for: "flag", "conquest", "annihilation", "deathmatch", "warlord" (bits as LOCAL GAME's modes) */
static int mode_bits(const char *v)
{
    int m = 0;
    if (strstr(v, "flag")) m |= 1;
    if (strstr(v, "conquest")) m |= 2;
    if (strstr(v, "annihilation")) m |= 4;
    if (strstr(v, "deathmatch")) m |= 8;
    if (strstr(v, "warlord")) m |= 16;
    if (strstr(v, "killconfirmed")) m |= 8;              /* (kill confirmed: team deathmatch's arenas) */
    if (strstr(v, "hardpoint")) m |= 2;                  /* (hardpoint: conquest's points) */
    return m;
}

static int read_modes;                      /* (read_name's: the modes it saw, or -1) */
static int read_arena, read_spawns, read_players, read_size, read_custom;

static void read_name(const char *path, char *name, size_t cap, int *biome, int *tod, int *weather, int *extras)
{
    char line[256];
    FILE *f = fopen(path, "rb");
    int k;
    const char *base = strrchr(path, '/');
    snprintf(name, cap, "%.63s", base ? base + 1 : path);
    if (!f) return;
    *biome = 0;
    *tod = TIME_DAY;
    *weather = *extras = -1;
    read_modes = -1; read_arena = 0; read_spawns = 0; read_players = 0; read_size = SIZE_LARGE; read_custom = 0;
    for (k = 0; k < 128 && fgets(line, sizeof line, f); k++) {
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = 0;
        if (!strncmp(line, "name=", 5)) snprintf(name, cap, "%.63s", line + 5);
        if (!strncmp(line, "biome=", 6)) *biome = (int)biome_from_name(line + 6);
        if (!strncmp(line, "time=", 5)) *tod = time_from_name(line + 5);
        if (!strncmp(line, "weather=", 8)) *weather = weather_from_name(line + 8);
        if (!strncmp(line, "extras=", 7)) *extras = (strstr(line + 7, "pickups") ? 1 : 0) | (strstr(line + 7, "hill") ? 2 : 0);
        if (!strncmp(line, "modes=", 6)) read_modes = mode_bits(line + 6);
        if (!strcmp(line, "bases=custom")) read_custom = 1;
        if (!strncmp(line, "arena=yes", 9)) read_arena = 1;
        if (!strncmp(line, "players=", 8)) read_players = atoi(line + 8);
        if (!strncmp(line, "size=", 5)) {                /* (its size: small, medium, large, huge) */
            int z;
            for (z = 0; z < MAP_SIZES; z++) if (!strncmp(line + 5, map_size_name[z], strlen(map_size_name[z]))) read_size = z;
        }
        if (!strncmp(line, "spawns=", 7)) { const char *q = line + 7; read_spawns = 0; while (*q) { if (*q == ',') read_spawns++; q++; } }
        if (!strcmp(line, "tiles")) break;
    }
    fclose(f);
}

static void add(MapEntry *out, int *n, int max, const char *dir, const char *file, int user)
{
    size_t len = strlen(file);
    int i;
    if (*n >= max || len < 8 || strcmp(file + len - 7, ".ofpmap")) return;
    snprintf(out[*n].path, sizeof out[*n].path, "%s%s", dir, file);
    read_name(out[*n].path, out[*n].name, sizeof out[*n].name, &out[*n].biome, &out[*n].time,
              &out[*n].weather, &out[*n].extras);
    /* (no modes= line: with camps, every mode but every-tank-for-itself; an arena, that one only) */
    out[*n].modes = read_modes >= 0 ? read_modes : read_arena ? 4 : 1 | 2 | 8 | 16;
    out[*n].room = read_arena && read_spawns ? (read_spawns > 8 ? 8 : read_spawns) : 8;   /* (camps: 4 a side) */
    if (read_players >= 2 && read_players < out[*n].room) out[*n].room = read_players;   /* (made for fewer) */
    out[*n].size = read_size;
    out[*n].custom = read_custom;
    out[*n].user = user;
    out[*n].original = strstr(dir, "/maps/classic/") != NULL;
    for (i = 0; i < *n; i++)                          /* imported twice (tool and game): list it once */
        if (!strcmp(out[i].name, out[*n].name) && !strncmp(out[i].name, "Original", 8)) return;
    (*n)++;
}

static void scan_dir(MapEntry *out, int *n, int max, const char *dir, int user)
{
    int first = *n, i, j;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    char pat[1100];
    HANDLE h;
    snprintf(pat, sizeof pat, "%s*.ofpmap", dir);
    h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do add(out, n, max, dir, fd.cFileName, user); while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *d = opendir(dir);
    struct dirent *e;
    if (d) {
        while ((e = readdir(d)) != NULL) add(out, n, max, dir, e->d_name, user);
        closedir(d);
    }
#endif
    for (i = first + 1; i < *n; i++)                 /* by name within each folder */
        for (j = i; j > first && strcmp(out[j - 1].name, out[j].name) > 0; j--) {
            MapEntry t = out[j]; out[j] = out[j - 1]; out[j - 1] = t;
        }
}

int maps_scan(MapEntry *out, int max)
{
    char dir[1100];
    int n = 0;
    snprintf(dir, sizeof dir, "%.1000s/maps/", theme_data_dir());
    scan_dir(out, &n, max, dir, 0);
    snprintf(dir, sizeof dir, "%.1000s/maps/classic/", theme_data_dir());
    scan_dir(out, &n, max, dir, 0);
    scan_dir(out, &n, max, maps_user_dir(), 1);
    {   /* and maps downloaded from the web site's gallery */
        const char *home = getenv("HOME") ? getenv("HOME") : getenv("USERPROFILE");   /* (Windows: USERPROFILE) */
        if (home) {
            snprintf(dir, sizeof dir, "%.1000s/Downloads/", home);
            scan_dir(out, &n, max, dir, 0);
        }
    }
    return n;
}

int maps_find(const MapEntry *maps, int n, const char *path)
{
    int i;
    for (i = 0; path && path[0] && i < n; i++)
        if (!strcmp(maps[i].path, path)) return i;
    return -1;
}
