/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <workbench/startup.h>
#ifdef __VBCC__
#include "platform/amiga/bsdsocket_vbcc.h"
#else
#include <proto/socket.h>
#include <proto/amissl.h>
#include <proto/amisslmaster.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif
#include "platform/amiga/public_servers.h"

#ifndef DIRECTORY_HOST
#define DIRECTORY_HOST "openfp.retrocs.org"
#endif
#define REQUEST "GET /api/?format=text HTTP/1.0\r\nHost: " DIRECTORY_HOST "\r\n" \
                "User-Agent: OpenFP-Amiga\r\nConnection: close\r\n\r\n"

#ifdef __VBCC__
/* Minimal AmiSSL 4.x ABI bindings, from the AmiSSL 4.11 SDK.
 * The types are opaque and all library bases are passed explicitly. */
typedef struct ssl_ctx_st SslContext;
typedef struct ssl_st Ssl;
typedef struct ssl_method_st SslMethod;

#define A6 __reg("a6")
#define A0 __reg("a0")
#define A1 __reg("a1")
#define A2 __reg("a2")
#define D0 __reg("d0")
#define D1 __reg("d1")
#define D2 __reg("d2")
LONG tls_master_init(A6 struct Library *, D0 LONG, D1 LONG) = "\tjsr\t-30(a6)";
struct Library *tls_open(A6 struct Library *) = "\tjsr\t-36(a6)";
void tls_close(A6 struct Library *) = "\tjsr\t-42(a6)";
LONG tls_init(A6 struct Library *, A0 struct TagItem *) = "\tjsr\t-36(a6)";
LONG tls_cleanup(A6 struct Library *, A0 struct TagItem *) = "\tjsr\t-42(a6)";
const SslMethod *tls_method(A6 struct Library *) = "\tjsr\t-26934(a6)";
SslContext *tls_context(A6 struct Library *, A0 const SslMethod *) = "\tjsr\t-8208(a6)";
void tls_free_context(A6 struct Library *, A0 SslContext *) = "\tjsr\t-8214(a6)";
int tls_ca(A6 struct Library *, A0 SslContext *, A1 const char *, A2 const char *) = "\tjsr\t-9174(a6)";
void tls_verify(A6 struct Library *, A0 SslContext *, D0 int, A1 APTR) = "\tjsr\t-8700(a6)";
Ssl *tls_session(A6 struct Library *, A0 SslContext *) = "\tjsr\t-8784(a6)";
void tls_free_session(A6 struct Library *, A0 Ssl *) = "\tjsr\t-8820(a6)";
int tls_fd(A6 struct Library *, A0 Ssl *, D0 int) = "\tjsr\t-8358(a6)";
int tls_host(A6 struct Library *, A0 Ssl *, A1 const char *) = "\tjsr\t-26856(a6)";
long tls_control(A6 struct Library *, A0 Ssl *, D0 int, D1 long, A1 const void *) = "\tjsr\t-8856(a6)";
int tls_connect(A6 struct Library *, A0 Ssl *) = "\tjsr\t-8832(a6)";
int tls_write(A6 struct Library *, A0 Ssl *, A1 const void *, D0 int) = "\tjsr\t-8850(a6)";
int tls_read(A6 struct Library *, A0 Ssl *, A1 void *, D0 int) = "\tjsr\t-8838(a6)";
LONG tcp_connect(A6 struct Library *, D0 LONG, A0 struct sockaddr *, D1 LONG) = "\tjsr\t-54(a6)";
LONG tcp_option(A6 struct Library *, D0 LONG, D1 LONG, D2 LONG, A0 const void *, __reg("d3") LONG) = "\tjsr\t-90(a6)";
#undef A6
#undef A0
#undef A1
#undef A2
#undef D0
#undef D1
#undef D2
#else
typedef SSL_CTX SslContext;
typedef SSL Ssl;
struct Library *AmiSSLBase, *AmiSSLMasterBase;
#define tls_master_init(base, api, structs) (AmiSSLMasterBase = (base), InitAmiSSLMaster((api), (structs)))
#define tls_open(base) (AmiSSLBase = OpenAmiSSL())
#define tls_close(base) CloseAmiSSL()
#define tls_init(base, tags) InitAmiSSLA(tags)
#define tls_cleanup(base, tags) CleanupAmiSSLA(tags)
#define tls_method(base) TLS_client_method()
#define tls_context(base, method) SSL_CTX_new(method)
#define tls_free_context(base, ctx) SSL_CTX_free(ctx)
#define tls_ca(base, ctx, file, path) SSL_CTX_load_verify_locations((ctx), (file), (path))
#define tls_verify(base, ctx, mode, callback) SSL_CTX_set_verify((ctx), (mode), (callback))
#define tls_session(base, ctx) SSL_new(ctx)
#define tls_free_session(base, ssl) SSL_free(ssl)
#define tls_fd(base, ssl, fd) SSL_set_fd((ssl), (fd))
#define tls_host(base, ssl, host) SSL_set1_host((ssl), (host))
#define tls_control(base, ssl, cmd, arg, pointer) SSL_ctrl((ssl), (cmd), (arg), (void *)(pointer))
#define tls_connect(base, ssl) SSL_connect(ssl)
#define tls_write(base, ssl, buf, len) SSL_write((ssl), (buf), (len))
#define tls_read(base, ssl, buf, len) SSL_read((ssl), (buf), (len))
#define tcp_connect(base, fd, address, length) connect((fd), (address), (length))
#define tcp_option(base, fd, level, opt, arg, length) setsockopt((fd), (level), (opt), (arg), (length))
#endif

enum { AMISSL_V4_API = 0x10, /* AmiSSL 4.10/4.11, compatible with 4.12 */
       TLS_VERIFY_PEER = 1, TLS_SNI = 55, TCP_STREAM = 1,
       TCP_OPTIONS = 0xffff, TCP_SEND_TIMEOUT = 0x1005, TCP_RECEIVE_TIMEOUT = 0x1006 };

/* Runs in a separate instance of this executable so TLS never blocks the menu. */
int amiga_public_worker(void)
{
    static const char request[] = REQUEST;
    struct Library *master = NULL, *tls = NULL;
    SslContext *ctx = NULL;
    Ssl *ssl = NULL;
    struct sockaddr_in address;
    struct TagItem tags[3];
    struct { LONG seconds, micros; } timeout = { 12, 0 };
    NetAddr peer;
    int socket_fd = -1, initialized = 0, error = 0, result = 20, n;
    size_t total = 0;
    char buffer[2048];

    if (net_init() || net_resolve(DIRECTORY_HOST, 443, &peer)) goto done;
    master = OpenLibrary((CONST_STRPTR)"amisslmaster.library", 4);
    if (!master || !tls_master_init(master, AMISSL_V4_API, 0)) goto done;
    tls = tls_open(master);
    if (!tls) goto done;
    tags[0].ti_Tag = TAG_USER + 1;
    tags[0].ti_Data = (ULONG)SocketBase;
    tags[1].ti_Tag = TAG_USER + 11;
    tags[1].ti_Data = (ULONG)&error;
    tags[2].ti_Tag = TAG_DONE;
    tags[2].ti_Data = 0;
    if (tls_init(tls, tags)) goto done;
    initialized = 1;
    ctx = tls_context(tls, tls_method(tls));
    if (!ctx || !tls_ca(tls, ctx, NULL, "AmiSSL:Certs")) goto done;
    tls_verify(tls, ctx, TLS_VERIFY_PEER, NULL);
    ssl = tls_session(tls, ctx);
    if (!ssl || !tls_host(tls, ssl, DIRECTORY_HOST) ||
        !tls_control(tls, ssl, TLS_SNI, 0, DIRECTORY_HOST)) goto done;

    socket_fd = (int)socket(AF_INET, TCP_STREAM, 0);
    if (socket_fd < 0) goto done;
    tcp_option(SocketBase, socket_fd, TCP_OPTIONS, TCP_SEND_TIMEOUT, &timeout, sizeof timeout);
    tcp_option(SocketBase, socket_fd, TCP_OPTIONS, TCP_RECEIVE_TIMEOUT, &timeout, sizeof timeout);
    memset(&address, 0, sizeof address);
    address.sin_len = sizeof address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(peer.ip);
    address.sin_port = htons(peer.port);
    if (tcp_connect(SocketBase, socket_fd, (struct sockaddr *)&address, sizeof address) ||
        !tls_fd(tls, ssl, socket_fd) || tls_connect(tls, ssl) != 1 ||
        tls_write(tls, ssl, request, sizeof request - 1) != sizeof request - 1) goto done;
    while ((n = tls_read(tls, ssl, buffer, sizeof buffer)) > 0) {
        total += (unsigned)n;
        if (total > 65536 || fwrite(buffer, 1, (size_t)n, stdout) != (size_t)n) goto done;
    }
    if (n == 0 && total && fflush(stdout) == 0) result = 0;

done:
    if (ssl) tls_free_session(tls, ssl);
    if (socket_fd >= 0) net_close(socket_fd);
    if (ctx) tls_free_context(tls, ctx);
    if (initialized) tls_cleanup(tls, NULL);
    if (tls) tls_close(master);
    if (master) CloseLibrary(master);
    return result;
}

static void remove_files(const AmigaPublicList *list)
{
    remove(list->reply);
    remove(list->script);
    remove(list->done);
}

static const struct WBArg *worker_program;

void amiga_worker_workbench(const struct WBStartup *startup)
{
    /* Workbench keeps the tool's name and drawer lock valid until we exit. */
    if (startup && startup->sm_NumArgs > 0) worker_program = &startup->sm_ArgList[0];
}

/* the worker: this executable again, with args, in the background (C:Execute of a script in T:), its output
 * into list->reply and OK or FAIL into list->done when it's finished */
static int start_job(AmigaPublicList *list, const char *args)
{
    unsigned long id = (unsigned long)FindTask(NULL);
    char command[64], directory[240], program[240];
    const char *name;
    BPTR input, output, program_dir;
    FILE *file;
    int failed;
    size_t n;
    if (list->pending) return 0;
    program_dir = worker_program ? worker_program->wa_Lock : GetProgramDir();
    if (!program_dir || !NameFromLock(program_dir, (STRPTR)directory, sizeof directory)) return -1;
    if (worker_program) name = (const char *)worker_program->wa_Name;
    else {
        if (!GetProgramName((STRPTR)program, sizeof program)) return -1;
        name = (const char *)FilePart((STRPTR)program);
    }
    if (!name || !name[0]) return -1;
    if (strpbrk(directory, "\"*\r\n") || strpbrk(name, "\"*:/\r\n")) return -1;
    n = strlen(directory);
    if (!n) return -1;
    snprintf(list->reply, sizeof list->reply, "T:ofp-%lx.out", id);
    snprintf(list->script, sizeof list->script, "T:ofp-%lx.script", id);
    snprintf(list->done, sizeof list->done, "T:ofp-%lx.done", id);
    remove_files(list);

    file = fopen(list->script, "w");
    if (!file) return -1;
    fprintf(file,
            "FailAt 21\nStack 65536\n"
            "\"%s%s%s\" %s >%s\n"
            "If WARN\n  Echo FAIL >%s\nElse\n  Echo OK >%s\nEndIf\n",
            directory, directory[n - 1] == ':' || directory[n - 1] == '/' ? "" : "/", name, args,
            list->reply, list->done, list->done);
    failed = ferror(file);
    if (fclose(file) || failed) { remove_files(list); return -1; }
    snprintf(command, sizeof command, "C:Execute %s", list->script);
    input = Open((STRPTR)"NIL:", MODE_OLDFILE);
    output = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (!input || !output ||
        SystemTags(command, SYS_Input, input, SYS_Output, output, SYS_Asynch, TRUE, TAG_DONE) == -1) {
        if (input) Close(input);
        if (output) Close(output);
        remove_files(list);
        return -1;
    }
    list->started = net_ms();
    list->pending = 1;
    return 0;
}

int amiga_public_start(AmigaPublicList *list) { return start_job(list, "--public-fetch"); }

/* ---- the map editor's F8: a map to the web site's gallery (action=map_upload), over plain HTTP ---- */

/* the game's part: the worker started for MY MAP (path) by its author (the profile's name) */
int amiga_share_start(AmigaPublicList *job, const char *path, const char *author)
{
    char args[160], who[16];
    int i, j = 0;
    if (strpbrk(path, "\"*\r\n ")) return -1;
    for (i = 0; author[i] && j < 15; i++)            /* (a name the Shell takes as it is) */
        if ((author[i] >= 'A' && author[i] <= 'Z') || (author[i] >= 'a' && author[i] <= 'z') ||
            (author[i] >= '0' && author[i] <= '9') || author[i] == ' ') who[j++] = author[i];
    who[j] = 0;
    snprintf(args, sizeof args, "--share-map %s \"%s\"", path, who);
    return start_job(job, args);
}

/* 0 while it's on its way; 1 shared, -2 ten maps a day already, -1 it didn't work */
int amiga_share_poll(AmigaPublicList *job)
{
    char result[8], line[128];
    FILE *file;
    int ok, code = 0;
    if (!job->pending) return 0;
    if (!(file = fopen(job->done, "r"))) return 0;
    ok = fgets(result, sizeof result, file) != NULL;
    fclose(file);
    if (!ok) return 0;
    job->pending = 0;
    if (!strncmp(result, "OK", 2) && (file = fopen(job->reply, "r")) != NULL) {
        if (fgets(line, sizeof line, file)) sscanf(line, "HTTP/%*s %d", &code);
        fclose(file);
    }
    remove_files(job);
    return code == 200 ? 1 : code == 429 ? -2 : -1;
}

static int server_row(char *line, AmigaPublicServer *out)
{
    char *column[7], *end, host[AMIGA_ADDRESS];
    uint16_t port;
    int i;
    column[0] = line;
    for (i = 1; i < 7; i++) {
        end = strchr(column[i - 1], '\t');
        if (!end) return 0;
        *end = 0;
        column[i] = end + 1;
    }
    end = strchr(column[6], '\t');
    out->build = 0;
    if (end) {
        char *field = end + 1;
        *end = 0;
        for (i = 7; i < 10 && field; i++) {
            field = strchr(field, '\t');
            if (field) field++;
        }
        if (field) out->build = (uint32_t)strtoul(field, NULL, 10);
    }
    if (strlen(column[0]) + strlen(column[1]) + 1 >= sizeof out->address) return 0;
    snprintf(out->address, sizeof out->address, "%s:%s", column[0], column[1]);
    if (amiga_server_address(out->address, host, &port)) return 0;
    for (i = 0; column[2][i]; i++)
        if ((unsigned char)column[2][i] < 32 || (unsigned char)column[2][i] >= 127) column[2][i] = '?';
    snprintf(out->name, sizeof out->name, "%.34s", column[2][0] ? column[2] : out->address);
    out->players = (unsigned)atoi(column[3]);
    out->capacity = (unsigned)atoi(column[4]);
    out->locked = atoi(column[5]) != 0;
    out->running = atoi(column[6]) != 0;
    return out->capacity > 0 && out->capacity <= 255 && out->players <= out->capacity;
}

static int read_reply(AmigaPublicList *list)
{
    AmigaPublicServer rows[AMIGA_PUBLIC_SERVERS];
    char line[768];
    int count = 0, code, header_end = 0, chunked = 0, content = 0;
    FILE *file = fopen(list->reply, "r");
    if (!file) return -1;
    if (!fgets(line, sizeof line, file) ||
        sscanf(line, "HTTP/%*s %d", &code) != 1 || code != 200) {
        fclose(file);
        return -1;
    }
    while (fgets(line, sizeof line, file)) {
        if (line[0] == '\r' || line[0] == '\n') { header_end = 1; break; }
        if (strstr(line, "chunked")) chunked = 1;
    }
    if (!header_end || chunked) { fclose(file); return -1; }
    while (count < AMIGA_PUBLIC_SERVERS && fgets(line, sizeof line, file)) {
        if (!strchr(line, '\n') && !feof(file)) {
            int ch;
            do { ch = fgetc(file); } while (ch != '\n' && ch != EOF);
            continue;
        }
        line[strcspn(line, "\r\n")] = 0;
        if (line[0]) content = 1;
        if (server_row(line, &rows[count])) count++;
    }
    if (ferror(file) || (content && !count)) { fclose(file); return -1; }
    fclose(file);
    memcpy(list->server, rows, count * sizeof rows[0]);
    list->count = count;
    return 1;
}

int amiga_public_poll(AmigaPublicList *list)
{
    char result[8];
    FILE *file;
    int ok;
    if (!list->pending) return 0;
    file = fopen(list->done, "r");
    if (!file) return 0;
    ok = fgets(result, sizeof result, file) != NULL;
    fclose(file);
    if (!ok) return 0;
    list->pending = 0;
    ok = !strncmp(result, "OK", 2) ? read_reply(list) : -1;
    remove_files(list);
    return ok;
}
