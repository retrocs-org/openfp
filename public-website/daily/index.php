<?php
// SPDX-License-Identifier: MIT
// The daily challenge: today's battlefield, the same for everyone; the fastest wins.
require __DIR__ . '/../inc/site.php';
require_once __DIR__ . '/../inc/daily.php';

$day = isset($_GET['day']) && preg_match('/^\d{8}$/', $_GET['day']) ? (int)$_GET['day'] : ofp_daily_today();
$setup = ofp_daily_setup($day);
$board = site_safe(fn() => ofp_daily_board($day), null);
$week = site_safe(fn() => ofp_daily_week($day), null);
$worlds = ['Classic', 'Desert', 'Jungle', 'Winter', 'Moon Base', 'Volcano', 'Wasteland', 'Moon Base', 'Swamp'];
$times = ['day', 'dusk', 'night'];
$when = DateTime::createFromFormat('Ymd', (string)$day, new DateTimeZone('UTC'));
page_start('Daily challenge', 'daily', 'Today\'s battlefield in OpenFP: the same for everyone, and the fastest win.');
function ticks_time(int $t): string { $s = intdiv($t, 50); return sprintf('%d:%02d.%d', intdiv($s, 60), $s % 60, intdiv($t % 50, 5)); }
?>
<section class="wrap">
  <h1>Daily challenge</h1>
  <p class="lead"><?= h($when ? $when->format('l j F Y') : (string)$day) ?>:
    <?= h($worlds[$setup['biome']]) ?>, by <?= h($times[$setup['time']]) ?><?= $setup['weather'] ? ', in a storm' : '' ?><?= $setup['extras'] ? ', with pickups' : '' ?>.
    The same battlefield for everyone, alone against the computer, in the Bison. Bring their flag home as fast as you can:
    in the game, TRAINING on the first page, left or right: DAILY CHALLENGE. A new one every day at midnight (UTC).</p>
  <p class="small">
    <a href="?day=<?= h(gmdate('Ymd', ($when ? $when->getTimestamp() : time()) - 86400)) ?>">&larr; the day before</a>
    <?php if ($day < ofp_daily_today()): ?> · <a href="?day=<?= h(gmdate('Ymd', $when->getTimestamp() + 86400)) ?>">the day after &rarr;</a><?php endif; ?>
  </p>
  <?php if ($board === null): ?>
    <p class="notice">The results aren't available right now.</p>
  <?php elseif (!$board): ?>
    <p class="notice">Nobody has won this one yet.</p>
  <?php else: ?>
    <table class="board"><thead><tr><th class="n">#</th><th>Player</th><th class="n">Time</th><th></th><th></th></tr></thead><tbody>
    <?php foreach ($board as $i => $r): ?>
      <tr><td class="n"><?= $i + 1 ?></td><td><b><a href="/player/?name=<?= h(urlencode($r['name'])) ?>"><?= h($r['name']) ?></a></b></td>
          <td class="n"><?= h(ticks_time((int)$r['ticks'])) ?></td>
          <td><?= $r['verified'] ? '<span class="tag on" title="played back here: a real win">checked</span>' : '<span class="tag" title="not played back on this site">unchecked</span>' ?>
              <?= !empty($r['flag']) ? '<span class="tag warn" title="much faster than the rest: it scores no league points">' . h($r['flag']) . '</span>' : '' ?></td>
          <td><a class="small" href="/replay/?daily=<?= (int)$r['id'] ?>">Watch</a></td></tr>
    <?php endforeach; ?>
    </tbody></table>
  <?php endif; ?>
  <?php if ($week && $week['table']): ?>
    <h2>This week's league <span class="muted small"><?= h(substr((string)$week['monday'], 6, 2) . '/' . substr((string)$week['monday'], 4, 2)) ?>
      to <?= h(substr((string)$week['sunday'], 6, 2) . '/' . substr((string)$week['sunday'], 4, 2)) ?></span></h2>
    <p class="muted small">Points a day by place: 10, 8, 6, 5, 4, 3, 2, 1.</p>
    <table class="board"><thead><tr><th class="n">#</th><th>Player</th><th class="n">Points</th><th class="n">Days won</th><th class="n">Days played</th></tr></thead><tbody>
    <?php foreach ($week['table'] as $i => $e): ?>
      <tr><td class="n"><?= $i + 1 ?></td><td><b><?= h($e['name']) ?></b></td><td class="n"><?= (int)$e['points'] ?></td>
          <td class="n"><?= (int)$e['wins'] ?></td><td class="n"><?= (int)$e['days'] ?></td></tr>
    <?php endforeach; ?>
    </tbody></table>
  <?php endif; ?>
</section>
<?php page_end();
