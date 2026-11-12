<?php
// SPDX-License-Identifier: MIT
require __DIR__ . '/../inc/site.php';
$views = ['leaders' => 'Leaderboard', 'daily' => 'Daily challenge', 'matches' => 'Matches'];
$view = $_GET['view'] ?? 'leaders';
if (ofp_ladders() || $view === 'ladders') $views['ladders'] = 'Ladders';
$valid = is_string($view) && isset($views[$view]);
if (!$valid) http_response_code(404);
page_start('Statistics', 'statistics', 'OpenFP leaderboards, daily challenges and match history in one place.');
?>
<section class="wrap statistics-head">
  <h1>Statistics</h1>
  <p class="lead">Player rankings, daily challenges and the latest battles.</p>
  <nav class="section-nav" aria-label="Statistics sections">
    <?php foreach ($views as $key => $label): ?>
      <a href="/statistics/?view=<?= h($key) ?>"<?= $valid && $view === $key ? ' aria-current="page"' : '' ?>><?= h($label) ?></a>
    <?php endforeach; ?>
  </nav>
</section>
<?php
// The include path comes only from this fixed map, never from request data.
$pages = ['leaders' => 'leaders.php', 'daily' => 'daily.php', 'matches' => 'matches.php', 'ladders' => 'ladders.php'];
if ($valid) require __DIR__ . '/../inc/activity/' . $pages[$view];
else echo '<section class="wrap"><p class="notice">Choose a Statistics section above.</p></section>';
page_end();
