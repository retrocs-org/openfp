/* SPDX-License-Identifier: MIT
 * The local game's team screen: people and computer players spread evenly, a player who
 * switches swaps with a computer player there, sides within their garages and a match's
 * four, ready and start, the Original's two; and the modes into the match (two virtual
 * pads, the real code). */
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "platform/sdl2/input.h"
#include "platform/sdl2/teams.h"
#include "core/game.h"
static SDL_Joystick *j[2];
static Teams t;
static Game match;
static int last;
static void pump(int ms) { SDL_Event ev; Uint32 until = SDL_GetTicks() + ms;
    do { SDL_Delay(5); while (SDL_PollEvent(&ev)) input_handle_event(&ev); { int r = teams_frame(&t); if (r) last = r; } } while (SDL_GetTicks() < until); }
static void button(int pad, int b) { SDL_JoystickSetVirtualButton(j[pad], b, 1); pump(40); SDL_JoystickSetVirtualButton(j[pad], b, 0); pump(40); }
static void stick(int pad, int axis, int v) { SDL_JoystickSetVirtualAxis(j[pad], axis, (Sint16)v); pump(40); SDL_JoystickSetVirtualAxis(j[pad], axis, 0); pump(40); }
static void begin(int mode, int humans, int nbots, int cap0, int cap1, int most) {
    int devs[2] = { DEV_PAD1, DEV_PAD1 + 1 }, cls[2] = { 1, 2 }, b[2] = { nbots, 0 };
    char names[4][16] = { "ANNA", "BERT" };
    pump(20); teams_begin(&t, mode, humans, devs, names, NULL, cls, b, AI_NORMAL, cap0, cap1, most); pump(40);
}
int main(void) {
    SDL_VirtualJoystickDesc d; int ok = 1, k;
    MatchSetup ms;
    SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);
    input_init(NULL);
    SDL_zero(d); d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION; d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    d.naxes = SDL_CONTROLLER_AXIS_MAX; d.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    for (k = 0; k < 2; k++) j[k] = SDL_JoystickOpen(SDL_JoystickAttachVirtualEx(&d));
    pump(60);
    begin(TEAMS_FLAG, 2, 2, 4, 3, MATCH_PLAYERS);
    printf("2 here, 2 bots: camp %d (bots %d), fortress %d (bots %d)\n", teams_count(&t, 0), t.bots[0], teams_count(&t, 1), t.bots[1]);
    ok &= teams_count(&t, 0) == 2 && teams_count(&t, 1) == 2 && t.side[0] == 0 && t.side[1] == 1 && t.bots[0] == 1 && t.bots[1] == 1;
    memset(&ms, 0, sizeof ms); teams_setup(&t, &ms);
    ok &= ms.bot[2] == (AI_NORMAL | MATCH_BOT_RAIDER | MATCH_BOT_SIEGE) &&
          ms.bot[3] == (AI_NORMAL | MATCH_BOT_RAIDER | MATCH_BOT_SIEGE);
    stick(1, SDL_CONTROLLER_AXIS_LEFTX, -30000);          /* BERT: over to the camp; its bot goes the other way */
    printf("BERT switches to the camp: camp %d (bots %d), fortress %d (bots %d)\n", teams_count(&t, 0), t.bots[0], teams_count(&t, 1), t.bots[1]);
    ok &= t.side[1] == 0 && t.bots[0] == 0 && t.bots[1] == 2 && teams_count(&t, 0) == 2 && teams_count(&t, 1) == 2;
    {
        SDL_Event event = {0};
        ok &= t.leader[0] == 0 && t.leader[1] == -1 && !teams_rename(&t, 1);
        ok &= teams_rename(&t, 0);
        t.edit_name[0] = 0;
        event.type = SDL_TEXTINPUT;
        snprintf(event.text.text, sizeof event.text.text, "Iron Guard");
        ok &= teams_name_event(&t, &event);
        event.type = SDL_KEYDOWN;
        event.key.keysym.sym = SDLK_RETURN;
        ok &= teams_name_event(&t, &event) && !strcmp(t.side_name[0], "Iron Guard");
        pump(30);
        stick(0, SDL_CONTROLLER_AXIS_LEFTX, 30000);
        ok &= t.leader[0] == 1 && t.leader[1] == 0;
        stick(0, SDL_CONTROLLER_AXIS_LEFTX, -30000);
        ok &= t.leader[0] == 1 && t.leader[1] == -1;
        memset(&ms, 0, sizeof ms);
        teams_setup(&t, &ms);
        ok &= ms.leader_mask == 2 && match_start(&match, &ms, NULL) == 0 &&
              match.team_leader[TEAM_PLAYER] == 1 && match.team_leader[TEAM_ENEMY] == -1;
        teams_remove_player(&t, 1);
        ok &= t.humans == 1 && t.leader[0] == 0 && t.leader[1] == -1;
    }
    begin(TEAMS_FLAG, 1, 3, 4, 3, MATCH_PLAYERS);
    printf("1 here, 3 bots: camp %d, fortress %d\n", teams_count(&t, 0), teams_count(&t, 1));
    ok &= teams_count(&t, 0) == 2 && teams_count(&t, 1) == 2;
    memset(&ms, 0, sizeof ms); teams_setup(&t, &ms);
    ok &= ms.bot[1] == (AI_NORMAL | MATCH_BOT_RAIDER | MATCH_BOT_SIEGE) && ms.bot[2] == AI_NORMAL &&
          ms.bot[3] == (AI_NORMAL | MATCH_BOT_RAIDER | MATCH_BOT_SIEGE);
    begin(TEAMS_FLAG, 2, 0, 1, 1, MATCH_PLAYERS);
    stick(1, SDL_CONTROLLER_AXIS_LEFTX, -30000);
    printf("one garage a side, no bots, BERT pushes left: %s (%s)\n", t.side[1] ? "stays" : "MOVED (wrong)", t.note); ok &= t.side[1] == 1;
    begin(TEAMS_FLAG, 2, 0, 4, 3, MATCH_PLAYERS);
    stick(1, SDL_CONTROLLER_AXIS_LEFTX, -30000);
    button(0, SDL_CONTROLLER_BUTTON_A); button(1, SDL_CONTROLLER_BUTTON_A); last = 0; button(0, SDL_CONTROLLER_BUTTON_A);
    printf("both on the camp, nobody against: %s\n", last == TEAMS_START ? "STARTED (wrong)" : "won't start"); ok &= last != TEAMS_START;
    begin(TEAMS_CONQUEST, 2, 2, 4, 3, MATCH_PLAYERS);
    button(0, SDL_CONTROLLER_BUTTON_A);                   /* The host lands on Start while BERT gets ready. */
    ok &= t.host_sel == HB_START;
    {   /* one up to LEVEL, then back to START */
        int lv0;
        button(0, SDL_CONTROLLER_BUTTON_DPAD_UP);
        lv0 = t.bot_level;
        button(0, SDL_CONTROLLER_BUTTON_A);
        printf("ANNA at LEVEL, FIRE: level %d -> %d\n", lv0, t.bot_level); ok &= t.bot_level == (lv0 >= AI_ADAPTIVE ? AI_EASY : lv0 + 1);
        button(0, SDL_CONTROLLER_BUTTON_DPAD_DOWN);       /* to START */
    }
    last = 0; button(0, SDL_CONTROLLER_BUTTON_A);
    printf("ANNA at START, BERT not ready: %s (%s)\n", last == TEAMS_START ? "STARTED (wrong)" : "waits", t.note);
    ok &= last != TEAMS_START && strstr(t.note, "NOT READY") != NULL;
    button(0, SDL_CONTROLLER_BUTTON_B);                   /* back to her side and tank */
    button(1, SDL_CONTROLLER_BUTTON_A);                   /* BERT: ready */
    button(0, SDL_CONTROLLER_BUTTON_A); last = 0; button(0, SDL_CONTROLLER_BUTTON_A);   /* ANNA: the buttons (on START now), START */
    printf("BERT ready, ANNA: FIRE, FIRE: %s\n", last == TEAMS_START ? "starts" : "DOESN'T START"); ok &= last == TEAMS_START;
    memset(&ms, 0, sizeof ms); teams_setup(&t, &ms);
    printf("conquest's match: %d players, versus %d, conquest %s\n", ms.players, ms.versus, (ms.extras & EXTRA_CONQUEST) ? "on" : "OFF");
    ok &= ms.players == 4 && ms.versus && (ms.extras & EXTRA_CONQUEST) && !ms.bot[0] && !ms.bot[1] && ms.bot[2] && ms.bot[3];
    begin(TEAMS_ANNIHILATION, 1, 1, 4, 3, MATCH_PLAYERS);
    memset(&ms, 0, sizeof ms); teams_setup(&t, &ms);
    printf("annihilation's match: %d players, annihilation %s\n", ms.players, (ms.extras & EXTRA_ANNIHILATION) ? "on" : "OFF");
    ok &= ms.players == 2 && (ms.extras & EXTRA_ANNIHILATION) && !ms.leader_mask;
    begin(TEAMS_FLAG, 2, 0, 1, 1, 2);
    button(1, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    memset(&ms, 0, sizeof ms); teams_setup(&t, &ms);
    ok &= t.cls[1] == TANK_RHINO && ms.cls[1] == TANK_HEAVY && ms.rocket_tanks == 2;
    begin(TEAMS_CLASSIC, 2, 0, 1, 1, 2);
    button(1, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    memset(&ms, 0, sizeof ms); teams_setup(&t, &ms);
    ok &= t.cls[1] == TANK_LIGHT && ms.cls[1] == TANK_LIGHT && !ms.rocket_tanks;
    begin(TEAMS_FLAG, 2, 3, 3, 3, 2);
    printf("the Original world, 2 here, 3 bots asked: %d in the match\n", teams_count(&t, 0) + teams_count(&t, 1));
    ok &= teams_count(&t, 0) + teams_count(&t, 1) == 2;
    begin(TEAMS_FLAG, 1, 3, 3, 3, 2);
    printf("the Original world, 1 here: camp %d, fortress %d (1 vs 1)\n", teams_count(&t, 0), teams_count(&t, 1));
    ok &= teams_count(&t, 0) == 1 && teams_count(&t, 1) == 1;
    last = 0; button(0, SDL_CONTROLLER_BUTTON_B);
    printf("ANNA's MINE, not ready: %s\n", last == TEAMS_BACK ? "back to the menu" : "STAYS (wrong)"); ok &= last == TEAMS_BACK;
    {   /* player 1's AI bots: + fills the smaller side up, then a pair; - takes the bigger side's, then a pair */
        int k2;
        begin(TEAMS_CONQUEST, 1, 0, 3, 3, 6);
        teams_add_bot(&t);
        printf("one of us, + once: camp %d, fortress %d (even)\n", teams_count(&t, 0), teams_count(&t, 1));
        ok &= teams_count(&t, 0) == 1 && teams_count(&t, 1) == 1;
        teams_add_bot(&t);
        printf("+ again: camp %d, fortress %d (a pair)\n", teams_count(&t, 0), teams_count(&t, 1));
        ok &= teams_count(&t, 0) == 2 && teams_count(&t, 1) == 2;
        for (k2 = 0; k2 < 5; k2++) teams_add_bot(&t);
        printf("+ five more on a 3-a-side map: camp %d, fortress %d\n", teams_count(&t, 0), teams_count(&t, 1));
        ok &= teams_count(&t, 0) == 3 && teams_count(&t, 1) == 3;
        teams_remove_bot(&t);
        printf("- once: camp %d, fortress %d (a pair)\n", teams_count(&t, 0), teams_count(&t, 1));
        ok &= teams_count(&t, 0) == 2 && teams_count(&t, 1) == 2;
        teams_remove_bot(&t);
        printf("- again: camp %d, fortress %d; - with only us left: %s\n", teams_count(&t, 0), teams_count(&t, 1),
               teams_remove_bot(&t) ? "TOOK ONE (wrong)" : "nothing (the sides would be uneven)");
        ok &= teams_count(&t, 0) == 1 && teams_count(&t, 1) == 1;
        last = 0; button(0, SDL_CONTROLLER_BUTTON_A); button(0, SDL_CONTROLLER_BUTTON_A);
        printf("1 against 1, FIRE: %s\n", last == TEAMS_START ? "starts" : "DOESN'T START"); ok &= last == TEAMS_START;
    }
    {   /* player 1's pad: up/down the tank; FIRE to the buttons: ADD, REMOVE (the sides even), back, START */
        int cls0;
        begin(TEAMS_CONQUEST, 1, 2, 4, 4, 8);
        cls0 = t.cls[0];
        button(0, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        printf("ANNA, down: tank %d -> %d\n", cls0, t.cls[0]); ok &= t.cls[0] == (cls0 + 1) % 3;
        button(0, SDL_CONTROLLER_BUTTON_A);
        printf("FIRE: at the buttons %s, on %s (one side still needs a bot)\n", t.host_buttons ? "yes" : "NO", t.host_sel == HB_START ? "START" : "WRONG");
        ok &= t.host_buttons && t.host_sel == HB_START;
        button(0, SDL_CONTROLLER_BUTTON_DPAD_DOWN);       /* Wrap from Start to Add. */
        button(0, SDL_CONTROLLER_BUTTON_A);
        printf("ADD: camp %d, fortress %d\n", teams_count(&t, 0), teams_count(&t, 1)); ok &= teams_count(&t, 0) == 2 && teams_count(&t, 1) == 2;
        button(0, SDL_CONTROLLER_BUTTON_A);
        printf("ADD: camp %d, fortress %d\n", teams_count(&t, 0), teams_count(&t, 1)); ok &= teams_count(&t, 0) == 3 && teams_count(&t, 1) == 3;
        button(0, SDL_CONTROLLER_BUTTON_DPAD_DOWN); button(0, SDL_CONTROLLER_BUTTON_A);
        printf("REMOVE: camp %d, fortress %d\n", teams_count(&t, 0), teams_count(&t, 1)); ok &= teams_count(&t, 0) == 2 && teams_count(&t, 1) == 2;
        button(0, SDL_CONTROLLER_BUTTON_B);
        printf("BACK: at the buttons %s, ready %s\n", t.host_buttons ? "STILL" : "no", t.ready[0] ? "STILL" : "no");
        ok &= !t.host_buttons && !t.ready[0];
        button(0, SDL_CONTROLLER_BUTTON_A); last = 0; button(0, SDL_CONTROLLER_BUTTON_A);
        printf("FIRE (on START now), FIRE: %s\n", last == TEAMS_START ? "starts" : "DOESN'T START"); ok &= last == TEAMS_START;
    }
    {   /* B: back, whatever the pad's buttons are (ANNA's MINE set to X here) */
        input_set_dev_pad(DEV_PAD1, PAD_CLASSIC, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X);   /* (FIRE on B, even) */
        begin(TEAMS_CONQUEST, 1, 1, 4, 4, 8);
        button(0, SDL_CONTROLLER_BUTTON_A);                   /* ready: to the buttons */
        button(0, SDL_CONTROLLER_BUTTON_B);
        printf("ANNA's FIRE on B, MINE on X; A ready, at the buttons, B: %s\n", !t.host_buttons && !t.ready[0] ? "back to her tank" : "NOTHING");
        ok &= !t.host_buttons && !t.ready[0];
        last = 0; button(0, SDL_CONTROLLER_BUTTON_B);
        printf("B again: %s\n", last == TEAMS_BACK ? "back to the menu" : "NOTHING");
        ok &= last == TEAMS_BACK;
        input_set_dev_pad(DEV_PAD1, -2, -2, -2);              /* (as it was) */
    }
    printf(ok ? "teams: ok\n" : "teams: FAILED\n");
    return !ok;
}
