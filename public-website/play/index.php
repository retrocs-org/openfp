<?php
// SPDX-License-Identifier: MIT
require __DIR__ . '/../inc/site.php';
$downloads = site_releases();
page_start('Play', 'play', 'Download OpenFP and join a game.');
?>
<section class="wrap prose">
  <h1>Play</h1>
  <p class="lead">Get a client to play, or run a Linux server for your own battles.</p>
  <nav class="section-nav" aria-label="Play sections">
    <a href="#play-game">Play a Game</a><a href="#host-game">Host a Game</a>
    <a href="#online">Join online</a><a href="#controls">Controls</a>
  </nav>

  <?php render_release_downloads($downloads); ?>

  <h2 id="controls">Default controls</h2>
  <table class="keys">
    <tr><th>Keyboard</th><th>Player 1</th><th>Player 2</th></tr>
    <tr><td>Desktop</td><td>Arrows, <kbd>.</kbd> fire, <kbd>,</kbd> mine</td><td>W A S D, Space fire, E mine</td></tr>
    <tr><td>AmigaOS</td><td>Arrows, Amiga fire, Alt mine</td><td>W A S D, Q fire, E mine</td></tr>
  </table>
  <p>Change your keyboard bindings in <b>Settings → Controls</b>. AmigaOS also supports joysticks.</p>

  <h2 id="how">How a battle goes</h2>
  <ol>
    <li>Roll out of your garage and find the enemy's fortress.</li>
    <li>Blow the gates, grab their flag, bring it home. Mind the guns, mines and helicopters.</li>
    <li>Break open prison camps and bring the rescued soldiers to first aid.</li>
    <li>Fuel up at your depot, patch up at first aid. A captured flag wins the day.</li>
  </ol>

  <h2 id="online">Play online</h2>
  <ol>
    <li>Sign in with a local profile. On desktop, open <b>Network Game</b> and pick <b>Quick Match</b> or <b>Find Servers</b>.
      On AmigaOS, open <b>Play → Network Game → Public</b>. The Amiga public list needs a working TCP/IP stack and AmiSSL.</li>
    <li>For an address from a friend, choose <b>Direct Connect</b>. On AmigaOS, find it under <b>Private</b>.
      Copy a public server address from <a href="/server/">the live server list</a> if you need to enter it manually.</li>
    <li>Choose <b>Ready</b> in the lobby. If a match is already running, look for the option to join it or watch.</li>
  </ol>
  <p>The first human to join is the lobby host. With manual start enabled, the host starts once the other humans are ready.
    Select a team-name button to rename it; the host, an authenticated administrator or that team’s leader can do this.
    Select the chat field before typing, or use its Keyboard button with a controller.</p>
  <p>Leaving a running match returns you to its server lobby. Rejoin while you still have tanks left;
    after elimination you can wait or spectate until the next match.</p>
  <p class="note">If your client is older than the server, update from the downloads above. If the server is older,
    ask the host to update it.</p>

  <p><a href="/source/">Source code and build instructions for every platform →</a></p>
</section>
<?php page_end();
