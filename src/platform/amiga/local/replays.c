/* SPDX-License-Identifier: MIT
 * src/platform/amiga/local/replays.c: playing and watching: local matches in the online client, recording, the replay viewer.
 * A part of the Amiga client, its own translation unit: what the parts share is in internal.h. */
#include "platform/amiga/local/internal.h"
#include "platform/amiga/local/pacing.h"

static int second_takeover;

void online_set_playing(int playing)
{
    online_playing = playing;
    chat_edit = 0;
    chat_line[0] = 0;
    chat_until = 0;
    online_clear_lobby_focus();
    memset(keys, 0, sizeof keys);
    ModifyIDCMP(window, RAW_EVENTS | (!playing && online.socket_open ? IDCMP_MOUSEBUTTONS : 0));
    online_dirty = 1;
}

void online_disconnect(const char *reason)
{
    snprintf(online_status, sizeof online_status, "%s", reason);
    netc_close(&online);
    online_set_playing(0);
}

void local_begin(int one_player, int side, int second_cls)
{
    second_takeover = 0;
    solo = one_player;
    new_match(side, second_cls);
    if (quit) {
        quit = 0;
        strcpy(online_status, "Could not start the local map.");
        return;
    }
    local_playing = 1;
    recorded = 0;
    local_tick = local_draw = net_ms();
    memset(keys, 0, sizeof keys);
    ModifyIDCMP(window, RAW_EVENTS);
}

void local_join_second(void)
{
    if (!local_playing || !solo || training_on || game.winner >= 0 ||
        game.time_up || game.num_tanks != 2 || game.tanks[1].out) return;
    solo = 0;
    second_takeover = 1;
    memset(keys, 0, sizeof keys);
    unpause();
}

void local_step(uint32_t now)
{
    enum { MAX_STEPS = 8, MAX_LAG_MS = 400 };
    int steps = 0;
    if (active && game.winner < 0 && !game.time_up) {
        InputBits inputs[MAX_TANKS] = { 0 };
        inputs[0] = player_input(0);
        if (!solo) inputs[1] = player_input(1);
        if ((int32_t)(now - local_tick) > MAX_LAG_MS) local_tick = now - MAX_LAG_MS;
        while ((int32_t)(now - local_tick) >= 0 && steps < MAX_STEPS &&
               game.winner < 0 && !game.time_up) {
            if (second_takeover) inputs[1] |= IN_TAKEOVER;
            if (solo && !training_on && game.num_tanks == 2 && !game.tanks[1].ai)
                inputs[1] = field_bot_input(1);
            if (game.bot_commands) {
                int bot;
                for (bot = 0; bot < game.num_tanks; bot++)
                    if (game.tanks[bot].ai) inputs[bot] = ai_think_local(&game, bot, 512);
            }
            game_tick(&game, inputs);
            if (operation.phase) operation_step(&game, &operation);
#ifdef OFP_AMIGA_ONLINE
            rec_tick(inputs);
#endif
            second_takeover = 0;
            inputs[1] &= (InputBits)~IN_TAKEOVER;
            local_tick += LOCAL_TICK_MS;
            steps++;
            if ((int32_t)(now - local_tick) >= 0 && (int32_t)(net_ms() - local_draw) >= LOCAL_TICK_MS) break;
        }
    } else local_tick = now + LOCAL_TICK_MS;
    now = net_ms();
    if ((int32_t)(now - local_draw) >= 0 &&
        ((int32_t)(now - local_tick) < LOCAL_TICK_MS || (int32_t)(now - local_draw) >= LOCAL_TICK_MS)) {
        draw_frame();
        local_draw = frame_deadline(local_draw, now, net_ms(), ONLINE_DRAW_MS);
    }
}

/* ---- replays: our matches recorded (PROGDIR:replays/), and any .ofprep watched ------------------ */

static void replay_dir(void)                  /* (made, if it isn't there) */
{
    BPTR l = Lock((CONST_STRPTR)REPLAY_DIR, ACCESS_READ);
    if (!l) l = CreateDir((CONST_STRPTR)REPLAY_DIR);
    if (l) UnLock(l);
}

void rec_begin(const MatchSetup *ms, const char *map)
{
    if (recording) replay_free(&rec);
    memset(&rec, 0, sizeof rec);
    replay_begin(&rec, ms, map, game.num_tanks);
    recording = 1;
}

void rec_tick(const InputBits *in) { if (recording) replay_tick(&rec, in, &game); }

/* the match over (or left): last.ofprep; a real match (a minute or more) kept, numbered, too */
void rec_end(void)
{
    char path[64];
    if (!recording) return;
    recording = 0;
    if (rec.ticks > 0) {
        replay_dir();
        replay_save(&rec, REPLAY_DIR "/last.ofprep");
        if (rec.ticks > 60 * TICK_RATE) {
            settings.replay_n = (settings.replay_n + 1) % 1000;
            snprintf(path, sizeof path, REPLAY_DIR "/match-%03d.ofprep", settings.replay_n);
            replay_save(&rec, path);
            save_menu_settings();
        }
    }
    replay_free(&rec);
}

void list_replays(void)                /* the .ofprep files in PROGDIR:replays (dos.library) */
{
    BPTR l;
    struct FileInfoBlock *fib;
    nreplays = 0;
    if (!(l = Lock((CONST_STRPTR)REPLAY_DIR, ACCESS_READ))) return;
    if ((fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL)) != NULL) {
        if (Examine(l, fib))
            while (nreplays < MAX_REPLAYS && ExNext(l, fib)) {
                const char *n = (const char *)fib->fib_FileName;
                size_t len = strlen(n);
                if (fib->fib_DirEntryType < 0 && len > 7 && !strcmp(n + len - 7, ".ofprep"))
                    snprintf(replay_names[nreplays++], sizeof replay_names[0], "%.31s", n);
            }
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(l);
}

static void watch_loading(int stage)
{
    replay_loading = stage;
    online_menu();
    WaitTOF();
}

void watch_start(const char *name)
{
    char path[80];
    int r;
    snprintf(path, sizeof path, REPLAY_DIR "/%s", name);
    online_status[0] = 0;
    watch_loading(1);
    r = replay_load(&watch, path);
    if (r == 0) {
        watch_loading(2);
        r = match_start_recorded(&game, &watch.setup, watch.map, watch.tanks);
        if (r == 0 && game.num_tanks != watch.tanks) r = -1;
        if (r != 0)
            snprintf(online_status, sizeof online_status, r == -2 ? "Map too large for this Amiga." : "That replay won't start here.");
    } else snprintf(online_status, sizeof online_status, "Can't read %.40s.", name);
    if (r != 0) {
        replay_free(&watch);
        replay_loading = 0;
        online_dirty = 1;
        return;
    }
    watch_loading(3);
    original_look = game.biome == BIOME_CLASSIC;
    solo = 1;
    reset_match_view();
    replay_loading = 0;
    replay_on = 1; replay_follow = 0; watch_tick = 0; watch_paused = 0; watch_speed = 2;
    local_draw = watch_next = net_ms();
    memset(keys, 0, sizeof keys);
    ModifyIDCMP(window, RAW_EVENTS);
}

void watch_end(void)
{
    replay_on = 0;
    replay_free(&watch);
    sound_engine(0);
    online_set_playing(0);
}

/* the strip across the view's top: the replay, its speed, who's followed */
void draw_watch_strip(void)
{
    char text[64];
    const char *who = watch.names[replay_follow % MATCH_PLAYERS][0] ? watch.names[replay_follow % MATCH_PLAYERS] : "THE COMPUTER";
    static const char *const sp[9] = { "", "1/2", "X1", "", "X2", "", "", "", "X4" };
    fill(INK, 0, 0, WIDTH - 1, 11);
    snprintf(text, sizeof text, "%s %s  TANK %d %.12s", watch_tick >= watch.ticks ? "END" : watch_paused ? "PAUSED" : "REPLAY",
             sp[watch_speed & 15 ? watch_speed : 2], replay_follow + 1, who);
    label(4, 9, GOLD, text);
    BltBitMapRastPort(frame, 0, 0, &presentation, 0, 0, WIDTH, 12, 0xc0);
}

void watch_step(uint32_t now)
{
    int steps = 0, k;
    uint32_t every = (uint32_t)(LOCAL_TICK_MS * 2 / watch_speed);
    if (watch_paused || watch_tick >= watch.ticks) watch_next = now + every;
    while (!watch_paused && watch_tick < watch.ticks && (int32_t)(now - watch_next) >= 0 && steps < 8) {
        InputBits in[MAX_TANKS] = { 0 };
        for (k = 0; k < watch.tanks && k < MAX_TANKS; k++) in[k] = watch.inputs[watch_tick * (uint32_t)watch.tanks + (uint32_t)k];
        game_tick(&game, in);
        watch_tick++;
        watch_next += every;
        steps++;
    }
    if ((int32_t)(now - watch_next) > 500) watch_next = now;   /* (a slow Amiga: no catching up for ever) */
    if ((int32_t)(now - local_draw) >= 0) {
        draw_frame();                                    /* (the strip: draw_frame's, before it's shown) */
        local_draw = now + ONLINE_DRAW_MS;
    }
}

/* ---- MAP EDITOR: the desktop's idea, on the Amiga. An empty map (sand, the border, both bases; the
 * bases can't be painted over), a cursor, seven brushes; F1 the world, F2 the size, F3 a random
 * battlefield to start from, F4 empty again, F5 save (PROGDIR:maps/my-map.ofpmap: LOCAL GAME's MY MAP),
 * F6 save and play it against the computer. Drawn by the game's own renderer: as it will look. */
