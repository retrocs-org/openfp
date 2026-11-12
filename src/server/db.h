/* SPDX-License-Identifier: MIT
 * The server's statistics in SQLite: players, matches, who did what, and
 * (if chat_log = yes) the chat. Without SQLite the server still runs, just
 * without statistics or a leaderboard. */
#ifndef OFP_SERVER_DB_H
#define OFP_SERVER_DB_H

#include "common/server.h"

int  db_open(const char *path);            /* 0 = ok ("" = no database) */
void db_close(void);
void db_chat(const char *name, const char *text);
void db_match(MatchResult *r, const char *world, const char *map, int versus);   /* also rates it (r->rating*) */
int  db_leaders(LeaderEntry *out, int max);
/* players' keys and the names they own, and the server's own identity */
int  db_key_get(const uint8_t id[16], uint8_t verifier[32]);
void db_key_put(const uint8_t id[16], const uint8_t verifier[32]);
int  db_name_get(const char *name, uint8_t id[16]);
void db_name_put(const char *name, const uint8_t id[16]);
int  db_server_key(uint8_t id[16], uint8_t secret[32]);      /* made on first use, kept */
int  db_ban_check(const uint8_t id[16], uint32_t ip);
void db_ban_add(const uint8_t id[16], uint32_t ip, const char *name, const char *reason);
int  db_ban_remove(const char *name);
/* forget chat older than chat_days and matches older than match_days (0 = keep);
 * the players' totals and ratings stay. Returns rows removed. */
int  db_prune(int chat_days, int match_days);
/* a copy of the whole database, consistent even while the server writes (SQLite's
 * online backup); 0 = ok */
int  db_backup(const char *path);

#endif
