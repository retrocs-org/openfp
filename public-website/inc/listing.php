<?php
// SPDX-License-Identifier: MIT
// The OpenFP server listing: the database, the UDP check, the list.
// Used by the API (api/index.php) and by the web site's server page.

require_once __DIR__ . '/map-catalog.php';

function ofp_settings(): array
{
    static $cfg = null;
    if ($cfg !== null) return $cfg;
    $file = __DIR__ . '/../config.inc.php';
    if (!is_file($file)) $file = __DIR__ . '/../config.inc.example.php';
    $cfg = require $file;
    $cfg['listing'] = array_merge(['expire_after' => 180, 'probe' => true, 'probe_timeout' => 1.5, 'rate_limit' => 60,
        'allow_host' => true, 'history_hours' => 24, 'sample_every' => 240, 'trusted_proxies' => []], $cfg['listing'] ?? []);
    return $cfg;
}

function ofp_config(): array { return ofp_settings()['listing']; }

function ofp_db(): PDO
{
    static $db = null;
    if ($db) return $db;
    $c = ofp_settings()['db'];
    $opt = [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION, PDO::ATTR_TIMEOUT => 5, PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC];
    if (strncmp($c['dsn'], 'mysql:', 6) !== 0)          // (MariaDB / MySQL only)
        throw new RuntimeException("db.dsn must be a MariaDB DSN (mysql:host=...)");
    if (!empty($c['ssl_ca'])) {
        $opt[PDO::MYSQL_ATTR_SSL_CA] = $c['ssl_ca'];
        $opt[PDO::MYSQL_ATTR_SSL_VERIFY_SERVER_CERT] = true;
    }
    $db = new PDO($c['dsn'], $c['user'] ?? null, $c['password'] ?? null, $opt);
    if ($c['create_tables'] ?? true) ofp_create_tables($db);
    return $db;
}

function ofp_create_tables(PDO $db): void
{
    require_once __DIR__ . '/migrations.php';           // MariaDB: the migrations (create_tables => true)
    ofp_migrate($db);
}

// the game's UDP query: header 'O' 'F' protocol type, then the answer (see src/common/net.c)
const OFP_PROTOCOL = 38;   // (servers of 8 read it too: newer packets are read, and any server answers a query)
const OFP_QUERY = 18;
const OFP_INFO = 19;

function ofp_probe(string $host, int $port, float $timeout): ?array
{
    $target = strpos($host, ':') !== false ? "[$host]" : $host;   // (an IPv6 address: in brackets)
    $sock = @stream_socket_client("udp://$target:$port", $errno, $errstr, $timeout);
    if (!$sock) return null;
    stream_set_timeout($sock, (int)$timeout, (int)(($timeout - (int)$timeout) * 1e6));
    $start = microtime(true);
    $answer = null;
    for ($try = 0; $try < 3 && $answer === null; $try++) {   // UDP: ask up to three times
        // padded to 160 bytes: servers ignore shorter questions (they'd be an amplifier otherwise)
        fwrite($sock, str_pad("OF" . chr(OFP_PROTOCOL) . chr(OFP_QUERY), 160, "\0"));
        $data = fread($sock, 1400);
        if ($data !== false && strlen($data) > 8 && substr($data, 0, 2) === 'OF' && ord($data[3]) === OFP_INFO)
            $answer = $data;
        if (microtime(true) - $start > $timeout) break;
    }
    fclose($sock);
    if ($answer === null) return null;
    $p = 4;
    $u8 = function () use (&$answer, &$p) { return $p < strlen($answer) ? ord($answer[$p++]) : 0; };
    $str = function () use (&$answer, &$p, $u8) { $n = $u8(); $s = substr($answer, $p, $n); $p += $n; return preg_replace('/[^\x20-\x7E]/', '', $s); };
    $build = unpack('V', substr($answer, $p, 4))[1]; $p += 4;
    $info = ['build' => $build, 'name' => $str(), 'world' => $str(), 'map' => $str()];
    $info['players'] = $u8(); $info['max_players'] = $u8(); $info['password'] = $u8();
    $info['running'] = $u8(); $info['versus'] = $u8(); $info['match_players'] = $u8();
    $info['ping_ms'] = (int)round((microtime(true) - $start) * 1000);
    return $info;
}

// the columns a table has (an older database may lack the newer ones)
function ofp_columns(string $table): array
{
    static $cache = [];
    if (isset($cache[$table])) return $cache[$table];
    $db = ofp_db();
    $cols = [];
    foreach ($db->query("SHOW COLUMNS FROM $table") as $r) $cols[] = $r['Field'];
    return $cache[$table] = $cols;
}

// add or refresh a server
function ofp_store_server(array $row): void
{
    $have = ofp_columns('servers');
    $row = array_intersect_key($row, array_flip($have));   // (what this database can hold)
    $cols = array_keys($row);
    $marks = implode(', ', array_fill(0, count($cols), '?'));
    $keep = array_diff($cols, ['host', 'port', 'first_seen']);
    $sql = 'INSERT INTO servers(' . implode(', ', $cols) . ") VALUES($marks) ON DUPLICATE KEY UPDATE " .
           implode(', ', array_map(fn($c) => "$c = VALUES($c)", $keep));
    ofp_db()->prepare($sql)->execute(array_values($row));
}

function ofp_expire(): void
{
    $db = ofp_db();
    try { ofp_privacy_tidy(); } catch (Throwable $e) { }
    $db->prepare('DELETE FROM servers WHERE last_seen < ?')->execute([time() - ofp_config()['expire_after']]);
    $db->prepare('DELETE FROM samples WHERE at < ?')->execute([time() - 7 * 86400]);
}

// every listed server, with its last day: peak and average players, and a
// player count per hour for a small chart
function ofp_list(): array
{
    $cfg = ofp_config();
    $db = ofp_db();
    ofp_expire();
    $hide = in_array('hidden', ofp_columns('servers'), true) ? ' WHERE hidden = 0' : '';   // (join-code only: not listed)
    $servers = $db->query("SELECT * FROM servers$hide ORDER BY players DESC, name")->fetchAll();
    $hours = (int)$cfg['history_hours'];
    $since = time() - $hours * 3600;
    $q = $db->prepare('SELECT at, players FROM samples WHERE host = ? AND port = ? AND at >= ? ORDER BY at');
    foreach ($servers as &$s) {
        $q->execute([$s['host'], $s['port'], $since]);
        $hist = array_fill(0, $hours, 0);
        $sum = 0; $n = 0; $peak = 0;
        foreach ($q->fetchAll() as $row) {
            $h = min($hours - 1, (int)(($row['at'] - $since) / 3600));
            $hist[$h] = max($hist[$h], (int)$row['players']);
            $peak = max($peak, (int)$row['players']);
            $sum += (int)$row['players']; $n++;
        }
        foreach (['port', 'players', 'max_players', 'password', 'running', 'ping_ms', 'matches', 'match_players', 'build',
                  'first_seen', 'last_seen', 'started', 'time_limit', 'maps'] as $k) $s[$k] = isset($s[$k]) ? (int)$s[$k] : null;
        foreach (['time_rule', 'bots', 'watching', 'rotation', 'version', 'people', 'elapsed', 'score_camp', 'score_fortress'] as $k)
            $s[$k] = $s[$k] ?? null;
        $s['peak_24h'] = max($peak, $s['players']);
        $s['average_24h'] = $n ? round($sum / $n, 1) : (float)$s['players'];
        $s['history'] = $hist;
        $s['uptime'] = $s['started'] ? max(0, time() - $s['started']) : null;
    }
    return $servers;
}

// the whole listing at a glance
function ofp_totals(array $servers): array
{
    $q = ofp_db()->prepare('SELECT COALESCE(MAX(t), 0) FROM (SELECT SUM(players) AS t FROM samples WHERE at >= ?
                            GROUP BY at - at % 300) AS per_slot');
    $q->execute([time() - 86400]);
    $players = array_sum(array_column($servers, 'players'));
    return [
        'servers' => count($servers),
        'players' => $players,
        'playing' => count(array_filter($servers, fn($s) => $s['running'])),
        'peak_players_24h' => max((int)$q->fetchColumn(), $players),
        'matches' => array_sum(array_column($servers, 'matches')),
    ];
}

function ofp_clean(string $s, int $max): string
{
    return substr(trim(preg_replace('/[^\x20-\x7E]/', '', $s)), 0, $max);   // ASCII only: no mbstring needed
}

// who's asking: the connection's address, or behind a trusted proxy the last
// address in X-Forwarded-For that isn't one of the proxies
// an address in a list of addresses and ranges ("172.16.1.1", "173.245.48.0/20", "2400:cb00::/32")?
function ofp_ip_in(string $ip, array $list): bool
{
    $bin = @inet_pton($ip);
    if ($bin === false) return false;
    foreach ($list as $entry) {
        [$net, $bits] = array_pad(explode('/', (string)$entry, 2), 2, null);
        $nb = @inet_pton($net);
        if ($nb === false || strlen($nb) !== strlen($bin)) continue;
        $bits = $bits === null ? strlen($nb) * 8 : max(0, min(strlen($nb) * 8, (int)$bits));
        $full = intdiv($bits, 8);
        if (substr($bin, 0, $full) !== substr($nb, 0, $full)) continue;
        if ($bits % 8) {
            $mask = (0xFF << (8 - $bits % 8)) & 0xFF;
            if ((ord($bin[$full]) & $mask) !== (ord($nb[$full]) & $mask)) continue;
        }
        return true;
    }
    return false;
}

// a server on the same network as the site arrives from an inside address (often the
// router's own); 'local_public' in the config says which public address those stand for,
// so it's listed and probed there (the router must reflect it: NAT reflection)
function ofp_public_ip(string $ip): string
{
    $lp = ofp_config()['local_public'] ?? null;
    if (!$lp || empty($lp['address']) || !filter_var($lp['address'], FILTER_VALIDATE_IP)) return $ip;
    return ofp_ip_in($ip, (array)($lp['from'] ?? [])) ? $lp['address'] : $ip;
}

// who's really calling: REMOTE_ADDR, or behind proxies we trust (HAProxy, Cloudflare's
// ranges) the last address in X-Forwarded-For that isn't one of them
function ofp_client_ip(): string
{
    $ip = $_SERVER['REMOTE_ADDR'] ?? '';
    $proxies = ofp_config()['trusted_proxies'];
    if (!$proxies || !ofp_ip_in($ip, $proxies) || empty($_SERVER['HTTP_X_FORWARDED_FOR'])) return $ip;
    $chain = array_reverse(array_map('trim', explode(',', $_SERVER['HTTP_X_FORWARDED_FOR'])));
    foreach ($chain as $hop) {
        if (!filter_var($hop, FILTER_VALIDATE_IP)) break;
        if (!ofp_ip_in($hop, $proxies)) return $hop;
    }
    return $ip;
}

// ---- 0.2: leaderboards and the match history ---------------------------------------

// a server's own leaderboard, as it announced it ("name\trating\twins\tmatches\tscore" lines)
function ofp_store_leaders(string $host, int $port, string $server_name, string $lines): void
{
    $db = ofp_db();
    $db->prepare('DELETE FROM leaders WHERE host = ? AND port = ?')->execute([$host, $port]);
    $ins = $db->prepare('INSERT INTO leaders(host, port, server_name, name, rating, wins, matches, score, updated)
                         VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)');
    $n = 0;
    foreach (explode("\n", $lines) as $line) {
        $f = explode("\t", trim($line, "\r"));
        if (count($f) < 4 || $n >= 10) continue;
        $name = ofp_clean($f[0], 15);
        if ($name === '') continue;
        $ins->execute([$host, $port, ofp_clean($server_name, 40), $name, max(0, min(4000, (int)$f[1])),
                       max(0, (int)$f[2]), max(0, (int)$f[3]), (int)($f[4] ?? 0), time()]);
        $n++;
    }
}

// a player's rating, for QUICK MATCH: the best any server's leaderboard gives that name (last 30 days; 0: none)
function ofp_player_rating(string $name): int
{
    $q = ofp_db()->prepare('SELECT MAX(rating) FROM leaders WHERE LOWER(name) = LOWER(?) AND updated >= ?');
    $q->execute([ofp_clean($name, 15), time() - 30 * 86400]);
    return (int)$q->fetchColumn();
}

// the people on a server now, rated: the average of those its own leaderboard knows (0: none known)
function ofp_server_rating(array $s): int
{
    $people = array_filter(array_map('trim', explode(',', (string)($s['people'] ?? ''))));
    if (!$people) return 0;
    $q = ofp_db()->prepare('SELECT rating FROM leaders WHERE host = ? AND port = ? AND LOWER(name) = LOWER(?)');
    $sum = 0; $n = 0;
    foreach ($people as $name) {
        $q->execute([$s['host'], (int)$s['port'], $name]);
        $r = $q->fetchColumn();
        if ($r !== false) { $sum += (int)$r; $n++; }
    }
    return $n ? (int)round($sum / $n) : 0;
}

// the best players across every server that has announced in the last 30 days
function ofp_leaders(int $limit = 50, ?string $host = null, ?int $port = null): array
{
    $db = ofp_db();
    if ($host !== null) {
        $q = $db->prepare('SELECT * FROM leaders WHERE host = ? AND port = ? ORDER BY rating DESC, wins DESC LIMIT ' . (int)$limit);
        $q->execute([$host, $port]);
    } else {
        $q = $db->prepare('SELECT * FROM leaders WHERE updated >= ? ORDER BY rating DESC, wins DESC LIMIT ' . (int)$limit);
        $q->execute([time() - 30 * 86400]);
    }
    return $q->fetchAll();
}

// the modes a match can be played in: their keys (the server's goal) and names
const OFP_GOALS = ['flag' => 'Capture the flag', 'conquest' => 'Conquest', 'hardpoint' => 'Hardpoint',
                   'deathmatch' => 'Team deathmatch', 'killconfirmed' => 'Kill confirmed', 'annihilation' => 'Annihilation',
                   'warlord' => 'Warlord', 'hill' => 'The flag or the hill'];

// a mode's leaderboard: from the shared matches of the last 30 days, every server's, each player's wins and
// matches in that mode (the most wins first, then the best share of them)
function ofp_mode_leaders(string $goal, int $limit = 50): array
{
    if (!isset(OFP_GOALS[$goal])) return [];
    $q = ofp_db()->prepare('SELECT players FROM matches WHERE goal = ? AND ended >= ?');
    $q->execute([$goal, time() - 30 * 86400]);
    $by = [];
    foreach ($q->fetchAll() as $row) {
        foreach (json_decode($row['players'] ?: '[]', true) ?: [] as $p) {
            if (!empty($p['bot']) || ($p['name'] ?? '') === '') continue;
            $n = $p['name'];
            $by[$n] ??= ['name' => $n, 'wins' => 0, 'matches' => 0, 'kills' => 0];
            $by[$n]['matches']++;
            $by[$n]['wins'] += (int)!empty($p['won']);
            $by[$n]['kills'] += (int)($p['kills'] ?? 0);
        }
    }
    $list = array_values($by);
    usort($list, fn($a, $b) => [$b['wins'], $b['wins'] / max(1, $b['matches'])] <=> [$a['wins'], $a['wins'] / max(1, $a['matches'])]);
    return array_slice($list, 0, $limit);
}

// the matches, newest first (one server's, or all)
function ofp_matches(int $limit = 50, ?string $host = null, ?int $port = null, int $before = 0): array
{
    $db = ofp_db();
    $where = $before ? 'ended < ' . (int)$before : '1 = 1';
    if ($host !== null) {
        $q = $db->prepare("SELECT * FROM matches WHERE host = ? AND port = ? AND $where ORDER BY ended DESC LIMIT " . (int)$limit);
        $q->execute([$host, $port]);
    } else {
        $q = $db->query("SELECT * FROM matches WHERE $where ORDER BY ended DESC LIMIT " . (int)$limit);
    }
    $rows = $q->fetchAll();
    foreach ($rows as &$m) {
        $m['players'] = json_decode($m['players'] ?: '[]', true) ?: [];
        foreach (['id', 'port', 'ended', 'seconds', 'has_replay'] as $k) $m[$k] = (int)$m[$k];
    }
    return $rows;
}

function ofp_store_match(array $m, ?string $replay): int
{
    $db = ofp_db();
    $db->prepare('INSERT INTO matches(host, port, server_name, ended, seconds, mode, goal, world, map, winner, players, has_replay)
                  VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)')
       ->execute([$m['host'], $m['port'], $m['server_name'], time(), $m['seconds'], $m['mode'], $m['goal'] ?? 'flag', $m['world'], $m['map'],
                  $m['winner'], json_encode($m['players']), $replay !== null ? 1 : 0]);
    $id = (int)$db->lastInsertId();
    if ($replay !== null) {
        $q = $db->prepare('INSERT INTO replays(match_id, replay) VALUES(?, ?)');
        $q->bindValue(1, $id, PDO::PARAM_INT);
        $q->bindValue(2, $replay, PDO::PARAM_LOB);
        $q->execute();
    }
    // keep each server's last 500 matches and the last 90 days
    $old = $db->prepare('SELECT id FROM matches WHERE (host = ? AND port = ? AND id <= ?) OR ended < ?');
    $old->execute([$m['host'], $m['port'], $id - 500, time() - 90 * 86400]);
    foreach ($old->fetchAll(PDO::FETCH_COLUMN) as $gone) {
        $db->prepare('DELETE FROM replays WHERE match_id = ?')->execute([$gone]);
        $db->prepare('DELETE FROM matches WHERE id = ?')->execute([$gone]);
    }
    return $id;
}

// ---- 0.3: ladders --------------------------------------------------------------------

function ofp_ladders(): array { return ofp_settings()['ladders'] ?? []; }

function ofp_ladder(string $slug): ?array
{
    foreach (ofp_ladders() as $l) if (($l['slug'] ?? '') === $slug) return $l;
    return null;
}

// a ladder's standings, from the matches on its servers between its dates
function ofp_ladder_table(array $l): array
{
    $db = ofp_db();
    $from = strtotime(($l['from'] ?? '2000-01-01') . ' 00:00:00');
    $to = strtotime(($l['to'] ?? '2100-01-01') . ' 23:59:59');
    $players = []; $teams = []; $matches = 0;
    $member = [];
    foreach ($l['teams'] ?? [] as $team => $names) foreach ($names as $n) $member[strtolower($n)] = $team;
    foreach ($l['servers'] ?? [] as $sv) {
        if (!preg_match('/^([A-Za-z0-9.\-]+):(\d{1,5})$/', $sv, $m)) continue;
        $q = $db->prepare('SELECT players, winner FROM matches WHERE host = ? AND port = ? AND ended BETWEEN ? AND ?');
        $q->execute([$m[1], (int)$m[2], $from, $to]);
        foreach ($q->fetchAll() as $row) {
            $ps = json_decode($row['players'] ?: '[]', true) ?: [];
            $matches++;
            $teams_here = [];
            foreach ($ps as $p) {
                if (!empty($p['bot'])) continue;
                $k = strtolower($p['name']);
                $e = &$players[$k];
                if (!$e) $e = ['name' => $p['name'], 'played' => 0, 'won' => 0, 'lost' => 0, 'points' => 0, 'kills' => 0, 'rating' => 0, 'team' => $member[$k] ?? null];
                $e['played']++;
                $e['kills'] += (int)$p['kills'];
                $e['rating'] += (int)$p['change'];
                if ($p['won']) { $e['won']++; $e['points'] += 3; }
                elseif ($row['winner'] === 'draw') { $e['drawn'] = ($e['drawn'] ?? 0) + 1; $e['points'] += 1; }   // (a draw: a point)
                elseif ($row['winner'] !== 'none') $e['lost']++;
                if (isset($member[$k])) $teams_here[$member[$k]][] = (int)!!$p['won'];
                unset($e);
            }
            foreach ($teams_here as $team => $res) {   // a team plays when its members do; it wins if they did
                $t = &$teams[$team];
                if (!$t) $t = ['name' => $team, 'played' => 0, 'won' => 0, 'points' => 0, 'members' => $l['teams'][$team]];
                $t['played']++;
                if (max($res)) { $t['won']++; $t['points'] += 3; }
                unset($t);
            }
        }
    }
    $min = (int)($l['min_matches'] ?? 0);
    $ranked = array_filter($players, fn($p) => $p['played'] >= $min);
    $unranked = array_filter($players, fn($p) => $p['played'] < $min);
    $order = fn($a, $b) => [$b['points'], $b['won'], $b['rating']] <=> [$a['points'], $a['won'], $a['rating']];
    usort($ranked, $order);
    usort($unranked, $order);
    usort($teams, fn($a, $b) => [$b['points'], $b['won']] <=> [$a['points'], $a['won']]);
    return ['players' => array_values($ranked), 'unranked' => array_values($unranked), 'teams' => array_values($teams),
            'matches' => $matches, 'from' => $from, 'to' => $to, 'running' => time() >= $from && time() <= $to];
}

// is the database behind this code? (for a notice on the site; null = can't tell)
function ofp_schema_behind(): ?int
{
    static $r = false;
    if ($r !== false) return $r;
    try {
        require_once __DIR__ . '/migrations.php';
        $r = max(0, ofp_schema_latest() - ofp_schema_version(ofp_db()));
    } catch (Throwable $e) { $r = null; }
    return $r;
}

// ---- 0.4: players --------------------------------------------------------------------

const OFP_TANKS = ['Jackal', 'Bison', 'Mammoth'];
const OFP_ACHIEVEMENTS = [                               // (as src/common/achieve.c)
    ['Victory', 'Win a match'], ['Untouchable', 'Win, losing no tank'], ['Tank Buster', '3 tanks in one match'],
    ['Rescuer', '5 men saved in a match'], ['Lifesaver', '100 men saved in all'], ['Heli Hunter', 'Shoot down a helicopter'],
    ['Centurion', '100 soldiers in all'], ['Night Owl', 'Win at night'], ['World Tour', 'Win in all five worlds'],
    ['Veteran', 'Play 25 matches'],
];

// everything about one player: on one server, or on all of them
function ofp_player(string $name, ?string $host = null, ?int $port = null): array
{
    $db = ofp_db();
    $like = '%' . json_encode($name) . '%';
    if ($host !== null) {
        $q = $db->prepare('SELECT * FROM matches WHERE host = ? AND port = ? AND players LIKE ? ORDER BY ended');
        $q->execute([$host, $port, $like]);
    } else {
        $q = $db->prepare('SELECT * FROM matches WHERE players LIKE ? ORDER BY ended');
        $q->execute([$like]);
    }
    $p = ['name' => $name, 'matches' => 0, 'wins' => 0, 'kills' => 0, 'score' => 0, 'rating' => null, 'history' => [],
          'tanks' => [0, 0, 0], 'worlds' => [], 'ach' => 0, 'servers' => [], 'recent' => []];
    foreach ($q->fetchAll() as $m) {
        foreach (json_decode($m['players'] ?: '[]', true) ?: [] as $pl) {
            if (!empty($pl['bot']) || strcasecmp($pl['name'], $name)) continue;
            $p['name'] = $pl['name'];
            $p['matches']++;
            $p['wins'] += (int)$pl['won'];
            $p['kills'] += (int)$pl['kills'];
            $p['score'] += (int)$pl['score'];
            $key = $m['host'] . ':' . $m['port'];
            $p['servers'][$key] = $m['server_name'];
            if ($host !== null || count($p['servers']) === 1) { $p['rating'] = (int)$pl['rating']; $p['history'][] = (int)$pl['rating']; }
            if (isset($pl['tank']) && $pl['tank'] !== null) $p['tanks'][(int)$pl['tank']]++;
            $w = &$p['worlds'][$m['world'] ?: 'classic'];
            if (!$w) $w = ['played' => 0, 'won' => 0];
            $w['played']++;
            $w['won'] += (int)$pl['won'];
            unset($w);
            $p['ach'] |= (int)($pl['ach'] ?? 0);
            $p['recent'][] = ['id' => (int)$m['id'], 'ended' => (int)$m['ended'], 'map' => $m['map'], 'world' => $m['world'],
                              'server' => $m['server_name'], 'won' => (int)$pl['won'], 'change' => (int)$pl['change'],
                              'kills' => (int)$pl['kills'], 'replay' => (int)$m['has_replay']];
        }
    }
    $p['recent'] = array_reverse(array_slice($p['recent'], -15));
    return $p;
}

// ---- 0.4: webhooks -------------------------------------------------------------------

function ofp_webhook(string $event, string $text): void
{
    foreach (ofp_settings()['webhooks'] ?? [] as $w) {
        if (empty($w['url']) || !in_array($event, $w['events'] ?? ['server_new', 'map', 'ladder'], true)) continue;
        // (map names, authors and player names come from anyone: no @everyone, @here or role pings)
        if (($w["format"] ?? "text") === "discord")
            $payload = ["content" => $text, "username" => "OpenFP", "allowed_mentions" => ["parse" => []]];
        else   // (Slack and the like: <!channel>, @here)
            $payload = ["text" => str_replace(['<!', '@'], ['< !', "@\u{200B}"], $text), "username" => "OpenFP"];
        $body = json_encode($payload, JSON_UNESCAPED_SLASHES);
        $ctx = stream_context_create(['http' => ['method' => 'POST', 'timeout' => 3, 'ignore_errors' => true,
                                                 'header' => "Content-Type: application/json\r\n", 'content' => $body]]);
        if (@file_get_contents($w['url'], false, $ctx) === false) error_log("openfirepower: webhook $event: no answer from " . $w['url']);
    }
}

// who leads each ladder this match counts for (to say when that changes)
function ofp_ladder_leaders(string $host, int $port): array
{
    $out = [];
    foreach (ofp_ladders() as $l) {
        if (!in_array("$host:$port", $l['servers'] ?? [], true)) continue;
        $t = ofp_ladder_table($l);
        $out[$l['slug']] = [$l['name'], $t['players'][0]['name'] ?? null];
    }
    return $out;
}

// ---- 0.5: privacy ------------------------------------------------------------------

// forget a player: their place on the leaderboards, their name in the match history (it
// becomes "a player"), their name on shared maps; on one server, or on all. Returns counts.
function ofp_forget(string $name, ?string $host = null, ?int $port = null): array
{
    $db = ofp_db();
    $n = ['leaders' => 0, 'matches' => 0, 'maps' => 0];
    $where = $host !== null ? ' AND host = ? AND port = ?' : '';
    $args = $host !== null ? [$host, $port] : [];
    $q = $db->prepare("DELETE FROM leaders WHERE name = ?$where");
    $q->execute(array_merge([$name], $args));
    $n['leaders'] = $q->rowCount();
    $q = $db->prepare("SELECT id, players FROM matches WHERE players LIKE ?$where");
    $q->execute(array_merge(['%' . json_encode($name) . '%'], $args));
    $up = $db->prepare('UPDATE matches SET players = ? WHERE id = ?');
    foreach ($q->fetchAll() as $m) {
        $ps = json_decode($m['players'], true) ?: [];
        foreach ($ps as &$p) if (!strcasecmp($p['name'], $name)) { $p['name'] = 'a player'; $p['ach'] = 0; }
        unset($p);
        $up->execute([json_encode($ps), $m['id']]);
        $n['matches']++;
    }
    if ($host === null) {
        $q = $db->prepare("UPDATE gallery SET author = '' WHERE author = ?");
        $q->execute([$name]);
        $n['maps'] = $q->rowCount();
    }
    return $n;
}

// the addresses kept for the limits go once the limits no longer need them
function ofp_privacy_tidy(): void
{
    $db = ofp_db();
    $db->prepare("UPDATE gallery SET ip = '' WHERE uploaded < ? AND ip <> ''")->execute([time() - 86400]);
}

function ofp_map_details(string $header): array
{
    $details = ['name' => '', 'author' => '', 'description' => '', 'contact' => '', 'biome' => '', 'time' => '', 'mode' => '', 'modes' => '', 'arena' => ''];
    $limits = ['name' => 40, 'author' => 40, 'description' => 160, 'contact' => 80, 'biome' => 16, 'time' => 7, 'mode' => 16, 'modes' => 160, 'arena' => 3];
    foreach (explode("\n", str_replace("\r", '', $header)) as $line) {
        if ($line === 'tiles') break;
        [$key, $value] = array_pad(explode('=', $line, 2), 2, '');
        if (isset($limits[$key])) $details[$key] = ofp_clean($value, $limits[$key]);
    }
    return $details;
}

// Filter before limiting the results, so an older matching map is not lost behind newer maps.
function ofp_gallery_maps(string $world = '', string $mode = '', int $limit = 200, ?PDO $db = null): array
{
    $limit = max(1, min(300, $limit));
    $db ??= ofp_db();
    $rows = [];
    for ($offset = 0; count($rows) < $limit; $offset += 300) {
        $q = $db->prepare("SELECT id, name, author, world, time, size, uploaded, downloads,
            SUBSTRING_INDEX(text, CONCAT(CHAR(10), 'tiles', CHAR(10)), 1) AS map_header
            FROM gallery WHERE hidden = 0" . ($world !== '' ? ' AND world = ?' : '')
            . ' ORDER BY uploaded DESC, id DESC LIMIT 300 OFFSET ' . $offset);
        $q->execute($world !== '' ? [$world] : []);
        $batch = $q->fetchAll();
        foreach ($batch as $m) {
            $details = ofp_map_details($m['map_header']);
            $m['modes'] = ofp_map_modes($details, $m['world']);
            if ($mode !== '' && !in_array($mode, $m['modes'], true)) continue;
            if ($m['author'] && $details['author'] !== '') $m['author'] = $details['author'];
            $m['description'] = $details['description'];
            $m['contact'] = $details['contact'];
            unset($m['map_header']);
            $rows[] = $m;
            if (count($rows) === $limit) break;
        }
        if (count($batch) < 300) break;
    }
    return $rows;
}

// a gallery map's picture: 64 x 64 characters (a tile's letter for each 2 x 2 of the map, or P/E a camp,
// p/e a building), worked out from its text once and kept beside it; the gallery page draws it
function ofp_map_thumb(string $text): string
{
    $lines = explode("\n", str_replace("\r", '', $text));
    $at = array_search('tiles', $lines, true);
    $grid = array_fill(0, 64, str_repeat('.', 64));
    if ($at !== false) {
        $rows = [];
        for ($y = $at + 1; $y < count($lines) && count($rows) < 256; $y++) {
            if ($lines[$y] === '' || $lines[$y] === 'end' || preg_match('/^[a-z_]+=/', $lines[$y])) break;   // (roads are '=': a key isn't)
            $rows[] = $lines[$y];
        }
        $h = count($rows); $w = $h ? max(array_map('strlen', $rows)) : 0;
        $scale = max(1, (int)ceil(max($w, $h) / 64));
        for ($ty = 0; $ty < 64; $ty++) {
            $line = '';
            for ($tx = 0; $tx < 64; $tx++) {
                $c = $rows[$ty * $scale] [$tx * $scale] ?? '.';
                $line .= strpos('.=~^T,#:x', $c) !== false ? $c : '.';
            }
            $grid[$ty] = $line;
        }
        foreach ($lines as $l) {
            if (preg_match('/^(base|struct=\S+) ?(player|enemy|independent)? ?(\d+) (\d+)/', str_replace('base=', 'base ', $l), $m)) {
                $x = (int)((int)$m[3] / $scale); $y = (int)((int)$m[4] / $scale);
                if ($x < 0 || $x > 63 || $y < 0 || $y > 63) continue;
                $mark = $m[1] === 'base' ? ($m[2] === 'enemy' ? 'E' : 'P') : (['enemy' => 'e', 'independent' => 'i'][$m[2]] ?? 'p');
                $grid[$y][$x] = $mark;
            }
        }
    }
    return implode('', $grid);
}

// the pictures of these gallery maps, by id (made and kept the first time anyone asks)
function ofp_map_thumbs(array $ids): array
{
    $ids = array_slice(array_values(array_unique(array_filter(array_map('intval', $ids)))), 0, 60);
    if (!$ids) return [];
    $db = ofp_db();
    $q = $db->prepare('SELECT id, thumb, text FROM gallery WHERE id IN (' . implode(',', array_fill(0, count($ids), '?')) . ')');
    $q->execute($ids);
    $out = [];
    $keep = $db->prepare('UPDATE gallery SET thumb = ? WHERE id = ?');
    foreach ($q->fetchAll() as $r) {
        $t = $r['thumb'];
        if (!$t || strlen($t) !== 4096) { $t = ofp_map_thumb((string)$r['text']); $keep->execute([$t, $r['id']]); }
        $out[(int)$r['id']] = $t;
    }
    return $out;
}

// a server's join code: six letters and digits (no 0/O, 1/I), the same for the same address every time, so
// a friend types that instead of an address. Worked out, not stored: the listing finds it among its servers.
const OFP_CODE_LETTERS = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
function ofp_join_code(string $host, int $port): string
{
    $h = sha1("ofp-join:$host:$port", true);
    $n = (ord($h[0]) << 22) | (ord($h[1]) << 14) | (ord($h[2]) << 6) | (ord($h[3]) >> 2);   // (30 bits)
    $c = '';                                             // (a letter first, a digit fourth: never mistaken for a name)
    for ($i = 0; $i < 6; $i++) {
        if ($i === 0) { $c .= OFP_CODE_LETTERS[$n % 24]; $n = intdiv($n, 24); }
        elseif ($i === 3) { $c .= OFP_CODE_LETTERS[24 + $n % 8]; $n = intdiv($n, 8); }
        else { $c .= OFP_CODE_LETTERS[$n & 31]; $n >>= 5; }
    }
    return $c;
}

// the server a join code stands for (listed or not), [host, port] or null
function ofp_join_find(string $code): ?array
{
    $code = strtoupper(preg_replace('/[^A-Za-z0-9]/', '', $code));
    if (strlen($code) !== 6) return null;
    ofp_expire();
    foreach (ofp_db()->query('SELECT host, port FROM servers')->fetchAll() as $r)
        if (hash_equals(ofp_join_code($r['host'], (int)$r['port']), $code)) return [$r['host'], (int)$r['port']];
    return null;
}

