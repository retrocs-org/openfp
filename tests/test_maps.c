/* SPDX-License-Identifier: MIT
 * Every bundled map: it loads, it plays a mode it is made for (modes=), an arena has no camps,
 * a map with camps has its garages. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/match.h"
#include "core/game.h"
static Game g;
static char text[300000];
int main(int argc, char **argv) {
    int a, bad = 0;
    for (a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "r"); size_t n; MatchSetup ms; InputBits in[MAX_TANKS]; int k, t, gar = 0, mode = EXTRA_TDM;
        if (!f) continue; n = fread(text, 1, sizeof text - 1, f); text[n] = 0; fclose(f);
        if (strstr(text, "modes=annihilation")) mode = EXTRA_ANNIHILATION;
        else if (strstr(text, "modes=conquest")) mode = EXTRA_CONQUEST;
        else if (strstr(text, "modes=flag")) mode = 0;
        memset(&ms, 0, sizeof ms); memset(in, 0, sizeof in);
        ms.seed = 5; ms.camp_garages = 4; ms.players = 4; ms.versus = 1; ms.extras = (uint8_t)mode;
        for (k = 0; k < 4; k++) { ms.team[k] = (uint8_t)(k & 1); ms.cls[k] = TANK_MEDIUM; ms.bot[k] = AI_NORMAL; }
        if (match_start(&g, &ms, text) != 0) { printf("%s: DOESN'T LOAD\n", argv[a]); bad++; continue; }
        for (k = 0; k < g.map.num_st; k++) gar += g.map.st[k].type == ST_GARAGE;
        for (t = 0; t < 3000 && g.winner < 0 && !g.time_up; t++) game_tick(&g, in);
        printf("%-30s %-13s arena %d, garages %d, %s\n", strrchr(argv[a], '/') + 1, mode == EXTRA_ANNIHILATION ? "annihilation" : mode == EXTRA_CONQUEST ? "conquest" : mode ? "deathmatch" : "flag",
               g.map.arena, gar, t < 50 ? "ENDED AT ONCE" : "plays");
        if (t < 50 || (mode == EXTRA_ANNIHILATION || mode == EXTRA_TDM) != (g.map.arena == 1) || (!g.map.arena && gar < 6)) bad++;

    }
    printf("%d maps with a problem\n", bad);
    return bad != 0;
}
