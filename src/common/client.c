/* SPDX-License-Identifier: MIT */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/client.h"
#ifndef __amigaos__
#include "common/http.h"
#endif
#include "common/rules.h"
#include "common/sha256.h"

#define NC_GIVE_UP_MS 12000

int netc_open(NetClient *c, const char *host, uint16_t port, const char *name, const char *password,
              const uint8_t key[32], int cls, int steer)
{
    memset(c, 0, sizeof *c);
    if (key) memcpy(c->key, key, 32); else net_random(c->key, 32);
    c->want_cls = (uint8_t)cls;
    c->steer = (uint8_t)steer;
    c->slot = -1;
    c->winner = -1;
    c->delay = NET_DELAY;
    c->mine_hi = NET_DELAY;
    snprintf(c->my_name, sizeof c->my_name, "%s", name && name[0] ? name : "Player");
    snprintf(c->password, sizeof c->password, "%s", password ? password : "");
    if (net_init() != 0 || net_resolve(host, port, &c->server) != 0) {
        c->state = NC_FAILED;
        snprintf(c->status, sizeof c->status, "can't find %s", host);
        return -1;
    }
    if ((c->sock = net_open(0)) < 0) { c->state = NC_FAILED; snprintf(c->status, sizeof c->status, "no network"); return -1; }
    c->socket_open = 1;
    c->state = NC_CONNECTING;
    /* Computing the rules fingerprint can take longer than the connect timeout. */
    net_rules();
    c->heard = net_ms();
    snprintf(c->status, sizeof c->status, "calling the server");
    return 0;
}

void netc_close(NetClient *c)
{
    if (c->socket_open) {
        uint8_t b[8];
        Pack k;
        pk_init(&k, b, sizeof b);
        pk_header(&k, NM_BYE);
        pk_u8(&k, 0);
        net_send(c->sock, &c->server, b, k.n);
        net_close(c->sock);
    }
    free(c->map);
    c->map = NULL;
    c->socket_open = 0;
    c->sock = -1;
}

void netc_diagnostics(const NetClient *c, char *out, size_t cap)
{
    static const char *const states[] = { "CONNECTING", "LOBBY", "MAP DOWNLOAD", "PLAYING", "MATCH ENDED", "FAILED" };
    char address[100];
    uint32_t now = net_ms();
    net_addr_str(&c->server, address, sizeof address);
    snprintf(out, cap,
        "OpenFP %s\nState: %s\nServer: %.48s\nAddress: %s\n"
        "Client protocol: %d / rules %08x\nServer protocol: %d (0: not reported)\n"
        "UDP reply: %s / age %lu ms\nLatency: %d ms / delay %d ticks\n"
        "Map bytes: %lu / %lu\nMap retries: %d / cache: %s\n"
        "Match world: %d / lobby world: %d\nOfficial tick: %lu / played: %lu\n"
        "Catch-up: %d ticks / desync: %lu\nStatus: %.95s\n",
        OFP_VERSION, (unsigned)c->state < sizeof states / sizeof states[0] ? states[c->state] : "UNKNOWN",
        c->server_name, address, NET_PROTOCOL, (unsigned)net_rules(), c->server_protocol,
        c->id > 0 ? "received" : "not confirmed", (unsigned long)(c->heard ? now - c->heard : 0), c->rtt, c->delay,
        (unsigned long)c->map_got, (unsigned long)c->map_len, c->map_fails, c->map_cached ? "yes" : "no",
        c->ms.biome, c->biome, (unsigned long)c->have, (unsigned long)c->used,
        netc_behind(c), (unsigned long)c->desync_tick, c->status);
}

int netc_save_diagnostics(const NetClient *c, const char *path)
{
    char text[1600];
    FILE *f;
    int ok;
    netc_diagnostics(c, text, sizeof text);
    if (!(f = fopen(path, "w"))) return -1;
    ok = fputs(text, f) >= 0;
    if (fclose(f) != 0) ok = 0;
    return ok ? 0 : -1;
}

static void send_pack(NetClient *c, Pack *k) { if (!k->bad && c->socket_open) net_send(c->sock, &c->server, k->p, k->n); }

/* what the server knows us by: a verifier made from our key and the server's id
 * (so each server sees a different one), and the id of that */
static void my_verifier(const NetClient *c, uint8_t v[32], uint8_t id[16])
{
    uint8_t buf[48], h[32];
    memcpy(buf, c->key, 32);
    memcpy(buf + 32, c->server_id, 16);
    sha256(buf, sizeof buf, v);
    sha256(v, 32, h);
    memcpy(id, h, 16);
}

static void send_hello(NetClient *c)
{
    uint8_t b[256], answer[32], v[32], id[16], proof[32], pb[40];
    Pack k;
    int i;
    memset(answer, 0, sizeof answer);
    memset(proof, 0, sizeof proof);
    my_verifier(c, v, id);
    if (c->cookie) {
        if (c->password[0]) sha256_password((uint32_t)c->cookie, c->password, answer);
        for (i = 0; i < 8; i++) pb[i] = (uint8_t)(c->cookie >> (8 * i));
        memcpy(pb + 8, v, 32);
        sha256(pb, sizeof pb, proof);
    }
    pk_init(&k, b, sizeof b);
    pk_header(&k, NM_HELLO);
    pk_u32(&k, net_build_id());
    pk_str(&k, c->my_name, NET_NAME - 1);
    pk_u32(&k, (uint32_t)c->cookie);
    pk_u32(&k, (uint32_t)(c->cookie >> 32));
    pk_bytes(&k, id, 16);
    pk_bytes(&k, proof, 32);
    pk_bytes(&k, answer, 32);
    pk_u8(&k, (uint32_t)c->send_verifier);
    if (c->send_verifier) pk_bytes(&k, v, 32);
    pk_u8(&k, NET_PROTOCOL_OLDEST);                      /* the oldest protocol we still speak */
    pk_u32(&k, net_rules());                             /* what our game computes (0 from older builds: not said) */
    while (k.n < 160) pk_u8(&k, 0);                      /* never smaller than the answers it gets */
    send_pack(c, &k);
}

static void send_ping(NetClient *c)
{
    uint8_t b[48];
    Pack k;
    pk_init(&k, b, sizeof b);
    pk_header(&k, NM_PING);
    pk_u8(&k, (uint32_t)((c->ready ? PING_READY : 0) | (c->watching ? PING_WATCH : 0) | (c->joining ? PING_JOIN : 0)));
    pk_u8(&k, c->want_cls);
    pk_u8(&k, c->steer);
    pk_u32(&k, c->chat_seq);
    pk_u8(&k, (uint32_t)c->side);
    pk_u32(&k, net_ms());                                /* echoed back: the round trip */
    pk_u16(&k, (uint32_t)(c->rtt > 9999 ? 9999 : c->rtt));
    pk_u32(&k, c->slot == 255 ? c->used : c->slot >= 0 ? c->have : 0);
    pk_u8(&k, (uint32_t)c->portrait);
    send_pack(c, &k);
}

void netc_watch(NetClient *c, int on) { c->watching = on ? 1 : 0; if (on) c->joining = 0; send_ping(c); }
void netc_join(NetClient *c, int on) { c->joining = on && !c->eliminated; if (c->joining) c->watching = 0; send_ping(c); }
void netc_side(NetClient *c, int side) { c->side = side >= SIDE_ANY && side <= SIDE_FORTRESS ? side : SIDE_ANY; send_ping(c); }

int netc_lobby_teams(const NetClient *c)
{
    return c->versus && (c->next_extras < 0 || !(c->next_extras & EXTRA_ANNIHILATION));
}

int netc_lobby_roles(const NetClient *c, const LobbyPlayer *p)
{
    int roles = 0;
    if (!p || !c->lobby_social) return 0;
    if (netc_lobby_teams(c) && (p->id == c->team_leader[0] || p->id == c->team_leader[1]))
        roles |= ROLE_LEADER;
    if (p->id == c->lobby_host) roles |= ROLE_HOST;
    if (p->admin || (p->id == c->id && (c->lobby_admin & LOBBY_ADMIN))) roles |= ROLE_ADMIN;
    return roles;
}

const char *netc_lobby_team_name(const NetClient *c, int side)
{
    if (side != SIDE_CAMP && side != SIDE_FORTRESS) return "AUTO";
    if (c->lobby_social && c->lobby_team_name[side - SIDE_CAMP][0])
        return c->lobby_team_name[side - SIDE_CAMP];
    return side == SIDE_CAMP ? "CAMP" : "FORTRESS";
}

int netc_lobby_can_rename(const NetClient *c)
{
    return netc_lobby_can_rename_side(c, c->side);
}

int netc_lobby_can_rename_side(const NetClient *c, int side)
{
    return c->lobby_social && c->state == NC_LOBBY &&
           (c->server_state == SRV_LOBBY || c->server_state == SRV_COUNTDOWN) &&
           netc_lobby_teams(c) && (side == SIDE_CAMP || side == SIDE_FORTRESS) &&
           (c->team_leader[side - SIDE_CAMP] == c->id ||
            ((c->lobby_admin & LOBBY_HOST_RENAME) &&
             (c->lobby_host == c->id || (c->lobby_admin & LOBBY_ADMIN))));
}

void netc_lobby_rename(NetClient *c, int side, const char *name)
{
    char command[NET_TEAM_NAME + 16];
    if (!name[0] || !netc_lobby_can_rename_side(c, side)) return;
    if (c->lobby_admin & LOBBY_HOST_RENAME) snprintf(command, sizeof command, "/teamname #%d %s", side, name);
    else snprintf(command, sizeof command, "/teamname %s", name);
    netc_chat(c, command);
}

int netc_lobby_can_start(const NetClient *c)
{
    return c->state == NC_LOBBY && c->server_state == SRV_LOBBY &&
           c->manual_start && (c->lobby_host == c->id || (c->lobby_admin & LOBBY_ADMIN));
}

int netc_lobby_bots(const NetClient *c)
{
    int open = (c->match_capacity ? c->match_capacity : MATCH_PLAYERS) - c->nlobby;
    return open <= 0 ? 0 : c->bots < open ? c->bots : open;
}

void netc_lobby_roster(const NetClient *c, LobbyRoster *r)
{
    int i, seats[2], bots = netc_lobby_bots(c);
    memset(r, 0, sizeof *r);
    r->columns = netc_lobby_teams(c) ? 3 : 2;
    r->pages = 1;
    for (i = 0; i < c->nlobby; i++) {
        int side = c->lobby[i].side;
        int col = r->columns == 2 ? i % 2 : side == SIDE_CAMP ? 0 : side == SIDE_FORTRESS ? 2 : 1;
        r->entry[col][r->count[col]++] = i;
    }
    seats[0] = r->count[0]; seats[1] = r->count[r->columns - 1];
    if (r->columns == 3)
        for (i = 0; i < r->count[1]; i++) seats[seats[0] > seats[1]]++;
    for (i = 0; i < bots; i++) {
        int side = seats[0] > seats[1], col = side ? r->columns - 1 : 0;
        r->entry[col][r->count[col]++] = c->nlobby + i;
        seats[side]++;
    }
    for (i = 0; i < r->columns; i++) {
        int pages = (r->count[i] + LOBBY_ROWS - 1) / LOBBY_ROWS;
        if (pages > r->pages) r->pages = pages;
    }
}

void netc_lobby_team_step(NetClient *c, int direction)
{
    int side = c->side;
    if (c->state != NC_LOBBY || !netc_lobby_teams(c)) return;
    if (direction < 0 && side != SIDE_CAMP) side = side == SIDE_FORTRESS ? SIDE_ANY : SIDE_CAMP;
    if (direction > 0 && side != SIDE_FORTRESS) side = side == SIDE_CAMP ? SIDE_ANY : SIDE_FORTRESS;
    if (side != c->side) netc_side(c, side);
}

void netc_lobby_tank_step(NetClient *c, int direction)
{
    if (c->state == NC_LOBBY)
        netc_ready(c, c->ready, (c->want_cls + TANK_CLASS_COUNT + direction) % TANK_CLASS_COUNT);
}

int  netc_spectating(const NetClient *c) { return (c->state == NC_LOADING || c->state == NC_RUNNING) && c->slot == 255; }

/* downloaded maps are kept under their content id */
static void cache_path(const NetClient *c, char *out, size_t cap)
{
    snprintf(out, cap, "%s/net-%02x%02x%02x%02x%02x%02x%02x%02x.ofpmap", c->cache_dir, c->map_id[0], c->map_id[1],
             c->map_id[2], c->map_id[3], c->map_id[4], c->map_id[5], c->map_id[6], c->map_id[7]);
}

static void send_out_chat(NetClient *c)
{
    uint8_t b[NET_MAX_PACKET];
    Pack k;
    if (!c->out_text[0]) return;
    pk_init(&k, b, sizeof b);
    pk_header(&k, NM_CHAT);
    pk_u32(&k, c->out_cseq);
    pk_str(&k, c->out_text, NET_CHAT - 1);
    send_pack(c, &k);
}

void netc_ready(NetClient *c, int ready, int cls)
{
    c->ready = ready ? 1 : 0;
    c->want_cls = (uint8_t)(cls % TANK_CLASS_COUNT);
    send_ping(c);
}

void netc_chat(NetClient *c, const char *text)
{
    if (!text[0]) return;
    if (!strncmp(text, "/login ", 7) && text[7]) {       /* the admin password: not sent; a challenge asked for */
        snprintf(c->admin_pw, sizeof c->admin_pw, "%.*s", (int)sizeof c->admin_pw - 1, text + 7);
        text = "/login";
    }
    snprintf(c->out_text, sizeof c->out_text, "%s", text);
    c->out_cseq++;
    send_out_chat(c);
}

void netc_team_order(NetClient *c, int kind)
{
    char line[64];
    if (!c->can_order || c->state != NC_RUNNING || kind < 0 || kind >= ORDER_COUNT) return;
    snprintf(line, sizeof line, "/order %u %d", (unsigned)c->assignment, kind);
    netc_chat(c, line);
}

static int chat_prefix(const char *text, const char *prefix, size_t n)
{
    while (n--) {
        if (!*text || tolower((unsigned char)*text++) != tolower((unsigned char)*prefix++)) return 0;
    }
    return 1;
}

static int handle_char(char ch)
{
    return isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == '+';
}

int netc_chat_mentions(const NetClient *c, const ClientChat *line, int id)
{
    const char *name = id == c->id ? c->my_name : "";
    const char *p;
    size_t n;
    int i;
    if (line->from == id) return 0;
    for (i = 0; i < c->nlobby; i++)
        if (c->lobby[i].id == id) { name = c->lobby[i].name; break; }
    n = strlen(name);
    if (!n) return 0;
    p = line->text + strspn(line->text, " ");
    if (chat_prefix(p, name, n) && p[n] == ':') return 1;
    for (p = line->text; (p = strchr(p, '@')) != NULL; p++) {
        if (p > line->text && (handle_char(p[-1]) || p[-1] == '.')) continue;
        if (chat_prefix(p + 1, name, n) && !handle_char(p[n + 1]) &&
            !(p[n + 1] == '.' && handle_char(p[n + 2]))) return 1;
    }
    return 0;
}

void netc_chat_complete(const NetClient *c, char *text, size_t cap)
{
    char *part = text;
    int slap = !strncmp(text, "/slap ", 6), addressed = 0;
    if (slap) part = text + 6 + strspn(text + 6, " ");
    else if (*text == '/') return;
    else {
        char *at = strrchr(text, '@');
        if (at && (at == text || !handle_char(at[-1]))) { part = at + 1; addressed = 1; }
    }
    for (;;) {
        const char *match = NULL;
        size_t n = strlen(part);
        int i, matches = 0;
        if (!n && !slap && !addressed) return;
        for (i = 0; i < c->nlobby; i++) {
            const LobbyPlayer *p = &c->lobby[i];
            if (p->id == c->id || !p->name[0] || !chat_prefix(p->name, part, n)) continue;
            match = p->name;
            if (!strcmp(part, match)) { matches = 1; break; }
            matches++;
        }
        if (matches == 1) {
            const char *mark = !slap && !addressed && part != text ? "@" : "";
            const char *end = slap ? "" : !addressed && part == text ? ": " : " ";
            size_t offset = (size_t)(part - text);
            if (offset + strlen(mark) + strlen(match) + strlen(end) < cap)
                snprintf(part, cap - offset, "%s%s%s", mark, match, end);
            return;
        }
        if (matches || slap || addressed) return;
        part = strchr(part, ' ');
        if (!part) return;
        part += strspn(part, " ");
    }
}

void netc_leaders(NetClient *c)
{
    uint8_t b[8];
    Pack k;
    pk_init(&k, b, sizeof b);
    pk_header(&k, NM_LEADREQ);
    send_pack(c, &k);
}

void netc_rematch(NetClient *c, int yes)
{
    uint8_t b[8];
    Pack k;
    c->my_rematch = yes ? 1 : 0;
    pk_init(&k, b, sizeof b);
    pk_header(&k, NM_REMATCH);
    pk_u8(&k, (uint32_t)c->my_rematch);
    send_pack(c, &k);
}

void netc_back_to_lobby(NetClient *c)
{
    if (c->state != NC_ENDED) return;
    c->state = NC_LOBBY;
    c->slot = -1;
    c->ready = 0;
    c->watching = c->joining = 0;
    snprintf(c->status, sizeof c->status, "in the lobby");
    send_ping(c);
}

static void send_leave_match(NetClient *c)
{
    uint8_t b[12];
    Pack k;
    pk_init(&k, b, sizeof b);
    pk_header(&k, NM_LEAVEMATCH);
    pk_u32(&k, c->left_assignment);
    send_pack(c, &k);
}

void netc_leave_match(NetClient *c)
{
    if (!c->socket_open || c->state == NC_CONNECTING || c->state == NC_FAILED) return;
    if ((c->state == NC_RUNNING || c->state == NC_LOADING) && c->slot != 255) {
        c->left_assignment = c->assignment;
        c->leaving = c->assignment != 0;
        c->legacy_left_match = !c->leaving;
        if (c->leaving) send_leave_match(c);
    }
    c->state = NC_ENDED;
    netc_back_to_lobby(c);
}

static void handle(NetClient *c, uint8_t *buf, int n)
{
    Pack k;
    int type;
    pk_init(&k, buf, n);
    type = uk_header(&k);
    if (!type) return;
    c->heard = net_ms();
    switch (type) {
    case NM_COOKIE: {                                    /* the server checks we're really here */
        uint64_t lo = uk_u32(&k), hi = uk_u32(&k);
        uk_bytes(&k, c->server_id, 16);
        (void)uk_u8(&k);
        if (k.bad || c->state != NC_CONNECTING) return;
        c->cookie = lo | hi << 32;
        send_hello(c);
        break;
    }
    case NM_NEEDKEY:                                     /* first visit: register our key */
        if (c->state != NC_CONNECTING) return;
        c->send_verifier = 1;
        send_hello(c);
        break;
    case NM_PONG: {                                      /* the round trip, smoothed */
        uint32_t sent = uk_u32(&k), r = net_ms() - sent;
        if (k.bad || r > 10000) return;
        c->rtt = c->rtt ? (c->rtt * 3 + (int)r) / 4 : (int)r;
        break;
    }
    case NM_RESYNC:                                      /* out of step: again from the start */
        if (c->state != NC_RUNNING) return;
        memset(c->tick_id, 0, sizeof c->tick_id);
        c->used = c->have = 0;
        c->restart = 1;
        {
            uint32_t at = uk_u32(&k);                    /* (where it happened) */
            c->desync_tick = k.bad ? 0 : at;
            k.bad = 0;
        }
        snprintf(c->status, sizeof c->status, "out of step: catching up again");
        break;
    case NM_WELCOME:
        c->id = (int)uk_u8(&k);
        uk_str(&k, c->server_name, sizeof c->server_name);
        uk_str(&k, c->motd, sizeof c->motd);
        c->max_players = (int)uk_u8(&k);
        c->min_players = (int)uk_u8(&k);
        c->versus = (int)uk_u8(&k);
        c->biome = (int)uk_u8(&k);
        uk_str(&k, c->map_name, sizeof c->map_name);
        {
            uint32_t seq = uk_u32(&k);
            if (k.bad) return;
            if (c->state == NC_CONNECTING) c->chat_seq = seq;   /* the chat from here on */
        }
        if (c->state == NC_CONNECTING) {
            c->state = NC_LOBBY;
            snprintf(c->status, sizeof c->status, "in the lobby");
            netc_leaders(c);
        }
        break;
    case NM_LOBBY: {
        int i, cnt;
        c->server_state = (int)uk_u8(&k);
        c->countdown = (int)uk_u8(&k);
        cnt = (int)uk_u8(&k);
        if (cnt > NET_LOBBY_MAX) cnt = NET_LOBBY_MAX;
        for (i = 0; i < cnt; i++) {
            LobbyPlayer *p = &c->lobby[i];
            p->id = (int)uk_u8(&k);
            uk_str(&k, p->name, sizeof p->name);
            p->ready = (int)uk_u8(&k);
            p->cls = (int)uk_u8(&k);
            p->playing = (int)uk_u8(&k);
            p->side = (int)uk_u8(&k);
            p->rtt = (int)uk_u16(&k);
            p->portrait = 0;
            p->admin = 0;
        }
        if (k.bad) return;
        c->nlobby = cnt;
        c->bots = (int)uk_u8(&k);
        c->bot_level = (int)uk_u8(&k);
        uk_str(&k, c->next_map, sizeof c->next_map);
        c->rotation = (int)uk_u8(&k);
        c->nmaps = (int)uk_u8(&k);
        c->start_wait = (int)uk_u8(&k);
        c->places_free = (int)uk_u8(&k);
        c->time_limit = (int)uk_u16(&k);
        c->time_rule = (int)uk_u8(&k);
        for (i = 0; i < c->nmaps && i < NET_MAPS; i++) c->votes[i] = (int)uk_u8(&k);
        c->nballot = 0;
        while (!k.bad) {               /* the ballot, if the server sends it */
            int bi = (int)uk_u8(&k);
            if (k.bad || bi == 0) break;
            if (c->nballot >= 3) { k.bad = 1; break; }
            c->ballot[c->nballot] = bi;
            uk_str(&k, c->ballot_name[c->nballot], sizeof c->ballot_name[c->nballot]);
            if (!k.bad) c->nballot++;
        }
        {   /* a rematch's tally (newer servers: after the ballot's end) */
            Pack k2 = k;
            int y = (int)uk_u8(&k2), o = (int)uk_u8(&k2);
            c->rematch_yes = k2.bad ? 0 : y;
            c->rematch_of = k2.bad ? 0 : o;
            c->next_extras = (int)uk_u8(&k2);
            if (k2.bad) c->next_extras = -1;
            c->match_capacity = (int)uk_u8(&k2);
            if (k2.bad || c->match_capacity > MATCH_PLAYERS) c->match_capacity = 0;
            c->lobby_social = 0;
            c->lobby_host = c->manual_start = 0;
            c->lobby_admin = 0;
            c->eliminated = 0;
            if (!k2.bad && k2.n < k2.cap) {
                for (i = 0; i < 2; i++) {
                    c->team_leader[i] = (int)uk_u8(&k2);
                    uk_str(&k2, c->lobby_team_name[i], NET_TEAM_NAME);
                }
                c->lobby_social = !k2.bad;
                if (!k2.bad && k2.cap - k2.n >= 2) {
                    c->lobby_host = (int)uk_u8(&k2);
                    c->manual_start = uk_u8(&k2) == 1;
                    if (k2.cap - k2.n >= cnt) {
                        for (i = 0; i < cnt; i++) {
                            int portrait = (int)uk_u8(&k2);
                            c->lobby[i].portrait = portrait <= NET_AVATARS ? portrait : 0;
                        }
                        if (k2.n < k2.cap) c->eliminated = uk_u8(&k2) == 1;
                        if (k2.n < k2.cap) c->lobby_admin = (int)uk_u8(&k2);
                        if (k2.cap - k2.n >= 2) {
                            unsigned admins = uk_u16(&k2);
                            for (i = 0; i < cnt; i++) c->lobby[i].admin = (admins >> i) & 1u;
                        }
                    }
                }
            }
        }
        if (c->eliminated) c->joining = 0;
        if (c->server_state == SRV_LOBBY || c->server_state == SRV_COUNTDOWN) c->legacy_left_match = 0;
        break;
    }
    case NM_TEAMORDER: {
        uint32_t assignment = uk_u32(&k);
        int allowed = uk_u8(&k);
        TeamOrder order = {0};
        order.kind = uk_u8(&k); order.issuer = uk_u8(&k);
        order.tx = (int16_t)uk_u16(&k); order.ty = (int16_t)uk_u16(&k); order.until = uk_u32(&k);
        if (!k.bad && c->state == NC_RUNNING && assignment == c->assignment && order.kind < ORDER_COUNT &&
            order.issuer < MAX_TANKS && order.tx >= 0 && order.tx < MAP_W && order.ty >= 0 && order.ty < MAP_H) {
            c->can_order = !!allowed; c->team_order = order;
        }
        break;
    }
    case NM_CHAT: {
        uint32_t seq = uk_u32(&k), from = uk_u8(&k), cseq = uk_u32(&k);
        ClientChat incoming = {0}, *l;
        uint32_t done = 0;
        int skip;
        if (k.bad || seq <= c->chat_seq) return;
        incoming.seq = seq;
        incoming.from = from == 255 ? -1 : (int)from;
        uk_str(&k, incoming.name, sizeof incoming.name);
        uk_str(&k, incoming.text, sizeof incoming.text);
        if (k.bad) return;
        skip = k.cap - k.n >= 4;                            /* first retained line after the server discarded old chat */
        if (skip) done = uk_u32(&k);
        if (seq != c->chat_seq + 1 && !skip) return;         /* wait for a missing line unless it was discarded */
        l = &c->chat[seq % 40];
        *l = incoming;
        c->chat_seq = seq;
        if ((l->from == c->id && cseq == c->out_cseq) || (skip && done >= c->out_cseq))
            c->out_text[0] = 0;                             /* received, even if its echo has already expired */
        if (l->from < 0 && c->admin_pw[0] && !strncmp(l->text, "login challenge ", 16)) {   /* /login: the answer */
            char line[160], proof[80];
            uint8_t h[32];
            int i;
            snprintf(line, sizeof line, "%.32s:%s", l->text + 16, c->admin_pw);
            sha256(line, strlen(line), h);
            strcpy(proof, "/login-proof ");
            for (i = 0; i < 32; i++) snprintf(proof + 13 + i * 2, 3, "%02x", h[i]);
            memset(c->admin_pw, 0, sizeof c->admin_pw);
            netc_chat(c, proof);
        }
        break;
    }
    case NM_LEADERS: {
        int i, cnt = (int)uk_u8(&k);
        if (cnt > NET_LEADERS) cnt = NET_LEADERS;
        for (i = 0; i < cnt; i++) {
            LeaderEntry *e = &c->leaders[i];
            uk_str(&k, e->name, sizeof e->name);
            e->matches = (int)uk_u16(&k); e->wins = (int)uk_u16(&k); e->kills = (int)uk_u16(&k);
            e->score = (long)uk_u32(&k);
            e->rating = (int)uk_u16(&k);
        }
        if (!k.bad) { c->nleaders = cnt; c->got_leaders = 1; }
        break;
    }
    case NM_MATCH: {
        c->my_rematch = 0;                               /* (a new match: a new vote, after it) */
        uint32_t slot = uk_u8(&k), len, hash, at, delay, assignment;
        MatchSetup ms;
        uint8_t id[8];
        char mname[32];
        uk_setup(&k, &ms);
        len = uk_u32(&k); hash = uk_u32(&k);
        at = uk_u32(&k);                                 /* where the match is now (rejoining, watching) */
        delay = uk_u8(&k);
        uk_bytes(&k, id, 8);
        uk_str(&k, mname, sizeof mname);
        if (k.bad) return;
        {   /* the weather, from builds that send it */
            uint32_t w = uk_u8(&k), x2;
            ms.weather = k.bad || w >= WEATHER_COUNT ? WEATHER_CLEAR : (uint8_t)w;
            k.bad = 0;
            x2 = uk_u8(&k);                              /* the extras, from builds that send them */
            ms.extras = k.bad ? 0 : (uint8_t)(x2 & EXTRA_ALL);   /* (every mode: conquest, annihilation too) */
            k.bad = 0;
        }
        assignment = k.cap - k.n >= 4 ? uk_u32(&k) : 0;
        if (c->state != NC_LOBBY && c->state != NC_ENDED) return;
        if ((assignment && assignment == c->left_assignment) || (!assignment && c->legacy_left_match)) return;
        if (!ms.bot_commands || (slot == 255 && !c->watching)) return;
        if (len > NET_MAP_MAX || (slot != 255 && slot >= MATCH_PLAYERS)) return;   /* (nonsense from a server: ignored) */
        c->can_order = 0; memset(&c->team_order, 0, sizeof c->team_order);
        if (slot != 255) c->joining = 0;
        c->catch_to = at;
        c->delay = delay >= NET_DELAY_MIN && delay <= NET_DELAY_MAX ? (int)delay : NET_DELAY;
        memcpy(c->map_id, id, 8);
        snprintf(c->next_map, sizeof c->next_map, "%s", mname);
        c->slot = (int)slot;
        c->assignment = assignment;
        c->ms = ms; c->map_len = len; c->map_hash = hash; c->map_got = 0; c->map_fails = 0;
        free(c->map);
        c->map = len ? (char *)calloc(1, len + 1) : NULL;
        memset(c->tick_id, 0, sizeof c->tick_id);
        c->mine_hi = (uint32_t)c->delay; c->have = c->used = 0; c->winner = -1;
        c->map_cached = 0;
        if (len && c->map && c->cache_dir[0]) {          /* had it before? then no download */
            char path[700];
            FILE *f;
            cache_path(c, path, sizeof path);
            if ((f = fopen(path, "rb")) != NULL) {
                int whole = fread(c->map, 1, len, f) == len && fgetc(f) == EOF;
                fclose(f);
                if (whole && match_hash(&c->ms, c->map) == c->map_hash) { c->map_got = len; c->map_cached = 1; }
                else { memset(c->map, 0, len + 1); remove(path); }   /* (not this match's map: fetched again) */
            }
        }
        c->ready = 0;                                    /* after this match: not ready for the next one yet */
        c->state = NC_LOADING;
        snprintf(c->status, sizeof c->status, "loading the match");
        break;
    }
    case NM_MAPDATA: {
        uint32_t off = uk_u32(&k), len = uk_u16(&k);
        if (k.bad || !c->map || off != c->map_got || off + len > c->map_len) return;
        uk_bytes(&k, c->map + off, (int)len);
        if (!k.bad) c->map_got += len;
        if (!k.bad && c->map_got == c->map_len) {
            if (memchr(c->map, 0, c->map_len) || match_hash(&c->ms, c->map) != c->map_hash) {   /* not what the server announced */
                if (++c->map_fails >= 3) {
                    c->state = NC_FAILED;
                    snprintf(c->status, sizeof c->status, "the server's map doesn't match what it announced");
                    break;
                }
                memset(c->map, 0, c->map_len + 1);
                c->map_got = 0;                          /* (again from the start) */
                break;
            }
            if (c->cache_dir[0]) {                       /* all here: keep it for next time (whole or not at all) */
                char path[700], tmp[710];
                FILE *f;
                cache_path(c, path, sizeof path);
                snprintf(tmp, sizeof tmp, "%s.part", path);
                if ((f = fopen(tmp, "wb")) != NULL) {
                    int ok = fwrite(c->map, 1, c->map_len, f) == c->map_len;
                    if (fclose(f) != 0) ok = 0;
                    remove(path);                        /* (rename doesn't replace on Windows) */
                    if (!ok || rename(tmp, path) != 0) remove(tmp);
                }
            }
        }
        break;
    }
    case NM_TICKS:
    case NM_HISTORY: {
        uint32_t first = uk_u32(&k), cnt = uk_u8(&k), tanks = uk_u8(&k), i;
        if (k.bad || tanks > MAX_TANKS || (c->state != NC_LOADING && c->state != NC_RUNNING)) return;
        if (type == NM_TICKS) c->live_first = first;
        if (c->state == NC_LOADING) {
            if (c->map_len && c->map_got < c->map_len) return;   /* (the map first) */
            c->state = NC_RUNNING;
            snprintf(c->status, sizeof c->status, "playing");
        }
        c->tanks = (int)tanks;
        for (i = 0; i < cnt; i++) {
            uint32_t t = first + i;
            uint8_t row[MAX_TANKS];
            uk_bytes(&k, row, (int)tanks);
            if (k.bad) break;
            if (t < c->used || t >= c->used + 240) continue;
            memcpy(c->ticks[t & 255], row, tanks);
            c->tick_id[t & 255] = t + 1;
        }
        if (c->have < c->used) c->have = c->used;
        while (c->tick_id[c->have & 255] == c->have + 1) c->have++;
        break;
    }
    case NM_LEAVEMATCH: {
        uint32_t assignment = uk_u32(&k);
        if (!k.bad && c->leaving && assignment == c->left_assignment) c->leaving = 0;
        break;
    }
    case NM_END: {
        uint32_t w = uk_u8(&k);
        if (c->state == NC_RUNNING || c->state == NC_LOADING) {
            c->winner = w == 255 ? -1 : (int)w;
            c->state = NC_ENDED;
            snprintf(c->status, sizeof c->status, "match over");
        }
        break;
    }
    case NM_BYE: {
        int r = (int)uk_u8(&k);
        c->reason = r;
        if (r == NR_DESYNC && (c->state == NC_RUNNING || c->state == NC_LOADING)) {   /* back to the lobby */
            c->state = NC_ENDED;
            c->winner = -1;
            snprintf(c->status, sizeof c->status, "out of step with the server: match stopped");
            break;
        }
        c->state = NC_FAILED;
        c->server_protocol = (int)uk_u8(&k);             /* (protocol 8 on: who the server is) */
        c->server_oldest = (int)uk_u8(&k);
        uk_str(&k, c->server_version, sizeof c->server_version);
        if (k.bad) { c->server_protocol = c->server_oldest = 0; c->server_version[0] = 0; }
        if (r == NR_FULL) {
            int place = (int)uk_u8(&k);                  /* (newer servers: our place in their queue) */
            c->queue_place = k.bad ? 0 : place;
            if (c->wait_for_place && c->queue_place > 0) {   /* waiting: asked again in a while */
                c->state = NC_CONNECTING;
                c->heard = net_ms();
                c->wait_until = net_ms() + 5000;         /* (the next HELLO in about 5 s) */
                snprintf(c->status, sizeof c->status, "the server is full: you're number %d in the queue", c->queue_place);
                break;
            }
        }
        if (r == NR_VERSION) {
            if (c->server_protocol && c->server_oldest > NET_PROTOCOL)
                snprintf(c->status, sizeof c->status, "update at openfp.retrocs.org/play/ (server %s%s)",
                         c->server_version[0] ? "v" : "", c->server_version[0] ? c->server_version : "is newer");
            else if (c->server_protocol && c->server_protocol < NET_PROTOCOL_OLDEST)
                snprintf(c->status, sizeof c->status, "this server runs an older OpenFP%s%s: it needs updating",
                         c->server_version[0] ? " " : "", c->server_version);
            else snprintf(c->status, sizeof c->status, "this server runs a different version of the game");
            break;
        }
        if (r == NR_RULES) {                             /* the same protocol, other rules: someone's build is off */
            snprintf(c->status, sizeof c->status, "this server's game plays by different rules%s%s: update both",
                     c->server_version[0] ? " (it runs " : "", c->server_version[0] ? c->server_version : "");
            if (c->server_version[0]) strncat(c->status, ")", sizeof c->status - strlen(c->status) - 1);
            break;
        }
        snprintf(c->status, sizeof c->status, "%s", r == NR_FULL ? "the server is full"
                 : r == NR_TOO_MANY ? "too many players from your address" : r == NR_KEY ? "that name or key belongs to someone else"
                 : r == NR_VERSION ? "a different version of the game" : r == NR_PASSWORD ? "wrong password"
                 : r == NR_KICKED ? "you were removed from the server" : "the server closed");
        break;
    }
    default:
        break;
    }
}

int netc_step(NetClient *c)
{
    uint8_t buf[NET_MAX_PACKET];
    NetAddr from;
    uint32_t now;
    int n;
    if (c->state == NC_FAILED) return c->state;
    while ((n = net_recv(c->sock, &from, buf, sizeof buf)) > 0)
        if (net_same(&from, &c->server)) handle(c, buf, n);
    now = net_ms();
    if ((int32_t)(now - c->heard) > NC_GIVE_UP_MS) {
        snprintf(c->status, sizeof c->status, c->state == NC_CONNECTING ? "no answer from the server" : "lost the server");
        c->state = NC_FAILED;
        return c->state;
    }
    if (now - c->last_send >= (c->state == NC_RUNNING ? 100u : 150u) &&
        (!c->wait_until || (int32_t)(now - c->wait_until) >= 0)) {   /* (a full server's queue: not before then) */
        uint8_t b[64];
        Pack k;
        c->wait_until = 0;
        c->last_send = now;
        if (c->state == NC_CONNECTING) send_hello(c);
        else {
            if (now - c->last_ping >= 300) { c->last_ping = now; send_ping(c); }
            if (c->leaving) send_leave_match(c);
            send_out_chat(c);
            if (c->state == NC_LOADING) {
                pk_init(&k, b, sizeof b);
                if (c->map_len && c->map_got < c->map_len) { pk_header(&k, NM_MAPREQ); pk_u32(&k, c->map_got); }
                else { pk_header(&k, NM_LOADED); pk_u32(&k, match_hash(&c->ms, c->map_len ? c->map : NULL)); }
                send_pack(c, &k);
            } else if (c->state == NC_RUNNING && c->mine_hi > (uint32_t)c->delay && c->slot != 255) {
                netc_input(c, c->mine_hi - 1, c->mine[(c->mine_hi - 1) & 255]);
            }
        }
    }
    if (c->state == NC_RUNNING && netc_behind(c) > 0 && c->have < c->used + 200 && now - c->last_hist >= 20) {
        uint8_t b[16];                                   /* behind the live ticks: the history from where we are */
        Pack k;
        c->last_hist = now;
        pk_init(&k, b, sizeof b);
        pk_header(&k, NM_HISTREQ);
        pk_u32(&k, c->have);
        send_pack(c, &k);
    }
    if (c->state == NC_LOADING && c->map_len && c->map_got < c->map_len) {   /* hurry the map along */
        uint8_t b[16];
        Pack k;
        pk_init(&k, b, sizeof b);
        pk_header(&k, NM_MAPREQ);
        pk_u32(&k, c->map_got);
        send_pack(c, &k);
    }
    return c->state;
}

int netc_tick(NetClient *c, uint32_t t, InputBits out[MAX_TANKS])
{
    if (c->tick_id[t & 255] != t + 1) return 0;
    if (t + 1 > c->used) c->used = t + 1;
    memset(out, 0, MAX_TANKS);
    memcpy(out, c->ticks[t & 255], (size_t)c->tanks);
    return 1;
}

void netc_input(NetClient *c, uint32_t t, InputBits b)
{
    uint8_t buf[64];
    Pack k;
    uint32_t first, i;
    c->mine[t & 255] = b;
    if (t + 1 > c->mine_hi) c->mine_hi = t + 1;
    first = c->mine_hi > (uint32_t)c->delay + NET_REDUNDANT ? c->mine_hi - NET_REDUNDANT : (uint32_t)c->delay;
    pk_init(&k, buf, sizeof buf);
    pk_header(&k, NM_INPUT);
    pk_u32(&k, c->have);
    pk_u32(&k, first);
    pk_u8(&k, c->mine_hi - first);
    for (i = first; i < c->mine_hi; i++) pk_u8(&k, c->mine[i & 255]);
    send_pack(c, &k);
}

void netc_check(NetClient *c, uint32_t t, uint32_t sum)
{
    uint8_t buf[16];
    Pack k;
    pk_init(&k, buf, sizeof buf);
    pk_header(&k, NM_CHECK);
    pk_u32(&k, t);
    pk_u32(&k, sum);
    send_pack(c, &k);
}

int netc_take_restart(NetClient *c)
{
    if (!c->restart) return 0;
    c->restart = 0;
    return 1;
}

int netc_waiting(const NetClient *c, uint32_t t)
{
    int n = 0;
    while (n < 200 && c->tick_id[(t + (uint32_t)n) & 255] == t + (uint32_t)n + 1) n++;
    return n;
}

int netc_behind(const NetClient *c)
{
    uint32_t live = c->live_first > c->catch_to ? c->live_first : c->catch_to;
    return live > c->used ? (int)(live - c->used) : 0;
}

int netc_can_retry(const NetClient *c)
{
    return c->state == NC_FAILED && c->reason != NR_PASSWORD && c->reason != NR_KICKED && c->reason != NR_KEY &&
           c->reason != NR_VERSION && c->reason != NR_FULL && c->reason != NR_TOO_MANY && c->reason != NR_DESYNC &&
           c->reason != NR_RULES;
}

void netc_fuzz_packet(NetClient *c, const uint8_t *buf, int n)
{
    uint8_t copy[NET_MAX_PACKET];
    if (n < 0 || n > NET_MAX_PACKET) return;
    memcpy(copy, buf, (size_t)n);
    handle(c, copy, n);
}

#ifndef __amigaos__                                     /* (the Amiga: no listing over HTTP here, no punching) */
int netc_punch(NetClient *const clients[], int count, const char *listing_url, const char *code,
               const char *stun_server, int wait_ms)
{
    char h[128], body[160] = "", resp[128], ports[6 * 4], first[6];
    uint8_t tx[4][12], buf[NET_MAX_PACKET];
    NetAddr stun, from, pub;
    uint16_t port[4] = {0};
    int pt = 3478, i, left = count, length = 0;
    uint32_t start, asked;

    if (count < 1 || count > 4 || !listing_url[0] || !code[0] || !stun_server[0]) return -1;
    net_split(stun_server, h, sizeof h, &pt);
    if (net_resolve(h, (uint16_t)pt, &stun) != 0) return -1;
    for (start = net_ms(), asked = start - 300; left && net_ms() - start < (uint32_t)wait_ms;) {
        uint32_t now = net_ms();
        int retry = now - asked >= 300;
        for (i = 0; i < count; i++) {
            NetClient *c = clients[i];
            int n;
            if (port[i]) continue;
            if (retry) net_stun_ask(c->sock, &stun, tx[i]);
            while ((n = net_recv(c->sock, &from, buf, sizeof buf)) > 0) {
                if (net_same(&from, &stun) && net_stun_answer(buf, n, tx[i], &pub) && pub.port) {
                    port[i] = pub.port;
                    left--;
                    break;
                }
                if (net_same(&from, &c->server)) handle(c, buf, n);
            }
        }
        if (retry) asked = now;
        if (left) net_sleep(5);
    }
    if (left == count) return -1;
    for (i = 0; i < count; i++) {
        if (!port[i]) continue;
        if (!length) snprintf(first, sizeof first, "%u", (unsigned)port[i]);
        length += snprintf(ports + length, sizeof ports - (size_t)length,
                           "%s%u", length ? "," : "", (unsigned)port[i]);
    }
    http_form(body, sizeof body, "action", "punch");
    http_form(body, sizeof body, "code", code);
    http_form(body, sizeof body, "port", first);       /* the first connection also works with an older listing */
    if (count - left > 1) http_form(body, sizeof body, "ports", ports);
    return http_request(listing_url, body, resp, sizeof resp, 4000) == 200 ? 0 : -1;
}
#endif
