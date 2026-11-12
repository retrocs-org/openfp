/* SPDX-License-Identifier: MIT
 * Per-profile statistics and controls: recorded after a match, saved in profiles.ini, loaded again (an
 * old line too); a pad's layouts (CLASSIC, TRIGGERS) on a virtual pad. */
#include "platform/sdl2/pref_path.h"
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "platform/sdl2/input.h"
#include "platform/sdl2/profiles.h"
#include "platform/sdl2/settings.h"

static int check_keymaps(void)
{
    const char *legacy =
        "version=3\n"
        "p1_up=W\np1_down=S\np1_left=A\np1_right=D\np1_fire=Space\np1_mine=Left Shift,E\n"
        "p2_up=Up\np2_down=Down\np2_left=Left\np2_right=Right\np2_fire=Right Ctrl,Return\np2_mine=Right Shift,/\n"
        "p3_up=I\n";
    Settings settings, reloaded;
    int ok;
    FILE *f = fopen(settings_path(), "w");
    if (!f) return 0;
    fputs(legacy, f);
    fclose(f);
    settings_defaults(&settings);
    ok = settings_load(&settings) == 0 &&
         settings.keys[0][ACT_UP][0] == SDL_SCANCODE_UP &&
         settings.keys[0][ACT_FIRE][0] == SDL_SCANCODE_PERIOD &&
         settings.keys[0][ACT_MINE][0] == SDL_SCANCODE_COMMA &&
         settings.keys[1][ACT_UP][0] == SDL_SCANCODE_W &&
         settings.keys[2][ACT_UP][0] == SDL_SCANCODE_I;
    settings.keys[0][ACT_MINE][1] = SDL_SCANCODE_SLASH;
    settings.keys[2][ACT_FIRE][0] = SDL_SCANCODE_K;
    settings.keys[2][ACT_FIRE][1] = SDL_SCANCODE_COMMA;
    settings_defaults(&reloaded);
    ok &= settings_save(&settings) == 0 && settings_load(&reloaded) == 0 &&
          !memcmp(settings.keys, reloaded.keys, sizeof settings.keys);

    f = fopen(settings_path(), "w");
    if (!f) return 0;
    fputs(legacy, f);
    fputs("p1_fire=F\np1_mine=,\n", f);
    fclose(f);
    settings_defaults(&settings);
    ok &= settings_load(&settings) == 0 && settings.keys[0][ACT_UP][0] == SDL_SCANCODE_W &&
          settings.keys[0][ACT_FIRE][0] == SDL_SCANCODE_F &&
          settings.keys[0][ACT_MINE][0] == SDL_SCANCODE_COMMA;
    remove(settings_path());

    ok &= input_device_key(DEV_KEYS1, SDL_SCANCODE_UP) == IN_UP &&
          input_device_key(DEV_KEYS1, SDL_SCANCODE_PERIOD) == IN_FIRE &&
          input_device_key(DEV_KEYS1, SDL_SCANCODE_COMMA) == IN_MINE &&
          input_device_key(DEV_KEYS1, SDL_SCANCODE_W) == 0 &&
          input_device_key(DEV_KEYS2, SDL_SCANCODE_UP) == 0 &&
          input_device_key(DEV_KEYS2, SDL_SCANCODE_W) == IN_UP;
    printf("default keymaps, migration and comma persistence: %s\n", ok ? "ok" : "FAILED");
    return ok;
}

int main(void) {
    char path[512], line[512]; FILE *f; int ok = 1;
    SDL_Init(SDL_INIT_EVENTS);
    input_init(NULL);
    ok &= check_keymaps();
    {
        char *pref = ofp_pref_path();
        if (!pref) return 1;
        snprintf(path, sizeof path, "%sprofiles.ini", pref);
        SDL_free(pref);
    }
    if (!(f = fopen(path, "w"))) return 1;
    fputs("ANNA\t", f); for (int k = 0; k < 32; k++) fputs("cd", f); fputs("\t2\t4\n", f); fclose(f);   /* (an old line) */
    signin_init();
    signin_test("ANNA");
    signin_record(0, 1, 5, 2, 1, 2);
    signin_record(0, 0, 3, 4, 3, 2);
    f = fopen(path, "r"); line[0] = 0;
    while (f && fgets(line, sizeof line, f)) if (!strncmp(line, "ANNA\t", 5)) break;
    if (f) fclose(f);
    { char *st = strchr(line, '\t'); int k = 0; while (st && k < 3) { st = strchr(st + 1, '\t'); k++; }
      printf("ANNA's line, its stats: %s", st ? st + 1 : "MISSING\n"); ok &= st && !strncmp(st + 1, "2 1 8 6\t0 1 0 0 0\t0 0 2", 23); }
    if (!(f = fopen(path, "a"))) return 1;
    fputs("OLDIE\t", f); for (int k = 0; k < 32; k++) fputs("ab", f); fputs("\t1\t3\n", f); fclose(f);
    signin_init();                                       /* (loaded again: an old line without stats, too) */
    printf("loaded again: fine\n");
    {   /* a pad's layout: TRIGGERS (RT forward, LT back, the stick turns), CLASSIC (RT fires, the stick drives) */
        SDL_VirtualJoystickDesc d; SDL_Joystick *j; SDL_Event ev; InputBits b; int lay;
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
        SDL_zero(d); d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION; d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
        d.naxes = SDL_CONTROLLER_AXIS_MAX; d.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
        j = SDL_JoystickOpen(SDL_JoystickAttachVirtualEx(&d));
        for (int q = 0; q < 20; q++) { SDL_Delay(5); while (SDL_PollEvent(&ev)) input_handle_event(&ev); }
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);   /* (both triggers at rest) */
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
        SDL_JoystickUpdate();
        for (lay = PAD_CLASSIC; lay <= PAD_TRIGGERS; lay++) {
            InputBits rt, lt, up, left;
            input_set_dev_pad(DEV_PAD1, lay, -2, -2);
            SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767); SDL_JoystickUpdate(); rt = input_device(DEV_PAD1);
            SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);   /* (at rest: the lowest) */
            SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767); SDL_JoystickUpdate(); lt = input_device(DEV_PAD1);
            SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
            SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_LEFTY, -30000); SDL_JoystickUpdate(); up = input_device(DEV_PAD1);
            SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_LEFTY, 0);
            SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_LEFTX, -30000); SDL_JoystickUpdate(); left = input_device(DEV_PAD1);
            SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_LEFTX, 0); SDL_JoystickUpdate();
            if (lay == PAD_TRIGGERS) {
                printf("TRIGGERS: RT %s, LT %s, stick up %s, stick left %s\n", (rt & IN_UP) && !(rt & IN_FIRE) ? "forward" : "WRONG",
                       (lt & IN_DOWN) && !(lt & IN_MINE) ? "back" : "WRONG", !(up & IN_UP) ? "nothing" : "WRONG", (left & IN_LEFT) ? "turns" : "WRONG");
                ok &= (rt & IN_UP) && !(rt & IN_FIRE) && (lt & IN_DOWN) && !(lt & IN_MINE) && !(up & IN_UP) && (left & IN_LEFT);
            } else {
                printf("CLASSIC: RT %s, stick up %s\n", (rt & IN_FIRE) && !(rt & IN_UP) ? "fires" : "WRONG", (up & IN_UP) ? "forward" : "WRONG");
                ok &= (rt & IN_FIRE) && !(rt & IN_UP) && (up & IN_UP);
            }
        }
        {   /* the triggers (the default): RT forward, LT back, the stick only turns; A or RB fires, X or LB lays mines, B nothing */
            InputBits rt, lt, up, left, a, x, b, rb, lb;
            input_set_dev_pad(DEV_PAD1, -2, -2, -2);      /* (the default) */
            SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767); SDL_JoystickUpdate(); rt = input_device(DEV_PAD1);
            SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
            SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767); SDL_JoystickUpdate(); lt = input_device(DEV_PAD1);
            SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
            SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_LEFTY, -30000); SDL_JoystickUpdate(); up = input_device(DEV_PAD1);
            SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_LEFTY, 0);
            SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_LEFTX, -30000); SDL_JoystickUpdate(); left = input_device(DEV_PAD1);
            SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_LEFTX, 0);
#define PRESS(btn, var) SDL_JoystickSetVirtualButton(j, btn, 1); SDL_JoystickUpdate(); var = input_device(DEV_PAD1); SDL_JoystickSetVirtualButton(j, btn, 0);
            PRESS(SDL_CONTROLLER_BUTTON_A, a) PRESS(SDL_CONTROLLER_BUTTON_X, x) PRESS(SDL_CONTROLLER_BUTTON_B, b)
            PRESS(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, rb) PRESS(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, lb)
            SDL_JoystickUpdate();
            printf("DEFAULT: RT %s, LT %s, stick up %s, stick left %s, A %s, X %s, B %s, RB %s, LB %s\n",
                   rt == IN_UP ? "forward" : "WRONG", lt == IN_DOWN ? "back" : "WRONG", !up ? "nothing" : "WRONG",
                   left == IN_LEFT ? "turns" : "WRONG", a == IN_FIRE ? "fires" : "WRONG", x == IN_MINE ? "a mine" : "WRONG",
                   !b ? "nothing" : "WRONG", rb == IN_FIRE ? "fires" : "WRONG", lb == IN_MINE ? "a mine" : "WRONG");
            ok &= rt == IN_UP && lt == IN_DOWN && !up && left == IN_LEFT && a == IN_FIRE && x == IN_MINE && !b && rb == IN_FIRE && lb == IN_MINE;
        }
    }
    {   /* a profile's own controls: saved, loaded again, back on sign-in */
        int keys[6][2] = { { 26, 0 }, { 22, 0 }, { 4, 0 }, { 7, 0 }, { 44, 0 }, { 225, 0 } }, got[6][2] = { { 0 } }, l2 = -9, f2 = -9, m2 = -9;
        signin_set_controls(0, PAD_TRIGGERS, -2, -2, keys);
        signin_init();
        signin_test("ANNA");
        signin_get_controls(0, &l2, &f2, &m2, got);
        printf("ANNA's controls, loaded again: layout %s, keys %s\n", l2 == PAD_TRIGGERS ? "TRIGGERS" : "WRONG",
               !memcmp(got, keys, sizeof keys) ? "hers" : "WRONG");
        ok &= l2 == PAD_TRIGGERS && !memcmp(got, keys, sizeof keys);
        signin_reset_keys();
        ok &= input_device_key(DEV_KEYS1, SDL_SCANCODE_UP) == IN_UP &&
              input_device_key(DEV_KEYS1, SDL_SCANCODE_PERIOD) == IN_FIRE;
    }
    printf(ok ? "stats: ok\n" : "stats: FAILED\n");
    return !ok;
}
