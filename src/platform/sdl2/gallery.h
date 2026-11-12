/* SPDX-License-Identifier: MIT
 * The web site's map gallery inside the game: the maps players shared, a picture of the
 * one picked, FIRE to download it into your maps (and play it). */
#ifndef OFP_SDL2_GALLERY_H
#define OFP_SDL2_GALLERY_H

#include <SDL.h>

typedef struct {
    int  id, size, downloads;
    char name[48], author[20], world[16], time[8];
} GalleryEntry;

typedef struct {
    GalleryEntry e[120];
    int  n, sel, top, fetched, shown;    /* shown: the map in the picture (index; -1 none) */
    char status[96];
    char saved_path[1100];               /* the last one downloaded */
} Gallery;

enum { GAL_NOTHING, GAL_BACK, GAL_SAVED };

int  gallery_fetch(Gallery *g, const char *listing_url);   /* 0 = ok */
int  gallery_key(Gallery *gl, SDL_Scancode key, const char *listing_url);   /* GAL_* */
void gallery_draw(SDL_Renderer *r, Gallery *gl, const char *listing_url, int sw, int sh, uint32_t frame);

#endif
