/* SPDX-License-Identifier: MIT
 * The network on AmigaOS 3.x: the same functions as src/common/net.c's desktop half (sockets, a
 * clock, sleeping), through bsdsocket.library, so any TCP/IP stack that offers it does: Roadshow,
 * AmiTCP, Miami; and any network card with a SANA-II driver (X-Surf 100, ZZ9000, A2065, ...).
 * The packing and the protocol are net.c's, the same on every machine; the clock and sleeping are
 * amiga_clock.c's (timer.device's headers and Roadshow's both have a struct timeval: apart, they
 * can't clash).
 *
 * Built with vbcc (vc +aos68k: only the NDK; the sockets are bsdsocket_vbcc.h's) or Bebbo's
 * m68k-amigaos-gcc (libnix: the toolchain's NDK and Roadshow headers). */
#ifdef __amigaos__
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#ifdef __VBCC__
#include "platform/amiga/bsdsocket_vbcc.h"     /* (vbcc: our own, from Roadshow's netinclude: no SDK needed) */
#else
#include <proto/socket.h>                     /* (gcc: the toolchain's Roadshow headers) */
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netdb.h>
#endif
#include "common/net.h"
#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffffUL                  /* (older SDKs: not defined) */
#endif

struct Library *SocketBase;                  /* bsdsocket.library (opened by net_init) */
static int cleanup_set;

static void net_amiga_cleanup(void)
{
    if (SocketBase) { CloseLibrary(SocketBase); SocketBase = NULL; }
}

int net_init(void)
{
    if (!cleanup_set) { atexit(net_amiga_cleanup); cleanup_set = 1; }
    if (SocketBase) return 0;
    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    return SocketBase ? 0 : -1;               /* (none: no TCP/IP stack running; local games still work) */
}

int net_open(uint16_t port)
{
    struct sockaddr_in a;
    LONG on = 1;
    LONG s;
    if (!SocketBase) return -1;
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return -1;
    memset(&a, 0, sizeof a);
    a.sin_len = sizeof a;                     /* (BSD 4.4, as Roadshow's sockaddr_in) */
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(s, (struct sockaddr *)&a, sizeof a) != 0) { CloseSocket(s); return -1; }
    IoctlSocket(s, FIONBIO, (char *)&on);     /* (non-blocking: nothing waiting, recvfrom says so) */
    return (int)s;
}

void net_close(int s)
{
    if (SocketBase && s >= 0) CloseSocket((LONG)s);
}

int net_resolve(const char *host, uint16_t port, NetAddr *out)
{
    struct hostent *h;
    struct in_addr ia;
    if (!SocketBase) return -1;
    memset(out, 0, sizeof *out);               /* (IPv4 only here: v6 stays 0) */
    ia.s_addr = inet_addr((STRPTR)host);      /* (an address as it is: no lookup) */
    if (ia.s_addr != INADDR_NONE) {
        out->ip = ntohl(ia.s_addr);
        out->port = port;
        return 0;
    }
    h = gethostbyname((STRPTR)host);
    if (!h || h->h_addrtype != AF_INET || !h->h_addr_list[0]) return -1;
    memcpy(&ia, h->h_addr_list[0], sizeof ia);
    out->ip = ntohl(ia.s_addr);
    out->port = port;
    return 0;
}

int net_send(int s, const NetAddr *to, const void *buf, int len)
{
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_len = sizeof a;
    a.sin_family = AF_INET;
    if (to->v6) return -1;                     /* (an IPv6 server: not from here) */
    a.sin_addr.s_addr = htonl(to->ip);
    a.sin_port = htons(to->port);
    return (int)sendto((LONG)s, (APTR)buf, (LONG)len, 0, (struct sockaddr *)&a, sizeof a);
}

int net_recv(int s, NetAddr *from, void *buf, int cap)
{
    struct sockaddr_in a;
    socklen_t al = sizeof a;
    LONG n = recvfrom((LONG)s, buf, (LONG)cap, 0, (struct sockaddr *)&a, &al);
    if (n <= 0) return -1;
    memset(from, 0, sizeof *from);
    from->ip = ntohl(a.sin_addr.s_addr);
    from->port = ntohs(a.sin_port);
    return (int)n;
}

int net_same(const NetAddr *a, const NetAddr *b) { return a->ip == b->ip && a->port == b->port; }

#endif /* __amigaos__ */
