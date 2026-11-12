/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include "platform/sdl2/teams.h"
#include "common/botnames.h"
#include "common/roles.h"
#include "platform/sdl2/input.h"
#include "platform/sdl2/profiles.h"
#include "platform/sdl2/render.h"
#include "platform/sdl2/ui.h"
#include "core/game.h"

static int humans_on(const Teams *t, int side)
{
    int i, n = 0;
    for (i = 0; i < t->humans; i++) n += t->side[i] == side;
    return n;
}

int teams_count(const Teams *t, int side) { return humans_on(t, side) + t->bots[side]; }

static int total(const Teams *t) { return t->humans + t->bots[0] + t->bots[1]; }

static void note(Teams *t, const char *msg)
{
    snprintf(t->note, sizeof t->note, "%s", msg);
    t->note_until = SDL_GetTicks() + 2500;
}

static InputBits team_input(const Teams *t, int player)
{
    int dev = t->dev[player];
    return dev < 0 ? input_player1_menu() : input_device_menu(dev);
}

static void find_leaders(Teams *t)
{
    int i;
    t->leader[0] = t->leader[1] = -1;
    if (t->type == TEAMS_ANNIHILATION) return;
    for (i = 0; i < t->humans; i++) {
        int side = t->side[i], lead = t->leader[side];
        if (lead < 0 || t->side_order[i] < t->side_order[lead]) t->leader[side] = i;
    }
}

int teams_rename(Teams *t, int player)
{
    int team;
    if (t->type == TEAMS_ANNIHILATION || player < 0 || player >= t->humans) return 0;
    team = t->side[player];
    if (t->leader[team] != player) return 0;
    t->editing_team = team + 1;
    snprintf(t->edit_name, sizeof t->edit_name, "%s", t->side_name[team]);
    SDL_StartTextInput();
    return 1;
}

int teams_name_event(Teams *t, const SDL_Event *event)
{
    int team = t->editing_team - 1;
    size_t n;
    if (team < 0) return 0;
    n = strlen(t->edit_name);
    if (event->type == SDL_TEXTINPUT) {
        const unsigned char *ch = (const unsigned char *)event->text.text;
        for (; *ch && n + 1 < sizeof t->edit_name; ch++)
            if (*ch >= 32 && *ch <= 126) t->edit_name[n++] = (char)*ch;
        t->edit_name[n] = 0;
    } else if (event->type == SDL_KEYDOWN) {
        switch (event->key.keysym.sym) {
        case SDLK_BACKSPACE: if (n) t->edit_name[n - 1] = 0; break;
        case SDLK_RETURN: case SDLK_KP_ENTER:
            while (n && t->edit_name[n - 1] == ' ') t->edit_name[--n] = 0;
            snprintf(t->side_name[team], sizeof t->side_name[team], "%s",
                     n ? t->edit_name : team ? "FORTRESS" : "CAMP");
            /* fall through */
        case SDLK_ESCAPE:
            t->editing_team = 0;
            memset(t->prev, 0xff, sizeof t->prev);
            SDL_StopTextInput();
            break;
        }
    }
    return 1;
}

void teams_begin(Teams *t, int type, int humans, const int *devs, char (*names)[16], const int *avatars,
                 const int *cls, const int bots[2], int bot_level, int cap_camp, int cap_fortress, int max_total)
{
    int i;
    memset(t, 0, sizeof *t);
    t->name_seed = SDL_GetTicks() * 2654435761u ^ (uint32_t)SDL_GetPerformanceCounter();   /* (new names, each time) */
    t->type = type;
    snprintf(t->side_name[0], sizeof t->side_name[0], "CAMP");
    snprintf(t->side_name[1], sizeof t->side_name[1], "FORTRESS");
    t->humans = humans < 1 ? 1 : humans > 4 ? 4 : humans;
    t->cap[0] = cap_camp; t->cap[1] = cap_fortress;
    t->max_total = max_total;
    t->bot_level = bot_level < AI_EASY || bot_level > AI_HARD ? AI_NORMAL : bot_level;
    for (i = 0; i < t->humans; i++) {                    /* the people: as evenly as they'll go */
        int want = i % 2, j, on = 0;
        t->dev[i] = devs ? devs[i] : -1;
        t->cls[i] = cls ? (type == TEAMS_CLASSIC ? tank_chassis(cls[i]) : cls[i]) : TANK_MEDIUM;
        snprintf(t->name[i], sizeof t->name[i], "%.15s", names ? names[i] : "PLAYER");
        t->avatar[i] = avatars ? avatars[i] : -1;
        for (j = 0; j < i; j++) on += t->side[j] == want;   /* (that side full? counting those placed so far) */
        if (on >= t->cap[want]) want = !want;
        t->side[i] = want;
        t->side_order[i] = t->join_order++;
    }
    find_leaders(t);
    for (i = 0; i < t->humans; i++)
        t->prev[i] = team_input(t, i) | IN_FIRE | IN_MINE;   /* ignore the press that opened this screen */
    {   /* the computer players, one by one to the smaller side (as many as fit) */
        int left = bots ? bots[0] + bots[1] : 0;
        t->bots[0] = t->bots[1] = 0;
        while (left-- > 0 && total(t) < t->max_total) {
            int to = teams_count(t, 0) <= teams_count(t, 1) ? 0 : 1;
            if (teams_count(t, to) >= t->cap[to]) to = !to;
            if (teams_count(t, to) >= t->cap[to]) break;
            t->bots[to]++;
        }
    }
}

int teams_add_player(Teams *t, int dev, const char *name, int avatar, int cls)
{
    int i, player = t->humans, side = t->side[0] ^ 1;
    if (player >= 4 || player >= t->max_total) return 0;
    if (!t->bots[side]) {
        if (t->bots[side ^ 1]) side ^= 1;
        else if (teams_count(t, side) >= t->cap[side]) side ^= 1;
    }
    if (t->bots[side]) t->bots[side]--;
    else if (teams_count(t, side) >= t->cap[side]) return 0;
    t->dev[player] = dev;
    t->side[player] = side;
    t->cls[player] = t->type == TEAMS_CLASSIC ? tank_chassis(cls) : cls;
    t->avatar[player] = avatar;
    snprintf(t->name[player], sizeof t->name[player], "%.15s", name);
    t->ready[player] = 0;
    t->side_order[player] = t->join_order++;
    t->humans++;
    find_leaders(t);
    for (i = 0; i < t->humans; i++) t->prev[i] = 0xff;
    return 1;
}

void teams_remove_player(Teams *t, int player)
{
    int i;
    if (player < 0 || player >= t->humans) return;
    t->bots[t->side[player]]++;
    t->humans--;
    for (i = player; i < t->humans; i++) {
        t->dev[i] = t->dev[i + 1];
        t->side[i] = t->side[i + 1];
        t->cls[i] = t->cls[i + 1];
        t->ready[i] = t->ready[i + 1];
        t->avatar[i] = t->avatar[i + 1];
        t->side_order[i] = t->side_order[i + 1];
        memcpy(t->name[i], t->name[i + 1], sizeof t->name[i]);
    }
    if (player == 0) {
        t->host_buttons = 0;
        t->ready[0] = 0;
    }
    memset(t->prev, 0xff, sizeof t->prev);
    find_leaders(t);
}

/* player 1's: AI bots more, or fewer. The team modes keep the sides even: uneven, the smaller side's
 * filled up to the other; even, one each side (two at once). Every tank for itself: one at a time. */
int teams_add_bot(Teams *t)
{
    int c0 = teams_count(t, 0), c1 = teams_count(t, 1), added = 0;
    if (t->type == TEAMS_ANNIHILATION) {
        int to = c0 <= c1 ? 0 : 1;
        if (c0 + c1 >= t->max_total || teams_count(t, to) >= t->cap[to]) { note(t, "NO ROOM FOR ANOTHER ON THIS MAP"); return 0; }
        t->bots[to]++;
        return 1;
    }
    if (c0 != c1) {                                      /* uneven: the smaller side up to the other */
        int to = c0 < c1 ? 0 : 1;
        while (teams_count(t, to) < teams_count(t, !to) && teams_count(t, to) < t->cap[to] && total(t) < t->max_total) { t->bots[to]++; added++; }
    } else if (c0 < t->cap[0] && c1 < t->cap[1] && total(t) + 2 <= t->max_total) {   /* even: one each side */
        t->bots[0]++; t->bots[1]++; added = 2;
    }
    if (!added) note(t, "NO ROOM FOR MORE ON THIS MAP");
    return added;
}

int teams_remove_bot(Teams *t)
{
    int c0 = teams_count(t, 0), c1 = teams_count(t, 1), gone = 0;
    if (t->type == TEAMS_ANNIHILATION) {
        int from = t->bots[0] >= t->bots[1] ? 0 : 1;
        if (!t->bots[from]) { note(t, "NO AI BOTS TO TAKE AWAY"); return 0; }
        t->bots[from]--;
        return 1;
    }
    if (c0 != c1) {                                      /* uneven: the bigger side's down to the other */
        int from = c0 > c1 ? 0 : 1;
        while (teams_count(t, from) > teams_count(t, !from) && t->bots[from]) { t->bots[from]--; gone++; }
    } else if (t->bots[0] && t->bots[1]) {               /* even: one from each side */
        t->bots[0]--; t->bots[1]--; gone = 2;
    }
    if (!gone) note(t, t->bots[0] + t->bots[1] ? "THE SIDES WOULDN'T BE EVEN: PEOPLE ON ONE SIDE" : "NO AI BOTS TO TAKE AWAY");
    return gone;
}

/* ready to start: everyone else ready, someone on each side, even sides in the team modes; why not (or "") */
static const char *start_blocked(const Teams *t, char *buf, size_t cap)
{
    int i, waiting = 0;
    for (i = 1; i < t->humans; i++) waiting += !t->ready[i];
    if (waiting) { snprintf(buf, cap, "%d NOT READY", waiting); return buf; }
    if (!teams_count(t, 0) || !teams_count(t, 1)) return "SOMEONE ON EACH SIDE";
    if (t->type != TEAMS_ANNIHILATION && teams_count(t, 0) != teams_count(t, 1)) return "THE SIDES AREN'T EVEN";
    return "";
}

/* Each player: LEFT/RIGHT the side, UP/DOWN the tank, FIRE ready, MINE not ready (again). Player 1, the
 * host: FIRE to the buttons in the middle (ADD AI BOTS, REMOVE AI BOTS, START GAME: UP/DOWN, FIRE presses;
 * START when everyone else is ready), MINE back from them; MINE before that: back to the menu. */
int teams_frame(Teams *t)
{
    int i;
    int choices = TANK_CLASS_COUNT + (t->type != TEAMS_CLASSIC);
    char why[40];
    if (t->editing_team) return TEAMS_NOTHING;
    for (i = 0; i < t->humans; i++) {
        InputBits b = team_input(t, i), pr;   /* (a pad: A ready, B back) */
        pr = b & ~t->prev[i];
        t->prev[i] = b;
        if (i == 0 && t->host_buttons) {                 /* the host, at the buttons */
            if (pr & IN_UP) t->host_sel = (t->host_sel + HB_COUNT - 1) % HB_COUNT;
            if (pr & IN_DOWN) t->host_sel = (t->host_sel + 1) % HB_COUNT;
            if (pr & IN_FIRE) {
                if (t->host_sel == HB_ADD) teams_add_bot(t);
                else if (t->host_sel == HB_REMOVE) teams_remove_bot(t);
                else if (t->host_sel == HB_LEVEL) t->bot_level = t->bot_level >= AI_ADAPTIVE ? AI_EASY : t->bot_level + 1;   /* (round) */
                else if (start_blocked(t, why, sizeof why)[0]) note(t, start_blocked(t, why, sizeof why));
                else return TEAMS_START;
            }
            if (pr & IN_MINE) { t->host_buttons = 0; t->ready[0] = 0; }   /* back to the side and tank */
            continue;
        }
        if (!t->ready[i]) {                              /* choosing: the side (left, right), the tank (up, down) */
            if ((pr & (IN_LEFT | IN_RIGHT)) && t->type != TEAMS_ANNIHILATION) {
                int to = (pr & IN_LEFT) ? 0 : 1, from = t->side[i];
                if (to != from) {
                    if (t->bots[to] > 0) { t->bots[to]--; t->bots[from]++; t->side[i] = to; }   /* a computer player swaps with you */
                    else if (teams_count(t, to) >= t->cap[to]) note(t, to ? "THE FORTRESS IS FULL" : "THE CAMP IS FULL");
                    else t->side[i] = to;
                    if (t->side[i] != from) {
                        t->side_order[i] = t->join_order++;
                        find_leaders(t);
                    }
                }
            }
            if (pr & IN_UP) t->cls[i] = (t->cls[i] + choices - 1) % choices;
            if (pr & IN_DOWN) t->cls[i] = (t->cls[i] + 1) % choices;
            if (pr & (IN_UP | IN_DOWN)) { t->stat_cls = t->cls[i]; t->stat_until = SDL_GetTicks() + 3000; }   /* (its stats, a moment) */
            if (pr & IN_FIRE) {
                t->ready[i] = 1;
                if (i == 0) {                            /* the host: on to the Start button */
                    t->host_buttons = 1;
                    t->host_sel = HB_START;
                }
            }
            if ((pr & IN_MINE) && i == 0) return TEAMS_BACK;
        } else if (pr & IN_MINE) t->ready[i] = 0;        /* not ready: change it again */
    }
    return TEAMS_NOTHING;
}

void teams_setup(const Teams *t, MatchSetup *ms)
{
    int i, s, k, n = 0, humans[2] = { 0, 0 };
    ms->leader_mask = 0;
    for (i = 0; i < t->humans && n < MATCH_PLAYERS; i++, n++) {   /* the players here: tanks 0.. */
        ms->team[n] = (uint8_t)(t->side[i] ? TEAM_ENEMY : TEAM_PLAYER);
        if (t->leader[t->side[i]] == i) ms->leader_mask |= (uint8_t)(1u << n);
        humans[ms->team[n]]++;
        ms->cls[n] = (uint8_t)tank_chassis(t->cls[i]);
        if (t->type != TEAMS_CLASSIC && t->cls[i] == TANK_RHINO) ms->rocket_tanks |= (uint8_t)(1u << n);
        ms->bot[n] = 0;
    }
    for (s = 0; s < 2; s++)                              /* then the computer's */
        for (k = 0; k < t->bots[s] && n < MATCH_PLAYERS; k++, n++) {
            ms->team[n] = (uint8_t)(s ? TEAM_ENEMY : TEAM_PLAYER);
            ms->cls[n] = (uint8_t)((k + s + 1) % 3);
            ms->bot[n] = (uint8_t)(t->bot_level > AI_HARD ? AI_NORMAL : t->bot_level);   /* (adaptive: the enemy tank's; bots normal) */
            if (ms->bot[n] && t->type == TEAMS_FLAG && (humans[s] || k > 0 || t->bots[s] == 1))
                ms->bot[n] |= MATCH_BOT_RAIDER | MATCH_BOT_SIEGE;   /* only a bot-only side leaves an opening defender */
        }
    ms->players = (uint8_t)n;
    ms->versus = 1;                                      /* (side against side, every mode) */
    if (t->type == TEAMS_CONQUEST) ms->extras |= EXTRA_CONQUEST;
    if (t->type == TEAMS_ANNIHILATION) ms->extras |= EXTRA_ANNIHILATION;
    if (t->type == TEAMS_TDM) ms->extras |= EXTRA_TDM;
    if (t->type == TEAMS_WARLORD) ms->extras |= EXTRA_WARLORD;
    if (t->type == TEAMS_KILLCONF) ms->extras |= EXTRA_KILLCONF;
    if (t->type == TEAMS_HARDPOINT) ms->extras |= EXTRA_HARDPOINT;
}

/* ---- the picture: the camp on the left, the fortress on the right ---------------------------- */
static int vehicle_biome;
static const char *tank_name(int cls) { return cls == TANK_RHINO ? "RHINO" : world_tank_stats(vehicle_biome, (TankClass)cls)->name; }
static const char *level_name(int l) { return l == AI_EASY ? "EASY" : l == AI_HARD ? "HARD" : l == AI_ADAPTIVE ? "ADAPTIVE" : "NORMAL"; }

static SDL_Texture *world_tanks;                    /* the world's sprites (NULL: the theme's cards) */
void teams_set_tanks(SDL_Texture *tanks) { world_tanks = tanks; }

/* a slim row: avatar (or AI), the name whole, the tank's name under it, a small tank at the end.
 * focus: the player's chosen row, shown (-1: none; TF_SIDE: arrows at the ends; TF_TANK: "< BISON >") */
static void member(SDL_Renderer *r, const Theme *th, int x, int y, int w, const char *name, int avatar, int cls, int ready, int bot,
                   int focus, int team, int roles)
{
    char buf[32];
    Rgb hi = ui_colors()->value;
    ui_fill(r, x, y, w, 20, ready ? (Rgb){ 26, 52, 26 } : bot ? (Rgb){ 20, 20, 18 } : (Rgb){ 30, 30, 24 });
    if (!bot) ui_frame(r, x, y, w, 20);                  /* (a person: their row stands out) */
    if (bot) {
        ui_fill(r, x + 2, y + 2, 16, 16, (Rgb){ 70, 74, 82 });
        ui_text(r, x + 3, y + 6, 1, (Rgb){ 200, 230, 255 }, "AI");
    } else if (avatar >= 0) signin_draw_avatar(r, x + 2, y + 2, 16, avatar, name);
    else ui_fill(r, x + 2, y + 2, 16, 16, (Rgb){ 60, 60, 50 });
    snprintf(buf, sizeof buf, "%.13s", name);
    for (char *q = buf; *q; q++) if (*q >= 'a' && *q <= 'z') *q = (char)(*q - 32);
    while (strlen(buf) > 1 && ui_text_width(buf, 1) > w - 22 - 20) buf[strlen(buf) - 1] = 0;   /* (the name: the room it has) */
    ui_text_bold(r, x + 22, y + 2, 1, bot ? (Rgb){ 180, 180, 170 } : UI_WHITE, buf);
    if ((focus == TF_TANK || focus == TF_BOTS) && !ready) {   /* choosing: "< BISON >" if it fits (else the name, lit) */
        snprintf(buf, sizeof buf, "< %s >", tank_name(cls));
        if (ui_text_width(buf, 1) > w - 22 - 20) snprintf(buf, sizeof buf, "%s", tank_name(cls));
    }
    else snprintf(buf, sizeof buf, "%s", ready && !bot ? "READY" : tank_name(cls));
    ui_text(r, x + 22, y + 11, 1, (focus == TF_TANK || focus == TF_BOTS) && !ready ? hi : ready && !bot ? (Rgb){ 150, 255, 150 } : UI_GREY, buf);
    if (world_tanks) {                                   /* the tank, small, in its side's colours (the world's sprite, facing up) */
        render_tank_icon(r, world_tanks, cls, team, x + w - 11, y + 11, 28);
    } else if (th && th->cards) {                        /* (no world sprites: the theme's card) */
        SDL_Rect src = { tank_chassis(cls) * THEME_CARD_W, 0, THEME_CARD_W, THEME_CARD_H }, dst = { x + w - 33, y + 2, 31, 17 };
        SDL_RenderCopy(r, th->cards, &src, &dst);
    }
    ui_role_badges(r, x + 18, y + 11, roles);
    if ((focus == TF_SIDE || focus == TF_BOTS) && !ready) {   /* (choosing: the way across, both ends) */
        ui_icon(r, ICON_LEFT, x - 8, y + 6, hi);
        ui_icon(r, ICON_RIGHT, x + w + 1, y + 6, hi);
    }
}

static const Game *teams_map;                        /* the map it'll be: the LOCAL GAME page's preview */
void teams_set_map(const Game *g) { teams_map = g; vehicle_biome = g ? g->biome : BIOME_CLASSIC; }

/* the map between the panels: where each side starts (camps, or an arena's spawn points by half) */
static void draw_starts(SDL_Renderer *r, const Teams *t, int x, int y, int w)
{
    SDL_Rect v = { x, y, w, w };
    int k;
    const Game *g = teams_map;
    if (!g) return;
    ui_frame(r, x - 2, y - 2, w + 4, w + 4);
    render_map_preview(r, g, &v, 0);
    if (g->map.arena) {                                  /* an arena: its spawn points, the side's colour (all: white) */
        for (k = 0; k < g->map.num_spawns; k++) {
            int sx = x + g->map.spawn_tx[k] * w / g->map.w, sy = y + g->map.spawn_ty[k] * w / g->map.h;
            int camp = (g->map.spawn_tx[k] + g->map.spawn_ty[k]) < g->map.w;
            Rgb col = t->type == TEAMS_ANNIHILATION ? UI_WHITE : render_side_rgb(camp ? TEAM_PLAYER : TEAM_ENEMY);
            ui_fill(r, sx - 2, sy - 2, 5, 5, (Rgb){ 10, 10, 8 });
            ui_fill(r, sx - 1, sy - 1, 3, 3, col);
        }
    } else {                                             /* camps: where each side's garages are */
        for (k = 0; k < 2; k++) {
            const Base *b = &g->bases[k];
            int bx = x + (b->x0 + b->x1) / 2 * w / g->map.w, by = y + (b->y0 + b->y1) / 2 * w / g->map.h;
            Rgb col = render_side_rgb(k);
            ui_fill(r, bx - 4, by - 4, 9, 9, (Rgb){ 10, 10, 8 });
            ui_fill(r, bx - 3, by - 3, 7, 7, col);
        }
    }
    ui_text(r, x + (w - ui_text_width("STARTS", 1)) / 2, y + w + 4, 1, UI_GREY, "STARTS");
}

void teams_draw(SDL_Renderer *r, const Theme *th, const Teams *t, int sw, int sh)
{
    vehicle_biome = t->type == TEAMS_CLASSIC || !teams_map ? BIOME_CLASSIC : teams_map->biome;
    static const char *types[TEAMS_MODES] = { "CAPTURE THE FLAG", "CONQUEST", "ANNIHILATION", "TEAM DEATHMATCH",
                                              "WARLORD", "CLASSIC (THE 1987 RULES)" , "KILL CONFIRMED", "HARDPOINT" };
    const HudColors *c = &th->hud;
    int s, i, pw = 132, mx = 8 + pw + 4, mw = sw - 2 * (8 + pw + 4);   /* (the sides; the map between) */
    char buf[64];
    ui_backdrop(r, 0, 0, sw, sh);
    ui_fill(r, 0, 4, sw, 16, (Rgb){ 18, 26, 50 });
    snprintf(buf, sizeof buf, "%s", types[t->type % TEAMS_MODES]);
    ui_text_bold(r, (sw - ui_text_width(buf, 1)) / 2, 8, 1, c->value, buf);
    if (t->type == TEAMS_ANNIHILATION) {                 /* every tank for itself: one list, two columns, no sides */
        int x = 8, y = 26, w = sw - 16, cw = (w - 20) / 2, n = 0, k;
        ui_frame(r, x, y, w, 154);
        ui_fill(r, x + 2, y + 2, w - 4, 150, (Rgb){ 16, 18, 26 });
        ui_fill(r, x + 2, y + 2, w - 4, 13, (Rgb){ 150, 60, 40 });
        snprintf(buf, sizeof buf, "EVERY TANK FOR ITSELF  %d/%d", total(t), t->max_total);
        ui_text_bold(r, x + (w - ui_text_width(buf, 1)) / 2, y + 5, 1, UI_WHITE, buf);
        for (i = 0; i < t->humans; i++, n++)
            member(r, th, x + 10 + (n % 2) * (cw + 4), y + 20 + 24 * (n / 2), cw - 6, t->name[i], t->avatar[i], t->cls[i],
                   t->ready[i], 0, TF_TANK, 0, i == 0 ? ROLE_HOST : 0);
        for (k = 0; k < t->bots[0] + t->bots[1]; k++, n++)
            member(r, th, x + 10 + (n % 2) * (cw + 4), y + 20 + 24 * (n / 2), cw - 6, bot_name(t->name_seed, t->humans + k),
                   -1, (k + 1) % 3, 0, 1, -1, 0, 0);
    } else {
        for (s = 0; s < 2; s++) {                        /* the two sides: a list each */
            int x = s ? sw - 8 - pw : 8, y = 26, row = 0;
            Rgb head = render_side_rgb(s);
            ui_frame(r, x, y, pw, 154);
            ui_fill(r, x + 2, y + 2, pw - 4, 150, (Rgb){ 16, 18, 26 });
            ui_fill(r, x + 2, y + 2, pw - 4, 13, head);
            snprintf(buf, sizeof buf, "%s %d/%d", t->side_name[s], teams_count(t, s), t->cap[s]);
            ui_text_bold(r, x + (pw - ui_text_width(buf, 1)) / 2, y + 5, 1, (Rgb){ 20, 18, 10 }, buf);
            if (t->leader[s] >= 0) {
                snprintf(buf, sizeof buf, "F%d RENAME", t->leader[s] + 1);
                ui_text(r, x + (pw - ui_text_width(buf, 1)) / 2, 149, 1, UI_GREY, buf);
            }
            for (i = 0; i < t->humans; i++)
                if (t->side[i] == s) member(r, th, x + 10, y + 20 + 24 * row++, pw - 20, t->name[i], t->avatar[i], t->cls[i],
                                          t->ready[i], 0, TF_BOTS, s, (i == 0 ? ROLE_HOST : 0) | (t->leader[s] == i ? ROLE_LEADER : 0));
            for (i = 0; i < t->bots[s]; i++)
                member(r, th, x + 10, y + 20 + 24 * row++, pw - 20, bot_name(t->name_seed, t->humans + (s ? t->bots[0] : 0) + i), -1,
                       (i + s + 1) % 3, 0, 1, -1, s, 0);
            if (!row) ui_text(r, x + (pw - ui_text_width("NOBODY YET", 1)) / 2, y + 70, 1, UI_GREY, "NOBODY YET");
        }
        {   /* between them: the map, where everyone starts, and how many a side */
            int ms = mw - 4 < 76 ? mw - 4 : 76;
            if (teams_map) draw_starts(r, t, mx + (mw - ms) / 2, 30, ms);
            snprintf(buf, sizeof buf, "%d VS %d", teams_count(t, 0), teams_count(t, 1));
            ui_text_bold(r, mx + (mw - ui_text_width(buf, 1)) / 2, 30 + ms + 14, 1, UI_WHITE, buf);
        }
    }
    {   /* the host's buttons, in the middle (under the map; every tank for itself: under the list) */
        const char *lbl[HB_COUNT] = { "+ BOTS", "- BOTS", level_name(t->bot_level), "START" };
        char why[40];
        const char *blocked = start_blocked(t, why, sizeof why);
        int bw = t->type == TEAMS_ANNIHILATION ? 110 : mw - 8, bx = t->type == TEAMS_ANNIHILATION ? (sw - bw) / 2 : mx + 4;
        int by = t->type == TEAMS_ANNIHILATION ? 122 : 133, k;
        if (t->type == TEAMS_ANNIHILATION) bx = sw - 8 - bw - 6;
        for (k = 0; k < HB_COUNT; k++) {
            int on = t->host_buttons && t->host_sel == k, off = k == HB_START && blocked[0];
            Rgb bg = on ? c->value : (Rgb){ 36, 34, 28 }, fg = on ? (Rgb){ 20, 18, 10 } : off ? UI_GREY : UI_WHITE;
            int yy = by + k * 12;
            ui_fill(r, bx, yy, bw, 11, bg);
            if (!on) ui_frame(r, bx, yy, bw, 11);
            ui_text_bold(r, bx + (bw - ui_text_width(lbl[k], 1)) / 2, yy + 2, 1, fg, lbl[k]);
            if (on) ui_text_bold(r, bx + 3, yy + 2, 1, fg, ">");   /* (the one chosen) */
        }
        if (t->host_buttons && blocked[0])               /* (why START isn't ready yet) */
            ui_text(r, bx + (bw - ui_text_width(blocked, 1)) / 2, by - 10, 1, UI_GREY, blocked);   /* (above the buttons) */
    }
    if (SDL_GetTicks() < t->stat_until) {               /* a tank just chosen: its speed, armour, gun (of 5, against the best) */
        const TankStats *ts = world_tank_stats(vehicle_biome, (TankClass)tank_chassis(t->stat_cls));
        int best_v = 0, best_a = 0, best_r = 255, k, q, xx, val[3];
        static const char *nm[3] = { "SPEED", "ARMOUR", "GUN" };
        for (k = 0; k < 3; k++) {
            if (world_tank_stats(vehicle_biome, (TankClass)k)->max_fwd > best_v) best_v = world_tank_stats(vehicle_biome, (TankClass)k)->max_fwd;
            if (world_tank_stats(vehicle_biome, (TankClass)k)->armor > best_a) best_a = world_tank_stats(vehicle_biome, (TankClass)k)->armor;
            if (world_tank_stats(vehicle_biome, (TankClass)k)->reload < best_r) best_r = world_tank_stats(vehicle_biome, (TankClass)k)->reload;
        }
        val[0] = (ts->max_fwd * 5 + best_v / 2) / (best_v ? best_v : 1);
        val[1] = (ts->armor * 5 + best_a / 2) / (best_a ? best_a : 1);
        val[2] = (best_r * 5 + ts->reload / 2) / (ts->reload ? ts->reload : 1);
        snprintf(buf, sizeof buf, "%s", tank_name(t->stat_cls));
        xx = (sw - 300) / 2;
        ui_text_bold(r, xx, 186, 1, c->value, buf);
        xx += 70;
        for (k = 0; k < 3; k++) {
            ui_text(r, xx, 186, 1, UI_GREY, nm[k]);
            xx += ui_text_width(nm[k], 1) + 4;
            for (q = 0; q < 5; q++) { ui_fill(r, xx + q * 5, 187, 4, 6, q < val[k] ? c->value : (Rgb){ 50, 48, 40 }); }
            xx += 32;
        }
    } else if (t->humans < 4 && t->humans < t->max_total) {
        const char *key = NULL;
        int dev;
        for (dev = DEV_KEYS1; dev < DEV_PAD1 && !key; dev++)
            if (!signin_is_in(dev)) key = input_join_key_name(dev);
        if (key) snprintf(buf, sizeof buf, "P%d JOIN: %.16s OR A ON ANOTHER PAD", t->humans + 1, key);
        else snprintf(buf, sizeof buf, "P%d JOIN: A ON ANOTHER PAD", t->humans + 1);
        ui_text(r, (sw - ui_text_width(buf, 1)) / 2, 186, 1, UI_GREY, buf);
    } else {
        snprintf(buf, sizeof buf, "%d OF %d IN THE MATCH, %d AI BOTS (%s)", total(t), t->max_total, t->bots[0] + t->bots[1], level_name(t->bot_level));
        ui_text(r, (sw - ui_text_width(buf, 1)) / 2, 186, 1, UI_GREY, buf);
    }
    if (SDL_GetTicks() < t->note_until) snprintf(buf, sizeof buf, "%s", t->note);
    else if (t->host_buttons) {
        for (i = 1; i < t->humans && t->ready[i]; i++) {}
        if (i < t->humans && t->host_sel == HB_START)
            snprintf(buf, sizeof buf, "P%d FIRE TO READY   P1 MINE EDIT TANK", i + 1);
        else snprintf(buf, sizeof buf, "P1 FIRE %s   MINE EDIT TANK", t->host_sel == HB_START ? "START MATCH" : "SELECT");
    }
    else snprintf(buf, sizeof buf, t->type == TEAMS_ANNIHILATION ? "UP/DOWN: TANK   FIRE: READY   BACK: UNDO"
                                                                  : "LEFT/RIGHT: SIDE  UP/DOWN: TANK  FIRE: READY");
    ui_text(r, (sw - ui_text_width(buf, 1)) / 2, 202, 1, SDL_GetTicks() < t->note_until ? (Rgb){ 255, 150, 110 } : c->value, buf);
    if (t->editing_team) {
        ui_panel(r, (sw - 210) / 2, 71, 210, 70);
        ui_text_bold(r, (sw - 184) / 2, 82, 1, c->value, "RENAME TEAM");
        ui_box(r, (sw - 184) / 2, 97, 184, 16);
        ui_text(r, (sw - 170) / 2, 101, 1, UI_WHITE, t->edit_name);
        ui_text(r, (sw - 184) / 2, 123, 1, UI_GREY, "ENTER SAVE   ESC CANCEL");
    }
}
