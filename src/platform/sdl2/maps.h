/* SPDX-License-Identifier: MIT
 * Map files on disk: the ones that come with the game (data/maps), the ones
 * made from your original disk (data/maps/original) and your own, saved by
 * the map editor in the settings folder (…/OpenFirePower/maps). */
#ifndef OFP_SDL2_MAPS_H
#define OFP_SDL2_MAPS_H

#define MAPS_MAX 128   /* bundled maps plus room for player maps */

typedef struct {
    char path[1024];
    char name[64];
    int  user;                       /* 1 = in your own maps folder (the editor may overwrite it) */
    int  biome;                      /* the world it belongs to (Biome), from its biome= line */
    int  custom;                     /* player-built camps */
    int  original;                   /* one of the original game's levels */
    int  room;                       /* tanks it holds: an arena's spawn points, else the camps' garages (7) */
    int  time;                       /* TIME_*, from its time= line */
    int  weather, extras;            /* from its weather= and extras= lines; -1: it doesn't say */
    int  modes;                      /* the modes it's made for: bit 1 << LOCAL GAME's mode (modes= line; arena= too) */
    int  size;                       /* SIZE_* (size= line; none: LARGE) */
} MapEntry;

int   maps_scan(MapEntry *out, int max);               /* sorted: built-in, original, yours */
int   maps_find(const MapEntry *maps, int n, const char *path);   /* index or -1 */
char *maps_read(const char *path);                     /* whole file, malloc'd, NUL-terminated; NULL = failed */
int   maps_write(const char *path, const char *text);  /* 0 = ok */
const char *maps_user_dir(void);                       /* ends in a separator; created on first use */

#endif
