/* SPDX-License-Identifier: MIT
 * Two kinds of art, both PNG sheets (layout: docs/THEMES.md):
 *   UI theme   <data>/themes/<name>/  HUD colours, selection pictures, icons,
 *              fallback tanks and backdrop. Missing files come from default.
 *   Map style  <data>/styles/<name>/  the terrain tiles of one kind of map
 *              (classic, desert, jungle, winter, ...), optionally its own tank
 *              camouflage and backdrop. Chosen per game on the selection screen. */
#ifndef OFP_SDL2_THEME_H
#define OFP_SDL2_THEME_H

#include <SDL.h>

#define THEME_TANK_CELL 32
#define THEME_CARD_W    176
#define THEME_CARD_H    104
#define STYLE_MAX       16

enum { ICON_POW, ICON_TANK, ICON_MINE, ICON_FUEL, ICON_ARMOR, ICON_LEFT, ICON_RIGHT, ICON_SPEED,
       ICON_UP, ICON_DOWN };

typedef struct { Uint8 r, g, b; } Rgb;

/* HUD colours, from theme.txt (key=rrggbb); missing keys come from default */
typedef struct {
    Rgb panel, frame, frame_dark, rivet, label, value, bar_fuel, bar_armor, team0, team1, backdrop;
} HudColors;

#define THEME_MAN_CELL  12
#define THEME_HELI_CELL 40

typedef struct {
    SDL_Texture *tanks, *cards, *ui, *backdrop;
    SDL_Texture *men, *heli, *skull;    /* soldiers/POWs, helicopter, respawn animation */
    HudColors hud;
    char name[64];
    char title[64];        /* human-readable, from theme.txt */
    char tank_names[3][24];/* optional per-theme names (tank_light= ...), else "" */
} Theme;

enum { WATER_WAVES, WATER_MURKY, WATER_ICE };
#define STRUCT_SLOT_W 64                /* structures.png: 64x48 slot per type and state */
#define STRUCT_SLOT_H 48

typedef struct {
    SDL_Texture *tiles;
    SDL_Texture *structures;            /* multi-tile buildings; NULL = building tiles */
    SDL_Texture *turrets;               /* rotating gun heads, 32 directions x 6 types */
    SDL_Texture *vehicles;              /* parked vehicles: 6 kinds x (ours, theirs, wreck), 32 dirs */
    SDL_Texture *tanks, *backdrop;      /* NULL = use the UI theme's */
    char name[64], title[32];
    int  biome;                         /* Biome, from style.txt */
    int  bollards;                      /* draw posts on the sand beside walls */
    int  faithful;                      /* "Classic": the original's maps, players and plain looks */
    int  water_fx;                      /* WATER_WAVES, WATER_MURKY or WATER_ICE */
    int  has_teams;                     /* team_camp=, team_fortress=: the sides' colours (its tanks') */
    Uint8 team[2][3];
} Style;

const char *theme_data_dir(void);
SDL_Texture *theme_load_data_png(SDL_Renderer *r, const char *file);   /* a PNG in data/ (NULL: not there) */                       /* resolved once */
int  theme_load(SDL_Renderer *r, Theme *t, const char *name);
void theme_free(Theme *t);

/* Load every style in <data>/styles; built-in ones first. Returns the count. */
int  styles_load(SDL_Renderer *r, Style *styles, int max);
void styles_free(Style *styles, int n);

#endif
