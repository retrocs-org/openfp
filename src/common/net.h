/* SPDX-License-Identifier: MIT
 * Network play, low level: UDP sockets (BSD sockets / Winsock) and the
 * messages of the OpenFP protocol, packed byte by byte so every
 * platform reads them the same. The design is in docs/NETWORK.md:
 * deterministic lockstep, a dedicated server as referee. */
#ifndef OFP_NET_H
#define OFP_NET_H

#include <stdint.h>
#include "core/match.h"

#define NET_PORT      29870          /* the default port */
/* From protocol 8 on: a change that only adds fields at the end of a message keeps
 * NET_PROTOCOL_OLDEST (older builds read what they know and ignore the rest); only a
 * change that breaks a message raises it. Two builds talk if each one's protocol is at
 * least the other's oldest. */
#define NET_PROTOCOL        39  /* 39: arena starts independent of team slot order; 38: ice, oil and optional power-ups; 37: world vehicles and match lives; 36: Classic matches stay clear; 35: snow, fog, sandstorms and clouds; 34: rain weather; 33: the server supplies bot inputs; 32: independent turrets and custom team areas; 31: authored roads through generated camps; 30: arenas' supply drop in the same place on every compiler (clang, gcc); 29: the checksum: the map's own area (any grid size: the Amiga's 128); 28: kill confirmed, hardpoint; compact bases on small maps, their country split; 27: 8 players together against the computer: no ninth tank (the match's 8 slots); 26: a side all out loses, patrols on the roads; 25: repairs in the field, changing weather, smarter computer players; 24: storms of their own, smarter computer players; 23: conquest's country guns, each half its side's; 22: map sizes (a random battlefield's in the setup); 21: a fortress of four garages (4 vs 4); 20: four new worlds (the swamp's mud, the moon's slide); 19: matches of up to 8; 18: supply drops, the computer players' levels, the ballot; 17: deathmatch ties, warlord escorts and arenas; 16: team deathmatch in arenas, halves; 15: arenas (annihilation: no camps, spawn points); 14: warlord; 13: team deathmatch, every tank for itself; */
#define NET_PROTOCOL_OLDEST 39  /*    12: conquest and annihilation (rule changes, all of them) */
#ifndef OFP_VERSION
/* Release version changes only by an explicit new-version decision. */
#define OFP_VERSION "0.1"
#endif
#define NET_MAX_PACKET 1400
#define NET_DELAY     4              /* input delay in ticks (80 ms at 50 Hz): the default */
#define NET_DELAY_MIN 2              /* the server picks one per match from the players' pings */
#define NET_DELAY_MAX 12
#define NET_REDUNDANT 8              /* every packet repeats this many recent ticks */
#define NET_MAP_CHUNK 1024
#define NET_MAP_MAX   (1024 * 1024)   /* a map file is never bigger than this */
#define NET_CHECK_EVERY 50           /* clients report their checksum this often */

/* An address: IPv4 or IPv6. ip is IPv4's (host byte order) or, for IPv6, a 32-bit key of its /64 network
 * (one household: what rate limits and bans count by); ip6 the whole address (IPv4 as ::ffff:a.b.c.d). */
typedef struct { uint32_t ip; uint16_t port; uint8_t v6; uint8_t ip6[16]; } NetAddr;
/* "host:port", "[v6]:port", "v6" or "host": the host (no brackets) and the port if one is given */
void     net_split(const char *in, char *host, int cap, int *port);
/* STUN (RFC 5389): what address and port the internet sees for a socket, asked of a STUN server. The request
 * goes out with net_stun_ask (txid: 12 random bytes, kept); a packet that comes back is checked and read with
 * net_stun_answer (1: it was the answer, out filled; 0: not a STUN answer to that question) */
#define  NET_STUN_DEFAULT "stun.l.google.com:19302"
int      net_stun_ask(int s, const NetAddr *stun, uint8_t txid[12]);
int      net_stun_answer(const uint8_t *buf, int n, const uint8_t txid[12], NetAddr *out);

int      net_init(void);                                  /* 0 = ok */
int      net_open(uint16_t port);                         /* non-blocking UDP socket; -1 = failed */
void     net_close(int s);
uint16_t net_port(int s);                                /* the port a socket has (0: don't know) */
int      net_wait(int s, int ms);                         /* up to ms for a packet: 1 one is there, 0 not */
int      net_resolve(const char *host, uint16_t port, NetAddr *out);   /* 0 = ok */
int      net_send(int s, const NetAddr *to, const void *buf, int len);
int      net_recv(int s, NetAddr *from, void *buf, int cap);          /* bytes, or -1 for none */
int      net_same(const NetAddr *a, const NetAddr *b);
uint32_t net_ms(void);                                    /* milliseconds, monotonic */
void     net_sleep(int ms);
void     net_addr_str(const NetAddr *a, char *out, int cap);

/* the build fingerprint: machines must agree on protocol and tank table */
uint32_t net_build_id(void);
/* can a peer with this build id and oldest protocol talk to us? */
int      net_compatible(uint32_t build, int oldest);
/* random bytes from the system (/dev/urandom; time-mixed as a last resort) */
void     net_random(void *buf, int n);

/* messages. Client to server: HELLO PING CHAT LEADREQ MAPREQ LOADED INPUT CHECK BYE QUERY.
 * Server to client: CHALLENGE WELCOME LOBBY CHAT LEADERS MATCH MAPDATA TICKS END BYE INFO. */
enum { NM_HELLO = 1, NM_CHALLENGE, NM_WELCOME, NM_PING, NM_LOBBY, NM_CHAT, NM_LEADREQ, NM_LEADERS,
       NM_MATCH, NM_MAPREQ, NM_MAPDATA, NM_LOADED, NM_INPUT, NM_TICKS, NM_CHECK, NM_END, NM_BYE,
       NM_QUERY, NM_INFO,
       /* protocol 3 */
       NM_COOKIE,        /* s>c: prove your address with this first (no state is kept until you do) */
       NM_NEEDKEY,       /* s>c: a key this server hasn't seen: send its verifier to register it */
       NM_HISTREQ,       /* c>s: official ticks from here, please (catching up) */
       NM_HISTORY,       /* s>c: official ticks from the match's start */
       NM_RESYNC,        /* s>c: you're out of step: play the match again from the start */
       /* protocol 4 */
       NM_PONG,          /* s>c: the answer to a PING, for measuring the round trip */
       /* protocol 28 */
       NM_REMATCH,       /* c>s: after a match, a rematch: yes (1) or no (0) */
       NM_LEAVEMATCH,    /* c<>s: leave a tank assignment while staying connected */
       NM_TEAMORDER };   /* optional s>c: own-team order/permission, tied to an assignment */
enum { NR_FULL = 1, NR_VERSION, NR_DESYNC, NR_TIMEOUT, NR_ENDED, NR_PASSWORD, NR_KICKED, NR_SHUTDOWN,
       NR_KEY,           /* the player key didn't match the one this name/key registered with */
       NR_TOO_MANY,      /* too many players from one address */
       NR_RULES };       /* the same protocol, but the game computes differently (common/rules.h) */

/* A QUERY must be at least this long (padded with zeros): the answer is never
 * bigger than the question, so a forged sender address can't be used to make a
 * server flood someone else (UDP amplification). */
#define NET_QUERY_MIN 160

#define NET_NAME      16             /* player names, with the 0 */
#define NET_TEAM_NAME 12             /* lobby team names, with the 0 */
#define NET_AVATARS   10             /* shared profile portraits */
#define NET_SERVER_NAME 40
#define NET_MOTD      200
#define NET_CHAT      120            /* a chat line */
#define NET_LOBBY_MAX 16             /* people in a server's lobby */
#define NET_LEADERS   10
#define NET_MAPS      32             /* maps in a server's rotation */

enum { SIDE_ANY, SIDE_CAMP, SIDE_FORTRESS };       /* which side a player would like, in versus */
#define PING_READY  1                /* PING flags */
#define PING_WATCH  2                /* watch the match that's on */
#define PING_JOIN   4                /* join the match that's on (a computer's or an abandoned tank) */
#define LOBBY_HOST_RENAME 1
#define LOBBY_ADMIN       2



/* byte packing */
typedef struct { uint8_t *p; int n, cap, bad; } Pack;
void     pk_init(Pack *k, uint8_t *buf, int cap);
void     pk_u8(Pack *k, uint32_t v);
void     pk_u16(Pack *k, uint32_t v);
void     pk_u32(Pack *k, uint32_t v);
void     pk_bytes(Pack *k, const void *b, int n);
uint32_t uk_u8(Pack *k);
uint32_t uk_u16(Pack *k);
uint32_t uk_u32(Pack *k);
void     uk_bytes(Pack *k, void *b, int n);
void     pk_header(Pack *k, int type);                    /* 'O' 'F' version type */
int      uk_header(Pack *k);                              /* type, or 0 if not ours */
void     pk_setup(Pack *k, const MatchSetup *ms);
void     uk_setup(Pack *k, MatchSetup *ms);

/* what a server says about itself, to anyone who asks (the listing too) */
typedef struct {
    char    name[NET_SERVER_NAME], world[16], map[32];
    uint8_t players, max_players, password, running, versus, match_players;
    uint32_t build;
} ServerInfo;

void pk_str(Pack *k, const char *s, int max);            /* length byte + bytes */
void uk_str(Pack *k, char *s, int cap);
void pk_info(Pack *k, const ServerInfo *i);
void uk_info(Pack *k, ServerInfo *i);
/* ask one server about itself (blocking, up to timeout_ms); 0 = answered, *ping_ms set */
int  net_query(const char *host, uint16_t port, ServerInfo *out, int timeout_ms, int *ping_ms);

#endif
