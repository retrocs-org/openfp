/* SPDX-License-Identifier: MIT
 * A small HTTP client for talking to the server listing service: servers announce
 * themselves, games fetch the list. https:// and http:// through libcurl where the
 * build has it (OFP_HAVE_CURL); otherwise our own plain HTTP/1.0, and an https://
 * address is asked over http:// instead for legacy listing reads only. Cloud account
 * callers require http_has_tls() and always use verified HTTPS without fallback. */
#ifndef OFP_HTTP_H
#define OFP_HTTP_H

#include <stddef.h>

/* GET (body NULL) or POST (application/x-www-form-urlencoded). Writes the
 * response body into out (cap bytes, NUL-terminated). Returns the HTTP status,
 * or -1 if the request failed. timeout_ms bounds the whole exchange. */
int  http_request(const char *url, const char *body, char *out, size_t cap, int timeout_ms);
/* the same, calling idle(ctx) every few milliseconds while it waits: a server
 * keeps serving (and answers the listing's own check) during its announcement */
int  http_request_idle(const char *url, const char *body, char *out, size_t cap, int timeout_ms,
                       void (*idle)(void *ctx), void *ctx);
/* POST a JSON body (webhooks) */
int  http_post_json(const char *url, const char *json, char *out, size_t cap, int timeout_ms,
                    void (*idle)(void *ctx), void *ctx);
/* a JSON string literal of s, quotes and all */
void http_json_str(char *out, size_t cap, const char *s);
/* the status and body of a raw HTTP response (NUL-terminated); -1 = not one */
int  http_parse_response(const char *resp, char *out, size_t cap);
/* set up the https library once, on the main thread before any other thread asks (0 = ok);
 * a request calls it too, so single-threaded programs needn't */
int  http_init(void);
/* 1: requests go over IPv4 if they can (then any way): the dedicated server's announcements, so the
 * listing lists it by the address every player reaches */
void http_ipv4_first(int on);
/* 1 if this build can do https:// */
int  http_has_tls(void);
/* append key=value (URL-encoded) to a form body */
void http_form(char *body, size_t cap, const char *key, const char *value);
/* the same into a growing malloc'd buffer (*body, *cap; start with NULL, 0); 0 = ok */
int  http_form_grow(char **body, size_t *cap, const char *key, const char *value);

#endif
