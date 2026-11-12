<?php
// SPDX-License-Identifier: MIT
// The maps that ship with OpenFP and the maps shared from the editor.
require __DIR__ . '/../inc/site.php';

$worlds = ['' => 'All worlds'] + OFP_MAP_WORLDS;
$modes = ['' => 'All game modes'] + OFP_MAP_MODES;
$world = $_GET['world'] ?? '';
$mode = $_GET['mode'] ?? '';
if (!is_string($world) || !isset($worlds[$world])) $world = '';
if (!is_string($mode) || !isset($modes[$mode])) $mode = '';
$catalog_path = __DIR__ . '/../assets/bundled-maps.json';
$catalog = is_readable($catalog_path) ? json_decode((string)file_get_contents($catalog_path), true) : null;
$bundled = array_filter($catalog ?? [], static fn($map) =>
    ($world === '' || $map['world'] === $world) && ($mode === '' || in_array($mode, $map['modes'] ?? [], true)));
$maps = site_safe(fn() => ofp_gallery_maps($world, $mode), null);
page_start('Maps', 'maps', 'Browse the maps included in OpenFP and the maps shared by players.');
?>
<section class="wrap">
  <h1>Maps</h1>
  <p class="lead">Browse the maps included with OpenFP and the maps shared by players.</p>
  <p><a href="#bundled">Bundled maps</a> · <a href="#community">Community maps</a></p>
  <form class="toolbar map-filters" method="get" action="/maps/" aria-label="Filter maps">
    <label>World<select name="world"><?php foreach ($worlds as $key => $label): ?><option value="<?= h($key) ?>"<?= $world === $key ? ' selected' : '' ?>><?= h($label) ?></option><?php endforeach; ?></select></label>
    <label>Game mode<select name="mode"><?php foreach ($modes as $key => $label): ?><option value="<?= h($key) ?>"<?= $mode === $key ? ' selected' : '' ?>><?= h($label) ?></option><?php endforeach; ?></select></label>
    <button class="btn small primary" type="submit">Apply filters</button>
    <?php if ($world !== '' || $mode !== ''): ?><a class="text-link" href="/maps/">Clear filters</a><?php endif; ?>
  </form>

  <h2 id="bundled">Bundled maps</h2>
  <?php if ($catalog === null): ?>
    <p class="notice">The bundled map catalog is temporarily unavailable.</p>
  <?php elseif (!$bundled): ?>
    <p class="notice">No bundled maps match these filters.</p>
  <?php else: ?>
    <p><?= count($bundled) ?> bundled <?= count($bundled) === 1 ? 'map' : 'maps' ?><?= $world !== '' || $mode !== '' ? (count($bundled) === 1 ? ' matches' : ' match') . ' your filters' : ' included with OpenFP' ?>.</p>
    <div class="gallery map-gallery">
      <?php foreach ($bundled as $m): ?>
        <article>
          <img width="96" height="96" loading="lazy" alt="" src="data:image/png;base64,<?= h($m['preview']) ?>">
          <h3><?= h($m['name']) ?></h3>
          <div class="muted small"><?= h($worlds[$m['world']] ?? ucfirst($m['world'])) ?> · <?= h(ucfirst($m['time'])) ?> · <?= (int)$m['size'] ?> × <?= (int)$m['size'] ?></div>
          <p class="map-modes"><?= h(implode(' · ', array_map(static fn($key) => OFP_MAP_MODES[$key], $m['modes']))) ?></p>
        </article>
      <?php endforeach; ?>
    </div>
  <?php endif; ?>

  <h2 id="community">Community maps</h2>
  <p>Share maps from the editor. Download one into your maps folder to play locally, or add
     <code>maps = gallery:NAME</code> to your server configuration.</p>
  <?php if ($maps === null): ?>
    <p class="notice">The gallery isn't available right now.</p>
  <?php elseif (!$maps): ?>
    <p class="notice">No community maps match these filters yet.</p>
  <?php else: ?>
    <div class="gallery">
      <?php foreach ($maps as $m):
          $author = $m['author'];
      ?>
        <article>
          <canvas width="128" height="128" data-map="<?= (int)$m['id'] ?>" data-world="<?= h($m['world']) ?>"></canvas>
          <h3><?= h($m['name']) ?></h3>
          <div class="muted small"><?= h($worlds[$m['world']] ?? ucfirst($m['world'])) ?> · <?= h(ucfirst($m['time'])) ?>
            <?= $author ? ' · by ' . h($author) : '' ?> · <?= h(ago((int)$m['uploaded'])) ?> · <?= (int)$m['downloads'] ?> downloads</div>
          <p class="map-modes"><?= h(implode(' · ', array_map(static fn($key) => OFP_MAP_MODES[$key], $m['modes']))) ?></p>
          <?php if ($m['description'] !== ''): ?><p><?= h($m['description']) ?></p><?php endif; ?>
          <?php if ($m['contact'] !== ''): ?><p class="muted small">Contact <?= h($m['contact']) ?></p><?php endif; ?>
          <p><a class="btn small" href="/api/?action=map&amp;id=<?= (int)$m['id'] ?>&amp;download=1">Download</a>
             <code class="small">gallery:<?= h($m['name']) ?></code></p>
        </article>
      <?php endforeach; ?>
    </div>
  <?php endif; ?>
</section>
<script>
// Request community map previews in batches of up to 60.
(function () {
  var ground = { classic: '#6f8d3a', desert: '#d8b878', jungle: '#3f7a2a', winter: '#e8eef2', scifi: '#3a3f48',
                 volcanic: '#3a3230', wasteland: '#8a7448', moon: '#9a9aa2', swamp: '#4a5a32' };
  var colour = { '=': '#707070', '~': '#2f68b8', '^': '#5a5048', 'T': '#2c5a1e', ',': '#6a5a3a', '#': '#9a8f80', ':': '#857a6c',
                 'x': '#4a4038', 'P': '#3b82ff', 'E': '#ff4030', 'p': '#9cc0ff', 'e': '#ff9a88', 'i': '#d25ff0' };
  var canvases = Array.prototype.slice.call(document.querySelectorAll('canvas[data-map]'));
  for (var start = 0; start < canvases.length; start += 60) {
    drawPreviews(canvases.slice(start, start + 60));
  }
  function drawPreviews(canvases) {
    fetch('/api/?action=map_thumbs&ids=' + canvases.map(function (c) { return c.dataset.map; }).join(','))
      .then(function (r) { return r.json(); }).then(function (thumbs) {
        canvases.forEach(function (c) {
          var t = thumbs[c.dataset.map], g = c.getContext('2d'), i;
          g.fillStyle = ground[c.dataset.world] || ground.classic; g.fillRect(0, 0, 128, 128);
          if (!t) return;
          for (i = 0; i < t.length; i++) {
            var k = t.charAt(i), big = 'PE'.indexOf(k) >= 0;
            if (!colour[k]) continue;
            g.fillStyle = c.dataset.world === 'moon' && k === '~' ? '#302f37' : colour[k];
            g.fillRect((i % 64) * 2 - (big ? 2 : 0), Math.floor(i / 64) * 2 - (big ? 2 : 0), big ? 6 : 2, big ? 6 : 2);
          }
        });
      }).catch(function () {});
  }
})();
</script>
<?php page_end();
