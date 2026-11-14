<?php
// SPDX-License-Identifier: MIT
// The listing's database, version by version (MariaDB / MySQL). Each step runs once, in
// order; the version reached is kept in the table ofp_meta. Run them with
//     php inc/migrate.php            (status: where the database is, what's to do)
//     php inc/migrate.php up         (do it)
// as a user that may create and alter tables: 'db_admin' in config.inc.php, or 'db'.
// With 'create_tables' => true the site does it by itself, with the 'db' user.

function ofp_migrations(): array
{
    return [
        1 => ['the servers, their player counts, the rate limit (0.1)', [
            "CREATE TABLE IF NOT EXISTS servers (
               host VARCHAR(128) NOT NULL, port INT UNSIGNED NOT NULL, name VARCHAR(48), build INT UNSIGNED,
               players TINYINT UNSIGNED, max_players TINYINT UNSIGNED, password TINYINT UNSIGNED, running TINYINT UNSIGNED,
               match_players TINYINT UNSIGNED, mode VARCHAR(12), world VARCHAR(16), map VARCHAR(32), motd VARCHAR(200),
               ping_ms INT UNSIGNED, matches INT UNSIGNED, started INT UNSIGNED, first_seen INT UNSIGNED, last_seen INT UNSIGNED,
               PRIMARY KEY (host, port), KEY last_seen (last_seen)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
            "CREATE TABLE IF NOT EXISTS samples (
               host VARCHAR(128) NOT NULL, port INT UNSIGNED NOT NULL, at INT UNSIGNED NOT NULL, players TINYINT UNSIGNED,
               running TINYINT UNSIGNED, KEY samples_srv (host, port, at), KEY samples_at (at)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
            "CREATE TABLE IF NOT EXISTS hits (ip VARCHAR(64) NOT NULL, at INT UNSIGNED NOT NULL, KEY hits_ip (ip, at))
               ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
        ]],
        2 => ['leaderboards and the match history with replays (0.2)', [
            "CREATE TABLE IF NOT EXISTS leaders (
               host VARCHAR(128) NOT NULL, port INT UNSIGNED NOT NULL, server_name VARCHAR(48), name VARCHAR(16) NOT NULL,
               rating SMALLINT UNSIGNED, wins INT UNSIGNED, matches INT UNSIGNED, score BIGINT, updated INT UNSIGNED,
               PRIMARY KEY (host, port, name), KEY rating (rating)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
            "CREATE TABLE IF NOT EXISTS matches (
               id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT, host VARCHAR(128) NOT NULL, port INT UNSIGNED NOT NULL,
               server_name VARCHAR(48), ended INT UNSIGNED NOT NULL, seconds INT UNSIGNED, mode VARCHAR(12), world VARCHAR(16),
               map VARCHAR(32), winner VARCHAR(10), players TEXT, has_replay TINYINT UNSIGNED DEFAULT 0,
               PRIMARY KEY (id), KEY server_ended (host, port, ended), KEY ended (ended)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
            "CREATE TABLE IF NOT EXISTS replays (match_id BIGINT UNSIGNED NOT NULL, replay MEDIUMBLOB, PRIMARY KEY (match_id))
               ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
        ]],
        3 => ['how each server\'s matches go, and its version (0.3)', [
            "ALTER TABLE servers ADD COLUMN IF NOT EXISTS time_limit INT UNSIGNED, ADD COLUMN IF NOT EXISTS time_rule VARCHAR(8),
               ADD COLUMN IF NOT EXISTS bots VARCHAR(8), ADD COLUMN IF NOT EXISTS watching VARCHAR(8),
               ADD COLUMN IF NOT EXISTS maps SMALLINT UNSIGNED, ADD COLUMN IF NOT EXISTS rotation VARCHAR(8),
               ADD COLUMN IF NOT EXISTS version VARCHAR(16)",
        ]],
        4 => ['who\'s on each server, and the map gallery (0.4)', [
            "ALTER TABLE servers ADD COLUMN IF NOT EXISTS people VARCHAR(400)",
            "CREATE TABLE IF NOT EXISTS gallery (
               id INT UNSIGNED NOT NULL AUTO_INCREMENT, name VARCHAR(40) NOT NULL, author VARCHAR(16), world VARCHAR(16),
               time VARCHAR(8), hash CHAR(40) NOT NULL, text MEDIUMTEXT NOT NULL, size INT UNSIGNED, uploaded INT UNSIGNED,
               downloads INT UNSIGNED DEFAULT 0, ip VARCHAR(64), hidden TINYINT UNSIGNED DEFAULT 0,
               PRIMARY KEY (id), UNIQUE KEY hash (hash), KEY name (name), KEY uploaded (uploaded)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
        ]],
        5 => ['the match that\'s on: how far in, the score (0.9)', [
            "ALTER TABLE servers ADD COLUMN IF NOT EXISTS elapsed INT UNSIGNED, ADD COLUMN IF NOT EXISTS score_camp INT,
               ADD COLUMN IF NOT EXISTS score_fortress INT",
        ]],
        6 => ['the daily challenge (0.9)', [
            "CREATE TABLE IF NOT EXISTS daily (
               id INT UNSIGNED NOT NULL AUTO_INCREMENT, day INT UNSIGNED NOT NULL, name VARCHAR(16) NOT NULL,
               ticks INT UNSIGNED NOT NULL, verified TINYINT UNSIGNED DEFAULT 0, replay MEDIUMBLOB, at INT UNSIGNED, ip VARCHAR(64),
               PRIMARY KEY (id), UNIQUE KEY day_name (day, name), KEY day_ticks (day, ticks)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
        ]],
        7 => ['the daily challenge: checked in the background, unusual times marked (0.9)', [
            "CREATE TABLE IF NOT EXISTS daily_queue (
               id INT UNSIGNED NOT NULL AUTO_INCREMENT, day INT UNSIGNED NOT NULL, name VARCHAR(16) NOT NULL,
               ticks INT UNSIGNED NOT NULL, replay MEDIUMBLOB, at INT UNSIGNED, ip VARCHAR(64),
               PRIMARY KEY (id), KEY at (at)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
            "ALTER TABLE daily ADD COLUMN IF NOT EXISTS flag VARCHAR(40)",
        ]],
        8 => ['matches live on the web site (0.9)', [
            "CREATE TABLE IF NOT EXISTS live (
               host VARCHAR(128) NOT NULL, port INT UNSIGNED NOT NULL, replay MEDIUMBLOB, updated INT UNSIGNED,
               PRIMARY KEY (host, port)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
        ]],
        9 => ['each server\'s mode: capture the flag, conquest, the deathmatches (0.9)', [
            "ALTER TABLE servers ADD COLUMN IF NOT EXISTS goal VARCHAR(16)",
        ]],
        10 => ['each match\'s mode: the leaderboards per mode (0.1)', [
            "ALTER TABLE matches ADD COLUMN IF NOT EXISTS goal VARCHAR(16)",
            "CREATE INDEX IF NOT EXISTS matches_goal ON matches (goal, ended)",
        ]],
        11 => ['the gallery\'s pictures: worked out once for each map (0.1)', [
            "ALTER TABLE gallery ADD COLUMN IF NOT EXISTS thumb TEXT",
        ]],
        12 => ['servers reached by a join code only, not on the list (0.1)', [
            "ALTER TABLE servers ADD COLUMN IF NOT EXISTS hidden TINYINT UNSIGNED NOT NULL DEFAULT 0",
        ]],
        13 => ['NAT traversal: how a server is seen from the internet, and players who want in (0.1)', [
            "ALTER TABLE servers ADD COLUMN IF NOT EXISTS stun VARCHAR(64)",
            "CREATE TABLE IF NOT EXISTS punch (
               host VARCHAR(128) NOT NULL, port INT UNSIGNED NOT NULL, ip VARCHAR(64) NOT NULL, cport INT UNSIGNED NOT NULL,
               at INT UNSIGNED NOT NULL, KEY punch_server (host, port, at)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
        ]],
        14 => ['cloud accounts, device sign-in and verified player statistics (0.1)', [
            "ALTER TABLE servers ADD COLUMN IF NOT EXISTS cloud_server VARCHAR(32)",
            "CREATE TABLE IF NOT EXISTS accounts (id CHAR(32) NOT NULL PRIMARY KEY, username VARCHAR(15) NOT NULL, username_key VARCHAR(15) NOT NULL UNIQUE, email VARCHAR(254) NOT NULL UNIQUE, password_hash VARCHAR(255) NOT NULL, player_key CHAR(64) NOT NULL, created BIGINT NOT NULL, password_changed BIGINT NOT NULL) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
            "CREATE TABLE IF NOT EXISTS account_sessions (token_hash CHAR(64) NOT NULL PRIMARY KEY, account_id CHAR(32) NOT NULL, expires BIGINT NOT NULL) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
            "CREATE TABLE IF NOT EXISTS account_tickets (token_hash CHAR(64) NOT NULL PRIMARY KEY, account_id CHAR(32) NOT NULL, server VARCHAR(32) NOT NULL, server_id CHAR(32) NOT NULL, nonce CHAR(16) NOT NULL, key_id CHAR(32) NOT NULL, expires BIGINT NOT NULL) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
            "CREATE TABLE IF NOT EXISTS account_resets (token_hash CHAR(64) NOT NULL PRIMARY KEY, account_id CHAR(32) NOT NULL, expires BIGINT NOT NULL) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
            "CREATE TABLE IF NOT EXISTS account_devices (token_hash CHAR(64) NOT NULL PRIMARY KEY, pin CHAR(8) NOT NULL UNIQUE, expires BIGINT NOT NULL, account_id CHAR(32)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
            "CREATE TABLE IF NOT EXISTS account_limits (bucket CHAR(64) NOT NULL PRIMARY KEY, started BIGINT NOT NULL, attempts INT NOT NULL) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
            "CREATE TABLE IF NOT EXISTS account_results (match_id BIGINT NOT NULL, account_id CHAR(32) NOT NULL, won INT NOT NULL, kills INT NOT NULL, score BIGINT NOT NULL, PRIMARY KEY(match_id, account_id)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
            "CREATE INDEX IF NOT EXISTS sessions_account ON account_sessions (account_id)",
            "CREATE INDEX IF NOT EXISTS results_account ON account_results (account_id)",
            "CREATE INDEX IF NOT EXISTS tickets_expires ON account_tickets (expires)",
            "CREATE INDEX IF NOT EXISTS sessions_expires ON account_sessions (expires)",

        ]],
    ];
}

function ofp_schema_latest(): int { return max(array_keys(ofp_migrations())); }

// where a database is: from ofp_meta, or (made before there was one) from what's in it
function ofp_schema_version(PDO $db): int
{
    try {
        $v = $db->query("SELECT v FROM ofp_meta WHERE k = 'schema_version'")->fetchColumn();
        if ($v !== false) return (int)$v;
    } catch (Throwable $e) { }
    $tables = array_map('strtolower', $db->query('SHOW TABLES')->fetchAll(PDO::FETCH_COLUMN));
    if (!in_array('servers', $tables, true)) return 0;
    $cols = $db->query('SHOW COLUMNS FROM servers')->fetchAll(PDO::FETCH_COLUMN);
    if (in_array('version', $cols, true)) return 3;
    if (in_array('matches', $tables, true)) return 2;
    return 1;
}

// bring it up to date; returns what was done (one line a step)
function ofp_migrate(PDO $db, bool $dry = false): array
{
    $done = [];
    $now = ofp_schema_version($db);
    foreach (ofp_migrations() as $v => [$what, $steps]) {
        if ($v <= $now) continue;
        if ($dry) { $done[] = "$v: $what (to do)"; continue; }
        foreach ($steps as $sql) $db->exec($sql);
        $db->exec('CREATE TABLE IF NOT EXISTS ofp_meta (k VARCHAR(32) NOT NULL PRIMARY KEY, v VARCHAR(64)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4');
        $db->prepare("REPLACE INTO ofp_meta (k, v) VALUES ('schema_version', ?)")->execute([(string)$v]);
        $done[] = "$v: $what";
    }
    return $done;
}
