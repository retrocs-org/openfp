/* SPDX-License-Identifier: MIT */
#ifndef OFP_SOUNDTRACK_H
#define OFP_SOUNDTRACK_H

#include "core/world.h"

/* BIOME_SCIFI retains the original synth/module fallback. */
enum { SOUNDTRACK_MENU = BIOME_COUNT, SOUNDTRACK_CREDITS, SOUNDTRACK_COUNT };

#define OFP_SOUNDTRACK(X) \
    X(BIOME_CLASSIC, "classic.mp3", "Classic", "Shattered Reality", "TheMightyRager") \
    X(BIOME_DESERT, "desert.mp3", "Desert", "Desert Wasteland", "tcarisland") \
    X(BIOME_JUNGLE, "jungle.mp3", "Jungle", "Chill Jungle Ambient", "Tausdei") \
    X(BIOME_WINTER, "winter.ogg", "Winter", "Cold Grounds", "Anipatok") \
    X(BIOME_VOLCANIC, "volcanic.wav", "Volcano", "Entering the Volcano", "Umplix") \
    X(BIOME_WASTELAND, "wasteland.mp3", "Wasteland", "Space Graveyard", "TinyWorlds") \
    X(BIOME_MOON, "moon.ogg", "Moon Base", "Crashed Ship", "Ted Kerr (Wolfgang_)") \
    X(BIOME_SWAMP, "swamp.ogg", "Swamp", "Swamp Environment Audio", "LokiF") \
    X(SOUNDTRACK_MENU, "menu.ogg", "Main Menu", "Commando Team (Action)", "Gregoire Lourme / Iwan Gabovitch") \
    X(SOUNDTRACK_CREDITS, "credits.mp3", "Credits", "Legion's Rally", "Li-Hsin Huang (Improbable Cadence)")

#endif
