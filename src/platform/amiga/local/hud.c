/* SPDX-License-Identifier: MIT
 * src/platform/amiga/local/hud.c: text, the panels below the views, the radar, the palette (day, dusk, night, storm), sounds and music, presenting a frame.
 * A part of the Amiga client, its own translation unit: what the parts share is in internal.h. */
#include <stdlib.h>
#include "platform/amiga/local/internal.h"
#include "common/fx_budget.h"

void label(int x, int y, int pen, const char *text)
{
    SetAPen(&canvas, pen);
    Move(&canvas, x, y);
    Text(&canvas, (STRPTR)text, strlen(text));
}

void fill(int pen, int x0, int y0, int x1, int y1)
{
    SetAPen(&canvas, pen);
    RectFill(&canvas, x0, y0, x1, y1);
}

void inset(int x, int y, int width, int height, int rim)
{
    fill(rim, x, y, x + width - 1, y + height - 1);
    fill(TRACK, x + 1, y + 1, x + width - 1, y + height - 1);
    fill(INK, x + 2, y + 2, x + width - 2, y + height - 2);
}

#ifdef OFP_AMIGA_ONLINE
int classic_solo_view(void)
{
    return solo && original_look && !editor_on && !title_on && !training_on && !replay_on;
}
#endif

void hud_radar_origin(int panel, int *x, int *y)
{
#ifdef OFP_AMIGA_ONLINE
    if (classic_solo_view()) {
        *x = CLASSIC_VIEW + 24;
        *y = 147;
        return;
    }
#endif
    *x = hud_left(panel) + RADAR_X;
    *y = RADAR_Y;
}

static int hud_tank(int panel)
{
#ifdef OFP_AMIGA_ONLINE
    if (online_playing) return online_view_tank();
    if (replay_on && replay_follow < game.num_tanks) return replay_follow;
#endif
    return panel < game.num_tanks ? panel : 0;
}

/* Five-pixel silhouettes for the compact HUD. */
enum { HUD_TANK, HUD_PERSON, HUD_MINE, HUD_FLAG };
static void hud_icon(int x, int y, int pen, int icon)
{
    static const UBYTE rows[][7] = {
        { 4, 4, 31, 21, 31, 31, 17 },
        { 4, 4, 31, 4, 14, 10, 17 },
        { 4, 4, 14, 31, 31, 14, 0 },
        { 31, 17, 31, 16, 16, 16, 16 }
    };
    int row, col;
    SetAPen(&canvas, pen);
    for (row = 0; row < 7; row++)
        for (col = 0; col < 5; col++)
            if (rows[icon][row] & (16 >> col)) WritePixel(&canvas, x + col, y + row);
}

static void start_hud(void)
{
    int i;
#ifdef OFP_AMIGA_ONLINE
    if (classic_solo_view()) {
        int left = CLASSIC_VIEW, rim = game.tanks[0].team & 1 ? TEAM_TWO : TEAM_ONE;
        fill(INK, 0, BOTTOM, WIDTH - 1, HEIGHT - 1);
        fill(TRACK, 0, BOTTOM, WIDTH - 1, BOTTOM + 2);
        inset(76, BOTTOM + 11, 168, 24, rim);
        label(136, BOTTOM + 26, GOLD, "OPENFP");
        fill(TRACK, left, 0, WIDTH - 1, BOTTOM - 1);
        fill(rim, left + 1, 1, WIDTH - 2, BOTTOM - 2);
        fill(4, left + 3, 3, WIDTH - 4, BOTTOM - 4);
        inset(left + 10, 9, 76, 15, rim);
        inset(left + 10, 27, 76, 17, rim);
        inset(left + 10, 51, 76, 16, rim);
        inset(left + 10, 72, 76, 16, rim);
        inset(left + 10, 94, 76, 18, rim);
        inset(left + 10, 117, 76, 18, rim);
        inset(left + 22, 145, RADAR_SIZE + 4, RADAR_SIZE + 4, rim);
        label(left + 28, 20, rim, "SCORE");
        hud_icon(left + 17, 55, rim, HUD_TANK);
        hud_icon(left + 17, 76, rim, HUD_PERSON);
        label(left + 16, 103, rim, "FUEL");
        label(left + 16, 126, rim, "ARMOR");
        for (int y = 8; y < BOTTOM - 3; y += 12) {
            fill(rim, left + 5, y, left + 6, y + 1);
            fill(rim, WIDTH - 7, y, WIDTH - 6, y + 1);
        }
        return;
    }
#endif
    fill(INK, 0, BOTTOM, WIDTH - 1, HEIGHT - 1);
    for (i = 0; i < (solo ? 1 : 2); i++) {
        int left = hud_left(i);
        int rim = (game.tanks[hud_tank(i)].team & 1) ? TEAM_TWO : TEAM_ONE;
        fill(TRACK, left, BOTTOM, left + VIEW - 1, HEIGHT - 1);
        fill(rim, left + 1, BOTTOM, left + VIEW - 2, HEIGHT - 1);
        fill(4, left + 3, BOTTOM + 2, left + VIEW - 4, HEIGHT - 3);
        inset(left + 6, BOTTOM + 4, 103, 12, rim);
        inset(left + 6, BOTTOM + 17, 24, 12, rim);
        inset(left + 31, BOTTOM + 17, 51, 12, rim);
        inset(left + 83, BOTTOM + 17, 26, 12, rim);
        hud_icon(left + 8, BOTTOM + 19, rim, HUD_TANK);
        hud_icon(left + 33, BOTTOM + 19, rim, HUD_PERSON);
        inset(left + 6, BOTTOM + 30, 103, 9, rim);
        inset(left + 6, BOTTOM + 40, 103, 9, rim);
        inset(left + RADAR_X - 2, RADAR_Y - 2, RADAR_SIZE + 4, RADAR_SIZE + 4, rim);
        {
            const char *who = i == 0 ? "P1" : "P2";
#ifdef OFP_AMIGA_ONLINE
            if (online_playing || replay_on) who = "VIEW";
#endif
            label(left + 10, BOTTOM + 13, rim, who);
        }
        label(left + 10, BOTTOM + 37, WHITE, "FUEL");
        label(left + 10, BOTTOM + 47, WHITE, "ARMOR");
    }
}

static int radar_offset(fx position, int extent)
{
    int start = fx_to_int(position) / TILE_SIZE - RADAR_SIZE / 2;
    if (start > extent - RADAR_SIZE) start = extent - RADAR_SIZE;
    return start < 0 ? 0 : start;
}

static void radar_dot(UBYTE radar[RADAR_SIZE][RADAR_SIZE], int x, int y, int pen)
{
    if ((unsigned)x < RADAR_SIZE && (unsigned)y < RADAR_SIZE) radar[y][x] = (UBYTE)pen;
}

static int update_radar(void)
{
    static UBYTE current[RADAR_SIZE][RADAR_SIZE];
    static UBYTE previous[2][RADAR_SIZE][RADAR_SIZE];
    static const uint8_t pens[TILE_COUNT] = {
        [TILE_ICE] = 5, [TILE_OIL] = 4, [TILE_ROAD] = 4, [TILE_WALL] = 5, [TILE_BUILDING] = 5, [TILE_FLOOR] = 4
    };
    int panel, changed = 0;
    for (panel = 0; panel < (solo ? 1 : 2); panel++) {
        int ti = hud_tank(panel), x, y, i, left, top;
        const Tank *me = &game.tanks[ti];
        int tx0 = radar_offset(me->x, game.map.w), ty0 = radar_offset(me->y, game.map.h);
        hud_radar_origin(panel, &left, &top);
        if (detailed_radar) {
            for (y = 0; y < RADAR_SIZE; y++) {
                int ty = ty0 + y;
                for (x = 0; x < RADAR_SIZE; x++) {
                    int tx = tx0 + x, pen = 0;
                    if (tx < game.map.w && ty < game.map.h)
                        pen = game.map.sid[ty][tx] ? 5 : pens[game.map.type[ty][tx]];
                    current[y][x] = pen ? (UBYTE)pen : PANEL;
                }
            }
        } else memset(current, INK, sizeof current);
        for (i = 0; i < game.num_tanks; i++) {
            const Tank *t = &game.tanks[i];
            int pen = (t->team & 1) ? TEAM_TWO : TEAM_ONE;
            if (i == ti || !t->active || t->out || t->respawn) continue;
            x = fx_to_int(t->x) / TILE_SIZE - tx0;
            y = fx_to_int(t->y) / TILE_SIZE - ty0;
            if ((unsigned)x >= RADAR_SIZE || (unsigned)y >= RADAR_SIZE) continue;
            radar_dot(current, x, y, pen);
            radar_dot(current, x - 1, y, pen);
        }
        if (me->active && !me->out) {
            int pen = (me->team & 1) ? TEAM_TWO : TEAM_ONE;
            x = fx_to_int(me->x) / TILE_SIZE - tx0;
            y = fx_to_int(me->y) / TILE_SIZE - ty0;
            radar_dot(current, x, y, pen);
            radar_dot(current, x - 1, y, pen); radar_dot(current, x + 1, y, pen);
            radar_dot(current, x, y - 1, pen); radar_dot(current, x, y + 1, pen);
        }
        if (game.extras & EXTRA_CONQUEST) {
            for (i = 0; i < game.num_cp; i++) {
                int dy, owner = game.cp_owner[i];
                int pen = owner < 0 ? WHITE : owner == TEAM_PLAYER ? TEAM_ONE : TEAM_TWO;
                x = game.cp_tx[i] - tx0;
                y = game.cp_ty[i] - ty0;
                if ((unsigned)x >= RADAR_SIZE || (unsigned)y >= RADAR_SIZE) continue;
                /* A hollow diamond keeps a tank on the checkpoint visible. */
                for (dy = -2; dy <= 2; dy++) {
                    int dx = 2 - abs(dy);
                    radar_dot(current, x - dx, y + dy, pen);
                    if (dx) radar_dot(current, x + dx, y + dy, pen);
                }
            }
        }
        if (!game.map.arena && !(game.extras & (EXTRA_NOFLAGS | EXTRA_KILLCONF | EXTRA_HARDPOINT))) {
            for (i = 0; i < 2; i++) {
                const Flag *flag = &game.flags[i];
                int pen = i == TEAM_PLAYER ? TEAM_ONE : TEAM_TWO;
                x = fx_to_int(flag->x) / TILE_SIZE - tx0;
                y = fx_to_int(flag->y) / TILE_SIZE - ty0;
                if ((unsigned)x >= RADAR_SIZE || (unsigned)y >= RADAR_SIZE) continue;
                radar_dot(current, x, y - 1, WHITE);
                radar_dot(current, x, y, WHITE);
                radar_dot(current, x, y + 1, WHITE);
                radar_dot(current, x + 1, y - 1, pen);
                radar_dot(current, x + 2, y - 1, pen);
                radar_dot(current, x + 1, y, pen);
            }
        }
#ifdef OFP_AMIGA_ONLINE
        if (online_playing && game_orders_allowed(&game)) {
            int team = me->team & 1;
            game.team_order[team] = online.team_order;
            if (online.team_order.kind) game.team_leader[team] = (int8_t)online.team_order.issuer;
        }
#endif
        if (game_order_active(&game, me->team & 1)) {
            const TeamOrder *o = &game.team_order[me->team & 1];
            int bx = (o->kind == ORDER_ESCORT ? fx_to_int(game.tanks[o->issuer].x) / TILE_SIZE : o->tx) - tx0;
            int by = (o->kind == ORDER_ESCORT ? fx_to_int(game.tanks[o->issuer].y) / TILE_SIZE : o->ty) - ty0;
            for (i = -3; i <= 3; i++) { radar_dot(current, bx + i, by, WHITE); radar_dot(current, bx, by + i, WHITE); }
        }
        /* Convert only changed groups of eight pixels. The radar starts on a
         * byte boundary, so each write replaces a whole group in each plane. */
        for (y = 0; y < RADAR_SIZE; y++) {
            if (hud_ready && !memcmp(current[y], previous[panel][y], RADAR_SIZE)) continue;
            if (!changed) WaitBlit();
            changed = 1;
            for (x = 0; x < RADAR_SIZE; x += 8) {
                int at = (top + y) * frame->BytesPerRow + (left + x) / 8, plane;
                for (plane = 0; plane < DEPTH; plane++) {
                    int mask = 1 << plane, p;
                    UBYTE row = 0;
                    for (p = 0; p < 8; p++) row = (UBYTE)((row << 1) | !!(current[y][x + p] & mask));
                    frame->Planes[plane][at] = row;
                }
            }
            memcpy(previous[panel][y], current[y], RADAR_SIZE);
        }
    }
    return changed;
}

static void gauge(int left, int y, int width, int pen)
{
    fill(INK, left, y, left + GAUGE_WIDTH - 1, y + 3);
    if (width > 0) fill(pen, left, y, left + width - 1, y + 3);
}

#ifdef OFP_AMIGA_ONLINE
static int draw_classic_hud(void)
{
    const Tank *tank = &game.tanks[0];
    HudState *old = &hud[0];
    int left = CLASSIC_VIEW, changed = !hud_ready;
    int fuel = (unsigned)tank->fuel * GAUGE_WIDTH / (world_tank_stats(game.vehicle_world, (TankClass)tank->cls)->fuel * TICK_RATE);
    int armor = (unsigned)tank->armor * GAUGE_WIDTH / game_armor_max(&game, 0);
    char text[16];
    if (!hud_ready) start_hud();
    if (fuel > GAUGE_WIDTH) fuel = GAUGE_WIDTH;
    if (armor > GAUGE_WIDTH) armor = GAUGE_WIDTH;
    if (!hud_ready || old->score != tank->score) {
        snprintf(text, sizeof text, "%06lu", (unsigned long)(tank->score % 1000000u));
        fill(INK, left + 19, 31, left + 75, 40);
        label(left + 24, 39, GOLD, text);
        old->score = tank->score;
        changed = 1;
    }
    if (!hud_ready || old->lives != tank->lives || old->mines != tank->mines || old->has_flag != tank->has_flag) {
        snprintf(text, sizeof text, tank->lives == 255 ? "INF" : "%u", (unsigned)tank->lives);
        fill(INK, left + 26, 54, left + 81, 63);
        label(left + 27, 62, GOLD, text);
        hud_icon(left + 49, 55, tank->has_flag ? GOLD : 5, tank->has_flag ? HUD_FLAG : HUD_MINE);
        snprintf(text, sizeof text, "%02u", (unsigned)tank->mines);
        label(left + 57, 62, GOLD, text);
        old->lives = tank->lives; old->mines = tank->mines; old->has_flag = tank->has_flag;
        changed = 1;
    }
    if (!hud_ready || old->pows != tank->pows) {
        snprintf(text, sizeof text, "%u/%u", (unsigned)tank->pows, (unsigned)world_tank_stats(game.vehicle_world, (TankClass)tank->cls)->pows);
        fill(INK, left + 26, 75, left + 81, 84);
        label(left + 28, 83, GOLD, text);
        old->pows = tank->pows;
        changed = 1;
    }
    if (!hud_ready || old->fuel != fuel) { gauge(left + 16, 105, fuel, 5); changed = 1; }
    if (!hud_ready || old->armor != armor) { gauge(left + 16, 128, armor, TEAM_TWO); changed = 1; }
    old->fuel = fuel;
    old->armor = armor;
    changed |= update_radar();
    hud_ready = 1;
    return changed;
}
#endif

static int draw_hud(void)
{
    int i, changed = !hud_ready;
#ifdef OFP_AMIGA_ONLINE
    if (classic_solo_view()) return draw_classic_hud();
#endif
    if (!hud_ready) start_hud();
    for (i = 0; i < (solo ? 1 : 2); i++) {
        int ti = hud_tank(i);
        const Tank *tank = &game.tanks[ti];
        HudState *old = &hud[i];
        int left = hud_left(i), pows_max = world_tank_stats(game.vehicle_world, (TankClass)tank->cls)->pows;
        int fuel = (unsigned)tank->fuel * GAUGE_WIDTH / (world_tank_stats(game.vehicle_world, (TankClass)tank->cls)->fuel * TICK_RATE);
        int armor = (unsigned)tank->armor * GAUGE_WIDTH / game_armor_max(&game, ti);
        char text[16];
        if (fuel > GAUGE_WIDTH) fuel = GAUGE_WIDTH;
        if (armor > GAUGE_WIDTH) armor = GAUGE_WIDTH;
        if (!hud_ready || old->score != tank->score) {
            changed = 1;
            snprintf(text, sizeof text, "%06lu", (unsigned long)(tank->score % 1000000u));
            fill(INK, left + 52, BOTTOM + 6, left + 105, BOTTOM + 14);
            label(left + 54, BOTTOM + 13, GOLD, text);
            old->score = tank->score;
        }
        if (!hud_ready || old->lives != tank->lives) {
            changed = 1;
            snprintf(text, sizeof text, tank->lives == 255 ? "INF" : "%u", (unsigned)tank->lives);
            fill(INK, left + 13, BOTTOM + 19, left + 28, BOTTOM + 27);
            label(left + 13, BOTTOM + 26, GOLD, text);
            old->lives = tank->lives;
        }
        if (!hud_ready || old->pows != tank->pows || old->pows_max != pows_max) {
            changed = 1;
            snprintf(text, sizeof text, "%u/%u", (unsigned)tank->pows, (unsigned)pows_max);
            fill(INK, left + 39, BOTTOM + 19, left + 80, BOTTOM + 27);
            label(left + 39, BOTTOM + 26, GOLD, text);
            old->pows = tank->pows;
            old->pows_max = pows_max;
        }
        if (!hud_ready || old->mines != tank->mines || old->has_flag != tank->has_flag) {
            changed = 1;
            snprintf(text, sizeof text, "%02u", (unsigned)tank->mines);
            fill(INK, left + 85, BOTTOM + 19, left + 107, BOTTOM + 27);
            hud_icon(left + 85, BOTTOM + 19, tank->has_flag ? GOLD : 5,
                     tank->has_flag ? HUD_FLAG : HUD_MINE);
            label(left + 91, BOTTOM + 26, GOLD, text);
            old->mines = tank->mines;
            old->has_flag = tank->has_flag;
        }
        if (!hud_ready || old->fuel != fuel) {
            changed = 1;
            gauge(left + 54, BOTTOM + 33, fuel, 5);
        }
        if (!hud_ready || old->armor != armor) {
            changed = 1;
            gauge(left + 54, BOTTOM + 43, armor, TEAM_TWO);
        }
        old->fuel = fuel;
        old->armor = armor;
    }
    changed |= update_radar();
    hud_ready = 1;
    return changed;
}

void wait_for_display(void)
{
    if (!swap_pending) return;
    while (!GetMsg(safe_port)) WaitPort(safe_port);
    while (!GetMsg(display_port)) WaitPort(display_port);
    swap_pending = 0;
}

uint32_t presents;                                    /* (frames shown: a gap means another screen came between) */
void present_frame(void)
{
    presents++;
    WaitBlit();
    if (ChangeScreenBuffer(screen, display_buffers[back_buffer])) {
        swap_pending = 1;
        back_buffer ^= 1;
    }
}

int music_on = 1;
void update_music(int in_match)
{
    int menu_song = SOUNDTRACK_MENU;
#ifdef OFP_AMIGA_ONLINE
    if (menu_page == MENU_ABOUT) menu_song = SOUNDTRACK_CREDITS;
#endif
    int want = !music_on || in_match < 0 ? -1 : in_match ?
        (game.biome < BIOME_COUNT ? game.biome : BIOME_CLASSIC) : menu_song;
    if (want != music_song()) music_play(want);
    music_update();
}

/* the match's new effects, heard: shots, hits, explosions, soldiers; quieter the further from our tank */
int paused;                            /* (below, with the menus: the pause page is open) */
static void match_sounds(void)
{
    static uint16_t seen[MAX_EFFECTS];
    static uint16_t bonus_seen[MAX_TANKS][2];
    static uint8_t laughed[MAX_TANKS];
    static uint32_t last_tick;
    int i, me_i = 0, listeners = solo ? 1 : 2, engine = 0;
#ifdef OFP_AMIGA_ONLINE
    if (online_playing) me_i = online_view_tank();
    if (replay_on && replay_follow < game.num_tanks) me_i = replay_follow;
#endif
    for (i = 0; i < listeners && me_i + i < game.num_tanks; i++) {
        const Tank *tank = &game.tanks[me_i + i];
        fx top = world_tank_stats(game.vehicle_world, (TankClass)tank->cls)->max_fwd;
        fx sp = tank->speed < 0 ? -tank->speed : tank->speed;
        int step = !tank->active || tank->respawn || sp == 0 || paused ? 0 :
                   sp * 3 < top ? 1 : sp * 3 < top * 2 ? 2 : 3;
        if (step > engine) engine = step;
    }
    sound_engine(engine);   /* one Paula channel, using the faster local tank */
    if (game.tick < last_tick) {
        memset(seen, 0, sizeof seen);
        memset(laughed, 0, sizeof laughed);
        memset(bonus_seen, 0, sizeof bonus_seen);
        namarks = amark_next = 0;
    }
    last_tick = game.tick;
    for (i = 0; i < listeners && me_i + i < game.num_tanks; i++) {
        int k, ti = me_i + i;
        for (k = 0; k < 2; k++) {
            uint16_t ticks = k ? game.tanks[ti].rapid_ticks : game.tanks[ti].boost_ticks;
            if (ticks > bonus_seen[ti][k]) sound_powerup(k);
            bonus_seen[ti][k] = ticks;
        }
    }
    for (i = 0; i < MAX_EFFECTS; i++) {
        const Effect *e = &game.effects[i];
        int id, p, d = 24, vol;
        if (!e->active || e->delay || e->serial == seen[i]) continue;
        seen[i] = e->serial;
        marks_add(e);
        id = e->kind == EFF_MUZZLE ? SND_CANNON : e->kind == EFF_SPARK ? SND_HIT : e->kind == EFF_EXPLOSION ? SND_EXPLODE_SMALL
           : e->kind == EFF_BLAST ? SND_EXPLODE : e->kind == EFF_SPLAT ? SND_SPLAT : e->kind == EFF_CHEER ? SND_PICKUP : -1;
        if (id < 0) continue;
        for (p = 0; p < listeners && me_i + p < game.num_tanks; p++) {
            const Tank *tank = &game.tanks[me_i + p];
            int dx = (fx_to_int(e->x) - fx_to_int(tank->x)) / TILE_SIZE;
            int dy = (fx_to_int(e->y) - fx_to_int(tank->y)) / TILE_SIZE;
            int distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            if (distance < d) d = distance;
        }
        vol = d >= 24 ? 0 : 64 - d * 64 / 24;
        if (id == SND_CANNON) vol /= 2;                /* (shots: many, softer) */
        sound_play(id, vol);
    }
#ifdef OFP_AMIGA_ONLINE
    if (replay_on || (!local_playing && !online_playing)) return;
#endif
    for (i = me_i; i < me_i + listeners && i < game.num_tanks; i++) {
        const Tank *tank = &game.tanks[i];
        int elapsed = RESPAWN_TICKS - tank->respawn;
        if (!tank->respawn) laughed[i] = 0;
        else if (!laughed[i] && (tank->out || (elapsed >= SKULL_START && elapsed < SKULL_END))) {
            laughed[i] = 1;
            sound_play(SND_SKULL_LAUGH, 52);
        }
    }
}

/* the time of day and the weather, in the screen's palette: dusk warmer and darker, night darker and
 * bluer, a storm a little dimmer (the screen's colours: the HUD's too) */
int palette_shown = PALETTE_DIRTY, colour_blind;
/* the screen's colours as set (0xRRGGBB): the classic palette, the green side blue when colour-blind
 * (OPTIONS: SIDES); with AGA also the sides' tank shades, the shared art's and the world's */
void base_palette(uint32_t pal[COLOURS])
{
    int i;
    for (i = 0; i < 16; i++) {
        unsigned c = classic_palette[i];
        pal[i] = (uint32_t)((c >> 8 & 15) * 17) << 16 | (uint32_t)((c >> 4 & 15) * 17) << 8 | (uint32_t)((c & 15) * 17);
    }
#if !defined(OFP_AMIGA_AGA) && !defined(OFP_LOCAL_DUEL)
    for (i = 0; i < 2; i++) {                            /* pens 1, 2: the world's ground (tools/amiga/amiga_palette.py) */
        unsigned c = world_ground_pens[art_world][i];
        pal[1 + i] = (uint32_t)((c >> 8 & 15) * 17) << 16 | (uint32_t)((c >> 4 & 15) * 17) << 8 | (uint32_t)((c & 15) * 17);
    }
#endif
#ifdef OFP_AMIGA_AGA
    const uint32_t *terrain_palette = aga_world_palette[art_world];
#ifdef OFP_AMIGA_ONLINE
    if (art_world == BIOME_CLASSIC && original_look) terrain_palette = aga_original_palette;
#endif
    for (i = 16; i < AGA_WORLD_FIRST; i++) pal[i] = aga_palette[i];
    for (i = AGA_WORLD_FIRST; i < COLOURS; i++) pal[i] = terrain_palette[i - AGA_WORLD_FIRST];
    if (colour_blind) {                                  /* (the fortress's tank shades, blue) */
        static const uint8_t blind_pens[3] = AGA_BLIND_PENS;
        for (i = 0; i < 3; i++) pal[blind_pens[i]] = aga_blind_ramp[i];
    }
#endif
    if (colour_blind) pal[TEAM_TWO] = 0x44AAFF;             /* (orange against blue: told apart by everyone) */
}
/* to the screen: 24 bits with AGA (LoadRGB32), 12 without */
void show_palette(const uint32_t pal[COLOURS])
{
    int i;
#ifdef OFP_AMIGA_AGA
    static ULONG table[1 + 3 * COLOURS + 1];
    table[0] = (ULONG)COLOURS << 16;
    for (i = 0; i < COLOURS; i++) {
        table[1 + 3 * i] = (pal[i] >> 16 & 255) * 0x01010101u;
        table[2 + 3 * i] = (pal[i] >> 8 & 255) * 0x01010101u;
        table[3 + 3 * i] = (pal[i] & 255) * 0x01010101u;
    }
    table[1 + 3 * COLOURS] = 0;
    LoadRGB32(&screen->ViewPort, table);
#else
    UWORD four[COLOURS];
    for (i = 0; i < COLOURS; i++)
        four[i] = (UWORD)(((pal[i] >> 20) & 15) << 8 | ((pal[i] >> 12) & 15) << 4 | ((pal[i] >> 4) & 15));
    LoadRGB4(&screen->ViewPort, four, COLOURS);
#endif
}
static void world_palette(void)
{
    int shown = palette_shown;
    int time = game.map.time, weather = weather_visual((Biome)game.biome, game.weather);
    uint32_t tick = game.tick;
#ifdef OFP_AMIGA_ONLINE
    if (editor_on) { time = editor_time_preview(); tick = net_ms() / 20; }
    else if (settings.weather_off) weather = WEATHER_CLEAR;
#endif
    int storm = weather == WEATHER_STORM, fog = weather == WEATHER_FOG;
    int sand = weather == WEATHER_SANDSTORM, eruption = weather == WEATHER_ERUPTION;
    int snowstorm = weather == WEATHER_SNOWSTORM;
    int flash = storm && weather_lightning(tick);
    int want = (time * 2 + storm) + 8 * art_world, i;
    if (flash) want += 16 * BIOME_COUNT;
    if (fog) want += 32 * BIOME_COUNT;
    if (sand) want += 64 * BIOME_COUNT;
    if (eruption) want += 128 * BIOME_COUNT;
    if (snowstorm) want += 256 * BIOME_COUNT;
    uint32_t pal[COLOURS], base[COLOURS];
#if defined(OFP_AMIGA_AGA) && defined(OFP_AMIGA_ONLINE)
    if (art_world == BIOME_CLASSIC && original_look) want += 8 * BIOME_COUNT;
#endif
    if (want == shown || !screen) return;
    palette_shown = want;
    base_palette(base);
    for (i = 0; i < COLOURS; i++) {
#ifdef OFP_AMIGA_AGA
        enum { CAP = 255, BLUE_LIFT = 17 };              /* (24 bits: eight a gun) */
        int r = base[i] >> 16 & 255, g = base[i] >> 8 & 255, b = base[i] & 255;
#else
        enum { CAP = 15, BLUE_LIFT = 1 };                /* (12 bits, as the 16-colour screen always had it) */
        int r = base[i] >> 20 & 15, g = base[i] >> 12 & 15, b = base[i] >> 4 & 15;
#endif
        if ((i >= TEAM_ONE && i <= PANEL) || i == BLOOD_PEN) { pal[i] = base[i]; continue; }
        if (time == TIME_DUSK) { r = r * 9 / 10; g = g * 3 / 4; b = b * 2 / 3; }
        else if (time == TIME_NIGHT) { r = r * 5 / 11; g = g * 5 / 10; b = b * 7 / 10 + BLUE_LIFT; }
        if (storm) { r = r * 6 / 7; g = g * 6 / 7; b = b * 6 / 7; }
        if (flash) { r += CAP / 5; g += CAP / 5; b += CAP / 4; }
        if (fog) { r = (r + CAP / 2) / 2; g = (g + CAP * 3 / 5) / 2; b = (b + CAP * 2 / 3) / 2; }
        if (sand) { r = (r * 2 + CAP * 3 / 4) / 3; g = (g * 2 + CAP / 2) / 3; b = (b * 2 + CAP / 4) / 3; }
        if (eruption) { r = (r * 3 + CAP / 2) / 4; g = g * 3 / 4; b = b * 3 / 4; }
        if (snowstorm) { r = (r * 3 + CAP / 2) / 4; g = (g * 3 + CAP * 2 / 3) / 4; b = (b * 3 + CAP * 4 / 5) / 4; }
        if (r > CAP) r = CAP;
        if (g > CAP) g = CAP;
        if (b > CAP) b = CAP;
#ifndef OFP_AMIGA_AGA
        r *= 17; g *= 17; b *= 17;
#endif
        pal[i] = (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
    }
    show_palette(pal);
}

#ifdef OFP_AMIGA_ONLINE
int training_on;                       /* TRAINING: the six steps (common/training.c's), its prompt, the arrow */
Training tr;

static int training_guide(fx *x, fx *y)
{
    if (tr.step == TR_DRIVE) {
        *x = tr.x0;
        *y = tr.y0 + FX(144);
        return 1;
    }
    if (tr.step == TR_SHOOT) {
        const Tank *tank = &game.tanks[0];
        int best = 0, i;
        for (i = 0; i < game.map.num_st; i++) {
            const Structure *st = &game.map.st[i];
            const StructInfo *info = &struct_info[st->type];
            int tx, ty, dx, dy, distance;
            if (st->team != TEAM_ENEMY || !st->hp || !info->hp) continue;
            tx = st->x * TILE_SIZE + info->w * TILE_SIZE / 2;
            ty = st->y * TILE_SIZE + info->h * TILE_SIZE / 2;
            dx = tx - fx_to_int(tank->x);
            dy = ty - fx_to_int(tank->y);
            distance = dx * dx + dy * dy;
            if (!best || distance < best) {
                best = distance;
                *x = FX(tx);
                *y = FX(ty);
            }
        }
        return best != 0;
    }
    return training_target(&game, &tr, x, y);
}

static void draw_guide_arrow(const View *view, int player, fx target_x, fx target_y)
{
    const Tank *tank = &game.tanks[player];
    angle16 angle = fx_atan2(target_x - tank->x, target_y - tank->y);
    fx dx = fx_sin(angle), dy = -fx_cos(angle);
    int x = fx_to_int(tank->x - FX(view->world_x + view->phase_x) + dx * 38);
    int y = fx_to_int(tank->y - FX(view->world_y + view->phase_y) + dy * 38);
    int pass, k, left = player * VIEW, width = view->width - SCROLL_WORD;
    x += left;
    if (x < left + 22) x = left + 22;
    if (x > left + width - 23) x = left + width - 23;
    if (y < 54) y = 54;
    if (y > BOTTOM - 23) y = BOTTOM - 23;
    for (pass = 0; pass < 2; pass++) {
        SetAPen(&presentation, pass ? GOLD : INK);
        for (k = 0; k < 18; k++) {
            int half = (k < 9 ? k * 2 / 3 : 2) + !pass;
            int cx = x - fx_to_int(dx * k), cy = y - fx_to_int(dy * k);
            int px = fx_to_int(-dy * half), py = fx_to_int(dx * half);
            Move(&presentation, cx - px, cy - py);
            Draw(&presentation, cx + px, cy + py);
        }
    }
}

static void draw_operation(const View *views, int count)
{
    char line[36];
    int p;
    unsigned left = game.tick >= game.time_limit ? 0 : (unsigned)((game.time_limit - game.tick) / TICK_RATE);
    fill(INK, 0, 0, WIDTH - 1, 28);
    snprintf(line, sizeof line, "%s  %u:%02u", operation_name(&operation), left / 60, left % 60);
    local_center(WIDTH / 2, 11, GOLD, line);
    local_center(WIDTH / 2, 24, WHITE, operation_prompt(&operation));
    BltBitMapRastPort(frame, 0, 0, &presentation, 0, 0, WIDTH, 29, 0xc0);
    if (operation.phase >= OP_WON) {
        fill(INK, 28, 92, WIDTH - 29, 111);
        local_center(WIDTH / 2, 105, GOLD, "FIRE RETRY   ESC BACK");
        BltBitMapRastPort(frame, 28, 92, &presentation, 28, 92, WIDTH - 56, 20, 0xc0);
    }
    for (p = 0; p < count; p++) {
        fx x, y;
        if (operation_target(&game, &operation, p, &x, &y)) draw_guide_arrow(&views[p], p, x, y);
    }
}

static void draw_training(const View *view)
{
    static const char *const prompts[TR_DONE + 1][3] = {
        { "1 OF 6: DRIVE", "DRIVE OUT OF THE GARAGE.", "FOLLOW THE ARROW." },
        { "2 OF 6: SHOOT", "SHOOT AN ENEMY STRUCTURE.", "" },
        { "3 OF 6: MINES", "LAY A MINE, THEN DRIVE CLEAR.", "" },
        { "4 OF 6: TREES", "FOLLOW THE ARROW UNDER THE TREES.", "THE ENEMY CAN'T SEE YOU THERE." },
        { "5 OF 6: RESCUE", "BREAK OPEN THE PRISON CAMP.", "PICK UP THE MEN AND FIND FIRST AID." },
        { "6 OF 6: THE FLAG", "TAKE THE FLAG FROM THE FORTRESS.", "BRING IT BACK TO YOUR CAMP." },
        { "WELL DONE!", "YOU'RE READY FOR THE REAL THING.", "ESC: RETURN TO THE MENU." }
    };
    int step = tr.step < TR_DONE ? tr.step : TR_DONE;
    const char *line = prompts[step][2];
    char key[16], hint[37];
    fx tx, ty;
    if (step == TR_SHOOT || step == TR_MINE) {
        int fire = step == TR_SHOOT;
        const char *binding = input_mode[0] == AMIGA_INPUT_JOY ? (fire ? "FIRE BUTTON" : "BLUE / GREEN BUTTON") :
                              raw_key_name(player_keys(0)[fire ? 4 : 5], key);
        snprintf(hint, sizeof hint, "%s: %s", fire ? "FIRE" : "MINE", binding);
        line = hint;
    } else if (step == TR_RESCUE && game.tanks[0].pows) {
        line = "FOLLOW THE ARROW TO FIRST AID.";
    }
    fill(INK, 0, 0, WIDTH - 1, 32);
    label(4, 9, GOLD, prompts[step][0]);
    label(4, 19, WHITE, prompts[step][1]);
    label(4, 29, WHITE, line);
    BltBitMapRastPort(frame, 0, 0, &presentation, 0, 0, WIDTH, 33, 0xc0);
    if (training_guide(&tx, &ty)) draw_guide_arrow(view, 0, tx, ty);
}
#endif

/* what the last frame drawn showed: the match at a tick, its first two tanks where they were */
static uint32_t frame_signature(void)
{
    uint32_t sig = game.tick * 2654435761u ^ (uint32_t)game.num_tanks;
    int i;
    for (i = 0; i < 2 && i < game.num_tanks; i++)
        sig = (sig << 7 | sig >> 25) ^ (uint32_t)game.tanks[i].x ^ (uint32_t)game.tanks[i].y << 3 ^ (uint32_t)game.tanks[i].angle;
    return sig ^ (uint32_t)(game.winner + 1) << 20 ^ (uint32_t)game.time_up << 22;
}

/* On the 68060 the CPU finishes this copy faster than the blitter.
 * The playfield's right padding supplies the extra word for a shifted row. */
AmigaFrameProfile frame_profile;
int frame_copy_method;

static void copy_playfield(const View *view, int dest_x, int dest_y)
{
    struct BitMap *dst = presentation.BitMap;
    int width = view->width - SCROLL_WORD, height = BOTTOM - dest_y;
#ifdef OFP_AMIGA_ONLINE
    if (editor_on) height = EDITOR_FIELD_H;
#endif
    {
    int planar = (GetBitMapAttr(dst, BMA_FLAGS) & (BMF_STANDARD | BMF_INTERLEAVED)) == BMF_STANDARD;
    if (planar && (frame_copy_method == 3 || (!frame_copy_method && (SysBase->AttnFlags & (AFF_68040 | AFF_68060))))) {
        int phase = view->phase_x, words = width / 16;
        int from = (FIELD_TOP + view->phase_y) * frame->BytesPerRow + view->left / 8;
        int to = dest_y * dst->BytesPerRow + dest_x / 8;
        int plane, row;
        WaitBlit();
        for (plane = 0; plane < DEPTH; plane++) {
            const UBYTE *src = frame->Planes[plane] + from;
            UBYTE *dest = dst->Planes[plane] + to;
            for (row = 0; row < height; row++) {
                if (!phase) memcpy(dest, src, width / 8);
                else {
                    const UWORD *in = (const UWORD *)src;
                    UWORD *out = (UWORD *)dest;
                    unsigned word = *in++;
                    int x;
                    for (x = 0; x < words; x++) {
                        unsigned next = *in++;
                        *out++ = (UWORD)((word << phase) | (next >> (16 - phase)));
                        word = next;
                    }
                }
                src += frame->BytesPerRow;
                dest += dst->BytesPerRow;
            }
        }
        return;
    }
    if (planar && frame_copy_method != 1) {
        /* On a shifted copy the final word is the blitter's scratch column.
         * Fill that word through graphics.library so we stay inside the view. */
        int bulk = width - (view->phase_x ? SCROLL_WORD : 0);
        blit_planar_words(frame, view->left, FIELD_TOP + view->phase_y, dst, dest_x, dest_y,
                          bulk, height, view->phase_x);
        if (view->phase_x)
            BltBitMap(frame, view->left + view->phase_x + bulk, FIELD_TOP + view->phase_y,
                      dst, dest_x + bulk, dest_y, SCROLL_WORD, height, 0xc0, PLANE_MASK, NULL);
        return;
    }
    }
    BltBitMap(frame, view->left + view->phase_x, FIELD_TOP + view->phase_y,
              dst, dest_x, dest_y, width, height, 0xc0, PLANE_MASK, NULL);
}

#ifdef OFP_AMIGA_ONLINE
/* Each display buffer needs changes from every frame since it was last used.
 * A change in the camera crop or a trip through a menu requires a full copy. */
static void copy_classic_playfield(const View *view, int chat_height)
{
    static struct {
        ViewDamage damage;
        int x, y, valid;
    } shown[2];
    static uint32_t last_presents;
    const ViewDamage *changed = view->damage;
    int i, b = back_buffer, x = view->world_x + view->phase_x, y = view->world_y + view->phase_y;
    ViewDamage damage;
    if (presents != last_presents + 1 || game.winner >= 0 || game.time_up)
        shown[0].valid = shown[1].valid = 0;
    for (i = 0; i < 2; i++) {
        if (shown[i].x != x || shown[i].y != y) shown[i].valid = 0;
        if (shown[i].valid && changed->x0 < changed->x1)
            add_view_damage(&shown[i].damage, changed->x0, changed->y0, changed->x1, changed->y1);
    }
    if (shown[b].valid) {
        damage = shown[b].damage;
        damage.x0 = (damage.x0 - view->phase_x) & ~15;
        damage.x1 = (damage.x1 - view->phase_x + 15) & ~15;
        damage.y0 -= view->phase_y;
        damage.y1 -= view->phase_y;
        if (damage.x0 < 0) damage.x0 = 0;
        if (damage.x1 > CLASSIC_VIEW) damage.x1 = CLASSIC_VIEW;
        if (damage.y0 < 0) damage.y0 = 0;
        if (damage.y1 > BOTTOM) damage.y1 = BOTTOM;
        if (damage.x0 < damage.x1 && damage.y0 < damage.y1)
            BltBitMap(frame, view->phase_x + damage.x0, FIELD_TOP + view->phase_y + damage.y0,
                      presentation.BitMap, damage.x0, damage.y0, damage.x1 - damage.x0,
                      damage.y1 - damage.y0, 0xc0, PLANE_MASK, NULL);
    } else copy_playfield(view, 0, 0);
    shown[b].valid = 1;
    shown[b].x = x;
    shown[b].y = y;
    shown[b].damage = (ViewDamage){ BUFFER_W, HEIGHT, 0, 0 };
    if (chat_height)
        add_view_damage(&shown[b].damage, view->phase_x, view->phase_y + BOTTOM - chat_height,
                        view->phase_x + CLASSIC_VIEW, view->phase_y + BOTTOM);
    last_presents = presents;
}

static void draw_match_chat(int width, int height, const char *latest, int mention)
{
    char text[41];
    int columns = (width - 8) / 8, top = BOTTOM - height, y = top + 8;
    fill(INK, 0, top, width - 1, BOTTOM - 1);
    if (latest[0]) {
        snprintf(text, sizeof text, "%.*s", columns, latest);
        label(4, y, mention ? GOLD : WHITE, text);
        y += 10;
        if (strlen(latest) > (size_t)columns) {
            snprintf(text, sizeof text, "%.*s", columns, latest + columns);
            label(4, y, mention ? GOLD : WHITE, text);
            y += 10;
        }
    }
    if (chat_edit) {
        size_t n = strlen(chat_line), visible = (size_t)columns - 3;
        snprintf(text, sizeof text, "> %.*s_", (int)visible, chat_line + (n > visible ? n - visible : 0));
        label(4, y, GOLD, text);
    }
    BltBitMapRastPort(frame, 0, top, &presentation, 0, top, width, height, 0xc0);
}
#endif

static void draw_mode_score(int center, int y)
{
    char left[12], right[12];
    const char *mode;
    unsigned long scores[2] = {0, 0};
    unsigned extras = game.extras;
    int i, text_width, x;
    if (extras & EXTRA_CONQUEST) {
        mode = "CONQ"; scores[0] = game.cp_score[0]; scores[1] = game.cp_score[1];
    } else if (extras & EXTRA_HARDPOINT) {
        mode = "HP"; scores[0] = game.hp_score[0]; scores[1] = game.hp_score[1];
    } else if (extras & EXTRA_KILLCONF) {
        mode = "TAGS"; scores[0] = game.kc_score[0]; scores[1] = game.kc_score[1];
    } else if (extras & EXTRA_WARLORD) {
        mode = "WL"; scores[0] = game.jug_score[0]; scores[1] = game.jug_score[1];
    } else if (extras & EXTRA_HILL) {
        mode = "HILL"; scores[0] = game.hill[0] / TICK_RATE; scores[1] = game.hill[1] / TICK_RATE;
    } else if (extras & EXTRA_TDM) {
        mode = "KILLS";
        for (i = 0; i < game.num_tanks; i++) scores[game.tanks[i].team & 1] += game.tanks[i].tank_kills;
    } else return;
    snprintf(left, sizeof left, "%lu", scores[0]);
    snprintf(right, sizeof right, "%lu", scores[1]);
    text_width = (int)strlen(mode) * 8;
    x = center - text_width / 2;
    fill(INK, center - 88, y, center + 87, y + 11);
    label(x - 8 - (int)strlen(left) * 8, y + 9, TEAM_ONE, left);
    label(x, y + 9, WHITE, mode);
    label(x + text_width + 8, y + 9, TEAM_TWO, right);
    BltBitMapRastPort(frame, center - 88, y, &presentation, center - 88, y, 176, 12, 0xc0);
}

void draw_frame(void)
{
    uint32_t fx_started = net_ms();
    View views[2];
    int i, count = solo ? 1 : 2, field_top = 0, field_bottom = BOTTOM, panels_changed = 0;
    static uint32_t drawn_sig = 1, drawn_presents;
    uint32_t stamp = 0, next_stamp;
#ifdef OFP_AMIGA_ONLINE
    char latest_chat[NET_CHAT + 10] = "";
    int chat_width = classic_solo_view() ? CLASSIC_VIEW : WIDTH;
    int chat_height = online_playing && chat_edit ? 10 : 0;
    int chat_mention = 0;
    if (online_playing && chat_until) {
        uint32_t q;
        for (q = online.chat_seq; q && online.chat_seq - q < 40; q--) {
            const ClientChat *line = &online.chat[q % 40];
            if (line->seq != q || !line->text[0]) continue;
            if (line->from < 0) snprintf(latest_chat, sizeof latest_chat, "* %s", line->text);
            else snprintf(latest_chat, sizeof latest_chat, "%.8s: %s", line->name, line->text);
            chat_height += strlen(latest_chat) > (size_t)((chat_width - 8) / 8) ? 20 : 10;
            chat_mention = netc_chat_mentions(&online, line, online.id);
            break;
        }
    }
#endif
    {   /* nothing has happened since the frame on the screen (paused, the result shown, waiting): no new one,
         * just a frame's wait; another screen in between, the editor, the title, a replay, TRAINING: always */
        int plain = 1;
        uint32_t sig = frame_signature();
#ifdef OFP_AMIGA_ONLINE
        plain = !editor_on && !title_on && !replay_on && !training_on && !(online_playing && online_dirty);
#endif
        if (plain && sig == drawn_sig && presents == drawn_presents) { WaitTOF(); return; }
        drawn_sig = plain ? sig : 1;
    }
#ifdef OFP_AMIGA_ONLINE
    if (editor_on) { field_top = TOP; field_bottom = EDITOR_BOTTOM; }
#endif
#ifdef OFP_AMIGA_ONLINE
    if (!editor_on && !title_on)
#endif
        match_sounds();
    world_palette();                                     /* (the time of day, the weather) */
    WaitBlit();
    if (frame_profile.enabled) stamp = amiga_us();
    for (i = 0; i < count; i++) views[i] = update_terrain(i);
    if (frame_profile.enabled) { next_stamp = amiga_us(); frame_profile.terrain_us = next_stamp - stamp; stamp = next_stamp; }
    for (i = 0; i < count; i++) draw_objects(&views[i]);
    if (frame_profile.enabled) { next_stamp = amiga_us(); frame_profile.objects_us = next_stamp - stamp; stamp = next_stamp; }
#ifdef OFP_AMIGA_ONLINE
    if (title_on) {
        const View *view = &views[0];
        wait_for_display();
        presentation.BitMap = display_buffers[back_buffer]->sb_BitMap;
        BltBitMap(frame, view->left + view->phase_x, FIELD_TOP + view->phase_y,
                  presentation.BitMap, 0, 0, WIDTH, HEIGHT, 0xc0, PLANE_MASK, NULL);
        draw_title_overlay();
        present_frame();
        return;
    }
    if (editor_on) draw_editor_panels();
    else
#endif
        panels_changed = draw_hud();
    if (frame_profile.enabled) { next_stamp = amiga_us(); frame_profile.hud_us = next_stamp - stamp; stamp = next_stamp; }
    wait_for_display();
    if (frame_profile.enabled) { next_stamp = amiga_us(); frame_profile.wait_us = next_stamp - stamp; stamp = next_stamp; }
    presentation.BitMap = display_buffers[back_buffer]->sb_BitMap;
    if (field_top) BltBitMapRastPort(frame, 0, 0, &presentation, 0, 0, WIDTH, field_top, 0xc0);
    {   /* Track HUD writes instead of reading and hashing chip RAM on every frame. */
        static uint32_t panel_revision = 1, shown_revision[2], last_presents;
        int b = back_buffer & 1, always = 0;
#ifdef OFP_AMIGA_ONLINE
        always = editor_on || replay_on || training_on;
#endif
        if (presents != last_presents + 1 || always) shown_revision[0] = shown_revision[1] = 0;
        if (panels_changed && !++panel_revision) {
            panel_revision = 1;
            shown_revision[0] = shown_revision[1] = 0;
        }
        if (always || shown_revision[b] != panel_revision) {
#ifdef OFP_AMIGA_ONLINE
            if (classic_solo_view())
                BltBitMapRastPort(frame, CLASSIC_VIEW, 0, &presentation,
                                  CLASSIC_VIEW, 0, WIDTH - CLASSIC_VIEW, BOTTOM, 0xc0);
#endif
            BltBitMapRastPort(frame, 0, field_bottom, &presentation, 0, field_bottom, WIDTH, HEIGHT - field_bottom, 0xc0);
            shown_revision[b] = always ? 0 : panel_revision;
        }
        last_presents = presents;
    }
    for (i = 0; i < count; i++) {
        const View *view = &views[i];
#ifdef OFP_AMIGA_ONLINE
        if (editor_on)
            BltBitMapRastPort(frame, EDITOR_WIDTH, TOP, &presentation, EDITOR_WIDTH, TOP, WIDTH - EDITOR_WIDTH, EDITOR_FIELD_H, 0xc0);
#endif
#ifdef OFP_AMIGA_ONLINE
        if (view->damage) copy_classic_playfield(view, chat_height);
        else
#endif
            copy_playfield(view, i * VIEW, field_top);
    }
    if (!solo) {
        SetAPen(&presentation, INK);
        RectFill(&presentation, VIEW - 1, field_top, VIEW, BOTTOM - 1);
    }
#ifdef OFP_AMIGA_ONLINE
    if (local_playing && !operation.phase && (game.winner >= 0 || game.time_up)) {
#else
    if (game.winner >= 0 || game.time_up) {
#endif
        int left = 64;
#ifdef OFP_AMIGA_ONLINE
        if (classic_solo_view()) left = (CLASSIC_VIEW - 192) / 2;
#endif
        fill(PANEL, left, 90, left + 191, 119);
        label(left + 16, 102, WHITE, game.time_up ? "DRAW" : game.winner == (game.tanks[0].team & 1) ? "PLAYER 1 WINS" : solo ? "CPU WINS" : "PLAYER 2 WINS");
#ifdef OFP_AMIGA_ONLINE
        label(left + 16, 113, GOLD, editor_test_running() ? "ESC BACK TO EDITOR" : "ESC MAIN MENU");
#else
        label(left + 16, 113, GOLD, "F1 REMATCH / ESC EXIT");
#endif
        BltBitMapRastPort(frame, left, 90, &presentation, left, 90, 192, 30, 0xc0);
    }
#ifdef OFP_AMIGA_ONLINE
    if (training_on) draw_training(&views[0]);
    if (operation.phase) draw_operation(views, count);
    if (replay_on) draw_watch_strip();
    if (!editor_on && !training_on)
        draw_mode_score(classic_solo_view() ? CLASSIC_VIEW / 2 : WIDTH / 2, replay_on ? 12 : 0);
    if (chat_height) draw_match_chat(chat_width, chat_height, latest_chat, chat_mention);
#else
    draw_mode_score(WIDTH / 2, 0);
#endif
    present_frame();
    if (frame_profile.enabled) frame_profile.copy_us = amiga_us() - stamp;
#ifdef OFP_AMIGA_ONLINE
    fx_budget_frame(net_ms() - fx_started, 40, settings.auto_effects);
#else
    fx_budget_frame(net_ms() - fx_started, 40, 0);
#endif
    drawn_presents = presents;                           /* (the frame now showing is this one) */
}

