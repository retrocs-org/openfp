/* SPDX-License-Identifier: MIT */
#ifndef OFP_SDL2_RENDER_H
#define OFP_SDL2_RENDER_H

#include <SDL.h>
#include "core/game.h"
#include "platform/sdl2/theme.h"

int  render_init(SDL_Renderer *r);
void render_shutdown(void);
void render_tank_icon(SDL_Renderer *r, SDL_Texture *tanks, int pick, int team, int x, int y, int size);
/* World into a viewport (logical px) following tank `follow`, drawn with
 * the map style's tiles (and its tanks, if the style has its own).
 * weather_effects: -1 to omit weather, 0 to omit flashes, 1 for both. */
void render_view(SDL_Renderer *r, const Theme *t, const Style *style, const Game *g,
                 const SDL_Rect *view, int follow, int alpha256, int weather_effects);
/* Which tile types join up visually (0 = none); shared with previews. */
int  render_connect_group(int type);
/* night: the scene goes dark and only lights show it (N key, --night) */
void render_set_night(int on);
void render_set_radar_zoom(int z);      /* 1 = nearby map, 2 = nearby tank dots (R key) */
/* the sides' colours: 0 blue and red; 1 blue and orange, with a shape over every tank
 * (a ring ours, a triangle theirs) and crosses for enemies on the radar */
void render_set_sides(int mode);
int  render_sides(void);
Rgb  render_side_rgb(int team);
void render_set_style_teams(const Style *st);   /* the world's sides' colours (its tanks'); NULL: blue and red */         /* a side's colour, as set */
void render_set_ground_fires(int on);
void render_set_red_marks(int on);   /* battle marks: the soldiers' red ones too (1) or not (0) */
int  render_radar_zoom(void);
enum { RADAR_SIZE = 56 };
/* tank = -1 previews the radar at the map's spawn point. */
void render_draw_radar(SDL_Renderer *r, const Game *g, int tank, int x, int y, uint32_t frame);
int  render_night(void);
/* Side panel for one player: score, POWs, lives, mines, fuel, armour, radar. */
void render_player_tag(SDL_Renderer *r, int x, int y, int player, int team);
/* the whole map with flags and tanks, fitted into a view */
void render_overview(SDL_Renderer *r, const Game *g, const SDL_Rect *view, uint32_t frame);
/* the same in a printed map's colours, for choosing a map */
void render_map_preview(SDL_Renderer *r, const Game *g, const SDL_Rect *view, uint32_t frame);
/* compact panel for split screen, 188 x 64; who = "P1" / "P2" */
void render_hud_small(SDL_Renderer *r, const Theme *th, const Game *g, int tank, int x, int y,
                      uint32_t frame, const char *who);
/* watching: a camera of its own (on: centred on world pixel x, y; render_overview shows it all) */
void render_free_camera(int on, int x, int y);
void render_hud(SDL_Renderer *r, const Theme *t, const Game *g, int tank, int x, int y,
                uint32_t frame);

void render_weather(SDL_Renderer *r, const SDL_Rect *view, uint32_t tick, int weather, int time, int flashes);

#endif
