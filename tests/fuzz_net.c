/* SPDX-License-Identifier: MIT
 * Fuzz testing what strangers can send: packets to a server (from an unknown address,
 * and from a player who's in, in the lobby and during a match), packets to a client in
 * each of its states, map files (they come over the network) and replay files.
 *
 * Two ways to run it:
 *   fuzz_net [iterations] [seed]   a mutating fuzzer of its own (ctest runs a short one).
 *                                  Build with -DOFP_FUZZ=ON for address and undefined-
 *                                  behaviour checks; a crash prints the input that did it.
 *   with libFuzzer (clang -fsanitize=fuzzer): LLVMFuzzerTestOneInput below, the first
 *                                  byte choosing the target.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/server.h"
#include "common/client.h"
#include "common/replay.h"
#include "common/net.h"
#include "core/match.h"
#include "core/game.h"
#include "common/http.h"

static Server srv;
static NetClient cl;
static Game g;
static int ready;
static uint32_t player_ip;
static uint16_t player_port;

/* ---- a server with someone in it, and a client that got a little way ---------------- */
static void setup(void)
{
    ServerConfig cfg;
    uint32_t start;
    uint16_t port = (uint16_t)(40000 + (net_ms() % 4000));
    int i;
    if (ready) return;
    ready = 1;
    server_default_config(&cfg);
    cfg.min_players = 1;
    cfg.countdown_ms = 100;
    snprintf(cfg.admin_password, sizeof cfg.admin_password, "fuzz");
    if (server_open(&srv, port, &cfg, NULL) != 0) { fprintf(stderr, "no server\n"); exit(2); }
    netc_open(&cl, "127.0.0.1", port, "Fuzzy", "", NULL, TANK_LIGHT, STEER_TANK);
    for (start = net_ms(); net_ms() - start < 3000 && cl.state != NC_LOBBY;) { server_step(&srv); netc_step(&cl); net_sleep(1); }
    for (i = 0; i < NET_LOBBY_MAX; i++)                  /* the fuzzer pretends to be this player */
        if (srv.c[i].used && srv.c[i].authed) { player_ip = srv.c[i].addr.ip; player_port = srv.c[i].addr.port; }
}

static void into_match(void)
{
    uint32_t start;
    netc_ready(&cl, 1, TANK_LIGHT);
    for (start = net_ms(); net_ms() - start < 4000 && srv.state != SRV_RUNNING;) { server_step(&srv); netc_step(&cl); net_sleep(1); }
}

/* ---- the targets -------------------------------------------------------------------- */
static void fuzz_server(const uint8_t *d, int n, int as_player)
{
    uint8_t pkt[NET_MAX_PACKET];
    if (n > NET_MAX_PACKET - 4) n = NET_MAX_PACKET - 4;
    if (n < 0) n = 0;
    pkt[0] = 'O'; pkt[1] = 'F'; pkt[2] = NET_PROTOCOL;   /* (a good header, so the handlers get to look) */
    memcpy(pkt + 3, d, (size_t)n);
    if (as_player) server_fuzz_packet(&srv, pkt, n + 3, player_ip, player_port);
    else server_fuzz_packet(&srv, pkt, n + 3, 0x0A000001u + (uint32_t)(d[0] & 7), 5000);
    server_step(&srv);
}

static void fuzz_client(const uint8_t *d, int n)
{
    uint8_t pkt[NET_MAX_PACKET];
    if (n > NET_MAX_PACKET - 4) n = NET_MAX_PACKET - 4;
    if (n < 0) n = 0;
    pkt[0] = 'O'; pkt[1] = 'F'; pkt[2] = NET_PROTOCOL;
    memcpy(pkt + 3, d, (size_t)n);
    netc_fuzz_packet(&cl, pkt, n + 3);
    if (cl.state == NC_FAILED || cl.state == NC_ENDED) {   /* (keep it in a state worth fuzzing) */
        cl.state = (d[0] & 1) ? NC_LOBBY : NC_RUNNING;
        cl.reason = 0;
    }
}

static void fuzz_map(const uint8_t *d, int n)
{
    static char text[70000];
    MatchSetup ms;
    if (n > (int)sizeof text - 1) n = (int)sizeof text - 1;
    memcpy(text, d, (size_t)n);
    text[n] = 0;
    memset(&ms, 0, sizeof ms);
    ms.seed = 1; ms.players = 1; ms.camp_garages = 4;
    if (match_start(&g, &ms, text) == 0) {
        InputBits in[MAX_TANKS];
        int t;
        memset(in, 0, sizeof in);
        for (t = 0; t < 20; t++) game_tick(&g, in);      /* and it must also play */
    }
}

static void fuzz_replay(const uint8_t *d, int n)
{
    static Replay r;
    FILE *f = fopen("fuzz.ofprep.tmp", "wb");
    if (!f) return;
    fwrite(d, 1, (size_t)n, f);
    fclose(f);
    if (replay_load(&r, "fuzz.ofprep.tmp") == 0) {
        if (r.ticks < 3000) { static Game back; replay_verify(&r, &back); }
        replay_free(&r);
    }
    remove("fuzz.ofprep.tmp");
}

static void one(const uint8_t *d, int n)
{
    if (n < 1) return;
    switch (d[0] % 7) {
    case 0: fuzz_server(d + 1, n - 1, 0); break;         /* from anyone */
    case 1: case 2: fuzz_server(d + 1, n - 1, 1); break; /* from a player who's in */
    case 3: fuzz_client(d + 1, n - 1); break;
    case 4: fuzz_map(d + 1, n - 1); break;
    case 6: {                                            /* an HTTP answer (the listing, webhooks) */
        static char raw[4100], out[512];
        int m = n - 1 > 4096 ? 4096 : n - 1;
        memcpy(raw, d + 1, (size_t)m);
        raw[m] = 0;
        http_parse_response(raw, out, sizeof out);
        break;
    }
    default: fuzz_replay(d + 1, n - 1); break;
    }
}

#ifdef OFP_LIBFUZZER
int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
    setup();
    one(d, (int)n);
    return 0;
}
#else
/* ---- our own: seeds that look like the real thing, then mutations of them ----------- */
static uint32_t rnd = 0x9E3779B9u;
static uint32_t next(void) { rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5; return rnd; }

static const char *map_seed =
    "OFPMAP 1\nname=Fuzz\nbiome=classic\ntime=day\nsize=16 16\n"
    "................\n..##....~~~~....\n..##....~~~~....\n................\n"
    "....GGG.........\n................\n";
static const char *replay_seed =
    "OFPREPLAY 1\nseed=5 biome=0 time=0 garages=4 versus=0 ai=2 players=1 tanks=2\n"
    "player=0 cls=1 team=0 steer=0 bot=0\nticks=60\n0 60 0,0\nCHECK 50 12345678\n";

int main(int argc, char **argv)
{
    long iters = argc > 1 ? atol(argv[1]) : 20000, i;
    static uint8_t buf[4096];
    int phase;
    if (argc > 2) rnd = (uint32_t)atol(argv[2]);
    setup();
    for (phase = 0; phase < 2; phase++) {               /* in the lobby, then during a match */
        if (phase == 1) into_match();
        for (i = 0; i < iters / 2; i++) {
            int n, k, muts;
            uint32_t pick = next() % 10;
            if (pick < 7) {                              /* a packet: a real message type, random fields */
                buf[0] = (uint8_t)(next() % 3 == 0 ? 3 : (next() & 1) ? 1 : 0);
                buf[1] = (uint8_t)(1 + next() % 26);     /* (the type byte) */
                n = 2 + (int)(next() % 300);
                for (k = 2; k < n; k++) buf[k] = (uint8_t)((next() % 4) ? next() : (next() % 3 ? 0 : 0xFF));
            } else if (pick == 7 && next() % 2) {        /* an HTTP answer */
                static const char *hs = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nok 12";
                buf[0] = 6;
                n = 1 + (int)strlen(hs);
                memcpy(buf + 1, hs, (size_t)n - 1);
            } else if (pick < 9) {                       /* a map file, mutated */
                buf[0] = 4;
                n = 1 + (int)strlen(map_seed);
                memcpy(buf + 1, map_seed, (size_t)n - 1);
            } else {                                     /* a replay, mutated */
                buf[0] = 5;
                n = 1 + (int)strlen(replay_seed);
                memcpy(buf + 1, replay_seed, (size_t)n - 1);
            }
            muts = (int)(next() % 8);
            for (k = 0; k < muts; k++) {                 /* flip, replace, insert digits, cut, grow */
                int at = 1 + (int)(next() % (uint32_t)(n > 1 ? n - 1 : 1));
                switch (next() % 5) {
                case 0: buf[at] ^= (uint8_t)(1u << (next() % 8)); break;
                case 1: buf[at] = (uint8_t)next(); break;
                case 2: buf[at] = (uint8_t)('0' + next() % 10); break;
                case 3: n = at; break;
                default: if (n < (int)sizeof buf - 8) { int g2; for (g2 = 0; g2 < 6; g2++) buf[n++] = (uint8_t)("99999\n"[g2]); } break;
                }
            }
            one(buf, n);
            if (i % 5000 == 0) { netc_step(&cl); server_step(&srv); }
        }
    }
    printf("fuzz: %ld inputs (packets to the server and the client, maps, replays): no crash\n", iters);
    netc_close(&cl);
    server_close(&srv);
    return 0;
}
#endif
