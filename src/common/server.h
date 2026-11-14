/* SPDX-License-Identifier: MIT
 * The match server: a lobby (chat, players getting ready, the leaderboard)
 * and lockstep matches with the server as referee (docs/NETWORK.md).
 *
 * People join the lobby (with the password, if there is one), chat, pick a
 * tank and say they're ready. When everyone there is ready (and there are
 * enough of them), a short countdown, then up to four of them play: the
 * server collects their buttons each tick, makes the official set, runs the
 * game itself, broadcasts the set and compares everyone's checksums with its
 * own. After the match: the results (to the hooks: statistics, the
 * leaderboard), and back to the lobby. Non-blocking: call server_step often. */
#ifndef OFP_SERVER_H
#define OFP_SERVER_H

#include "common/net.h"
#include "common/replay.h"

enum { SRV_LOBBY, SRV_COUNTDOWN, SRV_LOADING, SRV_RUNNING, SRV_CLOSED };
enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };

typedef struct {
    char    name[NET_NAME];
    int     matches, wins, kills;
    long    score;
    int     rating;                    /* Elo: 1500 to begin with */
} LeaderEntry;

typedef struct {                       /* how a match went, for the statistics */
    int      players, winner;          /* winner: TEAM_*, or -1 (a draw, or abandoned) */
    int      draw;                     /* it ended without a winner (the time up, level; nobody left), not abandoned */
    uint32_t ticks;
    char     name[MATCH_PLAYERS][NET_NAME];
    int      team[MATCH_PLAYERS], won[MATCH_PLAYERS], kills[MATCH_PLAYERS], score[MATCH_PLAYERS];
    char     account_id[MATCH_PLAYERS][33];
    int      bot[MATCH_PLAYERS];       /* a computer player (its AI level): not in the statistics */
    int      versus, ai_level;         /* the kind of match, and the computer tank's level (together) */
    int      late[MATCH_PLAYERS];      /* joined the match on its way (not rated) */
    int      saved[MATCH_PLAYERS], lost[MATCH_PLAYERS], soldiers[MATCH_PLAYERS], helis[MATCH_PLAYERS], cls[MATCH_PLAYERS];
    int      biome, night;
    uint32_t earned[MATCH_PLAYERS];    /* achievements just earned (filled in by the statistics) */
    int      rating[MATCH_PLAYERS], rating_change[MATCH_PLAYERS];   /* filled in by the statistics */
} MatchResult;

typedef struct Server Server;
typedef struct {                       /* all optional */
    void (*log)(void *ctx, int level, const char *msg);
    void (*chat)(void *ctx, const char *name, const char *text);
    void (*match_over)(void *ctx, const Server *s, MatchResult *r, const Replay *rep);
    /* a player out of step at tick t (theirs, ours: the checksums); once a match */
    void (*desync)(void *ctx, const Server *s, const char *name, uint32_t build, uint32_t t, uint32_t theirs, uint32_t ours);   /* build: theirs */
    /* player keys and names (all four, or none: then they're kept in memory only).
     * A key's verifier is what proves it; a name belongs to the first key that used it. */
    int  (*key_get)(void *ctx, const uint8_t id[16], uint8_t verifier[32]);   /* 1 = known */
    void (*key_put)(void *ctx, const uint8_t id[16], const uint8_t verifier[32]);
    int  (*name_get)(void *ctx, const char *name, uint8_t id[16]);            /* 1 = taken */
    void (*name_put)(void *ctx, const char *name, const uint8_t id[16]);
    /* the server's own lasting identity (players' keys are bound to it) and secret */
    int  (*server_key)(void *ctx, uint8_t id[16], uint8_t secret[32]);         /* 1 = loaded */
    /* bans, by key or by address (none: kept in memory) */
    int  (*ban_check)(void *ctx, const uint8_t id[16], uint32_t ip);          /* 1 = banned */
    void (*ban_add)(void *ctx, const uint8_t id[16], uint32_t ip, const char *name, const char *reason);
    int  (*ban_remove)(void *ctx, const char *name);                          /* 1 = there was one */
    int  (*leaders)(void *ctx, LeaderEntry *out, int max);      /* the top players, best first */
    /* 1 verified, 0 pending (retry HELLO), -1 refused. No password crosses UDP. */
    int (*cloud_verify)(void *ctx, const char *ticket, const uint8_t server_id[16], uint64_t nonce, const uint8_t key_id[16], char account[33], char name[NET_NAME]);
    void *ctx;
} ServerHooks;

enum { SERVER_WEATHER_RANDOM = -1 };

typedef struct {
    char        name[NET_SERVER_NAME];
    char        motd[NET_MOTD];
    char        cloud_server[33];       /* nonempty: cloud login required, including spectators */
    char        password[64];          /* "" = open */
    int         max_players;           /* people in the lobby, 1 .. NET_LOBBY_MAX */
    int         min_players;           /* ready players needed for a match, 1 .. 4 */
    MatchSetup  base;                  /* versus, ai_level, biome, time, camp_garages; seed 0 = new each match */
    const char *map;                   /* .ofpmap text, or NULL for a new random battlefield each match */
    char        map_name[32];
    int         countdown_ms;          /* everyone's ready: start after this */
    int         max_per_address;       /* people from one address at once (several behind one router) */
    int         start_after_ms;        /* 0: wait for everyone; else start this long after min_players are ready */
    int         manual_start;          /* the lobby host starts once the other humans are ready */
    int         allow_watching;        /* people in the lobby may watch the match that's on */
    int         weather_mode;          /* WEATHER_* or SERVER_WEATHER_RANDOM */
    int         spectator_delay_s;     /* ...this far behind (so they can't tell their side what they see) */
    char        admin_password[64];    /* "" = no admin from the chat (/login) */
    int         admin_plain_login;     /* 1: also "/login PASSWORD" in the clear (old games); 0: only the challenge */
    int         rotation;              /* ROT_*: how the next map is picked from maps[] */
    uint8_t     modes[8];              /* a rotation of modes (extras bits), a match of each in turn; nmodes 0: base.extras' */
    int         nmodes;
    int         nmaps;                 /* 0: the one map (or random) */
    const char *maps[NET_MAPS];        /* .ofpmap texts (the server copies them) */
    char        map_names[NET_MAPS][32];
    int         allow_bots;            /* 0: no computer players at all, whatever bots / fill_bots say */
    int         bots;                  /* computer players added to every match (0 .. 3) */
    int         fill_bots;             /* 1: instead, top every match up to four players with them */
    int         bot_level;             /* AI_EASY .. AI_HARD */
} ServerConfig;

enum { ROT_ORDER, ROT_RANDOM, ROT_VOTE };

typedef struct {
    int      used, gone, authed, ready, slot, id;   /* slot: tank in the match, -1 = in the lobby */
    int      side;                     /* SIDE_* wished for, in versus */
    uint64_t join_order, side_order;    /* order of joining the lobby and the selected team */
    int      watching, spectating;     /* wants to watch / is watching the match that's on */
    int      admin;                    /* logged in with the admin password */
    char     admin_nonce[33];          /* /login: the challenge given (hex; "" none), answered within a minute */
    uint32_t admin_nonce_at;
    int      vote;                     /* the map voted for (index + 1; 0 = none) */
    uint8_t  rematch;                  /* after a match: wants a rematch */
    uint8_t  last_team1;               /* the side in the last match + 1 (0: wasn't in it) */
    int      rtt;                      /* the round trip it measures, ms */
    int      chat_tokens;              /* chat lines it may still send right now (refills) */
    uint32_t chat_refill, muted_until;
    uint32_t slap_sent, slap_received;
    int      chat_dropped;             /* lines dropped for flooding, lately */
    int      told_no_watch;            /* was told watching is off */
    int      joining;                  /* wants to join the match that's on */
    uint32_t req_second; int req_count;   /* HISTREQ and MAPREQ this second (they're limited) */
    int      catching;                 /* catching up on the match (rejoined, or resynced): not waited for */
    int      resyncs;                  /* times out of step this match */
    char     account_id[33];
    uint8_t  key_id[16];               /* who this is, across visits */
    uint32_t ticks_at;                 /* when the match's ticks last went to them (watchers: every 100 ms) */
    uint32_t build;                    /* their game's build (net_build_id: version, protocol): for desync reports */
    NetAddr  addr;
    uint32_t heard, nonce, acked, chat_seen, cseq_done;
    uint32_t assignment;
    uint8_t  cls, steer, portrait;
    char     name[NET_NAME];
} SrvClient;

typedef struct {
    uint32_t seq, cseq;
    int      from;                     /* client id, -1 = the server itself */
    int      to;                       /* client id it's for alone, -1 = everyone */
    char     name[NET_NAME], text[NET_CHAT];
} ChatLine;

struct Server {
    ServerConfig cfg;
    ServerHooks  hooks;
    int          desync_reported;      /* this match's out-of-step report is written */
    int        sock, state, next_id, reason, matches_played;
    int        desyncs;                /* players out of step, since it started (for the metrics) */
    struct { uint8_t id[16]; uint32_t seen; } queue[16];   /* full: who's waiting for a place, first come first */
    int        nqueue;
    NetAddr    public_addr;            /* NAT traversal: how the internet sees our socket (STUN), have_public set */
    int        have_public;
    uint8_t    stun_tx[12];
    char      *map;                    /* the map of the match being played (or about to be) */
    char      *maps[NET_MAPS];         /* the rotation (copies) */
    int        nmaps, map_idx, next_idx;
    uint8_t    ballot[NET_MAPS];       /* vote rotation: the maps to vote on next (three, made for its mode) */
    uint32_t   last_seed;              /* the last match's seed and map: a rematch's */
    int        mode_idx, match_mode_idx;               /* the mode rotation: where it is */
    int        last_map_idx, rematch_open;
    int        room;                   /* tanks the next map holds (worked out when it's chosen; 0: not yet) */
    char       map_name[32];
    int        delay;                  /* this match's input delay in ticks */
    uint32_t   take_at[MATCH_PLAYERS]; /* a newcomer takes this tank over at this tick (0 = no) */
    int        late[MATCH_PLAYERS];    /* that place was taken over on the way */
    uint32_t   gone_since[MATCH_PLAYERS];
    char       left_name[MATCH_PLAYERS][NET_NAME];  /* a tank vacated from the lobby, until a new player takes it */
    char       left_account[MATCH_PLAYERS][33];
    uint8_t    left_key[MATCH_PLAYERS][16];        /* reserved for the same profile during this match */
    uint32_t   next_assignment;
    uint32_t   ready_since;            /* min_players were ready from this moment (start_after) */
    uint32_t   pkts_in, pkts_out, bytes_in, bytes_out;   /* for the metrics */
    struct { uint8_t id[16]; uint32_t ip; } mem_bans[64];
    int        nmem_bans;
    SrvClient  c[NET_LOBBY_MAX];
    uint64_t   lobby_order;
    uint64_t   start_host;             /* join order of the host who started a manual countdown */
    char       lobby_team_name[2][NET_TEAM_NAME];
    ChatLine   chat[32];
    uint32_t   chat_seq;
    /* the match */
    MatchSetup ms;
    uint32_t   map_len, map_hash;
    int        in_match[MATCH_PLAYERS];        /* client index per slot */
    Game       game;
    Replay     replay;
    uint32_t   tick;
    uint8_t    in[MATCH_PLAYERS][256];
    uint32_t   in_tick[MATCH_PLAYERS][256];
    uint8_t    final[256][MAX_TANKS];
    uint32_t   sums[256];
    uint32_t   last_final, last_send, last_lobby, ended_at, started, countdown_at, loading_at;
    uint8_t    server_id[16], secret[32];     /* players' keys are bound to the id; cookies use the secret */
    struct { uint32_t ip, second; int count; } qlimit[64];   /* queries per address per second */
    uint32_t   qsecond; int qcount;
    struct { uint32_t ip, since, blocked_until; int fails; } pwfail[32];   /* wrong passwords per address */
    struct { uint8_t id[16], verifier[32]; char name[NET_NAME]; } mem_keys[256];   /* without hooks */
    int        nmem_keys;
    LeaderEntry leaders[NET_LEADERS];
    int        nleaders;
    char       status[96];
};

void server_default_config(ServerConfig *cfg);
int  server_open(Server *s, uint16_t port, const ServerConfig *cfg, const ServerHooks *hooks);   /* 0 = listening */
int  server_step(Server *s);           /* returns SRV_* */
void server_say(Server *s, const char *text);      /* a line from the server in everyone's chat */
void server_info(const Server *s, ServerInfo *out);
int  server_people(const Server *s);   /* in the lobby or playing */
/* NAT traversal: ask a STUN server how the internet sees our socket (the answer comes in with the game's
 * packets: s->public_addr, have_public); and a few packets toward a player who wants in (from the listing,
 * by join code), so our router lets their packets through */
void server_stun(Server *s, const NetAddr *stun);
void server_punch(Server *s, const NetAddr *to);
void pick_next_for_test(Server *s);   /* (tests) */
void server_close(Server *s);
/* a console command ("help" lists them): kick, ban, say, bots, start, restart, map ...
 * The reply goes into out. Also reachable from the chat after /login <admin password>. */
void server_command(Server *s, const char *line, char *out, size_t cap);
/* new settings without a restart (not the port): name, motd, password, players, bots, maps ... */
void server_reconfigure(Server *s, const ServerConfig *cfg);
/* (fuzz testing: one packet, as if it came from this address) */
void server_fuzz_packet(Server *s, const uint8_t *buf, int n, uint32_t ip, uint16_t port);

#endif
