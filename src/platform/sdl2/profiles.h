/* SPDX-License-Identifier: MIT
 * Profiles and signing in, as on a games console: a player presses FIRE on their own keys
 * or pad, picks their profile (or makes one), and is in. Up to four at a time; everyone
 * signed in plays, locally or online, each with their own device, name and key (their
 * identity on servers and the web site). Holding MINE for a second signs out. Kept in
 * profiles.ini beside the settings. With nobody signed in the game plays as it always
 * did: the keyboard, and the settings' name. */
#ifndef OFP_SDL2_PROFILES_H
#define OFP_SDL2_PROFILES_H

#include <stdint.h>
#include <SDL.h>

#define MAX_PROFILES 16
#define MAX_SIGNED   4

void        signin_init(void);                       /* load the profiles */
void        signin_set_theme(const void *theme);     /* (a Theme: the tank cards in the profile window) */
void        signin_set_tanks(SDL_Texture *tanks, int classic);
/* an event, before anything else sees it: 1 = it was for signing in (joining, the picker,
 * typing a name), and nothing else should act on it. pad: input_handle_event's PAD_* */
int         signin_event(const SDL_Event *ev, int pad);
void        signin_open(int dev);                    /* the profile window for this device (the title screen's first press) */
void        signin_open_own(int dev, int keep_signed_in);                /* a signed-in player's own window: their profile, SIGN OUT, BACK */
int         signin_is_in(int dev);
int         signin_steer(int n);                     /* player n's steering (STEER_TANK, STEER_DIRECT): their profile's */
void signin_record(int n, int won, int kills, int lost, int mode, int cls);
void signin_set_controls(int n, int layout, int fire, int mine, const int keys[6][2]);   /* player n's own (keys: NULL, not those) */
int  signin_pad_layout(int n);
void signin_get_controls(int n, int *layout, int *fire, int *mine, int keys[6][2]);   /* into the KEYS page */
void signin_set_keys(int n, const int keys[6][2]);                       /* player n's pad layout (-2: the machine's) */   /* a match over: player n's stats */
void signin_reset_keys(void);                     /* signed-in keyboard profiles inherit the machine layout again */
void        signin_frame(void);                      /* once a frame: holding MINE to sign out, unplugged pads */
int         signin_busy(void);                       /* the picker is open: it has the input */
int         signin_count(void);
int         signin_device(int i);                    /* DEV_* of the i-th signed-in player */
const char *signin_name(int i);
const uint8_t *signin_key(int i);                    /* 32 bytes: who they are online */
int         signin_avatar(int i);                    /* 0..9: data/avatars.png */
int         signin_tank(int i);                      /* the tank they chose last */
void        signin_set_tank(int i, int tank);        /* (kept for next time) */
void        signin_assign_inputs(void);              /* players 1.. play with their own devices */
void        signin_draw_bar(SDL_Renderer *r, int x, int y, int w);   /* the four slots */
void        signin_draw_avatar(SDL_Renderer *r, int x, int y, int size, int avatar, const char *name);
void        signin_draw_picker(SDL_Renderer *r, int sw, int sh);    /* over everything, when open */
void        signin_test(const char *names);          /* (tests: "ANNA,BERT" signed in on KEYS 1, KEYS 2, PAD 1..) */

#endif
