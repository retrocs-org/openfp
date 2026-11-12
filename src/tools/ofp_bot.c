/* SPDX-License-Identifier: MIT
 * ofp_bot: a headless player, for soak tests (tools/soak.sh). It joins a server like the
 * game does, gets ready, plays its matches in lockstep (buttons of its own, the official
 * ticks, a checksum every second, catching up after an out-of-step), readies again after
 * each match, and once a minute prints one line of what it saw:
 *
 *   ofp_bot HOST[:PORT] [--name NAME] [--minutes N] [--password P]
 *
 *   bot NAME t=SECONDS state=... matches=N ticks=N rtt=MS max_rtt=MS resyncs=N reconnects=N
 *
 * It ends after --minutes (0: never), or when the server sends it away for good. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/client.h"
#include "common/net.h"
#include "core/match.h"
#include "core/game.h"

static Game g;
static NetClient c;

int main(int argc, char **argv)
{
    const char *host = NULL, *name = "SoakBot", *password = "";
    char hostbuf[200];
    uint16_t port = NET_PORT;
    long minutes = 0;
    int i, playing = 0, matches = 0, resyncs = 0, reconnects = 0, max_rtt = 0, was_running = 0;
    uint32_t net_next = 0, start, last_report, next_input, ticks_played = 0;
    unsigned seed = 1;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(argv[i], "--minutes") && i + 1 < argc) minutes = atol(argv[++i]);
        else if (!strcmp(argv[i], "--password") && i + 1 < argc) password = argv[++i];
        else host = argv[i];
    }
    if (!host) { fprintf(stderr, "usage: ofp_bot HOST[:PORT] [--name NAME] [--minutes N] [--password P]\n"); return 2; }
    {   /* HOST, HOST:PORT, [IPv6]:PORT or a bare IPv6 address */
        int pt = port;
        net_split(host, hostbuf, sizeof hostbuf, &pt);
        port = (uint16_t)pt;
    }
    for (i = 0; name[i]; i++) seed = seed * 31u + (unsigned char)name[i];
    if (netc_open(&c, hostbuf, port, name, password, NULL, (int)(seed % 3), STEER_TANK) != 0) {
        fprintf(stderr, "ofp_bot: can't reach %s\n", host);
        return 1;
    }
    start = last_report = next_input = net_ms();
    for (;;) {
        uint32_t now = net_ms();
        int st = netc_step(&c);
        if (minutes > 0 && now - start > (uint32_t)minutes * 60000u) break;
        if (st == NC_FAILED) {                           /* dropped: come back, as the game does */
            if (!netc_can_retry(&c)) { printf("bot %s: sent away for good: %s\n", name, c.status); fflush(stdout); return 1; }
            netc_close(&c);
            net_sleep(2000);
            reconnects++;
            if (netc_open(&c, hostbuf, port, name, password, NULL, (int)(seed % 3), STEER_TANK) != 0) continue;
            playing = 0;
            continue;
        }
        if (st == NC_LOBBY && !c.ready) netc_ready(&c, 1, (int)(seed % 3));
        if (st == NC_RUNNING && !playing) {              /* our match is on */
            if (match_start(&g, &c.ms, c.map_len ? c.map : NULL) != 0) { fprintf(stderr, "bot %s: the match wouldn't start\n", name); return 1; }
            playing = 1;
            net_next = 0;
            next_input = now;
        }
        if (playing && st == NC_RUNNING) {
            InputBits set[MAX_TANKS];
            if (netc_take_restart(&c)) {                 /* out of step: from the start again */
                match_start(&g, &c.ms, c.map_len ? c.map : NULL);
                net_next = 0;
                resyncs++;
            }
            while ((int32_t)(now - next_input) >= 0) {    /* our buttons, 50 a second, a few ticks ahead */
                next_input += 20;
                if (c.slot != 255 && c.mine_hi <= net_next + (uint32_t)c.delay + 2) {
                    uint32_t t = c.mine_hi;
                    seed = seed * 1103515245u + 12345u;
                    netc_input(&c, t, (InputBits)((t / 40) % 5 == 4 ? IN_LEFT | IN_UP : ((seed >> 16) & 7) == 0 ? IN_FIRE | IN_UP : IN_UP));
                }
            }
            while (netc_tick(&c, net_next, set)) {       /* the official ticks, as they come */
                game_tick(&g, set);
                if ((net_next + 1) % NET_CHECK_EVERY == 0 && c.slot != 255) netc_check(&c, net_next, game_checksum(&g));
                net_next++;
                ticks_played++;
            }
            was_running = 1;
        }
        if (st == NC_ENDED || (st == NC_LOBBY && was_running)) {   /* the match is over: back to the lobby */
            if (was_running) matches++;
            was_running = 0;
            playing = 0;
            if (st == NC_ENDED) netc_back_to_lobby(&c);
        }
        if (c.rtt > max_rtt) max_rtt = c.rtt;
        if (now - last_report >= 60000u) {
            static const char *sn[] = { "connecting", "lobby", "loading", "running", "ended", "failed" };
            last_report = now;
            printf("bot %s t=%u state=%s matches=%d ticks=%u rtt=%d max_rtt=%d resyncs=%d reconnects=%d\n", name,
                   (unsigned)((now - start) / 1000), sn[st >= 0 && st <= 5 ? st : 5], matches, (unsigned)ticks_played, c.rtt,
                   max_rtt, resyncs, reconnects);
            fflush(stdout);
            max_rtt = 0;
        }
        net_sleep(2);
    }
    printf("bot %s: done after %ld minutes, %d matches, %u ticks, %d resyncs, %d reconnects\n", name, minutes, matches,
           (unsigned)ticks_played, resyncs, reconnects);
    netc_close(&c);
    return 0;
}
