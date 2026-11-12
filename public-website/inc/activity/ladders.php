<?php
// SPDX-License-Identifier: MIT
// Ladders: seasons on chosen servers, ranked from the match history.

$slug = preg_replace('/[^a-z0-9\-]/', '', strtolower($_GET['l'] ?? ''));
$ladder = $slug ? ofp_ladder($slug) : null;
$all = ofp_ladders();
if (!$ladder && count($all) === 1) $ladder = $all[0];   // just the one: show it
$table = $ladder ? site_safe(fn() => ofp_ladder_table($ladder), null) : null;
?>
<section class="wrap">
<?php if (!$ladder): ?>
  <h2>Ladders</h2>
  <p class="lead">Seasons on chosen servers: every match won there between the dates is three points.</p>
  <?php if (!$all): ?><p class="notice">No ladders at the moment.</p><?php endif; ?>
  <div class="matches">
    <?php foreach ($all as $l): $run = time() >= strtotime($l['from'] ?? '2000-01-01') && time() <= strtotime(($l['to'] ?? '2100-01-01') . ' 23:59:59'); ?>
      <article class="match">
        <header><h3><a href="?view=ladders&amp;l=<?= h($l['slug']) ?>"><?= h($l['name']) ?></a></h3>
          <div class="muted small"><?= h($l['from'] ?? '') ?> to <?= h($l['to'] ?? '') ?> · <?= $run ? '<span class="tag on">On now</span>' : 'Not running' ?></div></header>
        <?php if (!empty($l['about'])): ?><p><?= h($l['about']) ?></p><?php endif; ?>
      </article>
    <?php endforeach; ?>
  </div>
<?php else: ?>
  <h2><?= h($ladder['name']) ?></h2>
  <p class="lead"><?= h($ladder['about'] ?? '') ?></p>
  <p class="muted"><?= h($ladder['from'] ?? '') ?> to <?= h($ladder['to'] ?? '') ?>
     <?= $table && $table['running'] ? '· <span class="tag on">On now</span>' : '' ?>
     · on <?php foreach ($ladder['servers'] ?? [] as $i => $sv): ?><?= $i ? ', ' : '' ?><a href="/statistics/?view=matches&amp;server=<?= h(urlencode($sv)) ?>"><?= h($sv) ?></a><?php endforeach; ?>
     · a win is 3 points<?= !empty($ladder['min_matches']) ? ' · ranked after ' . (int)$ladder['min_matches'] . ' matches' : '' ?></p>
  <?php if ($table === null): ?>
    <p class="notice">The standings aren't available right now.</p>
  <?php else: ?>
    <?php if ($table['teams']): ?>
      <h2>Teams</h2>
      <table class="board">
        <thead><tr><th class="n">#</th><th>Team</th><th>Players</th><th class="n">Played</th><th class="n">Won</th><th class="n">Points</th></tr></thead>
        <tbody><?php foreach ($table['teams'] as $i => $t): ?>
          <tr><td class="n"><?= $i + 1 ?></td><td><b><?= h($t['name']) ?></b></td><td class="muted"><?= h(implode(', ', $t['members'])) ?></td>
              <td class="n"><?= (int)$t['played'] ?></td><td class="n"><?= (int)$t['won'] ?></td><td class="n"><b><?= (int)$t['points'] ?></b></td></tr>
        <?php endforeach; ?></tbody>
      </table>
      <h2>Players</h2>
    <?php endif; ?>
    <?php if (!$table['players'] && !$table['unranked']): ?>
      <p class="notice">No matches yet in this ladder<?= $table['running'] ? '. Go and play one!' : '.' ?></p>
    <?php else: ?>
      <table class="board">
        <thead><tr><th class="n">#</th><th>Player</th><th class="n">Played</th><th class="n">Won</th><th class="n">Lost</th><th class="n">Kills</th><th class="n">Rating</th><th class="n">Points</th></tr></thead>
        <tbody>
        <?php foreach ($table['players'] as $i => $p): ?>
          <tr><td class="n"><?= $i + 1 ?></td><td><b><a href="/player/?name=<?= h(urlencode($p['name'])) ?>"><?= h($p['name']) ?></a></b><?= $p['team'] ? ' <span class="muted small">' . h($p['team']) . '</span>' : '' ?></td>
              <td class="n"><?= $p['played'] ?></td><td class="n"><?= $p['won'] ?></td><td class="n"><?= $p['lost'] ?></td>
              <td class="n"><?= $p['kills'] ?></td>
              <td class="n"><span class="<?= $p['rating'] >= 0 ? 'up' : 'down' ?>"><?= $p['rating'] >= 0 ? '+' : '' ?><?= $p['rating'] ?></span></td>
              <td class="n"><b><?= $p['points'] ?></b></td></tr>
        <?php endforeach; ?>
        <?php foreach ($table['unranked'] as $p): ?>
          <tr class="muted"><td class="n">-</td><td><?= h($p['name']) ?> <span class="small">(<?= (int)$ladder['min_matches'] - $p['played'] ?> more to be ranked)</span></td>
              <td class="n"><?= $p['played'] ?></td><td class="n"><?= $p['won'] ?></td><td class="n"><?= $p['lost'] ?></td>
              <td class="n"><?= $p['kills'] ?></td><td class="n"><?= $p['rating'] >= 0 ? '+' : '' ?><?= $p['rating'] ?></td><td class="n"><?= $p['points'] ?></td></tr>
        <?php endforeach; ?>
        </tbody>
      </table>
      <p class="muted small"><?= (int)$table['matches'] ?> matches so far. Ties on points go to more wins, then the bigger rating gain.</p>
    <?php endif; ?>
  <?php endif; ?>
  <?php if (count($all) > 1): ?><p><a href="/statistics/?view=ladders">All ladders &rarr;</a></p><?php endif; ?>
<?php endif; ?>
</section>
