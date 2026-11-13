/* SPDX-License-Identifier: MIT */
#ifndef OFP_SOUNDTRACK_H
#define OFP_SOUNDTRACK_H

#include <string.h>
#include "core/world.h"

/* BIOME_SCIFI retains the original synth/module fallback. */
enum { SOUNDTRACK_MENU = BIOME_COUNT, SOUNDTRACK_CREDITS, SOUNDTRACK_CANNON_TUBE, SOUNDTRACK_SNOW_THEME,
       SOUNDTRACK_SPACE_ATMOSPHERE, SOUNDTRACK_NEW_BEGINNING, SOUNDTRACK_LOST_DESERT, SOUNDTRACK_DESERT_DREAMS, SOUNDTRACK_URBAN_JUNGLE, SOUNDTRACK_JUNGLE_PRO_SENSORY, SOUNDTRACK_IRON_WASTELAND, SOUNDTRACK_COUNT };

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
    X(SOUNDTRACK_CREDITS, "credits.mp3", "Credits", "Legion's Rally", "Li-Hsin Huang (Improbable Cadence)") \
    X(SOUNDTRACK_CANNON_TUBE, "cannon-tube.ogg", "Wasteland", "Cannon Tube", "Gundatsch") \
    X(SOUNDTRACK_SNOW_THEME, "snow-theme.ogg", "Winter", "Through the Snow", "Cleyton Kauffman") \
    X(SOUNDTRACK_SPACE_ATMOSPHERE, "space-atmosphere.mp3", "Moon Base", "Space Atmosphere", "Alexandr Zhelanov") \
    X(SOUNDTRACK_NEW_BEGINNING, "new-beginning.ogg", "Wasteland", "A New Beginning", "remaxim") \
    X(SOUNDTRACK_LOST_DESERT, "lost-in-the-desert.mp3", "Desert", "Lost in the Desert", "Eldritch Grim") \
    X(SOUNDTRACK_DESERT_DREAMS, "desert-of-dreams-mix.ogg", "Desert", "Desert of Dreams Mix", "Insydnis / Tausdei / glitchart") \
    X(SOUNDTRACK_URBAN_JUNGLE, "urban-jungle-2061.mp3", "Jungle", "Urban Jungle 2061", "Eric Matyas / Soundimage.org") \
    X(SOUNDTRACK_JUNGLE_PRO_SENSORY, "jungle-pro-sensory.wav", "Jungle", "Jungle", "Alex McCulloch (Pro Sensory)") \
    X(SOUNDTRACK_IRON_WASTELAND, "iron-wasteland.ogg", "Volcano", "Iron Wasteland", "Alexandr Zhelanov")

/* A map stores a world-local choice. File keys remain stable when tracks are added. */
static const struct { int world, song; const char *key; } soundtrack_choices[] = {
    { BIOME_CLASSIC, BIOME_CLASSIC, "classic" },
    { BIOME_DESERT, BIOME_DESERT, "desert" },
    { BIOME_JUNGLE, BIOME_JUNGLE, "jungle" },
    { BIOME_WINTER, BIOME_WINTER, "winter" },
    { BIOME_SCIFI, BIOME_SCIFI, "scifi" },
    { BIOME_VOLCANIC, BIOME_VOLCANIC, "volcanic" },
    { BIOME_WASTELAND, BIOME_WASTELAND, "wasteland" },
    { BIOME_MOON, BIOME_MOON, "moon" },
    { BIOME_SWAMP, BIOME_SWAMP, "swamp" },
    { BIOME_WASTELAND, SOUNDTRACK_CANNON_TUBE, "cannon-tube" },
    { BIOME_WINTER, SOUNDTRACK_SNOW_THEME, "snow-theme" },
    { BIOME_MOON, SOUNDTRACK_SPACE_ATMOSPHERE, "space-atmosphere" },
    { BIOME_WASTELAND, SOUNDTRACK_NEW_BEGINNING, "new-beginning" },
    { BIOME_DESERT, SOUNDTRACK_LOST_DESERT, "lost-in-the-desert" },
    { BIOME_DESERT, SOUNDTRACK_DESERT_DREAMS, "desert-of-dreams-mix" },
    { BIOME_JUNGLE, SOUNDTRACK_URBAN_JUNGLE, "urban-jungle-2061" },
    { BIOME_JUNGLE, SOUNDTRACK_JUNGLE_PRO_SENSORY, "jungle-pro-sensory" },
    { BIOME_VOLCANIC, SOUNDTRACK_IRON_WASTELAND, "iron-wasteland" }
};
#define SOUNDTRACK_CHOICES ((int)(sizeof soundtrack_choices / sizeof *soundtrack_choices))

static inline int soundtrack_count(int world)
{
    int i, n = 0;
    for (i = 0; i < SOUNDTRACK_CHOICES; i++) n += soundtrack_choices[i].world == world;
    return n;
}

static inline int soundtrack_choice_index(int world, int choice)
{
    int i;
    for (i = 0; i < SOUNDTRACK_CHOICES; i++)
        if (soundtrack_choices[i].world == world && choice-- == 0) return i;
    return -1;
}

static inline int soundtrack_for_map(int world, int choice)
{
    int i = soundtrack_choice_index(world, choice);
    return i >= 0 ? soundtrack_choices[i].song : world;
}

static inline const char *soundtrack_key(int world, int choice)
{
    int i = soundtrack_choice_index(world, choice);
    return i >= 0 ? soundtrack_choices[i].key : "default";
}

static inline int soundtrack_choice(int world, const char *key)
{
    int i, choice = 0;
    for (i = 0; i < SOUNDTRACK_CHOICES; i++) if (soundtrack_choices[i].world == world) {
        if (!strcmp(soundtrack_choices[i].key, key)) return choice;
        choice++;
    }
    return 0;
}

static inline int soundtrack_step(int world, int choice, int direction)
{
    int count = soundtrack_count(world);
    return count ? ((choice % count + direction % count + count) % count) : 0;
}

static inline const char *soundtrack_title(int song)
{
    switch (song) {
#define TITLE(id, file, world, title, author) case id: return title;
        OFP_SOUNDTRACK(TITLE)
#undef TITLE
    default: return "DEFAULT";
    }
}

#endif
