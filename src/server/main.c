/* SPDX-License-Identifier: MIT
 * ofp_server: the dedicated OpenFP server (headless: no SDL needed).
 *
 *   ofp_server [-c server.cfg] [--port N] [--name TEXT] [--public] [--quiet] [--check]
 *
 * --check prints the settings as the server understood them, and stops.
 *
 * Everything else is in server.cfg (see server.cfg.example): name, password,
 * players, message of the day, the game, the statistics database, the chat
 * log, the log file, and whether to publish the server on a listing service.
 * It runs a lobby (chat, getting ready, the leaderboard) and one match after
 * another until stopped (Ctrl-C / SIGTERM).
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L
#endif
#include <signal.h>
#include <ctype.h>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>                                 /* (pruning replays/) */
#include <sys/stat.h>
#include "common/server.h"
#include "common/http.h"
#include "server/config.h"
#include "server/db.h"
#include "common/achieve.h"
#include "common/rules.h"
#include "common/dump.h"
#include "common/crash.h"
#include <signal.h>

static Server srv;
static ServerSettings st;
static FILE *log_fp;
static volatile sig_atomic_t stop;
static int quiet;
static time_t started_at;

static volatile sig_atomic_t reload;
static char cfg_file[512];

static void on_signal(int sig) { (void)sig; stop = 1; }
#ifdef SIGHUP
static void on_hup(int sig) { (void)sig; reload = 1; }
#endif

/* ---- maps: one file, a list ("a.ofpmap, b.ofpmap") or a folder ("maps/") ---------- */
static char *map_texts[NET_MAPS];
static int nmap_texts;

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = n > 0 && n < 4000000 ? (char *)malloc((size_t)n + 1) : NULL;
    if (b && fread(b, 1, (size_t)n, f) == (size_t)n) b[n] = 0; else { free(b); b = NULL; }
    fclose(f);
    return b;
}

static void log_line(void *ctx, int level, const char *line);

static const char *goal_name(uint8_t extras)
{
    return extras & EXTRA_CONQUEST ? "conquest" : extras & EXTRA_ANNIHILATION ? "annihilation"
         : extras & EXTRA_TDM ? "deathmatch" : extras & EXTRA_WARLORD ? "warlord"
         : extras & EXTRA_KILLCONF ? "killconfirmed" : extras & EXTRA_HARDPOINT ? "hardpoint"
         : extras & EXTRA_HILL ? "hill" : "flag";
}

/* a map from the web site's gallery, by name */
static char *gallery_map(const char *name)
{
    char url[700], enc[200] = "", *out;
    int st2;
    size_t i, n = 0;
    if (!st.listing_url[0]) return NULL;
    for (i = 0; name[i] && n + 4 < sizeof enc; i++) {    /* (URL-encoded) */
        unsigned char ch = (unsigned char)name[i];
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '.') enc[n++] = (char)ch;
        else n += (size_t)snprintf(enc + n, sizeof enc - n, "%%%02X", ch);
    }
    enc[n] = 0;
    snprintf(url, sizeof url, "%.450s%saction=map&name=%s", st.listing_url, strchr(st.listing_url, '?') ? "&" : "?", enc);
    if (!(out = (char *)malloc(210000))) return NULL;
    st2 = http_request(url, NULL, out, 210000, 8000);
    if (st2 != 200 || strncmp(out, "OFPMAP", 6)) { free(out); return NULL; }
    {   /* a stranger's map: only one this game reads and plays, within the limits, before anyone downloads it */
        static Game check;
        Map m;
        Base bases[2];
        Biome biome;
        char nm[64], why[120];
        MatchSetup ms;
        const char *p = out;
        int structs = 0;
        why[0] = 0;
        while ((p = strstr(p, "\nstruct=")) != NULL) { structs++; p++; }
        if (strlen(out) > 200000) snprintf(why, sizeof why, "too big");
        else if (structs > MAX_STRUCTS) snprintf(why, sizeof why, "%d buildings (at most %d)", structs, MAX_STRUCTS);
        else if (mapfile_load(&m, bases, &biome, nm, sizeof nm, out, 1) != 0) snprintf(why, sizeof why, "the game can't read it");
        else {
            memset(&ms, 0, sizeof ms);
            ms.seed = 1; ms.players = 2; ms.camp_garages = 4; ms.biome = (uint8_t)biome;
            if (match_start(&check, &ms, out) != 0) snprintf(why, sizeof why, "a match can't start on it");
        }
        if (why[0]) {
            char line[300];
            snprintf(line, sizeof line, "gallery map %s: %s: skipped", name, why);
            log_line(NULL, LOG_WARN, line);
            free(out);
            return NULL;
        }
    }
    return out;
}

static void add_map(ServerConfig *sc, const char *path)
{
    char *t, *nm;
    const char *base = strrchr(path, '/');
    if (nmap_texts >= NET_MAPS) return;
    if (!strncmp(path, "gallery:", 8)) {                 /* gallery:NAME: from the web site */
        if (!(t = gallery_map(path + 8))) { fprintf(stderr, "gallery map %s isn't there: skipped\n", path + 8); return; }
    } else if (!(t = read_file(path))) { fprintf(stderr, "map %s can't be read: skipped\n", path); return; }
    if (mapfile_biome(t) == BIOME_SCIFI) {
        fprintf(stderr, "map %s uses the retired Sci-Fi world: skipped\n", path);
        free(t);
        return;
    }
    map_texts[nmap_texts++] = t;
    sc->maps[sc->nmaps] = t;
    nm = strstr(t, "\nname=");                          /* its own name, or the file's */
    if (nm) {
        int k = 0;
        nm += 6;
        while (nm[k] && nm[k] != '\n' && nm[k] != '\r' && k < 31) k++;
        snprintf(sc->map_names[sc->nmaps], sizeof sc->map_names[0], "%.*s", k, nm);
    } else snprintf(sc->map_names[sc->nmaps], sizeof sc->map_names[0], "%.31s", base ? base + 1 : path);
    sc->nmaps++;
}

static int by_name_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void load_maps(const ServerSettings *set, ServerConfig *sc)
{
    char list[1024], *p, *tok;
    int i;
    for (i = 0; i < nmap_texts; i++) free(map_texts[i]);
    nmap_texts = 0;
    sc->nmaps = 0;
    sc->map = NULL;
    snprintf(list, sizeof list, "%s", set->maps[0] ? set->maps : set->map_file);
    for (p = list; (tok = strtok(p, ",")) != NULL; p = NULL) {
        size_t n;
        while (isspace((unsigned char)*tok)) tok++;
        n = strlen(tok);
        while (n && isspace((unsigned char)tok[n - 1])) tok[--n] = 0;
        if (!n) continue;
#ifndef _WIN32
        if (tok[n - 1] == '/') {                          /* a folder: every .ofpmap in it, by name */
            DIR *d = opendir(tok);
            struct dirent *e;
            char *names[NET_MAPS];
            int k = 0, j;
            if (!d) { fprintf(stderr, "maps folder %s can't be read\n", tok); continue; }
            while ((e = readdir(d)) != NULL && k < NET_MAPS) {
                size_t l = strlen(e->d_name);
                if (l > 7 && !strcmp(e->d_name + l - 7, ".ofpmap") && (names[k] = (char *)malloc(l + 1)) != NULL)
                    memcpy(names[k++], e->d_name, l + 1);
            }
            closedir(d);
            qsort(names, (size_t)k, sizeof names[0], by_name_cmp);
            for (j = 0; j < k; j++) {
                char path[1200];
                snprintf(path, sizeof path, "%s%s", tok, names[j]);
                add_map(sc, path);
                free(names[j]);
            }
            continue;
        }
#endif
        add_map(sc, tok);
    }
}

/* ---- status for monitoring: a JSON file and a Prometheus textfile ------------------- */
static void write_status(void)
{
    ServerInfo info;
    char tmp[600];
    FILE *f;
    int i, playing = 0, watching = 0;
    server_info(&srv, &info);
    for (i = 0; i < NET_LOBBY_MAX; i++)
        if (srv.c[i].used && srv.c[i].authed && !srv.c[i].gone) { playing += srv.c[i].slot >= 0; watching += srv.c[i].spectating; }
    if (st.status_file[0]) {
        snprintf(tmp, sizeof tmp, "%.500s.tmp", st.status_file);
        if ((f = fopen(tmp, "w")) != NULL) {
            fprintf(f, "{\n  \"name\": \"%s\",\n  \"uptime\": %ld,\n  \"state\": \"%s\",\n  \"people\": %d,\n"
                       "  \"max_players\": %d,\n  \"playing\": %d,\n  \"watching\": %d,\n  \"matches\": %d,\n"
                       "  \"tick\": %u,\n  \"map\": \"%s\",\n  \"delay_ticks\": %d,\n  \"packets_in\": %u,\n"
                       "  \"packets_out\": %u,\n  \"bytes_in\": %u,\n  \"bytes_out\": %u\n}\n",
                    info.name, (long)(time(NULL) - started_at),
                    srv.state == SRV_RUNNING ? "playing" : srv.state == SRV_LOADING ? "loading" : srv.state == SRV_COUNTDOWN ? "countdown" : "lobby",
                    info.players, info.max_players, playing, watching, srv.matches_played, (unsigned)srv.tick, info.map,
                    srv.delay, (unsigned)srv.pkts_in, (unsigned)srv.pkts_out, (unsigned)srv.bytes_in, (unsigned)srv.bytes_out);
            fclose(f);
            rename(tmp, st.status_file);
        }
    }
    if (st.metrics_file[0]) {                            /* node_exporter's textfile collector reads these */
        snprintf(tmp, sizeof tmp, "%.500s.tmp", st.metrics_file);
        if ((f = fopen(tmp, "w")) != NULL) {
            fprintf(f, "# HELP ofp_desyncs Players out of step since the server started.\n# TYPE ofp_desyncs counter\nofp_desyncs %d\n",
                    srv.desyncs);
            fprintf(f, "# HELP ofp_people People on the server (lobby and matches).\n# TYPE ofp_people gauge\nofp_people %d\n"
                       "# TYPE ofp_playing gauge\nofp_playing %d\n# TYPE ofp_watching gauge\nofp_watching %d\n"
                       "# TYPE ofp_max_players gauge\nofp_max_players %d\n# TYPE ofp_match_running gauge\nofp_match_running %d\n"
                       "# TYPE ofp_matches_total counter\nofp_matches_total %d\n# TYPE ofp_uptime_seconds counter\nofp_uptime_seconds %ld\n"
                       "# TYPE ofp_input_delay_ticks gauge\nofp_input_delay_ticks %d\n"
                       "# TYPE ofp_packets_received_total counter\nofp_packets_received_total %u\n"
                       "# TYPE ofp_packets_sent_total counter\nofp_packets_sent_total %u\n"
                       "# TYPE ofp_bytes_received_total counter\nofp_bytes_received_total %u\n"
                       "# TYPE ofp_bytes_sent_total counter\nofp_bytes_sent_total %u\n",
                    info.players, playing, watching, info.max_players, srv.state == SRV_RUNNING, srv.matches_played,
                    (long)(time(NULL) - started_at), srv.delay, (unsigned)srv.pkts_in, (unsigned)srv.pkts_out,
                    (unsigned)srv.bytes_in, (unsigned)srv.bytes_out);
            fclose(f);
            rename(tmp, st.metrics_file);
        }
    }
}

static void log_line(void *ctx, int level, const char *line);

/* ---- backups: on demand (--backup, the console's backup) and every backup_every hours */
static int backup_to(const char *path, char *out, size_t cap)
{
    if (db_backup(path) != 0) { snprintf(out, cap, "backup to %.300s failed (no database, or can't write there)", path); return -1; }
    snprintf(out, cap, "database backed up to %.300s", path);
    return 0;
}

static int by_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void backup_rotate(void)
{
    char path[1100], stamp[32], msg[400];
    time_t now = time(NULL);
    if (!st.backup_dir[0] || !st.database[0]) return;
#ifndef _WIN32
    mkdir(st.backup_dir, 0750);
#endif
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
    snprintf(path, sizeof path, "%.900s/ofp_server-%s.db", st.backup_dir, stamp);
    backup_to(path, msg, sizeof msg);
    log_line(NULL, LOG_INFO, msg);
#ifndef _WIN32
    {   /* keep the newest backup_keep */
        DIR *d = opendir(st.backup_dir);
        struct dirent *e;
        char *names[256];
        int n = 0, i;
        if (!d) return;
        while ((e = readdir(d)) != NULL && n < 256)
            if (!strncmp(e->d_name, "ofp_server-", 11) && strstr(e->d_name, ".db")) {
                size_t l = strlen(e->d_name);
                if ((names[n] = (char *)malloc(l + 1)) != NULL) memcpy(names[n++], e->d_name, l + 1);
            }
        closedir(d);
        qsort(names, (size_t)n, sizeof names[0], by_str);
        for (i = 0; i < n; i++) {
            if (i < n - st.backup_keep) { snprintf(path, sizeof path, "%.900s/%.100s", st.backup_dir, names[i]); remove(path); }
            free(names[i]);
        }
    }
#endif
}

static void crash_save_server(const char *path) { if (srv.state == SRV_RUNNING) replay_save(&srv.replay, path); }

static void keep_serving(void *ctx);

/* ---- live on the web site: the match so far, every 10 s (as far as spectators may see) - */
static void send_live(int over)
{
    static char path[] = "ofp_server-live.tmp";
    char *body = NULL, *text = NULL, num[24], resp[200];
    size_t cap = 0;
    if (!st.public_listed || !st.listing_url[0] || !st.share_replays || !st.allow_watching) return;
    http_form_grow(&body, &cap, "action", "live");
    snprintf(num, sizeof num, "%u", (unsigned)st.port);
    http_form_grow(&body, &cap, "port", num);
    if (!over && srv.state == SRV_RUNNING) {
        Replay so_far = srv.replay;                      /* (a look at it, cut at what spectators may see) */
        uint32_t delay = (uint32_t)st.spectator_delay_s * TICK_RATE;
        so_far.ticks = so_far.ticks > delay ? so_far.ticks - delay : 0;
        if (so_far.nchecks > so_far.ticks / REPLAY_CHECK_EVERY) so_far.nchecks = so_far.ticks / REPLAY_CHECK_EVERY;
        if (so_far.ticks < TICK_RATE || replay_save(&so_far, path) != 0 || !(text = read_file(path))) { free(body); return; }
        http_form_grow(&body, &cap, "replay", text);
        free(text);
        remove(path);
    } else http_form_grow(&body, &cap, "over", "1");    /* (the match is over: off the page) */
    if (body) http_request_idle(st.listing_url, body, resp, sizeof resp, 8000, keep_serving, NULL);
    free(body);
}

/* ---- replays and desync files: the newest kept, the rest deleted (keep: how many, max_bytes: how big) ---- */
static int by_name(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static int prune_dir(const char *dir, const char *prefix, int keep, long long max_bytes)
{
    DIR *d = opendir(dir[0] ? dir : ".");
    struct dirent *e;
    char **names = NULL, path[1200];
    int n = 0, cap = 0, i, gone = 0;
    long long total = 0;
    size_t plen = strlen(prefix);
    if (!d) return 0;
    while ((e = readdir(d)) != NULL) {                   /* (their names start with the time: by name is by age) */
        if (strncmp(e->d_name, prefix, plen)) continue;
        if (n == cap) {
            char **more = realloc(names, (size_t)(cap = cap ? cap * 2 : 64) * sizeof *names);
            if (!more) break;
            names = more;
        }
        if (!(names[n] = malloc(strlen(e->d_name) + 1))) break;
        strcpy(names[n++], e->d_name);
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof *names, by_name);
    for (i = n - 1; i >= 0; i--) {                       /* newest first: what stays */
        struct stat sb;
        int keep_it;
        snprintf(path, sizeof path, "%.800s/%s", dir[0] ? dir : ".", names[i]);
        if (stat(path, &sb) != 0) continue;
        total += (long long)sb.st_size;
        keep_it = (!keep || n - 1 - i < keep) && (!max_bytes || total <= max_bytes);
        if (!keep_it && remove(path) == 0) gone++;
    }
    for (i = 0; i < n; i++) free(names[i]);
    free(names);
    return gone;
}

static void prune_files(void)
{
    int gone = 0;
    char line[160];
    if (st.replay_dir[0]) gone += prune_dir(st.replay_dir, "match-", st.replays_keep, (long long)st.replays_max_mb << 20);
    if (st.desyncs_keep) gone += prune_dir("", "desync-", st.desyncs_keep * 2, 0);   /* (a replay and a dump each) */
    if (gone > 0) {
        snprintf(line, sizeof line, "replays and desync files: deleted the %d oldest", gone);
        log_line(NULL, LOG_INFO, line);
    }
}

/* ---- someone out of step: the match's replay, and our state at that tick, kept ------ */
static void on_desync(void *ctx, const Server *s, const char *name, uint32_t build, uint32_t t, uint32_t theirs, uint32_t ours)
{
    static Game scratch;
    char stamp[32], rep_path[300], dump_path[300], line[1400], who[24];
    time_t now = time(NULL);
    FILE *f;
    int i;
    (void)ctx;
    snprintf(who, sizeof who, "%.15s", name);
    for (i = 0; who[i]; i++) if (!((who[i] >= 'a' && who[i] <= 'z') || (who[i] >= 'A' && who[i] <= 'Z') || (who[i] >= '0' && who[i] <= '9'))) who[i] = '_';
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
    snprintf(rep_path, sizeof rep_path, "desync-%s-%s-tick%u.ofprep", stamp, who, (unsigned)t);
    snprintf(dump_path, sizeof dump_path, "desync-%s-%s-tick%u-server.txt", stamp, who, (unsigned)t);
    if (replay_save(&s->replay, rep_path) != 0) return;
    if ((f = fopen(dump_path, "w")) != NULL) { replay_dump_at(&s->replay, t + 1, &scratch, f); fclose(f); }   /* (after tick t) */
    {   /* (their build beside ours: two builds out of step is a rules difference, not a bug in one) */
        char b[160];
        snprintf(b, sizeof b, "out of step: %s's game is build %08x (protocol %u, %u tanks), the server's %08x%s",
                 name, (unsigned)build, (unsigned)(build >> 16), (unsigned)((build >> 8) & 255),
                 (unsigned)net_build_id(), build == net_build_id() ? "" : ": not the same build");
        log_line(NULL, LOG_WARN, b);
    }
    snprintf(line, sizeof line, "out of step: %s at tick %u (theirs %08x, ours %08x). Kept: %s and %s. Their game "
             "writes its state there too (desync-...-game.txt by its rules, -game-live.txt as it was); compare: ofp_match diff %s THEIRS.txt", name, (unsigned)t,
             (unsigned)theirs, (unsigned)ours, rep_path, dump_path, dump_path);
    log_line(NULL, LOG_WARN, line);
}

/* ---- the console: commands typed on the terminal the server runs in --------------- */
static void console(void)
{
#ifndef _WIN32
    static char line[512];
    static size_t have;
    static int off;
    char ch;
    if (off) return;
    for (;;) {
        ssize_t r = read(0, &ch, 1);
        if (r == 0) { off = 1; return; }                 /* no terminal (a service): no console */
        if (r < 0) return;
        if (ch == '\n') {
            char out[1200];
            line[have] = 0;
            have = 0;
            if (!line[0]) continue;
            if (!strncmp(line, "backup", 6)) {          /* (the database lives out here, not in the server) */
                char path[600];
                time_t now = time(NULL);
#ifndef _WIN32
                if (st.backup_dir[0]) mkdir(st.backup_dir, 0750);
#endif
                if (line[6] == ' ' && line[7]) snprintf(path, sizeof path, "%.500s", line + 7);
                else {
                    char stamp[32];
                    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
                    snprintf(path, sizeof path, "%.400s/ofp_server-%s.db", st.backup_dir[0] ? st.backup_dir : ".", stamp);
                }
                backup_to(path, out, sizeof out);
            } else server_command(&srv, line, out, sizeof out);
            if (out[0]) { printf("%s\n", out); fflush(stdout); }
            if (log_fp) { fprintf(log_fp, "console: %s -> %s\n", line, out); fflush(log_fp); }
        } else if (have + 1 < sizeof line) line[have++] = ch;
    }
#endif
}

static void log_line(void *ctx, int level, const char *msg)
{
    static const char *names[] = { "debug", "info", "warn", "error" };
    char when[32];
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    (void)ctx;
    if (level < st.log_level) return;
    strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", tm);
    if (log_fp) { fprintf(log_fp, "%s [%s] %s\n", when, names[level & 3], msg); fflush(log_fp); }
    if (!quiet) { printf("%s [%s] %s\n", when, names[level & 3], msg); fflush(stdout); }
}

static void on_chat(void *ctx, const char *name, const char *text)
{
    (void)ctx;
    if (st.chat_log) db_chat(name, text);
}

static void keep_serving(void *ctx);
static char *read_file(const char *path);

/* a finished match, to the listing (public servers with share_matches): the result,
 * everyone's part and rating, and (share_replays) the replay to download */
static char *pending_match;                  /* built at the end of a match, sent from the main loop */

/* ---- webhooks: a line in a chat room (Rocket.Chat, Slack, Mattermost, Matrix's hookshot:
 * {"text": ...}; Discord: {"content": ...}) when matches end, the server starts or stops */
static char hook_queue[8][900];
static int hook_n;

static int hook_wants(const char *event) { return st.webhook_url[0] && strstr(st.webhook_events, event) != NULL; }

static void hook_say(const char *event, const char *text)
{
    if (!hook_wants(event) || hook_n >= 8) return;
    snprintf(hook_queue[hook_n++], sizeof hook_queue[0], "%s", text);
}

static void hook_send_all(void)
{
    int i;
    for (i = 0; i < hook_n; i++) {
        char body[2000], esc[1900], resp[200];
        int status;
        http_json_str(esc, sizeof esc, hook_queue[i]);
        snprintf(body, sizeof body, "{\"%s\": %s, \"username\": \"OpenFP\"}",
                 !strcmp(st.webhook_format, "discord") ? "content" : "text", esc);
        status = http_post_json(st.webhook_url, body, resp, sizeof resp, 8000, keep_serving, NULL);
        if (status < 200 || status >= 300) {
            char line[200];
            snprintf(line, sizeof line, "webhook: not taken (%d)", status);
            log_line(NULL, LOG_WARN, line);
        }
    }
    hook_n = 0;
}

/* "Anna and Bert" */
static void names_of(const MatchResult *r, int side, char *out, size_t cap)
{
    int p, n = 0, total = 0;
    out[0] = 0;
    for (p = 0; p < r->players; p++) total += r->team[p] == side;
    for (p = 0; p < r->players; p++) {
        size_t l = strlen(out);
        if (r->team[p] != side) continue;
        snprintf(out + l, cap - l, "%s%s", n == 0 ? "" : n == total - 1 ? " and " : ", ", r->bot[p] ? "a computer" : r->name[p]);
        n++;
    }
}

static void hook_match(const Server *s, const MatchResult *r)
{
    char a[200], b[200], text[900], ch[200] = "";
    int p, secs = (int)(r->ticks / TICK_RATE);
    if (!hook_wants("matches")) return;
    names_of(r, r->winner == TEAM_ENEMY ? TEAM_ENEMY : TEAM_PLAYER, a, sizeof a);
    names_of(r, r->winner == TEAM_ENEMY ? TEAM_PLAYER : TEAM_ENEMY, b, sizeof b);
    for (p = 0; p < r->players; p++)
        if (!r->bot[p] && r->rating_change[p]) {
            size_t l = strlen(ch);
            snprintf(ch + l, sizeof ch - l, "%s%s %+d", l ? ", " : "", r->name[p], r->rating_change[p]);
        }
    if (r->winner < 0)
        snprintf(text, sizeof text, "%.40s: %.150s and %.150s drew on %s after %d:%02d.", st.name, a, b, s->map ? s->map_name : "a random map",
                 secs / 60, secs % 60);
    else if (!r->versus)
        snprintf(text, sizeof text, "%.40s: %.150s %s the enemy on %s in %d:%02d.%s%.200s%s", st.name, a,
                 r->winner == TEAM_PLAYER ? "beat" : "lost to", s->map ? s->map_name : "a random map", secs / 60, secs % 60,
                 ch[0] ? " Ratings: " : "", ch, ch[0] ? "." : "");
    else
        snprintf(text, sizeof text, "%.40s: %.150s beat %.150s on %s in %d:%02d.%s%.200s%s", st.name, a, b, s->map ? s->map_name : "a random map",
                 secs / 60, secs % 60, ch[0] ? " Ratings: " : "", ch, ch[0] ? "." : "");
    for (p = 0; p < r->players; p++) {                   /* and anything earned */
        int ach;
        for (ach = 0; ach < ACH_COUNT; ach++)
            if ((r->earned[p] >> ach) & 1) {
                size_t l = strlen(text);
                snprintf(text + l, sizeof text - l, " %s earned %s.", r->name[p], achieve_name(ach));
            }
    }
    hook_say("matches", text);
}

static void share_match(const Server *s, const MatchResult *r, const Replay *rep)
{
    char *body = NULL, num[32], line[200], players[1600] = "";
    size_t cap = 0;
    int p;
    for (p = 0; p < r->players; p++) {
        snprintf(line, sizeof line, "%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%u\n", r->bot[p] ? "Computer" : r->name[p],
                 r->team[p] == TEAM_PLAYER ? "camp" : "fortress", r->won[p], r->kills[p], r->score[p],
                 r->bot[p] ? 0 : r->rating[p], r->bot[p] ? 0 : r->rating_change[p], r->bot[p], r->cls[p],
                 (unsigned)r->earned[p]);
        if (strlen(players) + strlen(line) < sizeof players) strcat(players, line);
    }
    http_form_grow(&body, &cap, "action", "match");
    snprintf(num, sizeof num, "%u", (unsigned)st.port); http_form_grow(&body, &cap, "port", num);
    if (st.public_host[0]) http_form_grow(&body, &cap, "host", st.public_host);
    snprintf(num, sizeof num, "%u", (unsigned)(r->ticks / TICK_RATE)); http_form_grow(&body, &cap, "seconds", num);
    http_form_grow(&body, &cap, "mode", r->versus ? "versus" : "together");
    http_form_grow(&body, &cap, "goal", goal_name(s->game.extras));
    http_form_grow(&body, &cap, "world", biome_name((Biome)r->biome));
    http_form_grow(&body, &cap, "map", s->map ? s->map_name : "random");
    http_form_grow(&body, &cap, "winner", r->winner == TEAM_PLAYER ? "camp" : r->winner == TEAM_ENEMY ? "fortress" : r->draw ? "draw" : "none");
    http_form_grow(&body, &cap, "players", players);
    if (st.share_replays && rep && rep->ticks) {         /* the replay, as the file ofp_match verify reads */
        const char *tmp = "ofp_server-replay.tmp";       /* (beside the log and the database) */
        char *txt;
        if (replay_save(rep, tmp) == 0 && (txt = read_file(tmp)) != NULL) {
            if (strlen(txt) < 3000000) http_form_grow(&body, &cap, "replay", txt);
            free(txt);
        }
        remove(tmp);
    }
    /* not sent from here: this runs at the very end of a match, and the upload keeps the
     * server running meanwhile (keep_serving), which mustn't start on the same match again */
    free(pending_match);
    pending_match = body;
}

static void send_pending_match(void)
{
    char resp[256], line[200];
    char *body = pending_match;
    int status;
    pending_match = NULL;
    status = http_request_idle(st.listing_url, body, resp, sizeof resp, 15000, keep_serving, NULL);
    if (status != 200) {
        snprintf(line, sizeof line, "listing: the match wasn't taken (%s%.100s)", status < 0 ? "no answer" : "", status < 0 ? "" : resp);
        log_line(NULL, LOG_WARN, line);
    } else log_line(NULL, LOG_DEBUG, "listing: match shared");
    free(body);
}

static void on_match(void *ctx, const Server *s, MatchResult *r, const Replay *rep)
{
    (void)ctx;
    db_match(r, biome_name((Biome)s->ms.biome), s->map ? s->map_name : "random", s->ms.versus);
    if (st.replay_dir[0] && rep && rep->ticks) {          /* keep the replay too */
        char path[700], when[32];
        time_t t = time(NULL);
        strftime(when, sizeof when, "%Y%m%d-%H%M%S", localtime(&t));
        snprintf(path, sizeof path, "%.600s/match-%s.ofprep", st.replay_dir, when);
        if (replay_save(rep, path) == 0) log_line(NULL, LOG_INFO, path);
        prune_files();
    }
    if (st.public_listed && st.listing_url[0] && st.share_matches && (r->winner >= 0 || r->draw)) share_match(s, r, rep);
    hook_match(s, r);
}

static int on_leaders(void *ctx, LeaderEntry *out, int max) { (void)ctx; return db_leaders(out, max); }
static int  on_key_get(void *ctx, const uint8_t id[16], uint8_t v[32]) { (void)ctx; return db_key_get(id, v); }
static void on_key_put(void *ctx, const uint8_t id[16], const uint8_t v[32]) { (void)ctx; db_key_put(id, v); }
static int  on_name_get(void *ctx, const char *n, uint8_t id[16]) { (void)ctx; return db_name_get(n, id); }
static void on_name_put(void *ctx, const char *n, const uint8_t id[16]) { (void)ctx; db_name_put(n, id); }
static int  on_server_key(void *ctx, uint8_t id[16], uint8_t secret[32]) { (void)ctx; return db_server_key(id, secret); }
static int  on_ban_check(void *ctx, const uint8_t id[16], uint32_t ip) { (void)ctx; return db_ban_check(id, ip); }
static void on_ban_add(void *ctx, const uint8_t id[16], uint32_t ip, const char *n, const char *r) { (void)ctx; db_ban_add(id, ip, n, r); }
static int  on_ban_remove(void *ctx, const char *n) { (void)ctx; return db_ban_remove(n); }

static void keep_serving(void *ctx) { (void)ctx; server_step(&srv); }

/* tell the listing service we're here (and what's going on) */
static char join_code[8];                            /* the listing's code for this server */
static void announce(int leaving)
{
    static char body[16384];                             /* (the leaderboard and who's on take room) */
    char resp[512], num[24];
    body[0] = 0;
    ServerInfo info;
    int status;
    server_info(&srv, &info);
    http_form(body, sizeof body, "action", leaving ? "remove" : "announce");
    snprintf(num, sizeof num, "%u", (unsigned)st.port); http_form(body, sizeof body, "port", num);
    http_form(body, sizeof body, "name", info.name);
    snprintf(num, sizeof num, "%u", (unsigned)info.build); http_form(body, sizeof body, "build", num);
    snprintf(num, sizeof num, "%d", info.players); http_form(body, sizeof body, "players", num);
    snprintf(num, sizeof num, "%d", info.max_players); http_form(body, sizeof body, "max_players", num);
    http_form(body, sizeof body, "password", info.password ? "1" : "0");
    http_form(body, sizeof body, "running", info.running ? "1" : "0");
    http_form(body, sizeof body, "mode", info.versus ? "versus" : "together");
    http_form(body, sizeof body, "goal", goal_name(srv.state == SRV_RUNNING ? srv.game.extras : srv.cfg.base.extras));
    http_form(body, sizeof body, "world", info.world);
    http_form(body, sizeof body, "map", info.map);
    snprintf(num, sizeof num, "%d", srv.matches_played); http_form(body, sizeof body, "matches", num);
    snprintf(num, sizeof num, "%ld", (long)(time(NULL) - started_at)); http_form(body, sizeof body, "uptime", num);
    http_form(body, sizeof body, "motd", st.motd);
    if (st.public_host[0]) http_form(body, sizeof body, "host", st.public_host);
    if (st.public_listed == 2) http_form(body, sizeof body, "hidden", "1");   /* (public = code: not on the list) */
    if (srv.have_public) {                               /* (how the internet sees us: NAT traversal) */
        char where[64], h[64], v[80];
        int pt = 0;
        net_addr_str(&srv.public_addr, where, sizeof where);
        net_split(where, h, sizeof h, &pt);
        snprintf(v, sizeof v, "%s %d", h, pt);
        http_form(body, sizeof body, "stun", v);
    }
    /* how matches go here, for the server list */
    snprintf(num, sizeof num, "%d", srv.cfg.base.time_limit); http_form(body, sizeof body, "time_limit", num);
    http_form(body, sizeof body, "time_rule", srv.cfg.base.time_rule == TIME_RULE_DRAW ? "draw" : "points");
    if (!srv.cfg.allow_bots) snprintf(num, sizeof num, "0");
    else if (srv.cfg.fill_bots) snprintf(num, sizeof num, "fill");
    else snprintf(num, sizeof num, "%d", srv.cfg.bots);
    http_form(body, sizeof body, "bots", num);
    if (!srv.cfg.allow_watching) snprintf(num, sizeof num, "off");
    else snprintf(num, sizeof num, "%d", srv.cfg.spectator_delay_s);
    http_form(body, sizeof body, "watching", num);
    snprintf(num, sizeof num, "%d", srv.nmaps); http_form(body, sizeof body, "maps", num);
    http_form(body, sizeof body, "rotation", srv.cfg.rotation == ROT_VOTE ? "vote" : srv.cfg.rotation == ROT_RANDOM ? "random" : "order");
    http_form(body, sizeof body, "version", OFP_VERSION);
    if (srv.state == SRV_RUNNING) {                      /* the match that's on: how far in, the score */
        char v[24];
        int32_t pts[2] = { 0, 0 };
        int k;
        if (srv.game.extras & EXTRA_CONQUEST) {
            pts[TEAM_PLAYER] = srv.game.cp_score[TEAM_PLAYER];
            pts[TEAM_ENEMY] = srv.game.cp_score[TEAM_ENEMY];
        } else {
            for (k = 0; k < srv.game.num_tanks; k++) pts[srv.game.tanks[k].team & 1] += (int32_t)srv.game.tanks[k].score;
        }
        snprintf(v, sizeof v, "%u", (unsigned)(srv.tick / TICK_RATE));
        http_form(body, sizeof body, "elapsed", v);
        snprintf(v, sizeof v, "%d", (int)pts[TEAM_PLAYER]);
        http_form(body, sizeof body, "score_camp", v);
        snprintf(v, sizeof v, "%d", (int)pts[TEAM_ENEMY]);
        http_form(body, sizeof body, "score_fortress", v);
    }
    {   /* who's here (names are public anyway: the lobby, the leaderboards), so friends can find each other */
        char people[400] = "";
        int k;
        for (k = 0; k < NET_LOBBY_MAX; k++) {
            const SrvClient *c = &srv.c[k];
            if (!c->used || !c->authed || c->gone) continue;
            if (strlen(people) + strlen(c->name) + 2 < sizeof people) {
                if (people[0]) strcat(people, ",");
                strcat(people, c->name);
            }
        }
        http_form(body, sizeof body, "people", people);
    }
    {   /* our best players, for the web site's leaderboards */
        char lines[1000] = "", one[96];
        int k;
        for (k = 0; k < srv.nleaders && k < NET_LEADERS; k++) {
            snprintf(one, sizeof one, "%s\t%d\t%d\t%d\t%ld\n", srv.leaders[k].name, srv.leaders[k].rating,
                     srv.leaders[k].wins, srv.leaders[k].matches, srv.leaders[k].score);
            if (strlen(lines) + strlen(one) < sizeof lines) strcat(lines, one);
        }
        http_form(body, sizeof body, "leaders", lines);
    }
    status = http_request_idle(st.listing_url, body, resp, sizeof resp, 8000, keep_serving, NULL);
    if (status != 200) {
        char line[700];
        snprintf(line, sizeof line, "listing %.200s: %s%.300s", st.listing_url, status < 0 ? "no answer" : "refused: ",
                 status < 0 ? "" : resp);
        log_line(NULL, LOG_WARN, line);
    } else if (leaving || strncmp(resp, "ok", 2)) {
        char line[700];
        snprintf(line, sizeof line, "listing: %.500s", resp);
        log_line(NULL, strncmp(resp, "ok", 2) ? LOG_WARN : LOG_INFO, line);
    } else {
        const char *c = strstr(resp, " code ");          /* (the join code: friends type it instead of the address) */
        if (c) {
            char code[8] = "";
            sscanf(c + 6, "%7s", code);
            if (strcmp(code, join_code)) {
                char line[120];
                snprintf(join_code, sizeof join_code, "%s", code);
                snprintf(line, sizeof line, "join code: %s (friends type it in the game's JOIN, instead of the address)", join_code);
                log_line(NULL, LOG_INFO, line);
            }
        }
    }
}

/* ---- NAT traversal (public = code): how the internet sees us, and opening the router to who wants in ---- */
static void nat_traversal(uint32_t now)
{
    static uint32_t asked, polled;
    if (st.public_listed != 2 || !st.stun_server[0] || !st.listing_url[0]) return;
    if (!asked || now - asked > 60000) {                 /* (the STUN server: now and then; the answer comes in */
        char h[128];                                     /*  with the game's packets) */
        int pt = 3478;
        NetAddr a;
        asked = now ? now : 1;
        net_split(st.stun_server, h, sizeof h, &pt);
        if (net_resolve(h, (uint16_t)pt, &a) == 0) server_stun(&srv, &a);
    }
    if (srv.have_public && (!polled || now - polled > 3000)) {   /* who wants in: a few packets toward each */
        static char body[512], resp[2048];
        char num[16], *line;
        polled = now ? now : 1;
        body[0] = 0;
        http_form(body, sizeof body, "action", "punches");
        if (st.public_host[0]) http_form(body, sizeof body, "host", st.public_host);
        snprintf(num, sizeof num, "%u", (unsigned)st.port);
        http_form(body, sizeof body, "port", num);
        if (http_request_idle(st.listing_url, body, resp, sizeof resp, 3000, keep_serving, NULL) != 200) return;
        for (line = strtok(resp, "\n"); line; line = strtok(NULL, "\n")) {
            char ip[64];
            int pt = 0, k;
            NetAddr to;
            if (sscanf(line, "%63s %d", ip, &pt) != 2 || pt <= 0 || net_resolve(ip, (uint16_t)pt, &to) != 0) continue;
            for (k = 0; k < 3; k++) server_punch(&srv, &to);
            {
                char l2[120];
                snprintf(l2, sizeof l2, "NAT traversal: opening the way for %s:%d", ip, pt);
                log_line(NULL, LOG_DEBUG, l2);
            }
        }
    }
}

int main(int argc, char **argv)
{
    const char *cfg_path = "server.cfg";
    ServerConfig sc;
    ServerHooks hooks;
    char last[96] = "";
    uint32_t last_announce = 0, last_status = 0, last_prune = 0, last_backup = 0;
    int i, port_override = -1, public_override = 0, check = 0, have_db = 0;
    const char *backup_file = NULL;
    const char *name_override = NULL;
    for (i = 1; i < argc; i++) {
        if ((!strcmp(argv[i], "-c") || !strcmp(argv[i], "--config")) && i + 1 < argc) cfg_path = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port_override = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) name_override = argv[++i];
        else if (!strcmp(argv[i], "--public")) public_override = 1;
        else if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else if (!strcmp(argv[i], "--check")) check = 1;
        else if (!strcmp(argv[i], "--version")) {
            printf("ofp_server %s, network protocol %d (oldest %d), rules %08x\n", OFP_VERSION, NET_PROTOCOL,
                   NET_PROTOCOL_OLDEST, (unsigned)net_rules());
            return 0;
        }
        else if (!strcmp(argv[i], "--backup") && i + 1 < argc) backup_file = argv[++i];
        else { fprintf(stderr, "usage: ofp_server [-c server.cfg] [--port N] [--name TEXT] [--public] [--quiet] [--check]\n"); return 1; }
    }
    if (config_load(&st, cfg_path) != 0)
        fprintf(stderr, "%s not found: running with the defaults (see server.cfg.example)\n", cfg_path);
    if (port_override > 0) st.port = (uint16_t)port_override;
    if (name_override) snprintf(st.name, sizeof st.name, "%s", name_override);
    if (public_override) st.public_listed = 1;
    if (st.public_listed && !strncmp(st.listing_url, "https://", 8) && !http_has_tls())
        fprintf(stderr, "listing_url: this build has no TLS (no libcurl): it asks over http:// instead\n");
    if (check) {                                          /* what we understood, and stop */
        static const char *lv[] = { "debug", "info", "warn", "error" };
        printf("name           %s\nmotd           %s\nport           %u (UDP)\npassword       %s\n"
               "max_players    %d\nmin_players    %d\ncountdown      %d s\npublic         %s\n",
               st.name, st.motd, (unsigned)st.port, st.password[0] ? "(set)" : "(none: anyone may join)",
               st.max_players, st.min_players, st.countdown_s, st.public_listed ? "yes" : "no");
        printf("version        %s, protocol %d, rules %08x\n", OFP_VERSION, NET_PROTOCOL, (unsigned)net_rules());
        if (st.public_listed) {
            printf("https          %s\n", http_has_tls() ? "yes (libcurl)" : "no: https:// addresses are asked over http://");
            printf("listing_url    %s\npublic_host    %s\nannounce_every %d s\n", st.listing_url[0] ? st.listing_url : "(missing!)",
                   st.public_host[0] ? st.public_host : "(the address the announcement comes from)", st.announce_s);
        }
        if (st.webhook_url[0]) printf("webhook        %s (%s), on: %s\n", st.webhook_url, st.webhook_format, st.webhook_events);
        if (st.backup_dir[0]) printf("backups        %s, every %d h, the last %d\n", st.backup_dir, st.backup_every_h, st.backup_keep);
        if (!st.allow_bots) printf("bots           off (allow_bots = no)\n");
        else {
            const char *level = st.bot_level == AI_EASY ? "easy" : st.bot_level == AI_HARD ? "hard" : "normal";
            if (st.fill_bots) printf("bots           fill free slots up to the map capacity, %s\n", level);
            else printf("bots           up to %d per match, %s\n", st.bots, level);
        }
        printf("mode           %s\nenemy          %d (0 off, 1 easy, 2 normal, 3 hard)\nworld          %s\ntime           %s\n"
               "map            %s\noriginal_camp  %s\ndatabase       %s\nchat_log       %s\nlog_file       %s\nlog_level      %s\n"
               "replays        %s\n",
               st.versus ? "versus" : "together", st.ai_level, biome_name((Biome)st.biome), time_name(st.time),
               st.maps[0] ? st.maps : st.map_file[0] ? st.map_file : "(random every match)", st.original ? "yes" : "no",
               st.database[0] ? st.database : "(none)", st.chat_log ? "yes" : "no", st.log_file[0] ? st.log_file : "(console only)",
               lv[st.log_level & 3], st.replay_dir[0] ? st.replay_dir : "(not kept)");
        if (st.public_listed) printf("share_matches  %s%s\n", st.share_matches ? "yes" : "no",
                                     st.share_matches ? (st.share_replays ? ", with replays" : ", without replays") : "");
        if (st.time_limit_min) printf("time_limit     %d min, then %s\n", st.time_limit_min,
                                      st.time_rule == TIME_RULE_DRAW ? "a draw" : "the side with more points wins (a draw if equal)");
        else printf("time_limit     20 min (default)\n");
        {
            char a[24], b[24];
            snprintf(a, sizeof a, st.keep_chat_days ? "%d days" : "for ever", st.keep_chat_days);
            snprintf(b, sizeof b, st.keep_match_days ? "%d days" : "for ever", st.keep_match_days);
            printf("keep           chat %s, matches %s (players' totals and ratings always)\n", a, b);
        }
        printf("goal           %s\n", st.extras & EXTRA_CONQUEST ? "conquest (capture points, 300 wins)"
               : st.extras & EXTRA_ANNIHILATION ? "annihilation (every tank for itself: the last one standing)"
               : st.extras & EXTRA_TDM ? "team deathmatch (no flags: 20 enemy tanks)"
               : st.extras & EXTRA_WARLORD ? "warlord (a boss a side: destroy theirs 7 times)"
               : st.extras & EXTRA_HILL ? "the flag, or the hill" : "the flag");
        printf("extras         %s\n", st.extras & EXTRA_PICKUPS ? "pickups" : "none");
        printf("watching       %s\n", !st.allow_watching ? "off" : st.spectator_delay_s ? "on, delayed" : "on, live");
        if (st.allow_watching && st.spectator_delay_s) printf("               %d s behind the match\n", st.spectator_delay_s);
        printf("auto_start     %s\n", st.auto_start ? "yes" : "no (lobby host starts)");
        printf("start_after    %s\nadmin_password %s\nmax_per_addr.  %d\nstatus_file    %s\nmetrics_file   %s\n",
               st.start_after_s ? "set" : "(wait for everyone)", st.admin_password[0] ? "(set)" : "(none: no admins from the chat)",
               st.max_per_address, st.status_file[0] ? st.status_file : "(none)", st.metrics_file[0] ? st.metrics_file : "(none)");
        if (st.start_after_s) printf("               start %d s after min_players are ready\n", st.start_after_s);
        {
            ServerConfig tc;
            int k;
            config_to_server(&st, &tc);
            load_maps(&st, &tc);
            printf("maps           %d (%s)\n", tc.nmaps, tc.nmaps < 2 ? (tc.nmaps ? "one map" : "random every match")
                   : tc.rotation == ROT_VOTE ? "players vote" : tc.rotation == ROT_RANDOM ? "random" : "in order");
            for (k = 0; k < tc.nmaps; k++) printf("               %d %s\n", k + 1, tc.map_names[k]);
            if ((st.maps[0] || st.map_file[0]) && !tc.nmaps) { printf("\nproblem: none of the maps can be read\n"); return 1; }
        }
        return 0;
    }
    if (st.log_file[0] && !(log_fp = fopen(st.log_file, "a"))) fprintf(stderr, "cannot write the log %s\n", st.log_file);
    snprintf(cfg_file, sizeof cfg_file, "%s", cfg_path);
    if (db_open(st.database) != 0 && st.database[0]) log_line(NULL, LOG_WARN, "no statistics: the database won't open");
    else have_db = st.database[0] != 0;
#ifndef OFP_HAVE_SQLITE
    have_db = 0;
#endif
    if (backup_file) {                                   /* --backup FILE: a copy of the database, and done */
        char msg[400];
        int rc = backup_to(backup_file, msg, sizeof msg);
        printf("%s\n", msg);
        return rc ? 1 : 0;
    }
    srand((unsigned)time(NULL));
    started_at = time(NULL);
    crash_install("ofp_server", "", crash_save_server);     /* a crash: a report here, and the match that was on */

    config_to_server(&st, &sc);
    load_maps(&st, &sc);
    if ((st.maps[0] || st.map_file[0]) && !sc.nmaps) { log_line(NULL, LOG_ERROR, "none of the maps in server.cfg can be read"); return 1; }
    if (sc.nmaps) {
        char line[200];
        snprintf(line, sizeof line, "%d map%s in the rotation (%s)", sc.nmaps, sc.nmaps == 1 ? "" : "s",
                 sc.rotation == ROT_VOTE ? "by vote" : sc.rotation == ROT_RANDOM ? "random" : "in order");
        log_line(NULL, LOG_INFO, line);
    }
    memset(&hooks, 0, sizeof hooks);
    hooks.log = log_line;
    hooks.chat = on_chat;
    hooks.match_over = on_match;
    hooks.desync = on_desync;
    http_ipv4_first(1);                                   /* (announced by IPv4: the address every player reaches) */
    hooks.leaders = on_leaders;
    if (have_db) {                                        /* keys and names kept: they last across restarts */
        hooks.key_get = on_key_get; hooks.key_put = on_key_put;
        hooks.name_get = on_name_get; hooks.name_put = on_name_put;
        hooks.server_key = on_server_key;
        hooks.ban_check = on_ban_check; hooks.ban_add = on_ban_add; hooks.ban_remove = on_ban_remove;
    }
    if (server_open(&srv, st.port, &sc, &hooks) != 0) {
        char line[96];
        snprintf(line, sizeof line, "cannot listen on UDP port %u", (unsigned)st.port);
        log_line(NULL, LOG_ERROR, line);
        return 1;
    }
    if (st.public_listed && !st.listing_url[0]) log_line(NULL, LOG_WARN, "public = yes but no listing_url: not published");
    signal(SIGINT, on_signal);
#ifdef SIGTERM
    signal(SIGTERM, on_signal);
#endif
#ifdef SIGHUP
    signal(SIGHUP, on_hup);                              /* kill -HUP: read server.cfg again */
#endif
#ifndef _WIN32
    fcntl(0, F_SETFL, fcntl(0, F_GETFL, 0) | O_NONBLOCK);   /* the console */
#endif
    if (!quiet) printf("type help for the console commands\n");
    {
        char t[300];
        snprintf(t, sizeof t, "%s is open%s%s.", st.name, st.public_host[0] ? " at " : "", st.public_host);
        hook_say("server", t);
    }
    while (!stop) {
        uint32_t now = net_ms();
        server_step(&srv);
        if (pending_match) send_pending_match();
        if (hook_n) hook_send_all();
        {   /* live on the web site */
            static uint32_t last_live;
            static int was_running;
            if (srv.state == SRV_RUNNING && (!last_live || now - last_live > 10000u)) { last_live = now ? now : 1; send_live(0); was_running = 1; }
            if (srv.state != SRV_RUNNING && was_running) { send_live(1); was_running = 0; last_live = 0; }
        }
        if (getenv("OFP_TEST_CRASH") && srv.state == SRV_RUNNING && srv.tick > 200) raise(SIGSEGV);   /* (tests) */
        console();
        if (reload) {                                     /* new settings, same port; and a new log file (logrotate) */
            ServerSettings n;
            ServerConfig nc;
            reload = 0;
            if (log_fp) { fclose(log_fp); log_fp = NULL; }
            if (st.log_file[0]) log_fp = fopen(st.log_file, "a");
            if (config_load(&n, cfg_file) == 0) {
                n.port = st.port;
                st = n;
                config_to_server(&st, &nc);
                load_maps(&st, &nc);
                server_reconfigure(&srv, &nc);
            } else log_line(NULL, LOG_WARN, "reload: server.cfg can't be read");
        }
        if (st.backup_dir[0] && have_db && (!last_backup || now - last_backup > (uint32_t)st.backup_every_h * 3600u * 1000u)) {
            last_backup = now ? now : 1;                  /* (the first one: right after starting) */
            backup_rotate();
        }
        if (!last_prune || now - last_prune > 6u * 3600u * 1000u) {   /* old chat and matches, now and every 6 hours */
            int n;
            last_prune = now ? now : 1;
            n = db_prune(st.keep_chat_days, st.keep_match_days);
            if (n > 0) {
                char line[96];
                snprintf(line, sizeof line, "the database: forgot %d old chat lines and matches", n);
                log_line(NULL, LOG_INFO, line);
            }
        }
        if ((st.status_file[0] || st.metrics_file[0]) && (!last_status || now - last_status > 10000)) {
            last_status = now;
            write_status();
        }
        if (strcmp(last, srv.status)) { log_line(NULL, LOG_DEBUG, srv.status); snprintf(last, sizeof last, "%s", srv.status); }
        {   /* public = code behind a router: the first announcement waits (5 s at most) for the STUN answer, and
             * goes again as soon as it comes, so the listing knows how the internet sees us */
            static int had_public;
            if (srv.have_public && !had_public) { had_public = 1; last_announce = 0; }
        }
        if (st.public_listed && st.listing_url[0] &&     /* (more often while a match is on: the web site shows it live) */
            !(st.public_listed == 2 && st.stun_server[0] && !srv.have_public && (uint32_t)(time(NULL) - started_at) < 5) &&
            (!last_announce || now - last_announce > (uint32_t)(srv.state == SRV_RUNNING && st.announce_s > 20 ? 20 : st.announce_s) * 1000u)) {
            last_announce = now;
            announce(0);
        }
        nat_traversal(now);
        {   /* until a packet comes (at once), or the next tick; with nobody here, a tenth of a second at a time:
             * an empty server sleeps instead of waking 500 times a second */
            int idle = srv.state == SRV_LOBBY && server_people(&srv) == 0;
            net_wait(srv.sock, idle ? 100 : 2);
        }
    }
    log_line(NULL, LOG_INFO, "shutting down");
    if (st.public_listed && st.listing_url[0]) announce(1);
    {
        char t[200];
        snprintf(t, sizeof t, "%s has closed.", st.name);
        hook_say("server", t);
        hook_send_all();
    }
    server_close(&srv);
    db_close();
    for (i = 0; i < nmap_texts; i++) free(map_texts[i]);
    if (log_fp) fclose(log_fp);
    return 0;
}
