/* SPDX-License-Identifier: MIT
 * An on-screen keyboard, for typing with a game pad (a Steam Deck, the sofa): names, the
 * server's address, a password, the chat. The d-pad moves, A types, B deletes, X is shift,
 * Start is done, Back cancels. */
#ifndef OFP_SDL2_OSK_H
#define OFP_SDL2_OSK_H

#include <SDL.h>
#include <stddef.h>

typedef struct {
    int    open, row, col, shift, secret;
    char  *target;                   /* what's being typed into */
    size_t cap;
    char   before[256];              /* (for cancelling) */
    char   title[32];
} Osk;

enum { OSK_TYPING, OSK_DONE, OSK_CANCEL };

void osk_open(Osk *k, char *target, size_t cap, const char *title, int secret);
int  osk_pad(Osk *k, int pad);       /* a PAD_* event; returns OSK_* */
int  osk_click(Osk *k, int sw, int sh, int x, int y);
void osk_draw(SDL_Renderer *r, const Osk *k, int sw, int sh);

#endif
