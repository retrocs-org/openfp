<?php
// SPDX-License-Identifier: MIT
// The daily challenge's checker: plays back the wins waiting in the queue with ofp_match,
// and puts those that check out on the board (if they beat the player's earlier one).
// Every minute, e.g. from cron as the web server's user:
//     * * * * *  wwwrun  php /srv/www/vhosts/openfp.retrocs.org/inc/daily_worker.php
// or a systemd timer. Quiet unless there was something to do.
if (PHP_SAPI !== 'cli') { http_response_code(404); exit; }
require __DIR__ . '/listing.php';
require_once __DIR__ . '/daily.php';
if (!ofp_daily_checker()) { fwrite(STDERR, "no ofp_match set ('site' => ['ofp_match' => ...] in config.inc.php)\n"); exit(1); }
$db = ofp_db();
$done = 0;
foreach ($db->query('SELECT id, day, name, ticks, replay, ip FROM daily_queue ORDER BY id LIMIT 50')->fetchAll() as $w) {
    $ok = ofp_daily_verify((string)$w['replay']);
    if ($ok) {
        [$place, $of, $kept] = ofp_daily_store((int)$w['day'], $w['name'], (int)$w['ticks'], 1, (string)$w['replay'], (string)$w['ip']);
        echo "{$w['day']} {$w['name']}: checked, " . ($kept ? 'kept the faster earlier one' : "place $place of $of") . "\n";
    } else {
        echo "{$w['day']} {$w['name']}: doesn't play back as a win: left out\n";
    }
    $db->prepare('DELETE FROM daily_queue WHERE id = ?')->execute([$w['id']]);
    $done++;
}
exit(0);
