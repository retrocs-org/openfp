/* SPDX-License-Identifier: MIT
 * The profile window with a pad: the left stick and the d-pad both move in the list and on
 * the on-screen keyboard (a virtual pad, the real sign-in code). */
#include "platform/sdl2/pref_path.h"
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "platform/sdl2/input.h"
#include "platform/sdl2/profiles.h"
static SDL_Joystick *j;
static void pump(int ms) { SDL_Event ev; Uint32 until = SDL_GetTicks() + ms;
    do { SDL_Delay(5); while (SDL_PollEvent(&ev)) { int pad = input_handle_event(&ev); signin_event(&ev, pad); } signin_frame(); } while (SDL_GetTicks() < until); }
static void button(int b) { SDL_JoystickSetVirtualButton(j, b, 1); pump(40); SDL_JoystickSetVirtualButton(j, b, 0); pump(40); }
static void stick(int axis, int v) { SDL_JoystickSetVirtualAxis(j, axis, (Sint16)v); pump(40); SDL_JoystickSetVirtualAxis(j, axis, 0); pump(40); }
static void sign_out(void) { SDL_JoystickSetVirtualButton(j, SDL_CONTROLLER_BUTTON_B, 1); pump(1150); SDL_JoystickSetVirtualButton(j, SDL_CONTROLLER_BUTTON_B, 0); pump(40); }
static void make_one(int use_stick, char *out) {   /* NEW PROFILE: one key to the right on the keyboard, typed, done */
    button(SDL_CONTROLLER_BUTTON_A);                                  /* the window */
    button(SDL_CONTROLLER_BUTTON_DPAD_DOWN); button(SDL_CONTROLLER_BUTTON_DPAD_DOWN);   /* ANNA, BERT, NEW PROFILE */
    button(SDL_CONTROLLER_BUTTON_A);                                  /* the on-screen keyboard */
    if (use_stick) stick(SDL_CONTROLLER_AXIS_LEFTX, 30000); else button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    button(SDL_CONTROLLER_BUTTON_A);                                  /* type that key */
    button(SDL_CONTROLLER_BUTTON_START);                              /* done */
    button(SDL_CONTROLLER_BUTTON_A);                                  /* the tank and avatar page: as it is, done */
    snprintf(out, 32, "%s", signin_name(0));
}
int main(void) {
    SDL_VirtualJoystickDesc d; int ok = 1, idx; char a[32], b[32];
    SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);
    {   /* two profiles to start with (and nothing else): ANNA, BERT */
        char *pref = ofp_pref_path(), path[1100];
        FILE *f;
        snprintf(path, sizeof path, "%sprofiles.ini", pref ? pref : "");
        SDL_free(pref);
        if ((f = fopen(path, "w")) != NULL) {
            fprintf(f, "ANNA\t%064d\t1\t0\nBERT\t%063d1\t2\t1\n", 0, 0);
            fclose(f);
        }
    }
    input_init(NULL); signin_init();
    SDL_zero(d); d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION; d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    d.naxes = SDL_CONTROLLER_AXIS_MAX; d.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    idx = SDL_JoystickAttachVirtualEx(&d); j = SDL_JoystickOpen(idx); pump(60);
    button(SDL_CONTROLLER_BUTTON_A); stick(SDL_CONTROLLER_AXIS_LEFTY, 30000); button(SDL_CONTROLLER_BUTTON_A);
    printf("the list, the stick down: %s\n", signin_name(0)); ok &= !strcmp(signin_name(0), "BERT"); sign_out();
    button(SDL_CONTROLLER_BUTTON_A); button(SDL_CONTROLLER_BUTTON_DPAD_DOWN); button(SDL_CONTROLLER_BUTTON_A);
    printf("the list, the d-pad down: %s\n", signin_name(0)); ok &= !strcmp(signin_name(0), "BERT"); sign_out();
    make_one(1, a); sign_out(); make_one(0, b);
    printf("the on-screen keyboard: with the stick \"%s\", with the d-pad \"%s\"\n", a, b);
    ok &= a[0] && !strcmp(a, b) && strcmp(a, "PLAYER 1") != 0;
    sign_out();
    {   /* the tank and avatar page: right (the next tank), down (the avatars), right (the next one), done */
        int first;
        button(SDL_CONTROLLER_BUTTON_A); button(SDL_CONTROLLER_BUTTON_DPAD_DOWN); button(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        button(SDL_CONTROLLER_BUTTON_DPAD_DOWN); button(SDL_CONTROLLER_BUTTON_A); button(SDL_CONTROLLER_BUTTON_START);
        first = 3;                                        /* (the fourth profile: ANNA, BERT, PLAYER 1W before it) */
        stick(SDL_CONTROLLER_AXIS_LEFTX, 30000);          /* the tank: BISON -> MAMMOTH */
        stick(SDL_CONTROLLER_AXIS_LEFTY, 30000);          /* down: the avatars */
        button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);         /* the next avatar */
        button(SDL_CONTROLLER_BUTTON_A);
        printf("the tank and avatar page: %s, tank %d, avatar %d\n", signin_name(0), signin_tank(0), signin_avatar(0));
        ok &= signin_count() == 1 && signin_tank(0) == 2 && signin_avatar(0) == first + 1;
    }
    {   /* START's window: player 1 (the pad) signs out, and player 2 becomes player 1 */
        int pd = DEV_PAD1, guard = 0;
        while (signin_count() > 0 && guard++ < 4) sign_out();          /* (start with nobody in) */
        button(SDL_CONTROLLER_BUTTON_A);                                /* the pad: the window */
        button(SDL_CONTROLLER_BUTTON_A);                                /* ANNA */
        signin_test("BERT");                                            /* BERT on the keys: player 2 */
        printf("in: %d, player 1 %s (pad %s), player 2 %s\n", signin_count(), signin_name(0), signin_device(0) == pd ? "yes" : "NO", signin_name(1));
        ok &= signin_count() == 2 && signin_device(0) == pd;
        {   /* her own window: CONTROLLER (its page: DRIVE to THE STICK, saved; DEFAULTS), and FACE */
            int k2, face0;
            signin_open_own(pd, 0);                                        /* Starts on Back. */
            for (k2 = 0; k2 < 6; k2++) button(SDL_CONTROLLER_BUTTON_DPAD_UP);   /* up to CONTROLLER */
            button(SDL_CONTROLLER_BUTTON_A);                            /* its page: on PAD: DRIVE */
            button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);                   /* RT/LT -> THE STICK */
            printf("ANNA's CONTROLLER, DRIVE right: her layout %s\n", signin_pad_layout(0) == PAD_CLASSIC ? "THE STICK (saved)" : "WRONG");
            ok &= signin_pad_layout(0) == PAD_CLASSIC;
            for (k2 = 0; k2 < 3; k2++) button(SDL_CONTROLLER_BUTTON_DPAD_DOWN);  /* to DEFAULTS */
            button(SDL_CONTROLLER_BUTTON_A);
            printf("DEFAULTS: her layout %s\n", signin_pad_layout(0) == -2 ? "the default again" : "WRONG");
            ok &= signin_pad_layout(0) == -2;
            button(SDL_CONTROLLER_BUTTON_B);                            /* back to the window (on CONTROLLER) */
            face0 = signin_avatar(0);
            for (k2 = 0; k2 < 3; k2++) button(SDL_CONTROLLER_BUTTON_DPAD_DOWN);   /* to FACE */
            button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
            printf("FACE, right: face %d -> %d\n", face0, signin_avatar(0)); ok &= signin_avatar(0) != face0;
            {
                int old_tank = signin_tank(0);
                signin_set_tanks(NULL, 0);
                signin_set_tank(0, TANK_HEAVY);
                button(SDL_CONTROLLER_BUTTON_DPAD_DOWN);              /* Favorite Tank */
                button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
                ok &= signin_tank(0) == TANK_RHINO;
                signin_set_tanks(NULL, 1);
                button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
                ok &= signin_tank(0) == TANK_LIGHT;
                signin_set_tank(0, old_tank);
                button(SDL_CONTROLLER_BUTTON_DPAD_UP);                /* Face again. */
            }
            {   /* STEERING: two up from FACE; right: the original's, saved */
                button(SDL_CONTROLLER_BUTTON_DPAD_UP); button(SDL_CONTROLLER_BUTTON_DPAD_UP);
                button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
                printf("STEERING, right: %s\n", signin_steer(0) == 1 ? "ORIGINAL (saved)" : "WRONG"); ok &= signin_steer(0) == 1;
                button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);               /* (back to TANK) */
                button(SDL_CONTROLLER_BUTTON_DPAD_DOWN); button(SDL_CONTROLLER_BUTTON_DPAD_DOWN);   /* (FACE again) */
            }
            {   /* NAME: the on-screen keyboard with her name; a letter more, START: renamed */
                char before[32];
                snprintf(before, sizeof before, "%s", signin_name(0));
                button(SDL_CONTROLLER_BUTTON_DPAD_UP);                  /* to NAME */
                button(SDL_CONTROLLER_BUTTON_A);                        /* the keyboard */
                button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT); button(SDL_CONTROLLER_BUTTON_A);   /* a letter */
                button(SDL_CONTROLLER_BUTTON_START);                    /* done */
                printf("NAME: %s -> %s (window %s)\n", before, signin_name(0), signin_busy() ? "open" : "CLOSED");
                ok &= strcmp(before, signin_name(0)) != 0 && signin_busy();
            }
            button(SDL_CONTROLLER_BUTTON_B);                            /* (closed) */
            printf("B: the window %s\n", signin_busy() ? "STILL OPEN" : "closed"); ok &= !signin_busy();
        }
        signin_open_own(pd, 0);                                            /* START on the pad: its own window */
        button(SDL_CONTROLLER_BUTTON_DPAD_UP);                          /* BACK to SIGN OUT */
        button(SDL_CONTROLLER_BUTTON_A);
        printf("player 1 signed out: %d in, player 1 now %s\n", signin_count(), signin_name(0));
        ok &= signin_count() == 1 && !strcmp(signin_name(0), "BERT") && !signin_busy();
    }
    {   /* Calibration adjusts the centre, never accepts a pushed stick. */
        InputSnapshot snapshot;
        input_set_deadzone(12);
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_LEFTX, 6000); pump(40);
        ok &= input_snapshot(DEV_PAD1, &snapshot) && snapshot.axis[0] == 6000;
        ok &= input_calibrate(DEV_PAD1);
        ok &= !(input_device(DEV_PAD1) & IN_RIGHT);
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_LEFTX, 30000); pump(40);
        ok &= !input_calibrate(DEV_PAD1);
        ok &= !!(input_device(DEV_PAD1) & IN_RIGHT);
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_LEFTX, 0); pump(40);
        input_reset_calibration(); input_set_deadzone(37);
        ok &= !(input_device(DEV_PAD1) & (IN_LEFT | IN_RIGHT));
    }
    printf(ok ? "stick and d-pad: ok\n" : "stick and d-pad: FAILED\n");
    return !ok;
}
