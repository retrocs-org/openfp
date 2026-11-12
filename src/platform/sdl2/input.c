/* SPDX-License-Identifier: MIT */
#include <string.h>
#include "platform/sdl2/input.h"

static int stick_deadzone = 12000;
#define STICK_DEADZONE stick_deadzone
#define TRIGGER_PRESSED   8000

static SDL_GameController *pads[MAX_PADS];
static int centers[MAX_PADS][2];
static int stick_dir[MAX_PADS][2];
void input_set_deadzone(int percent) {
    if (percent < 12) percent = 12;
    if (percent > 50) percent = 50;
    stick_deadzone = percent * 32767 / 100;
}
int input_snapshot(int dev, InputSnapshot *out) {
    int i = dev - DEV_PAD1, k;
    memset(out, 0, sizeof *out);
    if (i < 0 || i >= MAX_PADS || !pads[i]) return 0;
    for (k = 0; k < 6; k++) out->axis[k] = SDL_GameControllerGetAxis(pads[i], (SDL_GameControllerAxis)k);
    for (k = 0; k < SDL_CONTROLLER_BUTTON_MAX && k < 32; k++)
        if (SDL_GameControllerGetButton(pads[i], (SDL_GameControllerButton)k)) out->buttons |= 1u << k;
    return 1;
}
int input_calibrate(int dev) {
    InputSnapshot snap; int i = dev - DEV_PAD1;
    if (!input_snapshot(dev, &snap) || snap.axis[0] < -8000 || snap.axis[0] > 8000 ||
        snap.axis[1] < -8000 || snap.axis[1] > 8000) return 0;
    centers[i][0] = snap.axis[0]; centers[i][1] = snap.axis[1];
    stick_dir[i][0] = stick_dir[i][1] = 0; return 1;
}
void input_reset_calibration(void) { memset(centers, 0, sizeof centers); memset(stick_dir, 0, sizeof stick_dir); }
static int stick_axis(int i, SDL_GameControllerAxis axis) {
    return (int)SDL_GameControllerGetAxis(pads[i], axis) - centers[i][axis == SDL_CONTROLLER_AXIS_LEFTY];
}
/* Keyboard layout per player: [player][up, down, left, right, fire, mine][main, spare]. */
const int input_default_keys[4][6][2] = {
    { { SDL_SCANCODE_UP, 0 }, { SDL_SCANCODE_DOWN, 0 }, { SDL_SCANCODE_LEFT, 0 }, { SDL_SCANCODE_RIGHT, 0 },
      { SDL_SCANCODE_PERIOD, 0 }, { SDL_SCANCODE_COMMA, 0 } },
    { { SDL_SCANCODE_W, 0 }, { SDL_SCANCODE_S, 0 }, { SDL_SCANCODE_A, 0 }, { SDL_SCANCODE_D, 0 },
      { SDL_SCANCODE_SPACE, SDL_SCANCODE_LALT }, { SDL_SCANCODE_LSHIFT, SDL_SCANCODE_E } },
};

static int keymap[4][6][2];
static int machine_keys[4][6][2];            /* (the machine's sets: what a profile's keys go back to) */
static int have_machine_keys;                /* (none set yet: the built-in ones are the machine's) */
void input_set_keys(const void *keys) { memcpy(keymap, keys, sizeof keymap); memcpy(machine_keys, keys, sizeof machine_keys); have_machine_keys = 1; }
void input_set_keyset(int set, const int keys[6][2])   /* one set (a profile's own keys) */
{
    if (set >= 0 && set < 4) memcpy(keymap[set], keys, sizeof keymap[set]);
}

#define STICK_PUSH    (stick_deadzone + 2000 > 16000 ? stick_deadzone + 2000 : 16000)                        /* the stick as a d-pad: pushed this far, it's a press */
#define STICK_RELEASE (stick_deadzone * 2 / 3)                        /* (and back within this, ready for the next) */
static int pad_fire = -1, pad_mine = -1;     /* SDL_CONTROLLER_BUTTON_*, or -1: as ever */
static int pad_layout = PAD_TRIGGERS;        /* a pad nobody signed in with: the triggers drive */
/* each pad's own (a signed-in profile's): -2 = the machine's */
static int own_fire[MAX_PADS] = { -2, -2, -2, -2 }, own_mine[MAX_PADS] = { -2, -2, -2, -2 }, own_layout[MAX_PADS] = { -2, -2, -2, -2 };
void input_set_pad_buttons(int fire, int mine) { pad_fire = fire; pad_mine = mine; }
void input_set_pad_layout(int layout) { pad_layout = layout; }
void input_set_dev_pad(int dev, int layout, int fire, int mine)   /* a profile's pad: its layout and buttons (-2: the machine's) */
{
    int i = dev - DEV_PAD1;
    if (i < 0 || i >= MAX_PADS) return;
    own_layout[i] = layout; own_fire[i] = fire; own_mine[i] = mine;
}
void input_reset_controls(void)              /* every pad and key set: the machine's again */
{
    int i;
    if (have_machine_keys) memcpy(keymap, machine_keys, sizeof keymap);
    else memcpy(keymap, input_default_keys, sizeof keymap);
    for (i = 0; i < MAX_PADS; i++) own_layout[i] = own_fire[i] = own_mine[i] = -2;
}

int input_dev_pad_layout(int dev)
{
    int i = dev - DEV_PAD1;
    return i >= 0 && i < MAX_PADS && own_layout[i] != -2 ? own_layout[i] : pad_layout;
}

static const InputBits key_action[6] = { IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_FIRE, IN_MINE };

static InputBits key_bits(const Uint8 *k, int player)
{
    InputBits in = 0;
    int a, j;
    for (a = 0; a < 6; a++)
        for (j = 0; j < 2; j++)
            if (keymap[player][a][j] > 0 && keymap[player][a][j] < SDL_NUM_SCANCODES && k[keymap[player][a][j]])
                in |= key_action[a];
    return in;
}

InputBits input_device_key(int dev, SDL_Scancode scancode)
{
    InputBits in = 0;
    int a;
    if (dev < DEV_KEYS1 || dev >= DEV_PAD1 || scancode <= SDL_SCANCODE_UNKNOWN) return 0;
    for (a = 0; a < 6; a++)
        if (keymap[dev][a][0] == (int)scancode || keymap[dev][a][1] == (int)scancode)
            in |= key_action[a];
    return in;
}
int input_pad_count(void);

void input_hints(void)
{
    /* talk to Xbox and PlayStation pads directly (USB and Bluetooth) for full
     * features: rumble, DualSense lightbar, correct buttons */
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
#ifdef SDL_HINT_JOYSTICK_HIDAPI_PS4
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4, "1");
#endif
#ifdef SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
#endif
#ifdef SDL_HINT_JOYSTICK_HIDAPI_PS5
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");
#endif
#ifdef SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");
#endif
#ifdef SDL_HINT_JOYSTICK_HIDAPI_XBOX
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_XBOX, "1");
#endif
#ifdef SDL_HINT_JOYSTICK_MFI
    SDL_SetHint(SDL_HINT_JOYSTICK_MFI, "1");          /* macOS: GameController.framework */
#endif
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
}

static void light_up(SDL_GameController *p)
{
#if SDL_VERSION_ATLEAST(2, 0, 14)
    SDL_GameControllerSetLED(p, 221, 136, 51);       /* DualSense / DS4 lightbar: our colour */
#else
    (void)p;
#endif
}

static void open_pad(int device)
{
    int i;
    SDL_GameController *p;
    if (!SDL_IsGameController(device)) return;
    for (i = 0; i < MAX_PADS; i++)                   /* already open? */
        if (pads[i] && SDL_JoystickGetDeviceInstanceID(device) ==
                       SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])))
            return;
    for (i = 0; i < MAX_PADS; i++)
        if (!pads[i]) {
            if ((p = SDL_GameControllerOpen(device)) != NULL) {
                pads[i] = p;
                light_up(p);
                SDL_Log("controller connected: %s", SDL_GameControllerName(p));
            }
            return;
        }
}

void input_init(const char *mapping_file)
{
    int i;
    input_reset_controls();
    if (mapping_file) {
        int n = SDL_GameControllerAddMappingsFromFile(mapping_file);
        if (n < 0) SDL_Log("no extra controller mappings (%s)", SDL_GetError());
    }
    for (i = 0; i < SDL_NumJoysticks(); i++)
        open_pad(i);
}

void input_shutdown(void)
{
    int i;
    for (i = 0; i < MAX_PADS; i++)
        if (pads[i]) { SDL_GameControllerClose(pads[i]); pads[i] = NULL; }
}

int input_handle_event(const SDL_Event *ev)
{
    int i;
    switch (ev->type) {
    case SDL_CONTROLLERDEVICEADDED:
        open_pad(ev->cdevice.which);
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
        for (i = 0; i < MAX_PADS; i++)
            if (pads[i] && ev->cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i]))) {
                SDL_GameControllerClose(pads[i]);
                pads[i] = NULL;
                centers[i][0] = centers[i][1] = 0;
                stick_dir[i][0] = stick_dir[i][1] = 0;
            }
        break;
    case SDL_CONTROLLERAXISMOTION:                       /* the left stick: the d-pad's events too (one per push) */
        if (ev->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || ev->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            int k = ev->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY, v = ev->caxis.value, pi;
            for (pi = 0; pi < MAX_PADS; pi++)
                if (pads[pi] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[pi])) == ev->caxis.which) break;
            if (pi == MAX_PADS) break;
            v -= centers[pi][k];
            if (stick_dir[pi][k] && v > -STICK_RELEASE && v < STICK_RELEASE) stick_dir[pi][k] = 0;   /* back near the middle */
            else if (!stick_dir[pi][k] && (v <= -STICK_PUSH || v >= STICK_PUSH)) {
                stick_dir[pi][k] = v < 0 ? -1 : 1;
                return k ? (v < 0 ? PAD_UP : PAD_DOWN) : (v < 0 ? PAD_LEFT : PAD_RIGHT);
            }
        }
        break;
    case SDL_CONTROLLERBUTTONDOWN:
        switch (ev->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_START: return PAD_PAUSE;    /* Menu / Options */
        case SDL_CONTROLLER_BUTTON_BACK:  return PAD_BACK;     /* View / Create */
        case SDL_CONTROLLER_BUTTON_Y:     return PAD_ZOOM;     /* Y / Triangle */
        case SDL_CONTROLLER_BUTTON_A:     return PAD_A;
        case SDL_CONTROLLER_BUTTON_B:     return PAD_B;
        case SDL_CONTROLLER_BUTTON_X:     return PAD_X;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return PAD_LB;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return PAD_RB;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:    return PAD_UP;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  return PAD_DOWN;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  return PAD_LEFT;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return PAD_RIGHT;
        default: break;
        }
        break;
    default:
        break;
    }
    return PAD_NONE;
}

/* a pad's state as the game's buttons. CLASSIC: the stick (or d-pad) drives, A/RB or RT fire, B/X or LT
 * mine. TRIGGERS: the stick (or d-pad) turns, RT drives forward, LT back; A/RB fire, B/X mine. (The
 * menus have the stick's ups and downs as events, either way.) */
static InputBits pad_bits_i(int i)
{
    SDL_GameController *p = pads[i];
    InputBits in = 0;
    int lx = stick_axis(i, SDL_CONTROLLER_AXIS_LEFTX);
    int ly = stick_axis(i, SDL_CONTROLLER_AXIS_LEFTY);
    int layout = own_layout[i] != -2 ? own_layout[i] : pad_layout;
    if (layout < 0 || layout >= PAD_LAYOUTS) layout = PAD_TRIGGERS;   /* (an old saved one: the triggers) */
    int fire = own_fire[i] != -2 ? own_fire[i] : pad_fire, mine = own_mine[i] != -2 ? own_mine[i] : pad_mine;
    int rt = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > TRIGGER_PRESSED;
    int lt = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > TRIGGER_PRESSED;
    int rb = SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER), lb = SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    if (layout == PAD_TRIGGERS) {                        /* the triggers drive */
        if (rt || SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_UP)) in |= IN_UP;
        if (lt || SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) in |= IN_DOWN;
        rt = lt = 0;                                     /* (not fire and mine, then) */
    } else {
        if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_UP)    || ly < -STICK_DEADZONE) in |= IN_UP;
        if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_DOWN)  || ly >  STICK_DEADZONE) in |= IN_DOWN;
    }
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_LEFT)  || lx < -STICK_DEADZONE) in |= IN_LEFT;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || lx >  STICK_DEADZONE) in |= IN_RIGHT;
    if (layout == PAD_TRIGGERS) {                        /* the triggers drive: A (Cross) or RB fires, X (Square) or LB lays mines */
        if (fire < 0 ? SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_A) || rb : SDL_GameControllerGetButton(p, (SDL_GameControllerButton)fire))
            in |= IN_FIRE;
        if (mine < 0 ? SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_X) || lb : SDL_GameControllerGetButton(p, (SDL_GameControllerButton)mine))
            in |= IN_MINE;
        return in;
    }
    if ((fire < 0 ? SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_A) || rb
                  : SDL_GameControllerGetButton(p, (SDL_GameControllerButton)fire)) || rt) in |= IN_FIRE;
    if ((mine < 0 ? SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_B) ||
                    SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_X)
                  : SDL_GameControllerGetButton(p, (SDL_GameControllerButton)mine)) || lt) in |= IN_MINE;
    return in;
}

/* a pad in the menus (and the screens around them), the same whatever it's set to for playing: the
 * d-pad and left stick choose, A or RB is OK (IN_FIRE), B is back (IN_MINE) */
static InputBits pad_menu_bits_i(int i)
{
    SDL_GameController *p = pads[i];
    InputBits in = 0;
    int lx = stick_axis(i, SDL_CONTROLLER_AXIS_LEFTX), ly = stick_axis(i, SDL_CONTROLLER_AXIS_LEFTY);
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_UP)    || ly < -STICK_DEADZONE) in |= IN_UP;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_DOWN)  || ly >  STICK_DEADZONE) in |= IN_DOWN;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_LEFT)  || lx < -STICK_DEADZONE) in |= IN_LEFT;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || lx >  STICK_DEADZONE) in |= IN_RIGHT;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_A) || SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) in |= IN_FIRE;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_B)) in |= IN_MINE;
    return in;
}

static InputBits pad_bits(SDL_GameController *p)
{
    int i;
    for (i = 0; i < MAX_PADS; i++) if (pads[i] == p) return pad_bits_i(i);
    return 0;
}

/* the n-th connected pad, or NULL */
static SDL_GameController *nth_pad(int n)
{
    int i;
    for (i = 0; i < MAX_PADS; i++)
        if (pads[i] && n-- == 0) return pads[i];
    return NULL;
}

/* which pad belongs to a player: with a pad for everyone, pad n is player
 * n; with fewer, the pads go to the last players, since players 1 and 2
 * can share the keyboard */
static SDL_GameController *pad_of(int player, int players)
{
    int npads = input_pad_count();
    if (npads >= players) return nth_pad(player);
    if (player >= players - npads) return nth_pad(player - (players - npads));
    return NULL;
}

/* ---- devices ---------------------------------------------------------------------------- */
static int assigned[4], n_assigned;

void input_assign(const int *devs, int n)
{
    int i;
    n_assigned = n < 0 ? 0 : n > 4 ? 4 : n;
    for (i = 0; i < n_assigned; i++) assigned[i] = devs[i];
}

/* B on a pad: back, in every menu and on every screen, whatever its buttons are set to (unless its
 * FIRE is B: then it's FIRE). Keys: 0 (Escape is theirs) */
int input_device_back(int dev)
{
    int i = dev - DEV_PAD1;
    if (dev < DEV_PAD1 || dev >= DEV_COUNT || !pads[i]) return 0;
    return SDL_GameControllerGetButton(pads[i], SDL_CONTROLLER_BUTTON_B);   /* (always: whatever FIRE is set to) */
}

/* the pads' right sticks (the strongest push) and triggers (any pressed): the map editor's scroll and tools */
void input_pads_extra(int *rx, int *ry, int *lt, int *rt)
{
    int i;
    *rx = *ry = *lt = *rt = 0;
    for (i = 0; i < MAX_PADS; i++) {
        int x, y;
        if (!pads[i]) continue;
        x = SDL_GameControllerGetAxis(pads[i], SDL_CONTROLLER_AXIS_RIGHTX);
        y = SDL_GameControllerGetAxis(pads[i], SDL_CONTROLLER_AXIS_RIGHTY);
        if (abs(x) > abs(*rx)) *rx = x;
        if (abs(y) > abs(*ry)) *ry = y;
        if (SDL_GameControllerGetAxis(pads[i], SDL_CONTROLLER_AXIS_TRIGGERLEFT) > TRIGGER_PRESSED) *lt = 1;
        if (SDL_GameControllerGetAxis(pads[i], SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > TRIGGER_PRESSED) *rt = 1;
    }
}

InputBits input_device_menu(int dev)
{
    if (dev >= DEV_KEYS1 && dev < DEV_PAD1) return key_bits(SDL_GetKeyboardState(NULL), dev);
    if (dev >= DEV_PAD1 && dev < DEV_COUNT && pads[dev - DEV_PAD1]) return pad_menu_bits_i(dev - DEV_PAD1);
    return 0;
}

InputBits input_player1_menu(void)
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    InputBits in = key_bits(k, DEV_KEYS1);
    int i;
    if (k[SDL_SCANCODE_M] || k[SDL_SCANCODE_X]) in |= IN_MINE;
    if (k[SDL_SCANCODE_RALT] || k[SDL_SCANCODE_KP_0]) in |= IN_FIRE;
    for (i = 0; i < MAX_PADS; i++) if (pads[i]) in |= pad_menu_bits_i(i);
    return in;
}

InputBits input_device(int dev)
{
    if (dev >= DEV_KEYS1 && dev < DEV_PAD1) return key_bits(SDL_GetKeyboardState(NULL), dev);
    if (dev >= DEV_PAD1 && dev < DEV_COUNT && pads[dev - DEV_PAD1]) return pad_bits(pads[dev - DEV_PAD1]);
    return 0;
}

int input_device_present(int dev)
{
    if (dev >= DEV_KEYS1 && dev < DEV_PAD1) {           /* a key set: when it has keys */
        int a, j;
        for (a = 0; a < 6; a++) for (j = 0; j < 2; j++) if (keymap[dev][a][j] > 0) return 1;
        return 0;
    }
    return dev >= DEV_PAD1 && dev < DEV_COUNT && pads[dev - DEV_PAD1] != NULL;
}

const char *input_device_name(int dev)
{
    static const char *n[DEV_COUNT] = { "KEYS 1", "KEYS 2", "KEYS 3", "KEYS 4", "PAD 1", "PAD 2", "PAD 3", "PAD 4" };
    return dev >= 0 && dev < DEV_COUNT ? n[dev] : "?";
}

const char *input_controller_model(int dev)
{
    int i = dev - DEV_PAD1;
    const char *name = i >= 0 && i < MAX_PADS && pads[i] ? SDL_GameControllerName(pads[i]) : NULL;
    return name ? name : "NOT CONNECTED";
}

int input_device_of_event(const SDL_Event *ev)
{
    int i, a, j;
    if (ev->type == SDL_KEYDOWN || ev->type == SDL_KEYUP) {
        for (i = 0; i < 4; i++)
            for (a = 0; a < 6; a++)
                for (j = 0; j < 2; j++)
                    if (keymap[i][a][j] > 0 && keymap[i][a][j] == (int)ev->key.keysym.scancode) return DEV_KEYS1 + i;
        return -1;
    }
    if (ev->type == SDL_CONTROLLERBUTTONDOWN || ev->type == SDL_CONTROLLERBUTTONUP || ev->type == SDL_CONTROLLERAXISMOTION) {
        SDL_JoystickID which = ev->type == SDL_CONTROLLERAXISMOTION ? ev->caxis.which : ev->cbutton.which;
        for (i = 0; i < MAX_PADS && i < 4; i++)
            if (pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == which) return DEV_PAD1 + i;
    }
    return -1;
}

int input_device_is_fire(int dev, const SDL_Event *ev)
{
    if (ev->type == SDL_KEYDOWN && !ev->key.repeat && dev >= DEV_KEYS1 && dev < DEV_PAD1) {
        SDL_Scancode sc = ev->key.keysym.scancode;
        if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) return 0;   /* (Enter: the menu's OK) */
        return (input_device_key(dev, sc) & IN_FIRE) != 0;
    }
    if (ev->type == SDL_CONTROLLERBUTTONDOWN && dev >= DEV_PAD1 && input_device_of_event(ev) == dev) {
        int b = ev->cbutton.button;
        return b == SDL_CONTROLLER_BUTTON_A || b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;   /* (signing in: A, as in every menu) */
    }
    return 0;
}

const char *input_join_key_name(int dev)
{
    int j;
    if (dev < DEV_KEYS1 || dev >= DEV_PAD1) return NULL;
    for (j = 0; j < 2; j++) {
        int key = keymap[dev][4][j];
        if (key > 0 && key < SDL_NUM_SCANCODES && key != SDL_SCANCODE_RETURN && key != SDL_SCANCODE_KP_ENTER &&
            SDL_GetScancodeName((SDL_Scancode)key)[0])
            return SDL_GetScancodeName((SDL_Scancode)key);
    }
    return NULL;
}

InputBits input_player(int player, int players)
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    InputBits in = 0;
    SDL_GameController *p;
    if (n_assigned && player < n_assigned) return input_device(assigned[player]);   /* (signed in: their own) */
    if (players < 2) return input_player1();
    if (player < 2) in = key_bits(k, player);      /* players 1 and 2 share the keyboard */
    if ((p = pad_of(player, players)) != NULL) in |= pad_bits(p);
    return in;
}

void input_rumble_player(int player, int players, int low, int high, int ms)
{
    SDL_GameController *p;
    if (n_assigned && player < n_assigned) {             /* (signed in: their own pad, if it is one) */
        int d = assigned[player];
        p = d >= DEV_PAD1 && d < DEV_COUNT ? pads[d - DEV_PAD1] : NULL;
    } else if (players < 2) { input_rumble(low, high, ms); return; }
    else p = pad_of(player, players);
#if SDL_VERSION_ATLEAST(2, 0, 9)
    if (p) SDL_GameControllerRumble(p, (Uint16)(low > 65535 ? 65535 : low), (Uint16)(high > 65535 ? 65535 : high), (Uint32)ms);
#else
    (void)p; (void)low; (void)high; (void)ms;
#endif
}

InputBits input_player1(void)
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    InputBits in = key_bits(k, 0) | key_bits(k, 1) | key_bits(k, 2) | key_bits(k, 3);   /* alone, every set drives */
    int i;
    if (k[SDL_SCANCODE_M] || k[SDL_SCANCODE_X]) in |= IN_MINE;
    if (k[SDL_SCANCODE_RALT] || k[SDL_SCANCODE_KP_0]) in |= IN_FIRE;

    for (i = 0; i < MAX_PADS; i++)                   /* fire: A / Cross, R1 / RB, right trigger; */
        if (pads[i]) in |= pad_bits(pads[i]);         /* mine: B / Circle, X / Square, left trigger */
    return in;
}

void input_rumble(int low, int high, int ms)
{
#if SDL_VERSION_ATLEAST(2, 0, 9)
    int i;
    for (i = 0; i < MAX_PADS; i++)
        if (pads[i])
            SDL_GameControllerRumble(pads[i], (Uint16)(low > 65535 ? 65535 : low),
                                     (Uint16)(high > 65535 ? 65535 : high), (Uint32)ms);
#else
    (void)low; (void)high; (void)ms;
#endif
}

int input_pad_count(void)
{
    int i, n = 0;
    for (i = 0; i < MAX_PADS; i++) n += pads[i] != NULL;
    return n;
}

const char *input_pad_name(void)
{
    int i;
    for (i = 0; i < MAX_PADS; i++)
        if (pads[i]) return SDL_GameControllerName(pads[i]);
    return NULL;
}

/* rumble on a pad (strength 0..100, for ms); nothing on keys, or a pad without it, or when it's off */
static int rumble_on = 1;
void input_set_rumble(int on) { rumble_on = on; }
void input_rumble_dev(int dev, int strength, int ms)
{
    SDL_GameController *p;
    Uint16 lo, hi;
    if (!rumble_on || dev < DEV_PAD1 || dev - DEV_PAD1 >= MAX_PADS || !(p = pads[dev - DEV_PAD1])) return;
    strength = strength < 0 ? 0 : strength > 100 ? 100 : strength;
    lo = (Uint16)(strength * 655);                       /* (the heavy motor: the knocks) */
    hi = (Uint16)(strength * 400);                       /* (the light one: a buzz on top) */
    SDL_GameControllerRumble(p, lo, hi, (Uint32)ms);
}
