<?php
// SPDX-License-Identifier: MIT
// The match history: what was played where, who won, how ratings moved, and the replays.

$host = $port = null;
if (preg_match('/^\[?([A-Za-z0-9.\-:]+?)\]?:(\d{1,5})$/', $_GET['server'] ?? '', $m)) { $host = $m[1]; $port = (int)$m[2]; }
$before = (int)($_GET['before'] ?? 0);
$matches = site_safe(fn() => ofp_matches(40, $host, $port, $before), null);
$title = $host ? 'Matches on ' . ($matches[0]['server_name'] ?? "$host:$port") : 'Matches';
?>
<section class="wrap">
  <h2><?= h($title) ?></h2>
  <p>Recent battles, team results and replays from public servers.
     <?php if ($host): ?><a href="/statistics/?view=matches">All servers &rarr;</a><?php endif; ?></p>

  <?php if ($matches === null): ?>
    <p class="notice">The match history isn't available right now.</p>
  <?php elseif (!$matches): ?>
    <p class="notice">No shared matches yet<?= $host ? ' on this server' : '' ?>. Finished public battles will appear here.</p>
  <?php else: ?>
    <div class="matches">
      <?php foreach ($matches as $m): ?>
        <article class="match">
          <header>
            <h3><?= h($m['map'] === 'random' ? 'A random battlefield' : $m['map']) ?>
                <span class="muted small"><?= h(['scifi' => 'Moon Base', 'moon' => 'Moon Base', 'volcanic' => 'Volcano'][$m['world']] ?? ucfirst($m['world'])) ?> ·
                <?= $m['mode'] === 'versus' ? 'Versus' : 'Co-op' ?> · <?= h(duration($m['seconds'])) ?></span>
                <?php if (($m['winner'] ?? '') === 'draw'): ?><span class="tag">A draw</span><?php endif; ?></h3>
            <div class="muted small">
              <a href="/statistics/?view=matches&amp;server=<?= h(urlencode($m['host'] . ':' . $m['port'])) ?>"><?= h($m['server_name']) ?></a>
              · <?= h(ago($m['ended'])) ?>
            </div>
          </header>
          <table class="players">
            <thead><tr><th>Player</th><th>Side</th><th></th><th class="n">Kills</th><th class="n">Score</th><th class="n">Rating</th></tr></thead>
            <tbody>
            <?php foreach ($m['players'] as $p): ?>
              <tr class="<?= $p['won'] ? 'won' : '' ?>">
                <td><?= $p['bot'] ? '<span class="muted">Computer</span>' : '<a href="/player/?name=' . h(urlencode($p['name'])) . '&amp;server=' . h(urlencode($m['host'] . ':' . $m['port'])) . '">' . h($p['name']) . '</a>' ?></td>
                <td><?= h(side_name($p['side'])) ?></td>
                <td><?= $p['won'] ? '<span class="tag on">Won</span>' : '' ?></td>
                <td class="n"><?= (int)$p['kills'] ?></td>
                <td class="n"><?= number_format((int)$p['score']) ?></td>
                <td class="n"><?php if (!$p['bot']): ?><?= (int)$p['rating'] ?>
                  <span class="<?= $p['change'] >= 0 ? 'up' : 'down' ?>"><?= $p['change'] >= 0 ? '+' : '' ?><?= (int)$p['change'] ?></span>
                <?php endif; ?></td>
              </tr>
            <?php endforeach; ?>
            </tbody>
          </table>
          <?php if ($m['has_replay']): ?>
            <a class="btn small" href="/replay/?id=<?= (int)$m['id'] ?>">Watch</a>
            <a class="btn small" href="/api/?action=replay&amp;id=<?= (int)$m['id'] ?>">Download the replay</a>
          <?php endif; ?>
        </article>
      <?php endforeach; ?>
    </div>
    <?php if (count($matches) === 40): ?>
      <p><a class="btn" href="?<?= h(http_build_query(array_filter(['view' => 'matches', 'server' => $host ? "$host:$port" : null,
                                    'before' => end($matches)['ended']]))) ?>">Older matches</a></p>
    <?php endif; ?>
  <?php endif; ?>
</section>
