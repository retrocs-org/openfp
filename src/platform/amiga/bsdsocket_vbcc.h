/* SPDX-License-Identifier: MIT
 * bsdsocket.library for vbcc: the eight calls the game makes, the structures and the constants they
 * use, declared here in vbcc's own calling style, so the network needs only the NDK, not a Roadshow
 * SDK. (The SDK's proto/socket.h wants an inline/bsdsocket_protos.h for vbcc that the freely
 * available netinclude doesn't have.) Every offset, register, structure and value is the Roadshow
 * netinclude's own: inline/bsdsocket.h, sys/socket.h, netinet/in.h, netdb.h, sys/filio.h. The 68k is
 * big-endian: network byte order is its own, so htonl and the rest change nothing. */
#ifndef OFP_BSDSOCKET_VBCC_H
#define OFP_BSDSOCKET_VBCC_H
#include <exec/types.h>
#include <exec/libraries.h>

typedef unsigned char  sa_family_t;
typedef unsigned short in_port_t;
typedef unsigned long  in_addr_t;
typedef unsigned long  socklen_t;

struct in_addr { in_addr_t s_addr; };
struct sockaddr { UBYTE sa_len; sa_family_t sa_family; char sa_data[14]; };
struct sockaddr_in {                         /* (BSD 4.4: sin_len first, as Roadshow has it) */
    UBYTE       sin_len;
    sa_family_t sin_family;
    in_port_t   sin_port;
    struct in_addr sin_addr;
    UBYTE       sin_zero[8];
};
struct hostent {
    STRPTR  h_name;
    STRPTR *h_aliases;
    LONG    h_addrtype;
    LONG    h_length;
    char  **h_addr_list;
};

#define AF_INET     2
#define SOCK_DGRAM  2
#define INADDR_ANY  0x00000000UL
#define INADDR_NONE 0xffffffffUL
#define FIONBIO     0x8004667EUL             /* _IOW('f', 126, long) */
#define htonl(x) (x)
#define htons(x) (x)
#define ntohl(x) (x)
#define ntohs(x) (x)

extern struct Library *SocketBase;

LONG __ofp_socket(__reg("a6") struct Library *, __reg("d0") LONG domain, __reg("d1") LONG type, __reg("d2") LONG protocol) = "\tjsr\t-30(a6)";
LONG __ofp_bind(__reg("a6") struct Library *, __reg("d0") LONG s, __reg("a0") struct sockaddr *name, __reg("d1") LONG namelen) = "\tjsr\t-36(a6)";
LONG __ofp_sendto(__reg("a6") struct Library *, __reg("d0") LONG s, __reg("a0") APTR buf, __reg("d1") LONG len, __reg("d2") LONG flags,
                  __reg("a1") struct sockaddr *to, __reg("d3") LONG tolen) = "\tjsr\t-60(a6)";
LONG __ofp_recvfrom(__reg("a6") struct Library *, __reg("d0") LONG s, __reg("a0") APTR buf, __reg("d1") LONG len, __reg("d2") LONG flags,
                    __reg("a1") struct sockaddr *from, __reg("a2") socklen_t *fromlen) = "\tjsr\t-72(a6)";
LONG __ofp_IoctlSocket(__reg("a6") struct Library *, __reg("d0") LONG s, __reg("d1") ULONG req, __reg("a0") APTR argp) = "\tjsr\t-114(a6)";
LONG __ofp_CloseSocket(__reg("a6") struct Library *, __reg("d0") LONG s) = "\tjsr\t-120(a6)";
in_addr_t __ofp_inet_addr(__reg("a6") struct Library *, __reg("a0") STRPTR cp) = "\tjsr\t-180(a6)";
struct hostent *__ofp_gethostbyname(__reg("a6") struct Library *, __reg("a0") STRPTR name) = "\tjsr\t-210(a6)";

#define socket(d, t, p)               __ofp_socket(SocketBase, (d), (t), (p))
#define bind(s, n, l)                 __ofp_bind(SocketBase, (s), (n), (l))
#define sendto(s, b, l, f, t, tl)     __ofp_sendto(SocketBase, (s), (b), (l), (f), (t), (tl))
#define recvfrom(s, b, l, f, fr, fl)  __ofp_recvfrom(SocketBase, (s), (b), (l), (f), (fr), (fl))
#define IoctlSocket(s, r, a)          __ofp_IoctlSocket(SocketBase, (s), (r), (a))
#define CloseSocket(s)                __ofp_CloseSocket(SocketBase, (s))
#define inet_addr(c)                  __ofp_inet_addr(SocketBase, (c))
#define gethostbyname(n)              __ofp_gethostbyname(SocketBase, (n))
#endif
