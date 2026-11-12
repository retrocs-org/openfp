/* SPDX-License-Identifier: MIT
 * Immediate-mode drawing helpers: bitmap text, riveted panels, framed boxes,
 * icons, bars. Frame and panel colours come from the active theme. */
#ifndef OFP_SDL2_UI_H
#define OFP_SDL2_UI_H

#include <SDL.h>
#include "platform/sdl2/theme.h"

extern const Rgb UI_BLACK, UI_WHITE, UI_GREY;

int  ui_init(SDL_Renderer *r);
/* the screen's logical size: set it here, and come back to the screen through
 * ui_screen_target() after drawing into a texture. (On a Retina Mac, SDL can lose the
 * scaling when it switches back from a texture, and draws everything at half size.) */
void ui_set_logical(SDL_Renderer *r, int w, int h, int integer_scale);
void ui_screen_target(SDL_Renderer *r);
/* A game view is relative to the logical screen or to the active texture. */
void ui_viewport(SDL_Renderer *r, const SDL_Rect *view);
/* SDL already translates event coordinates in windowed mode. */
void ui_mouse_position(SDL_Window *win, int *x, int *y);
void ui_shutdown(void);
void ui_set_theme(const Theme *t);
void ui_set_backdrop(SDL_Texture *tex);                          /* NULL = theme's */
const HudColors *ui_colors(void);

void ui_avatar(SDL_Renderer *r, int x, int y, int size, int avatar, const char *name);
int  ui_role_badges(SDL_Renderer *r, int right, int y, int roles);
void ui_text(SDL_Renderer *r, int x, int y, int scale, Rgb c, const char *s);
void ui_text_bold(SDL_Renderer *r, int x, int y, int scale, Rgb c, const char *s);
void ui_text_shadow(SDL_Renderer *r, int x, int y, int scale, Rgb c, const char *s);
int  ui_text_width(const char *s, int scale);      /* bold text is 1 px wider */
void ui_fill(SDL_Renderer *r, int x, int y, int w, int h, Rgb c);
void ui_backdrop(SDL_Renderer *r, int x, int y, int w, int h);   /* tiled theme backdrop */
void ui_frame(SDL_Renderer *r, int x, int y, int w, int h);      /* 2 px bevelled frame only */
void ui_box(SDL_Renderer *r, int x, int y, int w, int h);        /* frame, black inside */
void ui_panel(SDL_Renderer *r, int x, int y, int w, int h);      /* framed steel with rivets */
void ui_rivet(SDL_Renderer *r, int x, int y);
void ui_icon(SDL_Renderer *r, int icon, int x, int y, Rgb c);
/* 1 for the first half of every period, 0 for the second: blinking that
 * runs on the clock, the same speed at any frame rate */
int  ui_blink(int period_ms);
void ui_bar(SDL_Renderer *r, int x, int y, int w, int h, int value, int max, Rgb c);
void ui_volume_meter(SDL_Renderer *r, int x, int y, int w, int level, Rgb color);

#endif
