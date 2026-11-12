/* SPDX-License-Identifier: MIT
 * ofp68k: the Amiga port's first program, a Shell tool (no screen yet):
 *
 *   ofp68k verify FILE.ofprep   play a replay, check every checksum: the 68k plays exactly as the desktop
 *   ofp68k bench [SECONDS]      a 4 a side match between computer players: ticks a second (the game needs 50)
 *   ofp68k net HOST [PORT]      ask a server about itself, over bsdsocket.library (Roadshow, AmiTCP, Miami)
 *
 * It uses the game's own interfaces only (the core, the replays, net.h), so it builds and runs on any
 * machine; on AmigaOS the network is src/platform/amiga/net_amiga.c's, the clock amiga_clock.c's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/match.h"
#include "common/replay.h"
#include "common/net.h"

#ifdef __amigaos__
unsigned long __stack = 65536;               /* (libnix: a stack this big; the Shell's 4 KB isn't enough) */
#endif
static const char version[] = "$VER: ofp68k 0.1 (05.11.2026) OpenFP for AmigaOS";

static Game g;                               /* (a big struct: not on the stack) */

static int verify(const char *path)
{
    static Replay r;
    long bad;
    uint32_t t0 = net_ms();
    if (replay_load(&r, path) != 0) { printf("can't read the replay %s\n", path); return 20; }
    bad = replay_verify(&r, &g);
    if (bad >= 0) { printf("OUT OF STEP: %s differs from its recording at tick %ld\n", path, bad); replay_free(&r); return 10; }
    printf("%s: %u ticks, all %u checksums match (%lu s)\n", path, (unsigned)r.ticks, (unsigned)r.nchecks,
           (unsigned long)((net_ms() - t0) / 1000u));
    replay_free(&r);
    return 0;
}

/* bench: a match between computer players for a while; how many ticks a second this CPU manages (the
 * game needs 50) and the slowest tick (a stutter). Options: the mode, the map's size, tanks a side */
static int bench(int seconds, uint8_t extras, int size, int half)
{
    MatchSetup ms;
    InputBits in[MAX_TANKS];
    uint32_t t0, now, ticks = 0, worst = 0, before;
    int a;
    memset(&ms, 0, sizeof ms);
    memset(in, 0, sizeof in);
    if (half < 1) half = 1;
    if (half > 4) half = 4;
    ms.seed = 2026; ms.camp_garages = 4; ms.ai_level = AI_NORMAL; ms.versus = 1;
    ms.extras = extras;
    ms.map_size = (uint8_t)(size + 1);
    ms.players = (uint8_t)(2 * half);
    for (a = 0; a < ms.players; a++) {                  /* the computer driving every tank */
        ms.team[a] = (uint8_t)(a < half ? TEAM_PLAYER : TEAM_ENEMY);
        ms.cls[a] = (uint8_t)((a % half) % TANK_CLASS_COUNT);
        ms.bot[a] = AI_NORMAL;
    }
    printf("bench: %s, %d a side, the computer driving, %d s ...\n", map_size_name[size], half, seconds);
    t0 = net_ms();
    a = match_start(&g, &ms, NULL);
    if (a != 0) { printf("the match won't start%s\n", a == -2 ? ": that size is too big for this build's grid" : ""); return 20; }
    printf("  the match set up in %lu ms\n", (unsigned long)(net_ms() - t0));
    t0 = net_ms();
    do {
        before = net_ms();
        game_tick(&g, in);
        now = net_ms();
        if (now - before > worst) worst = now - before;
        ticks++;
    } while (now - t0 < (uint32_t)seconds * 1000u && g.winner < 0);
    {
        uint32_t ms_spent = now - t0 ? now - t0 : 1, per_s = ticks * 1000u / ms_spent;
        printf("  %lu ticks in %lu ms: %lu ticks a second (needs 50: %s); the slowest tick %lu ms (a tick has 20)\n",
               (unsigned long)ticks, (unsigned long)ms_spent, (unsigned long)per_s,
               per_s >= 60 ? "fast enough" : per_s >= 50 ? "just" : "too slow", (unsigned long)worst);
    }
    return 0;
}

static int net(const char *host, int port)
{
    uint8_t buf[NET_MAX_PACKET];
    NetAddr to, from;
    Pack k;
    int s, n, tries;
    if (net_init() != 0) { printf("no TCP/IP stack (bsdsocket.library): start Roadshow, AmiTCP or Miami first\n"); return 20; }
    if ((s = net_open(0)) < 0) { printf("can't open a UDP socket\n"); return 20; }
    if (net_resolve(host, (uint16_t)port, &to) != 0) { printf("can't find %s\n", host); net_close(s); return 20; }
    net_addr_str(&to, (char *)buf, sizeof buf);
    printf("asking %s about itself ...\n", (char *)buf);
    for (tries = 0; tries < 3; tries++) {
        uint32_t t0 = net_ms();
        pk_init(&k, buf, sizeof buf);
        pk_header(&k, NM_QUERY);
        while (k.n < NET_QUERY_MIN) pk_u8(&k, 0);   /* (the question as big as the answer: no amplifying) */
        net_send(s, &to, buf, k.n);
        while (net_ms() - t0 < 1500u) {
            n = net_recv(s, &from, buf, sizeof buf);
            if (n > 0 && net_same(&from, &to)) {
                ServerInfo info;
                pk_init(&k, buf, n);
                if (uk_header(&k) != NM_INFO) continue;
                uk_info(&k, &info);
                printf("  \"%s\": %u of %u players, %s, %s%s; protocol %lu (ours %d)%s\n", info.name,
                       (unsigned)info.players, (unsigned)info.max_players, info.world, info.running ? "playing " : "waiting",
                       info.running ? info.map : "", (unsigned long)(info.build >> 16), NET_PROTOCOL,
                       net_compatible(info.build, NET_PROTOCOL_OLDEST) ? "" : " (can't play: a different version)");
                printf("  answered in %lu ms\n", (unsigned long)(net_ms() - t0));
                net_close(s);
                return 0;
            }
            net_sleep(20);
        }
    }
    printf("no answer from %s:%d\n", host, port);
    net_close(s);
    return 5;
}

int main(int argc, char **argv)
{
    (void)version;
    if (argc >= 3 && !strcmp(argv[1], "verify")) {
        int i, worst = 0;
        for (i = 2; i < argc; i++) { int r = verify(argv[i]); if (r > worst) worst = r; }
        return worst;
    }
    if (argc >= 2 && !strcmp(argv[1], "bench")) {
        /* ofp68k bench [SECONDS] [small|medium|large] [TANKS-A-SIDE] [flag|conquest|deathmatch|...] */
        int secs = argc >= 3 ? atoi(argv[2]) : 20, size = SIZE_SMALL, half = 1, k;
        uint8_t x = 0;
        static const struct { const char *n; uint8_t x; } m[] = { { "flag", 0 }, { "conquest", EXTRA_CONQUEST },
            { "hardpoint", EXTRA_HARDPOINT }, { "deathmatch", EXTRA_TDM }, { "killconfirmed", EXTRA_KILLCONF },
            { "annihilation", EXTRA_ANNIHILATION }, { "warlord", EXTRA_WARLORD } };
        if (argc >= 4) for (k = 0; k < MAP_SIZES; k++) if (!strcmp(argv[3], map_size_name[k])) size = k;
        if (argc >= 5) half = atoi(argv[4]);
        if (argc >= 6) for (k = 0; k < 7; k++) if (!strcmp(argv[5], m[k].n)) x = m[k].x;
        return bench(secs < 1 ? 20 : secs, x, size, half);
    }
    if (argc >= 3 && !strcmp(argv[1], "net")) return net(argv[2], argc >= 4 ? atoi(argv[3]) : 29870);
    printf("%s\n\n"
           "  ofp68k verify FILE.ofprep ...   play replays, check every checksum (the 68k plays as the desktop)\n"
           "  ofp68k bench [SECONDS] [small|medium|large] [TANKS-A-SIDE] [MODE]\n"
           "                                  the computer driving: ticks a second (needs 50), the slowest tick\n"
           "  ofp68k net HOST [PORT]          ask a server about itself, over bsdsocket.library\n", version + 6);
    return 5;
}
