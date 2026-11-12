<?php
// SPDX-License-Identifier: MIT
// Watching a replay in the browser: the game's own core (WebAssembly) plays the match
// again, exactly as it went, and the page draws the whole battlefield from above.
require __DIR__ . '/../inc/site.php';

$id = (int)($_GET['id'] ?? 0);
$daily_id = (int)($_GET['daily'] ?? 0);                  // (a daily challenge's replay)
$live = preg_match('/^[A-Za-z0-9.\-]+:\d{1,5}$/', $_GET['live'] ?? '') ? $_GET['live'] : '';   // (a match that's on)
$m = $id ? site_safe(function () use ($id) {
    $q = ofp_db()->prepare('SELECT id, server_name, map, world, mode, ended, seconds, players FROM matches WHERE id = ? AND has_replay = 1');
    $q->execute([$id]);
    return $q->fetch() ?: null;
}, null) : null;
page_start($m ? 'Replay: ' . ($m['map'] === 'random' ? 'a random battlefield' : $m['map']) : 'Replay', 'matches',
           'Watch an OpenFP match again, in the browser.');
?>
<section class="wrap">
  <h1><?= $m ? h($m['map'] === 'random' ? 'A random battlefield' : $m['map']) : ($daily_id ? 'The daily challenge' : ($live ? 'Live' : 'Watch a replay')) ?></h1>
  <?php if ($m): ?>
    <p class="lead"><?= h($m['server_name']) ?> · <?= h(ago((int)$m['ended'])) ?> · <?= $m['mode'] === 'versus' ? 'Versus' : 'Co-op' ?>
      · <a href="/api/?action=replay&amp;id=<?= (int)$m['id'] ?>">Download the replay</a></p>
  <?php else: ?>
    <p class="lead">Drop an <code>.ofprep</code> file here (the game keeps them under REPLAYS), or pick a match
      from the <a href="/matches/">history</a>.</p>
  <?php endif; ?>
  <div class="viewer" data-replay="<?= $m ? (int)$m['id'] : '' ?>" data-daily="<?= $daily_id ?: '' ?>" data-live="<?= h($live) ?>">
    <canvas width="512" height="512" aria-label="The battlefield"></canvas>
    <div class="viewer-side">
      <div class="viewer-controls">
        <button class="btn small" data-play>Play</button>
        <button class="btn small" data-speed>1x</button>
        <button class="btn small" data-zoom>Zoom 1x</button>
        <span class="viewer-time muted small">0:00 / 0:00</span>
      </div>
      <input type="range" min="0" max="1000" value="0" data-seek aria-label="Where in the match">
      <table class="board viewer-score"><tbody></tbody></table>
      <p class="muted small viewer-status">Loading…</p>
      <p class="muted small">Click a player to follow their tank (closer); Zoom goes 1x, 2x, 4x. Blue: the camp
        side, red: the fortress side. The dots are soldiers and shots; the diamonds the flags.</p>
    </div>
  </div>
</section>
<script src="/assets/replay.js?v=4"></script>
<script src="/assets/viewer.js?v=6"></script>
<?php page_end();
