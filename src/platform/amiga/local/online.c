/* SPDX-License-Identifier: MIT
 * src/platform/amiga/local/online.c: the online client (OpenFirePowerOnline): its main loop.
 * A part of the Amiga client, its own translation unit: what the parts share is in internal.h. */
#include "platform/amiga/local/internal.h"
#include "platform/amiga/local/pacing.h"

uint32_t chat_until;
int online_follow;

int online_view_tank(void)
{
    return online.slot == 255 ? online_follow : online.slot;
}

int main(int argc, char **argv)
{
    uint32_t tick = 0, next_draw = 0, next_menu = 0;
    int benchmark_cli = argc == 2 && !strcmp(argv[1], "--benchmark");
    int pacing_cli = argc == 2 && !strcmp(argv[1], "--benchmark-pacing");
    int recovery_cli = argc == 2 && !strcmp(argv[1], "--editor-recovery-test");
    int detailed_cli = argc == 2 && !strcmp(argv[1], "--benchmark-detailed");
    int stability_cli = argc >= 4 && !strcmp(argv[1], "--stability-test");
    int exit_status = 0;
    if (argc == 2 && !strcmp(argv[1], "--public-fetch")) return amiga_public_worker();
    if (argc == 4 && !strcmp(argv[1], "--share-map")) return amiga_share_worker(argv[2], argv[3]);
    if (argc == 0) amiga_worker_workbench((const struct WBStartup *)argv);
    if (!open_display()) {
        puts("OpenFP needs an AmigaOS 3.x PAL screen and available chip RAM.");
        sound_close();
        close_display();
        return 20;
    }
    sound_open();                                        /* (no audio.device: silence) */
    KeymapBase = OpenLibrary((CONST_STRPTR)"keymap.library", 36);
    amiga_settings_load(&settings);
    read_my_map_metadata();
    {
        BPTR catalog = Lock((CONST_STRPTR)"PROGDIR:maps/catalog", ACCESS_READ);
        if (catalog) {
            local_map_count += catalog_map_count;
            UnLock(catalog);
        }
    }
    apply_menu_settings();
    online_set_playing(0);
    if (recovery_cli) exit_status = editor_recovery_test() == 0 ? 0 : 20;
    else if (stability_cli) {
        exit_status = run_stability_test(argv[2], atoi(argv[3]), argc > 4 ? atoi(argv[4]) : 50) == 0 ? 0 : 20;
    } else if (detailed_cli) {
        exit_status = run_detailed_benchmark() == 0 ? 0 : 20;
        puts(online_status);
    } else if (benchmark_cli) {
        exit_status = run_benchmark() == 0 ? 0 : 20;
        puts(online_status);
    } else if (pacing_cli) {
        exit_status = run_pacing_benchmark() == 0 ? 0 : 20;
        if (exit_status) puts("Pacing benchmark could not finish.");
    }
    else title_start();
    while (!quit && !benchmark_cli && !pacing_cli && !detailed_cli && !stability_cli && !recovery_cli) {
        uint32_t now, prior_chat;
        int state;
        online_events();
        update_music(editor_on ? -1 : (local_playing || online_playing || replay_on) ? 1 : 0);
        if (title_on) {
            online_escape = 0;
            if (online_enter) {
                online_enter = 0;
                title_on = 0;
                if (signed_profile[0] == SIGNED_OUT && signed_profile[1] == SIGNED_OUT) open_signin(0, MENU_MAIN);
                else menu_change(MENU_MAIN);
            } else {
                title_step(net_ms());
                WaitTOF();
                continue;
            }
        }
        if (online_escape) {
            online_escape = 0;
            if (replay_on) watch_end();                  /* (a replay: back to the list) */
            else if (editor_on) editor_toggle_menu();
            else if (profile_menu_active()) profile_back();
            else if (team_finish_name(0)) {}
            else if (paused) unpause();                  /* (ESC again: back to it) */
            else if (local_playing && (game.winner >= 0 || game.time_up || (training_on && tr.step >= TR_DONE))) {
                local_playing = 0; training_on = 0; operation.phase = OP_INACTIVE;      /* (over: ESC, as the banner says, to the menu) */
                online_set_playing(0);
                if (!editor_return_from_test()) menu_change(MENU_MAIN);
            } else if (local_playing || online_playing) {   /* a match: the pause page */
                paused = 1; pause_row = 0; online_dirty = 1;
            } else if (online.socket_open && online.state == NC_ENDED) {
                netc_back_to_lobby(&online);
                menu_row = 0;
                online_dirty = 1;
            } else if (online.socket_open) online_disconnect("Disconnected.");
            else if (menu_page == MENU_NETWORK) network_back();
            else if (menu_page == MENU_TEAMS) {
                if (team_ready[0]) { team_ready[0] = 0; online_dirty = 1; }
                else menu_change(MENU_LOCAL);
            }
            else if (menu_page == MENU_LOCAL || menu_page == MENU_CHALLENGES) menu_change(MENU_PLAY);
            else if (menu_page == MENU_REPLAYS || menu_page == MENU_DIAGNOSTICS) menu_change(MENU_TOOLS);
            else if (menu_page == MENU_ABOUT) { menu_change(MENU_MAIN); menu_row = MAIN_ABOUT; }
            else if (menu_page == MENU_CONTROLS || menu_page == MENU_KEYMAP) submenu_back();
            else if (menu_page >= MENU_OPTIONS && menu_page <= MENU_AUDIO) settings_back();
            else if (menu_page != MENU_MAIN) menu_change(MENU_MAIN);
            else {
                title_start();
                if (title_on) {
                    online_enter = 0;
                    continue;
                }
            }
        }
        if (online_enter) {
            online_enter = 0;
            if (paused || profile_menu_active() || (!local_playing && !online_playing && !replay_on && !editor_on))
                sound_play(SND_PICKUP, 20);
            if (profile_menu_active()) profile_accept();
            else if (operation.phase >= OP_WON && !paused) start_desert_operation();
            else if (team_finish_name(1)) {}
            else if (paused) {                           /* RESUME, or QUIT MATCH */
                if (pause_row >= 2) orders_accept();
                else if (pause_row == 0) unpause();
                else if (local_playing) {
                    paused = 0; local_playing = 0; training_on = 0; operation.phase = OP_INACTIVE;
                    online_set_playing(0);
                    if (!editor_return_from_test()) menu_change(MENU_MAIN);
                }
                else {
                    paused = 0;
                    netc_leave_match(&online);
                    online_set_playing(0);
                }
            } else if (!online.socket_open && !local_playing) menu_accept();
            else if (online.state == NC_LOBBY) netc_ready(&online, !online.ready, online.want_cls);
            else if (online.state == NC_ENDED) {          /* REMATCH (a vote, then the lobby), or LOBBY */
                if (menu_row == 0) netc_rematch(&online, 1);
                netc_back_to_lobby(&online);
                menu_row = 0;
            }
            online_dirty = 1;
        }
        if (title_on) continue;
        if (recording && !local_playing && !online_playing) rec_end();   /* (a match over or left: its replay saved) */
        if (replay_on) {
            watch_step(net_ms());
            WaitTOF();
            continue;
        }
        if (editor_on) {
            editor_step(net_ms());
            WaitTOF();
            continue;
        }
        if (local_playing && paused) {                   /* (the pause page: the match waits) */
            local_tick = net_ms() + LOCAL_TICK_MS;
            if (online_dirty) online_menu();
            WaitTOF();
            continue;
        }
        if (local_playing && (game.winner >= 0 || game.time_up)) record_result(game.winner == (game.tanks[0].team & 1));
        if (local_playing) {
            local_step(net_ms());
            if (training_on) training_check(&game, &tr);
            now = net_ms();
            if ((int32_t)(now - local_tick) < 0 && (int32_t)(now - local_draw) < 0) WaitTOF();
            continue;
        }
        if (online_refresh && menu_page == MENU_NETWORK && !online.socket_open) {
            online_refresh = 0;
            if (public_tab) {
                if (amiga_public_start(&public_list)) {
                    public_failed = 1;
                    strcpy(online_status, "Could not start the server list helper.");
                } else strcpy(online_status, "Fetching official list...");
            } else check_server();
            online_dirty = 1;
        }
        if (public_list.pending) {
            int result = amiga_public_poll(&public_list);
            if (result) {
                public_checked = 1;
                public_failed = result < 0;
                if (menu_page == MENU_NETWORK && public_tab && !online.socket_open) {
                    if (menu_row >= menu_rows()) menu_row = 0;
                    strcpy(online_status, result < 0 ? "Public list unavailable. Press F5 to retry." :
                           public_list.count ? "Official public list updated." : "The official list is empty.");
                    online_dirty = 1;
                }
            } else if (menu_page == MENU_NETWORK && public_tab && !online.socket_open &&
                       net_ms() - public_list.started > 15000 &&
                       !strcmp(online_status, "Fetching official list...")) {
                strcpy(online_status, "HTTPS still waiting. Try Private.");
                online_dirty = 1;
            }
        }
        prior_chat = online.chat_seq;
        state = online.socket_open ? netc_step(&online) : NC_FAILED;
        now = net_ms();
        if (online.socket_open && state == NC_FAILED) online_disconnect(online.status);
        if (state == NC_RUNNING && (netc_take_restart(&online) || !online_playing)) {
            int spectator = netc_spectating(&online);
            int started = !spectator && (online.slot < 0 || online.slot >= MAX_TANKS) ? -9 :
                          match_start(&game, &online.ms, online.map_len ? online.map : NULL);
            if (started != 0 || (!spectator && online.slot >= game.num_tanks) || game.num_tanks != online.tanks) {
                online_disconnect(started == -2 ? "That map is too big for this Amiga." :
                                  "That match won't start here.");
                state = NC_FAILED;
            } else {
                solo = 1;
                recorded = 0;
                online_follow = 0;
                rec_begin(&online.ms, online.map_len ? online.map : NULL);
                original_look = game.biome == BIOME_CLASSIC;
                reset_match_view();
                online_set_playing(1);
                tick = 0;
                next_draw = now;
            }
        }
        if (state == NC_RUNNING && online_playing) {
            int steps = 0;
            InputBits official[MAX_TANKS];
            InputBits input = active && !paused && !chat_edit ? player_input(0) : 0;
            uint32_t through = online.have + (uint32_t)online.delay + ONLINE_LOOKAHEAD;
            if (online.chat_seq != prior_chat) {
                chat_until = now + 8000;
                online_dirty = 1;
            } else if (chat_until && (int32_t)(now - chat_until) >= 0) {
                chat_until = 0;
                online_dirty = 1;
            }
            while (!netc_spectating(&online) && online.mine_hi <= through) netc_input(&online, online.mine_hi, input);
            while (steps < ONLINE_CATCHUP && netc_tick(&online, tick, official)) {
                game_tick(&game, official);
                rec_tick(official);
                if (!netc_spectating(&online) && (tick + 1) % NET_CHECK_EVERY == 0)
                    netc_check(&online, tick, game_checksum(&game));
                tick++;
                steps++;
            }
            if (paused) { if (online_dirty) online_menu(); }
            else if ((int32_t)(now - next_draw) >= 0 && (steps || online_dirty || next_draw == now)) {
                uint32_t due = next_draw;
                uint32_t start = net_ms();
                draw_frame();
                online_dirty = 0;
                now = net_ms();
                next_draw = frame_deadline(due, start, now, ONLINE_DRAW_MS);
                if (next_draw == now) continue;
            }
        } else {
            if (online_playing) online_set_playing(0);
            if (online_dirty || ((online.socket_open || menu_page == MENU_ABOUT || menu_page == MENU_DIAGNOSTICS) && (int32_t)(now - next_menu) >= 0)) {
                online_menu();
                next_menu = now + (menu_page == MENU_ABOUT ? 50 : 200);
            }
        }
        WaitTOF();
    }
    if (online.socket_open) netc_close(&online);
    music_close();
    sound_close();
    if (KeymapBase) CloseLibrary(KeymapBase);
    close_display();
    return exit_status;
}
