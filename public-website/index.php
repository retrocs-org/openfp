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
    <p class="lead">Classic tank combat, destructible bases and cross-platform battles.
      Play solo, share a split screen, or join friends online.</p>
    <div class="cta">
      <a class="btn primary" href="#get">Get the game</a>
      <a class="btn" href="/server/">Find a server</a>
    </div>
    <?php if ($live['ok']): ?>
    <ul class="live" data-live>
      <li><b data-k="servers"><?= (int)$t['servers'] ?></b> servers</li>
      <li><b data-k="players"><?= (int)$t['players'] ?></b> playing now</li>
    </ul>
    <?php endif; ?>
  </div>
</section>

<section class="wrap home-section downloads" id="get" aria-label="Get OpenFP">
  <?php render_release_downloads(site_releases()); ?>
</section>

<section class="wrap home-section play-guide" aria-labelledby="start-playing">
  <h2 id="start-playing">Start playing</h2>
  <div class="grid2 getting-started">
    <article>
      <h3>On one screen</h3>
      <ol>
        <li>Sign in with a profile, then choose <b>Local Game</b>.</li>
        <li>Pick a world, map and game mode. Choose your tank and team.</li>
        <li>Ready up and start. Play against bots or invite another local player.</li>
      </ol>
      <p class="note">Up to four local players on desktop, or two on AmigaOS.
        Keyboard, controllers and Amiga joysticks are supported.</p>
    </article>
    <article id="online">
      <h3>Join online</h3>
      <ol>
        <li>Sign in, then open <b>Network Game</b>. Choose <b>Quick Match</b> or <b>Find Servers</b> on desktop,
          or <b>Play → Network Game → Public</b> on AmigaOS.</li>
        <li>Pick a server, or enter a friend’s address with <b>Direct Connect</b>
          (<b>Private</b> on AmigaOS).</li>
        <li>Choose your team and tank, then select <b>Ready</b>.</li>
      </ol>
      <a href="/server/">Browse the live server list →</a>
    </article>
  </div>
  <details class="guide-detail" id="controls">
    <summary>Default keyboard controls</summary>
    <div class="wrap-table">
      <table class="keys">
        <thead><tr><th scope="col">Platform</th><th scope="col">Player 1</th><th scope="col">Player 2</th></tr></thead>
        <tbody>
          <tr><th scope="row">Desktop</th><td>Arrows, <kbd>.</kbd> fire, <kbd>,</kbd> mine</td><td>W A S D, Space fire, E mine</td></tr>
          <tr><th scope="row">AmigaOS</th><td>Arrows, Amiga fire, Alt mine</td><td>W A S D, Q fire, E mine</td></tr>
        </tbody>
      </table>
    </div>
    <p>Change keyboard bindings in <b>Settings → Controls</b>.</p>
  </details>
  <details class="guide-detail">
    <summary>Lobbies, chat and rejoining</summary>
    <p>Up to eight tanks, including bots, can join a network match, depending on the map.
      The dedicated server runs the bots and decides match results. The first human to join is the lobby host;
      with manual start enabled, the host starts once the other humans are ready.</p>
    <p>Select a team-name button to rename it. The host, an authenticated administrator or that team’s leader
      can do this. Select the chat field before typing, or use its Keyboard button with a controller.</p>
    <p>Leaving a running match returns you to its lobby. Rejoin while you still have tanks left;
      after elimination, wait or spectate until the next match.</p>
    <p class="note">AmigaOS needs a working TCP/IP stack and AmiSSL for the public list.
      Clients and servers should both use the latest available release.</p>
  </details>
</section>

<section class="wrap home-section" aria-labelledby="about-game">
  <h2 id="about-game">Classic roots. New battlefields.</h2>
  <div class="overview-grid">
    <article>
      <h3>The Classic game</h3>
      <p>An open remake of the 1987 Amiga game. Breach the enemy base, rescue soldiers and bring the flag home.
        Classic keeps the original three tanks and leaves out weather, power-ups and tactical team orders.</p>
      <p class="note">The native AmigaOS edition uses 16 colours and requires a 68040 or 68060.</p>
    </article>
    <article>
      <h3>Seven enhanced worlds</h3>
      <p>Moon Base, Winter, Desert, Jungle, Swamp, Volcano and Wasteland add their own maps and terrain.
        Weather, slippery ice, oil spills and optional power-ups bring more variety outside Classic.</p>
      <p><a href="/maps/">Explore the included maps →</a></p>
    </article>
    <article>
      <h3>Create and replay</h3>
      <p>Build your own battlefield, place buildings and team areas, then check that it’s playable before sharing.
        Recovery drafts protect unfinished maps, and replays let you watch a battle again.</p>
      <p><a href="/maps/">Visit the map gallery →</a></p>
    </article>
  </div>
</section>

<section class="wrap home-section" id="modes">
  <h2>Game modes</h2>
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
  <details class="guide-detail" id="how">
    <summary>How a Classic battle works</summary>
    <p>Roll out of your garage, breach the enemy gates and bring their flag home.
      Break open prison camps and take rescued soldiers to first aid.
      Refuel at your depot and repair at first aid as you fight through guns, mines and helicopters.</p>
  </details>
</section>

<section class="gallery-band" id="screenshots">
  <div class="wrap">
    <h2>Screenshots</h2>
    <p>Desktop and native 16-colour AmigaOS views. Select an image to see the full frame.</p>
    <?php require_once __DIR__ . '/inc/screenshots.php'; render_screenshots(true); ?>
    <details class="gallery-more">
      <summary>More screenshots</summary>
      <?php render_screenshots(false, true); ?>
    </details>
  </div>
</section>
<?php page_end();
