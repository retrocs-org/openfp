/* SPDX-License-Identifier: MIT */
/* Exercise the bounded verification worker without an external account service. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "server/cloud_auth.h"
#include "common/cloud.h"
#include "common/http.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static int blocked, entered, status = 200;
int http_has_tls(void) { return 1; }
int http_init(void) { return 0; }
void http_form(char *out, size_t cap, const char *key, const char *value) {
    size_t n = strlen(out);
    CHECK(n + strlen(key) + strlen(value) + 3 < cap);
    snprintf(out + n, cap - n, "&%s=%s", key, value);
}
int http_request(const char *url, const char *body, char *out, size_t cap, int timeout) {
    CHECK(!strcmp(url, CLOUD_API) && timeout == 3000 && strstr(body, "action=verify"));
    pthread_mutex_lock(&gate);
    entered = 1;
    while (blocked) pthread_cond_wait(&wake, &gate);
    pthread_mutex_unlock(&gate);
    snprintf(out, cap, "id=0123456789abcdef0123456789abcdef\nusername=Pilot\n");
    return status;
}
static int verify(unsigned n) {
    char ticket[65], account[33], name[NET_NAME]; uint8_t sid[16] = {1}, kid[16] = {2};
    snprintf(ticket, sizeof ticket, "%064x", n);
    int result = server_cloud_verify(NULL, ticket, sid, n, kid, account, name);
    if (result == 1) CHECK(!strcmp(name, "Pilot") && !strcmp(account, "0123456789abcdef0123456789abcdef"));
    return result;
}
int main(void) {
    uint32_t at; char secret[65]; int result;
    snprintf(secret, sizeof secret, "%064d", 1);
    blocked = 1;
    CHECK(!server_cloud_start("fixture", secret));
    CHECK(verify(1) == 0);
    at = net_ms();
    for (;;) {
        pthread_mutex_lock(&gate); int ready = entered; pthread_mutex_unlock(&gate);
        if (ready) break;
        CHECK(net_ms() - at < 2000); net_sleep(1);
    }
    at = net_ms(); CHECK(verify(1) == 0); CHECK(net_ms() - at < 1000);
    for (unsigned i = 2; i <= 16; i++) CHECK(verify(i) == 0);
    CHECK(verify(17) == -1); /* bounded queue, no blocking on a busy account service */
    pthread_mutex_lock(&gate); blocked = 0; pthread_cond_signal(&wake); pthread_mutex_unlock(&gate);
    server_cloud_stop();
    CHECK(!server_cloud_start("fixture", secret));
    for (unsigned i = 1; i <= 40; i++) {
        at = net_ms();
        do { result = verify(i); if (!result) net_sleep(1); CHECK(net_ms() - at < 2000); } while (!result);
        CHECK(result == 1); /* consumed results must free slots for reconnects */
    }
    server_cloud_stop(); status = 403;
    CHECK(!server_cloud_start("fixture", secret)); at = net_ms();
    do { result = verify(41); if (!result) net_sleep(1); CHECK(net_ms() - at < 2000); } while (!result);
    CHECK(result == -1); server_cloud_stop();
    puts("cloud worker: nonblocking admission, queue bound, repeated reconnects, rejection and shutdown passed");
    return 0;
}
