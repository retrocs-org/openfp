/* SPDX-License-Identifier: MIT
 * src/platform/amiga/local/system.c: the controls (keys, joysticks, the CD32 pad), the solo bot, the local client's events, opening and closing the screen.
 * A part of the Amiga client, its own translation unit: what the parts share is in internal.h. */
#include "platform/amiga/local/internal.h"

const uint8_t default_keys[2][6] = {
    AMIGA_PLAYER_ONE_KEYS, AMIGA_WASD_KEYS
};

const uint8_t *player_keys(int player)
{
#ifdef OFP_AMIGA_ONLINE
    if (settings.controls[player].has_keys) return settings.controls[player].keys;
    if (player == 0) return settings.profile[settings.current].keys;
#endif
    return default_keys[player];
}

#ifdef OFP_AMIGA_ONLINE
InputBits keymap_action(const uint8_t *mapping, UWORD code)
{
    int i;
    for (i = 0; i < 6; i++)
        if (code == mapping[i]) return (InputBits)(1u << i);
    if (!memcmp(mapping, default_keys[0], sizeof default_keys[0])) {
        if (code == AMIGA_KEY_RAMIGA) return IN_FIRE;
        if (code == AMIGA_KEY_RALT) return IN_MINE;
    }
    return 0;
}

InputBits player_key_action(int player, UWORD code)
{
    return keymap_action(player_keys(player), code);
}
#endif

InputBits joystick_actions(ULONG joy)
{
    InputBits input = 0;
    ULONG type = joy & JP_TYPE_MASK;
    if (type == JP_TYPE_JOYSTK || type == JP_TYPE_GAMECTLR) {
        if (joy & JPF_JOY_UP) input |= IN_UP;
        if (joy & JPF_JOY_DOWN) input |= IN_DOWN;
        if (joy & JPF_JOY_LEFT) input |= IN_LEFT;
        if (joy & JPF_JOY_RIGHT) input |= IN_RIGHT;
        if (joy & JPF_BUTTON_RED) input |= IN_FIRE;
        if (joy & JPF_BUTTON_BLUE) input |= IN_MINE;
        if (type == JP_TYPE_GAMECTLR && (joy & JPF_BUTTON_GREEN)) input |= IN_MINE;   /* (a CD32 pad: green too) */
    }
    return input;
}

int player_joystick_port(int player)
{
#ifdef OFP_AMIGA_ONLINE
    player ^= settings.swap_joysticks;
#endif
    return player ? AMIGA_JOY2_PORT : AMIGA_JOY1_PORT;
}

InputBits player_joystick(int player)
{
    return LowLevelBase ? joystick_actions(ReadJoyPort(player_joystick_port(player))) : 0;
}

InputBits player_input(int player)
{
    InputBits input = 0;
    const uint8_t *k = player_keys(player);
    int i;
    if (LowLevelBase && input_mode[player] != AMIGA_INPUT_KEYS) {
        input = player_joystick(player);
        if (input_mode[player] == AMIGA_INPUT_JOY) return input;
    }
    for (i = 0; i < 6; i++)
        if (keys[k[i] & 0x7f]) input |= (InputBits)(1u << i);
    if ((keys[AMIGA_KEY_RAMIGA] || keys[AMIGA_KEY_RALT]) &&
        !memcmp(k, default_keys[0], sizeof default_keys[0])) {
        if (keys[AMIGA_KEY_RAMIGA]) input |= IN_FIRE;
        if (keys[AMIGA_KEY_RALT]) input |= IN_MINE;
    }
    return input;
}

/* Drive the second tank through the same controls as a human. Local avoidance
 * only samples the ground ahead, so a new goal never triggers a map search. */
InputBits solo_bot_input(void)
{
    const Tank *bot = &game.tanks[1], *target = &game.tanks[0];
    fx dx = target->x - bot->x, dy = target->y - bot->y;
    fx distance = fx_abs(dx) > fx_abs(dy) ? fx_abs(dx) : fx_abs(dy);
    int16_t error;
    InputBits input = 0;

    if (!bot->active || bot->respawn || !target->active || target->respawn) {
        bot_avoid = 0;
        return 0;
    }
    if (bot_avoid) {
        bot_avoid--;
        return bot_avoid >= 12 ? (InputBits)bot_turn : IN_UP;
    }
    error = (int16_t)(fx_atan2(dx, dy) - bot->angle);
    if (error > 0x0300) input |= IN_RIGHT;
    else if (error < -0x0300) input |= IN_LEFT;
    if (error >= -0x0800 && error <= 0x0800 && distance < FX(190))
        input |= IN_FIRE;
    if (error >= -0x1800 && error <= 0x1800 && distance > FX(60)) {
        fx ahead_x = bot->x + fx_sin(bot->angle) * 14;
        fx ahead_y = bot->y - fx_cos(bot->angle) * 14;
        if (!map_box_solid(&game.map, ahead_x, ahead_y, TANK_HALF)) input |= IN_UP;
        else {
            bot_turn = bot_turn == IN_RIGHT ? IN_LEFT : IN_RIGHT;
            bot_avoid = 30;
        }
    }
    return input;
}

#ifdef OFP_AMIGA_ONLINE
/* Local only. Replay records these inputs like a second player's. The shared
 * lockstep AI stays in charge of older replays and network games. */
typedef struct {
    angle16 detour;
    uint8_t until_clear, turn_left;
} FieldBot;
static FieldBot field_bots[2];
static unsigned field_gate[2];

int light_local_ai(void)
{
#ifdef OFP_ART_PREVIEW
    return 0;
#else
    return !(SysBase->AttnFlags & (AFF_68040 | AFF_68060));
#endif
}

void reset_field_bots(void)
{
    int i;
    memset(field_bots, 0, sizeof field_bots);
    field_gate[0] = field_gate[1] = 0;
    for (i = 0; i < game.map.num_st; i++) {
        const Structure *st = &game.map.st[i];
        const Base *base = &game.bases[st->team & 1];
        if ((struct_info[st->type].flags & SF_GATE) &&
            st->x >= base->x0 && st->x <= base->x1 && st->y >= base->y0 && st->y <= base->y1)
            field_gate[st->team & 1] = i + 1;
    }
}

static int field_clear(const Tank *tank, angle16 angle)
{
    fx x = tank->x + fx_sin(angle) * 13, y = tank->y - fx_cos(angle) * 13;
    return !map_box_solid_for_team(&game.map, x, y, TANK_HALF, tank->team & 1);
}

InputBits field_bot_input(int player)
{
    const Tank *tank = &game.tanks[player], *foe = &game.tanks[player ^ 1];
    FieldBot *bot = &field_bots[player];
    int team = tank->team & 1;
    const Flag *own = &game.flags[team], *enemy = &game.flags[team ^ 1];
    fx gx = foe->x, gy = foe->y;
    angle16 goal, drive;
    int error, shot;
    InputBits in = 0;
    if (!tank->active || tank->out || tank->respawn) {
        bot->until_clear = 0;
        return 0;
    }

    if (tank->has_flag) {
        gx = FX(game.bases[team].spawn_tx * TILE_SIZE + TILE_SIZE / 2);
        gy = FX(game.bases[team].spawn_ty * TILE_SIZE + TILE_SIZE / 2);
    } else if ((game.extras & EXTRA_HARDPOINT) && game.num_cp) {
        gx = FX(game.cp_tx[game.hp_active] * TILE_SIZE + TILE_SIZE / 2);
        gy = FX(game.cp_ty[game.hp_active] * TILE_SIZE + TILE_SIZE / 2);
    } else if (game.extras & EXTRA_CONQUEST) {
        int i;
        fx nearest = INT32_MAX;
        for (i = 0; i < game.num_cp; i++) {
            fx x = FX(game.cp_tx[i] * TILE_SIZE + TILE_SIZE / 2);
            fx y = FX(game.cp_ty[i] * TILE_SIZE + TILE_SIZE / 2);
            fx distance = fx_abs(x - tank->x) + fx_abs(y - tank->y);
            if (game.cp_owner[i] != team && distance < nearest) {
                nearest = distance;
                gx = x; gy = y;
            }
        }
    } else if (game.extras & EXTRA_HILL) {
        gx = FX(game.hill_tx * TILE_SIZE + TILE_SIZE / 2);
        gy = FX(game.hill_ty * TILE_SIZE + TILE_SIZE / 2);
    } else if (!game.map.arena && !(game.extras & EXTRA_NOFLAGS)) {
        if (own->state == FLAG_GROUND && (own->x != own->home_x || own->y != own->home_y)) {
            gx = own->x; gy = own->y;
        } else if (own->state != FLAG_CARRIED) {
            gx = enemy->x; gy = enemy->y;
        }
    }
    if (!game.map.arena && field_gate[team]) {
        const Base *base = &game.bases[team];
        const Structure *gate = &game.map.st[field_gate[team] - 1];
        const StructInfo *info = &struct_info[gate->type];
        int tx = fx_to_int(tank->x) / TILE_SIZE, ty = fx_to_int(tank->y) / TILE_SIZE;
        int inside = tx >= base->x0 && tx <= base->x1 && ty >= base->y0 && ty <= base->y1;
        int goal_inside = fx_to_int(gx) / TILE_SIZE >= base->x0 && fx_to_int(gx) / TILE_SIZE <= base->x1 &&
                          fx_to_int(gy) / TILE_SIZE >= base->y0 && fx_to_int(gy) / TILE_SIZE <= base->y1;
        if (inside != goal_inside) {
            gx = FX(gate->x * TILE_SIZE + info->w * TILE_SIZE / 2);
            gy = FX(gate->y * TILE_SIZE + info->h * TILE_SIZE / 2);
            if (info->w > info->h && fx_abs(gx - tank->x) < FX(12))
                gy += FX(20) * (gate->y == base->y0 ? -1 : 1) * (inside ? 1 : -1);
            if (info->h > info->w && fx_abs(gy - tank->y) < FX(12))
                gx += FX(20) * (gate->x == base->x0 ? -1 : 1) * (inside ? 1 : -1);
        }
    }
    goal = fx_atan2(gx - tank->x, gy - tank->y);
    drive = goal;
    shot = (int16_t)(fx_atan2(foe->x - tank->x, foe->y - tank->y) - tank->angle);
    if (!foe->out && !foe->respawn && fx_abs(foe->x - tank->x) < FX(115 + level_choice * 20) &&
        fx_abs(foe->y - tank->y) < FX(115 + level_choice * 20) &&
        shot > -0x0800 && shot < 0x0800) in |= IN_FIRE;
    if (!game.map.arena && !(game.extras & EXTRA_NOFLAGS) && enemy->state == FLAG_HIDDEN &&
        gx == enemy->x && gy == enemy->y && fx_abs(gx - tank->x) < FX(90) && fx_abs(gy - tank->y) < FX(90)) {
        int aim = (int16_t)(goal - tank->angle);
        if (aim > -0x0600 && aim < 0x0600) in |= IN_FIRE;
    }

    if (bot->until_clear) {
        bot->until_clear--;
        if (field_clear(tank, bot->detour)) drive = bot->detour;
        else bot->until_clear = 0;
    }
    if (!bot->until_clear && !field_clear(tank, goal)) {
        int step;
        /* Try nearby headings first. A short commitment gets around the edge
         * before steering back towards the objective. */
        for (step = 1; step <= 8; step++) {
            int turn = (bot->turn_left ? -1 : 1) * step * 0x1000;
            if (field_clear(tank, (angle16)(goal + turn))) drive = (angle16)(goal + turn);
            else if (field_clear(tank, (angle16)(goal - turn))) {
                drive = (angle16)(goal - turn);
                bot->turn_left ^= 1;
            } else continue;
            bot->detour = drive;
            bot->until_clear = 36;
            break;
        }
        /* Destructible fortifications are a route into the flag bunker. */
        if (!field_clear(tank, tank->angle)) in |= IN_FIRE;
    }
    error = (int16_t)(drive - tank->angle);
    if (error > 0x0300) in |= IN_RIGHT;
    else if (error < -0x0300) in |= IN_LEFT;
    if (error >= -0x1800 && error <= 0x1800 && field_clear(tank, tank->angle)) in |= IN_UP;
    return in;
}
#endif

#ifndef OFP_AMIGA_ONLINE
void events(void)
{
    struct IntuiMessage *message;
    while ((message = (struct IntuiMessage *)GetMsg(window->UserPort))) {
        ULONG type = message->Class;
        UWORD code = message->Code;
        ReplyMsg((struct Message *)message);
        if (type == IDCMP_RAWKEY) {
            if (code & 0x80) keys[code & 0x7f] = 0;
            else if (!keys[code & 0x7f]) {
                keys[code & 0x7f] = 1;
                if (code == 0x45) quit = 1;
                if (code == 0x50) restart = 1;
                if (code == 0x51 && LowLevelBase)
                    input_mode[0] = input_mode[0] == AMIGA_INPUT_JOY ? AMIGA_INPUT_BOTH : AMIGA_INPUT_JOY;
                if (code == 0x52) {
                    solo = !solo;
                    restart = 1;
                }
                if (code == 0x53) {
                    do map_choice = (map_choice + 1) % LOCAL_MAPS; while (map_choice == MY_MAP);
                    restart = 1;
                }
                if (code == 0x54) music_on = !music_on;   /* F5: the music on or off */
            }
        } else {
            active = type == IDCMP_ACTIVEWINDOW;
            memset(keys, 0, sizeof keys);
        }
    }
}

#endif

int open_display(void)
{
    static struct TextAttr font = { (STRPTR)"topaz.font", 8, FS_NORMAL, FPF_ROMFONT };
#ifdef OFP_AMIGA_ONLINE
    const char *title = "OpenFP";
#else
    const char *title = "OpenFP Local";
#endif
    detailed_radar = 1;
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    LowLevelBase = OpenLibrary((STRPTR)"lowlevel.library", 40);
    if (!GfxBase || !IntuitionBase) return 0;
#ifdef OFP_AMIGA_AGA
    if (!(GfxBase->ChipRevBits0 & GFXF_AA_ALICE)) {    /* (the 64 colours need AGA: Amiga/68000's build for the others) */
        puts("This OpenFP is for AGA Amigas (A1200, A4000, CD32): use the ECS build on this one.");
        return 0;
    }
#endif
    screen = OpenScreenTags(NULL, SA_Width, WIDTH, SA_Height, HEIGHT, SA_Depth, DEPTH,
                            SA_DisplayID, PAL_MONITOR_ID | LORES_KEY, SA_Font, &font,
                            SA_Title, title, SA_Quiet, TRUE,
                            SA_ColorMapEntries, COLOURS <= 16 ? 32 : 256,   /* (the sprites' colours: past the playfield's) */
                            SA_ShowTitle, FALSE, TAG_DONE);
    if (!screen) return 0;
    window = OpenWindowTags(NULL, WA_CustomScreen, screen, WA_Left, 0, WA_Top, 0,
                            WA_Width, WIDTH, WA_Height, HEIGHT, WA_Borderless, TRUE,
                            WA_Backdrop, TRUE, WA_Activate, TRUE, WA_RMBTrap, TRUE,
                            WA_IDCMP, IDCMP_RAWKEY | IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW,
                            TAG_DONE);
    if (!window) return 0;
    frame = AllocBitMap(BUFFER_W, FIELD_TOP + ROWS * TILE_PX, DEPTH, BMF_CLEAR | BMF_DISPLAYABLE, NULL);
#ifndef OFP_NATIVE_AGA
    scroll_scratch = AllocBitMap(WIDTH + SCROLL_WORD, ROWS * TILE_PX, DEPTH, BMF_DISPLAYABLE, NULL);
    if (!scroll_scratch) return 0;
#endif
    sprite_chip = AllocMem(SPRITE_CHIP_BYTES, MEMF_CHIP);
    display_buffers[0] = AllocScreenBuffer(screen, NULL, SB_SCREEN_BITMAP);
    display_buffers[1] = AllocScreenBuffer(screen, NULL, 0);
    safe_port = CreateMsgPort();
    display_port = CreateMsgPort();
    if (!frame || !sprite_chip || !display_buffers[0] ||
        !display_buffers[1] || !safe_port || !display_port) return 0;
    {
        int i;
        for (i = 0; i < 2; i++) {
            display_buffers[i]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = safe_port;
            display_buffers[i]->sb_DBufInfo->dbi_DispMessage.mn_ReplyPort = display_port;
        }
    }
    InitRastPort(&presentation);
    {
        int i;
        uint32_t *dest = (uint32_t *)sprite_chip;
        for (i = 0; i < TANK_FRAMES / DIRECTIONS; i++) dest = load_sprite_bank(dest, DIRECTIONS, classic_tanks[i & 1]);
        for (i = 0; i < PROP_KINDS * 3; i++)
            dest = load_sprite_bank(dest, PROP_DIRECTIONS, classic_vehicles[i]);
        for (i = 0; i < TURRET_VARIANTS; i++)
            dest = load_sprite_bank(dest, TURRET_DIRECTIONS, classic_turrets[i]);
        for (i = 0; i < MAN_VARIANTS; i++)
            dest = load_sprite_bank(dest, MAN_FRAMES, classic_men[i]);
        for (i = 0; i < BATTLE_VARIANTS; i++)
            dest = load_sprite_bank(dest, BATTLE_FRAMES, classic_battle[i]);
#ifdef OFP_AMIGA_ONLINE
        for (i = 0; i < 2; i++)
            dest = load_sprite_bank(dest, ROCKET_DIRECTIONS, classic_rocket_tanks[i]);
#endif
    }
    blank_pointer = AllocMem(6 * sizeof *blank_pointer, MEMF_CHIP | MEMF_CLEAR);
    if (blank_pointer) SetPointer(window, blank_pointer, 1, 16, 0, 0);
    InitRastPort(&canvas);
    canvas.BitMap = frame;
    SetFont(&canvas, screen->RastPort.Font);
    SetDrMd(&canvas, JAM1);
    {
        uint32_t base[COLOURS];
        base_palette(base);
        show_palette(base);
    }
    return 1;
}

void close_display(void)
{
    if (GfxBase) WaitBlit();
    wait_for_display();
    if (frame) FreeBitMap(frame);
    if (scroll_scratch) FreeBitMap(scroll_scratch);
    if (sprite_chip) FreeMem(sprite_chip, SPRITE_CHIP_BYTES);
    if (window) CloseWindow(window);
    if (blank_pointer) FreeMem(blank_pointer, 6 * sizeof *blank_pointer);
    if (screen) {
        FreeScreenBuffer(screen, display_buffers[0]);
        FreeScreenBuffer(screen, display_buffers[1]);
        CloseScreen(screen);
    }
    if (safe_port) DeleteMsgPort(safe_port);
    if (display_port) DeleteMsgPort(display_port);
    if (LowLevelBase) CloseLibrary(LowLevelBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
}
