<?php
// SPDX-License-Identifier: MIT
// The public web site: settings, the page frame, and the server list.

require_once __DIR__ . '/releases.php';
require_once __DIR__ . '/listing.php';                 // the listing: settings, database, servers

// the site's part of public-website/config.inc.php
function site(): array
{
    static $cfg = null;
    if ($cfg !== null) return $cfg;
    $cfg = array_merge(['name' => 'OpenFP', 'tagline' => 'Classic tank combat, enhanced worlds and cross-platform battles',
                        'base_url' => 'https://openfp.retrocs.org', 'game_api' => 'https://openfp.retrocs.org/api/', 'repo_url' => 'https://github.com/retrocs-org/openfp', 'contact' => ''],
                       ofp_settings()['site'] ?? []);
    // Older installations may explicitly set these to empty strings.
    if (!trim((string)$cfg['repo_url'])) $cfg['repo_url'] = 'https://github.com/retrocs-org/openfp';
    if (!trim((string)$cfg['base_url'])) $cfg['base_url'] = 'https://openfp.retrocs.org';
    if (in_array($cfg['name'], ['OpenFirePower', 'Open FirePower', 'Fire Power'], true)) $cfg['name'] = 'OpenFP';
    if (str_contains($cfg['tagline'], 'Fire Power')) $cfg['tagline'] = 'Classic tank combat, enhanced worlds and cross-platform battles';
    $cfg['site_name'] = $cfg['name'];
    return $cfg;
}

function h($s): string { return htmlspecialchars((string)$s, ENT_QUOTES, 'UTF-8'); }

function site_release_version(): string
{
    // Source/build examples keep the explicit project version, independent of online releases.
    return '0.1';
}

// the servers right now, straight from the listing's database
function site_servers(): array
{
    try {
        $servers = ofp_list();
        return ['servers' => $servers, 'totals' => ofp_totals($servers), 'ok' => true];
    } catch (Throwable $e) {
        error_log('openfirepower site: ' . $e->getMessage());
    }
    return ['servers' => [], 'totals' => ['servers' => 0, 'players' => 0, 'playing' => 0, 'peak_players_24h' => 0, 'matches' => 0], 'ok' => false];
}

function page_start(string $title, string $page, string $description = ''): void
{
    $cfg = site();
    $full = $title === $cfg['site_name'] ? $title : "$title · {$cfg['site_name']}";
    $nav = ['index' => ['Home', '/'],
            'servers' => ['Servers', '/server/'], 'statistics' => ['Statistics', '/statistics/'],
            'maps' => ['Maps', '/maps/'], 'documentation' => ['Documentation', '/documentation/'],
            'source' => ['Source', '/source/']];
    ?><!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title><?= h($full) ?></title>
<meta name="description" content="<?= h($description ?: $cfg['tagline']) ?>">
<meta property="og:title" content="<?= h($full) ?>">
<meta property="og:description" content="<?= h($description ?: $cfg['tagline']) ?>">
<meta property="og:image" content="<?= h($cfg['base_url']) ?>/assets/img/20261111-desktop-classic.png">
<meta name="theme-color" content="#f5f2e9">
<link rel="icon" href="/assets/favicon.svg" type="image/svg+xml">
<link rel="stylesheet" href="/assets/site.css?v=20261112-10">
</head>
<body class="page-<?= h($page) ?>">
<a class="skip-link" href="#main">Skip to content</a>
<header class="top">
  <div class="wrap top-row">
    <a class="brand" href="/"><img src="/assets/favicon.svg" alt="" width="28" height="28"><span>Open<b>FP</b></span></a>
    <nav aria-label="Main navigation">
      <?php foreach ($nav as $key => [$label, $href]): ?>
        <a href="<?= h($href) ?>"<?= $key === $page ? ' aria-current="page"' : '' ?>><?= h($label) ?></a>
      <?php endforeach; ?>
    </nav>
    <a class="nav-download" href="/#get">Get the game <span aria-hidden="true">↗</span></a>
  </div>
</header>
<main id="main">
<?php
}

function page_end(): void
{
    $behind = site_safe(fn() => ofp_schema_behind(), null);
    if ($behind) {                                       // the database is behind the code: say so, quietly
        error_log("openfirepower site: the database is $behind version(s) behind: run php inc/migrate.php up");
        echo '<div class="wrap"><p class="notice small">The site\'s database is behind this version: '
           . 'some things are hidden until <code>php inc/migrate.php up</code> has been run.</p></div>';
    }
    $cfg = site();
    ?>
</main>
<footer class="bottom">
  <div class="wrap foot-row">
    <div>
      <a class="footer-brand" href="/">OpenFP<span aria-hidden="true">.</span></a>
      <p>Classic tank combat. New battlefields.<br>Free, open source, and made to be played together.</p>
      <small>MIT licensed. An independent tribute, made from scratch.</small>
    </div>
    <div class="foot-links">
      <a href="/credits/">Third Party Credits</a>
      <a href="/privacy/">Privacy</a>
      <a href="/server/">Servers</a>
      <a href="/#get">Downloads</a>
      <a href="/documentation/">Documentation</a>
      <a href="/api/docs/">API documentation</a>
      <?php if ($cfg['repo_url']): ?><a href="<?= h($cfg['repo_url']) ?>">GitHub</a><?php endif; ?>
      <?php if ($cfg['contact']): ?><a href="<?= h($cfg['contact']) ?>">Contact</a><?php endif; ?>
    </div>
  </div>
</footer>
<script src="/assets/site.js?v=20261112-7" defer></script>
</body>
</html>
<?php
}

// a few things every page might show, and that may not be there in an older database
function site_safe(callable $f, $fallback)
{
    try { return $f(); } catch (Throwable $e) { error_log('openfirepower site: ' . $e->getMessage()); return $fallback; }
}

function side_name(string $side): string { return $side === 'fortress' ? 'Fortress' : 'Camp'; }

function ago(?int $t): string
{
    if (!$t) return '-';
    $d = time() - $t;
    return $d < 60 ? 'just now' : ($d < 3600 ? (int)($d / 60) . ' min ago' : (int)($d / 3600) . ' h ago');
}

function duration(?int $s): string
{
    if ($s === null) return '-';
    if ($s < 3600) return max(1, (int)($s / 60)) . ' min';
    if ($s < 86400) return (int)($s / 3600) . ' h ' . (int)($s % 3600 / 60) . ' min';
    return (int)($s / 86400) . ' d ' . (int)($s % 86400 / 3600) . ' h';
}

// a little bar chart of the last day's players
function sparkline(array $hist, int $max): string
{
    $n = count($hist);
    if (!$n) return '';
    $max = max(1, $max, ...$hist);
    $w = 3; $gap = 1; $hgt = 22;
    $svg = '<svg class="spark" viewBox="0 0 ' . ($n * ($w + $gap)) . " $hgt\" preserveAspectRatio=\"none\" aria-hidden=\"true\">";
    foreach ($hist as $i => $v) {
        $bh = $v ? max(2, (int)round($v / $max * $hgt)) : 1;
        $svg .= '<rect x="' . ($i * ($w + $gap)) . '" y="' . ($hgt - $bh) . "\" width=\"$w\" height=\"$bh\"" . ($v ? '' : ' class="zero"') . '/>';
    }
    return $svg . '</svg>';
}
