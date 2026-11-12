<?php
// SPDX-License-Identifier: MIT
// The OpenFP server listing API, at https://openfp.retrocs.org/api/
// (and over plain http too: the game and the dedicated server don't speak TLS yet).
//
//   GET  /api/                    the list as JSON: servers, their last day, totals
//   GET  /api/?format=text        the same, one tab-separated line a server (what the game reads):
//                                 host port name players max_players password running mode world map build ping_ms
//                                 people goal rating (the people on it, as their leaderboard rates them: 0 unknown)
//   GET  /api/?action=join&code=XXXXXX  where a server with that join code is ("host<TAB>port[<TAB>ip<TAB>port]": the
//                                   last two how the internet sees it, for NAT traversal)
//   POST /api/  action=punch code, port (or ports, comma-separated)   players want in (NAT traversal); action=punches port: a server, who
//   GET  /api/?action=map_thumbs&ids=1,2 the gallery's pictures (64 x 64 characters each)
//   GET  /api/?action=rating&name=N   a player's rating (the best of the servers' leaderboards; 0: none)
//   GET  /api/?action=stats       just the totals
//   POST /api/  action=announce   a dedicated server says it's here (every minute or so):
//                port, name, build, players, max_players, password, running, mode, world, map,
//                matches, uptime, motd [, host]
//   POST /api/  action=remove     the server is shutting down: port [, host]
//   POST /api/  action=match      a listed server's finished match: port [, host], seconds, mode, world, map,
//                winner, players ("name side won kills score rating change bot" lines), replay
//   GET  /api/?action=matches     [&server=HOST:PORT] [&limit=N] [&before=UNIXTIME]   the match history
//   GET  /api/?action=replay&id=N the replay of a match (an .ofprep file: ofp_match verify plays it back)
//   GET  /api/?action=leaders     [&server=HOST:PORT]   the best players (ratings) across the servers
//   GET  /api/?action=version     the newest release: "VERSION<tab>DOWNLOAD URL" (or &format=json)
//   GET  /api/?action=maps        the map gallery [&world=desert]
//   GET  /api/?action=map&id=N    one map (.ofpmap text; or &name=NAME, the newest of that name)
//   POST /api/  action=map_upload a map for the gallery: map (the .ofpmap text) [, author]
//
// Announcements are checked: the listing asks the server itself over UDP and lists
// what it answers, so made-up addresses and player counts don't get in.
require __DIR__ . '/../inc/listing.php';
header('X-Content-Type-Options: nosniff');            // (replays and maps are text from anyone: never sniffed as a page)

function reply(int $code, string $text): void
{
    http_response_code($code);
    header('Content-Type: text/plain; charset=utf-8');
    echo $text, "\n";
    exit;
}

function json_out($data): void
{
    header('Content-Type: application/json; charset=utf-8');
    header('Access-Control-Allow-Origin: *');
    header('Cache-Control: no-store');
    echo json_encode($data, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);
    exit;
}

$cfg = ofp_config();
$post = $_SERVER['REQUEST_METHOD'] === 'POST';
$action = $_REQUEST['action'] ?? ($post ? '' : 'list');
// Release discovery does not need the listing database.
if (!$post && $action === 'version') {
    require_once __DIR__ . '/../inc/releases.php';
    $releases = site_releases();
    $versions = [];
    foreach ($releases['groups'] as $key => $group) {
        if ($key !== 'source') $versions[] = $group['version'];
    }
    usort($versions, static fn($a, $b) => version_compare($b, $a));
    $v = $versions[0] ?? '';
    $u = 'https://openfp.retrocs.org/play/#download';
    if (($_GET['format'] ?? 'text') === 'json') json_out(['version' => $v, 'download' => $u, 'protocol' => OFP_PROTOCOL]);
    header('Content-Type: text/plain; charset=utf-8');
    header('Cache-Control: public, max-age=300');
    echo $v, "\t", $u, "\n";
    exit;
}
try {
    $db = ofp_db();
} catch (Throwable $e) {
    error_log('openfirepower listing: database: ' . $e->getMessage());
    reply(503, 'the listing database is unavailable');
}

function server_arg(): array
{
    $sv = $_GET['server'] ?? '';
    if (!preg_match('/^\[?([A-Za-z0-9.\-:]+?)\]?:(\d{1,5})$/', $sv, $m)) return [null, null];
    return [$m[1], (int)$m[2]];
}

if (!$post) {
    if ($action === 'stats') json_out(ofp_totals(ofp_list()));
    if ($action === 'rating') {                          // QUICK MATCH: how a player is rated (0: not yet)
        header('Content-Type: text/plain; charset=utf-8');
        header('Cache-Control: no-store');
        echo ofp_player_rating((string)($_GET['name'] ?? '')), "\n";
        exit;
    }
    if ($action === 'leaders') {
        [$h, $p] = server_arg();
        json_out(['leaders' => ofp_leaders(min(200, max(1, (int)($_GET['limit'] ?? 50))), $h, $p), 'generated' => time()]);
    }
    if ($action === 'matches') {
        [$h, $p] = server_arg();
        json_out(['matches' => ofp_matches(min(200, max(1, (int)($_GET['limit'] ?? 50))), $h, $p, (int)($_GET['before'] ?? 0)),
                  'generated' => time()]);
    }
    if ($action === 'daily') {                           // the daily challenge: a day's best
        require_once __DIR__ . '/../inc/daily.php';
        $day = isset($_GET['day']) && preg_match('/^\d{8}$/', $_GET['day']) ? (int)$_GET['day'] : ofp_daily_today();
        json_out(['day' => $day, 'setup' => ofp_daily_setup($day), 'results' => ofp_daily_board($day)]);
    }
    if ($action === 'live') {                            // a match that's on, so far (?server=host:port)
        if (!preg_match('/^\[?([A-Za-z0-9.\-:]+?)\]?:(\d{1,5})$/', $_GET['server'] ?? '', $mm)) reply(400, 'which server?');
        $q = $db->prepare('SELECT replay, updated FROM live WHERE host = ? AND port = ?');
        $q->execute([$mm[1], (int)$mm[2]]);
        $r = $q->fetch();
        if (!$r || (int)$r['updated'] < time() - 60) reply(404, 'nothing on there now');
        header('Content-Type: text/plain; charset=utf-8');
        header('Cache-Control: no-store');
        echo $r['replay'];
        exit;
    }
    if ($action === 'daily_replay') {                    // one result's replay
        $q = $db->prepare('SELECT replay FROM daily WHERE id = ?');
        $q->execute([(int)($_GET['id'] ?? 0)]);
        $r = $q->fetchColumn();
        if ($r === false || $r === null) reply(404, 'no such replay');
        header('Content-Type: text/plain; charset=utf-8');
        if (!empty($_GET['download'])) header('Content-Disposition: attachment; filename="daily-' . (int)$_GET['id'] . '.ofprep"');
        echo $r;
        exit;
    }
    if ($action === 'maps') {                            // the gallery
        $w = preg_replace('/[^a-z]/', '', strtolower($_GET['world'] ?? ''));
        $q = $db->prepare("SELECT id, name, author, world, time, size, uploaded, downloads,
                                  SUBSTRING_INDEX(text, CONCAT(CHAR(10), 'tiles', CHAR(10)), 1) AS map_header
                           FROM gallery WHERE hidden = 0"
                          . ($w ? ' AND world = ?' : '') . ' ORDER BY uploaded DESC LIMIT 300');
        $q->execute($w ? [$w] : []);
        $rows = $q->fetchAll();
        foreach ($rows as &$m) {
            $details = ofp_map_details($m['map_header']);
            unset($m['map_header']);
            if ($m['author'] && $details['author'] !== '') $m['author'] = $details['author'];
            $m['description'] = $details['description'];
            $m['contact'] = $details['contact'];
        }
        unset($m);
        if (($_GET['format'] ?? '') === 'text') {        // (the game: id, name, author, world, time, size, downloads)
            header('Content-Type: text/plain; charset=utf-8');
            foreach ($rows as $m)
                echo implode("\t", [(int)$m['id'], str_replace("\t", ' ', $m['name']), str_replace("\t", ' ', (string)$m['author']),
                                    $m['world'], $m['time'], (int)$m['size'], (int)$m['downloads']]), "\n";
            exit;
        }
        json_out(['maps' => $rows, 'generated' => time()]);
    }
    if ($action === 'join') {                            // a join code: where that server is ("host<TAB>port")
        header('Content-Type: text/plain; charset=utf-8');
        header('Cache-Control: no-store');
        $where = ofp_join_find((string)($_GET['code'] ?? ''));
        if (!$where) reply(404, 'no server with that code (is it running?)');
        $q = $db->prepare('SELECT stun FROM servers WHERE host = ? AND port = ?');
        try { $q->execute([$where[0], $where[1]]); $stun = (string)$q->fetchColumn(); } catch (Throwable $e) { $stun = ''; }
        echo $where[0], "\t", $where[1], $stun !== '' ? "\t" . str_replace(' ', "\t", $stun) : '', "\n";   // (+ "ip port": how it's seen)
        exit;
    }
    if ($action === 'map_thumbs') {                      // the gallery's pictures: ?ids=1,2,3 (up to 60)
        header('Content-Type: application/json');
        header('Cache-Control: public, max-age=3600');
        echo json_encode(ofp_map_thumbs(explode(',', (string)($_GET['ids'] ?? ''))));
        exit;
    }
    if ($action === 'map') {                             // one map
        if (isset($_GET['id'])) {
            $q = $db->prepare('SELECT id, name, text FROM gallery WHERE id = ? AND hidden = 0');
            $q->execute([(int)$_GET['id']]);
        } else {
            $q = $db->prepare('SELECT id, name, text FROM gallery WHERE name = ? AND hidden = 0 ORDER BY uploaded DESC LIMIT 1');
            $q->execute([ofp_clean((string)($_GET['name'] ?? ''), 40)]);
        }
        $m = $q->fetch();
        if (!$m) reply(404, 'no such map');
        if (!empty($_GET['download'])) {
            $db->prepare('UPDATE gallery SET downloads = downloads + 1 WHERE id = ?')->execute([$m['id']]);
            header('Content-Disposition: attachment; filename="' . (preg_replace('/[^a-z0-9]+/', '-', strtolower($m['name'])) ?: 'map') . '.ofpmap"');
        }
        header('Content-Type: text/plain; charset=utf-8');
        echo $m['text'];
        exit;
    }
    if ($action === 'replay') {
        $q = $db->prepare('SELECT r.replay, m.map, m.ended FROM replays r JOIN matches m ON m.id = r.match_id WHERE r.match_id = ?');
        $q->execute([(int)($_GET['id'] ?? 0)]);
        $row = $q->fetch();
        if (!$row) reply(404, 'no such replay');
        $data = is_resource($row['replay']) ? stream_get_contents($row['replay']) : $row['replay'];
        header('Content-Type: text/plain; charset=utf-8');
        header('Content-Disposition: attachment; filename="openfirepower-' . (int)$_GET['id'] . '-' .
               preg_replace('/[^A-Za-z0-9]+/', '-', strtolower($row['map'])) . '.ofprep"');
        header('Cache-Control: public, max-age=86400');
        echo $data;
        exit;
    }
    if ($action !== 'list') reply(400, 'unknown action');
    $servers = ofp_list();
    if (($_GET['format'] ?? 'json') === 'text') {
        header('Content-Type: text/plain; charset=utf-8');
        header('Cache-Control: no-store');
        foreach ($servers as $s) {
            echo implode("\t", [$s['host'], $s['port'], str_replace("\t", ' ', $s['name']), $s['players'], $s['max_players'],
                                $s['password'], $s['running'], $s['mode'], $s['world'], $s['map'], $s['build'], $s['ping_ms'],
                                $s['people'] ?? '', $s['goal'] ?? 'flag', ofp_server_rating($s)]), "\n";
        }
        exit;
    }
    json_out(['servers' => $servers, 'totals' => ofp_totals($servers), 'generated' => time()]);
}

// ---- servers announcing themselves --------------------------------------------------
$ip = ofp_public_ip(ofp_client_ip());                    // (servers on the site's own network: its public address)

// Use the same announced identity for polling and for the other server operations.
function announced_host(string $ip, array $cfg): string
{
    if (!$cfg['allow_host'] || !is_string($_POST['host'] ?? null)) return $ip;
    $host = ofp_clean($_POST['host'], 120);
    if (!preg_match('/^[A-Za-z0-9.\-]+$/', $host)) return $ip;
    if ($host !== $ip && gethostbyname($host) !== $ip)
        reply(403, "$host doesn't point at $ip (the address this comes from)");
    return $host;
}

// ---- NAT traversal: a player who wants into a server by its join code leaves where the internet sees them
// (their STUN answer's port; their address is the one this comes from: nobody can ask a server to send to
// someone else); the server fetches those every few seconds and sends them a few packets, which opens its
// router to theirs while their game knocks on its door (docs/NETWORK.md)
if ($action === 'punch') {
    $where = ofp_join_find((string)($_POST['code'] ?? ''));
    if (!$where) reply(404, 'no server with that code');
    $raw = $_POST['ports'] ?? $_POST['port'] ?? '';
    if (!is_string($raw) || !preg_match('/^\d{1,5}(,\d{1,5}){0,3}$/D', $raw)) reply(400, 'bad port');
    $ports = array_unique(array_map('intval', explode(',', $raw)));
    foreach ($ports as $port) if ($port < 1 || $port > 65535) reply(400, 'bad port');
    $db->prepare('DELETE FROM punch WHERE at < ?')->execute([time() - 60]);
    $q = $db->prepare('SELECT COUNT(*) FROM punch WHERE ip = ?');
    $q->execute([$ip]);
    if ((int)$q->fetchColumn() + count($ports) > 20) reply(429, 'too many at once');
    $values = [];
    foreach ($ports as $port) array_push($values, $where[0], $where[1], $ip, $port, time());
    $db->prepare('INSERT INTO punch(host, port, ip, cport, at) VALUES ' .
        implode(', ', array_fill(0, count($ports), '(?, ?, ?, ?, ?)')))->execute($values);
    reply(200, 'ok');
}
if ($action === 'punches') {                             // a server: who wants in (their address and port, a line each)
    $sport = (int)($_POST['port'] ?? 0);
    if ($sport < 1 || $sport > 65535) reply(400, 'bad port');
    $key = [announced_host($ip, $cfg), $sport, time() - 30];
    $db->beginTransaction();
    try {
        $q = $db->prepare('SELECT ip, cport FROM punch WHERE host = ? AND port = ? AND at >= ? FOR UPDATE');
        $q->execute($key);
        $rows = $q->fetchAll();
        $db->prepare('DELETE FROM punch WHERE host = ? AND port = ? AND at >= ?')->execute($key);
        $db->commit();
    } catch (Throwable $e) {
        $db->rollBack();
        throw $e;
    }
    header('Content-Type: text/plain; charset=utf-8');
    foreach ($rows as $r) echo $r['ip'], "\t", (int)$r['cport'], "\n";
    echo "ok\n";
    exit;
}
$db->prepare('DELETE FROM hits WHERE at < ?')->execute([time() - 600]);
$q = $db->prepare('SELECT COUNT(*) FROM hits WHERE ip = ?');
$q->execute([$ip]);
if ((int)$q->fetchColumn() >= $cfg['rate_limit']) reply(429, 'too many announcements');
$db->prepare('INSERT INTO hits(ip, at) VALUES(?, ?)')->execute([$ip, time()]);

if ($action === 'daily_submit') {                        // a win in the daily challenge: the replay, checked
    require_once __DIR__ . '/../inc/daily.php';
    $day = (int)($_POST['day'] ?? 0);
    $today = ofp_daily_today();
    $yesterday = (int)gmdate('Ymd', time() - 86400);
    if ($day !== $today && $day !== $yesterday) reply(400, 'only today\'s challenge (or yesterday\'s, just after midnight)');
    $name = ofp_clean((string)($_POST['name'] ?? ''), 15);
    $text = str_replace("\r", '', (string)($_POST['replay'] ?? ''));
    if ($name === '' || strlen($text) > 3000000) reply(400, 'a name and a replay, please');
    [$ok, $ticks, $why] = ofp_daily_check($text, $day);
    if (!$ok) reply(400, $why);
    if (ofp_daily_checker()) {                           /* checked in the background: the queue (daily_worker.php) */
        $q = $db->prepare('SELECT COUNT(*) FROM daily_queue WHERE ip = ? AND at > ?');
        $q->execute([$ip, time() - 600]);
        if ((int)$q->fetchColumn() >= 20) reply(429, 'too many at once');
        $db->prepare('INSERT INTO daily_queue(day, name, ticks, replay, at, ip) VALUES(?, ?, ?, ?, ?, ?)')
           ->execute([$day, $name, $ticks, $text, time(), $ip]);
        $q = $db->prepare('SELECT COUNT(*) FROM daily WHERE day = ? AND ticks < ?');
        $q->execute([$day, $ticks]);
        $place = (int)$q->fetchColumn() + 1;
        $q = $db->prepare('SELECT COUNT(*) FROM daily WHERE day = ? AND name <> ?');
        $q->execute([$day, $name]);
        reply(200, "ok place $place of " . ((int)$q->fetchColumn() + 1) . " (being checked)");
    }
    [$place, $of, $kept] = ofp_daily_store($day, $name, $ticks, 0, $text, $ip);   /* (no checker here: unchecked) */
    if ($kept) reply(200, 'ok kept (your earlier win was faster)');
    reply(200, "ok place $place of $of");
}

if ($action === 'map_upload') {                          // a player shares a map
    $text = str_replace("\r", '', (string)($_POST['map'] ?? ''));
    if (strlen($text) > 200000 || str_contains($text, "\0") || strncmp($text, 'OFPMAP 1', 8) !== 0 || strpos($text, "\ntiles\n") === false)
        reply(400, 'that isn\'t a map file (.ofpmap) this game can read');
    $q = $db->prepare('SELECT COUNT(*) FROM gallery WHERE ip = ? AND uploaded > ?');
    $q->execute([$ip, time() - 86400]);
    if ((int)$q->fetchColumn() >= 10) reply(429, 'ten maps a day from one address');
    $details = ofp_map_details($text);
    $name = $details['name'] ?: 'Untitled';
    if (preg_match_all('/^struct=/m', $text) > 640) reply(400, 'more buildings than a map can hold (640)');
    if (substr_count($text, "\n") > 2000) reply(400, 'too many lines for a map');
    $world = in_array($details['biome'], ['classic', 'desert', 'jungle', 'winter', 'scifi', 'volcanic', 'wasteland', 'moon', 'swamp'], true)
             ? $details['biome'] : 'classic';
    $tool = (string)(ofp_settings()['site']['ofp_match'] ?? '');
    if (!is_executable($tool) || !function_exists('exec')) reply(503, 'map validation is unavailable');
    $tmp = tempnam(sys_get_temp_dir(), 'ofpmap');
    if ($tmp === false) reply(503, 'map validation is unavailable');
    $output = [];
    $status = -1;
    try {
        if (file_put_contents($tmp, $text) === strlen($text))
            exec(escapeshellarg($tool) . ' check-map ' . escapeshellarg($tmp) . ' 2>&1', $output, $status);
    } finally {
        unlink($tmp);
    }
    if ($status === 2 && $output === ['bad map']) reply(400, 'the game cannot start a match on this map');
    if ($status !== 0 || $output !== ['map ok']) reply(503, 'map validation is unavailable');
    $hash = sha1($text);
    $q = $db->prepare('SELECT id FROM gallery WHERE hash = ?');
    $q->execute([$hash]);
    if ($id = $q->fetchColumn()) reply(200, "ok map $id (it was here already)");
    $who = $details['author'] ?: ofp_clean((string)($_POST['author'] ?? ''), 15);
    $db->prepare('INSERT INTO gallery(name, author, world, time, hash, text, size, uploaded, ip) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)')
       ->execute([$name, substr($who, 0, 15), $world, $details['time'] ?: 'day',
                  $hash, $text, strlen($text), time(), $ip]);
    $mid = $db->lastInsertId();
    $site = ofp_settings()['site'] ?? [];
    ofp_webhook('map', ($who !== '' ? $who : 'Someone') . " shared a map: $name ($world). " . ($site['base_url'] ?? '') . '/maps/');
    reply(200, 'ok map ' . $mid);
}

$port = (int)($_POST['port'] ?? 0);
if ($port < 1 || $port > 65535) reply(400, 'bad port');
$host = announced_host($ip, $cfg);

if ($action === 'live') {                               // a listed server's match so far (or: it's over)
    $q = $db->prepare('SELECT 1 FROM servers WHERE host = ? AND port = ?');
    $q->execute([$host, $port]);
    if (!$q->fetchColumn()) reply(403, 'only listed servers (announce first)');
    if (!empty($_POST['over'])) {
        $db->prepare('DELETE FROM live WHERE host = ? AND port = ?')->execute([$host, $port]);
        reply(200, 'ok over');
    }
    $text = str_replace("\r", '', (string)($_POST['replay'] ?? ''));
    if (preg_match('/\AOFPREPLAY [12]\r?\n/', $text) !== 1 || strlen($text) > 3000000) reply(400, 'not a replay');
    $db->prepare('DELETE FROM live WHERE host = ? AND port = ?')->execute([$host, $port]);
    $db->prepare('INSERT INTO live(host, port, replay, updated) VALUES(?, ?, ?, ?)')->execute([$host, $port, $text, time()]);
    reply(200, 'ok live');
}

if ($action === 'match') {                              // a listed server's finished match
    $q = $db->prepare('SELECT name FROM servers WHERE host = ? AND port = ?');
    $q->execute([$host, $port]);
    $server_name = $q->fetchColumn();
    if ($server_name === false) reply(403, 'only listed servers send matches (announce first)');
    $players = [];
    foreach (explode("\n", (string)($_POST['players'] ?? '')) as $line) {
        $f = explode("\t", trim($line, "\r"));
        if (count($f) < 8 || count($players) >= 8) continue;   // (up to 8 in a match: protocol 19 on)
        $players[] = ['name' => ofp_clean($f[0], 15), 'side' => $f[1] === 'fortress' ? 'fortress' : 'camp', 'won' => (int)!!$f[2],
                      'kills' => max(0, (int)$f[3]), 'score' => (int)$f[4], 'rating' => max(0, min(4000, (int)$f[5])),
                      'change' => max(-100, min(100, (int)$f[6])), 'bot' => (int)$f[7],
                      'tank' => isset($f[8]) ? max(0, min(2, (int)$f[8])) : null,          // (0.4: the tank,
                      'ach' => isset($f[9]) ? (int)$f[9] & 0xFFFF : 0];                    //  achievements just earned)
    }
    if (!$players) reply(400, 'no players');
    $replay = $_POST['replay'] ?? null;
    if ($replay !== null && (strlen($replay) > 3000000 || preg_match('/\AOFPREPLAY [12]\r?\n/', $replay) !== 1)) $replay = null;
    $before = ofp_ladder_leaders($host, $port);
    $id = ofp_store_match([
        'host' => $host, 'port' => $port, 'server_name' => $server_name, 'seconds' => max(0, min(86400, (int)($_POST['seconds'] ?? 0))),
        'mode' => ($_POST['mode'] ?? '') === 'versus' ? 'versus' : 'together', 'world' => ofp_clean($_POST['world'] ?? '', 15),
        'map' => ofp_clean($_POST['map'] ?? '', 31), 'winner' => in_array($_POST['winner'] ?? '', ['camp', 'fortress', 'draw'], true) ? $_POST['winner'] : 'none',
        'goal' => isset(OFP_GOALS[$_POST['goal'] ?? '']) ? $_POST['goal'] : 'flag',
        'players' => $players,
    ], $replay);
    foreach (ofp_ladder_leaders($host, $port) as $slug => [$lname, $leader])   // a new leader?
        if ($leader !== null && ($before[$slug][1] ?? null) !== $leader)
            ofp_webhook('ladder', "$leader now leads $lname. " . (ofp_settings()['site']['base_url'] ?? '') . "/activity/?view=ladders?l=$slug");
    reply(200, "ok match $id");
}

if ($action === 'remove') {
    $db->prepare('DELETE FROM servers WHERE host = ? AND port = ?')->execute([$host, $port]);
    reply(200, 'ok removed');
}
if ($action !== 'announce') reply(400, 'unknown action');

// what it says about itself; the probe's answer wins
$info = [
    'name' => ofp_clean($_POST['name'] ?? 'OpenFP server', 40), 'build' => (int)($_POST['build'] ?? 0),
    'players' => (int)($_POST['players'] ?? 0), 'max_players' => (int)($_POST['max_players'] ?? 0),
    'password' => (int)!empty($_POST['password']), 'running' => (int)!empty($_POST['running']),
    'world' => ofp_clean($_POST['world'] ?? '', 15), 'map' => ofp_clean($_POST['map'] ?? '', 31),
    'versus' => ($_POST['mode'] ?? '') === 'versus' ? 1 : 0, 'ping_ms' => null, 'match_players' => 0,
];
// how the server is seen from the internet (its STUN answer): only its own address, any port
$stun = '';
if (preg_match('/^([0-9a-fA-F:.]+) (\d{1,5})$/', (string)($_POST['stun'] ?? ''), $sm) && $sm[1] === $ip && (int)$sm[2] > 0)
    $stun = $sm[1] . ' ' . (int)$sm[2];
if ($cfg['probe']) {
    $probe = ofp_probe($host, $port, (float)$cfg['probe_timeout']);
    if (!$probe && !(!empty($_POST['hidden']) && $stun !== ''))   // (behind a router, by join code: the punch will do)
        reply(409, "no answer from $host:$port (UDP); is the port open to the internet?");
    if ($probe) $info = array_merge($info, $probe);
}
$now = time();
$q = $db->prepare('SELECT 1 FROM servers WHERE host = ? AND port = ?');   // (new on the list?)
$q->execute([$host, $port]);
$is_new = !$q->fetchColumn();
// only the server knows these; the probe can't check them, so they're kept modest
$matches = max(0, min(1000000, (int)($_POST['matches'] ?? 0)));
$uptime = max(0, min(10 * 365 * 86400, (int)($_POST['uptime'] ?? 0)));
ofp_store_server([
    'host' => $host, 'port' => $port, 'name' => $info['name'], 'build' => $info['build'], 'players' => $info['players'],
    'max_players' => $info['max_players'], 'password' => $info['password'], 'running' => $info['running'],
    'match_players' => $info['match_players'], 'mode' => $info['versus'] ? 'versus' : 'together',
    'world' => $info['world'], 'map' => $info['map'], 'motd' => ofp_clean($_POST['motd'] ?? '', 200),
    'ping_ms' => $info['ping_ms'], 'matches' => $matches, 'started' => $now - $uptime,
    'first_seen' => $now, 'last_seen' => $now,
    // how matches go there (what the server says; nothing to check them against)
    'time_limit' => max(60, min(1800, (int)($_POST['time_limit'] ?? 1200) ?: 1200)),
    'time_rule' => ($_POST['time_rule'] ?? '') === 'draw' ? 'draw' : 'points',
    'bots' => preg_match('/^(fill|[0-7])$/', $_POST['bots'] ?? '') ? $_POST['bots'] : '0',
    'watching' => preg_match('/^(off|\d{1,3})$/', $_POST['watching'] ?? '') ? $_POST['watching'] : '0',
    'maps' => max(0, min(999, (int)($_POST['maps'] ?? 0))),
    'rotation' => in_array($_POST['rotation'] ?? '', ['order', 'random', 'vote'], true) ? $_POST['rotation'] : 'order',
    'goal' => in_array($_POST['goal'] ?? '', ['flag', 'hill', 'conquest', 'deathmatch', 'annihilation', 'warlord', 'killconfirmed', 'hardpoint'], true) ? $_POST['goal'] : 'flag',
    'version' => preg_match('/^[0-9][0-9.]{0,14}$/', $_POST['version'] ?? '') ? $_POST['version'] : null,
    // the match that's on (servers send these while it is): how far in, each side's score
    'elapsed' => isset($_POST['elapsed']) && ctype_digit((string)$_POST['elapsed']) ? min(86400, (int)$_POST['elapsed']) : null,
    'score_camp' => isset($_POST['score_camp']) && is_numeric($_POST['score_camp']) ? (int)$_POST['score_camp'] : null,
    'score_fortress' => isset($_POST['score_fortress']) && is_numeric($_POST['score_fortress']) ? (int)$_POST['score_fortress'] : null,
    'people' => implode(',', array_slice(array_filter(array_map(fn($n) => ofp_clean($n, 15),
                    explode(',', (string)($_POST['people'] ?? '')))), 0, 16)),
    'hidden' => (int)!empty($_POST['hidden']),           // (a join code only: not on the list)
    'stun' => $stun !== '' ? $stun : null,
]);
if ($is_new && empty($_POST['hidden'])) ofp_webhook('server_new', "A new server on the list: {$info['name']} ($host:$port). "
                         . (ofp_settings()['site']['base_url'] ?? '') . '/server/');
// its leaderboard, for the web site
if (isset($_POST['leaders'])) ofp_store_leaders($host, $port, $info['name'], (string)$_POST['leaders']);
// a sample for the charts, every few minutes
$q = $db->prepare('SELECT MAX(at) FROM samples WHERE host = ? AND port = ?');
$q->execute([$host, $port]);
if ($now - (int)$q->fetchColumn() >= $cfg['sample_every'])
    $db->prepare('INSERT INTO samples(host, port, at, players, running) VALUES(?, ?, ?, ?, ?)')
       ->execute([$host, $port, $now, $info['players'], $info['running']]);
reply(200, (empty($_POST['hidden']) ? "ok listed as" : "ok known as") . " $host:$port code " . ofp_join_code($host, $port));
