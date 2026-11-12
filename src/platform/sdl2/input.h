/* SPDX-License-Identifier: MIT
 * Keyboard and game controllers. SDL's game-controller layer covers Xbox
 * (One / Series, USB or Bluetooth) and PlayStation (DualShock 4, DualSense)
 * pads on macOS, Linux and Windows; data/gamecontrollerdb.txt adds mappings
 * for hundreds of others. Each signed-in player owns a separate input device. */
#ifndef OFP_SDL2_INPUT_H
#define OFP_SDL2_INPUT_H

#include <SDL.h>
#include "core/game.h"

#define MAX_PADS 4

/* ---- devices: what a signed-in player plays with ------------------------------------------
 * 0..3: the keyboard's four key sets (3 and 4 once they have keys); 4..7: the first to fourth pad */
enum { DEV_KEYS1, DEV_KEYS2, DEV_KEYS3, DEV_KEYS4, DEV_PAD1, DEV_COUNT = DEV_PAD1 + 4 };
InputBits   input_device(int dev);                 /* its buttons now */
int       input_device_back(int dev);
InputBits input_device_menu(int dev);         /* its buttons in the menus: a pad's d-pad/stick, A (OK), B (back), whatever it's set to */
InputBits input_player1_menu(void);          /* before sign-in: KEYS 1 and the pads */
InputBits input_device_key(int dev, SDL_Scancode scancode);   /* actions bound to this key on the device */
void      input_pads_extra(int *rx, int *ry, int *lt, int *rt);   /* right sticks, triggers (the map editor) */           /* (nobody signed in: every key set and pad, the menus' way) */   /* a pad's B: back, whatever its buttons are set to (not if it's its FIRE) */
void        input_rumble_dev(int dev, int strength, int ms);   /* a device's rumble: strength 0..100 (keys: nothing) */
void        input_set_rumble(int on);
int         input_device_present(int dev);         /* (keys: always; a pad: plugged in) */
const char *input_device_name(int dev);            /* "KEYS 1", "PAD 2" */
const char *input_controller_model(int dev);
const char *input_join_key_name(int dev);          /* first keyboard Fire binding used for sign-in */
int         input_device_is_fire(int dev, const SDL_Event *ev);   /* is this event its FIRE going down? (not Enter) */
int         input_device_of_event(const SDL_Event *ev);           /* the device a key or button event came from, or -1 */
/* players 0..n-1 play with these devices (input_player follows it); n = 0: as before */
void        input_assign(const int *devs, int n);
/* the pad's fire and mine buttons (SDL_CONTROLLER_BUTTON_*; -1: as ever, A or RB and B or X) */
void input_set_pad_buttons(int fire, int mine);
enum { PAD_CLASSIC, PAD_TRIGGERS, PAD_LAYOUTS };
/* the triggers drive (the default: RT forward, LT back, the stick only turns; A or RB fires, X or LB
 * lays mines; on a PlayStation pad Cross and Square); or the stick does (A or RB, RT fire; B or X, LT mines) */
void input_set_pad_layout(int layout);              /* the machine's */
void input_set_dev_pad(int dev, int layout, int fire, int mine);   /* a profile's own pad (-2: the machine's) */
int  input_dev_pad_layout(int dev);
void input_reset_controls(void);                     /* every pad and key set back to the machine's */
void input_set_keyset(int set, const int keys[6][2]);      /* (6: ACT_COUNT) a profile's own keys, for one set */
enum { PAD_NONE, PAD_PAUSE, PAD_BACK, PAD_ZOOM,     /* menu-ish buttons, from events */
       PAD_A, PAD_B, PAD_X, PAD_LB, PAD_RB, PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT };   /* (lobby, keyboard) */

void      input_hints(void);                  /* before SDL_Init */
typedef struct { int axis[6]; unsigned buttons; } InputSnapshot;
int input_snapshot(int dev, InputSnapshot *out);
int input_calibrate(int dev); /* sample a released stick; reject a deliberate push */
void input_reset_calibration(void);
void input_set_deadzone(int percent);
void      input_init(const char *mapping_file);
void      input_shutdown(void);
int       input_handle_event(const SDL_Event *ev);  /* hotplug; returns PAD_* for menu buttons */
InputBits input_player1(void);                /* keyboard + all controllers */
/* Two players (players == 2). By default KEYS 1 uses arrows, period to fire
 * and comma for mines. KEYS 2 uses WASD, Space/Left Alt and Left Shift/E.
 * Players 3 and 4 need pads. With a pad for everyone, pad n is player n;
 * with fewer, the pads go to the last players first (2 players, 1 pad: it's
 * player 2's; 4 players, 2 pads: players 3 and 4). players == 1 is input_player1. */
InputBits input_player(int player, int players);
/* the keyboard layout: [player 1/2][up, down, left, right, fire, mine][main, spare scancode] */
extern const int input_default_keys[4][6][2];
void      input_set_keys(const void *keys);    /* int [4][6][2], as in Settings */
void      input_rumble(int low, int high, int ms);  /* 0..65535 */
void      input_rumble_player(int player, int players, int low, int high, int ms);
int       input_pad_count(void);
const char *input_pad_name(void);             /* first pad, or NULL */

#endif
