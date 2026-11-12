<?php
// SPDX-License-Identifier: MIT
require __DIR__ . '/../inc/documentation.php';
documentation_start('overview');
?>
<div class="documentation-grid">
  <?php foreach (documentation_guides() as $key => [$title, $description]): if ($key === 'overview') continue; ?>
  <a class="documentation-card" href="/documentation/<?= h($key) ?>/"><h2><?= h($title) ?> <span aria-hidden="true">↗</span></h2><p><?= h($description) ?></p></a>
  <?php endforeach; ?>
  <a class="documentation-card" href="/api/docs/"><h2>API reference <span aria-hidden="true">↗</span></h2><p>Server listings, map downloads, match results and integration examples.</p></a>
</div>
<h2>Source and technical references</h2>
<p>For compilation and packaging, use <a href="/source/">Source</a>. The repository also includes the detailed <a href="https://github.com/retrocs-org/openfp/blob/main/docs/GAME.md">game guide</a>, <a href="https://github.com/retrocs-org/openfp/blob/main/docs/NETWORK.md">network guide</a>, <a href="https://github.com/retrocs-org/openfp/blob/main/docs/MAPS.md">map format</a>, <a href="https://github.com/retrocs-org/openfp/blob/main/docs/PORTING.md">AmigaOS port notes</a> and <a href="https://github.com/retrocs-org/openfp/blob/main/docs/TESTING.md">testing guide</a>.</p>
<p>Music, artwork and other third-party assets are listed in <a href="/credits/">Third Party Credits</a>. The website's deployment guide is included in the <a href="https://github.com/retrocs-org/openfp/blob/main/public-website/README.md">source repository</a>.</p>
<?php documentation_end();
