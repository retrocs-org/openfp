<?php
// SPDX-License-Identifier: MIT
// One player: their rating over time, recent matches, favourite tank, how they do in each
// world, and their achievements. ?name=Anna, and &server=HOST:PORT for one server's view.
require __DIR__ . '/../inc/site.php';

$name = ofp_clean((string)($_GET['name'] ?? ''), 15);
$host = $port = null;
if (preg_match('/^\[?([A-Za-z0-9.\-:]+?)\]?:(\d{1,5})$/', $_GET['server'] ?? '', $m)) { $host = $m[1]; $port = (int)$m[2]; }
$p = $name !== '' ? site_safe(fn() => ofp_player($name, $host, $port), null) : null;
page_start($name !== '' ? $name : 'Player', 'leaders', 'An OpenFP player: rating, matches and achievements.');

// a rating line (an SVG), 300 x 60
function rating_line(array $h): string
{
    if (count($h) < 2) return '';
    $lo = min($h) - 10; $hi = max($h) + 10; $n = count($h); $pts = [];
    foreach ($h as $i => $v) $pts[] = round($i * 300 / ($n - 1), 1) . ',' . round(56 - ($v - $lo) * 52 / max(1, $hi - $lo), 1);
    return '<svg class="rating-line" viewBox="0 0 300 60" preserveAspectRatio="none" aria-label="Rating over time">'
         . '<polyline fill="none" stroke="currentColor" stroke-width="2" points="' . implode(' ', $pts) . '"/></svg>'
         . '<div class="muted small">' . (int)min($h) . ' to ' . (int)max($h) . ' over ' . $n . ' matches</div>';
}
?>
<section class="wrap">
<?php if ($name === ''): ?>
  <h1>Player</h1><p class="notice">Which player? Pick one from the <a href="/statistics/?view=leaders">leaderboard</a>.</p>
<?php elseif ($p === null): ?>
  <h1><?= h($name) ?></h1><p class="notice">The match history isn't available right now.</p>
<?php elseif (!$p['matches']): ?>
  <h1><?= h($name) ?></h1><p class="notice">No matches for <?= h($name) ?><?= $host ? ' on this server' : '' ?> yet.</p>
<?php else: ?>
  <h1><?= h($p['name']) ?></h1>
  <p class="lead">
    <?php if ($host): ?>On <a href="/statistics/?view=matches&amp;server=<?= h(urlencode("$host:$port")) ?>"><?= h(reset($p['servers'])) ?></a>.
    <a href="?name=<?= h(urlencode($p['name'])) ?>">All servers &rarr;</a>
    <?php else: ?>On <?= count($p['servers']) ?> server<?= count($p['servers']) === 1 ? '' : 's' ?>:
      <?php $i = 0; foreach ($p['servers'] as $k => $sn): ?><?= $i++ ? ', ' : '' ?><a href="?name=<?= h(urlencode($p['name'])) ?>&amp;server=<?= h(urlencode($k)) ?>"><?= h($sn) ?></a><?php endforeach; ?>.
      <?php if (count($p['servers']) > 1): ?><span class="muted small">Each server rates its players itself: pick one for its rating.</span><?php endif; ?>
    <?php endif; ?>
  </p>
  <div class="stats-row">
    <div><b><?= $p['rating'] !== null && ($host || count($p['servers']) === 1) ? (int)$p['rating'] : '-' ?></b><span>rating</span></div>
    <div><b><?= $p['matches'] ?></b><span>matches</span></div>
    <div><b><?= $p['wins'] ?></b><span>won (<?= round($p['wins'] * 100 / max(1, $p['matches'])) ?>%)</span></div>
    <div><b><?= $p['kills'] ?></b><span>tanks destroyed</span></div>
    <?php $fav = array_sum($p['tanks']) ? OFP_TANKS[array_keys($p['tanks'], max($p['tanks']))[0]] : null; ?>
    <div><b><?= h($fav ?? '-') ?></b><span>favourite tank</span></div>
  </div>
  <?php if ($host || count($p['servers']) === 1): ?><?= rating_line($p['history']) ?><?php endif; ?>

  <h2>Achievements <span class="muted small"><?= substr_count(decbin($p['ach']), '1') ?> of <?= count(OFP_ACHIEVEMENTS) ?></span></h2>
  <div class="badges">
    <?php foreach (OFP_ACHIEVEMENTS as $i => [$an, $at]): $got = ($p['ach'] >> $i) & 1; ?>
      <span class="badge<?= $got ? ' got' : '' ?>" title="<?= h($at) ?>"><?= h($an) ?></span>
    <?php endforeach; ?>
  </div>

  <h2>Worlds</h2>
  <table class="board"><thead><tr><th>World</th><th class="n">Played</th><th class="n">Won</th><th class="n">Win rate</th></tr></thead><tbody>
  <?php foreach ($p['worlds'] as $w => $r): ?>
    <tr><td><?= h(['scifi' => 'Moon Base', 'moon' => 'Moon Base', 'volcanic' => 'Volcano'][$w] ?? ucfirst($w)) ?></td><td class="n"><?= $r['played'] ?></td><td class="n"><?= $r['won'] ?></td>
        <td class="n"><?= round($r['won'] * 100 / max(1, $r['played'])) ?>%</td></tr>
  <?php endforeach; ?>
  </tbody></table>

  <h2>Recent matches</h2>
  <table class="board"><thead><tr><th>When</th><th>Map</th><th>Server</th><th></th><th class="n">Tanks</th><th class="n">Rating</th><th></th></tr></thead><tbody>
  <?php foreach ($p['recent'] as $r): ?>
    <tr><td class="muted"><?= h(ago($r['ended'])) ?></td><td><?= h($r['map'] === 'random' ? 'Random' : $r['map']) ?></td>
        <td><?= h($r['server']) ?></td><td><?= $r['won'] ? '<span class="tag on">Won</span>' : '' ?></td>
        <td class="n"><?= $r['kills'] ?></td>
        <td class="n"><span class="<?= $r['change'] >= 0 ? 'up' : 'down' ?>"><?= $r['change'] >= 0 ? '+' : '' ?><?= $r['change'] ?></span></td>
        <td><?= $r['replay'] ? '<a class="small" href="/replay/?id=' . $r['id'] . '">Watch</a>' : '' ?></td></tr>
  <?php endforeach; ?>
  </tbody></table>
<?php endif; ?>
</section>
<?php page_end();
