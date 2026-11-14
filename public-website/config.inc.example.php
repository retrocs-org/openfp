<?php
// SPDX-License-Identifier: MIT
// The settings for the web site and the server listing, in one place.
// Copy to config.inc.php (next to this file) and fill in. It is never served:
// .htaccess (and your vhost) deny it, along with inc/.
return [
    'site' => [
        'name'     => 'OpenFP',
        'tagline'  => 'Classic tank combat, enhanced worlds and cross-platform battles',
        'base_url' => 'https://openfp.retrocs.org',
        // what the game and dedicated servers use: https. /api/ answers plain http as well, for
        // builds without TLS (old machines, the Amiga): keep that open in the web server too
        'game_api' => 'https://openfp.retrocs.org/api/',
        'repo_url' => 'https://github.com/retrocs-org/openfp',             // public source repository
        'contact'  => '',             // e.g. mailto:you@example.org
        // Downloads and client update notices follow published tagged releases automatically.
        'download_url'   => 'https://openfp.retrocs.org/#get',
        // the admin page (/admin/: maps in the gallery, forgetting a player): its password.
        // Long and random ('' = no admin page): php -r 'echo bin2hex(random_bytes(16)), "\n";'
        'admin_token'    => '',
        // Path to the current ofp_match on this machine, e.g. /usr/local/bin/ofp_match.
        // Required for gallery uploads. Also verifies daily challenge wins when configured.
        'ofp_match'      => '',
    ],

    // Cloud accounts use HTTPS only, independently of the legacy listing API.
    'accounts' => [
        'enabled' => true,
        'session_days' => 30,
        // Register each public dedicated server. Generate its secret and hash with:
        // php inc/cloud-server.php SERVER_NAME
        'servers' => [
            // 'retrocs-main' => 'SHA256_OF_SERVER_SECRET',
        ],
    ],
    // An empty host or sender automatically disables password recovery.
    'smtp' => [
        'host' => '', 'port' => 587,
        'encryption' => 'starttls', // starttls, tls (implicit TLS, usually 465), or none
        'username' => '', 'password' => '', // optional relay authentication
        'from' => '', 'from_name' => 'OpenFP',
        'ehlo' => 'localhost', // this sender's hostname, sent in EHLO
        'bind' => '', // optional local sender IP:port, e.g. 192.0.2.10:0 or [2001:db8::10]:0
        'timeout' => 10,
    ],

    // php inc/migrate.php: who may change the tables (only what differs from 'db'; leave out to use 'db')
    // 'db_admin' => ['user' => 'root', 'password' => '...'],

    // the listing's database: MariaDB / MySQL
    'db' => [
        'dsn'      => 'mysql:host=db.example.org;port=3306;dbname=openfirepower;charset=utf8mb4',
        'user'     => 'openfirepower',
        'password' => 'change-me',
        // a remote database: talk to it over TLS. The CA that signed its certificate
        // ('' = no TLS; then keep the database on a private network or a tunnel)
        'ssl_ca'   => '',
        // true: the site creates and upgrades its tables itself (this user needs CREATE, ALTER, INDEX).
        // false: run  php inc/migrate.php up  after each update instead (with 'db_admin' below)
        'create_tables' => true,
    ],

    // chat rooms told about what happens here: incoming webhooks (Rocket.Chat, Slack, Mattermost,
    // Matrix's hookshot: 'format' => 'text'; Discord: 'discord'). Events: server_new (a server
    // appears on the list), map (someone shares a map), ladder (a ladder has a new leader).
    'webhooks' => [
        // ['url' => 'https://chat.example.org/hooks/...', 'format' => 'text', 'events' => ['server_new', 'map', 'ladder', 'daily']],
    ],

    // ladders: seasons on chosen servers, ranked from the match history (the Ladders page).
    // A win is 3 points; 'teams' (optional) ranks fixed teams by their members' results too.
    'ladders' => [
        // [
        //     'slug'        => 'winter-2026',
        //     'name'        => 'Winter Ladder 2026',
        //     'about'       => 'Versus on the RetroCS servers. Most points by New Year wins.',
        //     'servers'     => ['game.retrocs.org:29870'],     // HOST:PORT as listed
        //     'from'        => '2026-12-01',
        //     'to'          => '2026-12-31',                    // inclusive
        //     'min_matches' => 5,                               // to be ranked
        //     'teams'       => ['Steel Wolves' => ['Anna', 'Bert'], 'Night Owls' => ['Cleo', 'Dora']],
        // ],
    ],

    'listing' => [
        'expire_after'  => 180,    // a server drops off this many seconds after its last announcement
        'probe'         => true,   // ask each announcing server over UDP whether it's really there
        'probe_timeout' => 1.5,
        'rate_limit'    => 60,     // announcements per address per 10 minutes
        'allow_host'    => true,   // servers may name their public host (DNS name, behind NAT)
        // behind a reverse proxy (HAProxy, nginx): its address(es). The announcing
        // server's own address is then taken from X-Forwarded-For. The listing checks
        // that address over UDP, so it must be the real one.
        // proxies in front of the site, whose X-Forwarded-For is believed: addresses or ranges.
        // Behind Cloudflare, add its ranges too (https://www.cloudflare.com/ips/), e.g.
        // ['172.16.1.1', '173.245.48.0/20', '103.21.244.0/22', ..., '2400:cb00::/32', ...]
        'trusted_proxies' => [],
        // game servers on the site's own network arrive from inside addresses: list and probe
        // them at the public address instead (the router needs NAT reflection for their port)
        // 'local_public' => ['from' => ['172.16.0.0/16'], 'address' => '203.0.113.10'],
        'history_hours' => 24,     // the charts: this many hours back
        'sample_every'  => 240,    // a player count per server this often (seconds)
    ],
];
