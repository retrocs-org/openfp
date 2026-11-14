/* SPDX-License-Identifier: MIT
 * server.cfg: "key = value" lines, # starts a comment. See server.cfg.example. */
#ifndef OFP_SERVER_CONFIG_H
#define OFP_SERVER_CONFIG_H

#include "common/server.h"

typedef struct {
    char     name[NET_SERVER_NAME], motd[NET_MOTD], password[64];
    uint16_t port;
    int      max_players, min_players, public_listed, announce_s;
    char     listing_url[256], public_host[128];
    char     cloud_server[33], cloud_secret[65];
    int      versus, ai_level, biome, time, original, countdown_s;
    char     map_file[512], map_name[32];
    char     database[512], log_file[512], replay_dir[512];
    int      chat_log, log_level;
    int      allow_bots, bots, fill_bots, bot_level, max_per_address;
    int      map_size;                /* a random battlefield's: SIZE_* + 1 (0: LARGE) */
    int      start_after_s, rotation, share_matches, share_replays, time_limit_min, time_rule, lives;
    int      auto_start;
    int      allow_watching, spectator_delay_s, keep_chat_days, keep_match_days, weather, extras, powerups;
    int      replays_keep, replays_max_mb, desyncs_keep;
    int      admin_plain_login;
    char     stun_server[128];         /* NAT traversal: where to ask how the internet sees us ("": not at all) */        /* 1: /login PASSWORD in the clear too (games before the challenge) */   /* replays/ and desync files: how many, how big at most */
    uint8_t  modes[8];                 /* modes = flag, conquest, ...: a match of each in turn (0: just goal's) */
    int      nmodes;
    char     backup_dir[512];
    char     webhook_url[512], webhook_format[16], webhook_events[96];
    int      backup_every_h, backup_keep;
    char     admin_password[64], maps[1024], status_file[512], metrics_file[512];
} ServerSettings;

void config_defaults(ServerSettings *s);
int  config_load(ServerSettings *s, const char *path);          /* 0 = read; defaults otherwise */
void config_to_server(const ServerSettings *s, ServerConfig *out);

#endif
