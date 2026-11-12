/* SPDX-License-Identifier: MIT */
#ifndef OFP_PREF_PATH_H
#define OFP_PREF_PATH_H

#include <SDL.h>

static inline char *ofp_pref_path(void)
{
    const char *test_dir = SDL_getenv("OFP_TEST_PREF_PATH");
    return test_dir && *test_dir ? SDL_strdup(test_dir) : SDL_GetPrefPath("", "OpenFirePower");
}

#endif
