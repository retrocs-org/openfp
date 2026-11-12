<?php
// SPDX-License-Identifier: MIT
// Forget a player, from the command line:  php inc/forget.php NAME [HOST:PORT]
if (PHP_SAPI !== 'cli') { http_response_code(404); exit; }
require __DIR__ . '/listing.php';
$name = $argv[1] ?? '';
if ($name === '') { fwrite(STDERR, "usage: php inc/forget.php NAME [HOST:PORT]\n"); exit(1); }
[$h, $p] = preg_match('/^([A-Za-z0-9.\-]+):(\d{1,5})$/', $argv[2] ?? '', $m) ? [$m[1], (int)$m[2]] : [null, null];
$n = ofp_forget($name, $h, $p);
printf("forgot %s%s: %d leaderboard places, %d matches, %d maps\n", $name, $h ? " on $h:$p" : '', $n['leaders'], $n['matches'], $n['maps']);
