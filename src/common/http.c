/* SPDX-License-Identifier: MIT */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/http.h"
#include "common/net.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#endif

void http_form(char *body, size_t cap, const char *key, const char *value)
{
    size_t n = strlen(body);
    const char *hex = "0123456789ABCDEF";
    if (n && n + 1 < cap) body[n++] = '&';
    for (; *key && n + 1 < cap; key++) body[n++] = *key;
    if (n + 1 < cap) body[n++] = '=';
    for (; value && *value && n + 4 < cap; value++) {
        unsigned char ch = (unsigned char)*value;
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.')
            body[n++] = (char)ch;
        else { body[n++] = '%'; body[n++] = hex[ch >> 4]; body[n++] = hex[ch & 15]; }
    }
    body[n] = 0;
}

int http_form_grow(char **body, size_t *cap, const char *key, const char *value)
{
    size_t need = (*body ? strlen(*body) : 0) + strlen(key) + 3 * (value ? strlen(value) : 0) + 4;
    if (need > *cap) {
        size_t nc = need * 2 + 1024;
        char *nb = (char *)realloc(*body, nc);
        if (!nb) return -1;
        if (!*body) nb[0] = 0;
        *body = nb;
        *cap = nc;
    }
    http_form(*body, *cap, key, value);
    return 0;
}

static void closesock(int s)
{
#ifdef _WIN32
    closesocket((SOCKET)s);
#else
    close(s);
#endif
}

/* wait for the socket, in short slices, giving the caller its idle time */
static int wait_sock(int s, int for_write, int timeout_ms, uint32_t start, void (*idle)(void *), void *ctx)
{
    for (;;) {
        fd_set f;
        struct timeval tv;
        int left = timeout_ms - (int)(net_ms() - start), r;
        if (left <= 0) return 0;
        FD_ZERO(&f); FD_SET((unsigned)s, &f);
        tv.tv_sec = 0; tv.tv_usec = (left < 5 ? left : 5) * 1000;
        r = select(s + 1, for_write ? NULL : &f, for_write ? &f : NULL, NULL, &tv);
        if (r > 0) return 1;
        if (r < 0) return 0;
        if (idle) idle(ctx);
    }
}

int http_request(const char *url, const char *body, char *out, size_t cap, int timeout_ms)
{
    return http_request_idle(url, body, out, cap, timeout_ms, NULL, NULL);
}

/* plain http, our own few lines (for builds without libcurl, and machines without TLS) */
static const char *content_type = "application/x-www-form-urlencoded";

static int plain_request(const char *url, const char *body, char *out, size_t cap, int timeout_ms,
                         void (*idle)(void *ctx), void *ctx)
{
    char host[256], port[8] = "80", req[1024];
    const char *p, *path;
    struct addrinfo hints, *res = NULL;
    int s, status = -1, n;
    size_t got = 0;
    uint32_t start = net_ms();
    if (cap) out[0] = 0;
    if (strncmp(url, "http://", 7)) return -1;               /* plain http only */
    p = url + 7;
    path = strchr(p, '/');
    n = path ? (int)(path - p) : (int)strlen(p);
    if (!path) path = "/";
    if (n <= 0 || n >= (int)sizeof host) return -1;
    memcpy(host, p, (size_t)n); host[n] = 0;
    if (strchr(host, ':')) { char *c = strchr(host, ':'); snprintf(port, sizeof port, "%s", c + 1); *c = 0; }
    if (net_init() != 0) return -1;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0 || !res) return -1;
    s = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { freeaddrinfo(res); return -1; }
#ifndef _WIN32
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#else
    { u_long on = 1; ioctlsocket((SOCKET)s, FIONBIO, &on); }
#endif
    connect(s, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);
    if (!wait_sock(s, 1, timeout_ms, start, idle, ctx)) { closesock(s); return -1; }   /* connected */
    if (body)
        n = snprintf(req, sizeof req, "POST %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: OpenFP\r\n"
                     "Content-Type: %s\r\nContent-Length: %lu\r\n\r\n",
                     path, host, content_type, (unsigned long)strlen(body));
    else
        n = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: OpenFP\r\n\r\n", path, host);
    if (n <= 0 || n >= (int)sizeof req) { closesock(s); return -1; }
    {   /* the request, then the body, however big: a bit at a time as the socket takes it */
        const char *parts[2];
        size_t lens[2];
        int part;
        parts[0] = req; lens[0] = (size_t)n;
        parts[1] = body; lens[1] = body ? strlen(body) : 0;
        for (part = 0; part < 2; part++) {
            size_t done = 0;
            while (done < lens[part]) {
                int k;
                if (!wait_sock(s, 1, timeout_ms, start, idle, ctx)) { closesock(s); return -1; }
                k = (int)send(s, parts[part] + done, (int)(lens[part] - done > 16384 ? 16384 : lens[part] - done), 0);
                if (k <= 0) { closesock(s); return -1; }
                done += (size_t)k;
            }
        }
    }
    {   /* read it all */
        enum { RESP_MAX = 262144 };                     /* (a gallery map is up to 200 KB) */
        char *resp = (char *)malloc(RESP_MAX);          /* (per request: two threads may ask at once) */
        size_t rl = 0;
        if (!resp) { closesock(s); return -1; }
        for (;;) {
            int k;
            if (!wait_sock(s, 0, timeout_ms, start, idle, ctx)) break;
            k = (int)recv(s, resp + rl, (int)(RESP_MAX - 1 - rl), 0);
            if (k <= 0) break;
            rl += (size_t)k;
            if (rl >= RESP_MAX - 1) break;
        }
        resp[rl] = 0;
        closesock(s);
        status = http_parse_response(resp, out, cap);
        free(resp);
    }
    (void)p; (void)got;
    return status;
}

int http_parse_response(const char *resp, char *out, size_t cap)
{
    const char *p;
    size_t got;
    int status;
    if (cap) out[0] = 0;
    if (sscanf(resp, "HTTP/%*s %d", &status) != 1 || status < 100 || status > 999) return -1;
    p = strstr(resp, "\r\n\r\n");
    if (p && cap) {
        p += 4;
        got = strlen(p);
        if (got >= cap) got = cap - 1;
        memcpy(out, p, got);
        out[got] = 0;
    }
    return status;
}

/* ---- https (and http) through libcurl, where it's there ----------------------------- */
#ifdef OFP_HAVE_CURL
#include <curl/curl.h>

typedef struct { char *out; size_t cap, got; } Sink;

static size_t sink(char *p, size_t size, size_t n, void *ud)
{
    Sink *s = (Sink *)ud;
    size_t len = size * n, room = s->cap > s->got + 1 ? s->cap - s->got - 1 : 0;
    if (len < room) room = len;
    if (room) { memcpy(s->out + s->got, p, room); s->got += room; s->out[s->got] = 0; }
    return len;                                         /* (the rest is dropped, not an error) */
}

static int ipv4_first;                               /* (the dedicated server: its listing announcements) */
void http_ipv4_first(int on) { ipv4_first = on; }

static int curl_request_once(const char *url, const char *body, char *out, size_t cap, int timeout_ms,
                             void (*idle)(void *ctx), void *ctx, int v4);
static int curl_request(const char *url, const char *body, char *out, size_t cap, int timeout_ms,
                        void (*idle)(void *ctx), void *ctx)
{   /* IPv4 first if asked (the listing lists a server by the address its announcement comes from, and IPv4
     * is the one every player can reach); then any way at all */
    int status = curl_request_once(url, body, out, cap, timeout_ms, idle, ctx, ipv4_first);
    if (status < 0 && ipv4_first) status = curl_request_once(url, body, out, cap, timeout_ms, idle, ctx, 0);
    return status;
}

static int curl_request_once(const char *url, const char *body, char *out, size_t cap, int timeout_ms,
                             void (*idle)(void *ctx), void *ctx, int v4)
{
    CURL *h;
    Sink sk;
    struct curl_slist *hdrs = NULL;
    long code = -1;
    int status = -1;
    if (http_init() != 0) return -1;
    if (!(h = curl_easy_init())) return -1;
    sk.out = out; sk.cap = cap; sk.got = 0;
    if (cap) out[0] = 0;
    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, sink);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &sk);
    curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, (long)timeout_ms);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, (long)(timeout_ms < 5000 ? timeout_ms : 5000));
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "OpenFP/" OFP_VERSION);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, body ? 0L : 1L);   /* (a GET may follow http -> https) */
    curl_easy_setopt(h, CURLOPT_MAXREDIRS, 3L);
    if (v4) curl_easy_setopt(h, CURLOPT_IPRESOLVE, (long)CURL_IPRESOLVE_V4);
    if (getenv("OFP_CA_FILE")) curl_easy_setopt(h, CURLOPT_CAINFO, getenv("OFP_CA_FILE"));   /* a private CA */
    if (body) {
        static char ct[96];
        snprintf(ct, sizeof ct, "Content-Type: %s", content_type);
        hdrs = curl_slist_append(NULL, ct);
        curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);
        curl_easy_setopt(h, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    }
    if (!idle) {
        if (curl_easy_perform(h) == CURLE_OK) status = 0;
    } else {                                            /* keep the caller going while we wait */
        CURLM *m = curl_multi_init();
        int running = 1;
        if (m && curl_multi_add_handle(m, h) == CURLM_OK) {
            while (running) {
                int nfds = 0;
                if (curl_multi_perform(m, &running) != CURLM_OK) break;
                if (!running) break;
                curl_multi_wait(m, NULL, 0, 10, &nfds);
                idle(ctx);
            }
            {
                CURLMsg *msg;
                int left;
                while ((msg = curl_multi_info_read(m, &left)) != NULL)
                    if (msg->msg == CURLMSG_DONE && msg->data.result == CURLE_OK) status = 0;
            }
            curl_multi_remove_handle(m, h);
        }
        if (m) curl_multi_cleanup(m);
    }
    if (status == 0 && curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code) == CURLE_OK) status = (int)code;
    curl_easy_cleanup(h);
    if (hdrs) curl_slist_free_all(hdrs);
    return status;
}
#endif

/* ---- Windows: https through WinHTTP, part of Windows (no libcurl to ship) -------------- */
#if defined(_WIN32) && !defined(OFP_HAVE_CURL)
#define OFP_HAVE_WINHTTP 1
#include <winhttp.h>

static int winhttp_request(const char *url, const char *body, char *out, size_t cap, int timeout_ms)
{
    wchar_t wurl[1100], host[256], path[1100];
    URL_COMPONENTSW uc;
    HINTERNET ses = NULL, con = NULL, req = NULL;
    DWORD status = 0, len = sizeof status, got = 0;
    size_t n = 0;
    int result = -1;
    wchar_t headers[96];
    if (cap) out[0] = 0;
    if (!MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 1100)) return -1;
    memset(&uc, 0, sizeof uc);
    uc.dwStructSize = sizeof uc;
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 1100;
    uc.dwSchemeLength = (DWORD)-1; uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) return -1;
    if (uc.dwExtraInfoLength && uc.lpszExtraInfo)       /* (the query string goes with the path) */
        wcsncat(path, uc.lpszExtraInfo, 1099 - wcslen(path));
    {
        wchar_t agent[48];
        MultiByteToWideChar(CP_UTF8, 0, "OpenFP/" OFP_VERSION, -1, agent, 48);
        ses = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!ses) return -1;
    WinHttpSetTimeouts(ses, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    con = WinHttpConnect(ses, host, uc.nPort, 0);
    if (con) req = WinHttpOpenRequest(con, body ? L"POST" : L"GET", path, NULL, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (req) {
        DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (body) WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof redirects);
        MultiByteToWideChar(CP_UTF8, 0, content_type, -1, headers + 14, 80);
        memcpy(headers, L"Content-Type: ", 14 * sizeof(wchar_t));
        if (WinHttpSendRequest(req, body ? headers : WINHTTP_NO_ADDITIONAL_HEADERS, body ? (DWORD)-1L : 0,
                               (LPVOID)body, body ? (DWORD)strlen(body) : 0, body ? (DWORD)strlen(body) : 0, 0) &&
            WinHttpReceiveResponse(req, NULL) &&
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                &status, &len, WINHTTP_NO_HEADER_INDEX)) {
            result = (int)status;
            while (cap && n + 1 < cap && WinHttpReadData(req, out + n, (DWORD)(cap - 1 - n), &got) && got) n += got;
            if (cap) out[n] = 0;
        }
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return result;
}
#endif

/* curl_global_init isn't thread-safe: http_init() runs it once, on the main thread, before any
 * thread makes a request (the game's update check runs on one); later calls just say how it went */
int http_init(void)
{
#ifdef OFP_HAVE_CURL
    static int state;                                   /* 0 not yet, 1 ready, -1 failed */
    if (!state) state = curl_global_init(CURL_GLOBAL_DEFAULT) == 0 ? 1 : -1;
    return state > 0 ? 0 : -1;
#else
    return 0;
#endif
}

int http_has_tls(void)
{
#if defined(OFP_HAVE_CURL) || defined(OFP_HAVE_WINHTTP) || defined(OFP_TVOS)
    return 1;
#else
    return 0;
#endif
}

int http_request_idle(const char *url, const char *body, char *out, size_t cap, int timeout_ms,
                      void (*idle)(void *ctx), void *ctx)
{
#ifdef OFP_TVOS
    extern int ofp_apple_http(const char *, const char *, const char *, char *, size_t, int, void (*)(void *), void *);
    (void)plain_request;
    return ofp_apple_http(url, body, content_type, out, cap, timeout_ms, idle, ctx);
#elif defined(OFP_HAVE_CURL)
    (void)plain_request;                                 /* (kept for builds without libcurl) */
    return curl_request(url, body, out, cap, timeout_ms, idle, ctx);
#elif defined(OFP_HAVE_WINHTTP)
    (void)plain_request; (void)idle; (void)ctx;          /* (the game only: it waits, no idle needed) */
    return winhttp_request(url, body, out, cap, timeout_ms);
#else
    if (!strncmp(url, "https://", 8)) {                  /* no TLS here: the same address over plain http */
        char plain[1024];
        static int told;
        snprintf(plain, sizeof plain, "http://%.1000s", url + 8);
        if (!told) { fprintf(stderr, "(no TLS in this build: using %s)\n", plain); told = 1; }
        return plain_request(plain, body, out, cap, timeout_ms, idle, ctx);
    }
    return plain_request(url, body, out, cap, timeout_ms, idle, ctx);
#endif
}

int http_post_json(const char *url, const char *json, char *out, size_t cap, int timeout_ms,
                   void (*idle)(void *ctx), void *ctx)
{
    int r;
    content_type = "application/json";
    r = http_request_idle(url, json, out, cap, timeout_ms, idle, ctx);
    content_type = "application/x-www-form-urlencoded";
    return r;
}

void http_json_str(char *out, size_t cap, const char *s)
{
    size_t n = 0;
    if (cap < 3) return;
    out[n++] = '"';
    for (; *s && n + 7 < cap; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"' || ch == '\\') { out[n++] = '\\'; out[n++] = (char)ch; }
        else if (ch == '\n') { out[n++] = '\\'; out[n++] = 'n'; }
        else if (ch < 32) { n += (size_t)snprintf(out + n, cap - n, "\\u%04x", ch); }
        else out[n++] = (char)ch;
    }
    out[n++] = '"';
    out[n] = 0;
}
