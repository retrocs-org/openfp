<?php
// SPDX-License-Identifier: MIT
require __DIR__ . '/../inc/site.php';
require __DIR__ . '/../inc/screenshots.php';
page_start('Screenshots', 'screenshots', 'OpenFP gameplay, split screen, editors and settings on desktop and native 16-colour AmigaOS.');
?>
<section class="wrap features">
  <h1>See the game</h1>
  <p class="lead">Classic battles, enhanced worlds and the tools to make your own maps.</p>
  <p>Gameplay, split screen, map editors and settings from the desktop and native 16-colour AmigaOS clients.
    The desktop client is shared by macOS, Windows and Linux. Open any image to see the complete game frame.</p>
  <?php render_screenshots(); ?>
  <p><a class="btn primary" href="/play/">Get the game</a> <a class="btn" href="/build/">Build from source</a></p>
</section>
<?php page_end();
