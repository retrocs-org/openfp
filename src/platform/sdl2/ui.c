/* SPDX-License-Identifier: MIT */
#include <string.h>
#include "common/roles.h"
#include "common/volume.h"
#include "platform/sdl2/ui.h"
#include "font8x8_basic.h"          /* public domain, third_party/ */

const Rgb UI_BLACK = {   0,   0,   0 };
const Rgb UI_WHITE = { 250, 250, 250 };
const Rgb UI_GREY  = { 170, 170, 170 };

int ui_role_badges(SDL_Renderer *r, int right, int y, int roles)
{
    static const Rgb colors[ROLE_BADGE_COUNT] = { {255, 209, 86}, {104, 210, 247}, {247, 138, 184} };
    int x = right;
    for (int i = ROLE_BADGE_COUNT - 1; i >= 0; i--) {
        if (!(roles & (1 << i))) continue;
        x -= ROLE_BADGE_STEP;
        ui_fill(r, x, y, ROLE_BADGE_SIZE, ROLE_BADGE_SIZE, (Rgb){ 16, 18, 23 });
        for (int row = 0; row < 5; row++)
            for (int col = 0; col < 5; col++)
                if (role_badge_row(i, row) & (16u >> col))
                    ui_fill(r, x + 1 + col, y + 1 + row, 1, 1, colors[i]);
    }
    return right - x;
}

static SDL_Texture *font;           /* 16x8 grid of 8x8 white glyphs, ASCII 0..127 */
static const Theme *theme;
static SDL_Texture *backdrop_override;

#define AVATARS 10
#define AVATAR_PX 24
static const Rgb avatar_rgb[AVATARS] = {             /* (no picture: a badge in this colour, the initial on it) */
    { 214, 72, 60 }, { 232, 160, 52 }, { 226, 210, 80 }, { 96, 176, 74 }, { 64, 170, 180 },
    { 76, 116, 214 }, { 160, 92, 200 }, { 200, 200, 200 }, { 200, 90, 140 }, { 120, 120, 90 } };

/* an avatar, size x size: its portrait (or, without the picture, a badge with the initial) */
void ui_avatar(SDL_Renderer *r, int x, int y, int size, int avatar, const char *name)
{
    static SDL_Texture *avatar_tex;
    static int avatar_tried;
    int a = (avatar % AVATARS + AVATARS) % AVATARS;
    if (!avatar_tried) { avatar_tried = 1; avatar_tex = theme_load_data_png(r, "avatars.png"); }
    if (avatar_tex) {
        SDL_Rect src = { a * AVATAR_PX, 0, AVATAR_PX, AVATAR_PX }, dst = { x, y, size, size };
        ui_fill(r, x, y, size, size, (Rgb){ 34, 34, 28 });
        SDL_RenderCopy(r, avatar_tex, &src, &dst);
    } else {
        char ini[2] = { name && name[0] ? name[0] : '?', 0 };
        if (ini[0] >= 'a' && ini[0] <= 'z') ini[0] -= 'a' - 'A';
        ui_fill(r, x, y, size, size, avatar_rgb[a]);
        ui_text(r, x + (size - 8) / 2 + 1, y + (size - 8) / 2 + 1, 1, (Rgb){ 16, 14, 10 }, ini);
    }
}

int ui_init(SDL_Renderer *r)
{
    static Uint32 px[128 * 64];
    int ch, x, y;
    for (ch = 0; ch < 128; ch++)
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x++) {
                int on = ((unsigned char)font8x8_basic[ch][y] >> x) & 1;     /* bit 0 = leftmost */
                px[((ch / 16) * 8 + y) * 128 + (ch % 16) * 8 + x] = on ? 0xFFFFFFFFu : 0;
            }
    font = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 128, 64);
    if (!font) return -1;
    SDL_UpdateTexture(font, NULL, px, 128 * 4);
    SDL_SetTextureBlendMode(font, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(font, SDL_ScaleModeNearest);
    return 0;
}

void ui_shutdown(void)
{
    if (font) SDL_DestroyTexture(font);
    font = NULL;
}

void ui_set_theme(const Theme *t) { theme = t; }
void ui_set_backdrop(SDL_Texture *tex) { backdrop_override = tex; }
const HudColors *ui_colors(void)  { return &theme->hud; }

int ui_text_width(const char *s, int scale) { return (int)strlen(s) * 8 * scale; }

void ui_text(SDL_Renderer *r, int x, int y, int scale, Rgb c, const char *s)
{
    SDL_SetTextureColorMod(font, c.r, c.g, c.b);
    for (; *s; s++, x += 8 * scale) {
        int ch = (unsigned char)*s & 127;
        SDL_Rect src = { (ch % 16) * 8, (ch / 16) * 8, 8, 8 };
        SDL_Rect dst = { x, y, 8 * scale, 8 * scale };
        SDL_RenderCopy(r, font, &src, &dst);
    }
}

/* heavier look closer to the stencil lettering of the period */
void ui_text_bold(SDL_Renderer *r, int x, int y, int scale, Rgb c, const char *s)
{
    ui_text(r, x, y, scale, c, s);
    ui_text(r, x + scale, y, scale, c, s);
}

void ui_text_shadow(SDL_Renderer *r, int x, int y, int scale, Rgb c, const char *s)
{
    ui_text_bold(r, x + scale, y + scale, scale, UI_BLACK, s);
    ui_text_bold(r, x, y, scale, c, s);
}

void ui_fill(SDL_Renderer *r, int x, int y, int w, int h, Rgb c)
{
    SDL_Rect rc = { x, y, w, h };
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    SDL_RenderFillRect(r, &rc);
}

void ui_backdrop(SDL_Renderer *r, int x, int y, int w, int h)
{
    int tw, th, tx, ty;
    SDL_Rect clip = { x, y, w, h };
    SDL_Texture *bd = backdrop_override ? backdrop_override : theme->backdrop;
    SDL_QueryTexture(bd, NULL, NULL, &tw, &th);
    SDL_RenderSetClipRect(r, &clip);
    for (ty = y; ty < y + h; ty += th)
        for (tx = x; tx < x + w; tx += tw) {
            SDL_Rect dst = { tx, ty, tw, th };
            SDL_RenderCopy(r, bd, NULL, &dst);
        }
    SDL_RenderSetClipRect(r, NULL);
}

static Rgb lighten(Rgb c, int d)
{
    Rgb o;
    o.r = (Uint8)(c.r + d > 255 ? 255 : c.r + d);
    o.g = (Uint8)(c.g + d > 255 ? 255 : c.g + d);
    o.b = (Uint8)(c.b + d > 255 ? 255 : c.b + d);
    return o;
}

void ui_frame(SDL_Renderer *r, int x, int y, int w, int h)
{
    const HudColors *c = &theme->hud;
    ui_fill(r, x, y, w, 1, lighten(c->frame, 40));          /* top/left light */
    ui_fill(r, x, y, 1, h, lighten(c->frame, 40));
    ui_fill(r, x + 1, y + h - 1, w - 1, 1, c->frame_dark);  /* bottom/right dark */
    ui_fill(r, x + w - 1, y + 1, 1, h - 1, c->frame_dark);
    ui_fill(r, x + 1, y + 1, w - 2, 1, c->frame);
    ui_fill(r, x + 1, y + 1, 1, h - 2, c->frame);
    ui_fill(r, x + 1, y + h - 2, w - 2, 1, c->frame);
    ui_fill(r, x + w - 2, y + 1, 1, h - 2, c->frame);
}

void ui_box(SDL_Renderer *r, int x, int y, int w, int h)
{
    ui_frame(r, x, y, w, h);
    ui_fill(r, x + 2, y + 2, w - 4, h - 4, UI_BLACK);
}

void ui_rivet(SDL_Renderer *r, int x, int y)
{
    const HudColors *c = &theme->hud;
    ui_fill(r, x, y, 3, 3, c->rivet);
    ui_fill(r, x, y, 2, 1, lighten(c->rivet, 50));
    ui_fill(r, x + 2, y + 1, 1, 2, c->frame_dark);
    ui_fill(r, x + 1, y + 2, 1, 1, c->frame_dark);
}

void ui_panel(SDL_Renderer *r, int x, int y, int w, int h)
{
    int ry;
    ui_frame(r, x, y, w, h);
    ui_fill(r, x + 2, y + 2, w - 4, h - 4, theme->hud.panel);
    for (ry = y + 6; ry + 3 < y + h - 3; ry += 24) {
        ui_rivet(r, x + 4, ry);
        ui_rivet(r, x + w - 7, ry);
    }
}

void ui_icon(SDL_Renderer *r, int icon, int x, int y, Rgb c)
{
    SDL_Rect src = { icon * 8, 0, 8, 8 }, dst = { x, y, 8, 8 };
    SDL_SetTextureColorMod(theme->ui, c.r, c.g, c.b);
    SDL_RenderCopy(r, theme->ui, &src, &dst);
}

void ui_bar(SDL_Renderer *r, int x, int y, int w, int h, int value, int max, Rgb c)
{
    int fill = max > 0 ? (int)((long)w * value / max) : 0;
    Rgb track = { 24, 24, 24 };
    if (value > 0 && fill == 0) fill = 1;
    ui_fill(r, x, y, w, h, track);
    if (fill) {
        ui_fill(r, x, y, fill, h, c);
        ui_fill(r, x, y, fill, 1, lighten(c, 60));
    }
}

void ui_volume_meter(SDL_Renderer *r, int x, int y, int w, int level, Rgb color)
{
    int i;
    ui_box(r, x, y, w, 10);
    for (i = 0; i < VOLUME_MAX; i++) {
        int left = 3 + i * (w - 4) / VOLUME_MAX;
        int right = 1 + (i + 1) * (w - 4) / VOLUME_MAX;
        ui_fill(r, x + left, y + 3, right - left, 4, i < level ? color : (Rgb){ 58, 58, 50 });
    }
}

int ui_blink(int period_ms)
{
    if (period_ms < 2) return 1;
    return (int)((SDL_GetTicks() % (Uint32)period_ms) < (Uint32)period_ms / 2);
}

static int logical_w, logical_h, logical_int = 1, screen_fill;
static SDL_Rect screen_viewport;

void ui_set_logical(SDL_Renderer *r, int w, int h, int integer_scale)
{
    logical_w = w; logical_h = h; logical_int = integer_scale;
    ui_screen_target(r);
}

void ui_screen_target(SDL_Renderer *r)
{
    SDL_SetRenderTarget(r, NULL);
    screen_fill = (SDL_GetWindowFlags(SDL_RenderGetWindow(r)) & SDL_WINDOW_FULLSCREEN) != 0;
    if (screen_fill && logical_w && logical_h) {
        int w, h;
        /* Fullscreen fills both axes. The viewport of a render texture must
         * never become the origin or scale of the window. */
        SDL_RenderSetLogicalSize(r, 0, 0);
        SDL_RenderSetViewport(r, NULL);
        if (SDL_GetRendererOutputSize(r, &w, &h) == 0 && w > 0 && h > 0)
            SDL_RenderSetScale(r, (float)w / logical_w, (float)h / logical_h);
    } else if (logical_w) {
        SDL_RenderSetLogicalSize(r, logical_w, logical_h);
        SDL_RenderSetIntegerScale(r, logical_int ? SDL_TRUE : SDL_FALSE);
    }
    SDL_RenderGetViewport(r, &screen_viewport);
    SDL_RenderSetClipRect(r, NULL);
}

void ui_viewport(SDL_Renderer *r, const SDL_Rect *view)
{
    SDL_Rect dst = *view;
    if (!SDL_GetRenderTarget(r)) {
        dst.x += screen_viewport.x;
        dst.y += screen_viewport.y;
    }
    SDL_RenderSetViewport(r, &dst);
}

void ui_mouse_position(SDL_Window *win, int *x, int *y)
{
    int w, h;
    if (!screen_fill) return;
    /* Events are in window units, including on displays with HiDPI output. */
    SDL_GetWindowSize(win, &w, &h);
    if (w > 0 && h > 0) {
        *x = *x * logical_w / w;
        *y = *y * logical_h / h;
    }
}
