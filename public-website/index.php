<?php
// SPDX-License-Identifier: MIT
require __DIR__ . '/inc/site.php';
require_once __DIR__ . '/inc/screenshots.php';
$live = site_servers();
$t = $live['totals'];
page_start(site()['site_name'], 'index', 'Free, open-source tank combat for AmigaOS, macOS, Windows and Linux. Classic battles, new worlds, split-screen and online play.');
?>
<section class="hero">
  <div class="wrap hero-grid">
    <div class="hero-copy">
      <p class="eyebrow"><span class="status-dot" aria-hidden="true"></span> The tank classic, re-created</p>
      <h1>Small tanks.<br><span>Big battles.</span></h1>
      <p class="hero-description">Breach the base. Rescue your soldiers.<br>Bring the flag home.</p>
      <p class="hero-intro">OpenFP brings classic Amiga tank combat to a new generation. Play solo, share a screen, or face off online.</p>
      <div class="cta"><a class="btn primary" href="#get">Get the game <span aria-hidden="true">↗</span></a><a class="text-link" href="#about">Meet OpenFP <span aria-hidden="true">↓</span></a></div>
    </div>
    <figure class="hero-scene">
      <div class="scene-label"><span>Field notes / 01</span><span>Classic</span></div>
      <a href="/assets/img/20261111-desktop-classic.png" data-zoom><img src="/assets/img/20261111-desktop-classic.png" width="1152" height="648" alt="Classic tank combat in OpenFP, with fortified bases and destructible walls" fetchpriority="high"></a>
      <figcaption>Old-school combat. Plenty of room for a new plan.</figcaption>
    </figure>
  </div>
  <div class="wrap hero-bottom"><p>AmigaOS <span>·</span> macOS <span>·</span> Windows <span>·</span> Linux</p><p>Free &amp; open source <span aria-hidden="true">↗</span></p></div>
</section>

<section class="wrap home-section about-game" id="about" aria-labelledby="about-heading">
  <div class="section-heading"><p class="eyebrow">01 / About the game</p><h2 id="about-heading">From the Amiga.<br>For everyone.</h2><p class="section-intro">A little history. A lot more battlefield.</p></div>
  <div class="about-copy">
    <article><h3>It started in 1987.</h3><p><a href="https://www.mobygames.com/game/1640/fire-power/"><i>Fire Power</i></a>, developed by Silent Software and published by MicroIllusions, brought overhead tank battles to the Amiga: fortified bases, soldiers to rescue and an enemy flag to bring home.</p><p>You could take on the computer, share a split screen or connect over a modem. Blasting a path through the defences was half the fun.</p></article>
    <article><h3>The spirit stays. The game grows.</h3><p>OpenFP is a free, open-source recreation made from scratch. Classic keeps the original spirit, with three tank classes, soldier rescues and flag raids.</p><p>Seven enhanced worlds add new maps, weather, terrain and optional power-ups. Play across platforms with up to eight tanks, including bots, build your own maps and watch your battles again through replays.</p></article>
  </div>
  <div class="about-gallery" id="screenshots">
    <?php render_screenshots(true); ?>
    <details class="gallery-more"><summary>More from the battlefield <span aria-hidden="true">+</span></summary><?php render_screenshots(false, true); ?></details>
  </div>
  <div class="world-index"><div><p class="eyebrow">Choose your ground</p><h3>Familiar roots. Unfamiliar territory.</h3></div><div class="world-links"><?php foreach (['classic'=>'Classic','moon'=>'Moon Base','winter'=>'Winter','desert'=>'Desert','jungle'=>'Jungle','swamp'=>'Swamp','volcanic'=>'Volcano','wasteland'=>'Wasteland'] as $world=>$name): ?><a href="/maps/?world=<?= h($world) ?>"><?= h($name) ?> <span aria-hidden="true">↗</span></a><?php endforeach; ?></div></div>
  <details class="mode-detail" id="modes"><summary>Eight ways to win <span aria-hidden="true">+</span></summary>
    <dl class="mode-list">
    <div><dt>Classic</dt><dd>Breach the enemy base and bring its flag home under the 1987 rules.</dd></div>
    <div><dt>Capture the Flag</dt><dd>Steal the enemy flag, return it to base and defend your own.</dd></div>
    <div><dt>Conquest</dt><dd>Capture checkpoints and hold them to score for your team.</dd></div>
    <div><dt>Hardpoint</dt><dd>Hold the active checkpoint, then race to the next when it moves.</dd></div>
    <div><dt>Team Deathmatch</dt><dd>Destroy enemy tanks and reach the team’s kill target first.</dd></div>
    <div><dt>Kill Confirmed</dt><dd>Collect enemy tags to score; recover friendly tags to deny a point.</dd></div>
    <div><dt>Annihilation</dt><dd>Every tank for itself. The last one standing wins.</dd></div>
    <div><dt>Warlord</dt><dd>Protect your boss tank and destroy the opposing Warlord.</dd></div>
    </dl>
  </details>
</section>

<section class="download-band" id="get" aria-labelledby="get-heading"><div class="wrap">
  <div class="section-heading"><p class="eyebrow">02 / Join the battle</p><h2 id="get-heading">Get the game.</h2><p class="section-intro">Pick your platform. Bring a friend.</p></div>
  <?php render_release_downloads(site_releases()); ?>
  <?php if ($live['ok']): ?><a class="online-line" href="/server/" data-live><span><span class="status-dot" aria-hidden="true"></span> On the battlefield now</span><span><b data-k="servers"><?= (int)$t['servers'] ?></b> servers <span aria-hidden="true">/</span> <b data-k="players"><?= (int)$t['players'] ?></b> players <span aria-hidden="true">↗</span></span></a><?php else: ?><a class="online-line" href="/server/"><span>Looking for company?</span><span>Browse public servers <span aria-hidden="true">↗</span></span></a><?php endif; ?>
</div></section>
<?php page_end();
