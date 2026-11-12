/* SPDX-License-Identifier: MIT
 * Playing on a match server: join its lobby (password if it asks), chat, get
 * ready, then play the matches. Non-blocking: call netc_step often. */
#ifndef OFP_CLIENT_H
#define OFP_CLIENT_H

#include "common/net.h"
#include "common/roles.h"
#include "common/server.h"

enum { NC_CONNECTING, NC_LOBBY, NC_LOADING, NC_RUNNING, NC_ENDED, NC_FAILED };

typedef struct { int id, ready, cls, playing, side, rtt, portrait, admin; char name[NET_NAME]; } LobbyPlayer;   /* portrait: 0 unknown, 1..NET_AVATARS */
typedef struct { uint32_t seq; int from; char name[NET_NAME], text[NET_CHAT]; } ClientChat;
enum { LOBBY_ROWS = 4 };
typedef struct {
    int columns, pages, count[3];
    int entry[3][NET_LOBBY_MAX];     /* roster index, or nlobby + bot index */
} LobbyRoster;

typedef struct {
    int        sock, socket_open, state, id, slot, reason;
    NetAddr    server;
    char       my_name[NET_NAME], password[64];
    uint8_t    key[32];              /* this player's own secret key (settings.ini keeps it) */
    uint64_t   cookie;               /* the server's proof-of-address token */
    uint8_t    server_id[16];
    int        send_verifier;        /* the server asked us to register our key */
    uint32_t   live_first;           /* the oldest tick in the latest live TICKS */
    uint32_t   catch_to;             /* a rejoin: the match was at this tick */
    int        restart;              /* the server said: play the match again from the start */
    uint32_t   last_hist;
    /* the server and its lobby */
    char       server_name[NET_SERVER_NAME], motd[NET_MOTD], map_name[32];
    int        max_players, min_players, versus, biome, server_state, countdown;
    int        bots, bot_level;      /* computer players the next match gets */
    int        match_capacity;      /* tanks including bots (0 if the server doesn't report it) */
    int        lobby_social, team_leader[2]; /* optional lobby extension, ids (0 = empty) */
    int        lobby_host, manual_start, lobby_admin;
    int        portrait;
    char       lobby_team_name[2][NET_TEAM_NAME];
    char       next_map[32];         /* what the next match is played on */
    int        rotation, nmaps, start_wait;   /* start_wait: seconds until it starts without the unready */
    int        time_limit, time_rule;  /* the server's matches: seconds (0 = no limit), TIME_RULE_* */
    int        places_free;          /* places a newcomer can take in the match that's on */
    int        eliminated;           /* already lost all tanks in the running match */
    int        server_protocol, server_oldest;   /* from a version refusal */
    char       server_version[16];
    int        joining;              /* asked to join it */
    int        votes[NET_MAPS];         /* 255: not on the ballot */
    int        rematch_yes, rematch_of; /* after a match: how many of its players want a rematch, of how many */
    int        next_extras;             /* the next match's mode (extras bits; -1: the server doesn't say) */
    int        my_rematch;              /* (ours, as sent)*/
    int        nballot, ballot[3];      /* the maps to vote on (index + 1), and their names */
    int        my_vote;                 /* (the game's own idea of its vote: index + 1, 0 none)*/
    char       ballot_name[3][32];
    int        rtt;                  /* the round trip to the server, ms */
    int        side, watching;       /* SIDE_* wished for; wants to watch the match that's on */
    int        can_order;
    TeamOrder team_order;             /* only our own team, filtered by server */
    int        delay;                /* this match's input delay, ticks */
    char       cache_dir[512];       /* where downloaded maps are kept ("" = not kept) */
    int        queue_place;          /* the server is full: our place in its queue (0: not waiting) */
    uint32_t   wait_until;           /* (the queue: the next HELLO not before then; 0 none) */
    int        wait_for_place;       /* 1: a full server: wait in its queue (asked again every 5 s) instead of failing */
    char       admin_pw[64];         /* /login PASSWORD: kept here, the server gets only a proof of it */
    int        map_fails;            /* downloads that didn't match the announced hash (3: give up) */
    int        map_cached;           /* this match's map came from there */
    uint8_t    map_id[8];
    LobbyPlayer lobby[NET_LOBBY_MAX];
    int        nlobby;
    ClientChat chat[40];
    uint32_t   chat_seq;             /* every line up to this has arrived */
    char       out_text[NET_CHAT];   /* my line on its way */
    uint32_t   out_cseq;
    LeaderEntry leaders[NET_LEADERS];
    int        nleaders, got_leaders;
    int        ready;
    /* the match */
    MatchSetup ms;
    uint32_t   assignment, left_assignment;
    int        leaving, legacy_left_match;
    uint32_t   map_len, map_hash, map_got;
    char      *map;
    uint8_t    ticks[256][MAX_TANKS];
    uint32_t   tick_id[256];
    int        tanks, winner;
    uint8_t    mine[256];
    uint32_t   mine_hi, have, used;
    uint32_t   desync_tick;          /* the last RESYNC's tick (0: not said) */
    uint32_t   last_send, last_ping, heard;
    uint8_t    want_cls, steer;
    char       status[96];
} NetClient;

/* key: the player's 32-byte secret (NULL: a new one, for this session only) */
int  netc_open(NetClient *c, const char *host, uint16_t port, const char *name, const char *password,
               const uint8_t key[32], int cls, int steer);   /* 0 = trying */
int  netc_take_restart(NetClient *c);
void netc_watch(NetClient *c, int on);                   /* watch the match that's on (a spectator) */
void netc_join(NetClient *c, int on);                    /* join the match that's on, in a free place */
void netc_side(NetClient *c, int side);                  /* SIDE_*: which side I'd like in versus */
int  netc_lobby_teams(const NetClient *c);
int  netc_lobby_roles(const NetClient *c, const LobbyPlayer *p);
const char *netc_lobby_team_name(const NetClient *c, int side);
int  netc_lobby_can_rename(const NetClient *c);
int  netc_lobby_can_rename_side(const NetClient *c, int side);
void netc_lobby_rename(NetClient *c, int side, const char *name);
int  netc_lobby_can_start(const NetClient *c);
int  netc_lobby_bots(const NetClient *c);
void netc_lobby_roster(const NetClient *c, LobbyRoster *r);
void netc_lobby_team_step(NetClient *c, int direction);
void netc_lobby_tank_step(NetClient *c, int direction);
int  netc_spectating(const NetClient *c);                /* in a match, but only watching */                    /* 1, once, when the match must be played again from 0 */
int  netc_behind(const NetClient *c);
/* official ticks here and waiting, from t on (a run without gaps; at most 200) */
int  netc_waiting(const NetClient *c, uint32_t t);                    /* ticks still to catch up on */
int  netc_can_retry(const NetClient *c);                 /* failed, but worth trying again (a dropout) */
void netc_fuzz_packet(NetClient *c, const uint8_t *buf, int n);   /* (fuzz testing: one packet from the server) */
int  netc_step(NetClient *c);                            /* returns NC_* */
void netc_ready(NetClient *c, int ready, int cls);
void netc_chat(NetClient *c, const char *text);
void netc_team_order(NetClient *c, int kind);
int  netc_chat_mentions(const NetClient *c, const ClientChat *line, int id);
void netc_chat_complete(const NetClient *c, char *text, size_t cap);
void netc_leaders(NetClient *c);                          /* ask for the leaderboard */
void netc_back_to_lobby(NetClient *c);                    /* after a match's report */
void netc_leave_match(NetClient *c);                      /* abandon an active tank, keep the lobby connection */
void netc_rematch(NetClient *c, int yes);                 /* after a match: a rematch, please (or not) */
int  netc_tick(NetClient *c, uint32_t t, InputBits out[MAX_TANKS]);   /* 1 = tick t is official (and played) */
void netc_input(NetClient *c, uint32_t t, InputBits b);
void netc_check(NetClient *c, uint32_t t, uint32_t sum);
void netc_close(NetClient *c);
/* Troubleshooting output excludes passwords, player keys, cookies and chat. */
void netc_diagnostics(const NetClient *c, char *out, size_t cap);
int netc_save_diagnostics(const NetClient *c, const char *path);

/* NAT traversal, joining by a join code: ask a STUN server how the internet sees our socket (up to wait_ms),
 * then tell the listing, so the server (behind its router) sends us a few packets and the way opens while our
 * HELLOs go on. 0 = told; -1 = no STUN answer or the listing said no (the join goes on anyway) */
int  netc_punch(NetClient *const clients[], int count, const char *listing_url, const char *code,
                const char *stun_server, int wait_ms);
#endif
