/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/sdl2/gallery.h"
#include "platform/sdl2/input.h"
#include "platform/sdl2/maps.h"
#include "platform/sdl2/render.h"
#include "platform/sdl2/ui.h"
#include "common/http.h"
#include "core/game.h"

static Game preview;                        /* (the picked map, loaded to be drawn) */
static char map_text[210000];

static void api_url(char *out, size_t cap, const char *listing, const char *query)
{
    snprintf(out, cap, "%.400s%s%s", listing, strchr(listing, '?') ? "&" : "?", query);
}

int gallery_fetch(Gallery *g, const char *listing_url)
{
    static char body[65536];
    char url[600], *line, *next;
    int st;
    g->n = g->sel = g->top = 0;
    g->fetched = 1;
    g->shown = -1;
    api_url(url, sizeof url, listing_url, "action=maps&format=text");
    st = http_request(url, NULL, body, sizeof body, 6000);
    if (st != 200) {
        snprintf(g->status, sizeof g->status, st < 0 ? "THE WEB SITE DOESN'T ANSWER" : "THE WEB SITE SAID %d", st);
        return -1;
    }
    for (line = body; *line && g->n < (int)(sizeof g->e / sizeof g->e[0]); line = next) {
        char *f[7], *p = line;
        int k;
        GalleryEntry *e = &g->e[g->n];
        next = strchr(line, '\n');
        if (next) *next++ = 0; else next = line + strlen(line);
        for (k = 0; k < 7; k++) { f[k] = p; p = strchr(p, '\t'); if (!p) { k++; break; } *p++ = 0; }
        if (k < 7) continue;
        e->id = atoi(f[0]);
        snprintf(e->name, sizeof e->name, "%.47s", f[1]);
        snprintf(e->author, sizeof e->author, "%.19s", f[2]);
        snprintf(e->world, sizeof e->world, "%.15s", f[3]);
        snprintf(e->time, sizeof e->time, "%.7s", f[4]);
        e->size = atoi(f[5]);
        e->downloads = atoi(f[6]);
        if (e->id > 0) g->n++;
    }
    snprintf(g->status, sizeof g->status, g->n ? "%d MAPS" : "NO MAPS SHARED YET", g->n);
    return 0;
}

static int load_preview(Gallery *g, const char *listing_url)    /* the picked map, from the web site */
{
    char url[600], q[64];
    if (g->sel < 0 || g->sel >= g->n) return -1;
    if (g->shown == g->sel) return 0;
    snprintf(q, sizeof q, "action=map&id=%d", g->e[g->sel].id);
    api_url(url, sizeof url, listing_url, q);
    if (http_request(url, NULL, map_text, sizeof map_text, 6000) != 200 || strncmp(map_text, "OFPMAP", 6)) { g->shown = -1; return -1; }
    if (game_init_map(&preview, 1, map_text) != 0) { g->shown = -1; return -1; }
    g->shown = g->sel;
    return 0;
}

static int save_it(Gallery *g, const char *listing_url)        /* into your maps: name.ofpmap */
{
    char url[600], q[64], fname[64];
    FILE *f;
    int i, n = 0;
    snprintf(q, sizeof q, "action=map&id=%d&download=1", g->e[g->sel].id);   /* (it counts the download) */
    api_url(url, sizeof url, listing_url, q);
    if (http_request(url, NULL, map_text, sizeof map_text, 8000) != 200 || strncmp(map_text, "OFPMAP", 6)) {
        snprintf(g->status, sizeof g->status, "THE DOWNLOAD DIDN'T WORK");
        return -1;
    }
    for (i = 0; g->e[g->sel].name[i] && n < 40; i++) {    /* a file name from the map's */
        char ch = g->e[g->sel].name[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) fname[n++] = ch;
        else if (ch >= 'A' && ch <= 'Z') fname[n++] = (char)(ch + 32);
        else if (n && fname[n - 1] != '-') fname[n++] = '-';
    }
    while (n && fname[n - 1] == '-') n--;
    fname[n] = 0;
    snprintf(g->saved_path, sizeof g->saved_path, "%sgallery-%s.ofpmap", maps_user_dir(), n ? fname : "map");
    if (!(f = fopen(g->saved_path, "w"))) { snprintf(g->status, sizeof g->status, "CAN'T WRITE IN YOUR MAPS FOLDER"); return -1; }
    fputs(map_text, f);
    fclose(f);
    snprintf(g->status, sizeof g->status, "SAVED: IT'S UNDER MAP NOW");
    return 0;
}

int gallery_key(Gallery *gl, SDL_Scancode key, const char *listing_url)
{
    switch (key) {
    case SDL_SCANCODE_ESCAPE: return GAL_BACK;
    case SDL_SCANCODE_UP:   if (gl->sel > 0) gl->sel--; break;
    case SDL_SCANCODE_DOWN: if (gl->sel + 1 < gl->n) gl->sel++; break;
    case SDL_SCANCODE_R: case SDL_SCANCODE_F5: gallery_fetch(gl, listing_url); break;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_SPACE: case SDL_SCANCODE_KP_ENTER:
        if (gl->n && save_it(gl, listing_url) == 0) return GAL_SAVED;
        break;
    default: break;
    }
    return GAL_NOTHING;
}

void gallery_draw(SDL_Renderer *r, Gallery *gl, const char *listing_url, int sw, int sh, uint32_t frame)
{
    const HudColors *c = ui_colors();
    int i, y;
    char buf[96];
    ui_backdrop(r, 0, 0, sw, sh);
    ui_fill(r, 6, 6, sw - 12, 22, (Rgb){ 22, 22, 18 });
    ui_frame(r, 6, 6, sw - 12, 22);
    ui_text_bold(r, 14, 9, 1, c->value, "MAP GALLERY");
    ui_text(r, sw - 14 - ui_text_width(gl->status, 1), 9, 1, UI_GREY, gl->status);
    ui_text(r, 14, 18, 1, UI_GREY, "SHARED FROM THE EDITOR (F8) ON THE WEB SITE");
    /* the list */
    ui_fill(r, 6, 32, 176, 166, (Rgb){ 22, 22, 18 });
    ui_frame(r, 6, 32, 176, 166);
    if (gl->sel < gl->top) gl->top = gl->sel;
    if (gl->sel >= gl->top + 15) gl->top = gl->sel - 14;
    for (i = gl->top, y = 38; i < gl->n && i < gl->top + 15; i++, y += 10) {
        const GalleryEntry *e = &gl->e[i];
        int sel = i == gl->sel;
        if (sel) ui_fill(r, 8, y - 1, 172, 10, c->value);
        snprintf(buf, sizeof buf, "%.19s", e->name);
        for (char *q = buf; *q; q++) if (*q >= 'a' && *q <= 'z') *q = (char)(*q - 32);
        ui_text(r, 12, y, 1, sel ? (Rgb){ 20, 18, 10 } : UI_WHITE, buf);
    }
    /* the picked one: a picture and what it is */
    ui_fill(r, 186, 32, sw - 192, 166, (Rgb){ 22, 22, 18 });
    ui_frame(r, 186, 32, sw - 192, 166);
    if (gl->n) {
        const GalleryEntry *e = &gl->e[gl->sel];
        SDL_Rect pic = { 192 + (sw - 204 - 128) / 2, 38, 128, 128 };
        if (load_preview(gl, listing_url) == 0) render_map_preview(r, &preview, &pic, frame);
        else ui_text(r, 196, 96, 1, UI_GREY, "(NO PICTURE)");
        snprintf(buf, sizeof buf, "%.11s %.6s%s%.12s", e->world, e->time, e->author[0] ? ", BY " : "", e->author);
        for (char *q = buf; *q; q++) if (*q >= 'a' && *q <= 'z') *q = (char)(*q - 32);
        ui_text(r, 192, 172, 1, UI_GREY, buf);
        snprintf(buf, sizeof buf, "%d DOWNLOADS", e->downloads);
        ui_text(r, 192, 183, 1, UI_GREY, buf);
    }
    ui_text(r, (sw - ui_text_width((input_pad_count() ? "A: DOWNLOAD   Y: AGAIN   B: BACK" : "FIRE: DOWNLOAD   R: AGAIN   ESC: BACK"), 1)) / 2, sh - 14, 1, UI_GREY,
            (input_pad_count() ? "A: DOWNLOAD   Y: AGAIN   B: BACK" : "FIRE: DOWNLOAD   R: AGAIN   ESC: BACK"));
}
