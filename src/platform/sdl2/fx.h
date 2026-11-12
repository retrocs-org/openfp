/* SPDX-License-Identifier: MIT
 * Visual-only particle effects: flashes, fireballs, smoke, sparks, flying
 * debris with shadows, embers, shock rings, scorch marks and screen shake.
 * Driven by the core's Effect list (each effect has a serial number, so
 * particles are spawned exactly once per effect). Floats and rand() are fine
 * here: none of this feeds back into the simulation. */
#ifndef OFP_SDL2_FX_H
#define OFP_SDL2_FX_H

#include <SDL.h>
#include "core/game.h"

void fx_reset(void);
void fx_update(const Game *g, float dt);                    /* once per frame */
void fx_shake(int *dx, int *dy);                            /* camera offset now */
void fx_set_shake(int on);              /* EFFECTS: FULL the view shaking for explosions near you and their
                                         * white flashes; CALM neither (screen shake and flashes off) */
/* layers, drawn by render_view in world space shifted by -cam */
void fx_draw_ground(SDL_Renderer *r, int cam_x, int cam_y);  /* scorch marks, shadows */
void fx_draw_air(SDL_Renderer *r, int cam_x, int cam_y);     /* everything else */
/* light sources among the particles (fireballs, flashes, embers, burning
 * wrecks), handed to cb in screen space; strength 0..255 */
typedef void (*FxLightFn)(void *ctx, float x, float y, float radius, int r, int g, int b, int a);
void fx_lights(FxLightFn cb, void *ctx, int cam_x, int cam_y, int strength);
void fx_set_details(int explosions, int ground_fires);
void fx_set_tracks(int on);             /* tank tracks in the ground (off for the faithful Classic) */

#endif
