<?php
// SPDX-License-Identifier: MIT
// CLI only. Preview by default. Preserve config.inc.php, schema and built-in maps.
if (PHP_SAPI !== 'cli') { http_response_code(404); exit; }
require __DIR__.'/listing.php';
require_once __DIR__.'/reset-plan.php';
try {
    $db=ofp_db();
    if ($argc!==1 && !($argc===3 && $argv[1]==='--confirm' && $argv[2]==='ERASE-ALL-USER-DATA')) {
        fwrite(STDERR,"usage: php inc/reset-data.php [--confirm ERASE-ALL-USER-DATA]\n"); exit(2);
    }
    foreach (ofp_reset_tables() as $table) echo $table,': ',$db->query("SELECT COUNT(*) FROM $table")->fetchColumn()," rows\n";
    if ($argc===1) { echo "Preview only. Stop game servers and background workers, back up the database, then run:\nphp inc/reset-data.php --confirm ERASE-ALL-USER-DATA\n"; exit; }
    ofp_reset_data($db);
    echo "Erased accounts, sessions, statistics, match history, replays, community maps and server-list data. Configuration, schema and built-in maps remain.\n";
} catch (Throwable $e) { fwrite(STDERR,'Reset failed: '.$e->getMessage()."\n"); exit(1); }
