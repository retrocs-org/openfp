<?php
// SPDX-License-Identifier: MIT
// The daily challenge, as the game has it (src/common/daily.c): the day's match follows
// from the date, so the site can check that a replay it's sent is that day's.

function ofp_daily_today(): int { return (int)gmdate('Ymd'); }

function ofp_daily_setup(int $day): array
{
    $seed = (($day * 2654435761) & 0xFFFFFFFF) ^ 0x000FDA11;
    return ['seed' => $seed, 'biome' => ($seed >> 3) % 9, 'time' => ($seed >> 7) % 3,
            'weather' => (($seed >> 11) % 4) === 0 ? 1 : 0, 'extras' => (($seed >> 13) % 3) === 0 ? 1 : 0,
            'garages' => 4, 'versus' => 0, 'ai' => 2, 'players' => 1, 'cls' => 1];
}

// is this replay that day's challenge? [ok, ticks, why not]
function ofp_daily_check(string $text, int $day): array
{
    $want = ofp_daily_setup($day);
    if (strncmp($text, 'OFPREPLAY 1', 11) !== 0) return [false, 0, 'not a replay'];
    if (!preg_match('/^seed=(\d+) biome=(\d+) time=(\d+) garages=(\d+) versus=(\d+) ai=(\d+) players=(\d+) tanks=\d+/m', $text, $m))
        return [false, 0, 'no match in it'];
    foreach (['seed', 'biome', 'time', 'garages', 'versus', 'ai', 'players'] as $i => $k)
        if ((int)$m[$i + 1] !== $want[$k]) return [false, 0, "not that day's battlefield ($k)"];
    if (!preg_match('/^player=0 cls=(\d+) team=0 steer=\d+ bot=0/m', $text, $p) || (int)$p[1] !== $want['cls'])
        return [false, 0, 'not that day\'s tank'];
    $weather = preg_match('/^weather=(\d+)/m', $text, $w) ? (int)$w[1] : 0;
    $extras = preg_match('/^extras=(\d+)/m', $text, $x) ? (int)$x[1] : 0;
    if ($weather !== $want['weather'] || $extras !== $want['extras']) return [false, 0, "not that day's weather or extras"];
    if (strpos($text, "\nmap=") !== false) return [false, 0, 'a map file: the challenge is a battlefield of its own'];
    $ticks = 0;
    $at = strpos($text, "\nticks\n");
    if ($at === false) return [false, 0, 'no ticks'];
    foreach (explode("\n", substr($text, $at + 7)) as $line) {
        if (preg_match('/^(\d+) [0-9a-f]*$/', $line, $r)) $ticks += (int)$r[1];
        elseif ($line === 'end') break;
    }
    return $ticks > 0 && $ticks < 50 * 3600 ? [true, $ticks, ''] : [false, 0, 'no ticks'];
}

// with ofp_match on this machine ('site' => ['ofp_match' => path]): play it back, and only a
// win that checks out counts. null: no ofp_match here (the result is shown unverified)
function ofp_daily_verify(string $text): ?bool
{
    $tool = (string)(ofp_settings()['site']['ofp_match'] ?? '');
    if ($tool === '' || !is_executable($tool)) return null;
    $tmp = tempnam(sys_get_temp_dir(), 'ofpdaily');
    file_put_contents($tmp, $text);
    $out = (string)shell_exec(escapeshellarg($tool) . ' verify ' . escapeshellarg($tmp) . ' 2>&1');
    unlink($tmp);
    return (bool)preg_match('/all \d+ checksums match, the camp won/', $out);
}

function ofp_daily_board(int $day, int $limit = 50): array
{
    $q = ofp_db()->prepare('SELECT id, name, ticks, verified, flag, at FROM daily WHERE day = ? ORDER BY ticks, at LIMIT ' . (int)$limit);
    $q->execute([$day]);
    return $q->fetchAll();
}

// is the checker here? (then wins wait in the queue for daily_worker.php)
function ofp_daily_checker(): bool
{
    $tool = (string)(ofp_settings()['site']['ofp_match'] ?? '');
    return $tool !== '' && is_executable($tool);
}

// a win onto the board, if it beats the player's earlier one; returns [place, of, kept_older]
function ofp_daily_store(int $day, string $name, int $ticks, int $verified, string $replay, string $ip): array
{
    $db = ofp_db();
    $q = $db->prepare('SELECT ticks FROM daily WHERE day = ? AND name = ?');
    $q->execute([$day, $name]);
    $before = $q->fetchColumn();
    $kept = $before !== false && (int)$before <= $ticks;
    if (!$kept) {
        // unusual: under 30 seconds, or under half the best so far once there are three
        $q = $db->prepare('SELECT MIN(ticks), COUNT(*) FROM daily WHERE day = ? AND name <> ?');
        $q->execute([$day, $name]);
        [$best, $n] = $q->fetch(PDO::FETCH_NUM);
        $flag = $ticks < 30 * 50 ? 'unusually fast' : ((int)$n >= 3 && $ticks * 2 < (int)$best ? 'unusually fast' : null);
        $q = $db->prepare('SELECT name FROM daily WHERE day = ? ORDER BY ticks, at LIMIT 1');
        $q->execute([$day]);
        $leader = $q->fetchColumn();
        $db->prepare('DELETE FROM daily WHERE day = ? AND name = ?')->execute([$day, $name]);
        $db->prepare('INSERT INTO daily(day, name, ticks, verified, replay, at, ip, flag) VALUES(?, ?, ?, ?, ?, ?, ?, ?)')
           ->execute([$day, $name, $ticks, $verified, $replay, time(), $ip, $flag]);
        $q = $db->prepare('SELECT name FROM daily WHERE day = ? ORDER BY ticks, at LIMIT 1');
        $q->execute([$day]);
        if ($q->fetchColumn() === $name && $leader !== $name && $flag === null) {   /* a new leader: tell the chat */
            $s = intdiv($ticks, 50);
            ofp_webhook('daily', sprintf('%s leads today\'s challenge: %d:%02d. %s/statistics/?view=daily', $name, intdiv($s, 60), $s % 60,
                                         ofp_settings()['site']['base_url'] ?? ''));
        }
    }
    $mine = $kept ? (int)$before : $ticks;
    $q = $db->prepare('SELECT COUNT(*) FROM daily WHERE day = ? AND ticks < ?');
    $q->execute([$day, $mine]);
    $place = (int)$q->fetchColumn() + 1;
    $q = $db->prepare('SELECT COUNT(*) FROM daily WHERE day = ?');
    $q->execute([$day]);
    return [$place, (int)$q->fetchColumn(), $kept];
}

// the week's league: points a day by place (10, 8, 6, 5, 4, 3, 2, 1), Monday to Sunday
function ofp_daily_week(int $day): array
{
    $points = [10, 8, 6, 5, 4, 3, 2, 1];
    $t = DateTime::createFromFormat('Ymd', (string)$day, new DateTimeZone('UTC'));
    $monday = (clone $t)->modify('monday this week');
    $days = [];
    for ($i = 0; $i < 7; $i++) $days[] = (int)(clone $monday)->modify("+$i day")->format('Ymd');
    $table = [];
    foreach ($days as $d) {
        $place = -1;
        foreach (ofp_daily_board($d, 30) as $r) {
            if ($r['flag']) continue;                    /* (unusual times score nothing, and take no place) */
            if (++$place >= count($points)) break;
            $e = &$table[$r['name']];
            if (!$e) $e = ['name' => $r['name'], 'points' => 0, 'days' => 0, 'wins' => 0];
            $e['points'] += $points[$place] ?? 0;
            $e['days']++;
            if ($place === 0) $e['wins']++;
            unset($e);
        }
    }
    usort($table, fn($a, $b) => [$b['points'], $b['wins']] <=> [$a['points'], $a['wins']]);
    return ['monday' => $days[0], 'sunday' => $days[6], 'table' => array_values($table)];
}
