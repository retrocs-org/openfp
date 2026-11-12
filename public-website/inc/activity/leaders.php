<?php
// SPDX-License-Identifier: MIT
// The leaderboard: the best-rated players across the public servers.

$goal = isset(OFP_GOALS[$_GET['mode'] ?? '']) ? $_GET['mode'] : '';
$leaders = site_safe(fn() => $goal === '' ? ofp_leaders(100) : ofp_mode_leaders($goal, 100), null);
?>
<section class="wrap">
  <h2>Leaderboard<?= $goal !== '' ? ': ' . h(OFP_GOALS[$goal]) : '' ?></h2>
  <p class="tabs"><a href="/statistics/?view=leaders"<?= $goal === '' ? ' class="on"' : '' ?>>Overall</a>
  <?php foreach (OFP_GOALS as $k => $name): ?> <a href="/statistics/?view=leaders&amp;mode=<?= h($k) ?>"<?= $goal === $k ? ' class="on"' : '' ?>><?= h($name) ?></a><?php endforeach; ?></p>
  <p>The best-rated players across public servers. Ratings are tracked separately for each server.</p>
  <?php if ($goal !== '' && $leaders !== null): ?>
    <p class="lead">The last 30 days of <?= h(strtolower(OFP_GOALS[$goal])) ?> matches on the public servers that share them: the most wins first.</p>
    <?php if (!$leaders): ?><p class="notice">No <?= h(strtolower(OFP_GOALS[$goal])) ?> matches shared in the last 30 days.</p>
    <?php else: ?>
    <table class="board">
      <thead><tr><th class="n">#</th><th>Player</th><th class="n">Won</th><th class="n">Matches</th><th class="n">Share</th><th class="n">Tanks</th></tr></thead>
      <tbody>
      <?php foreach ($leaders as $i => $l): ?>
        <tr><td class="n"><?= $i + 1 ?></td><td><b><?= h($l['name']) ?></b></td><td class="n"><b><?= (int)$l['wins'] ?></b></td>
            <td class="n"><?= (int)$l['matches'] ?></td><td class="n"><?= (int)round(100 * $l['wins'] / max(1, $l['matches'])) ?>%</td>
            <td class="n"><?= (int)$l['kills'] ?></td></tr>
      <?php endforeach; ?>
      </tbody>
    </table>
    <?php endif; ?>
  <?php elseif ($leaders === null): ?>
    <p class="notice">The leaderboard isn't available right now.</p>
  <?php elseif (!$leaders): ?>
    <p class="notice">No rated matches yet. Every finished match on a public server counts.</p>
  <?php else: ?>
    <table class="board">
      <thead><tr><th class="n">#</th><th>Player</th><th>Server</th><th class="n">Rating</th><th class="n">Won</th><th class="n">Matches</th><th class="n">Score</th></tr></thead>
      <tbody>
      <?php foreach ($leaders as $i => $l): ?>
        <tr>
          <td class="n"><?= $i + 1 ?></td>
          <td><b><a href="/player/?name=<?= h(urlencode($l['name'])) ?>&amp;server=<?= h(urlencode($l['host'] . ':' . $l['port'])) ?>"><?= h($l['name']) ?></a></b></td>
          <td><a href="/statistics/?view=matches&amp;server=<?= h(urlencode($l['host'] . ':' . $l['port'])) ?>"><?= h($l['server_name']) ?></a></td>
          <td class="n"><b><?= (int)$l['rating'] ?></b></td>
          <td class="n"><?= (int)$l['wins'] ?></td>
          <td class="n"><?= (int)$l['matches'] ?></td>
          <td class="n"><?= number_format((int)$l['score']) ?></td>
        </tr>
      <?php endforeach; ?>
      </tbody>
    </table>
  <?php endif; ?>
</section>
