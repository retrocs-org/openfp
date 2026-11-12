/* SPDX-License-Identifier: MIT
 * Fuzz testing what the game reads from the web: the server list (the listing's text
 * answer) and HTTP responses. Mutated real answers, then random ones. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/sdl2/netui.h"
#include "common/http.h"

static Browser b;
static uint32_t rnd = 0x2545F491u;
static uint32_t next(void) { rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5; return rnd; }

static const char *seed_list =
    "10.0.0.1\t29870\tRetroCS Arena\t3\t8\t0\t1\tversus\tclassic\trandom\t524545\t12\tAnna,Bert\n"
    "game.example.org\t29871\tNight Ops\t0\t4\t1\t0\ttogether\tscifi\tNeon Grid\t524545\t40\t\n"
    "10.0.0.2\t29870\tRated Arena\t2\t8\t0\t0\tversus\tclassic\trandom\t524545\t15\tCara,Dan\tflag\t1500\n";
static const char *seed_http = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\n0.4.0\thttps://openfp.retrocs.org/play/\n";

int main(int argc, char **argv)
{
    static char buf[70000], out[4096];
    long iters = argc > 1 ? atol(argv[1]) : 20000, i;
    for (i = 0; i < iters; i++) {
        int n, k, muts, http = (int)(next() % 3 == 0);
        const char *seed = http ? seed_http : seed_list;
        if (next() % 8 == 0) {                           /* random bytes */
            n = (int)(next() % 3000);
            for (k = 0; k < n; k++) buf[k] = (char)(next() % 4 ? (next() % 3 ? 0x20 + next() % 95 : '\t') : (next() & 1 ? '\n' : (char)next()));
        } else {                                         /* a real answer, mutated */
            n = (int)strlen(seed);
            memcpy(buf, seed, (size_t)n);
            muts = (int)(next() % 10);
            for (k = 0; k < muts; k++) {
                int at = n ? (int)(next() % (uint32_t)n) : 0;
                switch (next() % 6) {
                case 0: buf[at] ^= (char)(1u << (next() % 8)); break;
                case 1: buf[at] = '\t'; break;
                case 2: buf[at] = '\n'; break;
                case 3: n = at; break;
                case 4: if (n < (int)sizeof buf - 2000) { int r; for (r = 0; r < 300 + (int)(next() % 1500); r++) buf[n++] = (char)('A' + next() % 26); } break;
                default: if (n < (int)sizeof buf - 400) { int r; for (r = 0; r < 300; r++) buf[n++] = (char)(next() % 2 ? '\t' : '9'); } break;
                }
            }
        }
        buf[n] = 0;
        if (http) http_parse_response(buf, out, (size_t)(1 + next() % sizeof out));
        else { browser_parse(&b, buf); browser_mark(&b); browser_pick(&b); }
    }
    printf("fuzz_ui: %ld server lists and HTTP answers: no crash\n", iters);
    {   /* QUICK MATCH, ranked: two open lobbies alike but for their people's rating: mine near one, that one */
        static char list[] =
            "a.example\t1\tStrong\t2\t8\t0\t0\tversus\tclassic\trandom\t0\t20\tX,Y\tflag\t2100\n"
            "b.example\t2\tNovice\t2\t8\t0\t0\tversus\tclassic\trandom\t0\t20\tZ,W\tflag\t1250\n";
        int pick;
        memset(&b, 0, sizeof b);
        browser_parse(&b, list);
        b.my_rating = 1300;
        pick = browser_pick(&b);
        if (b.n != 2 || pick < 0 || strcmp(b.e[pick].name, "Novice") || b.e[pick].rating != 1250) {
            printf("fuzz_ui: ranked QUICK MATCH picked %s (%d servers)\n", pick >= 0 ? b.e[pick].name : "none", b.n);
            return 1;
        }
        b.my_rating = 2050;
        if (strcmp(b.e[browser_pick(&b)].name, "Strong")) { printf("fuzz_ui: ranked QUICK MATCH, a strong player: not Strong\n"); return 1; }
        b.e[0].build = net_build_id() + (1u << 16);
        b.e[1].build = net_build_id();
        if (browser_pick(&b) != 1) return 1;
        b.e[1].build = net_build_id() - (1u << 16);
        if (browser_pick(&b) != -1) return 1;
        printf("fuzz_ui: QUICK MATCH sorts by rating and skips version mismatches\n");
    }
    {   /* L in FIND SERVERS: the newest of a server's matches that has a replay (the listing's JSON, pretty) */
        const char *json = "{\n    \"matches\": [\n        {\n            \"id\": 2,\n            \"players\": [ { \"name\": \"Bert\" } ],\n"
                           "            \"has_replay\": 0\n        },\n        {\n            \"id\": 1,\n            \"has_replay\": 1\n        }\n    ]\n}";
        if (browser_replay_id(json) != 1 || browser_replay_id("{\"matches\":[]}") != 0 ||
            browser_replay_id("{\"matches\":[{\"id\":7,\"has_replay\":1}]}") != 7) {
            printf("fuzz_ui: the last match's replay: wrong id\n");
            return 1;
        }
        printf("fuzz_ui: the last match's replay: found\n");
    }
    return 0;
}
