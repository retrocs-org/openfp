/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include "platform/sdl2/select.h"
#include "platform/sdl2/input.h"
#include "platform/sdl2/ui.h"
#include "platform/sdl2/render.h"

#define CARD_W (THEME_CARD_W + 4)

void select_reset(SelectScreen *s, int choice, int style, int num_styles)
{
    s->choice = choice;
    s->style = style;
    s->num_styles = num_styles;
    s->slide = 0;
    s->frame = 0;
    s->prev = 0xFF;              /* ignore whatever is held when we arrive */
    {
        int i;
        for (i = 0; i < 4; i++) { s->prevs[i] = 0xFF; s->ready[i] = 0; }
        s->choices[0] = choice;
    }
}

const GameMode game_modes[MODE_COUNT] = {
    { "1 PLAYER", 1, 0 }, { "2P VERSUS", 2, 1 }, { "2P TEAM", 2, 0 },
    { "3P: 2 VS 1", 3, 1 }, { "3P TEAM", 3, 0 }, { "4P: 2 VS 2", 4, 1 }, { "4P TEAM", 4, 0 },
};

int mode_team(int mode, int player)
{
    return game_modes[mode].versus && (player & 1) ? TEAM_ENEMY : TEAM_PLAYER;
}

static int cap_ours = 4, cap_theirs = 3, cap_total = 4;

void select_set_caps(int ours, int theirs) { cap_ours = ours; cap_theirs = theirs; }
void select_set_max_players(int n) { cap_total = n; }

int mode_side_players(int mode, int team)
{
    int n = game_modes[mode].players;
    if (!game_modes[mode].versus) return team == TEAM_PLAYER ? n : 0;
    return team == TEAM_PLAYER ? (n + 1) / 2 : n / 2;
}

/* every player needs a garage of their own to start from */
int mode_available(int mode)
{
    return mode >= 0 && mode < MODE_COUNT && game_modes[mode].players <= OFP_MAX_LOCAL_PLAYERS &&
           game_modes[mode].players + !game_modes[mode].versus <= MAX_TANKS &&
           mode_side_players(mode, TEAM_PLAYER) <= cap_ours && mode_side_players(mode, TEAM_ENEMY) <= cap_theirs &&
           game_modes[mode].players <= cap_total;
}

void select_next_mode(SelectScreen *s)
{
    int i;
    do s->mode = (s->mode + 1) % MODE_COUNT;           /* skip what this machine can't do */
    while (!mode_available(s->mode));
    for (i = 0; i < 4; i++) s->ready[i] = 0;
}

int select_update_n(SelectScreen *s, const InputBits in[4], int confirm_key)
{
    int p, n = game_modes[s->mode].players, all = 1;
    InputBits any = 0;
    s->frame++;
    if (s->slide > 0) s->slide = s->slide * 3 / 4;
    if (s->slide < 0) s->slide = -((-s->slide) * 3 / 4);
    s->choices[0] = s->choice;
    for (p = 0; p < n; p++) {
        InputBits pr = (InputBits)(in[p] & ~s->prevs[p]);
        s->prevs[p] = in[p];
        any |= pr;
        if (!s->ready[p] && (pr & IN_LEFT))  { s->choices[p] = (s->choices[p] + TANK_CLASS_COUNT - 1) % TANK_CLASS_COUNT; if (!p) s->slide = -48; }
        if (!s->ready[p] && (pr & IN_RIGHT)) { s->choices[p] = (s->choices[p] + 1) % TANK_CLASS_COUNT; if (!p) s->slide = 48; }
        if (pr & IN_FIRE) s->ready[p] = !s->ready[p];
        all &= s->ready[p];
    }
    s->choice = s->choices[0];
    if (s->num_styles > 0 && (any & IN_UP))
        s->style = (s->style + s->num_styles - 1) % s->num_styles;
    if (s->num_styles > 0 && (any & IN_DOWN))
        s->style = (s->style + 1) % s->num_styles;
    return all || confirm_key;
}

int select_update(SelectScreen *s, InputBits in, int confirm_key)
{
    InputBits pressed = (InputBits)(in & ~s->prev);
    s->prev = in;
    s->frame++;
    if (s->slide > 0) s->slide = s->slide * 3 / 4;
    if (s->slide < 0) s->slide = -((-s->slide) * 3 / 4);

    if (pressed & IN_LEFT) {
        s->choice = (s->choice + TANK_CLASS_COUNT - 1) % TANK_CLASS_COUNT;
        s->slide = -48;
    }
    if (pressed & IN_RIGHT) {
        s->choice = (s->choice + 1) % TANK_CLASS_COUNT;
        s->slide = 48;
    }
    if (s->num_styles > 0 && (pressed & IN_UP))
        s->style = (s->style + s->num_styles - 1) % s->num_styles;
    if (s->num_styles > 0 && (pressed & IN_DOWN))
        s->style = (s->style + 1) % s->num_styles;
    return (pressed & IN_FIRE) || confirm_key;
}

static int speed_rating(const TankStats *st) { return fx_to_int(st->max_fwd * 6); }

static void stat_box(SDL_Renderer *r, int x, int y, int w, const char *label, int value, int digits)
{
    char buf[16];
    ui_box(r, x, y, w, 14);
    ui_text_bold(r, x + 4, y + 3, 1, ui_colors()->label, label);
    snprintf(buf, sizeof buf, "%0*d", digits, value);
    ui_text(r, x + w - 4 - ui_text_width(buf, 1), y + 3, 1, ui_colors()->value, buf);
}

static void icon_box(SDL_Renderer *r, int x, int y, int w, int icon, Rgb ic, int value)
{
    char buf[16];
    ui_box(r, x, y, w, 14);
    ui_icon(r, icon, x + 4, y + 3, ic);
    snprintf(buf, sizeof buf, "%02d", value);
    ui_text(r, x + w - 4 - ui_text_width(buf, 1), y + 3, 1, ui_colors()->value, buf);
}

/* a little landscape drawn with a style's tiles, so you see what you pick */
static const char PREVIEW[3][6] = {
    ".TT.B",
    "=====",
    "rW.~~",
};

static int preview_type(int x, int y)
{
    if (x < 0 || y < 0 || x >= 5 || y >= 3) return -1;
    switch (PREVIEW[y][x]) {
    case 'T': return TILE_TREE;  case 'B': return TILE_BUILDING; case '=': return TILE_ROAD;
    case '~': return TILE_WATER; case 'r': return TILE_ROCK;     case 'W': return TILE_WALL;
    default:  return TILE_SAND;
    }
}

static void draw_preview(SDL_Renderer *r, const Style *st, int px, int py)
{
    int x, y;
    for (y = 0; y < 3; y++)
        for (x = 0; x < 5; x++) {
            int type = preview_type(x, y), grp = render_connect_group(type), col = (x * 7 + y * 3) & 15;
            SDL_Rect src, dst = { px + x * TILE_SIZE, py + y * TILE_SIZE, TILE_SIZE, TILE_SIZE };
            if (grp) {
                col = 0;
                if (y == 0 || render_connect_group(preview_type(x, y - 1)) == grp) col |= 1;
                if (x == 4 || render_connect_group(preview_type(x + 1, y)) == grp) col |= 2;
                if (y == 2 || render_connect_group(preview_type(x, y + 1)) == grp) col |= 4;
                if (x == 0 || render_connect_group(preview_type(x - 1, y)) == grp) col |= 8;
            }
            src.x = col * TILE_SIZE; src.y = type * TILE_SIZE; src.w = src.h = TILE_SIZE;
            SDL_RenderCopy(r, st->tiles, &src, &dst);
        }
}

static void terrain_panel(SDL_Renderer *r, const Style *styles, const SelectScreen *s, int x, int y)
{
    const HudColors *c = ui_colors();
    const Style *st = &styles[s->style];
    int w = 88, i;
    ui_panel(r, x, y, w, 112);
    ui_box(r, x + 6, y + 6, w - 12, 13);
    ui_text_bold(r, x + (w - ui_text_width("WORLD", 1) - 1) / 2, y + 8, 1, c->label, "WORLD");
    ui_frame(r, x + 2 + (w - 4 - 84) / 2, y + 23, 84, 52);
    draw_preview(r, st, x + 2 + (w - 4 - 80) / 2, y + 25);
    ui_box(r, x + 6, y + 79, w - 12, 13);
    ui_text(r, x + (w - ui_text_width(st->title, 1)) / 2, y + 82, 1, c->value, st->title);
    ui_icon(r, ICON_UP,   x + w / 2 - 12, y + 97, c->value);
    ui_icon(r, ICON_DOWN, x + w / 2 + 4,  y + 97, c->value);
    for (i = 0; i < s->num_styles && i < 8; i++)          /* which one of how many */
        ui_fill(r, x + 10 + i * 4, y + 103, 2, 2, i == s->style ? c->value : c->frame_dark);
}

/* one player's own card: their tank's picture, name and figures, arrows to
 * swipe through the tanks, and whether they're ready. 184 x 178. */
static void player_card(SDL_Renderer *r, const Theme *t, const SelectScreen *s, int p, int x, int y)
{
    const HudColors *c = &t->hud;
    int ch = s->choices[p], team = mode_team(s->mode, p), i;
    const TankStats *st = &tank_stats[ch];
    const char *name = t->tank_names[ch][0] ? t->tank_names[ch] : st->name;
    Rgb rim = render_side_rgb(team);
    SDL_Rect src = { ch * THEME_CARD_W, 0, THEME_CARD_W, THEME_CARD_H };
    SDL_Rect dst = { x + 4, y + 20, THEME_CARD_W, THEME_CARD_H };
    char who[4] = { 'P', (char)('1' + p), 0, 0 };
    int ready = s->ready[p];

    ui_frame(r, x, y, 184, 178);
    ui_fill(r, x + 2, y + 2, 180, 174, (Rgb){ 14, 14, 18 });
    ui_fill(r, x + 2, y + 2, 180, 16, rim);                  /* the player's side, at a glance */
    ui_fill(r, x + 3, y + 3, 22, 14, (Rgb){ 12, 12, 16 });
    ui_text_bold(r, x + 6, y + 6, 1, UI_WHITE, who);
    ui_text_shadow(r, x + 30 + (150 - ui_text_width(name, 1)) / 2, y + 6, 1, UI_WHITE, name);
    SDL_RenderCopy(r, t->cards, &src, &dst);
    if (!ready) {                                            /* swipe for another tank */
        ui_icon(r, ICON_LEFT, x + 6, y + 66, c->value);
        ui_icon(r, ICON_RIGHT, x + 171, y + 66, c->value);
    }
    for (i = 0; i < TANK_CLASS_COUNT; i++)                    /* which of the three */
        ui_fill(r, x + 92 - 18 + i * 14, y + 127, 6, 6, i == ch ? c->value : c->frame_dark);
    stat_box(r, x + 4, y + 136, 87, "SPEED", speed_rating(st), 2);
    stat_box(r, x + 93, y + 136, 87, "LIVES", st->lives, 2);
    stat_box(r, x + 4, y + 151, 87, "FUEL", st->fuel, 3);
    stat_box(r, x + 93, y + 151, 87, "ARMOR", st->armor, 2);
    if (ready) {
        ui_fill(r, x + 4, y + 165, 176, 10, (Rgb){ 30, 70, 30 });
        ui_text_bold(r, x + (184 - ui_text_width("READY", 1)) / 2, y + 166, 1, (Rgb){ 140, 255, 140 }, "READY");
    } else {
        ui_text(r, x + (184 - ui_text_width(st->blurb, 1)) / 2, y + 166, 1, UI_GREY, st->blurb);
    }
}

/* a tank to choose, on its own (the profile window): 184 x 150; arrows when it has the focus */
void select_tank_card(SDL_Renderer *r, const Theme *t, SDL_Texture *tanks, int ch, int choices, int x, int y, int focus)
{
    const HudColors *c = &t->hud;
    int cls = tank_chassis(ch);
    const TankStats *st = &tank_stats[cls];
    const char *name = ch == TANK_RHINO ? "RHINO" : t->tank_names[cls][0] ? t->tank_names[cls] : st->name;
    SDL_Rect src = { cls * THEME_CARD_W, 0, THEME_CARD_W, THEME_CARD_H };
    SDL_Rect dst = { x + 4, y + 18, THEME_CARD_W, THEME_CARD_H };
    int i;
    ui_frame(r, x, y, 184, 150);
    ui_fill(r, x + 2, y + 2, 180, 146, (Rgb){ 14, 14, 18 });
    ui_fill(r, x + 2, y + 2, 180, 14, focus ? c->value : c->frame_dark);
    if (focus) ui_text(r, x + (184 - ui_text_width(name, 1)) / 2, y + 5, 1, (Rgb){ 20, 18, 10 }, name);   /* (dark on the bar) */
    else ui_text_shadow(r, x + (184 - ui_text_width(name, 1)) / 2, y + 5, 1, UI_WHITE, name);
    if (tanks) render_tank_icon(r, tanks, ch, TEAM_PLAYER, x + 92, y + 70, 124);
    else if (t->cards) SDL_RenderCopy(r, t->cards, &src, &dst);
    if (focus) {                                         /* swipe for another */
        ui_icon(r, ICON_LEFT, x + 6, y + 64, c->value);
        ui_icon(r, ICON_RIGHT, x + 171, y + 64, c->value);
    }
    for (i = 0; i < choices; i++)
        ui_fill(r, x + 92 - (choices * 14 - 8) / 2 + i * 14, y + 124, 6, 6, i == ch ? c->value : c->frame_dark);
    stat_box(r, x + 4, y + 104 + 28, 87, "SPEED", speed_rating(st), 2);
    stat_box(r, x + 93, y + 104 + 28, 87, "ARMOR", st->armor, 2);
}

/* the same, smaller, for three or four players: 186 x 97, picture at half size */
static void player_card_small(SDL_Renderer *r, const Theme *t, const SelectScreen *s, int p, int x, int y)
{
    const HudColors *c = &t->hud;
    int ch = s->choices[p], team = mode_team(s->mode, p), i, ready = s->ready[p];
    const TankStats *st = &tank_stats[ch];
    const char *name = t->tank_names[ch][0] ? t->tank_names[ch] : st->name;
    Rgb rim = render_side_rgb(team);
    SDL_Rect src = { ch * THEME_CARD_W, 0, THEME_CARD_W, THEME_CARD_H };
    SDL_Rect dst = { x + 4, y + 18, THEME_CARD_W / 2, THEME_CARD_H / 2 };
    char who[4] = { 'P', (char)('1' + p), 0, 0 }, buf[16];
    static const char *lab[4] = { "SPEED", "LIVES", "FUEL", "ARMOR" };
    int val[4];
    val[0] = speed_rating(st); val[1] = st->lives; val[2] = st->fuel; val[3] = st->armor;

    ui_frame(r, x, y, 186, 97);
    ui_fill(r, x + 2, y + 2, 182, 93, (Rgb){ 14, 14, 18 });
    ui_fill(r, x + 2, y + 2, 182, 13, rim);
    ui_fill(r, x + 3, y + 3, 20, 11, (Rgb){ 12, 12, 16 });
    ui_text_bold(r, x + 5, y + 5, 1, UI_WHITE, who);
    ui_text_shadow(r, x + 28 + (154 - ui_text_width(name, 1)) / 2, y + 5, 1, UI_WHITE, name);
    SDL_RenderCopy(r, t->cards, &src, &dst);
    for (i = 0; i < 4; i++) {                                 /* the figures beside the picture */
        snprintf(buf, sizeof buf, "%d", val[i]);
        ui_text_bold(r, x + 98, y + 19 + i * 12, 1, c->label, lab[i]);
        ui_text(r, x + 180 - ui_text_width(buf, 1), y + 19 + i * 12, 1, c->value, buf);
    }
    for (i = 0; i < TANK_CLASS_COUNT; i++)
        ui_fill(r, x + 48 - 16 + i * 12, y + 73, 5, 5, i == ch ? c->value : c->frame_dark);
    if (!ready) {
        ui_icon(r, ICON_LEFT, x + 6, y + 72, c->value);
        ui_icon(r, ICON_RIGHT, x + 84, y + 72, c->value);
    }
    if (ready) {
        ui_fill(r, x + 4, y + 83, 178, 10, (Rgb){ 30, 70, 30 });
        ui_text_bold(r, x + (186 - ui_text_width("READY", 1)) / 2, y + 84, 1, (Rgb){ 140, 255, 140 }, "READY");
    } else {
        ui_text(r, x + (186 - ui_text_width(st->blurb, 1)) / 2, y + 84, 1, UI_GREY, st->blurb);
    }
}

/* the shared choices above the cards: game type and terrain */
static void shared_bar(SDL_Renderer *r, const Theme *t, const Style *styles, const SelectScreen *s, int x, int y, int w)
{
    const HudColors *c = &t->hud;
    char buf[64];
    ui_box(r, x, y, w, 14);
    snprintf(buf, sizeof buf, "%s", game_modes[s->mode].name);
    ui_text_bold(r, x + 6, y + 3, 1, c->value, buf);
    snprintf(buf, sizeof buf, "WORLD: %s", s->num_styles ? styles[s->style].title : "-");
    {
        int i;
        for (i = 0; buf[i]; i++) if (buf[i] >= 'a' && buf[i] <= 'z') buf[i] = (char)(buf[i] - 32);
    }
    ui_text(r, x + w - 20 - ui_text_width(buf, 1), y + 3, 1, UI_WHITE, buf);
    ui_icon(r, ICON_UP, x + w - 16, y + 1, c->value);
    ui_icon(r, ICON_DOWN, x + w - 9, y + 5, c->value);
}

void select_draw(SDL_Renderer *r, const Theme *t, const Style *styles, const SelectScreen *s,
                 int sw, int sh)
{
    const TankStats *st = &tank_stats[s->choice];
    const HudColors *c = &t->hud;
    const char *name = t->tank_names[s->choice][0] ? t->tank_names[s->choice] : st->name;
    int cx = (sw - CARD_W) / 2 + 40 + s->slide, cy = 4;
    int blink = 1;                                   /* nothing on this screen flashes */
    int px, py, i;
    SDL_Rect src = { s->choice * THEME_CARD_W, 0, THEME_CARD_W, THEME_CARD_H };
    SDL_Rect dst = { cx + 2, cy + 2, THEME_CARD_W, THEME_CARD_H };

    ui_backdrop(r, 0, 0, sw, sh);
    if (s->mode != MODE_SOLO) {
        int n = game_modes[s->mode].players, p;
        const char *go = input_pad_count() ? "EVERYONE: FIRE (A) WHEN READY   B: BACK" : "EVERYONE: FIRE WHEN READY   ESC: BACK";
        if (n == 2) {                                        /* two cards side by side */
            shared_bar(r, t, styles, s, 6, 3, sw - 12);
            for (p = 0; p < 2; p++) player_card(r, t, s, p, 6 + p * 188, 21);
            if (blink) ui_text_shadow(r, (sw - ui_text_width(go, 1)) / 2, 203, 1, c->value, go);
        } else {                                             /* four tiles, one per player */
            shared_bar(r, t, styles, s, 4, 2, sw - 8);
            for (p = 0; p < 4; p++) {
                int x = 4 + (p % 2) * 190, y = 18 + (p / 2) * 99;
                if (p < n) {
                    player_card_small(r, t, s, p, x, y);
                } else if (s->num_styles > 0) {             /* the free tile: the terrain */
                    const Style *stl = &styles[s->style];
                    ui_frame(r, x, y, 186, 97);
                    ui_fill(r, x + 2, y + 2, 182, 93, (Rgb){ 14, 14, 18 });
                    ui_text_bold(r, x + (186 - ui_text_width("WORLD", 1)) / 2, y + 6, 1, c->label, "WORLD");
                    ui_frame(r, x + 51, y + 18, 84, 52);
                    draw_preview(r, stl, x + 53, y + 20);
                    ui_text(r, x + (186 - ui_text_width(stl->title, 1)) / 2, y + 76, 1, c->value, stl->title);
                    ui_text(r, x + (186 - ui_text_width("FIRE WHEN READY", 1)) / 2, y + 86, 1, c->value, "FIRE WHEN READY");
                }
            }
        }
        return;
    }
    if (s->num_styles > 0)
        terrain_panel(r, styles, s, 8, 52);
    {   /* how many play: Tab (or a pad's View button) changes it */
        const char *mn = game_modes[s->mode].name;
        ui_box(r, 8, 8, 120, 14);
        ui_text_bold(r, 8 + (120 - ui_text_width(mn, 1)) / 2, 11, 1, c->value, mn);
        ui_text(r, 8 + (120 - ui_text_width(input_pad_count() ? "VIEW: MODE" : "TAB: MODE", 1)) / 2, 26, 1, UI_GREY,
                input_pad_count() ? "VIEW: MODE" : "TAB: MODE");
    }
    /* picture with the tank's name */
    ui_frame(r, cx, cy, CARD_W, THEME_CARD_H + 4);
    SDL_RenderCopy(r, t->cards, &src, &dst);
    ui_text_shadow(r, cx + (CARD_W - ui_text_width(name, 2) - 2) / 2, cy + 7, 2, c->value, name);

    /* riveted stats panel */
    py = cy + THEME_CARD_H + 6;
    ui_panel(r, cx, py, CARD_W, 62);
    px = cx + 9;
    stat_box(r, px, py + 6, 80, "SPEED", speed_rating(st), 2);
    stat_box(r, px + 82, py + 6, 80, "LIVES", st->lives, 2);
    icon_box(r, px + 8, py + 24, 36, ICON_MINE, UI_GREY, st->mines);
    for (i = 0; i < TANK_CLASS_COUNT; i++)                 /* which of the three */
        ui_fill(r, cx + CARD_W / 2 - 18 + i * 14, py + 28, 6, 6,
                i == s->choice ? c->value : c->frame_dark);
    icon_box(r, px + 118, py + 24, 36, ICON_POW, c->frame, st->pows);
    stat_box(r, px, py + 42, 80, "FUEL", st->fuel, 3);
    stat_box(r, px + 82, py + 42, 80, "ARMOR", st->armor, 2);

    py += 66;
    ui_box(r, cx, py, CARD_W, 14);
    ui_text(r, cx + (CARD_W - ui_text_width(st->blurb, 1)) / 2, py + 3, 1, UI_GREY, st->blurb);
    if (blink) {
        const char *go = s->mode == MODE_SOLO ? "PRESS FIRE" : "ALL PRESS FIRE";
        ui_text_shadow(r, cx + (CARD_W - ui_text_width(go, 1) - 1) / 2, py + 20, 1, c->value, go);
    }

    if (!blink || s->slide) {
        ui_icon(r, ICON_LEFT,  cx - 14, cy + 50, c->value);
        ui_icon(r, ICON_RIGHT, cx + CARD_W + 6, cy + 50, c->value);
    }
}
