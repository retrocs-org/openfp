/* SPDX-License-Identifier: MIT */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/server.h"
#include "common/botnames.h"
#include "common/rules.h"
#include "common/achieve.h"
#include "common/sha256.h"

#define SRV_WAIT_MS   250            /* longest a tick waits for a late player */
#define SRV_GONE_MS   8000           /* silence before someone counts as gone */
#define SRV_LOAD_MS   20000          /* longest a match waits for everyone to load */

void server_default_config(ServerConfig *cfg)
{
    memset(cfg, 0, sizeof *cfg);
    snprintf(cfg->name, sizeof cfg->name, "OpenFP server");
    cfg->max_players = 4;
    cfg->min_players = 2;
    cfg->base.versus = 1;
    cfg->base.camp_garages = 4;
    cfg->base.ai_level = AI_NORMAL;
    cfg->countdown_ms = 3000;
    cfg->bot_level = AI_NORMAL;
    cfg->allow_bots = 1;
    cfg->max_per_address = 4;
    cfg->allow_watching = 1;
}

/* how many computer players the next match gets, with this many people */
/* how many tanks the next map holds (its spawn points, or its camps' garages; at most MATCH_PLAYERS) */
static int rematch_wanted(const Server *s);

static uint8_t next_extras(const Server *s)
{
    return rematch_wanted(s) ? s->ms.extras : s->cfg.base.extras;
}

static int next_map_index(const Server *s)
{
    return rematch_wanted(s) && s->last_map_idx >= 0 && s->last_map_idx < s->nmaps
           ? s->last_map_idx : s->next_idx;
}

static int room_for(const Server *s)
{
    MatchSetup ms;
    if (s->room > 0) return s->room;                     /* (worked out when the map was chosen) */
    ms = s->cfg.base;
    ms.extras = next_extras(s);
    ms.camp_garages = 4;
    return match_room(&ms, s->map);
}

static void work_out_room(Server *s) { s->room = 0; s->room = room_for(s); }

static int bots_for(const Server *s, int people)
{
    int room = room_for(s);
    int b = !s->cfg.allow_bots ? 0 : s->cfg.fill_bots ? room - people : s->cfg.bots;
    if (b < 0) b = 0;
    if (people + b > room) b = room - people;
    if (b < 0) b = 0;
    if (!(next_extras(s) & EXTRA_ANNIHILATION) && s->cfg.base.versus && b && (people + b) % 2)   /* (team modes: even sides) */
        b += people + b + 1 <= room ? 1 : -1;
    return b;
}

static void slog(Server *s, int level, const char *fmt, ...)
{
    char buf[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (s->hooks.log) s->hooks.log(s->hooks.ctx, level, buf);
}

static int have_caught_up(const Server *s, const SrvClient *c);
static void send_to(Server *s, const NetAddr *a, Pack *k);

/* BYE: the reason, and (for any build to read) the server's protocols and version */
static void send_bye_v(Server *s, const NetAddr *a, int reason)
{
    uint8_t b[64];
    Pack k;
    pk_init(&k, b, sizeof b);
    pk_header(&k, NM_BYE);
    pk_u8(&k, (uint32_t)reason);
    pk_u8(&k, NET_PROTOCOL);
    pk_u8(&k, NET_PROTOCOL_OLDEST);
    pk_str(&k, OFP_VERSION, 15);
    send_to(s, a, &k);
}
static int is_banned(Server *s, const uint8_t id[16], uint32_t ip);
static void pick_next_map(Server *s);
static int map_fits_mode(const char *text, uint8_t extras);
static const char *mode_word(uint8_t extras)            /* a mode's name, as the settings say it */
{
    return extras & EXTRA_CONQUEST ? "conquest" : extras & EXTRA_ANNIHILATION ? "annihilation" : extras & EXTRA_TDM ? "deathmatch"
         : extras & EXTRA_WARLORD ? "warlord" : extras & EXTRA_KILLCONF ? "killconfirmed" : extras & EXTRA_HARDPOINT ? "hardpoint"
         : extras & EXTRA_HILL ? "hill" : "flag";
}
/* a rematch: more than half of the last match's players still here want one */
static void rematch_count(const Server *s, int *yes, int *of)
{
    int i;
    *yes = *of = 0;
    if (!s->rematch_open) return;
    for (i = 0; i < NET_LOBBY_MAX; i++) {
        const SrvClient *c = &s->c[i];
        if (!c->used || !c->authed || c->gone || !c->last_team1) continue;
        (*of)++;
        *yes += c->rematch;
    }
}
static int rematch_wanted(const Server *s) { int y, o; rematch_count(s, &y, &o); return o > 0 && y * 2 > o; }
static void make_ballot(Server *s);
static void work_out_room(Server *s);
static void send_team_order(Server *s, SrvClient *c);
static void chat_command(Server *s, SrvClient *c, const char *cmd, uint32_t cseq);
static void send_match(Server *s, SrvClient *c);
static uint32_t visible_tick(const Server *s, const SrvClient *c, uint32_t now);
static int pw_blocked(Server *s, uint32_t ip, uint32_t now);
static int places_free(const Server *s, const SrvClient *c);

static int reserved_place(const Server *s, const SrvClient *c)
{
    int p;
    for (p = 0; p < s->ms.players; p++)
        if (s->left_name[p][0] && !memcmp(s->left_key[p], c->key_id, sizeof c->key_id)) return p;
    return -1;
}

static int eliminated(const Server *s, const SrvClient *c)
{
    int p;
    if (s->state != SRV_RUNNING) return 0;
    p = c->slot >= 0 ? c->slot : reserved_place(s, c);
    return p >= 0 && s->game.tanks[p].out;
}

static void leave_match(Server *s, SrvClient *c)
{
    int p = c->slot;
    memcpy(s->left_name[p], c->name, sizeof s->left_name[p]);
    memcpy(s->left_key[p], c->key_id, sizeof c->key_id);
    s->in_match[p] = -1;
    c->slot = -1;
    c->ready = c->joining = c->catching = 0;
}
static void join_running(Server *s, SrvClient *c);
static void pw_failed(Server *s, uint32_t ip, uint32_t now);
static void send_to(Server *s, const NetAddr *a, Pack *k)
{
    if (k->bad) return;
    net_send(s->sock, a, k->p, k->n);
    s->pkts_out++;
    s->bytes_out += (uint32_t)k->n;
}

static void send_bye_v(Server *s, const NetAddr *a, int reason);
static void send_bye(Server *s, const NetAddr *a, int reason) { send_bye_v(s, a, reason); }

int server_people(const Server *s)
{
    int i, n = 0;
    for (i = 0; i < NET_LOBBY_MAX; i++) n += s->c[i].used && s->c[i].authed && !s->c[i].gone;
    return n;
}

static Biome server_world(const Server *s)
{
    if (s->state == SRV_RUNNING) return s->game.biome;
    return s->map ? mapfile_biome(s->map) : (Biome)s->cfg.base.biome;
}

void server_info(const Server *s, ServerInfo *o)
{
    memset(o, 0, sizeof *o);
    o->build = net_build_id();
    snprintf(o->name, sizeof o->name, "%s", s->cfg.name);
    snprintf(o->world, sizeof o->world, "%s", biome_name(server_world(s)));
    snprintf(o->map, sizeof o->map, "%s", s->map_name);
    o->players = (uint8_t)server_people(s);
    o->max_players = (uint8_t)s->cfg.max_players;
    o->password = s->cfg.password[0] != 0;
    o->running = s->state == SRV_RUNNING || s->state == SRV_LOADING;
    o->versus = s->cfg.base.versus;
    o->match_players = (uint8_t)(o->running ? s->ms.players : 0);
}


int server_open(Server *s, uint16_t port, const ServerConfig *cfg, const ServerHooks *hooks)
{
    memset(s, 0, sizeof *s);
    s->cfg = *cfg;
    if (!s->cfg.base.time_limit) s->cfg.base.time_limit = MATCH_DEFAULT_SECONDS;
    if (s->cfg.base.time_limit > MATCH_MAX_SECONDS) s->cfg.base.time_limit = MATCH_MAX_SECONDS;
    if (s->cfg.base.lives > MATCH_MAX_LIVES && s->cfg.base.lives != MATCH_UNLIMITED_LIVES) s->cfg.base.lives = 5;
    if (hooks) s->hooks = *hooks;
    if (s->cfg.max_players < 1) s->cfg.max_players = 1;
    if (s->cfg.max_players > NET_LOBBY_MAX) s->cfg.max_players = NET_LOBBY_MAX;
    if (s->cfg.min_players < 1) s->cfg.min_players = 1;
    if (s->cfg.min_players > MATCH_PLAYERS) s->cfg.min_players = MATCH_PLAYERS;
    {   /* the maps: a rotation, or the one map, or none (a new random battlefield each match) */
        int i;
        const char *one = cfg->map;
        s->nmaps = 0;
        for (i = 0; i < cfg->nmaps && i < NET_MAPS; i++) {
            size_t n = strlen(cfg->maps[i]);
            if (!(s->maps[s->nmaps] = (char *)malloc(n + 1))) return -1;
            memcpy(s->maps[s->nmaps], cfg->maps[i], n + 1);
            s->nmaps++;
        }
        if (!s->nmaps && one) {
            size_t n = strlen(one);
            if (!(s->maps[0] = (char *)malloc(n + 1))) return -1;
            memcpy(s->maps[0], one, n + 1);
            snprintf(s->cfg.map_names[0], sizeof s->cfg.map_names[0], "%s", cfg->map_name);
            s->nmaps = 1;
        }
        for (i = 0; i < NET_MAPS; i++) s->cfg.maps[i] = NULL;
        s->cfg.map = NULL;                               /* ours now */
        s->next_idx = s->nmaps && s->cfg.rotation == ROT_RANDOM ? rand() % s->nmaps : 0;
        make_ballot(s);
        s->map = s->nmaps ? s->maps[s->next_idx] : NULL;
        work_out_room(s);
        snprintf(s->map_name, sizeof s->map_name, "%s", s->nmaps ? s->cfg.map_names[s->next_idx] : "random");
    }
    s->delay = NET_DELAY;
    if (net_init() != 0 || (s->sock = net_open(port)) < 0) {
        int i;
        for (i = 0; i < s->nmaps; i++) free(s->maps[i]);
        s->nmaps = 0;
        s->map = NULL;
        return -1;
    }
    s->state = SRV_LOBBY;
    s->next_id = 1;
    if (s->cfg.max_per_address < 1) s->cfg.max_per_address = 4;
    if (!s->hooks.server_key || !s->hooks.server_key(s->hooks.ctx, s->server_id, s->secret)) {
        net_random(s->server_id, sizeof s->server_id);  /* (players' keys are then good for this run only) */
        net_random(s->secret, sizeof s->secret);
    }
    if (s->hooks.leaders) s->nleaders = s->hooks.leaders(s->hooks.ctx, s->leaders, NET_LEADERS);
    snprintf(s->status, sizeof s->status, "lobby open: 0 of %d", s->cfg.max_players);
    slog(s, LOG_INFO, "listening on UDP port %u: \"%s\", %d players at most%s", port, s->cfg.name,
         s->cfg.max_players, s->cfg.password[0] ? ", password protected" : "");
    return 0;
}

void server_close(Server *s)
{
    int i;
    if (s->sock > 0) {
        for (i = 0; i < NET_LOBBY_MAX; i++) if (s->c[i].used && !s->c[i].gone) send_bye(s, &s->c[i].addr, NR_SHUTDOWN);
        net_close(s->sock);
    }
    replay_free(&s->replay);
    for (i = 0; i < s->nmaps; i++) free(s->maps[i]);
    s->nmaps = 0;
    s->map = NULL;
    s->sock = 0;
    s->state = SRV_CLOSED;
}

/* ---- chat: every line gets a number, and goes out until everyone has it ----------- */

static void add_chat_to(Server *s, int from, const char *name, const char *text, uint32_t cseq, int to)
{
    ChatLine *l = &s->chat[s->chat_seq & 31];
    size_t len = strlen(text), cap = sizeof l->text - 1;
    if (len > cap) {                                     /* longer than a chat line (/maps with many): in pieces, at a space */
        char piece[sizeof l->text];
        size_t cut = cap;
        while (cut > cap / 2 && text[cut] != ' ') cut--;
        if (text[cut] != ' ') cut = cap;                 /* (no space to break at: just cut) */
        memcpy(piece, text, cut);
        piece[cut] = 0;
        add_chat_to(s, from, name, piece, cseq, to);
        while (text[cut] == ' ') cut++;
        if (text[cut]) add_chat_to(s, from, name, text + cut, cseq, to);
        return;
    }
    memset(l, 0, sizeof *l);
    l->seq = ++s->chat_seq;
    l->from = from;
    l->to = to;
    l->cseq = cseq;
    snprintf(l->name, sizeof l->name, "%s", name);
    memcpy(l->text, text, len + 1);                      /* (it fits: checked above) */
    if (s->hooks.chat && from >= 0 && to < 0) s->hooks.chat(s->hooks.ctx, name, text);
}

static void add_chat(Server *s, int from, const char *name, const char *text, uint32_t cseq)
{
    add_chat_to(s, from, name, text, cseq, -1);
}

/* a line only this one player sees (the others get an empty one in its place) */
static void say_to(Server *s, const SrvClient *c, const char *text) { add_chat_to(s, -1, "", text, 0, c->id); }

void server_say(Server *s, const char *text)
{
    add_chat(s, -1, "", text, 0);
    slog(s, LOG_INFO, "* %s", text);
}

static void send_chat(Server *s, SrvClient *c)
{
    uint32_t first = c->chat_seen + 1, q;
    int sent = 0, missed = s->chat_seq - c->chat_seen > 32;
    if (missed) first = s->chat_seq - 31;
    for (q = first; q <= s->chat_seq && sent < 4; q++, sent++) {
        const ChatLine *l = &s->chat[(q - 1) & 31];
        uint8_t b[NET_MAX_PACKET];
        Pack k;
        if (l->seq != q) continue;
        pk_init(&k, b, sizeof b);
        pk_header(&k, NM_CHAT);
        pk_u32(&k, l->seq);
        pk_u8(&k, (uint32_t)(l->from < 0 ? 255 : l->from));
        pk_u32(&k, l->cseq);
        pk_str(&k, l->to < 0 || l->to == c->id ? l->name : "", NET_NAME - 1);
        pk_str(&k, l->to < 0 || l->to == c->id ? l->text : "", NET_CHAT - 1);
        /* Only the server can tell the client to skip discarded lines. Repeat this
         * marker until it acknowledges the new sequence, even if the packet is lost. */
        if (missed && q == first) pk_u32(&k, c->cseq_done);
        send_to(s, &c->addr, &k);
    }
}

/* ---- lobby -------------------------------------------------------------------------- */

static const SrvClient *lobby_team_leader(const Server *s, int side)
{
    const SrvClient *leader = NULL;
    int i;
    for (i = 0; i < NET_LOBBY_MAX; i++) {
        const SrvClient *p = &s->c[i];
        if (!p->used || !p->authed || p->gone || p->side != side) continue;
        if (!leader || p->side_order < leader->side_order) leader = p;
    }
    return leader;
}

static SrvClient *lobby_host(Server *s)
{
    SrvClient *host = NULL;
    int i;
    for (i = 0; i < NET_LOBBY_MAX; i++) {
        SrvClient *p = &s->c[i];
        if (!p->used || !p->authed || p->gone) continue;
        if (!host || p->join_order < host->join_order) host = p;
    }
    return host;
}

static int host_can_start(const Server *s, const SrvClient *host)
{
    int i, count = 0;
    for (i = 0; i < NET_LOBBY_MAX; i++) {
        const SrvClient *p = &s->c[i];
        if (!p->used || !p->authed || p->gone) continue;
        if (p != host && !p->ready) return 0;
        count++;
    }
    return count >= s->cfg.min_players;
}

static void lobby_set_side(Server *s, SrvClient *c, int side)
{
    if (c->side == side && !c->gone) return;
    if (side != SIDE_ANY) c->side_order = ++s->lobby_order;
    c->side = side;
}

static void send_welcome(Server *s, SrvClient *c)
{
    uint8_t b[NET_MAX_PACKET];
    Pack k;
    pk_init(&k, b, sizeof b);
    pk_header(&k, NM_WELCOME);
    pk_u8(&k, (uint32_t)c->id);
    pk_str(&k, s->cfg.name, NET_SERVER_NAME - 1);
    pk_str(&k, s->cfg.motd, NET_MOTD - 1);
    pk_u8(&k, (uint32_t)s->cfg.max_players);
    pk_u8(&k, (uint32_t)s->cfg.min_players);
    pk_u8(&k, s->cfg.base.versus);
    pk_u8(&k, (uint32_t)server_world(s));
    pk_str(&k, s->map_name, 31);
    pk_u32(&k, s->chat_seq - c->chat_seen > 32 ? s->chat_seq - 32 : c->chat_seen);
    send_to(s, &c->addr, &k);
}

static void send_lobby(Server *s, SrvClient *to)
{
    uint8_t b[NET_MAX_PACKET];
    Pack k;
    int i, n = 0;
    uint32_t left = 0;
    if (s->state == SRV_COUNTDOWN) {
        uint32_t el = net_ms() - s->countdown_at;
        left = el < (uint32_t)s->cfg.countdown_ms ? ((uint32_t)s->cfg.countdown_ms - el + 999) / 1000 : 0;
    }
    pk_init(&k, b, sizeof b);
    pk_header(&k, NM_LOBBY);
    pk_u8(&k, (uint32_t)s->state);
    pk_u8(&k, left);
    for (i = 0; i < NET_LOBBY_MAX; i++) n += s->c[i].used && s->c[i].authed && !s->c[i].gone;
    pk_u8(&k, (uint32_t)n);
    for (i = 0; i < NET_LOBBY_MAX; i++) {
        const SrvClient *c = &s->c[i];
        if (!c->used || !c->authed || c->gone) continue;
        pk_u8(&k, (uint32_t)c->id);
        pk_str(&k, c->name, NET_NAME - 1);
        pk_u8(&k, (uint32_t)c->ready);
        pk_u8(&k, c->cls);
        pk_u8(&k, (uint32_t)(c->slot >= 0 ? 1 : c->spectating ? 2 : 0));   /* 1 playing, 2 watching */
        pk_u8(&k, (uint32_t)c->side);
        pk_u16(&k, (uint32_t)(c->rtt > 9999 ? 9999 : c->rtt));
    }
    {   /* (after the list, so older clients just don't read it) */
        int ready = 0;
        for (i = 0; i < NET_LOBBY_MAX; i++) ready += s->c[i].used && s->c[i].authed && !s->c[i].gone && s->c[i].ready;
        (void)ready;
        pick_next_map(s);
        pk_u8(&k, (uint32_t)bots_for(s, n > MATCH_PLAYERS ? MATCH_PLAYERS : n));
        pk_u8(&k, (uint32_t)s->cfg.bot_level);
    }
    {   /* the next map, how it's chosen, and the start_after clock */
        uint32_t wait = 0;
        int m;
        pk_str(&k, s->map_name, 31);
        pk_u8(&k, (uint32_t)s->cfg.rotation);
        pk_u8(&k, (uint32_t)s->nmaps);
        if (!s->cfg.manual_start && s->ready_since && s->cfg.start_after_ms && s->state == SRV_LOBBY) {
            uint32_t el = net_ms() - s->ready_since;
            wait = el < (uint32_t)s->cfg.start_after_ms ? ((uint32_t)s->cfg.start_after_ms - el + 999) / 1000 : 0;
        }
        pk_u8(&k, wait > 255 ? 255 : wait);
        pk_u8(&k, (uint32_t)places_free(s, to));         /* this player may have a reserved tank */
        pk_u16(&k, s->cfg.base.time_limit);              /* the time limit, seconds (0 = none) */
        pk_u8(&k, s->cfg.base.time_rule);
        for (m = 0; m < s->nmaps && m < NET_MAPS; m++) {   /* votes per map (vote rotation; 255: not on the ballot) */
            int v = 0;
            for (i = 0; i < NET_LOBBY_MAX; i++) v += s->c[i].used && s->c[i].authed && !s->c[i].gone && s->c[i].vote == m + 1;
            pk_u8(&k, s->cfg.rotation == ROT_VOTE && !s->ballot[m] ? 255u : (uint32_t)v);
        }
        for (m = 0; m < s->nmaps && m < NET_MAPS; m++)   /* the ballot: its maps by name (newer games show them) */
            if (s->ballot[m]) { pk_u8(&k, (uint32_t)(m + 1)); pk_str(&k, s->cfg.map_names[m], 31); }
        pk_u8(&k, 0);
        {   /* a rematch: how many of the last match's players want one, of how many */
            int ry, ro;
            rematch_count(s, &ry, &ro);
            pk_u8(&k, (uint32_t)ry);
            pk_u8(&k, (uint32_t)ro);
        }
        pk_u8(&k, next_extras(s));                   /* the next match's mode (a mode rotation's) */
        pk_u8(&k, (uint32_t)room_for(s));             /* map capacity, including computer tanks */
        for (m = SIDE_CAMP; m <= SIDE_FORTRESS; m++) {
            const SrvClient *leader = lobby_team_leader(s, m);
            pk_u8(&k, leader ? (uint32_t)leader->id : 0);
            pk_str(&k, s->lobby_team_name[m - SIDE_CAMP], NET_TEAM_NAME - 1);
        }
        {
            const SrvClient *host = lobby_host(s);
            pk_u8(&k, host ? (uint32_t)host->id : 0);
            pk_u8(&k, (uint32_t)(s->cfg.manual_start != 0));
        }
        {
            unsigned admins = 0;
            int row = 0;
            for (i = 0; i < NET_LOBBY_MAX; i++) {
                const SrvClient *c = &s->c[i];
                if (!c->used || !c->authed || c->gone) continue;
                if (c->admin) admins |= 1u << row;
                pk_u8(&k, c->portrait);
                row++;
            }
            pk_u8(&k, (uint32_t)eliminated(s, to));
            pk_u8(&k, LOBBY_HOST_RENAME | (to->admin ? LOBBY_ADMIN : 0));
            pk_u16(&k, admins);
        }
    }
    send_to(s, &to->addr, &k);
}

static void send_leaders(Server *s, SrvClient *c)
{
    uint8_t b[NET_MAX_PACKET];
    Pack k;
    int i;
    pk_init(&k, b, sizeof b);
    pk_header(&k, NM_LEADERS);
    pk_u8(&k, (uint32_t)s->nleaders);
    for (i = 0; i < s->nleaders; i++) {
        pk_str(&k, s->leaders[i].name, NET_NAME - 1);
        pk_u16(&k, (uint32_t)s->leaders[i].matches);
        pk_u16(&k, (uint32_t)s->leaders[i].wins);
        pk_u16(&k, (uint32_t)s->leaders[i].kills);
        pk_u32(&k, (uint32_t)s->leaders[i].score);
        pk_u16(&k, (uint32_t)(s->leaders[i].rating < 0 ? 0 : s->leaders[i].rating));
    }
    send_to(s, &c->addr, &k);
}

static void send_match(Server *s, SrvClient *c)
{
    uint8_t b[NET_MAX_PACKET];
    Pack k;
    pk_init(&k, b, sizeof b);
    pk_header(&k, NM_MATCH);
    pk_u8(&k, (uint32_t)c->slot);
    pk_setup(&k, &s->ms);
    pk_u32(&k, s->map_len);
    pk_u32(&k, s->map_hash);
    pk_u32(&k, s->state == SRV_RUNNING ? visible_tick(s, c, net_ms()) : 0);   /* where the match is (a rejoin catches up to it) */
    pk_u8(&k, (uint32_t)s->delay);                       /* this match's input delay */
    {   /* the map's content id: a client that has it already needn't download it */
        uint8_t h[32];
        if (s->map) sha256(s->map, strlen(s->map), h); else memset(h, 0, sizeof h);
        pk_bytes(&k, h, 8);
    }
    pk_str(&k, s->map_name, 31);
    pk_u8(&k, s->ms.weather);                            /* (0.4: appended, older builds stop before it) */
    pk_u8(&k, s->ms.extras);                             /* (0.6: pickups, a hill: the same way) */
    pk_u32(&k, c->slot >= 0 ? c->assignment : 0);
    send_to(s, &c->addr, &k);
}

/* the latest official ticks, from whatever this client last confirmed */
/* how far a client may see: players all of it, watchers spectator_delay behind */
static uint32_t spectator_delay_ms(const Server *s) { return (uint32_t)s->cfg.spectator_delay_s * 1000; }

static uint32_t visible_tick(const Server *s, const SrvClient *c, uint32_t now)
{
    uint32_t delay = spectator_delay_ms(s);
    if (c->slot >= 0 || !delay) return s->tick;
    if (s->ended_at) {
        int32_t elapsed = (int32_t)(now - s->ended_at);
        if (elapsed >= (int32_t)delay) return s->tick;
        if (elapsed > 0) delay -= (uint32_t)elapsed;
    }
    uint32_t hidden = (delay * TICK_RATE + 999) / 1000;
    return s->tick > hidden ? s->tick - hidden : 0;
}

static void send_ticks(Server *s, SrvClient *c, uint32_t now)
{
    uint8_t b[NET_MAX_PACKET];
    Pack k;
    int tanks = s->game.num_tanks, room = (NET_MAX_PACKET - 16) / (tanks ? tanks : 1);
    uint32_t upto = visible_tick(s, c, now), first = c->acked, t;
    if (room > 250) room = 250;
    if (first > upto) first = upto;
    if (upto - first > (uint32_t)room) first = upto - (uint32_t)room;
    if (upto - first < NET_REDUNDANT) first = upto > NET_REDUNDANT ? upto - NET_REDUNDANT : 0;
    if (s->tick - first > 250) {                        /* older than the live ring: from the match's record */
        if (!s->replay.inputs || first >= s->replay.ticks) return;
        /* (the record may have stopped: every tick sent must be in the ring or in the record) */
        if (s->tick - s->replay.ticks > 250 && upto > s->replay.ticks) upto = s->replay.ticks;
    }
    pk_init(&k, b, sizeof b);
    pk_header(&k, NM_TICKS);
    pk_u32(&k, first);
    pk_u8(&k, upto - first);
    pk_u8(&k, (uint32_t)tanks);
    for (t = first; t < upto; t++)
        pk_bytes(&k, s->tick - t <= 250 ? s->final[t & 255] : s->replay.inputs + (size_t)t * (size_t)tanks, tanks);
    send_to(s, &c->addr, &k);
}

static void send_end(Server *s, SrvClient *c)
{
    uint8_t b[16];
    Pack k;
    pk_init(&k, b, sizeof b);
    pk_header(&k, NM_END);
    pk_u8(&k, (uint32_t)(s->game.winner < 0 ? 255 : s->game.winner));
    send_to(s, &c->addr, &k);
}

static SrvClient *find(Server *s, const NetAddr *a)
{
    int i;
    for (i = 0; i < NET_LOBBY_MAX; i++)
        if (s->c[i].used && net_same(&s->c[i].addr, a)) return &s->c[i];
    return NULL;
}

/* ---- who's knocking: cookies, keys, names ------------------------------------------- */

/* the cookie for an address, in 30-second windows: shows the sender really is at
 * that address before the server keeps anything for it */
static uint64_t cookie_for(const Server *s, const NetAddr *a, uint32_t window)
{
    uint8_t buf[32 + 10 + 16], h[32];
    uint64_t c = 0;
    int i, n = 42;
    memcpy(buf, s->secret, 32);
    buf[32] = (uint8_t)(a->ip >> 24); buf[33] = (uint8_t)(a->ip >> 16); buf[34] = (uint8_t)(a->ip >> 8); buf[35] = (uint8_t)a->ip;
    buf[36] = (uint8_t)(a->port >> 8); buf[37] = (uint8_t)a->port;
    buf[38] = (uint8_t)(window >> 24); buf[39] = (uint8_t)(window >> 16); buf[40] = (uint8_t)(window >> 8); buf[41] = (uint8_t)window;
    if (a->v6) { memcpy(buf + 42, a->ip6, 16); n += 16; }   /* (IPv6: the whole address, not just its key) */
    sha256(buf, (size_t)n, h);
    for (i = 0; i < 8; i++) c = c << 8 | h[i];
    return c ? c : 1;
}

static void pk_u64(Pack *k, uint64_t v) { pk_u32(k, (uint32_t)v); pk_u32(k, (uint32_t)(v >> 32)); }
static uint64_t uk_u64(Pack *k) { uint64_t lo = uk_u32(k); return lo | (uint64_t)uk_u32(k) << 32; }

static void key_proof(uint64_t cookie, const uint8_t verifier[32], uint8_t out[32])
{
    uint8_t buf[8 + 32];
    int i;
    for (i = 0; i < 8; i++) buf[i] = (uint8_t)(cookie >> (8 * i));
    memcpy(buf + 8, verifier, 32);
    sha256(buf, sizeof buf, out);
}

/* the key store: the hooks (the database), or memory for this run */
static int key_get(Server *s, const uint8_t id[16], uint8_t v[32])
{
    int i;
    if (s->hooks.key_get) return s->hooks.key_get(s->hooks.ctx, id, v);
    for (i = 0; i < s->nmem_keys; i++)
        if (!memcmp(s->mem_keys[i].id, id, 16)) { memcpy(v, s->mem_keys[i].verifier, 32); return 1; }
    return 0;
}
static void key_put(Server *s, const uint8_t id[16], const uint8_t v[32])
{
    if (s->hooks.key_put) { s->hooks.key_put(s->hooks.ctx, id, v); return; }
    if (s->nmem_keys < 256) {
        memcpy(s->mem_keys[s->nmem_keys].id, id, 16);
        memcpy(s->mem_keys[s->nmem_keys].verifier, v, 32);
        s->mem_keys[s->nmem_keys].name[0] = 0;
        s->nmem_keys++;
    }
}
static int name_get(Server *s, const char *name, uint8_t id[16])
{
    int i;
    if (s->hooks.name_get) return s->hooks.name_get(s->hooks.ctx, name, id);
    for (i = 0; i < s->nmem_keys; i++)
        if (!strcmp(s->mem_keys[i].name, name)) { memcpy(id, s->mem_keys[i].id, 16); return 1; }
    return 0;
}
static void name_put(Server *s, const char *name, const uint8_t id[16])
{
    int i;
    if (s->hooks.name_put) { s->hooks.name_put(s->hooks.ctx, name, id); return; }
    for (i = 0; i < s->nmem_keys; i++)
        if (!memcmp(s->mem_keys[i].id, id, 16) && !s->mem_keys[i].name[0]) {
            snprintf(s->mem_keys[i].name, sizeof s->mem_keys[i].name, "%s", name);
            return;
        }
}

static void send_cookie(Server *s, const NetAddr *to, uint32_t now)
{
    uint8_t b[64];
    Pack k;
    pk_init(&k, b, sizeof b);
    pk_header(&k, NM_COOKIE);
    pk_u64(&k, cookie_for(s, to, now / 30000));
    pk_bytes(&k, s->server_id, 16);
    pk_u8(&k, s->cfg.password[0] != 0);
    send_to(s, to, &k);                                  /* 29 bytes: smaller than the HELLO that asked */
}

static void hello(Server *s, SrvClient *c, const NetAddr *from, Pack *k, uint32_t now)
{
    uint32_t build = uk_u32(k), window = now / 30000;
    char name[NET_NAME];
    uint64_t cookie;
    uint8_t id[16], proof[32], answer[32], verifier[32], want[32];
    int i, free_slot = -1, has_verifier, same_ip = 0;
    uk_str(k, name, sizeof name);
    cookie = uk_u64(k);
    uk_bytes(k, id, 16);
    uk_bytes(k, proof, 32);
    uk_bytes(k, answer, 32);
    has_verifier = (int)uk_u8(k);
    if (has_verifier) uk_bytes(k, verifier, 32);
    {
        int oldest = (int)uk_u8(k);                      /* (protocol 8 on: the oldest the client still speaks) */
        if (k->bad || k->cap < 140) return;              /* too short to be a real HELLO: no answer at all */
        if (!oldest) oldest = (int)(build >> 16);
        if (!net_compatible(build, oldest)) {
            slog(s, LOG_INFO, "refused a game of protocol %u (we speak %d, oldest %d)", (unsigned)(build >> 16),
                 NET_PROTOCOL, NET_PROTOCOL_OLDEST);
            send_bye(s, from, NR_VERSION);
            return;
        }
        {   /* the same protocol: do we compute the same game? (0: an older build, doesn't say) */
            uint32_t rules = uk_u32(k);
            if (k->bad) rules = 0;
            k->bad = 0;
            if (rules && rules != net_rules()) {
                slog(s, LOG_WARN, "refused a game with other rules (%08x, ours %08x): a different build of %s?",
                     (unsigned)rules, (unsigned)net_rules(), OFP_VERSION);
                send_bye(s, from, NR_RULES);
                return;
            }
        }
    }
    if (cookie != cookie_for(s, from, window) && cookie != cookie_for(s, from, window - 1)) {
        send_cookie(s, from, now);                       /* first prove you're at that address */
        return;
    }
    if (c && c->authed) { send_welcome(s, c); return; }  /* (it didn't get the first one) */
    if (is_banned(s, id, from->ip)) {
        char where[64];
        net_addr_str(from, where, sizeof where);
        slog(s, LOG_INFO, "refused a banned player from %s", where);
        send_bye(s, from, NR_KICKED);
        return;
    }

    if (s->cfg.password[0]) {                            /* prove you know it */
        if (pw_blocked(s, from->ip, now)) return;        /* (guessing: no answers for now) */
        sha256_password((uint32_t)cookie, s->cfg.password, want);
        if (memcmp(want, answer, 32)) {
            char where[64];
            net_addr_str(from, where, sizeof where);
            slog(s, LOG_WARN, "wrong password from %s", where);
            pw_failed(s, from->ip, now);
            send_bye(s, from, NR_PASSWORD);
            return;
        }
    }
    {   /* and that you're who your key says */
        uint8_t known[32], check[32], hid[32];
        if (key_get(s, id, known)) {
            key_proof(cookie, known, check);
            if (memcmp(check, proof, 32)) { send_bye(s, from, NR_KEY); return; }
        } else if (has_verifier) {
            sha256(verifier, 32, hid);                   /* a new key: its id must come from its verifier */
            key_proof(cookie, verifier, check);
            if (memcmp(hid, id, 16) || memcmp(check, proof, 32)) { send_bye(s, from, NR_KEY); return; }
            key_put(s, id, verifier);
        } else {
            uint8_t b[16];
            Pack o;
            pk_init(&o, b, sizeof b);
            pk_header(&o, NM_NEEDKEY);
            send_to(s, from, &o);
            return;
        }
    }

    /* back after a dropout: take up the place in the match again, and catch up */
    for (i = 0; i < NET_LOBBY_MAX; i++) {
        SrvClient *o = &s->c[i];
        if (o->used && o->slot >= 0 && !memcmp(o->key_id, id, 16) &&
            (o->gone || !net_same(&o->addr, from)) && (s->state == SRV_RUNNING || s->state == SRV_LOADING)) {
            char line[NET_CHAT];
            if (eliminated(s, o)) {
                leave_match(s, o);
                o->used = 0;
                break;
            }
            if (c && c != o) c->used = 0;
            o->addr = *from;
            lobby_set_side(s, o, o->side);
            o->join_order = ++s->lobby_order;
            o->gone = 0;
            o->heard = now;
            o->acked = 0;
            o->catching = 1;
            o->ready = 2;
            slog(s, LOG_INFO, "%s is back, catching up on the match", o->name);
            snprintf(line, sizeof line, "%s is back", o->name);
            add_chat(s, -1, "", line, 0);
            send_welcome(s, o);
            send_match(s, o);
            return;
        }
    }

    for (i = 0; i < NET_LOBBY_MAX; i++)                  /* not too many from one address */
        if (s->c[i].used && s->c[i].authed && !s->c[i].gone && s->c[i].addr.ip == from->ip) same_ip++;
    if (!c && same_ip >= s->cfg.max_per_address) { send_bye(s, from, NR_TOO_MANY); return; }
    if (!c) {
        int full = server_people(s) >= s->cfg.max_players, q, place = -1;
        uint32_t now2 = net_ms();
        for (q = 0; q < s->nqueue; q++)                  /* (the queue: those not heard from for 15 s, gone) */
            if (now2 - s->queue[q].seen > 15000) { memmove(&s->queue[q], &s->queue[q + 1], (size_t)(s->nqueue - q - 1) * sizeof s->queue[0]); s->nqueue--; q--; }
        for (q = 0; q < s->nqueue; q++) if (!memcmp(s->queue[q].id, id, 16)) place = q;
        if (!full && s->nqueue && place != 0) {           /* (a place free, but someone's been waiting longer) */
            int free_places = s->cfg.max_players - server_people(s);
            if (place < 0 ? free_places <= s->nqueue : place >= free_places) full = 1;
        }
        if (full) {                                      /* full: in the queue (or still in it), told which place */
            uint8_t b[64];
            Pack q8;
            if (place < 0 && s->nqueue < 16) { memcpy(s->queue[s->nqueue].id, id, 16); place = s->nqueue++; }
            if (place >= 0) s->queue[place].seen = now2;
            pk_init(&q8, b, sizeof b);
            pk_header(&q8, NM_BYE);
            pk_u8(&q8, NR_FULL);
            pk_u8(&q8, NET_PROTOCOL);
            pk_u8(&q8, NET_PROTOCOL_OLDEST);
            pk_str(&q8, OFP_VERSION, 15);
            pk_u8(&q8, (uint32_t)(place >= 0 ? place + 1 : 0));   /* (newer games: their place in the queue; 0 none) */
            send_to(s, from, &q8);
            return;
        }
        if (place >= 0) {                                /* (in: out of the queue) */
            memmove(&s->queue[place], &s->queue[place + 1], (size_t)(s->nqueue - place - 1) * sizeof s->queue[0]);
            s->nqueue--;
        }
        for (i = 0; i < NET_LOBBY_MAX; i++)
            if (!s->c[i].used || (s->c[i].gone && s->c[i].slot < 0)) { free_slot = i; break; }
        if (free_slot < 0) { send_bye(s, from, NR_FULL); return; }
        c = &s->c[free_slot];
        memset(c, 0, sizeof *c);
        c->used = 1; c->addr = *from; c->heard = now; c->slot = -1; c->id = s->next_id++;
        if (s->next_id > 250) s->next_id = 1;
        c->cls = TANK_MEDIUM;
    }
    memcpy(c->key_id, id, 16);
    {   /* a name: yours if it's free or already yours; otherwise Name2, Name3, ... */
        char base[NET_NAME];
        uint8_t owner[16];
        int n = 2, ok;
        snprintf(base, sizeof base, "%s", name[0] ? name : "Player");
        if (!strcmp(base, "Computer")) snprintf(base, sizeof base, "Player");
        snprintf(c->name, sizeof c->name, "%s", base);
        do {
            ok = !name_get(s, c->name, owner) || !memcmp(owner, id, 16);
            for (i = 0; ok && i < NET_LOBBY_MAX; i++)
                if (&s->c[i] != c && s->c[i].used && s->c[i].authed && !s->c[i].gone && !strcmp(s->c[i].name, c->name)) ok = 0;
            if (!ok) snprintf(c->name, sizeof c->name, "%.12s%u", base, (unsigned)(n++ % 100));
        } while (!ok && n < 99);
        if (!name_get(s, c->name, owner)) name_put(s, c->name, id);
    }
    c->authed = 1;
    c->join_order = ++s->lobby_order;
    c->build = build;
    c->chat_seen = s->chat_seq > 8 ? s->chat_seq - 8 : 0;   /* the last few lines, and on from there */
    {
        char where[64];
        net_addr_str(from, where, sizeof where);
        slog(s, LOG_INFO, "%s joined from %s (%d here)", c->name, where, server_people(s));
    }
    {
        char line[NET_CHAT];
        snprintf(line, sizeof line, "%s joined", c->name);
        add_chat(s, -1, "", line, 0);
    }
    send_welcome(s, c);
    if (s->state == SRV_COUNTDOWN) s->state = SRV_LOBBY;  /* somebody new: wait for them to get ready */
}

/* wrong passwords (the server's and the admin's): 5 a minute from one address, then a
 * minute's pause; nothing is answered while it lasts */
static int pw_blocked(Server *s, uint32_t ip, uint32_t now)
{
    int i;
    for (i = 0; i < 32; i++)
        if (s->pwfail[i].ip == ip && s->pwfail[i].blocked_until && (int32_t)(s->pwfail[i].blocked_until - now) > 0) return 1;
    return 0;
}

static void pw_failed(Server *s, uint32_t ip, uint32_t now)
{
    int i, slot = 0;
    for (i = 0; i < 32; i++) {
        if (s->pwfail[i].ip == ip) { slot = i; break; }
        if (now - s->pwfail[i].since > now - s->pwfail[slot].since) slot = i;   /* (else the oldest) */
    }
    if (s->pwfail[slot].ip != ip || now - s->pwfail[slot].since > 60000) {
        s->pwfail[slot].ip = ip; s->pwfail[slot].since = now; s->pwfail[slot].fails = 0; s->pwfail[slot].blocked_until = 0;
    }
    if (++s->pwfail[slot].fails >= 5) {
        s->pwfail[slot].blocked_until = now + 60000;
        s->pwfail[slot].since = now;
        s->pwfail[slot].fails = 0;
        slog(s, LOG_WARN, "too many wrong passwords from %u.%u.%u.%u: not answering it for a minute",
             ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
    }
}

/* a few queries a second per address, a few hundred in all */
static int query_allowed(Server *s, const NetAddr *from, uint32_t now)
{
    uint32_t sec = now / 1000, h = (from->ip * 2654435761u) >> 26;
    if (sec != s->qsecond) { s->qsecond = sec; s->qcount = 0; }
    if (++s->qcount > 300) return 0;
    if (s->qlimit[h].ip != from->ip || s->qlimit[h].second != sec) {
        s->qlimit[h].ip = from->ip; s->qlimit[h].second = sec; s->qlimit[h].count = 0;
    }
    return ++s->qlimit[h].count <= 5;
}

void server_stun(Server *s, const NetAddr *stun) { if (s->sock >= 0) net_stun_ask(s->sock, stun, s->stun_tx); }

void server_punch(Server *s, const NetAddr *to)
{   /* (any packet will do: it's our router that learns to let theirs in; the game ignores what it didn't ask) */
    uint8_t b[16];
    Pack k;
    pk_init(&k, b, sizeof b);
    pk_header(&k, NM_PONG);
    pk_u32(&k, 0);
    send_to(s, to, &k);
}

static void handle(Server *s, const NetAddr *from, uint8_t *buf, int n, uint32_t now)
{
    Pack k;
    int type;
    SrvClient *c;
    {   /* the STUN server's answer: how the internet sees us */
        NetAddr pub;
        if (n >= 20 && buf[0] == 0x01 && net_stun_answer(buf, n, s->stun_tx, &pub)) {
            if (!s->have_public || !net_same(&pub, &s->public_addr)) {
                char w[64];
                net_addr_str(&pub, w, sizeof w);
                slog(s, LOG_INFO, "seen from the internet as %s (STUN)", w);
            }
            s->public_addr = pub;
            s->have_public = 1;
            return;
        }
    }
    pk_init(&k, buf, n);
    type = uk_header(&k);
    if (!type) return;
    if (type == NM_QUERY) {                              /* anyone may ask, if the question is as big as the answer */
        uint8_t b[NET_MAX_PACKET];
        Pack o;
        ServerInfo info;
        if (n < NET_QUERY_MIN || !query_allowed(s, from, now)) return;
        server_info(s, &info);
        pk_init(&o, b, sizeof b);
        pk_header(&o, NM_INFO);
        pk_info(&o, &info);
        send_to(s, from, &o);
        return;
    }
    c = find(s, from);
    if (type == NM_HELLO) { hello(s, c, from, &k, now); return; }
    if (!c || !c->authed) return;
    c->heard = now;
    if (type == NM_REMATCH) {                            /* a rematch: yes or no (if they played the last one) */
        int yes = uk_u8(&k) ? 1 : 0;
        if (!k.bad && s->rematch_open && c->last_team1 && c->rematch != yes) {
            char out[96];
            c->rematch = (uint8_t)yes;
            snprintf(out, sizeof out, yes ? "%s wants a rematch" : "%s doesn't want a rematch", c->name);
            add_chat(s, -1, "", out, 0);
        }
        return;
    }
    if (c->gone && type != NM_BYE) {
        lobby_set_side(s, c, c->side);
        c->join_order = ++s->lobby_order;
        c->gone = 0;
    }
    switch (type) {
    case NM_PING: {
        uint32_t flags = uk_u8(&k), cls = uk_u8(&k), steer = uk_u8(&k), seen = uk_u32(&k);
        uint32_t side = uk_u8(&k), sent_ms = uk_u32(&k), rtt = uk_u16(&k), have = uk_u32(&k), ready = flags & PING_READY;
        if (k.bad) return;
        if (k.n < k.cap) {
            uint32_t portrait = uk_u8(&k);
            c->portrait = portrait <= NET_AVATARS ? (uint8_t)portrait : 0;
        }
        {   /* the round trip: answer at once */
            uint8_t b[16];
            Pack o;
            pk_init(&o, b, sizeof b);
            pk_header(&o, NM_PONG);
            pk_u32(&o, sent_ms);
            send_to(s, from, &o);
        }
        c->rtt = (int)rtt;
        lobby_set_side(s, c, side <= SIDE_FORTRESS ? (int)side : SIDE_ANY);
        c->watching = (flags & PING_WATCH) != 0;
        c->joining = (flags & PING_JOIN) != 0;
        if (c->joining && s->state == SRV_RUNNING) {
            if (c->slot < 0) join_running(s, c);
            else if (c->catching && !c->acked) send_match(s, c);
        }
        if (c->watching && !s->cfg.allow_watching) {
            if (!c->told_no_watch) { say_to(s, c, "watching matches is off on this server"); c->told_no_watch = 1; }
            c->watching = 0;
        }
        if (!c->watching) c->spectating = 0;
        if (c->spectating && have > c->acked && have <= visible_tick(s, c, now)) c->acked = have;
        if (c->slot < 0 && c->watching && s->state == SRV_RUNNING) {
            if (!c->spectating) {
                c->spectating = 1;
                c->acked = 0;
                send_match(s, c);
            } else if (!c->acked && !have) send_match(s, c);
        }
        if (seen > c->chat_seen && seen <= s->chat_seq) c->chat_seen = seen;
        if (c->slot < 0) {
            int was = c->ready;
            if ((int)ready != c->ready && (s->state == SRV_LOBBY || s->state == SRV_COUNTDOWN))
                slog(s, LOG_DEBUG, "%s is %sready", c->name, ready ? "" : "not ");
            c->ready = (int)(ready & 1);
            c->cls = (uint8_t)(cls % TANK_CLASS_COUNT);
            c->steer = (uint8_t)(steer & 1);
            /* someone who was ready isn't any more: the countdown stops (the lobby's rules decide again). Not
             * every ping from someone who simply isn't ready: with start_after, those who never got ready
             * (watchers, someone away) stopped the countdown every few hundred ms, and the match never began */
            if (was && !c->ready && s->state == SRV_COUNTDOWN && !s->start_host) s->state = SRV_LOBBY;
        }
        break;
    }
    case NM_CHAT: {
        uint32_t cseq = uk_u32(&k);
        char text[NET_CHAT];
        uk_str(&k, text, sizeof text);
        if (k.bad || cseq <= c->cseq_done || !text[0]) return;   /* (a repeat of one we have) */
        c->cseq_done = cseq;
        {   /* no flooding: 5 lines at once, then one every 1.5 s; keep at it and it's 30 s of quiet */
            int refill = (int)((now - c->chat_refill) / 1500);
            if (!c->chat_refill) { c->chat_refill = now; c->chat_tokens = 5; }
            if (refill > 0) {
                c->chat_tokens = c->chat_tokens + refill > 5 ? 5 : c->chat_tokens + refill;
                c->chat_refill += (uint32_t)refill * 1500;
                if (c->chat_tokens == 5) c->chat_dropped = 0;
            }
            /* (muted_until 0: never muted. The clock wraps every 49.7 days, and past day 24.8 a
             * bare comparison would take 0 for a time still to come: everyone muted on joining) */
            int muted = c->muted_until && (int32_t)(c->muted_until - now) > 0;
            if (muted || c->chat_tokens <= 0) {
                /* not passed on: the sender gets it back as its own line (so its game stops resending) */
                add_chat_to(s, c->id, c->name, muted ? "(muted for a while: not sent)" : "(too fast: not sent)", cseq, c->id);
                if (++c->chat_dropped >= 5 && !muted) {
                    c->muted_until = now + 30000;
                    if (!c->muted_until) c->muted_until = 1;
                    c->chat_dropped = 0;
                    slog(s, LOG_INFO, "%s is flooding the chat: muted for 30 s", c->name);
                }
                break;
            }
            c->chat_tokens--;
        }
        if (text[0] == '/') {
            /* a command goes back to its sender alone, as its own line (a password masked): that's
             * what tells the game it arrived; otherwise it sends it again and again (/login's too) */
            char echo[NET_CHAT];
            if (!strncmp(text, "/login ", 7)) snprintf(echo, sizeof echo, "/login ****");
            else if (!strncmp(text, "/login-proof ", 13)) snprintf(echo, sizeof echo, "/login ...");
            else snprintf(echo, sizeof echo, "%s", text);
            add_chat_to(s, c->id, c->name, echo, cseq, c->id);
            chat_command(s, c, text + 1, cseq);
            break;
        }
        add_chat(s, c->id, c->name, text, cseq);
        slog(s, LOG_INFO, "<%s> %s", c->name, text);
        break;
    }
    case NM_LEADREQ:
        if (s->hooks.leaders) s->nleaders = s->hooks.leaders(s->hooks.ctx, s->leaders, NET_LEADERS);
        send_leaders(s, c);
        break;
    mapreq: {
        uint32_t off = uk_u32(&k);
        uint8_t b[NET_MAX_PACKET];
        Pack o;
        uint32_t len;
        if (k.bad || !s->map || off >= s->map_len || (c->slot < 0 && !c->spectating)) return;
        len = s->map_len - off < NET_MAP_CHUNK ? s->map_len - off : NET_MAP_CHUNK;
        pk_init(&o, b, sizeof b);
        pk_header(&o, NM_MAPDATA);
        pk_u32(&o, off);
        pk_u16(&o, len);
        pk_bytes(&o, s->map + off, (int)len);
        send_to(s, from, &o);
        break;
    }
    case NM_LOADED:
        if (c->slot >= 0 && (s->state == SRV_LOADING || s->state == SRV_RUNNING) && uk_u32(&k) == s->map_hash)
            c->ready = 2;                                /* 2 = loaded */
        break;
    case NM_INPUT: {
        uint32_t have = uk_u32(&k), first = uk_u32(&k), cnt = uk_u8(&k), i;
        int slot = c->slot;
        if (k.bad || s->state != SRV_RUNNING || c->spectating || c->gone ||
            slot < 0 || slot >= MATCH_PLAYERS || slot >= s->game.num_tanks ||
            s->in_match[slot] != (int)(c - s->c) ||
            (s->game.tanks[slot].ai && !s->take_at[slot])) return;
        if (have > c->acked && have <= s->tick) c->acked = have;
        if (c->catching && have_caught_up(s, c)) {
            c->catching = 0;
            slog(s, LOG_INFO, "%s has caught up", c->name);
        }
        for (i = 0; i < cnt; i++) {
            uint32_t t = first + i;
            uint8_t b = (uint8_t)uk_u8(&k);
            if (k.bad) break;
            if (t >= s->tick && t < s->tick + 200) {
                s->in[slot][t & 255] = b & (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT | IN_FIRE | IN_MINE);
                s->in_tick[slot][t & 255] = t + 1;
            }
        }
        break;
    }
    case NM_CHECK: {
        uint32_t t = uk_u32(&k), sum = uk_u32(&k);
        int i;
        if (k.bad || c->slot < 0 || s->state != SRV_RUNNING) return;
        (void)i;
        if (c->catching) return;                        /* (not caught up yet: its sums are old news) */
        if (t < s->tick && t + 200 > s->tick && s->sums[t & 255] != sum) {   /* not the game we're playing */
            uint8_t b[16];
            Pack o;
            c->resyncs++;
            if (c->resyncs > 2) {                       /* again and again: out of the match, the rest play on */
                slog(s, LOG_ERROR, "%s is out of step again at tick %u: dropped from the match", c->name, (unsigned)t);
                send_bye(s, &c->addr, NR_DESYNC);
                c->gone = 1;
                return;
            }
            slog(s, LOG_WARN, "%s is out of step at tick %u: playing the match again from the start", c->name, (unsigned)t);
            s->desyncs++;
            if (s->hooks.desync && !s->desync_reported) {   /* (the first time this match: a report) */
                s->desync_reported = 1;
                s->hooks.desync(s->hooks.ctx, s, c->name, c->build, t, sum, s->sums[t & 255]);
            }
            c->catching = 1;
            c->acked = 0;
            pk_init(&o, b, sizeof b);
            pk_header(&o, NM_RESYNC);
            pk_u32(&o, t);                              /* (where it happened) */
            send_to(s, &c->addr, &o);
        }
        break;
    }
    case NM_HISTREQ:
    case NM_MAPREQ:                                      /* both send a lot back: 60 a second each client */
        if (c->req_second != now / 1000) { c->req_second = now / 1000; c->req_count = 0; }
        if (++c->req_count > 60) return;
        if (type == NM_MAPREQ) goto mapreq;
        /* fall through */
    {                                                    /* catching up: the official ticks from here */
        uint32_t from_t = uk_u32(&k), t, cnt;
        uint8_t b[NET_MAX_PACKET];
        Pack o;
        int tanks = s->replay.tanks, room;
        uint32_t end;
        if (k.bad || (c->slot < 0 && !c->spectating) || s->state != SRV_RUNNING || tanks <= 0 || !s->replay.inputs) return;
        /* only what the match's record holds: it stops at REPLAY_MAX_TICKS (or out of memory) while
         * the match goes on, and reading past it would send the heap behind it */
        end = visible_tick(s, c, now);
        if (end > s->replay.ticks) end = s->replay.ticks;
        if (from_t >= end) return;
        room = (NET_MAX_PACKET - 16) / tanks;
        if (room > 250) room = 250;
        cnt = end - from_t;
        if (cnt > (uint32_t)room) cnt = (uint32_t)room;
        pk_init(&o, b, sizeof b);
        pk_header(&o, NM_HISTORY);
        pk_u32(&o, from_t);
        pk_u8(&o, cnt);
        pk_u8(&o, (uint32_t)tanks);
        for (t = from_t; t < from_t + cnt; t++) pk_bytes(&o, s->replay.inputs + (size_t)t * (size_t)tanks, tanks);
        send_to(s, from, &o);
        if (have_caught_up(s, c)) c->catching = 0;
        break;
    }
    case NM_LEAVEMATCH: {
        uint32_t assignment = uk_u32(&k);
        uint8_t reply[12];
        Pack out;
        int slot = c->slot;
        if (k.bad || !assignment) return;
        if (assignment == c->assignment && slot >= 0) {
            char line[NET_CHAT];
            leave_match(s, c);
            snprintf(line, sizeof line, "%s returned to the lobby", c->name);
            add_chat(s, -1, "", line, 0);
        }
        pk_init(&out, reply, sizeof reply);
        pk_header(&out, NM_LEAVEMATCH);
        pk_u32(&out, assignment);
        send_to(s, from, &out);
        break;
    }
    case NM_BYE:
        if (c->slot >= 0 && eliminated(s, c)) leave_match(s, c);
        if (!c->gone) {
            char line[NET_CHAT];
            c->gone = 1;
            slog(s, LOG_INFO, "%s left", c->name);
            snprintf(line, sizeof line, "%s left", c->name);
            add_chat(s, -1, "", line, 0);
        }
        if (c->slot < 0) c->used = 0;
        break;
    default:
        break;
    }
}

/* ---- matches -------------------------------------------------------------------------- */

/* a place a newcomer can take in the match that's on: a computer player's tank, or one
 * whose player left more than 30 s ago (and hasn't come back); -1 = none */
static int free_place(const Server *s, const SrvClient *c)
{
    int p, best = -1, humans[2] = { 0, 0 };
    int prefer_side = c->side;
    uint32_t now = net_ms();
    if (s->state != SRV_RUNNING || s->ended_at || eliminated(s, c)) return -1;
    p = reserved_place(s, c);
    if (p >= 0) return p;
    for (p = 0; p < s->ms.players; p++)
        if (s->in_match[p] >= 0 && !s->c[s->in_match[p]].gone) humans[s->ms.team[p] & 1]++;
    for (p = 0; p < s->ms.players; p++) {
        int ci = s->in_match[p];
        int open = ci < 0 ? !s->left_name[p][0] && s->ms.bot[p] != 0 && !s->late[p]
                          : s->c[ci].gone && s->gone_since[p] && now - s->gone_since[p] > 30000;
        if (!open || s->take_at[p] || s->game.tanks[p].out) continue;
        if (best < 0) best = p;
        if (prefer_side != SIDE_ANY && (s->ms.team[p] & 1) == (prefer_side == SIDE_FORTRESS ? TEAM_ENEMY : TEAM_PLAYER)) return p;
        if (prefer_side == SIDE_ANY && humans[s->ms.team[p] & 1] < humans[s->ms.team[best] & 1]) best = p;   /* the side with fewer people */
    }
    return best;
}

static int places_free(const Server *s, const SrvClient *c)
{
    int p, n = 0;
    uint32_t now = net_ms();
    if (s->state != SRV_RUNNING || s->ended_at || eliminated(s, c)) return 0;
    if (reserved_place(s, c) >= 0) return 1;
    for (p = 0; p < s->ms.players; p++) {
        int ci = s->in_match[p];
        if (s->game.tanks[p].out) continue;
        n += ci < 0 ? !s->left_name[p][0] && s->ms.bot[p] != 0 && !s->late[p] && !s->take_at[p]
                    : s->c[ci].gone && s->gone_since[p] && now - s->gone_since[p] > 30000;
    }
    return n;
}

static void join_running(Server *s, SrvClient *c)
{
    int p = free_place(s, c), ci;
    char line[NET_CHAT];
    if (p < 0) return;
    ci = s->in_match[p];
    if (ci >= 0) { s->c[ci].used = 0; s->c[ci].slot = -1; }   /* the one who left gives up the place */
    c->slot = p;
    c->assignment = ++s->next_assignment;
    c->spectating = 0;
    c->acked = 0;
    c->ready = 2;
    c->catching = 1;
    c->resyncs = 0;
    c->joining = 0;
    s->in_match[p] = (int)(c - s->c);
    memset(s->in_tick[p], 0, sizeof s->in_tick[p]);
    s->take_at[p] = s->tick + 1;                         /* in the official ticks: from the next one on */
    if (!s->left_name[p][0]) s->late[p] = 1;
    s->gone_since[p] = 0;
    slog(s, LOG_INFO, "%s joins the match on its way, in %s tank (place %d)", c->name,
         ci >= 0 || s->left_name[p][0] ? "a departed player's" : "a computer's", p + 1);
    s->left_name[p][0] = 0;
    memcpy(s->replay.names[p], c->name, sizeof s->replay.names[p]);
    snprintf(line, sizeof line, "%s joins the match", c->name);
    add_chat(s, -1, "", line, 0);
    send_match(s, c);
}

static int have_caught_up(const Server *s, const SrvClient *c) { return c->acked + 20 >= s->tick; }

static void start_loading(Server *s, uint32_t now)
{
    int i, n = 0, side[2] = { 0, 0 }, room[2], max_rtt = 0, pass, rematch_on = rematch_wanted(s);
    MatchSetup ms = s->cfg.base;
    pick_next_map(s);                                    /* the map: from the rotation (or the vote; a rematch: the same) */
    if (rematch_on) slog(s, 1, "* a rematch: the same %s, the same sides", s->nmaps ? "map" : "battlefield");
    ms.extras = next_extras(s);
    if (!rematch_on) s->match_mode_idx = s->mode_idx;
    s->map_idx = next_map_index(s);
    s->map = s->nmaps ? s->maps[s->map_idx] : NULL;
    if (s->map) ms.biome = (uint8_t)mapfile_biome(s->map);
    {   /* each side's room: the map's (team modes: even sides) */
        MatchSetup rs = ms;
        rs.camp_garages = ms.camp_garages ? ms.camp_garages : 4;
        match_room_sides(&rs, s->map, room);
    }
    for (i = 0; i < MATCH_PLAYERS; i++) s->in_match[i] = -1;
    /* the first four who are ready; in versus, the ones who asked for a side get it
     * (as far as garages and fairness go: never more than one ahead), the rest even it out */
    int order[NET_LOBBY_MAX], rtt_sum[2] = { 0, 0 };
    for (pass = 0; pass < 2; pass++) {
        int o, k2, nord = 0;
        for (i = 0; i < NET_LOBBY_MAX; i++) order[nord++] = i;
        if (pass == 1 && ms.versus && !rematch_on) {
            /* those with no side to ask for: who comes in stays as before (the first ready), but they're placed the
             * farthest first, each onto the side with fewer players or, as many, the lower round trips: the slow
             * connections spread over both sides, not all on one */
            int take = 0, room_left = (s->room > 0 ? s->room : MATCH_PLAYERS) - n;
            nord = 0;
            for (i = 0; i < NET_LOBBY_MAX && take < room_left; i++) {
                SrvClient *c = &s->c[i];
                if (!c->used || !c->authed || c->gone || !c->ready || c->slot >= 0 || c->side != SIDE_ANY) continue;
                order[nord++] = i;
                take++;
            }
            for (o = 1; o < nord; o++)
                for (k2 = o; k2 > 0 && s->c[order[k2]].rtt > s->c[order[k2 - 1]].rtt; k2--) { int t = order[k2]; order[k2] = order[k2 - 1]; order[k2 - 1] = t; }
            for (i = 0; i < NET_LOBBY_MAX; i++) {        /* (then anyone else, in their usual order) */
                int seen = 0;
                for (o = 0; o < nord; o++) seen |= order[o] == i;
                if (!seen) order[nord++] = i;
            }
        }
        for (o = 0; o < nord && n < (s->room > 0 ? s->room : MATCH_PLAYERS); o++) {   /* (as many as the map holds) */
            SrvClient *c;
            int team;
            i = order[o];
            c = &s->c[i];
            if (!c->used || !c->authed || c->gone || !c->ready || c->slot >= 0) continue;
            int wish = rematch_on && c->last_team1 ? (c->last_team1 == TEAM_ENEMY + 1 ? SIDE_FORTRESS : SIDE_CAMP) : c->side;   /* (a rematch: the same sides) */
            if (ms.versus && pass == 0 && wish == SIDE_ANY) continue;   /* wishes first */
            if (!ms.versus) team = TEAM_PLAYER;
            else {
                int want = wish == SIDE_FORTRESS ? TEAM_ENEMY : wish == SIDE_CAMP ? TEAM_PLAYER
                         : side[TEAM_PLAYER] < side[TEAM_ENEMY] ? TEAM_PLAYER : side[TEAM_ENEMY] < side[TEAM_PLAYER] ? TEAM_ENEMY
                         : rtt_sum[TEAM_PLAYER] <= rtt_sum[TEAM_ENEMY] ? TEAM_PLAYER : TEAM_ENEMY;
                team = want;
                if (side[team] >= room[team] || side[team] > side[1 - team]) team = 1 - team;   /* full, or too many */
                if (side[team] >= room[team]) continue;
            }
            side[team]++;
            rtt_sum[team] += c->rtt;
            c->slot = n;
            c->assignment = ++s->next_assignment;
            c->acked = 0;
            c->ready = 1;
            c->catching = 0;
            c->resyncs = 0;
            c->spectating = 0;
            s->in_match[n] = i;
            ms.cls[n] = c->cls;
            ms.steer[n] = c->steer;
            ms.team[n] = (uint8_t)team;
            ms.bot[n] = 0;
            if (c->rtt > max_rtt) max_rtt = c->rtt;
            n++;
        }
    }
    /* the input delay: enough for the slowest round trip, no more */
    if (rematch_on) ms.weather = s->ms.weather;
    else if (s->cfg.weather_mode > WEATHER_CLEAR && s->cfg.weather_mode < WEATHER_COUNT)
        ms.weather = (uint8_t)s->cfg.weather_mode;
    else if (s->cfg.weather_mode == SERVER_WEATHER_RANDOM) {
        uint32_t w = ((uint32_t)s->matches_played * 7u + now / 1000u) % 3u;
        ms.weather = (uint8_t)(w == 0 ? WEATHER_STORM : w == 1 ? WEATHER_CHANGING : WEATHER_CLEAR);
    }
    memset(s->take_at, 0, sizeof s->take_at);
    memset(s->late, 0, sizeof s->late);
    memset(s->gone_since, 0, sizeof s->gone_since);
    memset(s->left_name, 0, sizeof s->left_name);
    s->delay = (max_rtt / 2 + 25 + 19) / 20 + 1;
    if (s->delay < NET_DELAY_MIN) s->delay = NET_DELAY_MIN;
    if (s->delay > NET_DELAY_MAX) s->delay = NET_DELAY_MAX;
    {   /* computer players in the free slots: each with a tank of its own and a garage */
        int humans = n, b = bots_for(s, humans), k;
        static const char *lv[4] = { "", "easy", "normal", "hard" };
        for (k = 0; k < b && n < MATCH_PLAYERS; k++) {
            int team = !ms.versus ? TEAM_PLAYER : side[TEAM_PLAYER] <= side[TEAM_ENEMY] ? TEAM_PLAYER : TEAM_ENEMY;
            if (side[team] >= room[team]) team = 1 - team;
            if (side[team] >= room[team]) break;                    /* every garage is taken */
            side[team]++;
            ms.cls[n] = (uint8_t)((n + k) % TANK_CLASS_COUNT);
            ms.steer[n] = STEER_TANK;
            ms.team[n] = (uint8_t)team;
            ms.bot[n] = (uint8_t)(s->cfg.bot_level >= AI_EASY && s->cfg.bot_level <= AI_HARD ? s->cfg.bot_level : AI_NORMAL);
            n++;
        }
        b = n - humans;
        if (b) slog(s, LOG_INFO, "%d computer player%s (%s) join%s the match", b, b == 1 ? "" : "s",
                    lv[ms.bot[humans] & 3], b == 1 ? "s" : "");
    }
    if (ms.versus && n < 2) ms.versus = 0;               /* alone: against the computer instead */
    ms.players = (uint8_t)n;
    ms.bot_commands = 1;
    if (rematch_on) ms.seed = s->last_seed;   /* (a rematch: the same battlefield) */
    if (!ms.seed) ms.seed = (now * 2654435761u) ^ ((uint32_t)s->matches_played * 40503u) ^ 0x9E3779B9u;
    s->ms = ms;
    s->map_len = s->map ? (uint32_t)strlen(s->map) : 0;
    s->map_hash = match_hash(&s->ms, s->map);
    s->state = SRV_LOADING;
    s->loading_at = now;
    s->tick = 0;
    s->ended_at = 0;
    s->rematch_open = 0;
    s->reason = 0;
    memset(s->in_tick, 0, sizeof s->in_tick);
    slog(s, LOG_INFO, "match %d: %d players%s, %s%s%s, input delay %d ticks", s->matches_played + 1, n,
         ms.versus ? " versus" : " together", s->map ? s->map_name : biome_name((Biome)ms.biome),
         s->map ? "" : ", a random map", "", s->delay);
    snprintf(s->status, sizeof s->status, "loading a match for %d", n);
}

static int match_team_out(const Server *s, int team)
{
    int p, any = 0;
    for (p = 0; p < s->ms.players; p++)
        if (s->game.tanks[p].team == team) { if (!s->game.tanks[p].out) return 0; any = 1; }
    return any;
}

/* the match so far, in its mode's terms (the log: a line a minute, and at the end) */
static void match_score(const Server *s, char *out, size_t cap)
{
    const Game *g = &s->game;
    int k, a[2] = { 0, 0 };
    if (g->extras & EXTRA_CONQUEST) { snprintf(out, cap, "conquest %d - %d", g->cp_score[0], g->cp_score[1]); return; }
    if (g->extras & EXTRA_WARLORD) { snprintf(out, cap, "warlords down %d - %d", g->jug_score[0], g->jug_score[1]); return; }
    if (g->extras & EXTRA_KILLCONF) { snprintf(out, cap, "kills confirmed %d - %d", g->kc_score[0], g->kc_score[1]); return; }
    if (g->extras & EXTRA_HARDPOINT) { snprintf(out, cap, "hardpoint %d - %d", g->hp_score[0], g->hp_score[1]); return; }
    if (g->extras & EXTRA_ANNIHILATION) {
        for (k = 0; k < g->num_tanks; k++) a[0] += !g->tanks[k].out;
        snprintf(out, cap, "%d tank%s still in", a[0], a[0] == 1 ? "" : "s");
        return;
    }
    for (k = 0; k < g->num_tanks; k++) a[g->tanks[k].team & 1] += g->tanks[k].tank_kills;
    snprintf(out, cap, "%s %d - %d", g->extras & EXTRA_TDM ? "kills" : "tanks destroyed", a[0], a[1]);
}

static void open_rematch_vote(Server *s)
{
    int i;
    if (s->rematch_open) return;
    s->rematch_open = 1;
    for (i = 0; i < NET_LOBBY_MAX; i++) {
        s->c[i].rematch = 0;
        s->c[i].last_team1 = 0;
    }
    for (i = 0; i < s->ms.players; i++)
        if (s->in_match[i] >= 0)
            s->c[s->in_match[i]].last_team1 = (uint8_t)(s->ms.team[i] + 1);
    s->last_seed = s->ms.seed;
    s->last_map_idx = s->map_idx;
}

static void finish_match(Server *s)
{
    MatchResult r;
    int p;
    memset(&r, 0, sizeof r);
    r.players = s->ms.players;
    r.ticks = s->tick;
    r.versus = s->ms.versus;
    r.ai_level = s->ms.ai_level;
    r.winner = s->reason == NR_DESYNC ? -1 : s->game.winner;
    if (r.winner < 0 && s->reason != NR_DESYNC && !s->game.time_up) {   /* (time up without a winner: a draw) */
        if (match_team_out(s, TEAM_PLAYER) && !match_team_out(s, TEAM_ENEMY)) r.winner = TEAM_ENEMY;
        else if (match_team_out(s, TEAM_ENEMY) && !match_team_out(s, TEAM_PLAYER)) r.winner = TEAM_PLAYER;
    }
    open_rematch_vote(s);
    for (p = 0; p < r.players; p++) {
        const Tank *t = &s->game.tanks[p];
        int ci = s->in_match[p];
        snprintf(r.name[p], sizeof r.name[p], "%s", ci >= 0 ? s->c[ci].name :
                 s->left_name[p][0] ? s->left_name[p] : bot_name(s->ms.seed, p));
        r.bot[p] = ci >= 0 || s->left_name[p][0] ? 0 : s->ms.bot[p];
        r.late[p] = s->late[p];
        r.team[p] = t->team;
        r.won[p] = s->game.ffa_winner >= 0 ? s->game.ffa_winner == p   /* (every tank for itself: the one left only) */
                 : r.winner >= 0 && t->team == r.winner;
        r.kills[p] = t->tank_kills;
        r.score[p] = (int)t->score;
        r.saved[p] = t->saved;
        r.lost[p] = t->lost;
        r.soldiers[p] = t->kills;
        r.helis[p] = t->helis_down;
        r.cls[p] = t->cls;
    }
    s->matches_played++;
    slog(s, LOG_INFO, "match %d over after %u s: %s", s->matches_played, (unsigned)(s->tick / TICK_RATE),
         r.winner == TEAM_PLAYER ? "the camp side won" : r.winner == TEAM_ENEMY ? "the fortress side won" :
         s->game.time_up ? "time up, a draw" : "no winner");
    if (s->game.time_up) slog(s, LOG_INFO, "(the time limit decided it)");
    for (p = 0; p < r.players; p++)                     /* each player's: their side, tanks destroyed and lost, score */
        slog(s, LOG_INFO, "  %s (%s): destroyed %d, lost %d, score %d", r.name[p], r.team[p] == TEAM_PLAYER ? "camp" : "fortress",
             r.kills[p], r.lost[p], r.score[p]);
    {   /* and each player's */
        char sc[64];
        match_score(s, sc, sizeof sc);
        slog(s, LOG_INFO, "  %s", sc);
        for (p = 0; p < r.players; p++)
            slog(s, LOG_INFO, "  %-15s %-8s destroyed %d, lost %d, score %d%s", r.name[p], r.team[p] == TEAM_PLAYER ? "camp" : "fortress",
                 r.kills[p], r.lost[p], r.score[p], r.won[p] ? "  (won)" : "");
    }
    r.biome = s->game.biome;
    r.draw = r.winner < 0 && (s->game.time_up || s->game.humans_out);
    r.night = s->ms.time == TIME_NIGHT;
    if (s->hooks.match_over) s->hooks.match_over(s->hooks.ctx, s, &r, &s->replay);
    for (p = 0; p < r.players; p++) {                    /* achievements, in the lobby's chat */
        int a;
        for (a = 0; a < ACH_COUNT; a++)
            if ((r.earned[p] >> a) & 1) {
                char line[NET_CHAT];
                snprintf(line, sizeof line, "%s earned %s", r.name[p], achieve_name(a));
                add_chat(s, -1, "", line, 0);
            }
    }
    if (s->hooks.leaders) s->nleaders = s->hooks.leaders(s->hooks.ctx, s->leaders, NET_LEADERS);
    {
        char line[NET_CHAT];
        snprintf(line, sizeof line, "match over: %s", r.winner == TEAM_PLAYER ? "the camp side won" :
                 r.winner == TEAM_ENEMY ? "the fortress side won" : "no winner");
        add_chat(s, -1, "", line, 0);
    }
    for (p = 0; p < NET_LOBBY_MAX; p++) {               /* everyone back in the lobby, not ready */
        if (s->c[p].slot >= 0 && s->c[p].gone) s->c[p].used = 0;
        s->c[p].slot = -1;
        s->c[p].ready = 0;
    }
    for (p = 0; p < MATCH_PLAYERS; p++) s->in_match[p] = -1;
    for (p = 0; p < NET_LOBBY_MAX; p++) { s->c[p].vote = 0; s->c[p].spectating = 0; }
    if (s->cfg.nmodes > 1) {                       /* a rematch leaves the next ordinary mode pending */
        s->mode_idx = (s->match_mode_idx + 1) % s->cfg.nmodes;
        s->cfg.base.extras = (uint8_t)((s->cfg.base.extras & EXTRA_PICKUPS) | s->cfg.modes[s->mode_idx]);
        slog(s, LOG_INFO, "* the next mode: %s", mode_word(s->cfg.base.extras));
    }
    make_ballot(s);                                      /* (the next vote: three made for the mode) */
    if (s->nmaps > 1) {                                  /* the next map: made for the mode, if any are */
        int k, start = s->cfg.rotation == ROT_RANDOM ? rand() % s->nmaps : s->map_idx + 1;
        s->next_idx = (s->map_idx + 1) % s->nmaps;
        for (k = 0; k < s->nmaps; k++) {
            int cand = (start + k) % s->nmaps;
            if (cand == s->map_idx && s->nmaps > 1 && s->cfg.rotation == ROT_RANDOM) continue;   /* (not the same one twice) */
            if (map_fits_mode(s->maps[cand], s->cfg.base.extras)) { s->next_idx = cand; break; }
        }
    }
    s->ready_since = 0;
    s->state = SRV_LOBBY;
    pick_next_map(s);
    replay_free(&s->replay);
    snprintf(s->status, sizeof s->status, "lobby: %d of %d", server_people(s), s->cfg.max_players);
}

int server_step(Server *s)
{
    uint8_t buf[NET_MAX_PACKET];
    NetAddr from;
    uint32_t now = net_ms();
    int n, i;
    if (s->state == SRV_CLOSED) return SRV_CLOSED;
    while ((n = net_recv(s->sock, &from, buf, sizeof buf)) > 0) {
        s->pkts_in++;
        s->bytes_in += (uint32_t)n;
        handle(s, &from, buf, n, now);
    }
    for (i = 0; i < NET_LOBBY_MAX; i++) {                /* who has gone quiet */
        SrvClient *c = &s->c[i];
        if (c->used && !c->gone && (int32_t)(now - c->heard) > SRV_GONE_MS) {
            char line[NET_CHAT];
            c->gone = 1;
            if (c->authed) {
                slog(s, LOG_INFO, "%s timed out", c->name);
                snprintf(line, sizeof line, "%s lost the connection", c->name);
                add_chat(s, -1, "", line, 0);
            }
            if (c->slot < 0) c->used = 0;
        }
    }

    if (s->state == SRV_LOBBY || s->state == SRV_COUNTDOWN) {
        int here = 0, ready = 0;
        for (i = 0; i < NET_LOBBY_MAX; i++)
            if (s->c[i].used && s->c[i].authed && !s->c[i].gone) { here++; ready += s->c[i].ready != 0; }
        if (s->state == SRV_COUNTDOWN && s->start_host) {
            SrvClient *starter = NULL;
            for (i = 0; i < NET_LOBBY_MAX; i++)
                if (s->c[i].used && s->c[i].authed && !s->c[i].gone && s->c[i].join_order == s->start_host)
                    starter = &s->c[i];
            if (!starter || (!starter->admin && starter != lobby_host(s)) || !host_can_start(s, starter)) {
                s->state = SRV_LOBBY;
                server_say(s, "start cancelled: the host or readiness changed");
            } else if ((int32_t)(now - s->countdown_at) >= s->cfg.countdown_ms) {
                starter->ready = 1;
                s->start_host = 0;
                start_loading(s, now);
            }
        } else {
            /* start_after lets an automatic lobby start without waiting for everyone. */
            int enough = ready >= s->cfg.min_players && ready > 0;
            s->start_host = 0;
            if (!enough) s->ready_since = 0;
            else if (!s->ready_since) s->ready_since = now ? now : 1;
            if (s->state == SRV_LOBBY && !s->cfg.manual_start && enough && (ready == here ||
                (s->cfg.start_after_ms && (int32_t)(now - s->ready_since) >= s->cfg.start_after_ms))) {
                s->state = SRV_COUNTDOWN;                 /* a few seconds, then go */
                s->countdown_at = now;
                server_say(s, ready == here ? "everyone is ready: the match starts in a moment"
                                            : "enough are ready: the match starts in a moment");
            }
            if (s->state == SRV_COUNTDOWN &&
                (!enough || (ready != here && !(s->cfg.start_after_ms && (int32_t)(now - s->ready_since) >= s->cfg.start_after_ms))))
                s->state = SRV_LOBBY;
            if (s->state == SRV_COUNTDOWN && (int32_t)(now - s->countdown_at) >= s->cfg.countdown_ms)
                start_loading(s, now);
        }
        if (s->state == SRV_LOBBY) s->start_host = 0;
        if (s->state == SRV_LOBBY) snprintf(s->status, sizeof s->status, "lobby: %d of %d here, %d ready", here, s->cfg.max_players, ready);
    }

    if (s->state == SRV_LOADING) {
        int all = 1;
        for (i = 0; i < MATCH_PLAYERS; i++)
            if (s->in_match[i] >= 0 && !s->c[s->in_match[i]].gone && s->c[s->in_match[i]].ready != 2) all = 0;
        if (!all && (int32_t)(now - s->loading_at) > SRV_LOAD_MS) {   /* someone never got there */
            for (i = 0; i < MATCH_PLAYERS; i++)
                if (s->in_match[i] >= 0 && s->c[s->in_match[i]].ready != 2) s->c[s->in_match[i]].gone = 1;
            all = 1;
        }
        if (all) {                                        /* off we go */
            if (match_start(&s->game, &s->ms, s->map) != 0) {
                slog(s, LOG_ERROR, "the map would not load: back to the lobby");
                s->game.winner = -1;
                finish_match(s);
            } else {
                replay_begin(&s->replay, &s->ms, s->map, s->game.num_tanks);
                s->desync_reported = 0;
                {   /* who's who, in the replay */
                    int q;
                    for (q = 0; q < NET_LOBBY_MAX; q++)
                        if (s->c[q].used && s->c[q].slot >= 0 && s->c[q].slot < MATCH_PLAYERS)
                            snprintf(s->replay.names[s->c[q].slot], sizeof s->replay.names[0], "%s", s->c[q].name);
                    for (q = 0; q < s->ms.players; q++)
                        if (s->left_name[q][0])
                            snprintf(s->replay.names[q], sizeof s->replay.names[q], "%s", s->left_name[q]);
                }
                s->state = SRV_RUNNING;
                s->last_final = s->started = now;
                snprintf(s->status, sizeof s->status, "playing: %d players", s->ms.players);
            }
        }
    }

    if (s->state == SRV_RUNNING && !s->ended_at) {
        for (;;) {   /* make ticks official: everyone's buttons in, or a late one has waited long enough */
            uint32_t t = s->tick;
            int ready = 1, p;
            InputBits set[MAX_TANKS];
            memset(set, 0, sizeof set);
            if (t > (now - s->started) / (1000 / TICK_RATE) + (uint32_t)s->delay) break;   /* never ahead of the clock */
            for (p = 0; p < MATCH_PLAYERS; p++) {
                const SrvClient *o = s->in_match[p] >= 0 ? &s->c[s->in_match[p]] : NULL;
                if (o && !o->gone && !o->catching && t >= o->acked + 180) ready = 0;   /* nor so far ahead they can't catch up */
                if (o && t >= (uint32_t)s->delay) {
                    int has = s->in_tick[p][t & 255] == t + 1;
                    if (!has && !o->gone && !o->catching && now - s->last_final < SRV_WAIT_MS) ready = 0;
                    if (has) set[p] = s->in[p][t & 255];
                }
            }
            if (!ready) break;
            for (p = 0; p < MATCH_PLAYERS; p++)          /* a newcomer takes a tank over from this tick */
                if (s->take_at[p] && s->take_at[p] == t + 1) { set[p] = (InputBits)(set[p] | IN_TAKEOVER); s->take_at[p] = 0; }
            game_tick_server(&s->game, set);
            memcpy(s->final[t & 255], set, sizeof s->final[0]);
            replay_tick(&s->replay, set, &s->game);
            s->sums[t & 255] = game_checksum(&s->game);
            s->tick++;
            if (s->tick % (60 * TICK_RATE) == 0) {       /* a line a minute: how it's going */
                char sc[64];
                int pl = 0, q;
                for (q = 0; q < MATCH_PLAYERS; q++) pl += s->in_match[q] >= 0 && !s->c[s->in_match[q]].gone;
                match_score(s, sc, sizeof sc);
                slog(s, LOG_INFO, "match %d: %u min in, %d of %d playing, %s", s->matches_played + 1, (unsigned)(s->tick / TICK_RATE / 60),
                     pl, s->ms.players, sc);
            }
            s->last_final = now;
            if (s->replay.stopped || s->replay.ticks >= REPLAY_MAX_TICKS) {   /* the record is full (or out of memory):
                                                          catching up and watching need every tick, so the match ends here */
                slog(s, LOG_WARN, "match %d: the record is full at %u min: the match ends here", s->matches_played + 1,
                     (unsigned)(s->tick / TICK_RATE / 60));
                s->ended_at = now ? now : 1;
                break;
            }
            if (s->game.winner >= 0 || s->game.time_up || match_team_out(s, TEAM_PLAYER) || match_team_out(s, TEAM_ENEMY)) {
                s->ended_at = now ? now : 1;
                break;
            }
            if (s->tick % 8 == 0) break;
        }
        {   /* everyone in the match has left */
            int left = 1;
            for (i = 0; i < MATCH_PLAYERS; i++) if (s->in_match[i] >= 0 && !s->c[s->in_match[i]].gone) left = 0;
            if (left && !s->ended_at) s->ended_at = now ? now : 1;
        }
    }

    for (i = 0; i < MATCH_PLAYERS; i++) {                /* when did they leave (for taking over) */
        int ci = s->in_match[i];
        if (ci >= 0 && s->c[ci].gone && !s->gone_since[i]) s->gone_since[i] = now ? now : 1;
        if (ci >= 0 && !s->c[ci].gone) s->gone_since[i] = 0;
    }

    /* Keep the final recording until active spectators have had time to play its delayed tail. */
    uint32_t end_wait = 2500;
    if (s->state == SRV_RUNNING && s->ended_at) {
        open_rematch_vote(s);
        if (s->reason != NR_ENDED) {                   /* an admin can still end or restart immediately */
            for (i = 0; i < NET_LOBBY_MAX; i++) {
                const SrvClient *c = &s->c[i];
                if (c->used && !c->gone && c->spectating) {
                    end_wait += spectator_delay_ms(s);
                    break;
                }
            }
        }
    }
    if (now - s->last_send >= 15) {
        int lobby_time = now - s->last_lobby >= 250;
        s->last_send = now;
        if (lobby_time) s->last_lobby = now;
        for (i = 0; i < NET_LOBBY_MAX; i++) {
            SrvClient *c = &s->c[i];
            if (!c->used || !c->authed || c->gone) continue;
            if (c->slot >= 0 && s->state == SRV_LOADING && c->ready != 2 && lobby_time) send_match(s, c);
            if ((c->slot >= 0 || c->spectating) && s->state == SRV_RUNNING) {
                /* a watcher (not catching up) gets the ticks ten times a second, several to a packet: a sixth
                 * of the packets a player gets, the match no later to the eye (they watch behind anyway) */
                int watcher = c->slot < 0 && c->spectating && !c->catching;
                if (!watcher || now - c->ticks_at >= 100) { send_ticks(s, c, now); c->ticks_at = now; }
            }
            if (s->state == SRV_RUNNING && s->ended_at && lobby_time &&
                (c->slot >= 0 || (c->spectating && (s->reason == NR_ENDED || c->acked >= s->tick ||
                 (int32_t)(now - s->ended_at) >= (int32_t)(end_wait - 750)))))
                send_end(s, c);
            if (lobby_time) { send_lobby(s, c); send_chat(s, c); send_team_order(s, c); }
            else if (c->chat_seen < s->chat_seq) send_chat(s, c);
        }
    }
    if (s->state == SRV_RUNNING && s->ended_at && (int32_t)(now - s->ended_at) > (int32_t)end_wait) finish_match(s);
    return s->state;
}

/* ---- maps, bans, commands ------------------------------------------------------------ */

/* a map made for the server's mode? (its modes= line; none: it has camps, so every mode but annihilation) */
static int map_fits_mode(const char *text, uint8_t extras)
{
    const char *m = text ? strstr(text, "\nmodes=") : NULL, *word;
    char line[96];
    int n = 0;
    word = extras & EXTRA_CONQUEST ? "conquest" : extras & EXTRA_ANNIHILATION ? "annihilation" : extras & EXTRA_TDM ? "deathmatch"
         : extras & EXTRA_WARLORD ? "warlord" : extras & EXTRA_KILLCONF ? "killconfirmed" : extras & EXTRA_HARDPOINT ? "hardpoint" : "flag";
    if (!m) return !(extras & EXTRA_ANNIHILATION) && !(text && strstr(text, "\narena=yes"));
    m += 7;
    while (m[n] && m[n] != '\n' && n < (int)sizeof line - 1) { line[n] = m[n]; n++; }
    line[n] = 0;
    return strstr(line, word) != NULL;
}

/* vote rotation: three maps made for the mode to vote on (not the one just played, if there are enough) */
static void make_ballot(Server *s)
{
    int fit[NET_MAPS], nfit = 0, i, k;
    memset(s->ballot, 0, sizeof s->ballot);
    if (s->cfg.rotation != ROT_VOTE || s->nmaps < 2) return;
    for (i = 0; i < s->nmaps && i < NET_MAPS; i++) if (map_fits_mode(s->maps[i], s->cfg.base.extras)) fit[nfit++] = i;
    if (!nfit) for (i = 0; i < s->nmaps && i < NET_MAPS; i++) fit[nfit++] = i;   /* (none made for it: any) */
    for (i = nfit - 1; i > 0; i--) { int j = rand() % (i + 1), t = fit[i]; fit[i] = fit[j]; fit[j] = t; }
    for (i = k = 0; i < nfit && k < 3; i++)
        if (fit[i] != s->map_idx || nfit <= 3) { s->ballot[fit[i]] = 1; k++; }
}

/* which map comes next: in order, at random (chosen after each match), or by vote (of the ballot) */
static void pick_next_map(Server *s)
{
    int chosen;
    if (s->state == SRV_RUNNING || s->state == SRV_LOADING) return;
    if (s->nmaps > 1 && s->cfg.rotation == ROT_VOTE) {
        int votes[NET_MAPS], i, best = -1, nb = 0, pick;
        memset(votes, 0, sizeof votes);
        for (i = 0; i < NET_LOBBY_MAX; i++)
            if (s->c[i].used && s->c[i].authed && !s->c[i].gone && s->c[i].vote > 0 && s->c[i].vote <= s->nmaps)
                votes[s->c[i].vote - 1]++;
        for (i = 0; i < s->nmaps; i++) if (s->ballot[i] && votes[i] && (best < 0 || votes[i] > votes[best])) best = i;
        if (best < 0) {                                  /* (no votes: one of the ballot, at random) */
            for (i = 0; i < s->nmaps; i++) nb += s->ballot[i];
            pick = nb ? rand() % nb : -1;
            for (i = 0; i < s->nmaps && pick >= 0; i++) if (s->ballot[i] && pick-- == 0) best = i;
        }
        if (best >= 0) s->next_idx = best;
    }
    chosen = next_map_index(s);
    snprintf(s->map_name, sizeof s->map_name, "%s", s->nmaps ? s->cfg.map_names[chosen] : "random");
    s->map = s->nmaps ? s->maps[chosen] : NULL;
    work_out_room(s);
}

static int is_banned(Server *s, const uint8_t id[16], uint32_t ip)
{
    int i;
    static const uint8_t zero[16];
    if (s->hooks.ban_check) return s->hooks.ban_check(s->hooks.ctx, id, ip);
    for (i = 0; i < s->nmem_bans; i++)
        if ((memcmp(s->mem_bans[i].id, zero, 16) && !memcmp(s->mem_bans[i].id, id, 16)) || (s->mem_bans[i].ip && s->mem_bans[i].ip == ip))
            return 1;
    return 0;
}

static SrvClient *by_name(Server *s, const char *name)
{
    int i;
    for (i = 0; i < NET_LOBBY_MAX; i++)
        if (s->c[i].used && s->c[i].authed && !s->c[i].gone && !strcmp(s->c[i].name, name)) return &s->c[i];
    for (i = 0; i < NET_LOBBY_MAX; i++)                  /* (any case) */
        if (s->c[i].used && s->c[i].authed && !s->c[i].gone) {
            const char *a = s->c[i].name, *b = name;
            while (*a && *b && ((*a | 32) == (*b | 32))) { a++; b++; }
            if (!*a && !*b) return &s->c[i];
        }
    return NULL;
}

static void drop_player(Server *s, SrvClient *c, int reason, const char *why)
{
    char line[NET_CHAT];
    send_bye(s, &c->addr, reason);
    c->gone = 1;
    if (c->slot < 0) c->used = 0;
    snprintf(line, sizeof line, "%s %s", c->name, why);
    add_chat(s, -1, "", line, 0);
}

void server_command(Server *s, const char *line, char *out, size_t cap)
{
    char cmd[32] = "", arg[NET_CHAT] = "", rest[NET_CHAT] = "";
    const char *p = line;
    int i, n = 0;
    while (*p == ' ' || *p == '/') p++;
    while (*p && *p != ' ' && n < (int)sizeof cmd - 1) cmd[n++] = *p++;
    cmd[n] = 0;
    while (*p == ' ') p++;
    snprintf(rest, sizeof rest, "%s", p);
    n = 0;
    while (*p && *p != ' ' && n < (int)sizeof arg - 1) arg[n++] = *p++;
    arg[n] = 0;
    out[0] = 0;

    if (!strcmp(cmd, "help") || !cmd[0]) {
        snprintf(out, cap, "commands: backup [FILE] (console), status, players, say TEXT, kick NAME, ban NAME [REASON], unban NAME, "
                           "bots N, start, restart, win camp|fortress, maps, map N|next");
    } else if (!strcmp(cmd, "status")) {
        static const char *st[] = { "lobby", "countdown", "loading", "playing", "closed" };
        snprintf(out, cap, "%s: %d of %d here, %s, match %d%s, map %s, bots %s%d, delay %d ticks", s->cfg.name,
                 server_people(s), s->cfg.max_players, st[s->state < 5 ? s->state : 4], s->matches_played + 1,
                 s->state == SRV_RUNNING ? " on" : "", s->map_name, s->cfg.allow_bots ? "" : "off/",
                 s->cfg.fill_bots ? 9 : s->cfg.bots, s->delay);
    } else if (!strcmp(cmd, "players") || !strcmp(cmd, "list")) {
        size_t len = 0;
        len += (size_t)snprintf(out + len, cap - len, "%d here:", server_people(s));
        for (i = 0; i < NET_LOBBY_MAX && len < cap; i++) {
            const SrvClient *c = &s->c[i];
            if (!c->used || !c->authed || c->gone) continue;
            len += (size_t)snprintf(out + len, cap - len, " %s(%s%s, %d ms)", c->name,
                                    c->slot >= 0 ? "playing" : c->spectating ? "watching" : c->ready ? "ready" : "lobby",
                                    c->admin ? ", admin" : "", c->rtt);
        }
    } else if (!strcmp(cmd, "say")) {
        if (rest[0]) { server_say(s, rest); snprintf(out, cap, "said"); }
    } else if (!strcmp(cmd, "kick") || !strcmp(cmd, "ban")) {
        SrvClient *c = by_name(s, arg);
        int ban = !strcmp(cmd, "ban");
        const char *reason = rest[strlen(arg)] ? rest + strlen(arg) + 1 : "";
        if (!c) { snprintf(out, cap, "nobody called %s here", arg); return; }
        if (ban) {
            if (s->hooks.ban_add) s->hooks.ban_add(s->hooks.ctx, c->key_id, c->addr.ip, c->name, reason);
            else if (s->nmem_bans < 64) {
                memcpy(s->mem_bans[s->nmem_bans].id, c->key_id, 16);
                s->mem_bans[s->nmem_bans].ip = c->addr.ip;
                s->nmem_bans++;
            }
        }
        slog(s, LOG_INFO, "%s %s%s%s", c->name, ban ? "banned" : "kicked", reason[0] ? ": " : "", reason);
        snprintf(out, cap, "%s %s", c->name, ban ? "banned" : "kicked");
        drop_player(s, c, NR_KICKED, ban ? "was banned" : "was kicked");
    } else if (!strcmp(cmd, "unban")) {
        int done = s->hooks.ban_remove ? s->hooks.ban_remove(s->hooks.ctx, arg) : (s->nmem_bans = 0, 1);
        snprintf(out, cap, done ? "unbanned %s" : "no ban for %s", arg);
    } else if (!strcmp(cmd, "bots")) {
        int b = atoi(arg);
        if (b < 0 || b > 3) { snprintf(out, cap, "bots 0 to 3"); return; }
        s->cfg.bots = b;
        s->cfg.fill_bots = 0;
        s->cfg.allow_bots = 1;
        snprintf(out, cap, "%d computer player%s from the next match", b, b == 1 ? "" : "s");
    } else if (!strcmp(cmd, "start")) {
        if (s->state != SRV_LOBBY) { snprintf(out, cap, "not in the lobby"); return; }
        for (i = n = 0; i < NET_LOBBY_MAX; i++) n += s->c[i].used && s->c[i].authed && !s->c[i].gone && s->c[i].ready;
        if (!n) { snprintf(out, cap, "nobody is ready"); return; }
        s->start_host = 0;
        s->state = SRV_COUNTDOWN;
        s->countdown_at = net_ms() - (uint32_t)s->cfg.countdown_ms;   /* now */
        s->ready_since = 1;
        s->cfg.start_after_ms = s->cfg.start_after_ms ? s->cfg.start_after_ms : 1;
        snprintf(out, cap, "starting with the %d who are ready", n);
    } else if (!strcmp(cmd, "restart") || !strcmp(cmd, "end")) {
        if (s->state != SRV_RUNNING && s->state != SRV_LOADING) { snprintf(out, cap, "no match on"); return; }
        s->game.winner = -1;
        s->reason = NR_ENDED;
        s->ended_at = net_ms() - 2000;                   /* over, in half a second (time to tell everyone: */
                                                         /*  at 2600 it closed before a word reached them) */
        if (s->state == SRV_LOADING) s->state = SRV_RUNNING;
        server_say(s, "the match was ended by the server");
        snprintf(out, cap, "match ended: back to the lobby");
    } else if (!strcmp(cmd, "win")) {                   /* end the match, and give it to a side */
        int side = !strcmp(arg, "camp") ? TEAM_PLAYER : !strcmp(arg, "fortress") ? TEAM_ENEMY : -1;
        if (side < 0) { snprintf(out, cap, "win camp or win fortress"); return; }
        if (s->state != SRV_RUNNING) { snprintf(out, cap, "no match on"); return; }
        s->game.winner = side;
        s->reason = NR_ENDED;
        s->ended_at = net_ms() - 2000;                   /* (half a second: time to tell everyone) */
        server_say(s, side == TEAM_PLAYER ? "the server gave the match to the camp" : "the server gave the match to the fortress");
        snprintf(out, cap, "match given to the %s", arg);
    } else if (!strcmp(cmd, "maps")) {
        size_t len = 0;
        if (!s->nmaps) { snprintf(out, cap, "a new random battlefield every match"); return; }
        len += (size_t)snprintf(out, cap, "maps (%s):", s->cfg.rotation == ROT_VOTE ? "vote with /vote N" :
                                 s->cfg.rotation == ROT_RANDOM ? "random" : "in order");
        for (i = 0; i < s->nmaps && len < cap; i++)
            len += (size_t)snprintf(out + len, cap - len, " %d %s%s%s", i + 1, s->cfg.map_names[i], i == next_map_index(s) ? " (next)" : "",
                                    s->cfg.rotation == ROT_VOTE && s->ballot[i] ? " (vote)" : "");
    } else if (!strcmp(cmd, "map")) {
        int m = !strcmp(arg, "next") ? (s->next_idx + 1) % (s->nmaps ? s->nmaps : 1) : atoi(arg) - 1;
        if (!s->nmaps || m < 0 || m >= s->nmaps) { snprintf(out, cap, "map 1 to %d", s->nmaps); return; }
        s->next_idx = m;
        pick_next_map(s);
        snprintf(out, cap, "next map: %s", s->map_name);
    } else {
        snprintf(out, cap, "unknown command %s (help lists them)", cmd);
    }
}

static SrvClient *match_team_leader(Server *s, int team)
{
    SrvClient *best = NULL; int i;
    for (i = 0; i < NET_LOBBY_MAX; i++) {
        SrvClient *p = &s->c[i];
        if (!p->used || !p->authed || p->gone || p->slot < 0 || p->slot >= s->game.num_tanks ||
            s->game.tanks[p->slot].ai || (s->game.tanks[p->slot].team & 1) != team) continue;
        if (!best || (p->side_order ? p->side_order : p->join_order) <
                     (best->side_order ? best->side_order : best->join_order)) best = p;
    }
    return best;
}
static void send_team_order(Server *s, SrvClient *c)
{
    uint8_t b[32]; Pack k; TeamOrder order = {0, 0, 0, 0, 0};
    int team, allowed = 0; SrvClient *leader;
    if (c->slot < 0 || c->slot >= s->game.num_tanks || s->state != SRV_RUNNING) return;
    team = s->game.tanks[c->slot].team & 1; leader = match_team_leader(s, team);
    s->game.team_leader[team] = leader ? (int8_t)leader->slot : -1;
    if (game_orders_allowed(&s->game)) {
        allowed = leader == c;
        if (game_order_active(&s->game, team)) order = s->game.team_order[team];
    }
    pk_init(&k, b, sizeof b); pk_header(&k, NM_TEAMORDER); pk_u32(&k, c->assignment);
    pk_u8(&k, (uint8_t)allowed); pk_u8(&k, order.kind); pk_u8(&k, order.issuer);
    pk_u16(&k, (uint16_t)order.tx); pk_u16(&k, (uint16_t)order.ty); pk_u32(&k, order.until);
    send_to(s, &c->addr, &k);
}

/* /commands in the chat: everyone may vote and list maps; the rest after /login */
static void chat_command(Server *s, SrvClient *c, const char *cmd, uint32_t cseq)
{
    char out[NET_CHAT * 3];
    (void)cseq;
    if (!strncmp(cmd, "order ", 6)) {
        unsigned assignment; int kind, n = 0, team;
        if (sscanf(cmd + 6, "%u %d%n", &assignment, &kind, &n) != 2 || cmd[6 + n] ||
            s->state != SRV_RUNNING || s->ended_at || c->slot < 0 || c->slot >= s->game.num_tanks ||
            assignment != c->assignment || c->gone || c->spectating || !game_orders_allowed(&s->game)) {
            say_to(s, c, "team orders are available only in an enhanced team match"); return;
        }
        team = s->game.tanks[c->slot].team & 1;
        if (match_team_leader(s, team) != c) { say_to(s, c, "only your team leader can give orders"); return; }
        s->game.team_leader[team] = (int8_t)c->slot;
        if (game_team_order(&s->game, c->slot, kind)) send_team_order(s, c);
        return;
    }
    if (!strcmp(cmd, "start") && (s->cfg.manual_start || !c->admin)) {
        if (!s->cfg.manual_start) say_to(s, c, "this server starts automatically");
        else if (c != lobby_host(s) && !c->admin) say_to(s, c, "only the lobby host or an admin can start the match");
        else if (s->state != SRV_LOBBY) say_to(s, c, "the lobby is already starting or playing");
        else if (!host_can_start(s, c)) say_to(s, c, "waiting for the other humans to ready up and for enough players");
        else {
            s->start_host = c->join_order;
            s->state = SRV_COUNTDOWN;
            s->countdown_at = net_ms();
            server_say(s, c == lobby_host(s) ? "the lobby host started the countdown" : "an admin started the countdown");
        }
        return;
    }
    if (!strcmp(cmd, "teamname") || !strncmp(cmd, "teamname ", 9)) {
        const char *name = cmd + 8;
        int side = c->side;
        size_t n;
        if (s->state != SRV_LOBBY && s->state != SRV_COUNTDOWN) {
            say_to(s, c, "change the team name between matches");
            return;
        }
        name += strspn(name, " ");
        if (name[0] == '#' && (name[1] == '1' || name[1] == '2') && name[2] == ' ') {
            side = name[1] == '1' ? SIDE_CAMP : SIDE_FORTRESS;
            name += 3;
            name += strspn(name, " ");
        }
        if (!s->cfg.base.versus || (next_extras(s) & EXTRA_ANNIHILATION) || side == SIDE_ANY ||
            (lobby_team_leader(s, side) != c && lobby_host(s) != c && !c->admin)) {
            say_to(s, c, "only the team leader, lobby host or an admin can rename this team");
            return;
        }
        n = strlen(name);
        while (n && name[n - 1] == ' ') n--;
        if (!n || n >= NET_TEAM_NAME) {
            snprintf(out, sizeof out, "use /teamname NAME (1 to %d characters)", NET_TEAM_NAME - 1);
            say_to(s, c, out);
            return;
        }
        snprintf(s->lobby_team_name[side - SIDE_CAMP], NET_TEAM_NAME, "%.*s", (int)n, name);
        snprintf(out, sizeof out, "%s named their team %.*s", c->name, (int)n, name);
        add_chat(s, -1, "", out, 0);
        return;
    }
    if (!strcmp(cmd, "slap") || !strncmp(cmd, "slap ", 5)) {
        const char *name = cmd + 4;
        size_t n;
        char target_name[NET_NAME];
        SrvClient *target;
        uint32_t now = net_ms();
        name += strspn(name, " ");
        n = strlen(name);
        while (n && name[n - 1] == ' ') n--;
        if (!n || n >= sizeof target_name) { say_to(s, c, "use /slap PLAYER"); return; }
        snprintf(target_name, sizeof target_name, "%.*s", (int)n, name);
        target = by_name(s, target_name);
        if (!target || target == c) { say_to(s, c, "pick another player who is online"); return; }
        if ((c->slap_sent && now - c->slap_sent < 6000) ||
            (target->slap_received && now - target->slap_received < 6000)) {
            say_to(s, c, "wait a few seconds between slaps");
            return;
        }
        c->slap_sent = target->slap_received = now ? now : UINT32_MAX;
        snprintf(out, sizeof out, "%s slaps @%s with a rubber tank", c->name, target->name);
        add_chat(s, -1, "", out, 0);
        return;
    }
    if (!strcmp(cmd, "login")) {                         /* /login: a challenge; the game answers with a proof */
        uint8_t r[16];
        int i;
        if (!s->cfg.admin_password[0]) { say_to(s, c, "no admin on this server"); return; }
        net_random(r, 16);
        for (i = 0; i < 16; i++) snprintf(c->admin_nonce + i * 2, 3, "%02x", r[i]);
        c->admin_nonce_at = net_ms();
        snprintf(out, sizeof out, "login challenge %s", c->admin_nonce);
        say_to(s, c, out);
        return;
    }
    if (!strncmp(cmd, "login-proof ", 12)) {             /* the proof: sha256 of "challenge:password", in hex */
        uint32_t now = net_ms();
        char want[65], line[160];
        uint8_t h[32];
        int i, ok;
        if (pw_blocked(s, c->addr.ip, now)) { say_to(s, c, "too many tries: wait a minute"); return; }
        ok = s->cfg.admin_password[0] && c->admin_nonce[0] && now - c->admin_nonce_at < 60000;
        if (ok) {
            snprintf(line, sizeof line, "%s:%s", c->admin_nonce, s->cfg.admin_password);
            sha256(line, strlen(line), h);
            for (i = 0; i < 32; i++) snprintf(want + i * 2, 3, "%02x", h[i]);
            ok = strlen(cmd + 12) == 64 && !memcmp(cmd + 12, want, 64);   /* (all of it, every time) */
        }
        c->admin_nonce[0] = 0;                           /* (one answer per challenge) */
        if (ok) {
            c->admin = 1;
            slog(s, LOG_INFO, "%s logged in as admin", c->name);
            say_to(s, c, "you're an admin now: /help");
        } else {
            slog(s, LOG_WARN, "%s: a wrong admin password", c->name);
            pw_failed(s, c->addr.ip, now);
            say_to(s, c, "no");
        }
        return;
    }
    if (!strncmp(cmd, "login ", 6)) {                    /* the password itself, in the clear (old games) */
        uint32_t now = net_ms();
        if (!s->cfg.admin_plain_login) {
            say_to(s, c, "this server takes the admin password only from an up-to-date game");
            return;
        }
        if (pw_blocked(s, c->addr.ip, now)) { say_to(s, c, "too many tries: wait a minute"); return; }
        if (s->cfg.admin_password[0] && !strcmp(cmd + 6, s->cfg.admin_password)) {
            c->admin = 1;
            slog(s, LOG_INFO, "%s logged in as admin", c->name);
            say_to(s, c, "you're an admin now: /help");
        } else {
            slog(s, LOG_WARN, "%s: a wrong admin password", c->name);
            pw_failed(s, c->addr.ip, now);
            say_to(s, c, "no");
        }
        return;
    }
    if (!strcmp(cmd, "rematch")) {                       /* /rematch: the same again (if you played the last one) */
        if (!s->rematch_open || !c->last_team1) { say_to(s, c, "only the last match's players vote on a rematch"); return; }
        c->rematch = 1;
        snprintf(out, sizeof out, "%s wants a rematch", c->name);
        add_chat(s, -1, "", out, 0);
        return;
    }
    if (!strncmp(cmd, "vote", 4)) {
        int m = atoi(cmd + 4);
        if (s->cfg.rotation != ROT_VOTE || s->nmaps < 2) { say_to(s, c, "no map vote on this server"); return; }
        if (m < 1 || m > s->nmaps) { say_to(s, c, "/vote 1 to the number of maps (/maps lists them)"); return; }
        if (!s->ballot[m - 1]) { say_to(s, c, "that map isn't on the ballot this time (/maps)"); return; }
        c->vote = m;
        snprintf(out, sizeof out, "%s votes for %s", c->name, s->cfg.map_names[m - 1]);
        add_chat(s, -1, "", out, 0);
        return;
    }
    if (!strcmp(cmd, "maps") || !strcmp(cmd, "help") || c->admin) {
        if (!strcmp(cmd, "help") && !c->admin) snprintf(out, sizeof out, "/maps, /vote N, /start, /teamname NAME, /slap PLAYER, /login PASSWORD");
        else server_command(s, cmd, out, sizeof out);
        {   /* the reply, in chat-sized pieces */
            const char *p = out;
            while (*p) {
                char piece[NET_CHAT];
                size_t n = strlen(p);
                if (n > NET_CHAT - 1) n = NET_CHAT - 1;
                memcpy(piece, p, n);
                piece[n] = 0;
                say_to(s, c, piece);
                p += n;
            }
        }
        if (c->admin && strcmp(cmd, "help")) slog(s, LOG_INFO, "%s (admin): /%s", c->name, cmd);
        return;
    }
    say_to(s, c, "that's for admins (/login PASSWORD)");
}

void server_reconfigure(Server *s, const ServerConfig *cfg)
{
    int i;
    char *keep[NET_MAPS];
    int nkeep = s->nmaps;
    memcpy(keep, s->maps, sizeof keep);
    snprintf(s->cfg.name, sizeof s->cfg.name, "%s", cfg->name);
    snprintf(s->cfg.motd, sizeof s->cfg.motd, "%s", cfg->motd);
    snprintf(s->cfg.password, sizeof s->cfg.password, "%s", cfg->password);
    snprintf(s->cfg.admin_password, sizeof s->cfg.admin_password, "%s", cfg->admin_password);
    s->cfg.max_players = cfg->max_players < 1 ? 1 : cfg->max_players > NET_LOBBY_MAX ? NET_LOBBY_MAX : cfg->max_players;
    s->cfg.min_players = cfg->min_players < 1 ? 1 : cfg->min_players > MATCH_PLAYERS ? MATCH_PLAYERS : cfg->min_players;
    s->cfg.countdown_ms = cfg->countdown_ms;
    s->cfg.start_after_ms = cfg->start_after_ms;
    s->cfg.manual_start = cfg->manual_start;
    s->cfg.allow_watching = cfg->allow_watching;
    s->cfg.weather_mode = cfg->weather_mode;
    s->cfg.spectator_delay_s = cfg->spectator_delay_s;
    s->cfg.allow_bots = cfg->allow_bots; s->cfg.bots = cfg->bots; s->cfg.fill_bots = cfg->fill_bots;
    s->cfg.bot_level = cfg->bot_level;
    s->cfg.max_per_address = cfg->max_per_address;
    s->cfg.base = cfg->base;
    s->cfg.rotation = cfg->rotation;
    if (s->state != SRV_RUNNING && s->state != SRV_LOADING) {   /* new maps (not in the middle of a match) */
        const char *one = cfg->map;
        s->nmaps = 0;
        for (i = 0; i < cfg->nmaps && i < NET_MAPS; i++) {
            size_t n = strlen(cfg->maps[i]);
            if ((s->maps[s->nmaps] = (char *)malloc(n + 1)) == NULL) break;
            memcpy(s->maps[s->nmaps], cfg->maps[i], n + 1);
            snprintf(s->cfg.map_names[s->nmaps], sizeof s->cfg.map_names[0], "%s", cfg->map_names[i]);
            s->nmaps++;
        }
        if (!s->nmaps && one && (s->maps[0] = (char *)malloc(strlen(one) + 1)) != NULL) {
            memcpy(s->maps[0], one, strlen(one) + 1);
            snprintf(s->cfg.map_names[0], sizeof s->cfg.map_names[0], "%s", cfg->map_name);
            s->nmaps = 1;
        }
        for (i = 0; i < nkeep; i++) free(keep[i]);
        s->next_idx = 0;
        s->last_map_idx = -1;
        s->map = s->nmaps ? s->maps[0] : NULL;
        pick_next_map(s);
    }
    slog(s, LOG_INFO, "settings reloaded");
}

void server_fuzz_packet(Server *s, const uint8_t *buf, int n, uint32_t ip, uint16_t port)
{
    uint8_t copy[NET_MAX_PACKET];
    NetAddr from;
    if (n < 0 || n > NET_MAX_PACKET) return;
    memcpy(copy, buf, (size_t)n);
    memset(&from, 0, sizeof from);
    from.ip = ip;
    from.port = port;
    handle(s, &from, copy, n, net_ms());
}

void pick_next_for_test(Server *s) { pick_next_map(s); }   /* (tests: the vote's outcome, now) */
