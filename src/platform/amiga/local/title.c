/* SPDX-License-Identifier: MIT
 * src/platform/amiga/local/title.c: TRAINING, and the title screen.
 * A part of the Amiga client, its own translation unit: what the parts share is in internal.h. */
#include "platform/amiga/local/internal.h"
#include "platform/amiga/local/pacing.h"

static void demo_tick(void)
{
    InputBits inputs[MAX_TANKS] = { 0 };
    if (!game.tanks[0].ai) {
        inputs[0] = field_bot_input(0);
        inputs[1] = field_bot_input(1);
    }
    if (game.bot_commands) {
        int bot;
        for (bot = 0; bot < game.num_tanks; bot++)
            if (game.tanks[bot].ai) inputs[bot] = ai_think_local(&game, bot, 512);
    }
    game_tick(&game, inputs);
}

static int start_benchmark_match(void)
{
    if (!light_local_ai()) return match_benchmark_start(&game, 1);
    {
        MatchSetup setup = { 0 };
        setup.seed = 2026;
        setup.versus = 1;
        setup.players = 2;
        setup.cls[0] = setup.cls[1] = TANK_MEDIUM;
        setup.team[1] = TEAM_ENEMY;
        if (match_start(&game, &setup, classic_dust_crossing) != 0) return -1;
    }
    reset_field_bots();
    return 0;
}

int run_benchmark(void)
{
    uint32_t start, sim_ms, draw_ms;
    unsigned ticks = 0, frames = 0;
    if (start_benchmark_match() != 0) {
        strcpy(online_status, "COULD NOT START BENCHMARK");
        return -1;
    }
    start = net_ms();
    do {
        demo_tick();
        ticks++;
        sim_ms = net_ms() - start;
    } while (ticks < BENCH_TICKS && sim_ms < BENCH_MAX_MS);
    if (start_benchmark_match() != 0) {
        strcpy(online_status, "COULD NOT START DISPLAY TEST");
        return -1;
    }
    solo = 1;
    original_look = light_local_ai();
    reset_match_view();
    wait_for_display();
    start = net_ms();
    do {
        int step;
        for (step = 0; step < BENCH_TICKS_PER_FRAME; step++) demo_tick();
        draw_frame();
        frames++;
        draw_ms = net_ms() - start;
    } while (frames < BENCH_FRAMES && draw_ms < BENCH_MAX_MS);
    wait_for_display();
    draw_ms = net_ms() - start;
    snprintf(online_status, sizeof online_status, "%lu TICKS/S   %lu.%lu FPS",
             (unsigned long)(ticks * 1000u / (sim_ms ? sim_ms : 1)),
             (unsigned long)(frames * 1000u / (draw_ms ? draw_ms : 1)),
             (unsigned long)(frames * 10000u / (draw_ms ? draw_ms : 1) % 10));
    return 0;
}

int run_pacing_benchmark(void)
{
    enum { DURATION_MS = 5000, MAX_INTERVAL = 1000 };
    int split;
    for (split = 0; split <= 1; split++) {
        uint16_t histogram[MAX_INTERVAL + 1] = {0};
        MatchSetup setup = {0};
        unsigned frames = 0, ticks = 0, longest = 0, p95 = 0, seen = 0;
        uint32_t start, now, tick_due, draw_due, previous = 0;
        setup.seed = 2026;
        setup.versus = 1;
        setup.players = 2;
        setup.team[1] = TEAM_ENEMY;
        setup.cls[0] = setup.cls[1] = TANK_MEDIUM;
        setup.bot[1] = split ? AI_OFF : AI_NORMAL;
        if (match_start(&game, &setup, classic_dust_crossing) != 0) return -1;
        reset_field_bots();
        solo = !split;
        original_look = 1;
        reset_match_view();
        wait_for_display();
        start = now = tick_due = draw_due = net_ms();
        while (now - start < DURATION_MS) {
            int steps = 0;
            while ((int32_t)(now - tick_due) >= 0 && steps < 8) {
                demo_tick();
                tick_due += LOCAL_TICK_MS;
                ticks++;
                steps++;
                if ((int32_t)(now - tick_due) >= 0 && (int32_t)(net_ms() - draw_due) >= LOCAL_TICK_MS) break;
            }
            now = net_ms();
            if ((int32_t)(now - draw_due) >= 0 &&
                ((int32_t)(now - tick_due) < LOCAL_TICK_MS || (int32_t)(now - draw_due) >= LOCAL_TICK_MS)) {
                uint32_t before = now;
                int buffer = back_buffer;
                draw_frame();
                now = net_ms();
                if (buffer != back_buffer) {
                    if (frames) {
                        unsigned gap = now - previous;
                        histogram[gap < MAX_INTERVAL ? gap : MAX_INTERVAL]++;
                        if (gap > longest) longest = gap;
                    }
                    previous = now;
                    frames++;
                }
                draw_due = frame_deadline(draw_due, before, now, ONLINE_DRAW_MS);
            }
            if ((int32_t)(now - tick_due) < 0 && (int32_t)(now - draw_due) < 0) WaitTOF();
            now = net_ms();
        }
        if (frames < 2) return -1;
        while (p95 < MAX_INTERVAL && (seen += histogram[p95]) * 100 < (frames - 1) * 95) p95++;
        printf("%s: %lu.%lu FPS, p95 %u ms, max %u ms, %u ticks\n",
               split ? "SPLIT" : "SOLO",
               (unsigned long)(frames * 1000u / (now - start)),
               (unsigned long)(frames * 10000u / (now - start) % 10), p95, longest, ticks);
    }
    return 0;
}

/* A repeatable two-tank workload. Profiling is inactive during normal play. */
int run_detailed_benchmark(void)
{
    FILE *report = fopen("PROGDIR:benchmark.txt", "w");
    int scenario, method, saved_solo = solo, saved_look = original_look;
    unsigned last_fps = 0;
    if (!report) { strcpy(online_status, "CANNOT WRITE BENCHMARK.TXT"); return -1; }
    fprintf(report, "OpenFP %s / 16 colours / two tanks / CPU flags %04x\n", OFP_VERSION, SysBase->AttnFlags);
    fprintf(report, "Free memory at start: Chip %lu bytes; Fast %lu bytes\n", (unsigned long)AvailMem(MEMF_CHIP), (unsigned long)AvailMem(MEMF_FAST));
    fprintf(report, "Times are microseconds per displayed frame; includes one simulation tick.\n");
    for (method = 1; method <= 3; method++) for (scenario = 0; scenario < 3; scenario++) {
        MatchSetup ms = {0};
        uint64_t sum[6] = {0};
        uint32_t start, elapsed, worst = 0, sim_worst = 0;
        unsigned frames = 0, histogram[1001] = {0}, p95 = 0, seen = 0;
        ms.seed = 2026; ms.players = 2; ms.versus = 1; ms.map_size = SIZE_MEDIUM + 1;
        ms.team[1] = TEAM_ENEMY;
        ms.cls[0] = ms.cls[1] = TANK_MEDIUM;
        ms.bot_commands = 1;
        ms.bot[0] = ms.bot[1] = AI_NORMAL;
        ms.biome = scenario == 2 ? BIOME_WINTER : BIOME_CLASSIC;
        if (match_start(&game, &ms, NULL) != 0) { fclose(report); return -1; }
        if (scenario == 2) { game.map.weather = WEATHER_SNOWSTORM; game.map.has_weather = 1; }
        solo = scenario != 1; original_look = 1;
        reset_match_view();
        frame_copy_method = method; frame_profile.enabled = 1;
        start = net_ms();
        do {
            InputBits input[MAX_TANKS] = {0};
            uint32_t before = amiga_us(), sim, end, cost;
            { int bot; for (bot = 0; bot < 2; bot++) input[bot] = ai_think_local(&game, bot, 512); }
            game_tick(&game, input);
            sim = amiga_us() - before;
            draw_frame();
            end = amiga_us(); cost = end - before;
            sum[0] += sim; sum[1] += frame_profile.terrain_us; sum[2] += frame_profile.objects_us;
            sum[3] += frame_profile.hud_us; sum[4] += frame_profile.wait_us; sum[5] += frame_profile.copy_us;
            if (cost > worst) worst = cost;
            if (sim > sim_worst) sim_worst = sim;
            histogram[cost / 1000 < 1000 ? cost / 1000 : 1000]++;
            frames++; elapsed = net_ms() - start;
        } while (elapsed < 3000 && frames < 120);
        frame_profile.enabled = 0;
        while (p95 < 1000 && (seen += histogram[p95]) * 100 < frames * 95) p95++;
        last_fps = frames * 10000u / (elapsed ? elapsed : 1);
        fprintf(report, "%s %s: %u.%u FPS; worst %lu us; p95 %u ms; sim worst %lu us\n",
                method == 1 ? "LIBRARY" : method == 2 ? "BLITTER" : "CPU",
                scenario == 0 ? "SOLO" : scenario == 1 ? "SPLIT" : "SNOW COMBAT", last_fps / 10, last_fps % 10,
                (unsigned long)worst, p95, (unsigned long)sim_worst);
        fprintf(report, "  sim %lu terrain %lu objects %lu hud %lu wait %lu copy/present %lu\n",
                (unsigned long)(sum[0] / frames), (unsigned long)(sum[1] / frames), (unsigned long)(sum[2] / frames),
                (unsigned long)(sum[3] / frames), (unsigned long)(sum[4] / frames), (unsigned long)(sum[5] / frames));
        fprintf(report, "  free memory: Chip %lu bytes; Fast %lu bytes\n", (unsigned long)AvailMem(MEMF_CHIP), (unsigned long)AvailMem(MEMF_FAST));
        fflush(report);
    }
    fclose(report); frame_copy_method = 0; solo = saved_solo; original_look = saved_look;
    snprintf(online_status, sizeof online_status, "DETAILS SAVED TO BENCHMARK.TXT");
    return 0;
}

/* Run against a private test server, never automatically against the public listing. */
int run_stability_test(const char *host, int port, int cycles)
{
#ifdef OFP_ART_PREVIEW
    (void)host; (void)port; (void)cycles;
    puts("The native stack diagnostic requires AmigaOS.");
    return -1;
#else
    FILE *report = fopen("PROGDIR:stability.txt", "w");
    uint8_t key[32];
    ULONG base_chip = 0, base_fast = 0, low_margin = ~0UL;
    unsigned char stack_marker;
    unsigned char *stack_low, *stack_paint_end;
    int cycle, failed = 0;
    struct Task *task = FindTask(NULL);
    if (!report) return -1;
    if (cycles < 2) cycles = 2;
    if (cycles > 1000) cycles = 1000;
    /* This diagnostic owns the task. Paint only unused stack below a 4 KB guard;
     * the scan records the deepest use during the whole run, including nested calls. */
    stack_low = (unsigned char *)task->tc_SPLower;
    stack_paint_end = (unsigned char *)((ULONG)&stack_marker - 4096u);
    if (stack_paint_end > stack_low && stack_paint_end < (unsigned char *)task->tc_SPUpper)
        memset(stack_low, 0xa5, (size_t)(stack_paint_end - stack_low));
    else stack_paint_end = stack_low;
    net_random(key, sizeof key);
    fprintf(report, "OpenFP %s native stability test: %d cycles\n", OFP_VERSION, cycles);
    fprintf(report, "cycle joined running map-bytes chip-free fast-free sampled-stack-margin untouched-stack\n");
    for (cycle = 0; cycle < cycles; cycle++) {
        uint32_t start = net_ms(), running_at = 0;
        int joined = 0, reached_running = 0, loaded = 0, music_id;
        MatchSetup ms = {0};
        unsigned char marker;
        ULONG margin = (ULONG)&marker - (ULONG)task->tc_SPLower;
        if (margin < low_margin) low_margin = margin;
        if (netc_open(&online, host, (uint16_t)port, "AmigaStress", "", key, TANK_MEDIUM, 0) != 0) failed = 1;
        else while (net_ms() - start < 30000u) {
            int state = netc_step(&online);
            if (state == NC_FAILED) break;
            if (state >= NC_LOBBY && !joined) { joined = 1; netc_ready(&online, 1, TANK_MEDIUM); }
            loaded = (int)online.map_got;
            if (state == NC_RUNNING && !reached_running) { reached_running = 1; running_at = net_ms(); }
            if (reached_running && net_ms() - running_at >= 1000u) break;
            music_update(); Delay(1);
        }
        if (!joined || !reached_running) failed = 1;
        netc_close(&online); netc_close(&online); /* close is intentionally idempotent */
        ms.seed = 1987u + (unsigned)cycle; ms.biome = (uint8_t)(cycle % BIOME_COUNT);
        if (ms.biome == BIOME_SCIFI) ms.biome = BIOME_CLASSIC;
        ms.players = 2; ms.versus = 1; ms.team[1] = TEAM_ENEMY; ms.cls[0] = ms.cls[1] = TANK_MEDIUM;
        if (match_start(&game, &ms, NULL) != 0) failed = 1;
        solo = 1; original_look = 1; reset_match_view();
        music_id = ms.biome;
        music_volume(7); music_play(music_id);
        { int frame_no; for (frame_no = 0; frame_no < 4; frame_no++) { demo_tick(); music_update(); draw_frame(); } }
        music_play(-1); music_close(); wait_for_display();
        {
            ULONG chip = AvailMem(MEMF_CHIP), fast = AvailMem(MEMF_FAST);
            unsigned char *untouched = stack_low;
            while (untouched < stack_paint_end && *untouched == 0xa5) untouched++;
            /* The first cycle warms sockets, graphics and timer libraries. */
            if (cycle == 0) { base_chip = chip; base_fast = fast; }
            fprintf(report, "%d %d %d %d %lu %lu %lu %lu\n", cycle + 1, joined, reached_running, loaded,
                    (unsigned long)chip, (unsigned long)fast, (unsigned long)margin,
                    (unsigned long)(untouched - stack_low));
            fflush(report);
        }
        if (failed) break;
    }
    fprintf(report, "finished %d/%d; chip delta %ld; fast delta %ld; minimum sampled stack margin %lu; %s\n",
            cycle < cycles ? cycle + 1 : cycle, cycles,
            (long)(AvailMem(MEMF_CHIP) - base_chip), (long)(AvailMem(MEMF_FAST) - base_fast),
            (unsigned long)low_margin, failed ? "FAILED" : "COMPLETED (inspect memory trend)");
    fclose(report);
    return failed ? -1 : 0;
#endif
}

/* TRAINING: the training ground, one player, nothing that fights back (not in the profile's record) */
static void training_start(void)
{
    MatchSetup ms;
    const char *ground = training_ground();
    int k;
    training_setup(&ms);
    ms.relaxed_pace = 1;
    ms.cls[0] = (uint8_t)tank_chassis(cls_choice);
    ms.steer[0] = (uint8_t)menu_steer;
    if (!ground[0] || match_start(&game, &ms, ground) != 0) { strcpy(online_status, "Could not start the training."); return; }
    training_clean_up(&game);
    for (k = 0; k < game.num_props; k++) game.props[k].moving = 0;   /* (no patrols either) */
    training_begin(&tr, &game);
    training_on = 1;
    original_look = 1;
    solo = 1;
    recorded = 1;                                        /* (practice: not in the record) */
    reset_match_view();
    local_playing = 1;
    local_tick = local_draw = net_ms();
    memset(keys, 0, sizeof keys);
    ModifyIDCMP(window, RAW_EVENTS);
}

DesertOperation operation;
static int selected_challenge = -1;

int start_desert_operation(void)
{
    MatchSetup crew = {0};
    crew.players = signed_profile[1] == SIGNED_OUT ? 1 : 2;
    crew.steer[0] = (uint8_t)menu_steer;
    crew.steer[1] = (uint8_t)profile_steering(1);
    crew.relaxed_pace = 1;
    if ((selected_challenge < 0 ? operation_start(&game, &operation, &crew) :
         operation_challenge_start(&game, &operation, &crew, selected_challenge)) != 0) {
        strcpy(online_status, "COULD NOT START SAND BRIDGE");
        return -1;
    }
    solo = crew.players == 1;
    original_look = training_on = 0;
    recorded = 1;
    reset_match_view();
    local_playing = 1;
    local_tick = local_draw = net_ms();
    memset(keys, 0, sizeof keys);
    ModifyIDCMP(window, RAW_EVENTS);
    return 0;
}

void menu_accept(void)
{
    if (profile_menu_active()) {
        profile_accept();
    } else if (menu_page == MENU_MAIN) {
        switch (menu_row) {
        case MAIN_PLAY: if (signed_profile[0] == SIGNED_OUT) open_signin(0, MENU_PLAY); else menu_change(MENU_PLAY); break;
        case MAIN_TOOLS: menu_change(MENU_TOOLS); break;
        case MAIN_SETTINGS: menu_change(MENU_SETTINGS); break;
        case MAIN_ABOUT: menu_change(MENU_ABOUT); break;
        case MAIN_QUIT: quit = 1; break;
        case MAIN_P1: case MAIN_P2: main_profile_action(menu_row - MAIN_P1); break;
        }
    } else if (menu_page == MENU_PLAY) {
        switch (menu_row) {
        case 0: menu_change(MENU_LOCAL); break;
        case 1:
            if (signed_profile[0] < 0) {
                open_signin(0, MENU_PLAY);
                strcpy(online_status, "CHOOSE A PROFILE FOR ONLINE PLAY");
            } else menu_change(MENU_NETWORK);
            break;
        case 2: training_start(); break;
        case 3: menu_change(MENU_CHALLENGES); break;
        case 4: menu_change(MENU_MAIN); break;
        }
    } else if (menu_page == MENU_CHALLENGES) {
        if (menu_row < 3) { selected_challenge = menu_row; start_desert_operation(); }
        else menu_change(MENU_PLAY);
    } else if (menu_page == MENU_TOOLS) {
        switch (menu_row) {
        case TOOL_REPLAYS: menu_change(MENU_REPLAYS); list_replays(); break;
        case TOOL_CONTROLLERS: menu_change(MENU_DIAGNOSTICS); break;
        case TOOL_EDITOR: editor_start(); break;
        case TOOL_BENCHMARK:
            strcpy(online_status, "RUNNING BENCHMARK...");
            online_menu();
            run_detailed_benchmark();
            break;
        case TOOL_BACK: menu_change(MENU_MAIN); break;
        }
    } else if (menu_page == MENU_DIAGNOSTICS) { if (menu_row) menu_change(MENU_TOOLS);
    } else if (menu_page == MENU_SETTINGS) {
        switch (menu_row) {
        case SET_GAME: menu_change(MENU_OPTIONS); break;
        case SET_GRAPHICS: menu_change(MENU_GRAPHICS); break;
        case SET_AUDIO: menu_change(MENU_AUDIO); break;
        case SET_CONTROLS: menu_change(MENU_CONTROLS); break;
        case SET_BACK: menu_change(MENU_MAIN); break;
        }
    } else if (menu_page == MENU_REPLAYS) {
        if (menu_row < nreplays) watch_start(replay_names[menu_row]);
        else menu_change(MENU_TOOLS);
    } else if (menu_page == MENU_ABOUT) {
        menu_change(MENU_MAIN);
        menu_row = MAIN_ABOUT;
    } else if (menu_page == MENU_LOCAL) {
        if (menu_row == LG_NEXT) {
            if (signed_profile[0] == SIGNED_OUT) open_signin(0, MENU_LOCAL);
            else menu_change(MENU_TEAMS);
        }
        else if (menu_row == LG_BACK) menu_change(MENU_PLAY);
        else menu_side_step(1);
    } else if (menu_page == MENU_TEAMS) {
        if (!team_ready[0]) return;
        if (menu_row == TM_START) {
            if (settings.players == 2 && !team_ready[1]) return;
            settings.cls = cls_choice = team_tank[0];
            save_menu_settings();
            local_begin(settings.players != 2, host_side, settings.players == 2 ? team_tank[1] : TANK_MEDIUM);
        }
        else if (menu_row == TM_BACK) menu_change(MENU_LOCAL);
        else menu_side_step(1);
    } else if (menu_page == MENU_NETWORK) {
        network_accept();
    } else if (menu_page == MENU_CONTROLS || menu_page == MENU_KEYMAP) {
        controls_accept();
    } else if (menu_page >= MENU_OPTIONS && menu_page <= MENU_AUDIO) {
        if (menu_row == menu_rows() - 1) settings_back();
        else menu_side_step(1);
    }
}

enum { TITLE_LOGO_ROW = 0, TITLE_PROMPT_ROW = 24 };

static void make_title_mask(const char *text, int row, int scale)
{
    int stride = frame->BytesPerRow;
    UBYTE *mask = frame->Planes[0] + row * stride;
    int i, x, y;
    memset(mask, 0, stride * 8 * scale);
    for (i = 0; text[i]; i++)
        for (y = 0; y < 8 * scale; y++) {
            UBYTE bits = font8x8_basic[(unsigned char)text[i]][y / scale];
            for (x = 0; x < 8 * scale; x++) {
                int dx = i * 8 * scale + x;
                if (bits & (1u << (x / scale)))
                    mask[y * stride + dx / 8] |= 0x80 >> (dx & 7);
            }
        }
}

static void draw_title_mask(int row, int y, int width, int height, int pen)
{
    int stride = frame->BytesPerRow, x = (WIDTH - width) / 2;
    PLANEPTR mask = frame->Planes[0] + row * stride;
    SetAPen(&presentation, INK);
    BltTemplate(mask, 0, stride, &presentation, x + 2, y + 2, width, height);
    SetAPen(&presentation, pen);
    BltTemplate(mask, 0, stride, &presentation, x, y, width, height);
}

void draw_title_overlay(void)
{
    static UWORD dim_pattern[] = { 0xeeee, 0xbbbb };
    struct BitMap *saved = canvas.BitMap;
    SetDrMd(&presentation, JAM1);
    SetAPen(&presentation, INK);
    presentation.AreaPtrn = dim_pattern;
    presentation.AreaPtSz = 1;
    RectFill(&presentation, 0, 0, WIDTH - 1, 63);
    RectFill(&presentation, 0, 210, WIDTH - 1, HEIGHT - 1);
    presentation.AreaPtrn = NULL;
    presentation.AreaPtSz = 0;
    draw_title_mask(TITLE_LOGO_ROW, 12, 240, 24, GOLD);
    draw_title_mask(TITLE_PROMPT_ROW, 217, 160, 16, (game.tick / 32) % 2 ? TEAM_ONE : GOLD);
    canvas.BitMap = presentation.BitMap;
    local_center(160, 53, WHITE, "AN OPEN REMAKE OF THE 1987 CLASSIC");
    local_center(160, 237, WHITE, "ENTER OR JOYSTICK FIRE");
    local_center(160, 248, 5, "ORIGINAL GAME BY SILENT SOFTWARE, 1987");
    canvas.BitMap = saved;
}

void title_start(void)
{
    MatchSetup ms = { 0 };
    ms.seed = 1987;
    ms.players = 2;
    ms.versus = 1;
    ms.team[1] = TEAM_ENEMY;
    ms.cls[0] = TANK_MEDIUM;
    ms.cls[1] = TANK_MEDIUM;
    ms.bot[0] = ms.bot[1] = light_local_ai() ? 0 : AI_HARD;
    ms.camp_garages = 3;
    original_look = 0;
    title_on = match_start(&game, &ms, classic_dust_crossing) == 0;
    if (!title_on) { online_dirty = 1; return; }
    reset_field_bots();
    WaitBlit();
    make_title_mask("OPENFP", TITLE_LOGO_ROW, 3);
    make_title_mask("PRESS FIRE", TITLE_PROMPT_ROW, 2);
    solo = 1;
    reset_match_view();
    local_draw = net_ms();
}

void title_step(uint32_t now)
{
    if ((int32_t)(now - local_draw) < 0) return;
    if (game.winner >= 0 || game.tick >= 1500) title_start();
    if (!title_on) return;
    demo_tick();
    demo_tick();
    draw_frame();
    local_draw = now + ONLINE_DRAW_MS;
}
