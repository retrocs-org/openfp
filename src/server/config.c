/* SPDX-License-Identifier: MIT */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "server/config.h"

void config_defaults(ServerSettings *s)
{
    memset(s, 0, sizeof *s);
    snprintf(s->name, sizeof s->name, "OpenFP server");
    snprintf(s->motd, sizeof s->motd, "Welcome! Say hello, pick a tank, press READY.");
    s->port = NET_PORT;
    s->max_players = 8;
    s->min_players = 2;
    s->auto_start = 1;
    s->time_limit_min = 20;
    s->lives = 5;
    s->powerups = 1;
    s->announce_s = 60;
    s->versus = 1;
    s->ai_level = AI_NORMAL;
    s->countdown_s = 5;
    snprintf(s->database, sizeof s->database, "ofp_server.db");
    snprintf(s->log_file, sizeof s->log_file, "ofp_server.log");
    s->chat_log = 1;
    s->log_level = LOG_INFO;
    s->bot_level = AI_NORMAL;
    s->allow_bots = 1;
    s->max_per_address = 4;
    s->share_matches = 1;
    s->share_replays = 1;
    snprintf(s->stun_server, sizeof s->stun_server, "%s", NET_STUN_DEFAULT);
    s->replays_keep = 500;                               /* (the newest; 0: no limit) */
    s->replays_max_mb = 1000;
    s->desyncs_keep = 40;
    s->allow_watching = 1;
    s->keep_chat_days = 30;
    s->backup_every_h = 24;
    snprintf(s->webhook_format, sizeof s->webhook_format, "text");
    snprintf(s->webhook_events, sizeof s->webhook_events, "matches, server");
    s->backup_keep = 7;
}

/* a mode's name (the goal and modes settings): its extras bits; -1: not one */
static int goal_bits(const char *v)
{
    if (!strcmp(v, "flag")) return 0;
    if (!strcmp(v, "hill")) return EXTRA_HILL;
    if (!strcmp(v, "conquest")) return EXTRA_CONQUEST;
    if (!strcmp(v, "annihilation")) return EXTRA_ANNIHILATION;
    if (!strcmp(v, "deathmatch")) return EXTRA_TDM;
    if (!strcmp(v, "warlord") || !strcmp(v, "juggernaut")) return EXTRA_WARLORD;
    if (!strcmp(v, "killconfirmed") || !strcmp(v, "kill_confirmed")) return EXTRA_KILLCONF;
    if (!strcmp(v, "hardpoint")) return EXTRA_HARDPOINT;
    return -1;
}

static char *trim(char *s)
{
    char *e;
    while (isspace((unsigned char)*s)) s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

/* "value   # a comment" -> "value": a # at the start or after a space or tab
 * begins a comment, unless it's inside "quotes" (so name = "Room #1" works) */
static void strip_comment(char *v)
{
    int quoted = 0;
    char *p;
    for (p = v; *p; p++) {
        if (*p == '"') quoted = !quoted;
        else if (*p == '#' && !quoted && (p == v || p[-1] == ' ' || p[-1] == '\t')) { *p = 0; break; }
    }
}

static int yes(const char *v) { return !strcmp(v, "yes") || !strcmp(v, "true") || !strcmp(v, "1") || !strcmp(v, "on"); }

int config_load(ServerSettings *s, const char *path)
{
    char line[1024];
    FILE *f = fopen(path, "r");
    config_defaults(s);
    if (!f) return -1;
    while (fgets(line, sizeof line, f)) {
        char *k = trim(line), *v;
        if (*k == '#' || !*k) continue;
        if (!(v = strchr(k, '='))) continue;
        *v++ = 0;
        k = trim(k);
        strip_comment(v);
        v = trim(v);
        if (strlen(v) >= 2 && *v == '"' && v[strlen(v) - 1] == '"') { v[strlen(v) - 1] = 0; v++; }
        if (!strcmp(k, "name")) snprintf(s->name, sizeof s->name, "%s", v);
        else if (!strcmp(k, "motd")) snprintf(s->motd, sizeof s->motd, "%s", v);
        else if (!strcmp(k, "password")) snprintf(s->password, sizeof s->password, "%s", v);
        else if (!strcmp(k, "port")) s->port = (uint16_t)atoi(v);
        else if (!strcmp(k, "max_players")) s->max_players = atoi(v);
        else if (!strcmp(k, "min_players")) s->min_players = atoi(v);
        else if (!strcmp(k, "auto_start")) s->auto_start = yes(v);
        else if (!strcmp(k, "public")) s->public_listed = !strcmp(v, "code") ? 2 : yes(v);   /* (code: a join code, not listed) */
        else if (!strcmp(k, "cloud_server")) snprintf(s->cloud_server, sizeof s->cloud_server, "%s", v);
        else if (!strcmp(k, "cloud_secret")) snprintf(s->cloud_secret, sizeof s->cloud_secret, "%s", v);
        else if (!strcmp(k, "listing_url")) snprintf(s->listing_url, sizeof s->listing_url, "%s", v);
        else if (!strcmp(k, "public_host")) snprintf(s->public_host, sizeof s->public_host, "%s", v);
        else if (!strcmp(k, "announce_every")) s->announce_s = atoi(v) < 20 ? 20 : atoi(v);
        else if (!strcmp(k, "mode")) s->versus = strcmp(v, "together") && strcmp(v, "coop");
        else if (!strcmp(k, "enemy")) s->ai_level = !strcmp(v, "off") ? AI_OFF : !strcmp(v, "easy") ? AI_EASY
                                                 : !strcmp(v, "hard") ? AI_HARD : AI_NORMAL;
        else if (!strcmp(k, "world")) {
            Biome b = biome_from_name(v);
            if (b == BIOME_SCIFI || !strcmp(v, "moon-base")) b = BIOME_MOON;
            else if (!strcmp(v, "volcano")) b = BIOME_VOLCANIC;
            s->biome = (int)b;
        }
        else if (!strcmp(k, "time")) s->time = time_from_name(v);
        else if (!strcmp(k, "original_camp")) s->original = yes(v);
        else if (!strcmp(k, "countdown")) s->countdown_s = atoi(v);
        else if (!strcmp(k, "allow_bots")) s->allow_bots = yes(v);
        else if (!strcmp(k, "share_matches")) s->share_matches = yes(v);
        else if (!strcmp(k, "allow_watching")) s->allow_watching = yes(v);
        else if (!strcmp(k, "pickups") || !strcmp(k, "powerups")) {
            s->powerups = yes(v);
            s->extras = s->powerups ? (s->extras | EXTRA_PICKUPS) : (s->extras & ~EXTRA_PICKUPS);
        }
        else if (!strcmp(k, "modes")) {                  /* a rotation of modes: flag, conquest, killconfirmed, ... in turn */
            char list[200], *t;
            snprintf(list, sizeof list, "%s", v);
            s->nmodes = 0;
            for (t = strtok(list, ", \t"); t && s->nmodes < 8; t = strtok(NULL, ", \t")) {
                int b = goal_bits(t);
                if (b >= 0) s->modes[s->nmodes++] = (uint8_t)b;
            }
            if (s->nmodes) s->extras = (s->extras & ~(EXTRA_HILL | EXTRA_CONQUEST | EXTRA_ANNIHILATION | EXTRA_TDM | EXTRA_WARLORD | EXTRA_KILLCONF | EXTRA_HARDPOINT)) | s->modes[0];
        }
        else if (!strcmp(k, "goal")) {                   /* flag, hill (the flag and the hill), conquest, annihilation */
            s->extras &= ~(EXTRA_HILL | EXTRA_CONQUEST | EXTRA_ANNIHILATION | EXTRA_TDM | EXTRA_WARLORD | EXTRA_KILLCONF | EXTRA_HARDPOINT);
            if (!strcmp(v, "hill")) s->extras |= EXTRA_HILL;
            else if (!strcmp(v, "conquest")) s->extras |= EXTRA_CONQUEST;
            else if (!strcmp(v, "annihilation")) s->extras |= EXTRA_ANNIHILATION;
            else if (!strcmp(v, "deathmatch")) s->extras |= EXTRA_TDM;
            else if (!strcmp(v, "warlord") || !strcmp(v, "juggernaut")) s->extras |= EXTRA_WARLORD;   /* (the old name too) */
            else if (!strcmp(v, "killconfirmed") || !strcmp(v, "kill_confirmed")) s->extras |= EXTRA_KILLCONF;
            else if (!strcmp(v, "hardpoint")) s->extras |= EXTRA_HARDPOINT;
        }
        else if (!strcmp(k, "weather")) s->weather = !strcmp(v, "random") ? SERVER_WEATHER_RANDOM : weather_from_name(v);
        else if (!strcmp(k, "webhook_url")) snprintf(s->webhook_url, sizeof s->webhook_url, "%s", v);
        else if (!strcmp(k, "webhook_format")) snprintf(s->webhook_format, sizeof s->webhook_format, "%s", v);
        else if (!strcmp(k, "webhook_events")) snprintf(s->webhook_events, sizeof s->webhook_events, "%s", v);
        else if (!strcmp(k, "backup_dir")) snprintf(s->backup_dir, sizeof s->backup_dir, "%s", v);
        else if (!strcmp(k, "backup_every")) s->backup_every_h = atoi(v) < 1 ? 1 : atoi(v);
        else if (!strcmp(k, "backup_keep")) s->backup_keep = atoi(v) < 1 ? 1 : atoi(v);
        else if (!strcmp(k, "keep_chat_days")) s->keep_chat_days = atoi(v) < 0 ? 0 : atoi(v);
        else if (!strcmp(k, "keep_match_days")) s->keep_match_days = atoi(v) < 0 ? 0 : atoi(v);
        else if (!strcmp(k, "spectator_delay")) s->spectator_delay_s = atoi(v) < 0 ? 0 : atoi(v) > 600 ? 600 : atoi(v);
        else if (!strcmp(k, "time_limit")) s->time_limit_min = atoi(v) < 1 ? 20 : atoi(v) > 30 ? 30 : atoi(v);
        else if (!strcmp(k, "lives")) s->lives = !strcmp(v, "unlimited") || !strcmp(v, "0") ? MATCH_UNLIMITED_LIVES : atoi(v) < 1 ? 5 : atoi(v) > MATCH_MAX_LIVES ? MATCH_MAX_LIVES : atoi(v);
        else if (!strcmp(k, "on_time_up")) s->time_rule = !strcmp(v, "draw") ? TIME_RULE_DRAW : TIME_RULE_POINTS;
        else if (!strcmp(k, "share_replays")) s->share_replays = yes(v);
        else if (!strcmp(k, "start_after")) s->start_after_s = atoi(v) < 0 ? 0 : atoi(v);
        else if (!strcmp(k, "admin_password")) snprintf(s->admin_password, sizeof s->admin_password, "%s", v);
        else if (!strcmp(k, "admin_plain_login")) s->admin_plain_login = yes(v);
        else if (!strcmp(k, "stun_server")) snprintf(s->stun_server, sizeof s->stun_server, "%s", v);
        else if (!strcmp(k, "maps")) snprintf(s->maps, sizeof s->maps, "%s", v);
        else if (!strcmp(k, "rotation")) s->rotation = !strcmp(v, "random") ? ROT_RANDOM : !strcmp(v, "vote") ? ROT_VOTE : ROT_ORDER;
        else if (!strcmp(k, "status_file")) snprintf(s->status_file, sizeof s->status_file, "%s", v);
        else if (!strcmp(k, "metrics_file")) snprintf(s->metrics_file, sizeof s->metrics_file, "%s", v);
        else if (!strcmp(k, "map_size")) {               /* a random battlefield's: small, medium, large, huge */
            int z;
            for (z = 0; z < MAP_SIZES; z++) if (!strcmp(v, map_size_name[z])) s->map_size = z + 1;
        }
        else if (!strcmp(k, "max_per_address")) s->max_per_address = atoi(v) < 1 ? 1 : atoi(v) > 16 ? 16 : atoi(v);
        else if (!strcmp(k, "bots")) s->bots = atoi(v) < 0 ? 0 : atoi(v) > MATCH_PLAYERS - 1 ? MATCH_PLAYERS - 1 : atoi(v);
        else if (!strcmp(k, "fill_bots")) s->fill_bots = yes(v);
        else if (!strcmp(k, "bot_level")) s->bot_level = !strcmp(v, "easy") ? AI_EASY : !strcmp(v, "hard") ? AI_HARD : AI_NORMAL;
        else if (!strcmp(k, "map")) {
            const char *b = strrchr(v, '/');
            snprintf(s->map_file, sizeof s->map_file, "%s", v);
            snprintf(s->map_name, sizeof s->map_name, "%s", b ? b + 1 : v);
        }
        else if (!strcmp(k, "database")) snprintf(s->database, sizeof s->database, "%s", v);
        else if (!strcmp(k, "chat_log")) s->chat_log = yes(v);
        else if (!strcmp(k, "log_file")) snprintf(s->log_file, sizeof s->log_file, "%s", v);
        else if (!strcmp(k, "log_level")) s->log_level = !strcmp(v, "debug") ? LOG_DEBUG : !strcmp(v, "warn") ? LOG_WARN
                                                     : !strcmp(v, "error") ? LOG_ERROR : LOG_INFO;
        else if (!strcmp(k, "replays")) snprintf(s->replay_dir, sizeof s->replay_dir, "%s", v);
        else if (!strcmp(k, "replays_keep")) s->replays_keep = atoi(v) < 0 ? 0 : atoi(v);
        else if (!strcmp(k, "replays_max_mb")) s->replays_max_mb = atoi(v) < 0 ? 0 : atoi(v);
        else if (!strcmp(k, "desyncs_keep")) s->desyncs_keep = atoi(v) < 0 ? 0 : atoi(v);
        else fprintf(stderr, "%s: unknown setting \"%s\"\n", path, k);
    }
    fclose(f);
    return 0;
}

void config_to_server(const ServerSettings *s, ServerConfig *o)
{
    server_default_config(o);
    if (s->public_listed == 1) snprintf(o->cloud_server, sizeof o->cloud_server, "%s", s->cloud_server[0] ? s->cloud_server : "unconfigured");
    snprintf(o->name, sizeof o->name, "%s", s->name);
    snprintf(o->motd, sizeof o->motd, "%s", s->motd);
    snprintf(o->password, sizeof o->password, "%s", s->password);
    snprintf(o->map_name, sizeof o->map_name, "%s", s->map_name);
    o->max_players = s->max_players;
    o->min_players = s->min_players;
    o->countdown_ms = s->countdown_s * 1000;
    o->allow_bots = s->allow_bots;
    o->max_per_address = s->max_per_address;
    o->base.map_size = (uint8_t)s->map_size;            /* (a random battlefield's size) */
    o->start_after_ms = s->start_after_s * 1000;
    o->manual_start = !s->auto_start;
    o->allow_watching = s->allow_watching;
    o->spectator_delay_s = s->spectator_delay_s;
    o->base.time_limit = (uint16_t)(s->time_limit_min * 60);
    o->base.time_rule = (uint8_t)s->time_rule;
    o->base.no_powerups = !s->powerups;
    o->base.lives = (uint8_t)s->lives;
    o->weather_mode = s->weather;
    o->base.extras = (uint8_t)s->extras;
    o->rotation = s->rotation;
    memcpy(o->modes, s->modes, sizeof o->modes);
    o->nmodes = s->nmodes;
    snprintf(o->admin_password, sizeof o->admin_password, "%s", s->admin_password);
    o->admin_plain_login = s->admin_plain_login;
    o->bots = s->bots;
    o->fill_bots = s->fill_bots;
    o->bot_level = s->bot_level;
    o->base.versus = (uint8_t)s->versus;
    o->base.ai_level = (uint8_t)s->ai_level;
    o->base.biome = (uint8_t)s->biome;
    o->base.time = (uint8_t)s->time;
    o->base.camp_garages = (uint8_t)(s->original ? 3 : 4);
}
