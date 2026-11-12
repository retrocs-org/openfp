/* SPDX-License-Identifier: MIT */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L          /* sockets, getaddrinfo, clock_gettime under -std=c99 */
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "common/net.h"
#if defined(__amigaos__)
/* (AmigaOS: the sockets, the clock and sleeping are src/platform/amiga/net_amiga.c's, through
 *  bsdsocket.library: Roadshow, AmiTCP, Miami; the packing below is the same everywhere) */
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef int socklen_t;
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#endif

#ifndef __amigaos__
int net_init(void)
{
#ifdef _WIN32
    WSADATA w;
    return WSAStartup(MAKEWORD(2, 2), &w) == 0 ? 0 : -1;
#else
    return 0;
#endif
}

/* a socket's family: dual-stack IPv6 (taking IPv4 too, as ::ffff:a.b.c.d) where the system has it, else IPv4 */
static struct { int s, v6; } fam[64];
static int fam_n, have_v6 = -1;                       /* (-1: not tried; 1: dual-stack sockets work here) */
static int sock_v6(int s)
{
    struct sockaddr_storage a;
    socklen_t len = sizeof a;
    int i;
    for (i = 0; i < fam_n; i++) if (fam[i].s == s) return fam[i].v6;
    if (getsockname(s, (struct sockaddr *)&a, &len) != 0) return 0;   /* (not in the table: asked) */
#ifdef AF_INET6
    return a.ss_family == AF_INET6;
#else
    return 0;
#endif
}
static void remember(int s, int v6)
{
    int i;
    for (i = 0; i < fam_n; i++) if (fam[i].s == s) { fam[i].v6 = v6; return; }
    if (fam_n < 64) { fam[fam_n].s = s; fam[fam_n++].v6 = v6; }
}

static void key_of(NetAddr *a)                        /* (the 32-bit key: IPv4's own, IPv6's /64 folded) */
{
    static const uint8_t mapped[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };
    if (!memcmp(a->ip6, mapped, 12)) {
        a->v6 = 0;
        a->ip = (uint32_t)a->ip6[12] << 24 | (uint32_t)a->ip6[13] << 16 | (uint32_t)a->ip6[14] << 8 | a->ip6[15];
    } else {
        uint32_t h = 2166136261u;
        int i;
        a->v6 = 1;
        for (i = 0; i < 8; i++) h = (h ^ a->ip6[i]) * 16777619u;
        a->ip = (h & 0x00ffffffu) | 1u;                /* (in 0.x.x.x, which no IPv4 player comes from: an IPv6
                                                          household's key never is an IPv4 player's address) */
    }
}
static void v4_to(NetAddr *a, uint32_t ip)
{
    memset(a->ip6, 0, 10);
    a->ip6[10] = a->ip6[11] = 0xff;
    a->ip6[12] = (uint8_t)(ip >> 24); a->ip6[13] = (uint8_t)(ip >> 16); a->ip6[14] = (uint8_t)(ip >> 8); a->ip6[15] = (uint8_t)ip;
    a->ip = ip;
    a->v6 = 0;
}

int net_open(uint16_t port)
{
    int s;
#ifdef AF_INET6
    if (have_v6 != 0 && !getenv("OFP_NO_IPV6")) {      /* (OFP_NO_IPV6=1: IPv4 only, as before) */
        struct sockaddr_in6 a6;
        int off = 0;
        s = (int)socket(AF_INET6, SOCK_DGRAM, 0);
        if (s >= 0 && setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&off, sizeof off) != 0) {
            net_close(s);                                 /* (a system without dual-stack sockets: IPv4 then) */
            s = -1;
        }
        if (s >= 0) {
            memset(&a6, 0, sizeof a6);
            a6.sin6_family = AF_INET6;
            a6.sin6_addr = in6addr_any;
            a6.sin6_port = htons(port);
            if (bind(s, (struct sockaddr *)&a6, sizeof a6) == 0) {
#ifdef _WIN32
                { u_long on = 1; ioctlsocket((SOCKET)s, FIONBIO, &on); }
#else
                fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
                have_v6 = 1;
                remember(s, 1);
                return s;
            }
            net_close(s);
        }
        have_v6 = 0;                                  /* (no IPv6 here: IPv4 sockets from now on) */
    }
#endif
    {
        struct sockaddr_in a;
        s = (int)socket(AF_INET, SOCK_DGRAM, 0);
        if (s < 0) return -1;
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons(port);
        if (bind(s, (struct sockaddr *)&a, sizeof a) != 0) { net_close(s); return -1; }
#ifdef _WIN32
        { u_long on = 1; ioctlsocket((SOCKET)s, FIONBIO, &on); }
#else
        fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
        remember(s, 0);
        return s;
    }
}

uint16_t net_port(int s)
{
    struct sockaddr_storage a;
    socklen_t len = sizeof a;
    if (getsockname(s, (struct sockaddr *)&a, &len) != 0) return 0;
    if (a.ss_family == AF_INET) return ntohs(((struct sockaddr_in *)&a)->sin_port);
#ifdef AF_INET6
    if (a.ss_family == AF_INET6) return ntohs(((struct sockaddr_in6 *)&a)->sin6_port);
#endif
    return 0;
}

int net_wait(int s, int ms)
{   /* (the dedicated server, idle: asleep until a packet or the next thing to do, not 500 times a second) */
    fd_set set;
    struct timeval tv;
    FD_ZERO(&set);
    FD_SET((unsigned)s, &set);
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return select(s + 1, &set, NULL, NULL, &tv) > 0;
}

void net_close(int s)
{
#ifdef _WIN32
    closesocket((SOCKET)s);
#else
    close(s);
#endif
}

void net_split(const char *in, char *host, int cap, int *port)
{
    const char *colon = strchr(in, ':');
    if (in[0] == '[') {                                   /* [v6]:port */
        const char *end = strchr(in, ']');
        int n = end ? (int)(end - in - 1) : (int)strlen(in + 1);
        snprintf(host, (size_t)cap, "%.*s", n < cap ? n : cap - 1, in + 1);
        if (end && end[1] == ':' && port) *port = atoi(end + 2);
    } else if (colon && !strchr(colon + 1, ':')) {        /* host:port */
        int n = (int)(colon - in);
        snprintf(host, (size_t)cap, "%.*s", n < cap ? n : cap - 1, in);
        if (port) *port = atoi(colon + 1);
    } else snprintf(host, (size_t)cap, "%s", in);         /* a bare host, or a bare IPv6 address */
}

int net_resolve(const char *host, uint16_t port, NetAddr *out)
{   /* IPv4 first where a name has both (it works the most places); IPv6 where that's all there is, and only
     * if this system's sockets do IPv6 */
    struct addrinfo hints, *res = NULL, *r, *pick = NULL;
    char bare[256];
    net_split(host, bare, sizeof bare, NULL);
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(bare, NULL, &hints, &res) != 0 || !res) return -1;
    for (r = res; r && !pick; r = r->ai_next) if (r->ai_family == AF_INET) pick = r;
#ifdef AF_INET6
    if (have_v6 != 0)
        for (r = res; r && !pick; r = r->ai_next) if (r->ai_family == AF_INET6) pick = r;
#endif
    if (!pick) { freeaddrinfo(res); return -1; }
    memset(out, 0, sizeof *out);
    if (pick->ai_family == AF_INET) v4_to(out, ntohl(((struct sockaddr_in *)pick->ai_addr)->sin_addr.s_addr));
#ifdef AF_INET6
    else { memcpy(out->ip6, &((struct sockaddr_in6 *)pick->ai_addr)->sin6_addr, 16); key_of(out); }
#endif
    out->port = port;
    freeaddrinfo(res);
    return 0;
}

int net_send(int s, const NetAddr *to, const void *buf, int len)
{
#ifdef AF_INET6
    if (sock_v6(s)) {
        struct sockaddr_in6 a6;
        NetAddr t = *to;
        if (!t.v6 && t.ip6[10] != 0xff) v4_to(&t, t.ip);   /* (made by hand, as a number: the IPv6 form) */
        memset(&a6, 0, sizeof a6);
        a6.sin6_family = AF_INET6;
        memcpy(&a6.sin6_addr, t.ip6, 16);
        a6.sin6_port = htons(to->port);
        return (int)sendto(s, (const char *)buf, len, 0, (struct sockaddr *)&a6, sizeof a6);
    }
#endif
    {
        struct sockaddr_in a;
        if (to->v6) return -1;                            /* (an IPv6 address, an IPv4 socket) */
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(to->ip);
        a.sin_port = htons(to->port);
        return (int)sendto(s, (const char *)buf, len, 0, (struct sockaddr *)&a, sizeof a);
    }
}

int net_recv(int s, NetAddr *from, void *buf, int cap)
{
    struct sockaddr_storage a;
    socklen_t al = sizeof a;
    int n = (int)recvfrom(s, (char *)buf, cap, 0, (struct sockaddr *)&a, &al);
    if (n <= 0) return -1;
    memset(from, 0, sizeof *from);
    if (a.ss_family == AF_INET) {
        v4_to(from, ntohl(((struct sockaddr_in *)&a)->sin_addr.s_addr));
        from->port = ntohs(((struct sockaddr_in *)&a)->sin_port);
    }
#ifdef AF_INET6
    else if (a.ss_family == AF_INET6) {
        memcpy(from->ip6, &((struct sockaddr_in6 *)&a)->sin6_addr, 16);
        key_of(from);
        from->port = ntohs(((struct sockaddr_in6 *)&a)->sin6_port);
    }
#endif
    else return -1;
    return n;
}

int net_same(const NetAddr *a, const NetAddr *b)
{
    if (a->port != b->port || a->v6 != b->v6) return 0;
    return a->v6 ? !memcmp(a->ip6, b->ip6, 16) : a->ip == b->ip;
}

uint32_t net_ms(void)
{
    static int have_offset;
    static uint32_t offset;                              /* (tests: OFP_CLOCK_OFFSET=N starts the clock at N ms,
                                                            e.g. weeks of uptime, or just before it wraps) */
    if (!have_offset) { const char *o = getenv("OFP_CLOCK_OFFSET"); offset = o ? (uint32_t)strtoul(o, NULL, 10) : 0; have_offset = 1; }
#ifdef _WIN32
    return (uint32_t)GetTickCount() + offset;
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000u + (uint32_t)(t.tv_nsec / 1000000)) + offset;
#endif
}

void net_sleep(int ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec t;
    t.tv_sec = ms / 1000; t.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&t, NULL);
#endif
}

#endif /* !__amigaos__ */

void net_addr_str(const NetAddr *a, char *out, int cap)
{
    if (a->v6) {                                          /* [v6]:port */
        char t[64];
        int i, at = 0;
        for (i = 0; i < 16; i += 2)                      /* (written out plainly: no :: shortening, no inet_ntop) */
            at += snprintf(t + at, sizeof t - (size_t)at, i ? ":%x" : "%x", (unsigned)(a->ip6[i] << 8 | a->ip6[i + 1]));
        snprintf(out, (size_t)cap, "[%s]:%u", t, (unsigned)a->port);
        return;
    }
    snprintf(out, (size_t)cap, "%u.%u.%u.%u:%u", (unsigned)((a->ip >> 24) & 255), (unsigned)((a->ip >> 16) & 255),
             (unsigned)((a->ip >> 8) & 255), (unsigned)(a->ip & 255), (unsigned)a->port);
}

int net_stun_ask(int s, const NetAddr *stun, uint8_t txid[12])
{
    uint8_t b[20] = { 0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42 };   /* a Binding Request, no attributes */
    net_random(txid, 12);
    memcpy(b + 8, txid, 12);
    return net_send(s, stun, b, 20) == 20 ? 0 : -1;
}

int net_stun_answer(const uint8_t *b, int n, const uint8_t txid[12], NetAddr *out)
{
    int at = 20, len;
    if (n < 20 || b[0] != 0x01 || b[1] != 0x01 || b[4] != 0x21 || b[5] != 0x12 || b[6] != 0xA4 || b[7] != 0x42 ||
        memcmp(b + 8, txid, 12)) return 0;               /* (a Binding Success to our question, or not ours) */
    len = b[2] << 8 | b[3];
    if (20 + len > n) return 0;
    while (at + 4 <= 20 + len) {
        int type = b[at] << 8 | b[at + 1], alen = b[at + 2] << 8 | b[at + 3];
        const uint8_t *v = b + at + 4;
        if (at + 4 + alen > n) break;
        if ((type == 0x0020 || type == 0x0001) && alen >= 8) {   /* XOR-MAPPED-ADDRESS (or the old MAPPED-ADDRESS) */
            int x = type == 0x0020;
            memset(out, 0, sizeof *out);
            out->port = (uint16_t)((v[2] << 8 | v[3]) ^ (x ? 0x2112 : 0));
            if (v[1] == 0x01) {                          /* IPv4 */
                uint32_t ip = (uint32_t)v[4] << 24 | (uint32_t)v[5] << 16 | (uint32_t)v[6] << 8 | v[7];
                if (x) ip ^= 0x2112A442u;
#ifndef __amigaos__
                v4_to(out, ip);
#else
                out->ip = ip;
#endif
                return 1;
            }
#ifndef __amigaos__
            if (v[1] == 0x02 && alen >= 20) {            /* IPv6 */
                int i;
                for (i = 0; i < 16; i++) out->ip6[i] = (uint8_t)(v[4 + i] ^ (x ? (i < 4 ? b[4 + i] : b[8 + i - 4]) : 0));
                key_of(out);
                return 1;
            }
#endif
        }
        at += 4 + ((alen + 3) & ~3);
    }
    return 0;
}

uint32_t net_build_id(void) { return (uint32_t)NET_PROTOCOL << 16 | (uint32_t)MAX_TANKS << 8 | 1u; }

int net_compatible(uint32_t build, int oldest)
{
    int proto = (int)(build >> 16), tanks = (int)((build >> 8) & 255);
    return tanks == MAX_TANKS && proto >= NET_PROTOCOL_OLDEST && oldest <= NET_PROTOCOL;
}

void net_random(void *buf, int n)
{
    unsigned char *b = (unsigned char *)buf;
    int got = 0, i;
#if !defined(_WIN32) && !defined(__amigaos__)
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) { got = (int)fread(b, 1, (size_t)n, f); fclose(f); }
#endif
    if (got < n) {                                          /* (no /dev/urandom: mix what we have) */
        static uint32_t x = 0x9E3779B9u;
        uint32_t t = net_ms();
        for (i = got; i < n; i++) {
            x ^= t + (uint32_t)i * 2654435761u + (uint32_t)(size_t)&x;
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            b[i] = (unsigned char)(x >> 24);
        }
    }
}

/* ---- packing -------------------------------------------------------------------- */

void pk_init(Pack *k, uint8_t *buf, int cap) { k->p = buf; k->n = 0; k->cap = cap; k->bad = 0; }
void pk_u8(Pack *k, uint32_t v) { if (k->n + 1 > k->cap) { k->bad = 1; return; } k->p[k->n++] = (uint8_t)v; }
void pk_u16(Pack *k, uint32_t v) { pk_u8(k, v & 255); pk_u8(k, (v >> 8) & 255); }
void pk_u32(Pack *k, uint32_t v) { pk_u16(k, v & 0xFFFF); pk_u16(k, v >> 16); }
void pk_bytes(Pack *k, const void *b, int n)
{
    if (n < 0 || k->n + n > k->cap) { k->bad = 1; return; }
    memcpy(k->p + k->n, b, (size_t)n);
    k->n += n;
}
uint32_t uk_u8(Pack *k) { if (k->n + 1 > k->cap) { k->bad = 1; return 0; } return k->p[k->n++]; }
uint32_t uk_u16(Pack *k) { uint32_t a = uk_u8(k); return a | uk_u8(k) << 8; }
uint32_t uk_u32(Pack *k) { uint32_t a = uk_u16(k); return a | uk_u16(k) << 16; }
void uk_bytes(Pack *k, void *b, int n)
{
    if (n < 0 || k->n + n > k->cap) { k->bad = 1; return; }
    memcpy(b, k->p + k->n, (size_t)n);
    k->n += n;
}
void pk_header(Pack *k, int type) { pk_u8(k, 'O'); pk_u8(k, 'F'); pk_u8(k, NET_PROTOCOL); pk_u8(k, (uint32_t)type); }
int uk_header(Pack *k)
{
    /* newer builds (that kept our layouts) are read, and BYE from any build, so a
     * version refusal always gets through */
    if (k->cap < 4 || k->p[0] != 'O' || k->p[1] != 'F') return 0;
    /* (a HELLO: to refuse it properly; a QUERY: so older games and listings still see us) */
    if (k->p[2] < NET_PROTOCOL_OLDEST && k->p[3] != NM_BYE && k->p[3] != NM_HELLO && k->p[3] != NM_QUERY) return 0;
    k->n = 4;
    return k->p[3];
}

void pk_setup(Pack *k, const MatchSetup *ms)
{
    int i;
    pk_u32(k, ms->seed); pk_u8(k, ms->biome); pk_u8(k, ms->time); pk_u8(k, ms->camp_garages);
    pk_u8(k, ms->versus); pk_u8(k, ms->ai_level); pk_u8(k, ms->players);
    for (i = 0; i < MATCH_PLAYERS; i++) {
        pk_u8(k, ms->cls[i]); pk_u8(k, ms->team[i]); pk_u8(k, ms->steer[i]); pk_u8(k, ms->bot[i]);
    }
    pk_u16(k, ms->time_limit); pk_u8(k, ms->time_rule);
    pk_u8(k, ms->map_size);
    pk_u8(k, ms->bot_commands);
    pk_u8(k, ms->lives);
    pk_u8(k, ms->no_powerups);
}

void uk_setup(Pack *k, MatchSetup *ms)
{
    int i;
    memset(ms, 0, sizeof *ms);
    ms->seed = uk_u32(k); ms->biome = (uint8_t)uk_u8(k); ms->time = (uint8_t)uk_u8(k);
    ms->camp_garages = (uint8_t)uk_u8(k); ms->versus = (uint8_t)uk_u8(k); ms->ai_level = (uint8_t)uk_u8(k);
    ms->players = (uint8_t)uk_u8(k);
    for (i = 0; i < MATCH_PLAYERS; i++) {
        ms->cls[i] = (uint8_t)uk_u8(k); ms->team[i] = (uint8_t)uk_u8(k);
        ms->steer[i] = (uint8_t)uk_u8(k); ms->bot[i] = (uint8_t)uk_u8(k);
    }
    ms->time_limit = (uint16_t)uk_u16(k); ms->time_rule = (uint8_t)uk_u8(k);
    ms->map_size = (uint8_t)uk_u8(k);
    ms->bot_commands = uk_u8(k) == 1;
    ms->lives = (uint8_t)uk_u8(k);
    ms->no_powerups = uk_u8(k) != 0;
}

void pk_str(Pack *k, const char *s, int max)
{
    int n = (int)strlen(s);
    if (n > max) n = max;
    if (n > 255) n = 255;
    pk_u8(k, (uint32_t)n);
    pk_bytes(k, s, n);
}

void uk_str(Pack *k, char *s, int cap)
{
    int n = (int)uk_u8(k), i, j = 0;
    char tmp[256];
    uk_bytes(k, tmp, n);
    if (k->bad) { if (cap) s[0] = 0; return; }
    for (i = 0; i < n && j + 1 < cap; i++)                /* printable only: names and chat come from strangers */
        if ((unsigned char)tmp[i] >= 32 && (unsigned char)tmp[i] < 127) s[j++] = tmp[i];
    if (cap) s[j] = 0;
}

void pk_info(Pack *k, const ServerInfo *i)
{
    pk_u32(k, i->build);
    pk_str(k, i->name, NET_SERVER_NAME - 1); pk_str(k, i->world, 15); pk_str(k, i->map, 31);
    pk_u8(k, i->players); pk_u8(k, i->max_players); pk_u8(k, i->password); pk_u8(k, i->running);
    pk_u8(k, i->versus); pk_u8(k, i->match_players);
}

void uk_info(Pack *k, ServerInfo *i)
{
    memset(i, 0, sizeof *i);
    i->build = uk_u32(k);
    uk_str(k, i->name, sizeof i->name); uk_str(k, i->world, sizeof i->world); uk_str(k, i->map, sizeof i->map);
    i->players = (uint8_t)uk_u8(k); i->max_players = (uint8_t)uk_u8(k); i->password = (uint8_t)uk_u8(k);
    i->running = (uint8_t)uk_u8(k); i->versus = (uint8_t)uk_u8(k); i->match_players = (uint8_t)uk_u8(k);
}

int net_query(const char *host, uint16_t port, ServerInfo *out, int timeout_ms, int *ping_ms)
{
    NetAddr to, from;
    uint8_t b[NET_MAX_PACKET];
    Pack k;
    uint32_t start = net_ms(), last = 0;
    int s, n, got = -1;
    if (net_init() != 0 || net_resolve(host, port, &to) != 0 || (s = net_open(0)) < 0) return -1;
    while ((int)(net_ms() - start) < timeout_ms && got < 0) {
        if (!last || net_ms() - last > 300) {               /* ask (again), padded: see NET_QUERY_MIN */
            pk_init(&k, b, sizeof b);
            pk_header(&k, NM_QUERY);
            memset(b + k.n, 0, NET_QUERY_MIN - k.n);
            net_send(s, &to, b, NET_QUERY_MIN);
            last = net_ms();
        }
        while ((n = net_recv(s, &from, b, sizeof b)) > 0) {
            pk_init(&k, b, n);
            if (net_same(&from, &to) && uk_header(&k) == NM_INFO) {
                uk_info(&k, out);
                if (!k.bad) { got = 0; if (ping_ms) *ping_ms = (int)(net_ms() - start); }
            }
        }
        net_sleep(2);
    }
    net_close(s);
    return got;
}
