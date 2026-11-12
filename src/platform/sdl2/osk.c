/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include "platform/sdl2/osk.h"
#include "platform/sdl2/input.h"
#include "platform/sdl2/ui.h"

#define COLS 10
static const char *rows_lo[4] = { "1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm.:/" };
static const char *rows_up[4] = { "!?@#&*()+=", "QWERTYUIOP", "ASDFGHJKL_", "ZXCVBNM,;'" };
enum { K_SHIFT, K_SPACE, K_DEL, K_DONE, K_BOTTOM };   /* the bottom row */
static const char *bottom[K_BOTTOM] = { "SHIFT", "SPACE", "DEL", "DONE" };

void osk_open(Osk *k, char *target, size_t cap, const char *title, int secret)
{
    memset(k, 0, sizeof *k);
    k->open = 1;
    k->target = target;
    k->cap = cap;
    k->secret = secret;
    k->row = 1;
    snprintf(k->before, sizeof k->before, "%s", target);
    snprintf(k->title, sizeof k->title, "%s", title);
}

static void type(Osk *k, char ch)
{
    size_t n = strlen(k->target);
    if (n + 1 < k->cap) { k->target[n] = ch; k->target[n + 1] = 0; }
}

int osk_pad(Osk *k, int pad)
{
    if (!k->open) return OSK_CANCEL;
    switch (pad) {
    case PAD_UP:    k->row = (k->row + 4) % 5; break;
    case PAD_DOWN:  k->row = (k->row + 1) % 5; break;
    case PAD_LEFT:  if (k->row == 4) k->col = (k->col + K_BOTTOM - 1) % K_BOTTOM; else k->col = (k->col + COLS - 1) % COLS; break;
    case PAD_RIGHT: if (k->row == 4) k->col = (k->col + 1) % K_BOTTOM; else k->col = (k->col + 1) % COLS; break;
    case PAD_X:     k->shift = !k->shift; break;
    case PAD_B:     { size_t n = strlen(k->target); if (n) k->target[n - 1] = 0; } break;
    case PAD_PAUSE: k->open = 0; return OSK_DONE;
    case PAD_BACK:  snprintf(k->target, k->cap, "%s", k->before); k->open = 0; return OSK_CANCEL;
    case PAD_A:
        if (k->row < 4) type(k, (k->shift ? rows_up : rows_lo)[k->row][k->col]);
        else switch (k->col) {
            case K_SHIFT: k->shift = !k->shift; break;
            case K_SPACE: type(k, ' '); break;
            case K_DEL: { size_t n = strlen(k->target); if (n) k->target[n - 1] = 0; } break;
            default: k->open = 0; return OSK_DONE;
        }
        break;
    default: break;
    }
    if (k->row == 4 && k->col >= K_BOTTOM) k->col = K_BOTTOM - 1;
    return OSK_TYPING;
}

enum { OSK_WIDTH = COLS * 22 + 12, OSK_HEIGHT = 5 * 17 + 34 };

int osk_click(Osk *k, int sw, int sh, int x, int y)
{
    int width = (OSK_WIDTH - 12 - 3 * 4) / K_BOTTOM;
    x -= (sw - OSK_WIDTH) / 2 + 6;
    y -= sh - OSK_HEIGHT - 6 + 30;
    if (x < 0 || y < 0 || y >= 5 * 17 || y % 17 >= 15) return OSK_TYPING;
    if (y < 4 * 17) {
        if (x >= COLS * 22 || x % 22 >= 20) return OSK_TYPING;
        k->col = x / 22;
    } else {
        if (x >= K_BOTTOM * (width + 4) || x % (width + 4) >= width) return OSK_TYPING;
        k->col = x / (width + 4);
    }
    k->row = y / 17;
    return osk_pad(k, PAD_A);
}

void osk_draw(SDL_Renderer *r, const Osk *k, int sw, int sh)
{
    const HudColors *c = ui_colors();
    int w = OSK_WIDTH, h = OSK_HEIGHT, x0 = (sw - w) / 2, y0 = sh - h - 6, i, j;
    char shown[260];
    size_t n;
    if (!k->open) return;
    ui_fill(r, x0, y0, w, h, (Rgb){ 18, 18, 14 });
    ui_frame(r, x0, y0, w, h);
    snprintf(shown, sizeof shown, "%s", k->target);
    if (k->secret) for (n = 0; shown[n]; n++) shown[n] = '*';
    n = strlen(shown);
    ui_text(r, x0 + 6, y0 + 5, 1, UI_GREY, k->title);
    ui_fill(r, x0 + 6, y0 + 15, w - 12, 11, (Rgb){ 40, 38, 30 });
    ui_text(r, x0 + 8, y0 + 17, 1, c->value, n > 34 ? shown + n - 34 : shown);
    for (i = 0; i < 4; i++)
        for (j = 0; j < COLS; j++) {
            int kx = x0 + 6 + j * 22, ky = y0 + 30 + i * 17, sel = k->row == i && k->col == j;
            char ch[2] = { (k->shift ? rows_up : rows_lo)[i][j], 0 };
            ui_fill(r, kx, ky, 20, 15, sel ? c->value : (Rgb){ 50, 48, 40 });
            ui_text_bold(r, kx + 7, ky + 4, 1, sel ? (Rgb){ 20, 18, 10 } : UI_WHITE, ch);
        }
    for (j = 0; j < K_BOTTOM; j++) {
        int kw = (w - 12 - 3 * 4) / K_BOTTOM, kx = x0 + 6 + j * (kw + 4), ky = y0 + 30 + 4 * 17, sel = k->row == 4 && k->col == j;
        ui_fill(r, kx, ky, kw, 15, sel ? c->value : (Rgb){ 50, 48, 40 });
        ui_text_bold(r, kx + (kw - ui_text_width(bottom[j], 1)) / 2, ky + 4, 1,
                     sel ? (Rgb){ 20, 18, 10 } : (j == K_SHIFT && k->shift ? c->value : UI_WHITE), bottom[j]);
    }
}
