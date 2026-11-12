/* SPDX-License-Identifier: MIT
 * Bring the original game's levels in straight from YOUR OWN Fire Power
 * disk image (.adf): drop it on the game window, or use OPTIONS -> IMPORT
 * ORIGINAL MAPS. The same conversion as tools/ofp_maps.py, done in the
 * game, so no Python is needed. The maps go to your own maps folder. */
#ifndef OFP_SDL2_ORIGINAL_H
#define OFP_SDL2_ORIGINAL_H

#include <stddef.h>

/* returns how many maps were written (0 = none; msg says why) */
int original_import(const char *adf_path, char *msg, size_t cap);
/* look for a Fire Power .adf in the usual places and import it */
int original_import_search(char *msg, size_t cap);

#endif
