/* SPDX-License-Identifier: MIT */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/sdl2/theme.h"
#include "core/world.h"
#include "stb_image.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#ifndef OFP_DATA_DIR
#define OFP_DATA_DIR "data"
#endif

static char data_dir[1024];

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int try_data_dir(const char *dir)
{
    char probe[1100];
    if (!dir || !*dir) return 0;
    snprintf(probe, sizeof probe, "%s/themes/default/theme.txt", dir);
    if (!file_exists(probe)) return 0;
    snprintf(data_dir, sizeof data_dir, "%s", dir);
    return 1;
}

const char *theme_data_dir(void)
{
    if (!data_dir[0]) {
        char *base = SDL_GetBasePath();       /* exe dir, or Resources/ in a .app */
        char buf[1100] = "", installed[1100] = "";
        if (base) snprintf(buf, sizeof buf, "%sdata", base);
        if (base) snprintf(installed, sizeof installed, "%s../share/openfirepower", base);   /* bin/../share (packages) */
        if (!try_data_dir(getenv("OFP_DATA")) && !try_data_dir(buf) && !try_data_dir(installed) &&
            !try_data_dir("data") && !try_data_dir(OFP_DATA_DIR))
            snprintf(data_dir, sizeof data_dir, "data");
        SDL_free(base);
    }
    return data_dir;
}

static SDL_Texture *load_png_at(SDL_Renderer *r, const char *path, int quiet)
{
    int w, h, n;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    SDL_Texture *tex;
    if (!px) {
        if (!quiet) fprintf(stderr, "cannot load %s (%s)\n", path, stbi_failure_reason());
        return NULL;
    }
    tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (tex) {
        SDL_UpdateTexture(tex, NULL, px, w * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
    }
    stbi_image_free(px);
    return tex;
}

SDL_Texture *theme_load_data_png(SDL_Renderer *r, const char *file)   /* data/<file>, or NULL */
{
    char path[1300];
    snprintf(path, sizeof path, "%s/%s", theme_data_dir(), file);
    return load_png_at(r, path, 1);
}

static SDL_Texture *load_png(SDL_Renderer *r, const char *theme, const char *file)
{
    char path[1300];
    SDL_Texture *t;
    snprintf(path, sizeof path, "%s/themes/%s/%s", theme_data_dir(), theme, file);
    if ((t = load_png_at(r, path, strcmp(theme, "default") != 0)) != NULL)
        return t;
    snprintf(path, sizeof path, "%s/themes/default/%s", theme_data_dir(), file);
    return load_png_at(r, path, 0);
}

static int hexval(const char *s, Rgb *out)
{
    unsigned v = 0;
    int i;
    for (i = 0; i < 6; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return 0;
    }
    out->r = (Uint8)(v >> 16); out->g = (Uint8)(v >> 8); out->b = (Uint8)v;
    return 1;
}

static void read_theme_txt(const char *theme, HudColors *h, char *title, size_t title_len,
                           char names[3][24])
{
    static const struct { const char *key; size_t off; } keys[] = {
        { "panel",      offsetof(HudColors, panel) },      { "frame",     offsetof(HudColors, frame) },
        { "frame_dark", offsetof(HudColors, frame_dark) }, { "rivet",     offsetof(HudColors, rivet) },
        { "label",      offsetof(HudColors, label) },      { "value",     offsetof(HudColors, value) },
        { "bar_fuel",   offsetof(HudColors, bar_fuel) },   { "bar_armor", offsetof(HudColors, bar_armor) },
        { "team0",      offsetof(HudColors, team0) },      { "team1",     offsetof(HudColors, team1) },
        { "backdrop",   offsetof(HudColors, backdrop) },
    };
    char path[1300], line[256];
    FILE *f;
    snprintf(path, sizeof path, "%s/themes/%s/theme.txt", theme_data_dir(), theme);
    if (!(f = fopen(path, "r")))
        return;
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '='), *nl = strpbrk(line, "\r\n");
        size_t k;
        if (nl) *nl = 0;
        if (!eq) continue;
        *eq = 0;
        if (!strcmp(line, "name") && title)
            snprintf(title, title_len, "%s", eq + 1);
        if (names) {
            if (!strcmp(line, "tank_light"))  snprintf(names[0], 24, "%.23s", eq + 1);
            if (!strcmp(line, "tank_medium")) snprintf(names[1], 24, "%.23s", eq + 1);
            if (!strcmp(line, "tank_heavy"))  snprintf(names[2], 24, "%.23s", eq + 1);
        }
        for (k = 0; k < sizeof keys / sizeof keys[0]; k++)
            if (!strcmp(line, keys[k].key))
                hexval(eq + 1, (Rgb *)((char *)h + keys[k].off));
    }
    fclose(f);
}

int theme_load(SDL_Renderer *r, Theme *t, const char *name)
{
    static const HudColors fallback = {
        { 85, 85, 85 }, { 221, 136, 51 }, { 119, 51, 17 }, { 221, 136, 51 }, { 187, 0, 0 },
        { 255, 255, 0 }, { 136, 136, 136 }, { 51, 170, 0 }, { 221, 136, 51 }, { 51, 170, 0 }, { 0, 0, 0 } };
    Theme nt;
    memset(&nt, 0, sizeof nt);
    snprintf(nt.name, sizeof nt.name, "%s", name);
    snprintf(nt.title, sizeof nt.title, "%s", name);
    nt.hud = fallback;
    read_theme_txt("default", &nt.hud, NULL, 0, NULL);
    read_theme_txt(name, &nt.hud, nt.title, sizeof nt.title, nt.tank_names);
    nt.tanks    = load_png(r, name, "tanks.png");
    nt.cards    = load_png(r, name, "cards.png");
    nt.ui       = load_png(r, name, "ui.png");
    nt.backdrop = load_png(r, name, "backdrop.png");
    nt.men      = load_png(r, name, "men.png");
    nt.heli     = load_png(r, name, "heli.png");
    nt.skull    = load_png(r, name, "skull.png");
    if (!nt.tanks || !nt.cards || !nt.ui || !nt.backdrop || !nt.men || !nt.heli) {
        theme_free(&nt);
        return -1;
    }
    theme_free(t);
    *t = nt;
    return 0;
}

void theme_free(Theme *t)
{
    if (t->tanks) SDL_DestroyTexture(t->tanks);
    if (t->cards) SDL_DestroyTexture(t->cards);
    if (t->ui)    SDL_DestroyTexture(t->ui);
    if (t->backdrop) SDL_DestroyTexture(t->backdrop);
    if (t->men)      SDL_DestroyTexture(t->men);
    if (t->heli)     SDL_DestroyTexture(t->heli);
    if (t->skull)    SDL_DestroyTexture(t->skull);
    t->tanks = t->cards = t->ui = t->backdrop = t->men = t->heli = t->skull = NULL;
}

static const char *probe_file = "theme.txt";

static void add_theme(char names[][64], int *n, int max, const char *root, const char *entry)
{
    char probe[1400];
    size_t len = strlen(entry);
    if (*n >= max || len == 0 || len >= 64 || entry[0] == '.') return;
    snprintf(probe, sizeof probe, "%.1100s/%.63s/%s", root, entry, probe_file);
    if (file_exists(probe)) {
        memcpy(names[*n], entry, len + 1);
        (*n)++;
    }
}

static int rank(const char *s)
{
    /* worlds in this order: Original (faithful), Original (disk), Remaster, the others, then imported looks */
    static const char *first[] = { "default", "retro", "original", "classic", "desert", "jungle", "winter" };
    int i;
    for (i = 0; i < 7; i++)
        if (!strcmp(s, first[i])) return i;
    return 7;
}

static int name_cmp(const void *a, const void *b)
{
    const char *x = (const char *)a, *y = (const char *)b;
    int rx = rank(x), ry = rank(y);
    return rx != ry ? rx - ry : strcmp(x, y);
}

static int list_dir(const char *sub, const char *probe, char names[][64], int max)
{
    char root[1100];
    int n = 0;
    probe_file = probe;
    snprintf(root, sizeof root, "%.1000s/%s", theme_data_dir(), sub);
#ifdef _WIN32
    {
        WIN32_FIND_DATAA fd;
        char pat[1200];
        HANDLE h;
        snprintf(pat, sizeof pat, "%s\\*", root);
        h = FindFirstFileA(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    add_theme(names, &n, max, root, fd.cFileName);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
#else
    {
        DIR *d = opendir(root);
        struct dirent *e;
        if (d) {
            while ((e = readdir(d)) != NULL) {
                add_theme(names, &n, max, root, e->d_name);
            }
            closedir(d);
        }
    }
#endif
    qsort(names, (size_t)n, 64, name_cmp);
    return n;
}


int styles_load(SDL_Renderer *r, Style *styles, int max)
{
    char names[STYLE_MAX][64], path[1300], line[256];
    int n = list_dir("styles", "tiles.png", names, max < STYLE_MAX ? max : STYLE_MAX), i, got = 0;
    for (i = 0; i < n; i++) {
        Style *st = &styles[got];
        FILE *f;
        if (!strcmp(names[i], "classic") || !strcmp(names[i], "scifi")) continue;
        memset(st, 0, sizeof *st);
        snprintf(st->name, sizeof st->name, "%.63s", names[i]);
        snprintf(st->title, sizeof st->title, "%.31s", names[i]);
        snprintf(path, sizeof path, "%.1000s/styles/%.63s/tiles.png", theme_data_dir(), names[i]);
        if (!(st->tiles = load_png_at(r, path, 0))) continue;
        snprintf(path, sizeof path, "%.1000s/styles/%.63s/tanks.png", theme_data_dir(), names[i]);
        st->tanks = load_png_at(r, path, 1);
        snprintf(path, sizeof path, "%.1000s/styles/%.63s/backdrop.png", theme_data_dir(), names[i]);
        st->backdrop = load_png_at(r, path, 1);
        snprintf(path, sizeof path, "%.1000s/styles/%.63s/structures.png", theme_data_dir(), names[i]);
        st->structures = load_png_at(r, path, 1);
        snprintf(path, sizeof path, "%.1000s/styles/%.63s/turrets.png", theme_data_dir(), names[i]);
        st->turrets = load_png_at(r, path, 1);
        snprintf(path, sizeof path, "%.1000s/styles/%.63s/vehicles.png", theme_data_dir(), names[i]);
        st->vehicles = load_png_at(r, path, 1);
        snprintf(path, sizeof path, "%.1000s/styles/%.63s/style.txt", theme_data_dir(), names[i]);
        if ((f = fopen(path, "r")) != NULL) {
            while (fgets(line, sizeof line, f)) {
                char *eq = strchr(line, '='), *nl = strpbrk(line, "\r\n");
                if (nl) *nl = 0;
                if (!eq) continue;
                *eq = 0;
                if (!strcmp(line, "name"))  snprintf(st->title, sizeof st->title, "%.31s", eq + 1);
                if (!strcmp(line, "biome")) st->biome = biome_from_name(eq + 1);
                if (!strcmp(line, "bollards")) st->bollards = atoi(eq + 1);
                if (!strcmp(line, "faithful")) st->faithful = atoi(eq + 1);
                if (!strcmp(line, "team_camp") || !strcmp(line, "team_fortress")) {   /* "r,g,b": that side's colour */
                    int rr, gg, bb, k = !strcmp(line, "team_fortress");
                    if (sscanf(eq + 1, "%d,%d,%d", &rr, &gg, &bb) == 3) {
                        st->team[k][0] = (Uint8)rr; st->team[k][1] = (Uint8)gg; st->team[k][2] = (Uint8)bb;
                        st->has_teams |= 1 << k;
                    }
                }
                if (!strcmp(line, "water")) st->water_fx = !strcmp(eq + 1, "ice") ? WATER_ICE
                                                         : !strcmp(eq + 1, "murky") ? WATER_MURKY : WATER_WAVES;
            }
            fclose(f);
        }
        if (!st->structures) {          /* e.g. imported styles: borrow the biome's */
            snprintf(path, sizeof path, "%.1000s/styles/%s/structures.png", theme_data_dir(),
                     biome_name((Biome)st->biome));
            st->structures = load_png_at(r, path, 1);
        }
        if (!st->vehicles) {                   /* imported styles borrow their biome's */
            snprintf(path, sizeof path, "%.1000s/styles/%s/vehicles.png", theme_data_dir(),
                     biome_name((Biome)st->biome));
            st->vehicles = load_png_at(r, path, 1);
        }
        if (!st->turrets) {
            snprintf(path, sizeof path, "%.1000s/styles/%s/turrets.png", theme_data_dir(),
                     biome_name((Biome)st->biome));
            st->turrets = load_png_at(r, path, 1);
        }
        got++;
    }
    return got;
}

void styles_free(Style *styles, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        if (styles[i].tiles)    SDL_DestroyTexture(styles[i].tiles);
        if (styles[i].structures) SDL_DestroyTexture(styles[i].structures);
        if (styles[i].turrets)    SDL_DestroyTexture(styles[i].turrets);
        if (styles[i].vehicles)   SDL_DestroyTexture(styles[i].vehicles);
        if (styles[i].tanks)    SDL_DestroyTexture(styles[i].tanks);
        if (styles[i].backdrop) SDL_DestroyTexture(styles[i].backdrop);
    }
}
