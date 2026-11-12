/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/sdl2/netui.h"
#include "platform/sdl2/input.h"
#include "platform/sdl2/render.h"
#include "platform/sdl2/ui.h"
#include "common/http.h"
#include "core/game.h"

/* the menu's colours */
static const Rgb STEEL = { 24, 26, 22 }, STEEL_HI = { 58, 60, 52 }, BRONZE = { 168, 122, 56 },
                 LABEL = { 204, 196, 170 }, VALUE = { 250, 196, 64 }, DIM = { 104, 100, 86 },
                 BAR = { 232, 176, 48 }, INK = { 22, 16, 8 }, LINE = { 54, 54, 46 },
                 GOOD = { 140, 230, 120 }, BAD = { 240, 100, 80 }, OURS = { 90, 150, 255 };

static void plate(SDL_Renderer *r, int x, int y, int w, int h)
{
    ui_fill(r, x, y, w, h, (Rgb){ 0, 0, 0 });
    ui_fill(r, x + 1, y + 1, w - 2, h - 2, BRONZE);
    ui_fill(r, x + 2, y + 2, w - 4, h - 4, (Rgb){ 0, 0, 0 });
    ui_fill(r, x + 3, y + 3, w - 6, h - 6, STEEL);
    ui_fill(r, x + 3, y + 3, w - 6, 1, STEEL_HI);
}

static void dim_backdrop(SDL_Renderer *r, int sw, int sh)
{
    SDL_Rect all = { 0, 0, sw, sh };
    ui_backdrop(r, 0, 0, sw, sh);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 160);
    SDL_RenderFillRect(r, &all);
}

static void upper(char *s) { for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 32); }

static void text_fit(SDL_Renderer *r, int x, int y, int w, Rgb col, const char *s)
{
    char buf[256];
    int n;
    snprintf(buf, sizeof buf, "%s", s);
    upper(buf);
    for (n = (int)strlen(buf); n > 0 && ui_text_width(buf, 1) > w; n--) buf[n - 1] = 0;
    ui_text(r, x, y, 1, col, buf);
}

/* ---- the server browser ------------------------------------------------------------ */

int browser_fetch(Browser *b, const char *listing_url)
{
    static char body[65536];
    char url[400];
    int status;
    b->n = 0; b->sel = 0; b->top = 0; b->fetched = 1;
    if (!listing_url[0]) { snprintf(b->status, sizeof b->status, "no listing set (NETWORK GAME -> LISTING)"); return -1; }
    snprintf(url, sizeof url, "%s%saction=list&format=text", listing_url, strchr(listing_url, '?') ? "&" : "?");
    status = http_request(url, NULL, body, sizeof body, 4000);
    if (status != 200) {
        snprintf(b->status, sizeof b->status, status < 0 ? "the listing doesn't answer" : "the listing said %d", status);
        return -1;
    }
    return browser_parse(b, body);
}

int browser_parse(Browser *b, char *body)
{
    char *line, *next;
    b->n = 0; b->sel = 0; b->top = 0; b->fetched = 1;
    for (line = body; line && *line && b->n < 64; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        {
        BrowserEntry *e = &b->e[b->n];
        char *f[15], *p = line;
        int k;
        for (k = 0; k < 15; k++) { f[k] = p; p = strchr(p, '\t'); if (!p) { k++; break; } *p++ = 0; }
        if (k < 10) continue;
        memset(e, 0, sizeof *e);
        snprintf(e->host, sizeof e->host, "%.63s", f[0]);
        e->port = atoi(f[1]);
        snprintf(e->name, sizeof e->name, "%.*s", (int)sizeof e->name - 1, f[2]);
        e->players = atoi(f[3]); e->max_players = atoi(f[4]);
        e->password = atoi(f[5]); e->running = atoi(f[6]);
        snprintf(e->mode, sizeof e->mode, "%s", f[7]);
        snprintf(e->world, sizeof e->world, "%s", f[8]);
        snprintf(e->map, sizeof e->map, "%s", f[9]);
        e->build = k > 10 ? (unsigned)strtoul(f[10], NULL, 10) : 0;
        e->ping = k > 11 ? atoi(f[11]) : -1;
        if (k > 12) snprintf(e->people, sizeof e->people, "%s", f[12]);
        snprintf(e->goal, sizeof e->goal, "%s", k > 13 ? f[13] : "flag");   /* (an older web site: the flag) */
        e->rating = k > 14 ? atoi(f[14]) : 0;            /* (the people on it, rated: newer web sites) */
        if (e->port > 0) b->n++;
        }
    }
    snprintf(b->status, sizeof b->status, "%d server%s", b->n, b->n == 1 ? "" : "s");
    browser_mark(b);
    return 0;
}

static const char *name_in(const char *list, const char *name)   /* "Anna,Bert" has bert? where (any case) */
{
    size_t n = strlen(name);
    const char *p = list;
    while (*p) {
        const char *e = strchr(p, ',');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        if (l == n && !SDL_strncasecmp(p, name, n)) return p;
        if (!e) break;
        p = e + 1;
    }
    return NULL;
}

void browser_mark(Browser *b)
{
    int i, k, w = 0;
    BrowserEntry tmp[64];
    char key[72];
    b->friends_online = 0;
    for (i = 0; i < b->n; i++) {
        BrowserEntry *e = &b->e[i];
        snprintf(key, sizeof key, "%s:%d", e->host, e->port);
        e->fav = e->recent = 0;
        e->friends[0] = 0;
        for (k = 0; k < b->nfavs; k++) if (!strcmp(b->favs[k], key)) e->fav = 1;
        for (k = 0; k < b->nrecent; k++) if (!strcmp(b->recent[k], key)) e->recent = 1;
        for (k = 0; k < b->nfriends; k++)
            {
                const char *at = name_in(e->people, b->friends[k]);   /* (as the server spells it) */
                size_t l = strlen(e->friends);
                if (!at) continue;
                snprintf(e->friends + l, sizeof e->friends - l, "%s%.*s", l ? ", " : "", (int)strlen(b->friends[k]), at);
                b->friends_online++;
            }
    }
    for (k = 0; k < 3; k++)                              /* favourites, then recent ones, then the rest */
        for (i = 0; i < b->n; i++) {
            const BrowserEntry *e = &b->e[i];
            int group = e->fav ? 0 : e->recent ? 1 : 2;
            if (group == k) tmp[w++] = *e;
        }
    memcpy(b->e, tmp, sizeof b->e[0] * (size_t)b->n);
}

int browser_input(Browser *b, InputBits pressed, int key, const char *text)
{
    if (b->typing_pw) {                                  /* the server wants a password */
        if (text) {
            size_t n = strlen(b->pw);
            for (; *text && n + 1 < sizeof b->pw; text++) if (*text >= 32 && *text < 127) b->pw[n++] = *text;
            b->pw[n] = 0;
        }
        if (key == SDL_SCANCODE_BACKSPACE && b->pw[0]) b->pw[strlen(b->pw) - 1] = 0;
        if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER) { b->typing_pw = 0; return BR_JOIN; }
        if (key == SDL_SCANCODE_ESCAPE) { b->typing_pw = 0; return BR_NOTHING; }
        return BR_NOTHING;
    }
    if ((pressed & (IN_LEFT | IN_RIGHT)) || key == SDL_SCANCODE_TAB) {   /* the tabs: PUBLIC, MY SERVERS */
        b->tab = (pressed & IN_LEFT) ? 0 : (pressed & IN_RIGHT) ? 1 : !b->tab;
        return BR_NOTHING;
    }
    if (b->tab == 1) {                                   /* MY SERVERS */
        if (key == SDL_SCANCODE_ESCAPE) return BR_BACK;
        if ((pressed & IN_UP) && b->mine_sel > 0) b->mine_sel--;
        if ((pressed & IN_DOWN) && b->mine_sel + 1 < b->nmine) b->mine_sel++;
        if (b->nmine && (key == SDL_SCANCODE_F || key == SDL_SCANCODE_DELETE || key == SDL_SCANCODE_BACKSPACE)) return BR_DROP_MINE;
        if (b->nmine && ((pressed & IN_FIRE) || key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER)) return BR_JOIN_MINE;
        return BR_NOTHING;
    }
    if (key == SDL_SCANCODE_F1 && b->sel < b->n && (int)(b->e[b->sel].build >> 16) > NET_PROTOCOL) {
        SDL_OpenURL("https://openfp.retrocs.org/play/");
        return BR_NOTHING;
    }
    if (key == SDL_SCANCODE_F && b->n) return BR_FAVOURITE;   /* F: a favourite, or not any more */
    if (key == SDL_SCANCODE_L && b->sel < b->n) return BR_WATCH;   /* L: watch its last match (W: up, with W A S D) */
    if (key == SDL_SCANCODE_ESCAPE) return BR_BACK;
    if (key == SDL_SCANCODE_R || key == SDL_SCANCODE_F5) return BR_REFRESH;
    if ((pressed & IN_UP) && b->sel > 0) b->sel--;
    if ((pressed & IN_DOWN) && b->sel + 1 < b->n) b->sel++;
    if (b->sel < b->top) b->top = b->sel;
    if (b->sel >= b->top + 9) b->top = b->sel - 8;
    if ((pressed & IN_FIRE) || key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER) {
        if (b->sel < b->n && b->need > 1 && b->e[b->sel].max_players - b->e[b->sel].players < b->need) {   /* no room for us all */
            snprintf(b->status, sizeof b->status, "NO ROOM THERE FOR %d OF YOU", b->need);
            return BR_NOTHING;
        }
        if (b->sel < b->n && b->e[b->sel].password) {
            b->pw[0] = 0;
            b->typing_pw = 1;
            return BR_NOTHING;
        }
        if (b->sel < b->n) return BR_JOIN;
    }
    return BR_NOTHING;
}

/* a server's mode, as words (shorter: for the list's column) */
static const char *goal_name(const char *g, int brief)
{
    if (!strcmp(g, "conquest")) return "CONQUEST";
    if (!strcmp(g, "deathmatch")) return brief ? "TDM" : "TEAM DEATHMATCH";
    if (!strcmp(g, "annihilation")) return brief ? "FFA" : "ANNIHILATION, EVERY TANK FOR ITSELF";
    if (!strcmp(g, "warlord")) return "WARLORD";
    if (!strcmp(g, "killconfirmed")) return brief ? "KC" : "KILL CONFIRMED";
    if (!strcmp(g, "hardpoint")) return brief ? "HP" : "HARDPOINT";
    if (!strcmp(g, "hill")) return brief ? "HILL" : "THE FLAG OR THE HILL";
    return brief ? "FLAG" : "CAPTURE THE FLAG";
}

static void upper_copy(char *t) { for (; *t; t++) if (*t >= 'a' && *t <= 'z') *t = (char)(*t - 32); }

/* text centred in a strip, shortened (..) to fit */
static void centre_fit(SDL_Renderer *r, int x, int w, int y, Rgb col, const char *text)
{
    char t[200];
    size_t n;
    snprintf(t, sizeof t, "%s", text);
    while ((n = strlen(t)) > 3 && ui_text_width(t, 1) > w) { t[n - 3] = '.'; t[n - 2] = '.'; t[n - 1] = 0; }
    ui_text(r, x + (w - ui_text_width(t, 1)) / 2, y, 1, col, t);
}

/* the server list: the tabs (and how many) on top, the list, the chosen one's details, the keys */
void browser_draw(SDL_Renderer *r, const Browser *b, const char *listing_url, int sw, int sh)
{
    int i, y, pad = input_pad_count() > 0;
    char buf[200];
    (void)listing_url;                                   /* (its address: settings.ini's, not on the screen) */
    dim_backdrop(r, sw, sh);
    plate(r, 6, 6, sw - 12, 22);
    {   /* the tabs, with the way between them */
        int tw = ui_text_width("PUBLIC", 1) + 12, tw2 = ui_text_width("MY SERVERS", 1) + 12, tx = 24;
        ui_icon(r, ICON_LEFT, 12, 12, b->tab == 1 ? VALUE : DIM);
        ui_fill(r, tx, 10, tw, 13, b->tab == 0 ? VALUE : (Rgb){ 40, 38, 30 });
        ui_text_bold(r, tx + 6, 13, 1, b->tab == 0 ? INK : LABEL, "PUBLIC");
        ui_fill(r, tx + tw + 4, 10, tw2, 13, b->tab == 1 ? VALUE : (Rgb){ 40, 38, 30 });
        ui_text_bold(r, tx + tw + 10, 13, 1, b->tab == 1 ? INK : LABEL, "MY SERVERS");
        ui_icon(r, ICON_RIGHT, tx + tw + 4 + tw2 + 4, 12, b->tab == 0 ? VALUE : DIM);
        {   /* on the right: how many (and friends online) */
            int rx = tx + tw + tw2 + 24;
            if (b->tab == 1) snprintf(buf, sizeof buf, "%d SAVED", b->nmine);
            else if (b->friends_online) snprintf(buf, sizeof buf, "%s, %d FRIEND%s ON", b->status, b->friends_online, b->friends_online == 1 ? "" : "S");
            else snprintf(buf, sizeof buf, "%s", b->status);
            upper_copy(buf);
            {
                char t[200];
                size_t n;
                snprintf(t, sizeof t, "%s", buf);
                while ((n = strlen(t)) > 3 && ui_text_width(t, 1) > sw - 14 - rx) { t[n - 3] = '.'; t[n - 2] = '.'; t[n - 1] = 0; }
                ui_text(r, sw - 14 - ui_text_width(t, 1), 13, 1, b->friends_online && b->tab == 0 ? GOOD : DIM, t);
            }
        }
    }
    plate(r, 6, 32, sw - 12, 146);
    if (b->tab == 1) {                                   /* MY SERVERS: the list, and how to add one */
        int k;
        ui_text(r, 14, 38, 1, DIM, "ADDRESS");
        ui_fill(r, 12, 48, sw - 24, 1, LINE);
        if (!b->nmine) centre_fit(r, 12, sw - 24, 100, DIM, "NONE YET");
        for (k = 0; k < b->nmine && k < 8; k++) {
            int yy = 52 + k * 14, sel2 = k == b->mine_sel;
            if (sel2) ui_fill(r, 10, yy - 2, sw - 20, 12, BAR);
            text_fit(r, 14, yy, sw - 110, sel2 ? INK : LABEL, b->mine[k]);
            if (b->mine_pw[k][0]) ui_text(r, sw - 14 - ui_text_width("PASSWORD", 1), yy, 1, sel2 ? INK : DIM, "PASSWORD");
        }
        centre_fit(r, 12, sw - 24, 166, DIM, "ADD ONE WITH DIRECT CONNECT (SAVE: YES)");
        centre_fit(r, 6, sw - 12, 200, DIM, pad ? "A: JOIN   X: REMOVE   B: BACK" : "ENTER: JOIN   DEL: REMOVE   ESC: BACK");
        return;
    }
    ui_text(r, 14, 38, 1, DIM, "SERVER");
    ui_text(r, 176, 38, 1, DIM, "IN");
    ui_text(r, 216, 38, 1, DIM, "WORLD");
    ui_text(r, 298, 38, 1, DIM, "MODE");
    ui_fill(r, 12, 48, sw - 24, 1, LINE);
    for (i = b->top, y = 52; i < b->n && i < b->top + 9; i++, y += 14) {
        const BrowserEntry *e = &b->e[i];
        int sel = i == b->sel;
        Rgb col = sel ? INK : LABEL;
        if (sel) ui_fill(r, 10, y - 2, sw - 20, 12, BAR);
        snprintf(buf, sizeof buf, "%s%s%s", e->fav ? "+ " : e->recent ? "> " : "", e->password ? "* " : "", e->name);
        text_fit(r, 14, y, 152, sel ? INK : VALUE, buf);
        if (e->friends[0]) ui_fill(r, 168, y + 2, 4, 4, GOOD);   /* a friend's on it */
        snprintf(buf, sizeof buf, "%d/%d", e->players, e->max_players);
        ui_text(r, 176, y, 1, sel ? INK : e->max_players - e->players < (b->need > 1 ? b->need : 1) ? BAD : GOOD, buf);   /* (room for us?) */
        text_fit(r, 216, y, 78, col, e->world);
        {   /* a server we can't play on (its version): say so instead */
            int proto = (int)(e->build >> 16), ok = !e->build || (proto >= NET_PROTOCOL_OLDEST && proto <= NET_PROTOCOL);
            if (!ok) snprintf(buf, sizeof buf, "%s", proto > NET_PROTOCOL ? "NEWER" : "OLD SERVER");
            else snprintf(buf, sizeof buf, "%s", goal_name(e->goal, 1));   /* (its mode) */
            text_fit(r, 298, y, sw - 312, ok ? col : BAD, buf);
        }
    }
    if (!b->n) centre_fit(r, 12, sw - 24, 100, DIM, b->fetched ? "NO SERVERS RIGHT NOW" : "LOOKING...");
    plate(r, 6, 182, sw - 12, 14);                       /* the chosen one */
    if (b->n && b->sel < b->n) {
        const BrowserEntry *e = &b->e[b->sel];
        if (e->build && (int)(e->build >> 16) > NET_PROTOCOL) snprintf(buf, sizeof buf, "NEWER SERVER  F1: GET THE LATEST GAME");
        else if (e->build && (int)(e->build >> 16) < NET_PROTOCOL_OLDEST) snprintf(buf, sizeof buf, "OLDER SERVER  ASK THE HOST TO UPDATE");
        else if (e->friends[0]) snprintf(buf, sizeof buf, "FRIENDS HERE: %s", e->friends);
        else snprintf(buf, sizeof buf, "%s, %s, %s%s", goal_name(e->goal, 0), e->map, e->running ? "PLAYING NOW" : "IN THE LOBBY",
                      e->password ? ", PASSWORD" : "");
        upper_copy(buf);
        centre_fit(r, 10, sw - 20, 185, LABEL, buf);
    }
    if (b->typing_pw) {
        plate(r, 70, 84, sw - 140, 44);
        ui_text_bold(r, 82, 92, 1, VALUE, "PASSWORD");
        {
            char stars[70];
            size_t k;
            for (k = 0; k < strlen(b->pw) && k < 60; k++) stars[k] = '*';
            stars[k] = '_'; stars[k + 1] = 0;
            ui_text(r, 82, 108, 1, LABEL, stars);
        }
    }
    centre_fit(r, 6, sw - 12, 202, DIM, pad ? "A: JOIN  X: FAV  LB: LAST MATCH  Y: REFRESH  B: BACK" : "ENTER: JOIN  F: FAV  L: LAST MATCH  R: REFRESH  ESC: BACK");
}

/* ---- the lobby ----------------------------------------------------------------------- */

static const char *const lobby_tank_short[] = { "JAK", "BIS", "MAM" };

static Rgb lobby_side_color(int side)
{
    return side == SIDE_CAMP ? render_side_rgb(TEAM_PLAYER) :
           side == SIDE_FORTRESS ? render_side_rgb(TEAM_ENEMY) : VALUE;
}

enum { LOBBY_TOP = 56, LOBBY_ROW_H = 23, LOBBY_ACTION_Y = 151,
       LOBBY_CHAT_Y = 198, LOBBY_CHAT_H = 12, LOBBY_KEYBOARD_W = 78 };
static const int lobby_sides[] = { SIDE_CAMP, SIDE_ANY, SIDE_FORTRESS };

static void lobby_column(int columns, int col, int sw, int *x, int *width)
{
    *width = columns == 2 ? (sw - 16) / 2 : col == 1 ? 94 : (sw - 114) / 2;
    *x = columns == 2 ? 6 + col * (*width + 4) :
         col == 0 ? 6 : col == 1 ? (sw - *width) / 2 : sw - 6 - *width;
}

const LobbyPlayer *lobby_player_at(const NetClient *c, const LobbyUI *ui, int sw, int x, int y)
{
    LobbyRoster roster;
    int col, page, row;
    if (y < LOBBY_TOP || y >= LOBBY_TOP + LOBBY_ROWS * LOBBY_ROW_H) return NULL;
    netc_lobby_roster(c, &roster);
    page = ui->page < roster.pages ? ui->page : roster.pages - 1;
    row = page * LOBBY_ROWS + (y - LOBBY_TOP) / LOBBY_ROW_H;
    for (col = 0; col < roster.columns; col++) {
        int left, width;
        lobby_column(roster.columns, col, sw, &left, &width);
        if (x < left + 4 || x >= left + width - 4 || row >= roster.count[col]) continue;
        int i = roster.entry[col][row];
        return i < c->nlobby ? &c->lobby[i] : NULL;
    }
    return NULL;
}

static int lobby_action_on(const NetClient *c, const NetClient *player, const LobbyUI *ui, int action)
{
    LobbyRoster roster;
    switch (action) {
    case LB_CAMP: case LB_FORTRESS:
        return !ui->show_scores && netc_lobby_can_rename_side(player, action == LB_CAMP ? SIDE_CAMP : SIDE_FORTRESS);
    case LB_READY: return player->state == NC_LOBBY && c->server_state != SRV_LOADING && c->server_state != SRV_COUNTDOWN && !player->eliminated;
    case LB_WATCH: return player->state == NC_LOBBY && c->server_state == SRV_RUNNING;
    case LB_PAGE:
        netc_lobby_roster(c, &roster);
        return !ui->show_scores && roster.pages > 1;
    case LB_CHAT: case LB_KEYBOARD: return player->state == NC_LOBBY;
    case LB_STATUS: case LB_SCORES: case LB_EXIT: return 1;
    }
    return 0;
}

int lobby_action_step(const NetClient *c, const NetClient *player, const LobbyUI *ui, int dir)
{
    int action = ui->action;
    do action = (action + LB_COUNT + dir) % LB_COUNT;
    while (action && !lobby_action_on(c, player, ui, action));
    return action;
}

int lobby_action_at(const NetClient *c, const NetClient *player, const LobbyUI *ui, int sw, int x, int y)
{
    int action = LB_NONE;
    if (y >= 39 && y < LOBBY_TOP) {
        int left, w;
        lobby_column(3, 0, sw, &left, &w);
        if (x >= left && x < left + w) action = LB_CAMP;
        lobby_column(3, 2, sw, &left, &w);
        if (x >= left && x < left + w) action = LB_FORTRESS;
    } else if (y >= LOBBY_ACTION_Y && y < LOBBY_ACTION_Y + 15 && x >= 6 && x < sw - 6) {
        int w = (sw - 12) / 6;
        if (x < 6 + 6 * w) action = LB_READY + (x - 6) / w;
    } else if (y >= LOBBY_CHAT_Y && y < LOBBY_CHAT_Y + LOBBY_CHAT_H && x >= 10 && x < sw - 10)
        action = x >= sw - 10 - LOBBY_KEYBOARD_W ? LB_KEYBOARD : LB_CHAT;
    return lobby_action_on(c, player, ui, action) ? action : LB_NONE;
}

void lobby_draw(SDL_Renderer *r, const NetClient *c, const NetClient *extra, int extras,
                const LobbyUI *ui, SDL_Texture *tanks, int hosting, int sw, int sh)
{
    if (ui->diagnostics) {
        char report[1600], *line, *next;
        int y = 33, button;
        netc_diagnostics(c, report, sizeof report);
        plate(r, 8, 8, sw - 16, sh - 16);
        ui_text(r, 16, 17, 1, VALUE, "NETWORK STATUS");
        for (line = report; *line && y < sh - 35; line = next, y += 9) {
            next = strchr(line, '\n');
            if (next) *next++ = 0; else next = line + strlen(line);
            text_fit(r, 16, y, sw - 32, LABEL, line);
        }
        for (button = 0; button < 2; button++) {
            int x = 16 + button * (sw - 32) / 2, w = (sw - 36) / 2;
            ui_fill(r, x, sh - 29, w, 14, ui->diagnostic_button == button ? BAR : LINE);
            centre_fit(r, x, w, sh - 25, ui->diagnostic_button == button ? INK : VALUE,
                       button ? "BACK" : ui->diagnostic_saved[0] ? ui->diagnostic_saved : "SAVE REPORT");
        }
        return;
    }

    const NetClient *locals[] = { c, &extra[0], &extra[1], &extra[2] };
    int seat = ui->seat <= extras ? ui->seat : 0;
    const NetClient *player = locals[seat];
    int teams = netc_lobby_teams(c), bots = netc_lobby_bots(c), i;
    char buf[200], detail[64];
    dim_backdrop(r, sw, sh);

    plate(r, 6, 3, sw - 12, 34);
    if (c->time_limit)
        snprintf(buf, sizeof buf, "%s %d MIN", c->versus ? "VERSUS" : "CO-OP", (c->time_limit + 59) / 60);
    else snprintf(buf, sizeof buf, "%s", c->versus ? "VERSUS" : "CO-OP");
    i = ui_text_width(buf, 1);
    ui_text(r, sw - 12 - i, 8, 1, DIM, buf);
    text_fit(r, 12, 8, sw - 32 - i, VALUE, c->server_name[0] ? c->server_name : "CONNECTING");
    text_fit(r, 12, 17, sw - 24, LABEL, c->state == NC_CONNECTING || c->state == NC_FAILED ? c->status : c->motd);
    if (c->nballot) {
        ui_text(r, 12, 26, 1, VALUE, "/VOTE");
        for (i = 0; i < c->nballot; i++) {
            int ballot_width = (sw - 68) / c->nballot;
            int map = c->ballot[i], votes = map > 0 && map <= NET_MAPS ? c->votes[map - 1] : 0;
            int voted = map == c->my_vote;
            snprintf(buf, sizeof buf, "%s%d (%d) %s", voted ? "*" : "", map, votes == 255 ? 0 : votes, c->ballot_name[i]);
            text_fit(r, 60 + i * ballot_width, 26, ballot_width - 4, voted ? VALUE : LABEL, buf);
        }
    } else if (c->state == NC_LOBBY) {
        snprintf(detail, sizeof detail, c->match_capacity ? "MAX %d TANKS" : "UP TO %d TANKS",
                 c->match_capacity ? c->match_capacity : MATCH_PLAYERS);
        if (bots)
            snprintf(detail + strlen(detail), sizeof detail - strlen(detail), " / %d AI", bots);
        i = ui_text_width(detail, 1);
        ui_text(r, sw - 12 - i, 26, 1, LABEL, detail);
        snprintf(buf, sizeof buf, "NEXT %s", c->next_map[0] ? c->next_map : "RANDOM");
        text_fit(r, 12, 26, sw - 32 - i, LABEL, buf);
    }

    if (ui->show_scores) {
        plate(r, 6, 39, sw - 12, 109);
        ui_text_bold(r, 12, 44, 1, VALUE, "BEST HERE");
        if (!c->nleaders)
            ui_text(r, 12, 62, 1, DIM, c->got_leaders ? "NO MATCHES YET" : "LOADING...");
        for (i = 0; i < c->nleaders && i < 8; i++) {
            const LeaderEntry *e = &c->leaders[i];
            snprintf(buf, sizeof buf, "%d  %.15s  %d", i + 1, e->name, e->rating ? e->rating : 1500);
            text_fit(r, 12, 60 + i * 10, sw - 24, i == 0 ? VALUE : LABEL, buf);
        }
    } else {
        LobbyRoster roster;
        int col, page;
        netc_lobby_roster(c, &roster);
        page = ui->page < roster.pages ? ui->page : roster.pages - 1;
        for (col = 0; col < roster.columns; col++) {
            int x, w, side = teams ? lobby_sides[col] : SIDE_ANY;
            Rgb color = lobby_side_color(side);
            const char *heading = teams ? netc_lobby_team_name(c, side) : c->versus ? "PILOTS" : "CO-OP";
            int selected = teams && player->side == side;
            lobby_column(roster.columns, col, sw, &x, &w);
            plate(r, x, 39, w, 109);
            int action = col == 0 ? LB_CAMP : LB_FORTRESS;
            int rename = teams && col != 1 && lobby_action_on(c, player, ui, action);
            int focus = rename && ui->action == action;
            if (selected || focus) ui_fill(r, x + 3, 42, w - 6, 12, focus ? BAR : color);
            if (rename) {
                centre_fit(r, x + 3, w - 17, 44, selected || focus ? INK : color, heading);
                ui_text(r, x + w - 12, 44, 1, selected || focus ? INK : VALUE, ">");
            } else centre_fit(r, x, w, 44, selected ? INK : color, heading);
            for (i = page * LOBBY_ROWS; i < (page + 1) * LOBBY_ROWS && i < roster.count[col]; i++) {
                int index = roster.entry[col][i], y = LOBBY_TOP + (i % LOBBY_ROWS) * LOBBY_ROW_H;
                const LobbyPlayer *p = index < c->nlobby ? &c->lobby[index] : NULL;
                int local = -1, roles = netc_lobby_roles(c, p);
                int cls = p ? p->cls : TANK_MEDIUM, ready = p && p->ready;
                int ping = p ? p->rtt : 0, portrait = p ? p->portrait : 0;
                const char *status = p ? p->playing == 1 ? "PLAY" : p->playing == 2 ? "VIEW" : ready ? "RDY" : "WAIT" : "AI";
                int j;
                for (j = 0; p && j <= extras; j++)
                    if (locals[j]->state == NC_LOBBY && locals[j]->id == p->id) { local = j; break; }
                if (local >= 0) {
                    cls = locals[local]->want_cls;
                    ping = locals[local]->rtt;
                    portrait = locals[local]->portrait;
                    if (!p->playing) status = (ready = locals[local]->ready) ? "RDY" : "WAIT";
                }
                ui_fill(r, x + 4, y, w - 8, 21, local == seat ? LINE : STEEL_HI);
                if (local == seat) ui_fill(r, x + 4, y, 1, 21, color);
                if (p && portrait) ui_avatar(r, x + 6, y + 2, 16, portrait - 1, p->name);
                else {
                    ui_fill(r, x + 6, y + 2, 16, 16, STEEL);
                    ui_text(r, x + 7, y + 7, 1, LABEL, p ? "?" : "AI");
                }
                if (p) snprintf(buf, sizeof buf, "%s", p->name);
                else snprintf(buf, sizeof buf, "BOT %d", index - c->nlobby + 1);
                if (p && w >= 120) snprintf(detail, sizeof detail, "%s %dMS", status, ping);
                else if (p) snprintf(detail, sizeof detail, "%s %d", status, ping > 999 ? 999 : ping);
                else snprintf(detail, sizeof detail, "%s", status);
                if (tanks) render_tank_icon(r, tanks, cls, side == SIDE_FORTRESS ? TEAM_ENEMY : TEAM_PLAYER, x + w - 13, y + 14, 23);
                else if (w >= 120) ui_text(r, x + w - 29, y + 11, 1, LABEL, lobby_tank_short[cls % TANK_CLASS_COUNT]);
                int badges = ui_role_badges(r, x + w - 5, y + 2, roles);
                text_fit(r, x + 25, y + 2, w - 31 - badges, p ? color : LABEL, buf);
                text_fit(r, x + 25, y + 12, w - 48, ping > 150 ? BAD : ready ? GOOD : LABEL, detail);
            }
        }
    }

    {
        LobbyRoster roster;
        int action, w = (sw - 12) / 6;
        netc_lobby_roster(c, &roster);
        for (action = LB_READY; action <= LB_EXIT; action++) {
            int x = 6 + (action - LB_READY) * w;
            int on = lobby_action_on(c, player, ui, action), focus = on && action == ui->action;
            const char *name = "";
            switch (action) {
            case LB_READY:
                if (c->server_state == SRV_COUNTDOWN && c->countdown > 0) {
                    snprintf(detail, sizeof detail, "IN %d", c->countdown); name = detail;
                } else if (c->server_state == SRV_LOADING) name = "LOADING";
                else if (netc_lobby_can_start(player)) name = "START";
                else if (player->eliminated) name = "OUT";
                else if (c->server_state == SRV_RUNNING && player->joining) name = "JOINING";
                else if (c->server_state == SRV_RUNNING && c->places_free > 0) name = "JOIN";
                else name = player->ready ? "UNREADY" : "READY";
                if (player->eliminated) on = focus = 0;
                break;
            case LB_WATCH: name = "WATCH"; break;
            case LB_SCORES: name = ui->show_scores ? "TEAMS" : "SCORES"; break;
            case LB_PAGE:
                if (on) {
                    snprintf(detail, sizeof detail, "PG %d/%d", (ui->page < roster.pages ? ui->page + 1 : roster.pages), roster.pages);
                    name = detail;
                } else name = "PAGE";
                break;
            case LB_STATUS: name = "STATUS"; break;
            case LB_EXIT: name = "LEAVE"; break;
            }
            plate(r, x, LOBBY_ACTION_Y, w - 2, 15);
            if (focus) ui_fill(r, x + 3, LOBBY_ACTION_Y + 3, w - 8, 9, BAR);
            centre_fit(r, x, w - 2, LOBBY_ACTION_Y + 4, focus ? INK : on ? VALUE : LABEL, name);
        }
    }

    plate(r, 6, 169, sw - 12, sh - 173);
    {
        const ClientChat *recent[3];
        uint32_t q;
        int count = 0, y = 173;
        for (q = c->chat_seq; q && c->chat_seq - q < 40 && count < 3; q--) {
            const ClientChat *l = &c->chat[q % 40];
            if (l->seq == q && l->text[0]) recent[count++] = l;
        }
        while (count) {
            const ClientChat *l = recent[--count];
            int own = 0, mention = 0;
            for (i = 0; i <= extras; i++) {
                if (locals[i]->state != NC_LOBBY) continue;
                own |= l->from == locals[i]->id;
                mention |= netc_chat_mentions(c, l, locals[i]->id);
            }
            if (l->from < 0) snprintf(buf, sizeof buf, "* %s", l->text);
            else snprintf(buf, sizeof buf, "%s: %s", l->name, l->text);
            if (mention) ui_fill(r, 10, y, sw - 20, 8, LINE);
            text_fit(r, 12, y, sw - 24, mention ? VALUE : l->from < 0 ? DIM : own ? OURS : LABEL, buf);
            y += 8;
        }
    }
    {
        int keyboard_x = sw - 10 - LOBBY_KEYBOARD_W, chat_w = keyboard_x - 14;
        int chat_focus = ui->typing || ui->action == LB_CHAT;
        int keyboard_focus = ui->action == LB_KEYBOARD;
        ui_fill(r, 10, LOBBY_CHAT_Y, chat_w, LOBBY_CHAT_H, chat_focus ? BAR : LINE);
        ui_fill(r, keyboard_x, LOBBY_CHAT_Y, LOBBY_KEYBOARD_W, LOBBY_CHAT_H, keyboard_focus ? BAR : LINE);
        centre_fit(r, keyboard_x, LOBBY_KEYBOARD_W, LOBBY_CHAT_Y + 3,
                   keyboard_focus ? INK : VALUE, "KEYBOARD");
        if (ui->line[0] || ui->typing) {
            int visible = (chat_w - 4) / ui_text_width("M", 1) - 3;
            int n = (int)strlen(ui->line);
            snprintf(buf, sizeof buf, "> %s%s", ui->line + (n > visible ? n - visible : 0),
                     ui->typing && (SDL_GetTicks() / 500) % 2 ? "_" : "");
            text_fit(r, 12, LOBBY_CHAT_Y + 3, chat_w - 4, chat_focus ? INK : LABEL, buf);
        } else {
            ui_text(r, 12, LOBBY_CHAT_Y + 3, 1, chat_focus ? INK : LABEL, "> CHAT");
            if (hosting && ui->join_code[0]) {
                snprintf(buf, sizeof buf, "JOIN CODE %s", ui->join_code);
                ui_text(r, keyboard_x - 6 - ui_text_width(buf, 1), LOBBY_CHAT_Y + 3, 1,
                        chat_focus ? INK : GOOD, buf);
            }
        }
    }
}

int browser_pick(const Browser *b)
{
    int i, best = -1;
    long best_score = -1;
    for (i = 0; i < b->n; i++) {
        const BrowserEntry *e = &b->e[i];
        long sc;
        if (e->password || e->max_players - e->players < (b->need > 1 ? b->need : 1)) continue;   /* (room for everyone here) */
        if (e->build && !net_compatible(e->build, (int)(e->build >> 16))) continue;
        sc = e->running ? 2000 : e->players > 0 ? 4000 : 1000;   /* waiting people > a match on > an empty one */
        sc += e->players * 100;
        if (e->friends[0]) sc += 2500;                   /* (a friend: worth a match that has started) */
        if (e->ping > 0) sc -= e->ping > 400 ? 400 : e->ping;
        if (b->want_goal[0] && !strcmp(e->goal, b->want_goal)) sc += 100000;   /* (the mode asked for: first, any open one) */
        if (b->my_rating > 0 && e->rating > 0 && e->players > 0) {   /* (ranked: a game near my rating first) */
            int gap = abs(e->rating - b->my_rating);
            sc -= gap > 1500 ? 1500 : gap;                /* (400 apart: a little; 1500: no better than an empty one) */
        }
        if (sc > best_score) { best_score = sc; best = i; }
    }
    return best;
}

int browser_my_rating(const char *listing_url, const char *name)
{
    char url[600], q[200] = "", resp[64];
    if (!listing_url[0] || !name[0]) return 0;
    http_form(q, sizeof q, "name", name);                  /* (URL-encoded) */
    snprintf(url, sizeof url, "%.380s%saction=rating&%s", listing_url, strchr(listing_url, '?') ? "&" : "?", q);
    if (http_request(url, NULL, resp, sizeof resp, 3000) != 200) return 0;
    return atoi(resp) > 0 && atoi(resp) <= 4000 ? atoi(resp) : 0;
}

int browser_replay_id(const char *json)
{   /* (the matches come newest first, each {"id":N, ... "has_replay":0|1, ...}; no other "id" in them) */
    const char *p = json;
    while (p && (p = strstr(p, "\"id\":")) != NULL) {
        int id = atoi(p + 5);
        const char *next = strstr(p + 5, "\"id\":"), *has = strstr(p + 5, "\"has_replay\":");
        if (has && (!next || has < next) && atoi(has + 13) == 1 && id > 0) return id;
        p = next;
    }
    return 0;
}
