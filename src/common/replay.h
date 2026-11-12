/* SPDX-License-Identifier: MIT
 * Replays: how a match began plus every tick's buttons. Because the core is
 * deterministic that is all it takes to play a match again exactly, a few
 * KB for a whole battle. The checksums recorded along the way prove it.
 *
 * File format (.ofprep, text):
 *   OFPREPLAY 1            local AI runs during playback
 *   OFPREPLAY 2            every tick includes authoritative bot commands
 *   seed=12345  biome=0  time=0  garages=4  versus=0  ai=2  players=1
 *   player=0 cls=1 team=0 steer=0
 *   map=<length>            followed by the .ofpmap text (only for map files)
 *   ticks
 *   <run> <hex>             <run> ticks of the same buttons, two hex digits a tank
 *   check <tick> <checksum> (every 250 ticks)
 *   end
 */
#ifndef OFP_REPLAY_H
#define OFP_REPLAY_H

#include <stddef.h>
#include "core/match.h"

#define REPLAY_CHECK_EVERY 250
#define REPLAY_MAX_TICKS (2u * 60u * 60u * TICK_RATE)

typedef struct {
    MatchSetup setup;
    char      *map;                  /* malloc'd .ofpmap text, or NULL */
    int        stopped;              /* recording reached its limit or ran out of memory */
    int        tanks;                /* buttons recorded per tick */
    uint8_t   *inputs;               /* ticks * tanks */
    uint32_t   ticks, cap;
    uint32_t  *checks;               /* checksum at every REPLAY_CHECK_EVERY ticks (index = tick / N) */
    uint32_t   nchecks, ccap;
    char       names[MATCH_PLAYERS][16]; /* who played (optional; "" = not known) */
} Replay;

void replay_begin(Replay *r, const MatchSetup *ms, const char *map_text, int tanks);
/* record one tick: the buttons that went into game_tick, then the game after it */
void replay_tick(Replay *r, const InputBits *in, const Game *after);
void replay_free(Replay *r);
int  replay_save(const Replay *r, const char *path);       /* 0 = ok */
int  replay_load(Replay *r, const char *path);             /* 0 = ok */
/* play it back into g: returns the tick of the first checksum mismatch, or -1
 * if every recorded checksum matched (and g ends where the match ended) */
long replay_verify(const Replay *r, Game *g);

#endif
