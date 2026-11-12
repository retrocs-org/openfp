/* SPDX-License-Identifier: MIT
 * src/platform/amiga/local/duel.c: the local client (OpenFirePower): its main loop.
 * A part of the Amiga client, its own translation unit: what the parts share is in internal.h. */
#include "platform/amiga/local/internal.h"
#include "platform/amiga/local/pacing.h"

int main(int argc, char **argv)
{
    enum { TICK_MS = 1000 / TICK_RATE, DRAW_MS = 40, MAX_CATCHUP = 8, MAX_LAG_MS = 400 };
    InputBits input[MAX_TANKS] = { 0 };
    uint32_t next_tick, next_draw;
    int status = 0;
    (void)argc;
    (void)argv;
    if (!open_display()) {
        puts("OpenFP needs an AmigaOS 3.x PAL screen and available chip RAM.");
        sound_close();
        close_display();
        return 20;
    }
    sound_open();                                        /* (no audio.device: silence) */
    label(88, 124, WHITE, "LOADING ARENA");
    BltBitMapRastPort(frame, 0, 0, window->RPort, 0, 0, WIDTH, HEIGHT, 0xc0);
    new_match(TEAM_PLAYER, TANK_MEDIUM);
    if (quit) status = 20;
    next_tick = next_draw = net_ms();
    while (!quit) {
        uint32_t now;
        int steps = 0;
        events();
        update_music(1);
        if (quit) break;
        if (restart) {
            restart = 0;
            new_match(TEAM_PLAYER, TANK_MEDIUM);
            if (quit) { status = 20; break; }
            next_tick = next_draw = net_ms();
        }
        now = net_ms();
        if (active && game.winner < 0 && !game.time_up && (int32_t)(now - next_tick) >= 0) {
            if ((int32_t)(now - next_tick) > MAX_LAG_MS) next_tick = now - MAX_LAG_MS;
            input[0] = player_input(0);
            input[1] = solo ? 0 : player_input(1);
            while ((int32_t)(now - next_tick) >= 0 && steps < MAX_CATCHUP &&
                   game.winner < 0 && !game.time_up) {
                if (solo) input[1] = solo_bot_input();
                game_tick(&game, input);
                next_tick += TICK_MS;
                steps++;
                if ((int32_t)(now - next_tick) >= 0 && (int32_t)(net_ms() - next_draw) >= TICK_MS) break;
            }
        } else if (!active || game.winner >= 0 || game.time_up) {
            next_tick = now + TICK_MS;
        }
        now = net_ms();
        if ((int32_t)(now - next_draw) >= 0 &&
            ((int32_t)(now - next_tick) < TICK_MS || (int32_t)(now - next_draw) >= TICK_MS)) {
            draw_frame();
            next_draw = frame_deadline(next_draw, now, net_ms(), DRAW_MS);
        }
        now = net_ms();
        if ((int32_t)(now - next_tick) < 0 && (int32_t)(now - next_draw) < 0) WaitTOF();
    }
    music_close();
    sound_close();
    close_display();
    return status;
}
