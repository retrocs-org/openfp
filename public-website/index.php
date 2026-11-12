<?php
// SPDX-License-Identifier: MIT
require __DIR__ . '/inc/site.php';
$live = site_servers();
$t = $live['totals'];
page_start(site()['site_name'], 'index');
?>
<section class="hero">
  <div class="hero-art" style="background-image:url(/assets/img/20261111-desktop-classic.png)"></div>
  <div class="wrap hero-text">
    <p class="kicker">Open source · AmigaOS · macOS · Linux · Windows</p>
    <h1>OpenFP<span>. Ready for battle.</span></h1>
    <p class="lead">An open remake of the 1987 Amiga tank game. In Classic, roll out of your garage, breach the enemy base,
      rescue soldiers and bring the flag home. The newer worlds have their own maps, weather and ways to win.</p>
    <div class="cta">
      <a class="btn primary" href="#get">Get the game</a>
      <a class="btn" href="/server/">Find a server<?php if ($t['players']): ?> · <?= (int)$t['players'] ?> online<?php endif; ?></a>
      <a class="btn" href="/source/">Build from source</a>
    </div>
    <ul class="live" data-live>
      <li><b data-k="servers"><?= (int)$t['servers'] ?></b> servers</li>
      <li><b data-k="players"><?= (int)$t['players'] ?></b> playing now</li>
      <li><b data-k="peak_players_24h"><?= (int)$t['peak_players_24h'] ?></b> at the busiest today</li>
    </ul>
  </div>
</section>

<section class="wrap features">
  <h2>What's in it</h2>
  <div class="grid3">
    <article><h3>Classic tank combat</h3><p>Classic maps and rules capture the feel of the original Amiga game.
      Play solo against the computer or take on a friend. Classic keeps the original three tanks and leaves out weather, power-ups and tactical team orders.</p></article>
    <article><h3>Local split screen</h3><p>Play with up to four people on macOS, Linux and Windows, or two on AmigaOS.
      The Amiga edition uses a native 16-colour display on a 68040 or 68060.</p></article>
    <article><h3>Cross-platform multiplayer</h3><p>Join a public or private server. The dedicated server runs the bots
      and decides match results. Up to eight tanks, including bots, fit each match, subject to the map’s capacity. The lobby has teams, chat and ready status.</p></article>
    <article><h3>Seven newer worlds</h3><p>Moon Base, Winter, Desert, Jungle, Swamp, Volcano and Wasteland.
      Browse the <a href="/maps/">maps included with the game</a>.</p></article>
    <article><h3>Destructible battlefields</h3><p>Blast through gates and walls, rescue soldiers, and take cover beneath trees.
      Enhanced worlds can have weather such as jungle thunderstorms or winter snowstorms. Winter mixes water and slippery ice; Moon Base has dry crater trenches. Oil spills and optional speed and rapid-fire boosts add variety outside Classic.</p></article>
    <article><h3>Maps and replays</h3><p>Build a battlefield in the map editor, check that it’s playable and share it in
      the <a href="/maps/">map gallery</a>. Three rotating recovery drafts protect unfinished maps. Replays let you watch a match again.</p></article>
  </div>
</section>

<section class="wrap features" id="modes">
  <h2>Game modes</h2>
  <div class="grid3">
    <article><h3>Classic</h3><p>Fight through a fortified base and bring its flag home under the 1987 rules.</p></article>
    <article><h3>Capture the Flag</h3><p>Steal the enemy flag and return it to your base. Defend your own flag along the way.</p></article>
    <article><h3>Conquest</h3><p>Capture checkpoints and hold them to earn points for your team.</p></article>
    <article><h3>Hardpoint</h3><p>Hold the active checkpoint to score, then race to the next when it moves.</p></article>
    <article><h3>Team Deathmatch</h3><p>Score by destroying enemy tanks. The first team to reach the kill target wins.</p></article>
    <article><h3>Kill Confirmed</h3><p>Collect tags dropped by enemy tanks to score. Recover friendly tags to deny the enemy a point.</p></article>
    <article><h3>Annihilation</h3><p>Every tank for itself. The last one standing wins.</p></article>
    <article><h3>Warlord</h3><p>Protect your team’s boss tank and destroy the opposing Warlord to score.</p></article>
  </div>
</section>

<section class="wrap features">
  <h2>Play your way</h2>
  <div class="grid3">
    <article><h3>Keyboard or controller</h3><p>Each local player signs in with their own profile and controls.
      Change key bindings, check controller assignment and use the on-screen keyboard to chat.</p></article>
    <article><h3>Choose the detail</h3><p>Adjust explosions, ground fires, smoke, particles and weather separately.
      A live preview shows each setting, and optional Auto Effects reduces decoration when frames slow.</p></article>
    <article><h3>Build and share</h3><p>Start with an empty battlefield. Place roads and buildings, draw team areas,
      set the mode and player count, then run a playability check before testing or sharing the map.</p></article>
  </div>
</section>

<section class="gallery-band" id="screenshots">
  <div class="wrap">
    <h2>Screenshots</h2>
    <p>Current desktop and native 16-colour AmigaOS views. Open an image to see the full frame.</p>
    <?php require_once __DIR__ . '/inc/screenshots.php'; render_screenshots(true); ?>
    <details class="gallery-more">
      <summary>More screenshots</summary>
      <?php render_screenshots(false, true); ?>
    </details>
  </div>
</section>

<section class="wrap features downloads" id="get">
  <?php render_release_downloads(site_releases()); ?>
</section>

<section class="wrap split">
  <div>
    <h2>Build from source</h2>
    <p>Build the game for Linux, macOS, Windows or AmigaOS. The dedicated server can run on Linux without a desktop or SDL.</p>
    <a class="btn" href="/source/">Client build steps</a>
    <a class="btn" href="/source/#server">Host a server</a>
  </div>
</section>
<?php page_end();
