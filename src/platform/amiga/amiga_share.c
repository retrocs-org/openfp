/* SPDX-License-Identifier: MIT
 * The map editor's F8, the background half: MY MAP POSTed to the web site's gallery (action=map_upload) over
 * plain HTTP (the listing answers it too, for machines without TLS), the server's answer to stdout. Started by
 * public_servers.c's amiga_share_start, as `OpenFirePowerOnline --share-map PATH "AUTHOR"`. */
#include <stdio.h>
#include <string.h>
#include "common/net.h"
#if defined(__amigaos__) && defined(__VBCC__)
#include <exec/types.h>
#include "platform/amiga/bsdsocket_vbcc.h"
#define A6 __reg("a6")
#define A0 __reg("a0")
#define D0 __reg("d0")
#define D1 __reg("d1")
#define D2 __reg("d2")
LONG tcp_connect(A6 struct Library *, D0 LONG, A0 struct sockaddr *, D1 LONG) = "\tjsr\t-54(a6)";
LONG tcp_option(A6 struct Library *, D0 LONG, D1 LONG, D2 LONG, A0 const void *, __reg("d3") LONG) = "\tjsr\t-90(a6)";
LONG tcp_send(A6 struct Library *, D0 LONG, A0 const void *, D1 LONG, D2 LONG) = "\tjsr\t-66(a6)";
LONG tcp_recv(A6 struct Library *, D0 LONG, A0 void *, D1 LONG, D2 LONG) = "\tjsr\t-78(a6)";
#undef A6
#undef A0
#undef D0
#undef D1
#undef D2
#else
#ifdef __amigaos__
#include <proto/socket.h>
#else
#include <unistd.h>                                   /* (the desktop: tests/test_amiga_share.c) */
#define SocketBase NULL
#define sin_len sin_zero[0]
#endif
#include <netinet/in.h>
#include <sys/socket.h>
#define tcp_connect(base, fd, address, length) connect((fd), (address), (length))
#define tcp_option(base, fd, level, opt, arg, length) setsockopt((fd), (level), (opt), (arg), (length))
#define tcp_send(base, fd, buf, len, flags) send((fd), (buf), (len), (flags))
#define tcp_recv(base, fd, buf, len, flags) recv((fd), (buf), (len), (flags))
#endif
#include "platform/amiga/public_servers.h"

#ifndef DIRECTORY_HOST
#define DIRECTORY_HOST "openfp.retrocs.org"
#endif
#ifndef DIRECTORY_HTTP_PORT
#define DIRECTORY_HTTP_PORT 80
#endif
#ifdef __amigaos__
enum { TCP_STREAM = 1, TCP_OPTIONS = 0xffff, TCP_SEND_TIMEOUT = 0x1005, TCP_RECEIVE_TIMEOUT = 0x1006 };
#else
enum { TCP_STREAM = SOCK_STREAM, TCP_OPTIONS = SOL_SOCKET, TCP_SEND_TIMEOUT = SO_SNDTIMEO, TCP_RECEIVE_TIMEOUT = SO_RCVTIMEO };
#endif

/* the worker's part: path's map, POSTed as a form (author too); the server's answer to stdout */
int amiga_share_worker(const char *path, const char *author)
{
    static char map[200001];
    static const char hex[] = "0123456789ABCDEF";
    struct sockaddr_in address;
    struct { long seconds, micros; } timeout = { 15, 0 };
    char head[400], chunk[512], buffer[1024];
    NetAddr peer;
    FILE *file = fopen(path, "rb");
    size_t len = file ? fread(map, 1, sizeof map - 1, file) : 0, i;
    unsigned long body = 0;
    int socket_fd = -1, result = 20, n, pass, used;
    if (file) fclose(file);
    if (!len || len >= sizeof map - 1) return 20;
    map[len] = 0;
    if (net_init() || net_resolve(DIRECTORY_HOST, DIRECTORY_HTTP_PORT, &peer)) return 20;
    socket_fd = (int)socket(AF_INET, TCP_STREAM, 0);
    if (socket_fd < 0) return 20;
    tcp_option(SocketBase, socket_fd, TCP_OPTIONS, TCP_SEND_TIMEOUT, &timeout, sizeof timeout);
    tcp_option(SocketBase, socket_fd, TCP_OPTIONS, TCP_RECEIVE_TIMEOUT, &timeout, sizeof timeout);
    memset(&address, 0, sizeof address);
    address.sin_len = sizeof address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(peer.ip);
    address.sin_port = htons(peer.port);
    if (tcp_connect(SocketBase, socket_fd, (struct sockaddr *)&address, sizeof address)) goto done;
    for (pass = 0; pass < 2; pass++) {               /* (the first counts the body's bytes, the second sends it) */
        const char *parts[2];
        int part;
        snprintf(head, sizeof head, "action=map_upload&author=");
        for (i = 0; author[i] && i < 15; i++) {      /* (letters, digits and spaces only: the caller checked) */
            size_t at = strlen(head);
            if (author[i] == ' ') head[at] = '+', head[at + 1] = 0;
            else head[at] = author[i], head[at + 1] = 0;
        }
        strcat(head, "&map=");
        if (pass == 1) {
            char request[300];
            int r = snprintf(request, sizeof request,
                             "POST /api/ HTTP/1.0\r\nHost: " DIRECTORY_HOST "\r\nUser-Agent: OpenFP-Amiga\r\n"
                             "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: %lu\r\n\r\n", body);
            if ((int)tcp_send(SocketBase, socket_fd, request, r, 0) != r) goto done;
            if ((int)tcp_send(SocketBase, socket_fd, head, (int)strlen(head), 0) != (int)strlen(head)) goto done;
        } else body = strlen(head);
        parts[0] = map; parts[1] = NULL;
        for (part = 0; parts[part]; part++) {
            used = 0;
            for (i = 0; parts[part][i]; i++) {
                unsigned char c = (unsigned char)parts[part][i];
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
                    chunk[used++] = (char)c;
                else { chunk[used++] = '%'; chunk[used++] = hex[c >> 4]; chunk[used++] = hex[c & 15]; }
                if (used > (int)sizeof chunk - 4 || !parts[part][i + 1]) {
                    if (pass == 0) body += (unsigned long)used;
                    else if ((int)tcp_send(SocketBase, socket_fd, chunk, used, 0) != used) goto done;
                    used = 0;
                }
            }
        }
    }
    while ((n = (int)tcp_recv(SocketBase, socket_fd, buffer, sizeof buffer, 0)) > 0)
        if (fwrite(buffer, 1, (size_t)n, stdout) != (size_t)n) goto done;
    if (n == 0 && fflush(stdout) == 0) result = 0;
done:
    if (socket_fd >= 0) net_close(socket_fd);
    return result;
}

