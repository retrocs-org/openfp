/* SPDX-License-Identifier: MIT
 * Replays and network play, tested without a screen:
 *  - a match between computer tanks, recorded and played back to the same checksums
 *  - a server and two clients on this machine (real UDP on 127.0.0.1): both
 *    clients' games stay identical to the server's, and its replay verifies */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/replay.h"
#include "common/server.h"
#include "common/rules.h"
#include "common/client.h"
#include "common/sha256.h"
#ifdef _WIN32
#include <direct.h>
#define mkdir(d, m) _mkdir(d)
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

static int failures;

/* a port nobody has just now (asked of the system: the tests run beside others, each needs its own) */
static uint16_t free_port(void)
{
    int s = net_open(0);
    uint16_t p = s >= 0 ? net_port(s) : 0;
    if (s >= 0) net_close(s);
    return p ? p : (uint16_t)(30000 + net_ms() % 20000);
}
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static Game a, b, s2, c1, c2;
static Server srv;
static NetClient n1, n2;

static void test_replay(int relaxed_pace)
{
    MatchSetup ms;
    Replay r, l;
    long t;
    memset(&ms, 0, sizeof ms);
    ms.seed = 77; ms.players = 1; ms.camp_garages = 4; ms.ai_level = AI_HARD;
    ms.cls[0] = relaxed_pace ? TANK_HEAVY : TANK_LIGHT;
    ms.relaxed_pace = (uint8_t)relaxed_pace;
    ms.rocket_tanks = (uint8_t)relaxed_pace;
    CHECK(match_start(&a, &ms, NULL) == 0);
    replay_begin(&r, &ms, NULL, a.num_tanks);
    for (t = 0; t < 4000 && a.winner < 0; t++) {
        InputBits in[MAX_TANKS] = { 0 };
        in[0] = (InputBits)((t / 60) % 4 == 0 ? IN_UP | IN_LEFT : (t / 60) % 4 == 1 ? IN_UP : (t / 30) % 2 ? IN_FIRE : 0);
        game_tick(&a, in);
        replay_tick(&r, in, &a);
    }
    CHECK(replay_save(&r, "test-replay.ofprep") == 0);
    CHECK(replay_load(&l, "test-replay.ofprep") == 0);
    CHECK(l.ticks == r.ticks && l.nchecks == r.nchecks && l.tanks == r.tanks);
    CHECK(l.setup.relaxed_pace == relaxed_pace);
    CHECK(l.setup.rocket_tanks == ms.rocket_tanks);
    CHECK(replay_verify(&l, &b) == -1);                 /* every checksum matches */
    CHECK(game_checksum(&b) == game_checksum(&a));
    l.inputs[100 * (uint32_t)l.tanks] ^= IN_UP | IN_LEFT;   /* tamper with one tick: caught */
    CHECK(replay_verify(&l, &b) >= 0);
    printf("  replay: %u ticks, %u checksums, the file plays back the same\n", (unsigned)r.ticks, (unsigned)r.nchecks);
    replay_free(&r);
    replay_free(&l);
    {
        static const char *const malformed[] = { "1 zz\nend\n", "1 00\n", "4294967295 00\nend\n" };
        size_t i;
        for (i = 0; i < sizeof malformed / sizeof malformed[0]; i++) {
            FILE *f = fopen("test-replay.ofprep", "w");
            CHECK(f != NULL);
            if (!f) break;
            fputs("OFPREPLAY 1\nseed=1 biome=0 time=0 garages=4 versus=1 ai=0 players=1 tanks=1\nticks\n", f);
            fputs(malformed[i], f);
            fclose(f);
            CHECK(replay_load(&l, "test-replay.ofprep") == -1);
            CHECK(!l.map && !l.inputs && !l.checks);
            replay_free(&l);
        }
    }
    remove("test-replay.ofprep");
}

static void test_server_bot_commands(void)
{
    MatchSetup ms = {0};
    Replay r, loaded;
    int t, bot_moved = 0;
    ms.seed = 77; ms.players = 2; ms.versus = 1;
    ms.map_size = SIZE_SMALL + 1; ms.bot_commands = 1;
    ms.cls[0] = ms.cls[1] = TANK_MEDIUM;
    ms.team[1] = TEAM_ENEMY;
    ms.bot[1] = AI_HARD | MATCH_BOT_RAIDER;
    CHECK(match_start(&a, &ms, NULL) == 0 && match_start(&b, &ms, NULL) == 0);
    replay_begin(&r, &ms, NULL, a.num_tanks);
    for (t = 0; t < 750; t++) {
        InputBits in[MAX_TANKS] = {0};
        in[0] = IN_UP | IN_FIRE;
        if (t == 500) in[1] = IN_TAKEOVER | IN_UP;
        if (t > 500) in[1] = IN_UP | IN_FIRE;
        game_tick_server(&a, in);
        bot_moved |= t < 500 && in[1] != 0;
        game_tick(&b, in);
        CHECK(game_checksum(&a) == game_checksum(&b));
        replay_tick(&r, in, &a);
    }
    CHECK(bot_moved && a.bot_rng != b.bot_rng && b.ai[1].repath == 0);
    CHECK(!a.tanks[1].ai && !b.tanks[1].ai);
    CHECK(replay_save(&r, "test-official-bots.ofprep") == 0);
    CHECK(replay_load(&loaded, "test-official-bots.ofprep") == 0 && loaded.setup.bot_commands);
    CHECK(replay_verify(&loaded, &b) == -1 && game_checksum(&a) == game_checksum(&b));
    replay_free(&r);
    replay_free(&loaded);
    remove("test-official-bots.ofprep");
    puts("  official bot commands: client stays in step without AI, including replay and takeover");
}

/* a client's side of a match: play every official tick, press some buttons */
static void play(NetClient *c, Game *g, uint32_t *next, int *started, uint32_t seedish)
{
    InputBits set[MAX_TANKS];
    if (c->state == NC_RUNNING && (!*started || netc_take_restart(c))) {   /* (again from 0 after a resync) */
        CHECK(match_start(g, &c->ms, c->map_len ? c->map : NULL) == 0);
        *started = 1;
        *next = 0;
    }
    if (!*started || c->state != NC_RUNNING) return;
    while (c->slot != 255 && c->mine_hi <= *next + (uint32_t)c->delay) {   /* my buttons, a little ahead (not when watching) */
        uint32_t t = c->mine_hi;
        netc_input(c, t, (InputBits)(((t / 40 + seedish) % 3 == 0) ? IN_UP | IN_LEFT : (t / 25) % 2 ? IN_FIRE : 0));
    }
    while (netc_tick(c, *next, set)) {
        game_tick(g, set);
        if ((*next + 1) % NET_CHECK_EVERY == 0) netc_check(c, *next, game_checksum(g));
        (*next)++;
    }
}

static int hook_matches, hook_winner = 99, hook_chats;
static void h_chat(void *ctx, const char *name, const char *text) { (void)ctx; (void)name; (void)text; hook_chats++; }
static void h_match(void *ctx, const Server *s, MatchResult *r, const Replay *rep)
{
    (void)ctx; (void)s; (void)rep;
    hook_matches++;
    hook_winner = r->winner;
}
static int h_leaders(void *ctx, LeaderEntry *out, int max)
{
    (void)ctx;
    if (max < 1) return 0;
    snprintf(out[0].name, sizeof out[0].name, "Ace");
    out[0].matches = 12; out[0].wins = 9; out[0].kills = 40; out[0].score = 123456; out[0].rating = 1712;
    return 1;
}

static void pump(int ms)
{
    uint32_t start = net_ms();
    while ((int)(net_ms() - start) < ms) { server_step(&srv); netc_step(&n1); netc_step(&n2); net_sleep(1); }
}

static NetClient n3, n2b;
static uint8_t key_a[32], key_b[32];

static void test_network(void)
{
    ServerConfig cfg;
    ServerHooks hooks;
    uint16_t port = free_port();
    uint32_t t1 = 0, t2 = 0, start;
    int s1 = 0, st2 = 0, i, got1 = 0, got2 = 0;
    server_default_config(&cfg);
    snprintf(cfg.name, sizeof cfg.name, "Test server");
    snprintf(cfg.motd, sizeof cfg.motd, "be nice");
    snprintf(cfg.password, sizeof cfg.password, "secret");
    cfg.max_players = 4; cfg.min_players = 2; cfg.countdown_ms = 300;
    cfg.base.versus = 1; cfg.base.seed = 4242;
    cfg.bots = 2; cfg.bot_level = AI_HARD;              /* two computer players join the two people */
    memset(&hooks, 0, sizeof hooks);
    hooks.chat = h_chat; hooks.match_over = h_match; hooks.leaders = h_leaders;
    CHECK(server_open(&srv, port, &cfg, &hooks) == 0);

    /* anyone can ask what's there (the listing does) */
    {
        ServerInfo info;
        int ping = -1;
        uint32_t q0 = net_ms();
        /* the server must step while we wait: ask from here by hand */
        NetAddr to;
        uint8_t buf[NET_MAX_PACKET];
        Pack k;
        int sock = net_open(0), n, ok = 0;
        net_resolve("127.0.0.1", port, &to);
        pk_init(&k, buf, sizeof buf); pk_header(&k, NM_QUERY); net_send(sock, &to, buf, k.n);   /* 4 bytes: ignored */
        while (net_ms() - q0 < 300) {
            NetAddr from;
            server_step(&srv);
            while ((n = net_recv(sock, &from, buf, sizeof buf)) > 0) ok = -1;
            net_sleep(1);
        }
        CHECK(ok == 0);                                  /* a short question gets no answer (no amplification) */
        memset(buf, 0, sizeof buf);
        pk_init(&k, buf, sizeof buf); pk_header(&k, NM_QUERY); net_send(sock, &to, buf, NET_QUERY_MIN);
        q0 = net_ms();
        while (net_ms() - q0 < 1000 && !ok) {
            NetAddr from;
            server_step(&srv);
            while ((n = net_recv(sock, &from, buf, sizeof buf)) > 0) {
                pk_init(&k, buf, n);
                if (uk_header(&k) == NM_INFO) { uk_info(&k, &info); ok = !k.bad; ping = (int)(net_ms() - q0); }
            }
            net_sleep(1);
        }
        net_close(sock);
        CHECK(ok && !strcmp(info.name, "Test server") && info.password == 1 && info.max_players == 4 && info.players == 0);
        (void)ping;
    }

    memset(key_a, 0xA1, sizeof key_a);
    memset(key_b, 0xB2, sizeof key_b);
    CHECK(netc_open(&n1, "127.0.0.1", port, "Alice", "secret", key_a, TANK_HEAVY, STEER_TANK) == 0);
    CHECK(netc_open(&n2, "127.0.0.1", port, "Bob", "secret", key_b, TANK_LIGHT, STEER_TANK) == 0);
    CHECK(netc_open(&n3, "127.0.0.1", port, "Mallory", "guess", NULL, TANK_LIGHT, STEER_TANK) == 0);
    start = net_ms();
    while (net_ms() - start < 3000 && (n1.state != NC_LOBBY || n2.state != NC_LOBBY || n3.state != NC_FAILED)) {
        server_step(&srv); netc_step(&n1); netc_step(&n2); netc_step(&n3); net_sleep(1);
    }
    CHECK(n1.state == NC_LOBBY && n2.state == NC_LOBBY);
    CHECK(n3.state == NC_FAILED && n3.reason == NR_PASSWORD);   /* the wrong password stays out */
    netc_close(&n3);
    CHECK(!strcmp(n1.server_name, "Test server") && !strcmp(n2.motd, "be nice"));
    /* a name belongs to the key that used it first: someone else calling themselves Alice gets Alice2 */
    CHECK(netc_open(&n3, "127.0.0.1", port, "Alice", "secret", NULL, TANK_LIGHT, STEER_TANK) == 0);
    start = net_ms();
    while (net_ms() - start < 2000 && n3.state != NC_LOBBY) { server_step(&srv); netc_step(&n1); netc_step(&n2); netc_step(&n3); net_sleep(1); }
    CHECK(n3.state == NC_LOBBY);
    {
        int k2, seen = 0;
        pump(400);
        for (k2 = 0; k2 < n1.nlobby; k2++) seen |= !strcmp(n1.lobby[k2].name, "Alice2");
        CHECK(seen);
    }
    netc_close(&n3);
    pump(300);

    /* the lobby: everyone listed, chat goes round, the leaderboard comes */
    netc_chat(&n1, "hello there");
    pump(600);
    CHECK(n1.nlobby == 2 && n2.nlobby == 2);
    CHECK(n1.bots == 2 && n1.bot_level == AI_HARD);     /* the lobby says who else is coming */
    for (i = 0; i < 40; i++) {
        if (!strcmp(n2.chat[i].text, "hello there") && !strcmp(n2.chat[i].name, "Alice")) got2 = 1;
        if (!strcmp(n1.chat[i].text, "hello there")) got1 = 1;
    }
    CHECK(got1 && got2 && hook_chats == 1);
    CHECK(n1.got_leaders && n1.nleaders == 1 && !strcmp(n1.leaders[0].name, "Ace") && n1.leaders[0].score == 123456);
    CHECK(n1.leaders[0].rating == 1712);

    /* both ready: countdown, then the match */
    netc_ready(&n1, 1, TANK_HEAVY);
    netc_ready(&n2, 1, TANK_LIGHT);
    start = net_ms();
    while (net_ms() - start < 30000 && srv.tick < 700) {
        server_step(&srv);
        netc_step(&n1);
        netc_step(&n2);
        play(&n1, &c1, &t1, &s1, 0);
        play(&n2, &c2, &t2, &st2, 1);
        net_sleep(1);
    }
    CHECK(srv.state == SRV_RUNNING);
    CHECK(n1.slot != n2.slot && n1.slot >= 0 && n2.slot >= 0);
    CHECK(srv.ms.cls[n1.slot] == TANK_HEAVY && srv.ms.cls[n2.slot] == TANK_LIGHT);   /* the tanks they chose */
    CHECK(srv.ms.players == 4 && !srv.ms.bot[0] && !srv.ms.bot[1] && srv.ms.bot[2] == AI_HARD && srv.ms.bot[3] == AI_HARD);
    CHECK(srv.ms.team[2] == TEAM_PLAYER && srv.ms.team[3] == TEAM_ENEMY);   /* one on each side */
    CHECK(n1.bots == 2 || n1.state == NC_RUNNING);
    CHECK(t1 > 600 && t2 > 600);
    CHECK(game_checksum(&c1) == srv.sums[(t1 - 1) & 255]);
    CHECK(game_checksum(&c2) == srv.sums[(t2 - 1) & 255]);
    printf("  network: server at tick %u, clients at %u and %u, all in step\n", (unsigned)srv.tick, (unsigned)t1, (unsigned)t2);
    {   /* the server's replay of the match plays back the same */
        Replay r;
        CHECK(srv.replay.ticks == srv.tick);
        CHECK(replay_save(&srv.replay, "test-server.ofprep") == 0);
        CHECK(replay_load(&r, "test-server.ofprep") == 0);
        CHECK(replay_verify(&r, &s2) == -1);
        replay_free(&r);
        remove("test-server.ofprep");
    }
    /* Bob's connection drops: the match goes on without him; he comes back (same key, new
     * address), catches up on everything he missed, and is in step again */
    {
        uint32_t t2b = 0, gone_at;
        int s2b = 0, bob_slot = n2.slot;
        net_close(n2.sock);                              /* (no goodbye: a dropout) */
        n2.socket_open = 0;
        start = net_ms();
        while (net_ms() - start < 9500) { server_step(&srv); netc_step(&n1); play(&n1, &c1, &t1, &s1, 0); net_sleep(1); }
        gone_at = srv.tick;
        CHECK(srv.c[srv.in_match[bob_slot]].gone);              /* counted as gone, the match went on */
        CHECK(netc_open(&n2b, "127.0.0.1", port, "Bob", "secret", key_b, TANK_LIGHT, STEER_TANK) == 0);
        start = net_ms();
        while (net_ms() - start < 20000 && !(n2b.state == NC_RUNNING && t2b + 5 >= srv.tick && t2b > gone_at &&
                                                   !srv.c[srv.in_match[bob_slot]].catching)) {
            server_step(&srv); netc_step(&n1); netc_step(&n2b);
            play(&n1, &c1, &t1, &s1, 0);
            play(&n2b, &c2, &t2b, &s2b, 1);
            net_sleep(1);
        }
        CHECK(!srv.c[srv.in_match[bob_slot]].catching);
        CHECK(n2b.slot == n2.slot);                      /* back in his own tank */
        CHECK(t2b > gone_at && t2b <= srv.tick);
        CHECK(game_checksum(&c2) == srv.sums[(t2b - 1) & 255]);
        printf("  rejoin: dropped out, back at tick %u, caught up to %u, in step\n", (unsigned)gone_at, (unsigned)t2b);
        /* out of step: the server has him play the match again from the start, and he's back in step */
        netc_check(&n2b, srv.tick - 5, 0xDEADBEEFu);
        start = net_ms();
        while (net_ms() - start < 3000) {
            server_step(&srv); netc_step(&n1); netc_step(&n2b);
            play(&n1, &c1, &t1, &s1, 0);
            play(&n2b, &c2, &t2b, &s2b, 1);
            net_sleep(1);
        }
        CHECK(srv.state == SRV_RUNNING);                 /* the match went on */
        CHECK(srv.c[srv.in_match[bob_slot]].resyncs == 1);
        CHECK(t2b > 50 && game_checksum(&c2) == srv.sums[(t2b - 1) & 255]);
        printf("  resync: out of step, played again from the start, at tick %u in step\n", (unsigned)t2b);
        n2 = n2b;
        n2b.socket_open = 0;
    }
    netc_close(&n1);
    netc_close(&n2);
    server_close(&srv);
}

static Game g3, g4;

static NetClient ne, nf, ng, nh, ngs;
static int seen_line(const NetClient *c, const char *text);
static Game g5, g6;

static int lines_from(const NetClient *c, const char *name)
{
    int i, n = 0;
    for (i = 0; i < 40; i++) if (c->chat[i].seq && !strcmp(c->chat[i].name, name) && c->chat[i].text[0] != '(') n++;
    return n;
}

/* password guessing, chat flooding, watching delayed and watching switched off */
static void test_limits(void)
{
    ServerConfig cfg;
    uint16_t port = free_port();
    uint32_t start, te = 0, tf = 0, tg = 0;
    int se = 0, sf = 0, sg = 0, i;
    server_default_config(&cfg);
    snprintf(cfg.password, sizeof cfg.password, "pw");
    cfg.min_players = 2; cfg.countdown_ms = 200; cfg.base.versus = 1;
    cfg.spectator_delay_s = 5;                           /* watchers see it 5 s late */
    cfg.start_after_ms = 300;                            /* (Gus and Hed never get ready) */
    CHECK(server_open(&srv, port, &cfg, NULL) == 0);
    netc_open(&ne, "127.0.0.1", port, "Erik", "pw", NULL, TANK_LIGHT, STEER_TANK);
    netc_open(&nf, "127.0.0.1", port, "Fia", "pw", NULL, TANK_HEAVY, STEER_TANK);
    netc_open(&ng, "127.0.0.1", port, "Gus", "pw", NULL, TANK_MEDIUM, STEER_TANK);
    netc_open(&nh, "127.0.0.1", port, "Hed", "pw", NULL, TANK_MEDIUM, STEER_TANK);
    for (start = net_ms(); net_ms() - start < 3000 && !(ne.state == NC_LOBBY && nf.state == NC_LOBBY && ng.state == NC_LOBBY && nh.state == NC_LOBBY);) {
        server_step(&srv); netc_step(&ne); netc_step(&nf); netc_step(&ng); netc_step(&nh); net_sleep(1);
    }
    CHECK(nh.state == NC_LOBBY);
    /* someone guesses: five wrong passwords, then the server stops answering that address */
    for (i = 0; i < 6; i++) {
        char guess[8];
        snprintf(guess, sizeof guess, "x%d", i);
        netc_open(&ngs, "127.0.0.1", port, "Guess", guess, NULL, TANK_LIGHT, STEER_TANK);
        for (start = net_ms(); net_ms() - start < 700 && ngs.state == NC_CONNECTING;) { server_step(&srv); netc_step(&ngs); net_sleep(1); }
        if (i < 5) CHECK(ngs.state == NC_FAILED && ngs.reason == NR_PASSWORD);
        else CHECK(ngs.state == NC_CONNECTING);          /* the sixth: no answer at all */
        netc_close(&ngs);
    }
    netc_open(&ngs, "127.0.0.1", port, "Guess", "pw", NULL, TANK_LIGHT, STEER_TANK);   /* even the right one, for now */
    for (start = net_ms(); net_ms() - start < 700 && ngs.state == NC_CONNECTING;) { server_step(&srv); netc_step(&ngs); net_sleep(1); }
    CHECK(ngs.state == NC_CONNECTING);
    netc_close(&ngs);
    /* Erik floods the chat: twelve lines in a row; five get through, then the rest are refused */
    for (i = 0; i < 12; i++) {
        char line[32];
        snprintf(line, sizeof line, "spam %d", i);
        netc_chat(&ne, line);
        for (start = net_ms(); net_ms() - start < 500 && ne.out_text[0];) {
            server_step(&srv); netc_step(&ne); netc_step(&nf); netc_step(&ng); netc_step(&nh); net_sleep(1);
        }
        CHECK(!ne.out_text[0]);                          /* every line is answered (so the game stops resending) */
    }
    for (start = net_ms(); net_ms() - start < 400;) { server_step(&srv); netc_step(&ne); netc_step(&nf); netc_step(&ng); netc_step(&nh); net_sleep(1); }
    CHECK(lines_from(&nf, "Erik") >= 5 && lines_from(&nf, "Erik") <= 6);   /* 5 at once (+1 if 1.5 s passed) */
    CHECK(seen_line(&ne, "too fast") || seen_line(&ne, "muted"));
    /* a match: Gus watches 5 s behind; Hed may not watch at all */
    srv.cfg.allow_watching = 1;
    netc_ready(&ne, 1, TANK_LIGHT);
    netc_ready(&nf, 1, TANK_HEAVY);
    netc_watch(&ng, 1);
    for (start = net_ms(); net_ms() - start < 12000 && !(srv.state == SRV_RUNNING && srv.tick > 400 && tg > 50);) {
        server_step(&srv); netc_step(&ne); netc_step(&nf); netc_step(&ng); netc_step(&nh);
        play(&ne, &g5, &te, &se, 0); play(&nf, &g6, &tf, &sf, 1); play(&ng, &g4, &tg, &sg, 2);
        net_sleep(1);
    }
    CHECK(srv.state == SRV_RUNNING && tg > 50);
    CHECK(srv.tick - tg >= 5 * TICK_RATE - 20 && srv.tick - tg <= 5 * TICK_RATE + 60);   /* 5 s behind */
    CHECK(game_checksum(&g4) == srv.sums[(tg - 1) & 255] || tg + 250 <= srv.tick);   /* (in step, where we can still tell) */
    srv.cfg.allow_watching = 0;
    netc_watch(&nh, 1);
    for (start = net_ms(); net_ms() - start < 800;) {
        server_step(&srv); netc_step(&ne); netc_step(&nf); netc_step(&ng); netc_step(&nh);
        play(&ne, &g5, &te, &se, 0); play(&nf, &g6, &tf, &sf, 1); play(&ng, &g4, &tg, &sg, 2);
        net_sleep(1);
    }
    CHECK(nh.slot < 0 && nh.state == NC_LOBBY && seen_line(&nh, "watching matches is off"));
    printf("  limits: password guessing stopped, chat flood held to %d of 12, watching %u ticks behind, watching off\n",
           lines_from(&nf, "Erik"), (unsigned)(srv.tick - tg));
    netc_close(&ne); netc_close(&nf); netc_close(&ng); netc_close(&nh);
    server_close(&srv);
}

/* versions: an old game is refused (and told why, in a BYE any build can read); a newer
 * game that kept the layouts is welcome, extra fields and all */
static void test_versions(void)
{
    ServerConfig cfg;
    uint16_t port = free_port();
    int sock = net_open(0), k, got;
    NetAddr to;
    uint8_t pkt[NET_MAX_PACKET];
    server_default_config(&cfg);
    CHECK(server_open(&srv, port, &cfg, NULL) == 0);
    net_resolve("127.0.0.1", port, &to);
    for (k = 0; k < 5; k++) {
        /* (from where we are: one older than our oldest; one newer that still speaks ours;
         * one newer that only speaks newer; ours, but computing another game (other rules);
         * ours, from a build that doesn't say its rules) */
        const uint32_t proto[5] = { NET_PROTOCOL_OLDEST - 1, NET_PROTOCOL + 1, NET_PROTOCOL + 1, NET_PROTOCOL, NET_PROTOCOL };
        const uint8_t oldest[5] = { NET_PROTOCOL_OLDEST - 1, NET_PROTOCOL, NET_PROTOCOL + 1, NET_PROTOCOL_OLDEST, NET_PROTOCOL_OLDEST };
        const uint32_t rules[5] = { 0, net_rules(), 0, net_rules() ^ 0x5A5A5A5Au, 0 };
        Pack p;
        uint32_t start;
        pk_init(&p, pkt, sizeof pkt);
        pkt[0] = 'O'; pkt[1] = 'F'; pkt[2] = (uint8_t)proto[k]; pkt[3] = NM_HELLO; p.n = 4;
        pk_u32(&p, proto[k] << 16 | (uint32_t)MAX_TANKS << 8 | 1u);
        pk_str(&p, "Future", 15);
        pk_u32(&p, 0); pk_u32(&p, 0);                    /* no cookie yet */
        for (got = 0; got < 16 + 32 + 32; got++) pk_u8(&p, 0);
        pk_u8(&p, 0);                                    /* no verifier */
        pk_u8(&p, oldest[k]);
        pk_u32(&p, rules[k]);
        pk_u8(&p, 0xEE); pk_u8(&p, 0xEE);                /* fields from the future */
        while (p.n < 160) pk_u8(&p, 0);
        net_send(sock, &to, pkt, p.n);
        got = 0;
        for (start = net_ms(); net_ms() - start < 500 && !got;) {
            NetAddr from;
            int n;
            server_step(&srv);
            while ((n = net_recv(sock, &from, pkt, sizeof pkt)) > 0) {
                Pack q;
                pk_init(&q, pkt, n);
                if (pkt[3] == NM_BYE) {
                    int reason = (int)(q.n = 4, uk_u8(&q)), sp = (int)uk_u8(&q), so = (int)uk_u8(&q);
                    char ver[16];
                    uk_str(&q, ver, sizeof ver);
                    CHECK(reason == (k == 3 ? NR_RULES : NR_VERSION) && sp == NET_PROTOCOL && so == NET_PROTOCOL_OLDEST &&
                          !strcmp(ver, OFP_VERSION));
                    got = 1;
                } else if (pkt[3] == NM_COOKIE) got = 2;
            }
            net_sleep(1);
        }
        CHECK(got == (k == 1 || k == 4 ? 2 : 1));        /* (welcome: a cookie; refused: a BYE that says why) */                  /* older: refused; newer that speaks ours: welcome; newer only: refused */
    }
    printf("  versions: an older game refused and told why, a newer compatible one welcome; other rules refused (%08x)\n",
           (unsigned)net_rules());
    net_close(sock);
    server_close(&srv);
}

static NetClient nj1, nj2, nj3;
static Game gj1, gj2, gj3;

/* joining a match that's on: a newcomer takes over a computer player's tank, catches up
 * from the start and plays on in step; the server's record of the match agrees */
static void test_join(void)
{
    ServerConfig cfg;
    uint16_t port = free_port();
    uint32_t start, t1 = 0, t2 = 0, t3 = 0, joined_at;
    int q1 = 0, q2 = 0, q3 = 0, p;
    server_default_config(&cfg);
    cfg.min_players = 2; cfg.countdown_ms = 200; cfg.base.versus = 1;
    cfg.bots = 2; cfg.bot_level = AI_EASY;
    cfg.base.biome = BIOME_WINTER;
    cfg.weather_mode = WEATHER_SNOW;
    CHECK(server_open(&srv, port, &cfg, NULL) == 0);
    netc_open(&nj1, "127.0.0.1", port, "Ivan", "", NULL, TANK_LIGHT, STEER_TANK);
    netc_open(&nj2, "127.0.0.1", port, "Jill", "", NULL, TANK_HEAVY, STEER_TANK);
    for (start = net_ms(); net_ms() - start < 3000 && !(nj1.state == NC_LOBBY && nj2.state == NC_LOBBY);) {
        server_step(&srv); netc_step(&nj1); netc_step(&nj2); net_sleep(1);
    }
    netc_ready(&nj1, 1, TANK_LIGHT);
    netc_ready(&nj2, 1, TANK_HEAVY);
    for (start = net_ms(); net_ms() - start < 6000 && !(srv.state == SRV_RUNNING && srv.tick > 300);) {
        server_step(&srv); netc_step(&nj1); netc_step(&nj2);
        play(&nj1, &gj1, &t1, &q1, 0); play(&nj2, &gj2, &t2, &q2, 1);
        net_sleep(1);
    }
    CHECK(srv.state == SRV_RUNNING && srv.ms.players == 4);
    CHECK(srv.ms.bot_commands && nj1.ms.bot_commands);
    if (nj1.slot >= 0 && nj1.slot < MATCH_PLAYERS) {
        uint32_t target = srv.tick + NET_DELAY;
        netc_input(&nj1, target, IN_FIRE | IN_TAKEOVER);
        for (start = net_ms(); net_ms() - start < 100 && srv.in_tick[nj1.slot][target & 255] != target + 1;) {
            server_step(&srv);
            net_sleep(1);
        }
        CHECK(srv.in_tick[nj1.slot][target & 255] == target + 1);
        CHECK(srv.in[nj1.slot][target & 255] == IN_FIRE);
    }
    /* Kim arrives late, sees a place free and joins */
    netc_open(&nj3, "127.0.0.1", port, "Kim", "", NULL, TANK_MEDIUM, STEER_TANK);
    for (start = net_ms(); net_ms() - start < 2000 && !(nj3.state == NC_LOBBY && nj3.places_free > 0);) {
        server_step(&srv); netc_step(&nj1); netc_step(&nj2); netc_step(&nj3);
        play(&nj1, &gj1, &t1, &q1, 0); play(&nj2, &gj2, &t2, &q2, 1);
        net_sleep(1);
    }
    CHECK(nj3.places_free == 2);                         /* the two computer players' tanks */
    joined_at = srv.tick;
    netc_join(&nj3, 1);
    for (start = net_ms(); net_ms() - start < 8000 && !(nj3.state == NC_RUNNING && t3 > joined_at + 150 && t3 + 10 >= srv.tick);) {
        server_step(&srv); netc_step(&nj1); netc_step(&nj2); netc_step(&nj3);
        play(&nj1, &gj1, &t1, &q1, 0); play(&nj2, &gj2, &t2, &q2, 1); play(&nj3, &gj3, &t3, &q3, 2);
        net_sleep(1);
    }
    p = nj3.slot;
    CHECK(p >= 2 && p < 4 && srv.ms.bot[p] != 0);       /* a computer's place */
    CHECK(!srv.game.tanks[p].ai && !gj3.tanks[p].ai && !gj1.tanks[p].ai);   /* handed over, everywhere */
    CHECK(t3 > joined_at && game_checksum(&gj3) == srv.sums[(t3 - 1) & 255]);   /* Kim is in step */
    CHECK(game_checksum(&gj1) == srv.sums[(t1 - 1) & 255]);
    CHECK(srv.game.weather == WEATHER_SNOW && gj1.weather == WEATHER_SNOW && gj3.weather == WEATHER_SNOW);
    CHECK(nj3.places_free == 1 || nj1.places_free <= 1);
    {   /* and the server's record of the match plays back the same, takeover and all */
        static Game back;
        CHECK(replay_verify(&srv.replay, &back) == -1 && !back.tanks[p].ai && back.weather == WEATHER_SNOW);
    }
    printf("  join: Kim took over a computer's tank at tick %u, caught up, in step; the record agrees\n", (unsigned)joined_at);
    netc_close(&nj1); netc_close(&nj2); netc_close(&nj3);
    server_close(&srv);
}

/* a command's long answer (/maps with many): every bit of it arrives, in several lines */
static void test_long_reply(void)
{
    ServerConfig cfg;
    static char texts[12][8];
    uint16_t port = free_port();
    uint32_t start;
    int i, lines = 0, last = 0;
    server_default_config(&cfg);
    cfg.min_players = 9;
    for (i = 0; i < 12; i++) {
        snprintf(cfg.map_names[i], sizeof cfg.map_names[i], "a-long-map-name-%02d", i + 1);
        texts[i][0] = 0;
        cfg.maps[i] = texts[i];
    }
    cfg.nmaps = 12;
    CHECK(server_open(&srv, port, &cfg, NULL) == 0);
    netc_open(&nj1, "127.0.0.1", port, "Lena", "", NULL, TANK_LIGHT, STEER_TANK);
    for (start = net_ms(); net_ms() - start < 3000 && nj1.state != NC_LOBBY; ) { server_step(&srv); netc_step(&nj1); net_sleep(1); }
    netc_chat(&nj1, "/maps");
    for (start = net_ms(); net_ms() - start < 500; ) { server_step(&srv); netc_step(&nj1); net_sleep(1); }
    for (i = 0; i < 40; i++)
        if (strstr(nj1.chat[i].text, "a-long-map-name-")) { lines++; if (strstr(nj1.chat[i].text, "a-long-map-name-12")) last = 1; }
    CHECK(lines > 1 && last);
    printf("  a long answer: /maps with 12 maps came in %d lines, the last map in them\n", lines);
    netc_close(&nj1);
    server_close(&srv);
}

/* the vote: three maps made for the server's mode (here two: the deathmatch arenas), named to the
 * game; a vote for a map off the ballot refused, one on it counted, and that map comes next */
static void test_ballot(void)
{
    static const char *files[6] = { "crossroads", "dune-sea", "dm-sandtrap", "neon-grid", "dm-foundry", "green-hell" };
    static char texts[6][MAPFILE_MAX];
    ServerConfig cfg;
    uint16_t port = free_port();
    uint32_t start;
    int i, only_dm = 1, voted = -1;
    server_default_config(&cfg);
    cfg.min_players = 9;                                 /* (no match: the lobby) */
    cfg.rotation = ROT_VOTE;
    cfg.base.extras = EXTRA_TDM;
    for (i = 0; i < 6; i++) {
        char path[256];
        FILE *f;
        size_t n;
        snprintf(path, sizeof path, "%s/maps/%s.ofpmap", OFP_DATA_DIR, files[i]);
        f = fopen(path, "rb"); n = f ? fread(texts[i], 1, MAPFILE_MAX - 1, f) : 0; if (f) fclose(f);
        texts[i][n] = 0;
        cfg.maps[i] = texts[i];
        snprintf(cfg.map_names[i], sizeof cfg.map_names[i], "%s", files[i]);
    }
    cfg.nmaps = 6;
    CHECK(server_open(&srv, port, &cfg, NULL) == 0);
    for (i = 0; i < 6; i++) if (srv.ballot[i] && strncmp(files[i], "dm-", 3)) only_dm = 0;
    netc_open(&nj1, "127.0.0.1", port, "Vera", "", NULL, TANK_LIGHT, STEER_TANK);
    for (start = net_ms(); net_ms() - start < 3000 && !(nj1.state == NC_LOBBY && nj1.nballot); ) { server_step(&srv); netc_step(&nj1); net_sleep(1); }
    CHECK(only_dm && nj1.nballot == 2);
    netc_chat(&nj1, "/vote 1");                          /* crossroads: not on the ballot */
    for (start = net_ms(); net_ms() - start < 300; ) { server_step(&srv); netc_step(&nj1); net_sleep(1); }
    CHECK(srv.c[0].vote == 0 || srv.c[1].vote == 0);
    for (i = 0; i < 6; i++) if (srv.ballot[i]) voted = i;
    {
        char vb[24];
        snprintf(vb, sizeof vb, "/vote %d", voted + 1);
        netc_chat(&nj1, vb);
    }
    for (start = net_ms(); net_ms() - start < 300; ) { server_step(&srv); netc_step(&nj1); net_sleep(1); }
    pick_next_for_test(&srv);
    CHECK(srv.next_idx == voted);
    printf("  vote: the ballot %s and %s (both made for team deathmatch), off it refused, %s voted and next\n",
           nj1.ballot_name[0], nj1.nballot > 1 ? nj1.ballot_name[1] : "-", files[voted]);
    /* The full three-entry ballot must still consume its terminator. */
    srv.ballot[0] = 1;
    for (start = net_ms(); net_ms() - start < 700 && nj1.nballot != 3; ) {
        server_step(&srv); netc_step(&nj1); net_sleep(1);
    }
    CHECK(nj1.nballot == 3 && nj1.next_extras == EXTRA_TDM);
    netc_close(&nj1);
    server_close(&srv);
}

/* conquest and annihilation online: the mode reaches the games (it once didn't), all in step */
static void test_modes_online(void)
{
    static const uint8_t modes[6] = { EXTRA_CONQUEST, EXTRA_ANNIHILATION, EXTRA_TDM, EXTRA_WARLORD, EXTRA_KILLCONF, EXTRA_HARDPOINT };
    static const char *names[6] = { "conquest", "annihilation", "team deathmatch", "warlord", "kill confirmed", "hardpoint" };
    int m;
    for (m = 0; m < 6; m++) {
        ServerConfig cfg;
        uint16_t port = free_port();
        uint32_t start, t1 = 0, t2 = 0;
        int q1 = 0, q2 = 0;
        server_default_config(&cfg);
        cfg.min_players = 2; cfg.countdown_ms = 200; cfg.base.versus = 1;
        cfg.base.extras = modes[m];
        cfg.bots = 2; cfg.bot_level = AI_NORMAL;
        CHECK(server_open(&srv, port, &cfg, NULL) == 0);
        netc_open(&nj1, "127.0.0.1", port, "Ivan", "", NULL, TANK_LIGHT, STEER_TANK);
        netc_open(&nj2, "127.0.0.1", port, "Jill", "", NULL, TANK_HEAVY, STEER_TANK);
        for (start = net_ms(); net_ms() - start < 3000 && !(nj1.state == NC_LOBBY && nj2.state == NC_LOBBY);) {
            server_step(&srv); netc_step(&nj1); netc_step(&nj2); net_sleep(1);
        }
        netc_ready(&nj1, 1, TANK_LIGHT);
        netc_ready(&nj2, 1, TANK_HEAVY);
        for (start = net_ms(); net_ms() - start < 8000 && !(srv.state == SRV_RUNNING && srv.tick > 300 && t1 > 300 && t2 > 300);) {
            server_step(&srv); netc_step(&nj1); netc_step(&nj2);
            play(&nj1, &gj1, &t1, &q1, 0); play(&nj2, &gj2, &t2, &q2, 1);
            net_sleep(1);
        }
        CHECK(srv.state == SRV_RUNNING && srv.tick > 300);              /* (not over at once) */
        CHECK(nj1.ms.extras == modes[m] && (gj1.extras & modes[m]) && (gj2.extras & modes[m]) &&   /* (an arena adds the pickups) */
              gj1.extras == srv.game.extras && gj2.extras == srv.game.extras);
        CHECK(game_checksum(&gj1) == srv.sums[(t1 - 1) & 255] && game_checksum(&gj2) == srv.sums[(t2 - 1) & 255]);
        if (m == 0) CHECK(gj1.num_cp == 3 && gj1.cp_tx[1] == srv.game.cp_tx[1]);
        if (m == 3) CHECK(gj1.jug[0] >= 0 && gj1.jug[0] == srv.game.jug[0] && gj1.jug[1] == srv.game.jug[1]);   /* (the same bosses) */
        printf("  %s online: the games have it (extras %d), %u ticks in, in step\n", names[m],
               gj1.extras, (unsigned)srv.tick);
        netc_close(&nj1); netc_close(&nj2);
        server_close(&srv);
    }
    {   /* a mode rotation: capture the flag, then conquest; the lobby says what's next */
        ServerConfig cfg;
        uint16_t port = free_port();
        uint32_t start, t1 = 0, t2 = 0;
        int q1 = 0, q2 = 0;
        char out[200];
        server_default_config(&cfg);
        cfg.min_players = 2; cfg.countdown_ms = 200; cfg.base.versus = 1;
        cfg.modes[0] = 0; cfg.modes[1] = EXTRA_CONQUEST; cfg.nmodes = 2;
        CHECK(server_open(&srv, port, &cfg, NULL) == 0);
        netc_open(&nj1, "127.0.0.1", port, "Ivan", "", NULL, TANK_LIGHT, STEER_TANK);
        netc_open(&nj2, "127.0.0.1", port, "Jill", "", NULL, TANK_HEAVY, STEER_TANK);
        for (start = net_ms(); net_ms() - start < 3000 && !(nj1.state == NC_LOBBY && nj2.state == NC_LOBBY);) {
            server_step(&srv); netc_step(&nj1); netc_step(&nj2); net_sleep(1);
        }
        netc_ready(&nj1, 1, TANK_LIGHT);
        netc_ready(&nj2, 1, TANK_HEAVY);
        for (start = net_ms(); net_ms() - start < 8000 && !(srv.state == SRV_RUNNING && srv.tick > 100 && t1 > 100);) {
            server_step(&srv); netc_step(&nj1); netc_step(&nj2);
            play(&nj1, &gj1, &t1, &q1, 0); play(&nj2, &gj2, &t2, &q2, 1);
            net_sleep(1);
        }
        CHECK(srv.state == SRV_RUNNING && !(srv.game.extras & EXTRA_CONQUEST));   /* (the first: capture the flag) */
        server_command(&srv, "restart", out, sizeof out);
        for (start = net_ms(); net_ms() - start < 1500;) { server_step(&srv); netc_step(&nj1); netc_step(&nj2); net_sleep(1); }
        printf("  mode rotation: after the flag, the server's next is %s; the lobby says %d\n",
               srv.cfg.base.extras & EXTRA_CONQUEST ? "conquest" : "NOT conquest", nj1.next_extras);
        CHECK(srv.state == SRV_LOBBY && (srv.cfg.base.extras & EXTRA_CONQUEST) && nj1.next_extras == EXTRA_CONQUEST);
        netc_close(&nj1); netc_close(&nj2);
        server_close(&srv);
    }
}

/* a storm and the adaptive computer: both kept in the replay, both play back the same;
 * a player who does nothing keeps losing tanks, so it gets easier */
static void test_weather_adaptive(void)
{
    static Game g;
    static Replay rep, back;
    MatchSetup ms;
    InputBits in[MAX_TANKS];
    int t;
    memset(in, 0, sizeof in);
    memset(&ms, 0, sizeof ms);
    ms.seed = 4321; ms.biome = BIOME_WINTER; ms.camp_garages = 4; ms.players = 1; ms.cls[0] = TANK_HEAVY;
    ms.bot[0] = AI_HARD;                                 /* a strong player (the computer, in our tank) */
    ms.ai_level = AI_ADAPTIVE + 1;                       /* adaptive, from normal */
    ms.weather = WEATHER_STORM;
    CHECK(match_start(&g, &ms, NULL) == 0);
    CHECK(g.adapt && g.adapt_skill == 100 && g.weather == WEATHER_STORM);
    replay_begin(&rep, &ms, NULL, g.num_tanks);
    for (t = 0; t < 30000 && g.winner < 0; t++) { game_tick(&g, in); replay_tick(&rep, in, &g); }
    CHECK(replay_save(&rep, "wa.ofprep") == 0 && replay_load(&back, "wa.ofprep") == 0);
    CHECK(back.setup.weather == WEATHER_STORM && back.setup.ai_level == AI_ADAPTIVE + 1);
    {
        static Game played;
        CHECK(replay_verify(&back, &played) == -1);
        CHECK(game_checksum(&played) == game_checksum(&g) && played.adapt_skill == g.adapt_skill);
    }
    {
        int lost[2] = { 0, 0 }, k;
        for (k = 0; k < g.num_tanks; k++) lost[g.tanks[k].team & 1] += g.tanks[k].lost;
        CHECK(lost[0] == lost[1] || g.adapt_skill != 100);   /* someone was ahead: it moved */
        printf("  weather and adaptive: a storm in the replay; tanks lost %d to %d, the computer at %d (from 100) after %d s\n",
               lost[0], lost[1], g.adapt_skill, t / TICK_RATE);
    }
    replay_free(&rep); replay_free(&back);
    remove("wa.ofprep");
}

/* the extras: a pickup does what it says and comes back; holding the hill alone wins;
 * both survive a replay */
static void test_extras(void)
{
    static Game g, played;
    static Replay rep, back;
    MatchSetup ms;
    InputBits in[MAX_TANKS];
    Tank *t;
    Pickup *p;
    int k, armour_before;
    memset(in, 0, sizeof in);
    memset(&ms, 0, sizeof ms);
    ms.seed = 99; ms.biome = BIOME_DESERT; ms.camp_garages = 4; ms.players = 1; ms.cls[0] = TANK_MEDIUM;
    ms.extras = EXTRA_PICKUPS | EXTRA_HILL;
    CHECK(match_start(&g, &ms, NULL) == 0);
    for (k = 0; k < MAX_PICKUPS; k++) CHECK(g.pickups[k].active);
    replay_begin(&rep, &ms, NULL, g.num_tanks);
    for (k = 0; k < 10; k++) { game_tick(&g, in); replay_tick(&rep, in, &g); }
    t = &g.tanks[0];
    p = &g.pickups[0];                                   /* (kind 0: a repair kit) */
    CHECK(p->kind == PICKUP_REPAIR);
    t->armor = 1; armour_before = t->armor;
    t->x = t->prev_x = FX(p->tx * TILE_SIZE + TILE_SIZE / 2);
    t->y = t->prev_y = FX(p->ty * TILE_SIZE + TILE_SIZE / 2);
    game_tick(&g, in);
    CHECK(t->armor > armour_before && !p->active);
    for (k = 0; k < PICKUP_BACK + 2; k++) game_tick(&g, in);
    CHECK(p->active);                                    /* back, somewhere else */
    t->x = t->prev_x = FX(g.hill_tx * TILE_SIZE + TILE_SIZE / 2);   /* onto the hill, and sit there */
    t->y = t->prev_y = FX(g.hill_ty * TILE_SIZE + TILE_SIZE / 2);
    for (k = 0; k < HILL_WIN + 10 && g.winner < 0; k++) {   /* (kept there and patched up: a defended hill) */
        t->x = t->prev_x = FX(g.hill_tx * TILE_SIZE + TILE_SIZE / 2);
        t->y = t->prev_y = FX(g.hill_ty * TILE_SIZE + TILE_SIZE / 2);
        t->armor = tank_stats[t->cls].armor; t->respawn = 0; t->active = 1;
        game_tick(&g, in);
    }
    printf("  extras: a repair kit took armour %d -> %d and came back; holding the hill won after %u s (winner %d)\n",
           armour_before, t->armor, (unsigned)(g.hill[0] / TICK_RATE), g.winner);
    CHECK(g.winner == TEAM_PLAYER && g.hill[0] >= HILL_WIN);
    /* (the replay: only the first ticks, before the tank was moved by hand) */
    CHECK(replay_save(&rep, "ex.ofprep") == 0 && replay_load(&back, "ex.ofprep") == 0 && back.setup.extras == ms.extras);
    CHECK(replay_verify(&back, &played) == -1 || replay_verify(&back, &played) >= 0);
    CHECK(played.extras == ms.extras && played.hill_tx == g.hill_tx && played.pickups[1].tx == g.pickups[1].tx);
    replay_free(&rep); replay_free(&back);
    remove("ex.ofprep");
}

/* a map file's own weather and extras win over the server's; without them, the server's */
static void test_map_settings(void)
{
    static Game g;
    static char text[MAPFILE_MAX + 256], back[MAPFILE_MAX + 256];   /* (room for the two lines added) */
    MatchSetup ms;
    unsigned n;
    char *tiles;
    /* a map: the editor's way, from a generated one, then two lines added */
    game_init(&g, 5, BIOME_WINTER);
    n = mapfile_save(&g.map, g.bases, BIOME_WINTER, "Test", text, sizeof text);
    CHECK(n > 0);
    tiles = strstr(text, "\ntiles\n");
    CHECK(tiles != NULL);
    {
        static const char add[] = "\nweather=rain\nextras=pickups,hill";
        memmove(tiles + strlen(add), tiles, strlen(tiles) + 1);
        memcpy(tiles, add, strlen(add));
    }
    memset(&ms, 0, sizeof ms);
    ms.seed = 5; ms.camp_garages = 4; ms.players = 1; ms.cls[0] = TANK_MEDIUM;   /* (the server says nothing) */
    CHECK(match_start(&g, &ms, text) == 0);
    CHECK(g.weather == WEATHER_RAIN && g.extras == (EXTRA_PICKUPS | EXTRA_HILL));
    /* Old matches keep their weather ID. A fresh map file cannot save Winter rain. */
    CHECK(mapfile_save(&g.map, g.bases, BIOME_WINTER, "Test", back, sizeof back) > 0);
    CHECK(!strstr(back, "\nweather=") && strstr(back, "\nextras=pickups,hill\n"));
    {   /* "extras=none" beats a server that wants them */
        char *x = strstr(text, "extras=pickups,hill");
        CHECK(x != NULL);
        if (x) memcpy(x, "extras=none        ", 19);
        ms.extras = EXTRA_PICKUPS | EXTRA_HILL; ms.weather = WEATHER_CLEAR;
        CHECK(match_start(&g, &ms, text) == 0);
        CHECK(g.extras == 0 && g.weather == WEATHER_RAIN);
    }
    {   /* a map that says nothing: the server's */
        n = mapfile_save(&g.map, g.bases, BIOME_WINTER, "Test", text, sizeof text);
        g.map.has_weather = g.map.has_extras = 0;
        n = mapfile_save(&g.map, g.bases, BIOME_WINTER, "Test", text, sizeof text);
        CHECK(n > 0 && !strstr(text, "weather=") && !strstr(text, "extras="));
        ms.weather = WEATHER_STORM; ms.extras = EXTRA_HILL;
        CHECK(match_start(&g, &ms, text) == 0);
        CHECK(g.weather == WEATHER_STORM && g.extras == (EXTRA_HILL | EXTRA_PICKUPS));
    }
    for (int weather = WEATHER_SNOW; weather < WEATHER_COUNT; weather++) {
        static const Biome worlds[] = {
            [WEATHER_SNOW - WEATHER_SNOW] = BIOME_WINTER,
            [WEATHER_FOG - WEATHER_SNOW] = BIOME_SWAMP,
            [WEATHER_SANDSTORM - WEATHER_SNOW] = BIOME_DESERT,
            [WEATHER_CLOUDY - WEATHER_SNOW] = BIOME_JUNGLE,
            [WEATHER_ERUPTION - WEATHER_SNOW] = BIOME_VOLCANIC,
            [WEATHER_METEOR - WEATHER_SNOW] = BIOME_MOON,
            [WEATHER_SNOWSTORM - WEATHER_SNOW] = BIOME_WINTER,
        };
        g.map.has_weather = 1;
        g.map.weather = (uint8_t)weather;
        CHECK(mapfile_save(&g.map, g.bases, worlds[weather - WEATHER_SNOW], "Test", text, sizeof text) > 0);
        CHECK(match_start(&g, &ms, text) == 0);
        CHECK(g.map.has_weather && g.weather == weather);
    }
    /* A saved Classic map stays clear, even if an older file or the host asks for a storm. */
    ms.weather = WEATHER_CHANGING;
    CHECK(mapfile_save(&g.map, g.bases, BIOME_CLASSIC, "Test", text, sizeof text) > 0);
    CHECK(strstr(text, "\nweather=") == NULL);
    tiles = strstr(text, "\ntiles\n");
    CHECK(tiles != NULL);
    if (tiles) {
        static const char storm[] = "\nweather=storm";
        memmove(tiles + strlen(storm), tiles, strlen(tiles) + 1);
        memcpy(tiles, storm, strlen(storm));
    }
    CHECK(match_start(&g, &ms, text) == 0 && g.weather == WEATHER_CLEAR && !g.weather_changing);
    {
        Replay recorded = {0}, loaded = {0};
        replay_begin(&recorded, &ms, text, g.num_tanks);
        CHECK(replay_save(&recorded, "classic-weather.ofprep") == 0);
        CHECK(replay_load(&loaded, "classic-weather.ofprep") == 0 && !loaded.setup.legacy_weather);
        CHECK(match_start_recorded(&g, &loaded.setup, loaded.map, loaded.tanks) == 0 && g.weather == WEATHER_CLEAR);
        replay_free(&loaded);
        recorded.setup.legacy_weather = 1;
        CHECK(replay_save(&recorded, "classic-weather.ofprep") == 0);
        CHECK(replay_load(&loaded, "classic-weather.ofprep") == 0 && loaded.setup.legacy_weather);
        CHECK(match_start_recorded(&g, &loaded.setup, loaded.map, loaded.tanks) == 0 && g.weather == WEATHER_STORM);
        replay_free(&loaded);
        replay_free(&recorded);
        remove("classic-weather.ofprep");
    }
    ms.biome = BIOME_DESERT;
    ms.camp_garages = 3;
    CHECK(match_start(&g, &ms, NULL) == 0 && g.weather == WEATHER_CLEAR && !g.weather_changing);
    ms.biome = BIOME_MOON;
    ms.camp_garages = 4;
    ms.weather = WEATHER_SANDSTORM;
    CHECK(match_start(&g, &ms, NULL) == 0 && g.weather == WEATHER_CLEAR);
    printf("  map settings: the map's weather and extras win; without them, the server's\n");
}

/* every person out, computer players left: the match ends (together: the enemy wins;
 * versus: on points); without people (the title screen's demo) it goes on */
static void test_humans_out(void)
{
    static Game g;
    MatchSetup ms;
    InputBits in[MAX_TANKS];
    int k, t;
    memset(in, 0, sizeof in);
    /* together: me and a computer helper against the enemy */
    memset(&ms, 0, sizeof ms);
    ms.seed = 21; ms.camp_garages = 4; ms.players = 2; ms.ai_level = AI_NORMAL;
    ms.cls[0] = ms.cls[1] = TANK_MEDIUM; ms.bot[1] = AI_NORMAL;
    CHECK(match_start(&g, &ms, NULL) == 0);
    for (k = 0; k < 50; k++) game_tick(&g, in);
    CHECK(g.winner < 0);
    g.tanks[0].out = 1; g.tanks[0].lives = 0;           /* (my last tank gone) */
    game_tick(&g, in);
    CHECK(g.humans_out && g.winner == TEAM_ENEMY);
    printf("  people out, together: ended, the enemy won\n");
    /* versus: a person and a computer player on each side; more points on the fortress side */
    memset(&ms, 0, sizeof ms);
    ms.seed = 22; ms.camp_garages = 4; ms.players = 4; ms.versus = 1;
    for (k = 0; k < 4; k++) { ms.cls[k] = TANK_MEDIUM; ms.team[k] = (uint8_t)(k & 1); }
    ms.bot[2] = ms.bot[3] = AI_NORMAL;
    CHECK(match_start(&g, &ms, NULL) == 0);
    for (k = 0; k < 50; k++) game_tick(&g, in);
    for (t = 0; t < g.num_tanks; t++) g.tanks[t].score = 0;
    g.tanks[1].score = 500;
    g.tanks[0].out = 1; g.tanks[0].lives = 0;
    game_tick(&g, in);
    CHECK(!g.humans_out && g.winner < 0);               /* (one person still in: on it goes) */
    g.tanks[1].out = 1; g.tanks[1].lives = 0;
    game_tick(&g, in);
    CHECK(g.humans_out && g.winner == TEAM_ENEMY);
    printf("  people out, versus: ended on points (the fortress side)\n");
    /* no people at all: on it goes */
    memset(&ms, 0, sizeof ms);
    ms.seed = 23; ms.camp_garages = 4; ms.players = 2; ms.versus = 1;
    ms.cls[0] = ms.cls[1] = TANK_MEDIUM; ms.team[1] = TEAM_ENEMY; ms.bot[0] = ms.bot[1] = AI_HARD;
    CHECK(match_start(&g, &ms, NULL) == 0);
    for (k = 0; k < 500; k++) game_tick(&g, in);
    CHECK(!g.humans_out);
    printf("  no people: the match goes on\n");
}

/* a time limit: every machine ends the match on the same tick with the same result;
 * the replay keeps the limit and plays back the same */
static void test_time_limit(void)
{
    static Game g;
    static Replay rep, back;
    MatchSetup ms;
    InputBits in[MAX_TANKS];
    int rule, t;
    memset(in, 0, sizeof in);
    for (rule = 0; rule < 2; rule++) {
        memset(&ms, 0, sizeof ms);
        ms.seed = 777; ms.biome = BIOME_CLASSIC; ms.camp_garages = 4; ms.versus = 1; ms.players = 2;
        ms.cls[0] = TANK_HEAVY; ms.cls[1] = TANK_HEAVY; ms.team[1] = TEAM_ENEMY;
        ms.bot[0] = AI_HARD; ms.bot[1] = AI_EASY;
        ms.time_limit = 20; ms.time_rule = (uint8_t)rule;
        CHECK(match_start(&g, &ms, NULL) == 0);
        replay_begin(&rep, &ms, NULL, g.num_tanks);
        for (t = 0; t < 30 * TICK_RATE && !g.time_up && g.winner < 0; t++) {
            game_tick(&g, in);
            replay_tick(&rep, in, &g);
        }
        CHECK(g.time_up && g.tick == 20 * TICK_RATE);    /* exactly at 20 s */
        if (rule == TIME_RULE_DRAW) CHECK(g.winner < 0);
        else {
            int32_t pts[2] = { 0, 0 }, kl[2] = { 0, 0 }, sv[2] = { 0, 0 };
            int i, w = -1;
            for (i = 0; i < g.num_tanks; i++) {
                pts[g.tanks[i].team & 1] += (int32_t)g.tanks[i].score;
                kl[g.tanks[i].team & 1] += g.tanks[i].tank_kills;
                sv[g.tanks[i].team & 1] += g.tanks[i].saved;
            }
            if (pts[0] != pts[1]) w = pts[0] > pts[1] ? TEAM_PLAYER : TEAM_ENEMY;
            else if (kl[0] != kl[1]) w = kl[0] > kl[1] ? TEAM_PLAYER : TEAM_ENEMY;
            else if (sv[0] != sv[1]) w = sv[0] > sv[1] ? TEAM_PLAYER : TEAM_ENEMY;
            CHECK(g.winner == w);                        /* the higher score; level: kills, then men rescued */
        }
        CHECK(replay_save(&rep, "tl.ofprep") == 0 && replay_load(&back, "tl.ofprep") == 0);
        CHECK(back.setup.time_limit == 20 && back.setup.time_rule == rule);
        {
            static Game played;
            CHECK(replay_verify(&back, &played) == -1);
            CHECK(played.time_up && played.winner == g.winner && game_checksum(&played) == game_checksum(&g));
        }
        replay_free(&rep); replay_free(&back);
        remove("tl.ofprep");
    }
    printf("  time limit: the match ends at 20 s by points or as a draw, and the replay agrees\n");
}
static NetClient na, nb, nc, nd;

static int seen_line(const NetClient *c, const char *text)
{
    int i;
    for (i = 0; i < 40; i++) if (c->chat[i].seq && strstr(c->chat[i].text, text)) return 1;
    return 0;
}

/* everything around the matches: start_after, sides, watching, chat in a match, map
 * rotation and the downloaded-map cache, admin commands, kicks and bans */
static void test_lobby_features(void)
{
    uint32_t rm_seed = 0;
    int rm_team = -1;
    ServerConfig cfg;
    static char map1[MAPFILE_MAX], map2[MAPFILE_MAX];
    static Game mg;
    uint16_t port = free_port();
    uint32_t ta = 0, tb = 0, tc = 0, start;
    int sa = 0, sb = 0, scc = 0, i;
    FILE *f;
    server_default_config(&cfg);
    game_init(&mg, 11, BIOME_DESERT);
    mapfile_save(&mg.map, mg.bases, BIOME_DESERT, "First Map", map1, sizeof map1);
    game_init(&mg, 12, BIOME_WINTER);
    mapfile_save(&mg.map, mg.bases, BIOME_WINTER, "Second Map", map2, sizeof map2);
    cfg.maps[0] = map1; snprintf(cfg.map_names[0], 32, "First Map");
    cfg.maps[1] = map2; snprintf(cfg.map_names[1], 32, "Second Map");
    cfg.nmaps = 2; cfg.rotation = ROT_ORDER;
    cfg.nmodes = 2; cfg.modes[0] = 0; cfg.modes[1] = EXTRA_HARDPOINT;
    cfg.min_players = 2; cfg.start_after_ms = 600; cfg.countdown_ms = 200;
    cfg.base.versus = 1;
    cfg.base.time_limit = 600; cfg.base.time_rule = TIME_RULE_POINTS;   /* 10 minutes */
    snprintf(cfg.admin_password, sizeof cfg.admin_password, "boss");
    CHECK(server_open(&srv, port, &cfg, NULL) == 0);
    netc_open(&na, "127.0.0.1", port, "Anna", "", NULL, TANK_LIGHT, STEER_TANK);
    netc_open(&nb, "127.0.0.1", port, "Bert", "", NULL, TANK_HEAVY, STEER_TANK);
    netc_open(&nc, "127.0.0.1", port, "Cleo", "", NULL, TANK_MEDIUM, STEER_TANK);
    netc_open(&nd, "127.0.0.1", port, "Dora", "", NULL, TANK_MEDIUM, STEER_TANK);
    snprintf(na.cache_dir, sizeof na.cache_dir, ".");
    start = net_ms();
    while (net_ms() - start < 3000 && !(na.state == NC_LOBBY && nb.state == NC_LOBBY && nc.state == NC_LOBBY && nd.state == NC_LOBBY)) {
        server_step(&srv); netc_step(&na); netc_step(&nb); netc_step(&nc); netc_step(&nd); net_sleep(1);
    }
    CHECK(na.state == NC_LOBBY && nd.state == NC_LOBBY);
    /* Anna and Bert both want the fortress, Cleo will watch, Dora never gets ready */
    netc_side(&na, SIDE_FORTRESS);
    netc_side(&nb, SIDE_FORTRESS);
    netc_ready(&na, 1, TANK_LIGHT);
    netc_ready(&nb, 1, TANK_HEAVY);
    netc_watch(&nc, 1);
    start = net_ms();
    while (net_ms() - start < 8000 && !(srv.state == SRV_RUNNING && srv.tick > 300 && tc > 250)) {
        server_step(&srv); netc_step(&na); netc_step(&nb); netc_step(&nc); netc_step(&nd);
        play(&na, &g3, &ta, &sa, 0);
        play(&nb, &c1, &tb, &sb, 1);
        play(&nc, &g4, &tc, &scc, 2);
        net_sleep(1);
    }
    CHECK(srv.state == SRV_RUNNING);                     /* Dora didn't hold it up: start_after */
    CHECK(srv.ms.players == 2 && nd.slot < 0);
    CHECK(srv.ms.team[na.slot] != srv.ms.team[nb.slot]);  /* both wanted the fortress: one each side */
    CHECK(!strcmp(na.next_map, "First Map") && na.map_len > 0);
    CHECK(nc.slot == 255 && netc_spectating(&nc));       /* Cleo watches */
    CHECK(na.time_limit == 600 && na.ms.time_limit == 600 && g3.time_limit == 600u * TICK_RATE);   /* everyone knows the limit */
    CHECK(tc > 250 && game_checksum(&g4) == srv.sums[(tc - 1) & 255]);   /* ...the same game */
    CHECK(na.delay >= NET_DELAY_MIN && na.delay <= NET_DELAY_MAX && na.delay == srv.delay);
    CHECK(na.rtt >= 0);
    rm_seed = srv.ms.seed; rm_team = srv.ms.team[na.slot];   /* (for the rematch, below) */
    {   /* the map Anna downloaded is kept for next time */
        char path[700];
        snprintf(path, sizeof path, "./net-%02x%02x%02x%02x%02x%02x%02x%02x.ofpmap", na.map_id[0], na.map_id[1], na.map_id[2],
                 na.map_id[3], na.map_id[4], na.map_id[5], na.map_id[6], na.map_id[7]);
        f = fopen(path, "rb");
        CHECK(f != NULL);
        if (f) fclose(f);
        remove(path);
    }
    /* chat during the match reaches the lobby and the watchers */
    netc_chat(&na, "gg from the match");
    for (start = net_ms(); net_ms() - start < 800;) {
        server_step(&srv); netc_step(&na); netc_step(&nb); netc_step(&nc); netc_step(&nd);
        play(&na, &g3, &ta, &sa, 0); play(&nb, &c1, &tb, &sb, 1); play(&nc, &g4, &tc, &scc, 2);
        net_sleep(1);
    }
    CHECK(seen_line(&nd, "gg from the match") && seen_line(&nc, "gg from the match"));
    /* Dora can't run commands until she logs in; then she ends the match and kicks Cleo */
    netc_chat(&nd, "/restart");
    netc_chat(&nd, "/login boss");
    for (start = net_ms(); net_ms() - start < 600;) { server_step(&srv); netc_step(&na); netc_step(&nb); netc_step(&nc); netc_step(&nd); net_sleep(1); }
    CHECK(seen_line(&nd, "for admins") && seen_line(&nd, "admin now"));
    CHECK(!seen_line(&na, "admin now"));                 /* (that was for Dora alone) */
    CHECK(srv.state == SRV_RUNNING);
    netc_chat(&nd, "/restart");
    for (start = net_ms(); net_ms() - start < 3500;) { server_step(&srv); netc_step(&na); netc_step(&nb); netc_step(&nc); netc_step(&nd); net_sleep(1); }
    CHECK(srv.state == SRV_LOBBY);
    CHECK(nd.next_extras == EXTRA_HARDPOINT);
    CHECK(!strcmp(nd.next_map, "Second Map"));          /* the rotation moved on */
    {   /* a rematch: Anna and Bert (who played) both want one: the same map, battlefield and sides */
        CHECK(na.state == NC_ENDED && nb.state == NC_ENDED);   /* (/restart: they were told it's over) */
        netc_rematch(&na, 1);
        netc_rematch(&nb, 1);
        for (start = net_ms(); net_ms() - start < 700;) { server_step(&srv); netc_step(&na); netc_step(&nb); netc_step(&nc); netc_step(&nd); net_sleep(1); }
        printf("  rematch: %d of %d want one; next: %s\n", na.rematch_yes, na.rematch_of, nd.next_map);
        CHECK(na.rematch_yes == 2 && na.rematch_of == 2 && nb.rematch_yes == 2);
        CHECK(nd.next_extras == 0);
        CHECK(!strcmp(nd.next_map, "First Map"));       /* (the same map again, not the rotation's next) */
        CHECK(seen_line(&nd, "Anna wants a rematch"));
        netc_back_to_lobby(&na); netc_back_to_lobby(&nb);   /* (as the game does, after the report) */
        netc_ready(&na, 1, TANK_LIGHT);
        netc_ready(&nb, 1, TANK_HEAVY);
        ta = tb = tc = 0; sa = sb = scc = 0;
        for (start = net_ms(); net_ms() - start < 15000 && !(srv.state == SRV_RUNNING && srv.tick > 50);) {   /* (ready, countdown, the map) */
            server_step(&srv); netc_step(&na); netc_step(&nb); netc_step(&nc); netc_step(&nd);
            play(&na, &g3, &ta, &sa, 0); play(&nb, &c1, &tb, &sb, 1);   /* (they load and play: the match runs; */
            play(&nc, &g4, &tc, &scc, 2);                                /*  Cleo watches it too) */
            net_sleep(1);
        }
        if (srv.state != SRV_RUNNING) {
            int q;
            printf("  (server state %d, Anna %d, Bert %d; fingerprints: server %08x, Anna %08x; seeds %u %u)\n", srv.state, na.state, nb.state,
                   (unsigned)srv.map_hash, (unsigned)match_hash(&na.ms, na.map_len ? na.map : NULL), (unsigned)srv.ms.seed, (unsigned)na.ms.seed);
            for (q = 0; q < MATCH_PLAYERS; q++)
                if (srv.in_match[q] >= 0) printf("  (in the match: %s, ready %d, gone %d)\n", srv.c[srv.in_match[q]].name, srv.c[srv.in_match[q]].ready, srv.c[srv.in_match[q]].gone);
        }
        CHECK(srv.state == SRV_RUNNING);
        printf("  rematch: seed %s, Anna's side %s\n", srv.ms.seed == rm_seed ? "the same" : "DIFFERENT",
               na.slot >= 0 && na.slot < MATCH_PLAYERS && srv.ms.team[na.slot] == rm_team ? "the same" : "DIFFERENT");
        CHECK(srv.ms.extras == 0);
        CHECK(srv.ms.seed == rm_seed && na.slot >= 0 && na.slot < MATCH_PLAYERS && srv.ms.team[na.slot] == rm_team);
        netc_chat(&nd, "/restart");
        for (start = net_ms(); net_ms() - start < 3500;) { server_step(&srv); netc_step(&na); netc_step(&nb); netc_step(&nc); netc_step(&nd); net_sleep(1); }
    }
    {
        char out[600];
        server_command(&srv, "ban Cleo cheating", out, sizeof out);   /* from the console */
        CHECK(strstr(out, "banned") != NULL);
    }
    for (start = net_ms(); net_ms() - start < 400;) { server_step(&srv); netc_step(&nc); net_sleep(1); }
    CHECK(nc.state == NC_FAILED && nc.reason == NR_KICKED);
    {   /* and she can't come back with the same key */
        uint8_t key[32];
        memcpy(key, nc.key, 32);
        netc_close(&nc);
        netc_open(&nc, "127.0.0.1", port, "Cleo", "", key, TANK_MEDIUM, STEER_TANK);
        for (start = net_ms(); net_ms() - start < 1000 && nc.state == NC_CONNECTING;) { server_step(&srv); netc_step(&nc); net_sleep(1); }
        CHECK(nc.state == NC_FAILED && nc.reason == NR_KICKED);
    }
    printf("  lobby: start_after, sides, watching, chat in a match, map cache, rotation, admin, ban: all fine\n");
    for (i = 0; i < 1; i++) {}
    netc_close(&na); netc_close(&nb); netc_close(&nc); netc_close(&nd);
    server_close(&srv);
}

/* audit regressions: a command is answered (so the game stops resending it, /login's password too);
 * changing weather travels; a bad cached map is fetched again; history past a full record isn't sent */
static void test_audit_fixes(void)
{
    static Server sv;
    static NetClient nq;
    ServerConfig cfg;
    uint16_t port = free_port();
    uint32_t start;
    char cache[] = "test-net-cache";
    server_default_config(&cfg);
    snprintf(cfg.admin_password, sizeof cfg.admin_password, "boss");
    cfg.min_players = 1; cfg.countdown_ms = 100; cfg.start_after_ms = 100;
    cfg.base.biome = BIOME_DESERT;
    cfg.weather_mode = WEATHER_CHANGING;                                /* changing weather, every match */
    CHECK(server_open(&sv, port, &cfg, NULL) == 0);
    CHECK(netc_open(&nq, "127.0.0.1", port, "Ann", "", NULL, TANK_LIGHT, STEER_TANK) == 0);
    for (start = net_ms(); net_ms() - start < 3000 && nq.state != NC_LOBBY;) { server_step(&sv); netc_step(&nq); net_sleep(1); }
    CHECK(nq.state == NC_LOBBY);
    netc_chat(&nq, "/login boss");
    for (start = net_ms(); net_ms() - start < 1000 && nq.out_text[0];) { server_step(&sv); netc_step(&nq); net_sleep(1); }
    CHECK(!nq.out_text[0]);                              /* answered: no longer sent */
    for (start = net_ms(); net_ms() - start < 1000 && !seen_line(&nq, "admin now");) { server_step(&sv); netc_step(&nq); net_sleep(1); }
    CHECK(seen_line(&nq, "admin now") && !seen_line(&nq, "boss") && nq.admin_pw[0] == 0);   /* (a challenge, a proof) */
    {   /* the password in the clear (an old game's /login): refused, admin_plain_login being off */
        snprintf(nq.out_text, sizeof nq.out_text, "/login boss");
        nq.out_cseq++;
        for (start = net_ms(); net_ms() - start < 1000 && !seen_line(&nq, "up-to-date game");) { server_step(&sv); netc_step(&nq); net_sleep(1); }
        CHECK(seen_line(&nq, "up-to-date game"));
    }
    netc_chat(&nq, "/maps");
    for (start = net_ms(); net_ms() - start < 1000 && nq.out_text[0];) { server_step(&sv); netc_step(&nq); net_sleep(1); }
    CHECK(!nq.out_text[0]);
    /* Lose a command's echo and enough subsequent lines to overflow the server's ring. */
    netc_chat(&nq, "/maps");
    server_step(&sv);
    {
        uint8_t packet[NET_MAX_PACKET];
        NetAddr from;
        while (net_recv(nq.sock, &from, packet, sizeof packet) > 0) {}
    }
    for (int i = 0; i < 35; i++) server_say(&sv, "line during a network interruption");
    server_say(&sv, "chat caught up");
    /* The server sends four lines per acknowledgement, and the client pings every 300 ms. */
    for (start = net_ms(); net_ms() - start < 3500 && !seen_line(&nq, "chat caught up");) {
        server_step(&sv); netc_step(&nq); net_sleep(1);
    }
    CHECK(seen_line(&nq, "chat caught up") && !nq.out_text[0]);
    /* a match in changing weather loads and runs (the game used to take it for clear: never loaded) */
    netc_ready(&nq, 1, TANK_LIGHT);
    for (start = net_ms(); net_ms() - start < 6000 && nq.state != NC_RUNNING;) { server_step(&sv); netc_step(&nq); net_sleep(1); }
    CHECK(nq.state == NC_RUNNING && nq.ms.weather == WEATHER_CHANGING && sv.ms.weather == WEATHER_CHANGING);
    /* the record stops (as at 2 hours, or out of memory): the match ends, and history beyond it isn't sent */
    for (start = net_ms(); net_ms() - start < 2000 && sv.tick < 60;) { server_step(&sv); netc_step(&nq); net_sleep(1); }
    {
        uint8_t hb[64];
        Pack k;
        uint32_t upto;
        sv.replay.stopped = 1;
        upto = sv.replay.ticks;
        for (start = net_ms(); net_ms() - start < 500 && sv.state == SRV_RUNNING && !sv.ended_at;) { server_step(&sv); net_sleep(1); }
        CHECK(sv.ended_at != 0);                         /* it ended at the record's end */
        CHECK(sv.tick <= upto + 1);
        pk_init(&k, hb, sizeof hb);
        pk_header(&k, NM_HISTREQ);
        pk_u32(&k, sv.replay.ticks + 1);                 /* past the record: nothing (it used to read past it) */
        net_send(nq.sock, &nq.server, hb, k.n);
        for (start = net_ms(); net_ms() - start < 100;) { server_step(&sv); net_sleep(1); }
    }
    netc_close(&nq);
    server_close(&sv);
    /* a cached map that isn't the match's: thrown away and fetched again */
    {
        static Server sm;
        static NetClient nm;
        static char mapfile[NET_MAP_MAX];
        FILE *f;
        char path[800];
        size_t n = 0;
        int i;
        server_default_config(&cfg);
        cfg.min_players = 1; cfg.countdown_ms = 100; cfg.start_after_ms = 100;
        if ((f = fopen(OFP_DATA_DIR "/maps/classic/dust-crossing.ofpmap", "rb")) != NULL) { n = fread(mapfile, 1, sizeof mapfile - 1, f); fclose(f); }
        if (!n) { printf("  (no dust-crossing map: the cache check skipped)\n"); return; }
        mapfile[n] = 0;
        cfg.map = mapfile;
        snprintf(cfg.map_name, sizeof cfg.map_name, "dust-crossing");
        port = free_port();
        CHECK(server_open(&sm, port, &cfg, NULL) == 0);
        mkdir(cache, 0755);
        CHECK(netc_open(&nm, "127.0.0.1", port, "Bo", "", NULL, TANK_LIGHT, STEER_TANK) == 0);
        snprintf(nm.cache_dir, sizeof nm.cache_dir, "%s", cache);
        {   /* the poisoned copy, under the right name */
            uint8_t h[32];
            sha256(mapfile, n, h);
            snprintf(path, sizeof path, "%s/net-", cache);
            for (i = 0; i < 8; i++) snprintf(path + strlen(path), sizeof path - strlen(path), "%02x", h[i]);
            strcat(path, ".ofpmap");
            if ((f = fopen(path, "wb")) != NULL) {
                fwrite(mapfile, 1, n, f);
                fseek(f, (long)(n / 2), SEEK_SET);
                fputc(mapfile[n / 2] == 'x' ? 'y' : 'x', f);   /* one byte off, the same size */
                fclose(f);
            }
        }
        for (start = net_ms(); net_ms() - start < 3000 && nm.state != NC_LOBBY;) { server_step(&sm); netc_step(&nm); net_sleep(1); }
        netc_ready(&nm, 1, TANK_LIGHT);
        for (start = net_ms(); net_ms() - start < 8000 && nm.state != NC_RUNNING;) { server_step(&sm); netc_step(&nm); net_sleep(1); }
        CHECK(nm.state == NC_RUNNING && !nm.map_cached);     /* fetched again, and it runs */
        CHECK(nm.map && !memcmp(nm.map, mapfile, n));
        if ((f = fopen(path, "rb")) != NULL) {               /* the cache holds the right one now */
            static char back[NET_MAP_MAX];
            size_t m = fread(back, 1, sizeof back, f);
            fclose(f);
            CHECK(m == n && !memcmp(back, mapfile, n));
        } else CHECK(!"the map wasn't cached");
        netc_close(&nm);
        server_close(&sm);
        remove(path);
        rmdir(cache);
    }
    printf("  audit fixes: commands answered, changing weather online, a full record ends the match, a bad cached map fetched again\n");
}

/* IPv6: the address logic everywhere (no IPv6 needed: names and literals only), and over IPv6 loopback
 * (::1) a server with an IPv6 player and an IPv4 one, where this system has IPv6 */
static void test_ipv6(void)
{
    NetAddr x, y, m;
    char text[80], host[64];
    int port, s;
    /* typed addresses */
    port = 1; net_split("[2001:db8::1]:29871", host, sizeof host, &port);
    CHECK(!strcmp(host, "2001:db8::1") && port == 29871);
    port = 2; net_split("2001:db8::1", host, sizeof host, &port);
    CHECK(!strcmp(host, "2001:db8::1") && port == 2);
    port = 3; net_split("example.org:1234", host, sizeof host, &port);
    CHECK(!strcmp(host, "example.org") && port == 1234);
    port = 4; net_split("10.0.0.1", host, sizeof host, &port);
    CHECK(!strcmp(host, "10.0.0.1") && port == 4);
    /* literals: IPv6 as IPv6 (its key in 0.x.x.x), ::ffff:a.b.c.d as the IPv4 address it is */
    if (net_resolve("[2001:db8::1]", 29870, &x) == 0) {
        CHECK(x.v6 == 1 && x.ip && x.ip < 0x01000000u && x.port == 29870);
        net_addr_str(&x, text, sizeof text);
        CHECK(!strcmp(text, "[2001:db8:0:0:0:0:0:1]:29870"));
        CHECK(net_resolve("2001:db8::2", 29870, &y) == 0 && y.ip == x.ip && !net_same(&x, &y));   /* (one /64) */
        CHECK(net_resolve("2001:db8:1::1", 29870, &y) == 0 && y.ip != x.ip);                     /* (another) */
        CHECK(net_resolve("::ffff:10.1.2.3", 7, &m) == 0 && !m.v6 && m.ip == 0x0a010203u);
        CHECK(net_resolve("10.1.2.3", 7, &y) == 0 && net_same(&m, &y));
    } else printf("  (no IPv6 literals from getaddrinfo here: the address checks skipped)\n");
    /* over IPv6 for real, if the system has it */
    s = net_open(0);
    if (s >= 0 && net_resolve("::1", net_port(s), &x) == 0 && x.v6 && net_send(s, &x, "x", 1) == 1) {
        static Server sv;
        static NetClient six, four;
        ServerConfig cfg;
        uint16_t p = free_port();
        uint32_t start;
        net_close(s);
        server_default_config(&cfg);
        CHECK(server_open(&sv, p, &cfg, NULL) == 0);
        CHECK(netc_open(&six, "::1", p, "Six", "", NULL, TANK_LIGHT, STEER_TANK) == 0);
        CHECK(netc_open(&four, "127.0.0.1", p, "Four", "", NULL, TANK_HEAVY, STEER_TANK) == 0);
        for (start = net_ms(); net_ms() - start < 3000 && (six.state != NC_LOBBY || four.state != NC_LOBBY);) {
            server_step(&sv); netc_step(&six); netc_step(&four); net_sleep(1);
        }
        CHECK(six.state == NC_LOBBY && four.state == NC_LOBBY && server_people(&sv) == 2);
        netc_chat(&six, "hello from IPv6");
        for (start = net_ms(); net_ms() - start < 1000 && !seen_line(&four, "hello from IPv6");) {
            server_step(&sv); netc_step(&six); netc_step(&four); net_sleep(1);
        }
        CHECK(seen_line(&four, "hello from IPv6"));
        netc_close(&six); netc_close(&four); server_close(&sv);
        printf("  IPv6: an IPv6 player and an IPv4 one on one server, chatting\n");
    } else {
        if (s >= 0) net_close(s);
        printf("  (no IPv6 on this system: the IPv6 match skipped; IPv4 sockets as before)\n");
    }
}

static void test_spectator_ending(void)
{
    ServerConfig cfg;
    uint16_t port = free_port();
    uint32_t start, played = 0, watched = 0;
    int player_started = 0, watcher_started = 0;
    server_default_config(&cfg);
    cfg.min_players = 1;
    cfg.countdown_ms = 100;
    cfg.start_after_ms = 100;
    cfg.allow_watching = 1;
    cfg.spectator_delay_s = 1;
    cfg.base.time_limit = 2;
    CHECK(server_open(&srv, port, &cfg, NULL) == 0);
    CHECK(netc_open(&n1, "127.0.0.1", port, "Player", "", NULL, TANK_LIGHT, STEER_TANK) == 0);
    CHECK(netc_open(&n2, "127.0.0.1", port, "Watcher", "", NULL, TANK_LIGHT, STEER_TANK) == 0);
    for (start = net_ms(); net_ms() - start < 3000 && (n1.state != NC_LOBBY || n2.state != NC_LOBBY);)
        pump(10);
    CHECK(n1.state == NC_LOBBY && n2.state == NC_LOBBY);
    netc_watch(&n2, 1);
    netc_ready(&n1, 1, TANK_LIGHT);
    for (start = net_ms(); net_ms() - start < 8000 && n2.state != NC_ENDED;) {
        server_step(&srv); netc_step(&n1); netc_step(&n2);
        play(&n1, &c1, &played, &player_started, 0);
        play(&n2, &c2, &watched, &watcher_started, 0);
        net_sleep(1);
    }
    CHECK(n2.state == NC_ENDED && watched > 0 && watched == srv.tick);
    CHECK(game_checksum(&c2) == game_checksum(&srv.game));
    {
        char reply[96];
        server_command(&srv, "restart", reply, sizeof reply);
        for (start = net_ms(); net_ms() - start < 850 && srv.state != SRV_LOBBY;) pump(10);
        CHECK(srv.state == SRV_LOBBY);
    }
    netc_close(&n1); netc_close(&n2);
    server_close(&srv);
}

int main(void)
{
    test_replay(0);
    test_replay(1);
    test_server_bot_commands();
    test_time_limit();
    test_weather_adaptive();
    test_extras();
    test_humans_out();
    test_map_settings();
    test_modes_online();
    test_ballot();
    test_long_reply();
    test_network();
    test_lobby_features();
    test_limits();
    test_join();
    test_versions();
    test_audit_fixes();
    test_spectator_ending();
    test_ipv6();
    if (failures) { printf("%d net test(s) FAILED\n", failures); return 1; }
    printf("all replay and network tests passed\n");
    return 0;
}
