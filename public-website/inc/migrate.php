<?php
// SPDX-License-Identifier: MIT
// Bring the listing's database up to date, after unpacking a new version:
//     php inc/migrate.php        where it is, and what's to do
//     php inc/migrate.php up     do it
// It uses 'db_admin' from config.inc.php if there is one (a user that may create and
// alter tables), so the web site's own user can keep just SELECT, INSERT, UPDATE, DELETE.
if (PHP_SAPI !== 'cli') { http_response_code(404); exit; }
require __DIR__ . '/listing.php';
require_once __DIR__ . '/migrations.php';

$cfg = ofp_settings();
$c = $cfg['db'];
if (!empty($cfg['db_admin'])) $c = array_merge($c, $cfg['db_admin']);
$opt = [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION, PDO::ATTR_TIMEOUT => 10];
if (!empty($c['ssl_ca'])) { $opt[PDO::MYSQL_ATTR_SSL_CA] = $c['ssl_ca']; $opt[PDO::MYSQL_ATTR_SSL_VERIFY_SERVER_CERT] = true; }
try {
    $db = new PDO($c['dsn'], $c['user'] ?? null, $c['password'] ?? null, $opt);
} catch (Throwable $e) {
    fwrite(STDERR, "can't reach the database: " . $e->getMessage() . "\n");
    exit(1);
}
$now = ofp_schema_version($db);
$latest = ofp_schema_latest();
echo "the database is at version $now; this site needs $latest\n";
$up = ($argv[1] ?? '') === 'up';
try {
    $steps = ofp_migrate($db, !$up);
} catch (Throwable $e) {
    fwrite(STDERR, "failed: " . $e->getMessage() . "\n(does the user have CREATE and ALTER? set 'db_admin' in config.inc.php)\n");
    exit(1);
}
foreach ($steps as $s) echo "  $s\n";
if (!$steps) echo "up to date\n";
elseif (!$up) echo "run: php inc/migrate.php up\n";
else echo "done: version $latest\n";
