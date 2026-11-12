/* SPDX-License-Identifier: MIT */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "server/db.h"
#include "common/achieve.h"
#include "common/net.h"

#ifdef OFP_HAVE_SQLITE
#include <sqlite3.h>

static sqlite3 *db;

static const char *schema =
    "PRAGMA journal_mode=WAL;"
    "CREATE TABLE IF NOT EXISTS players(name TEXT PRIMARY KEY, matches INTEGER DEFAULT 0, wins INTEGER DEFAULT 0,"
    " losses INTEGER DEFAULT 0, kills INTEGER DEFAULT 0, score INTEGER DEFAULT 0, first_seen TEXT, last_seen TEXT);"
    "CREATE TABLE IF NOT EXISTS matches(id INTEGER PRIMARY KEY, ended TEXT, seconds INTEGER, players INTEGER,"
    " mode TEXT, world TEXT, map TEXT, winner TEXT);"
    "CREATE TABLE IF NOT EXISTS match_players(match_id INTEGER, name TEXT, side TEXT, won INTEGER, kills INTEGER, score INTEGER);"
    "CREATE TABLE IF NOT EXISTS chat(id INTEGER PRIMARY KEY, at TEXT, name TEXT, text TEXT);"
    "CREATE INDEX IF NOT EXISTS players_score ON players(wins DESC, score DESC);"
    "CREATE TABLE IF NOT EXISTS keys(id TEXT PRIMARY KEY, verifier TEXT NOT NULL, first_seen TEXT);"
    "CREATE TABLE IF NOT EXISTS names(name TEXT PRIMARY KEY, id TEXT NOT NULL, claimed TEXT);"
    "CREATE TABLE IF NOT EXISTS meta(k TEXT PRIMARY KEY, v TEXT);"
    "CREATE TABLE IF NOT EXISTS bans(id TEXT, ip TEXT, name TEXT, reason TEXT, at TEXT);"
    "CREATE TABLE IF NOT EXISTS awards(name TEXT PRIMARY KEY, earned INTEGER DEFAULT 0, matches INTEGER DEFAULT 0,"
    " wins INTEGER DEFAULT 0, saved INTEGER DEFAULT 0, kills INTEGER DEFAULT 0, worlds INTEGER DEFAULT 0);";

/* columns added after the first version: added to older databases on open */
static const char *upgrades[] = {
    "ALTER TABLE players ADD COLUMN rating INTEGER DEFAULT 1500",
    "ALTER TABLE match_players ADD COLUMN rating INTEGER",
    "ALTER TABLE match_players ADD COLUMN rating_change INTEGER",
    NULL
};

int db_open(const char *path)
{
    char *err = NULL;
    if (!path || !path[0]) return 0;
    if (sqlite3_open(path, &db) != SQLITE_OK) { db = NULL; return -1; }
    if (sqlite3_exec(db, schema, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "database: %s\n", err ? err : "?");
        sqlite3_free(err);
        sqlite3_close(db);
        db = NULL;
        return -1;
    }
    {
        int i;
        for (i = 0; upgrades[i]; i++) sqlite3_exec(db, upgrades[i], NULL, NULL, NULL);   /* (fails if there already) */
    }
    return 0;
}

/* ---- ratings: Elo, side against side ---------------------------------------------------
 * A side's strength is its members' average rating; the computer counts by its level
 * (easy 1200, normal 1500, hard 1800). Winning against the stronger side earns more,
 * losing to the weaker costs more. New players move faster (K 40 for their first ten
 * matches, then 24). An abandoned match (no winner) changes nothing. */
static int bot_rating(int level) { return level <= 1 ? 1200 : level == 2 ? 1500 : 1800; }

static int player_rating(const char *name, int *matches)
{
    sqlite3_stmt *q;
    int r = 1500;
    *matches = 0;
    if (sqlite3_prepare_v2(db, "SELECT rating, matches FROM players WHERE name = ?", -1, &q, NULL) != SQLITE_OK) return r;
    sqlite3_bind_text(q, 1, name, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(q) == SQLITE_ROW) {
        if (sqlite3_column_type(q, 0) != SQLITE_NULL) r = sqlite3_column_int(q, 0);
        *matches = sqlite3_column_int(q, 1);
    }
    sqlite3_finalize(q);
    return r;
}

static void rate(MatchResult *r)
{
    double sum[2] = { 0, 0 };
    int cnt[2] = { 0, 0 }, p, before[MATCH_PLAYERS], played[MATCH_PLAYERS];
    for (p = 0; p < r->players; p++) {
        before[p] = r->bot[p] ? bot_rating(r->bot[p]) : player_rating(r->name[p], &played[p]);
        sum[r->team[p] & 1] += before[p];
        cnt[r->team[p] & 1]++;
        r->rating[p] = before[p];
        r->rating_change[p] = 0;
    }
    if (!r->versus && r->ai_level) { sum[TEAM_ENEMY] = bot_rating(r->ai_level); cnt[TEAM_ENEMY] = 1; }   /* the computer's tank */
    if (r->winner < 0 || !cnt[0] || !cnt[1]) return;
    for (p = 0; p < r->players; p++) {
        int me = r->team[p] & 1;
        double mine = sum[me] / cnt[me], theirs = sum[1 - me] / cnt[1 - me];
        double expect = 1.0 / (1.0 + pow(10.0, (theirs - mine) / 400.0));
        double k = played[p] < 10 ? 40.0 : 24.0;
        if (r->bot[p] || r->late[p]) continue;           /* (joined on the way: not rated) */
        r->rating_change[p] = (int)floor(k * ((r->won[p] ? 1.0 : 0.0) - expect) + 0.5);
        r->rating[p] = before[p] + r->rating_change[p];
    }
}

void db_close(void) { if (db) sqlite3_close(db); db = NULL; }

void db_chat(const char *name, const char *text)
{
    sqlite3_stmt *q;
    if (!db || sqlite3_prepare_v2(db, "INSERT INTO chat(at, name, text) VALUES(datetime('now'), ?, ?)", -1, &q, NULL) != SQLITE_OK) return;
    sqlite3_bind_text(q, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q, 2, text, -1, SQLITE_TRANSIENT);
    sqlite3_step(q);
    sqlite3_finalize(q);
}

/* achievements: everyone's totals here, and what this match just earned them */
static void award(MatchResult *r)
{
    int p;
    for (p = 0; p < r->players; p++) {
        sqlite3_stmt *q;
        AchieveTotals t;
        AchieveMatch m;
        r->earned[p] = 0;
        if (r->bot[p] || r->late[p]) continue;
        memset(&t, 0, sizeof t);
        if (sqlite3_prepare_v2(db, "SELECT earned, matches, wins, saved, kills, worlds FROM awards WHERE name = ?", -1, &q, NULL) == SQLITE_OK) {
            sqlite3_bind_text(q, 1, r->name[p], -1, SQLITE_TRANSIENT);
            if (sqlite3_step(q) == SQLITE_ROW) {
                t.earned = (uint32_t)sqlite3_column_int64(q, 0); t.matches = sqlite3_column_int(q, 1);
                t.wins = sqlite3_column_int(q, 2); t.saved = sqlite3_column_int(q, 3);
                t.kills = sqlite3_column_int(q, 4); t.worlds_won = (uint32_t)sqlite3_column_int64(q, 5);
            }
            sqlite3_finalize(q);
        }
        m.won = r->won[p]; m.lost = r->lost[p]; m.saved = r->saved[p]; m.tank_kills = r->kills[p];
        m.kills = r->soldiers[p]; m.helis = r->helis[p]; m.biome = r->biome; m.night = r->night;
        m.played_against_people = r->versus;
        r->earned[p] = achieve_update(&t, &m);
        if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO awards(name, earned, matches, wins, saved, kills, worlds)"
                                   " VALUES(?, ?, ?, ?, ?, ?, ?)", -1, &q, NULL) == SQLITE_OK) {
            sqlite3_bind_text(q, 1, r->name[p], -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(q, 2, t.earned); sqlite3_bind_int(q, 3, t.matches); sqlite3_bind_int(q, 4, t.wins);
            sqlite3_bind_int(q, 5, t.saved); sqlite3_bind_int(q, 6, t.kills); sqlite3_bind_int64(q, 7, t.worlds_won);
            sqlite3_step(q);
            sqlite3_finalize(q);
        }
    }
}

void db_match(MatchResult *r, const char *world, const char *map, int versus)
{
    sqlite3_stmt *q;
    sqlite3_int64 id;
    int p;
    if (!db) return;
    rate(r);
    award(r);
    sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);
    if (sqlite3_prepare_v2(db, "INSERT INTO matches(ended, seconds, players, mode, world, map, winner)"
                               " VALUES(datetime('now'), ?, ?, ?, ?, ?, ?)", -1, &q, NULL) == SQLITE_OK) {
        sqlite3_bind_int(q, 1, (int)(r->ticks / TICK_RATE));
        sqlite3_bind_int(q, 2, r->players);
        sqlite3_bind_text(q, 3, versus ? "versus" : "together", -1, SQLITE_STATIC);
        sqlite3_bind_text(q, 4, world, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(q, 5, map, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(q, 6, r->winner == TEAM_PLAYER ? "camp" : r->winner == TEAM_ENEMY ? "fortress" : "none", -1, SQLITE_STATIC);
        sqlite3_step(q);
        sqlite3_finalize(q);
    }
    id = sqlite3_last_insert_rowid(db);
    for (p = 0; p < r->players; p++) {
        if (r->bot[p]) continue;                         /* computer players don't get statistics */
        if (sqlite3_prepare_v2(db, "INSERT INTO match_players(match_id, name, side, won, kills, score, rating, rating_change)"
                                   " VALUES(?, ?, ?, ?, ?, ?, ?, ?)", -1, &q, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(q, 1, id);
            sqlite3_bind_text(q, 2, r->name[p], -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(q, 3, r->team[p] == TEAM_PLAYER ? "camp" : "fortress", -1, SQLITE_STATIC);
            sqlite3_bind_int(q, 4, r->won[p]);
            sqlite3_bind_int(q, 5, r->kills[p]);
            sqlite3_bind_int(q, 6, r->score[p]);
            sqlite3_bind_int(q, 7, r->rating[p]);
            sqlite3_bind_int(q, 8, r->rating_change[p]);
            sqlite3_step(q);
            sqlite3_finalize(q);
        }
        if (sqlite3_prepare_v2(db, "INSERT INTO players(name, first_seen) VALUES(?, datetime('now')) ON CONFLICT(name) DO NOTHING",
                               -1, &q, NULL) == SQLITE_OK) {
            sqlite3_bind_text(q, 1, r->name[p], -1, SQLITE_TRANSIENT);
            sqlite3_step(q);
            sqlite3_finalize(q);
        }
        if (sqlite3_prepare_v2(db, "UPDATE players SET matches = matches + 1, wins = wins + ?, losses = losses + ?,"
                                   " kills = kills + ?, score = score + ?, rating = ?, last_seen = datetime('now') WHERE name = ?",
                               -1, &q, NULL) == SQLITE_OK) {
            sqlite3_bind_int(q, 1, r->won[p]);
            sqlite3_bind_int(q, 2, r->winner >= 0 && !r->won[p]);
            sqlite3_bind_int(q, 3, r->kills[p]);
            sqlite3_bind_int(q, 4, r->score[p]);
            sqlite3_bind_int(q, 5, r->rating[p]);
            sqlite3_bind_text(q, 6, r->name[p], -1, SQLITE_TRANSIENT);
            sqlite3_step(q);
            sqlite3_finalize(q);
        }
    }
    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
}

int db_leaders(LeaderEntry *out, int max)
{
    sqlite3_stmt *q;
    int n = 0;
    if (!db || sqlite3_prepare_v2(db, "SELECT name, matches, wins, kills, score, COALESCE(rating, 1500) FROM players"
                                      " WHERE matches > 0 ORDER BY 6 DESC, wins DESC, score DESC LIMIT ?", -1, &q, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(q, 1, max);
    while (n < max && sqlite3_step(q) == SQLITE_ROW) {
        const unsigned char *nm = sqlite3_column_text(q, 0);
        snprintf(out[n].name, sizeof out[n].name, "%s", nm ? (const char *)nm : "?");
        out[n].matches = sqlite3_column_int(q, 1);
        out[n].wins = sqlite3_column_int(q, 2);
        out[n].kills = sqlite3_column_int(q, 3);
        out[n].score = (long)sqlite3_column_int64(q, 4);
        out[n].rating = sqlite3_column_int(q, 5);
        n++;
    }
    sqlite3_finalize(q);
    return n;
}

static void hex(const uint8_t *b, int n, char *out)
{
    static const char d[] = "0123456789abcdef";
    int i;
    for (i = 0; i < n; i++) { out[i * 2] = d[b[i] >> 4]; out[i * 2 + 1] = d[b[i] & 15]; }
    out[n * 2] = 0;
}

static int unhex(const char *s, uint8_t *b, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        unsigned v;
        if (!s[i * 2] || !s[i * 2 + 1] || sscanf(s + i * 2, "%2x", &v) != 1) return 0;
        b[i] = (uint8_t)v;
    }
    return 1;
}

static int get_text(const char *sql, const char *arg, char *out, size_t cap)
{
    sqlite3_stmt *q;
    int found = 0;
    if (!db || sqlite3_prepare_v2(db, sql, -1, &q, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(q, 1, arg, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(q) == SQLITE_ROW && sqlite3_column_text(q, 0)) {
        snprintf(out, cap, "%s", (const char *)sqlite3_column_text(q, 0));
        found = 1;
    }
    sqlite3_finalize(q);
    return found;
}

static void put_two(const char *sql, const char *a, const char *b)
{
    sqlite3_stmt *q;
    if (!db || sqlite3_prepare_v2(db, sql, -1, &q, NULL) != SQLITE_OK) return;
    sqlite3_bind_text(q, 1, a, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q, 2, b, -1, SQLITE_TRANSIENT);
    sqlite3_step(q);
    sqlite3_finalize(q);
}

int db_key_get(const uint8_t id[16], uint8_t verifier[32])
{
    char h[40], v[80];
    hex(id, 16, h);
    return get_text("SELECT verifier FROM keys WHERE id = ?", h, v, sizeof v) && unhex(v, verifier, 32);
}

void db_key_put(const uint8_t id[16], const uint8_t verifier[32])
{
    char h[40], v[80];
    hex(id, 16, h);
    hex(verifier, 32, v);
    put_two("INSERT OR IGNORE INTO keys(id, verifier, first_seen) VALUES(?, ?, datetime('now'))", h, v);
}

int db_name_get(const char *name, uint8_t id[16])
{
    char h[80];
    return get_text("SELECT id FROM names WHERE name = ?", name, h, sizeof h) && unhex(h, id, 16);
}

void db_name_put(const char *name, const uint8_t id[16])
{
    char h[40];
    hex(id, 16, h);
    put_two("INSERT OR IGNORE INTO names(name, id, claimed) VALUES(?, ?, datetime('now'))", name, h);
}

int db_server_key(uint8_t id[16], uint8_t secret[32])
{
    char a[80], b[80];
    if (!db) return 0;
    if (get_text("SELECT v FROM meta WHERE k = ?", "server_id", a, sizeof a) &&
        get_text("SELECT v FROM meta WHERE k = ?", "secret", b, sizeof b) && unhex(a, id, 16) && unhex(b, secret, 32))
        return 1;
    net_random(id, 16);                                  /* the first run: made once, kept from now on */
    net_random(secret, 32);
    hex(id, 16, a);
    hex(secret, 32, b);
    put_two("INSERT OR REPLACE INTO meta(k, v) VALUES(?, ?)", "server_id", a);
    put_two("INSERT OR REPLACE INTO meta(k, v) VALUES(?, ?)", "secret", b);
    return 1;
}

static void ip_str(uint32_t ip, char *out) { sprintf(out, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255); }

int db_ban_check(const uint8_t id[16], uint32_t ip)
{
    sqlite3_stmt *q;
    char h[40], a[20];
    int hit = 0;
    if (!db || sqlite3_prepare_v2(db, "SELECT 1 FROM bans WHERE id = ? OR ip = ? LIMIT 1", -1, &q, NULL) != SQLITE_OK) return 0;
    hex(id, 16, h);
    ip_str(ip, a);
    sqlite3_bind_text(q, 1, h, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q, 2, a, -1, SQLITE_TRANSIENT);
    hit = sqlite3_step(q) == SQLITE_ROW;
    sqlite3_finalize(q);
    return hit;
}

void db_ban_add(const uint8_t id[16], uint32_t ip, const char *name, const char *reason)
{
    sqlite3_stmt *q;
    char h[40], a[20];
    if (!db || sqlite3_prepare_v2(db, "INSERT INTO bans(id, ip, name, reason, at) VALUES(?, ?, ?, ?, datetime('now'))",
                                  -1, &q, NULL) != SQLITE_OK) return;
    hex(id, 16, h);
    ip_str(ip, a);
    sqlite3_bind_text(q, 1, h, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q, 2, a, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q, 3, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q, 4, reason, -1, SQLITE_TRANSIENT);
    sqlite3_step(q);
    sqlite3_finalize(q);
}

int db_ban_remove(const char *name)
{
    sqlite3_stmt *q;
    int n = 0;
    if (!db || sqlite3_prepare_v2(db, "DELETE FROM bans WHERE name = ? OR id = ? OR ip = ?", -1, &q, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(q, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q, 2, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q, 3, name, -1, SQLITE_TRANSIENT);
    sqlite3_step(q);
    n = sqlite3_changes(db);
    sqlite3_finalize(q);
    return n > 0;
}

int db_prune(int chat_days, int match_days)
{
    char sql[200];
    int n = 0;
    if (!db) return 0;
    if (chat_days > 0) {
        snprintf(sql, sizeof sql, "DELETE FROM chat WHERE at < datetime('now', '-%d days')", chat_days);
        if (sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK) n += sqlite3_changes(db);
    }
    if (match_days > 0) {
        snprintf(sql, sizeof sql, "DELETE FROM match_players WHERE match_id IN (SELECT id FROM matches WHERE ended < datetime('now', '-%d days'))", match_days);
        sqlite3_exec(db, sql, NULL, NULL, NULL);
        snprintf(sql, sizeof sql, "DELETE FROM matches WHERE ended < datetime('now', '-%d days')", match_days);
        if (sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK) n += sqlite3_changes(db);
    }
    if (n > 1000) sqlite3_exec(db, "VACUUM", NULL, NULL, NULL);   /* (give the space back after a big clear-out) */
    return n;
}

int db_backup(const char *path)
{
    sqlite3 *to;
    sqlite3_backup *b;
    int rc;
    if (!db) return -1;
    if (sqlite3_open(path, &to) != SQLITE_OK) { sqlite3_close(to); return -1; }
    b = sqlite3_backup_init(to, "main", db, "main");
    if (!b) { sqlite3_close(to); return -1; }
    do rc = sqlite3_backup_step(b, 256); while (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED);
    sqlite3_backup_finish(b);
    rc = rc == SQLITE_DONE ? 0 : -1;
    sqlite3_close(to);
    return rc;
}

#else   /* built without SQLite */

int  db_open(const char *path) { return path && path[0] ? -1 : 0; }
void db_close(void) {}
void db_chat(const char *name, const char *text) { (void)name; (void)text; }
void db_match(MatchResult *r, const char *world, const char *map, int versus) { (void)r; (void)world; (void)map; (void)versus; }
int  db_leaders(LeaderEntry *out, int max) { (void)out; (void)max; return 0; }
int  db_key_get(const uint8_t id[16], uint8_t v[32]) { (void)id; (void)v; return 0; }
void db_key_put(const uint8_t id[16], const uint8_t v[32]) { (void)id; (void)v; }
int  db_name_get(const char *name, uint8_t id[16]) { (void)name; (void)id; return 0; }
void db_name_put(const char *name, const uint8_t id[16]) { (void)name; (void)id; }
int  db_server_key(uint8_t id[16], uint8_t secret[32]) { (void)id; (void)secret; return 0; }
int  db_ban_check(const uint8_t id[16], uint32_t ip) { (void)id; (void)ip; return 0; }
void db_ban_add(const uint8_t id[16], uint32_t ip, const char *n, const char *r) { (void)id; (void)ip; (void)n; (void)r; }
int  db_ban_remove(const char *name) { (void)name; return 0; }
int  db_prune(int chat_days, int match_days) { (void)chat_days; (void)match_days; return 0; }
int  db_backup(const char *path) { (void)path; return -1; }

#endif
