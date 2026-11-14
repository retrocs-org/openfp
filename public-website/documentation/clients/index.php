<?php
// SPDX-License-Identifier: MIT
require __DIR__ . '/../../inc/documentation.php';
documentation_start('clients');
?>
<p>For public servers, <a href="/documentation/accounts/">sign in with a cloud account</a>. Desktop and AmigaOS clients use username and password; Apple TV uses a pairing PIN. Local profiles remain available for local and private-server games.</p>

<h2 id="installation">Install a client</h2>
<p>Get a published package from <a href="/#get">Get the game</a>. While the game is unreleased, <a href="/source/">build it from source</a>.</p>
<ul>
  <li><strong>macOS:</strong> open the DMG and copy the app to Applications, or extract the app ZIP. Both Apple silicon and Intel use the universal app.</li>
  <li><strong>Windows:</strong> run the MSI installer, or extract the portable ZIP and launch <code>openfirepower.exe</code>. Keep the accompanying data and SDL2 library with the executable.</li>
  <li><strong>Linux:</strong> openSUSE Leap 16 is the preferred platform. Extract the archive for your architecture and launch <code>OpenFirePower/Play</code>. Install the runtime libraries shown below.</li>
  <li><strong>AmigaOS:</strong> extract the LHA and use its Workbench installer. The ECS client uses 16 colours and requires AmigaOS 3.x, a 68040 or 68060, 2 MB Chip RAM and at least 8 MB Fast RAM; 16 MB Fast RAM is recommended.</li>
</ul>
<pre class="code"><code>sudo zypper install libSDL2-2_0-0 libcurl4 ca-certificates</code></pre>
<p>Other Linux distributions can use compatible packages or <a href="/source/#linux">build from source</a>. Apple TV remains an experimental build preview.</p>
<h2 id="profiles">Profiles and local players</h2>
<p>Press Fire on the keyboard or controller you want to use, then select a profile or create one. A profile stores your name, avatar, favourite tank and steering choice. Additional players press Fire on their own unassigned controls to sign in. Use each player's sign-out control in the main menu to log out.</p>
<h2 id="controls">Default controls</h2>
<p>Choose a keyboard layout or sign in with your controller. These are the defaults; saved profiles can use different bindings. Change them in <strong>Settings → Controls</strong>.</p>
<nav class="control-jump" aria-label="Control layouts"><a href="#desktop-keys">Desktop keyboards</a><a href="#desktop-pads">Xbox &amp; PS5</a><a href="#amiga-keys">Amiga keyboards</a><a href="#amiga-pads">Amiga joystick &amp; CD32</a></nav>

<h3 id="desktop-keys" class="control-heading">Desktop keyboards</h3>
<p>For macOS, Windows and Linux. Player 1's direction keys also navigate menus; Fire selects and Mine goes back.</p>
<figure class="control-card control-keyboard">
  <a href="/assets/img/controls/keyboard-desktop.svg" aria-label="Open the desktop keyboard illustration at full size"><img src="/assets/img/controls/keyboard-desktop.svg" width="900" height="530" alt="Player 1 arrow keys with period for Fire and comma for Mine; Player 2 W A S D with Space for Fire and Left Shift for Mine." loading="lazy"></a>
  <figcaption>
    <div class="control-columns">
      <div><h4>Player 1</h4><dl class="control-bindings"><div><dt>Forward / backward</dt><dd>↑ / ↓</dd></div><div><dt>Turn</dt><dd>← / →</dd></div><div><dt>Fire / select</dt><dd>Period <kbd>.</kbd></dd></div><div><dt>Mine / back</dt><dd>Comma <kbd>,</kbd></dd></div></dl></div>
      <div><h4>Player 2</h4><dl class="control-bindings"><div><dt>Forward / backward</dt><dd><kbd>W</kbd> / <kbd>S</kbd></dd></div><div><dt>Turn</dt><dd><kbd>A</kbd> / <kbd>D</kbd></dd></div><div><dt>Fire</dt><dd>Space or Left Alt</dd></div><div><dt>Mine</dt><dd>Left Shift or <kbd>E</kbd></dd></div></dl></div>
    </div>
    <p class="control-note">Enter selects; Escape goes back or pauses a match. Desktop Players 3 and 4 use controllers.</p>
  </figcaption>
</figure>

<h3 id="desktop-pads" class="control-heading">Xbox &amp; PlayStation 5 controllers</h3>
<p>The default <strong>trigger driving</strong> layout uses the triggers for forward and backward, and the left stick to turn. The D-pad can drive in all four directions. In menus, use the left stick or D-pad to navigate.</p>
<div class="control-grid">
  <figure class="control-card">
    <a href="/assets/img/controls/controller-xbox.svg" aria-label="Open the Xbox controller illustration at full size"><img src="/assets/img/controls/controller-xbox.svg" width="1000" height="720" alt="Redrawn Xbox controller with six numbered action callouts." loading="lazy"></a>
    <figcaption><h4>Xbox</h4><dl class="control-bindings numbered">
      <div><dt><span>1</span> Backward</dt><dd>LT</dd></div><div><dt><span>2</span> Forward</dt><dd>RT</dd></div><div><dt><span>3</span> Turn / navigate</dt><dd>Left stick or D-pad</dd></div><div><dt><span>4</span> Fire / select</dt><dd>A or RB</dd></div><div><dt><span>5</span> Lay mine</dt><dd>X</dd></div><div><dt><span>6</span> Menu back</dt><dd>B</dd></div>
    </dl><p class="control-note">Menu pauses a match. LB is an alternate mine button in local play; in an online match it opens the chat keyboard.</p></figcaption>
  </figure>
  <figure class="control-card">
    <a href="/assets/img/controls/controller-ps5.svg?v=20261112-2" aria-label="Open the PS5 controller illustration at full size"><img src="/assets/img/controls/controller-ps5.svg?v=20261112-2" width="1000" height="720" alt="Redrawn PS5 controller with six numbered action callouts." loading="lazy"></a>
    <figcaption><h4>PlayStation 5 / DualSense</h4><dl class="control-bindings numbered">
      <div><dt><span>1</span> Backward</dt><dd>L2</dd></div><div><dt><span>2</span> Forward</dt><dd>R2</dd></div><div><dt><span>3</span> Turn / navigate</dt><dd>Left stick or D-pad</dd></div><div><dt><span>4</span> Fire / select</dt><dd>Cross (×) or R1</dd></div><div><dt><span>5</span> Lay mine</dt><dd>Square (□)</dd></div><div><dt><span>6</span> Menu back</dt><dd>Circle (○)</dd></div>
    </dl><p class="control-note">Options pauses a match. L1 is an alternate mine button in local play; in an online match it opens the chat keyboard.</p></figcaption>
  </figure>
</div>
<p class="control-note">The optional stick-driving layout changes gameplay movement and action bindings. Menu navigation still uses the stick or D-pad, A/Cross to select and B/Circle to go back.</p>

<h3 id="amiga-keys" class="control-heading">AmigaOS keyboards</h3>
<figure class="control-card control-keyboard">
  <a href="/assets/img/controls/keyboard-amiga.svg" aria-label="Open the Amiga keyboard illustration at full size"><img src="/assets/img/controls/keyboard-amiga.svg" width="900" height="530" alt="Amiga Player 1 uses arrow keys, Amiga to fire and Alt for mines; Player 2 uses W A S D, Q to fire and E for mines." loading="lazy"></a>
  <figcaption><div class="control-columns">
    <div><h4>Player 1</h4><dl class="control-bindings"><div><dt>Move / turn</dt><dd>Arrow keys</dd></div><div><dt>Fire / select</dt><dd>Left or Right Amiga</dd></div><div><dt>Mine / back</dt><dd>Left or Right Alt</dd></div></dl></div>
    <div><h4>Player 2</h4><dl class="control-bindings"><div><dt>Move / turn</dt><dd><kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd></dd></div><div><dt>Fire</dt><dd><kbd>Q</kbd></dd></div><div><dt>Mine</dt><dd><kbd>E</kbd></dd></div></dl></div>
  </div><p class="control-note">Player 1's directions, Fire and Mine also control menus. Enter selects and Escape goes back or pauses.</p></figcaption>
</figure>

<h3 id="amiga-pads" class="control-heading">AmigaOS joystick &amp; CD32 gamepad</h3>
<p>Each player can use a joystick or CD32 pad on their assigned port. <strong>Settings → Controls</strong> can swap the ports or combine keys and joystick input.</p>
<div class="control-grid">
  <figure class="control-card">
    <a href="/assets/img/controls/joystick-amiga-tac2.png?v=2" aria-label="Open the TAC-2 Amiga joystick illustration at full size"><img src="/assets/img/controls/joystick-amiga-tac2.png?v=2" width="1000" height="720" alt="Illustrated TAC-2 Amiga joystick with a black ball top, silver shaft, two aligned orange fire buttons, no cord and external control labels." loading="lazy"></a>
    <figcaption><h4>TAC-2 joystick</h4><dl class="control-bindings numbered"><div><dt><span>1</span> Move / navigate</dt><dd>Joystick directions</dd></div><div><dt><span>2</span> Fire / select</dt><dd>Fire button</dd></div></dl>
    <p class="control-note">On the TAC-2, both orange buttons send Fire. For mines and menu Back, enable keys + joystick and use Player 1's Alt key or Player 2's E key. A supported independent second button can also lay mines.</p></figcaption>
  </figure>
  <figure class="control-card">
    <a href="/assets/img/controls/controller-cd32.svg" aria-label="Open the CD32 controller illustration at full size"><img src="/assets/img/controls/controller-cd32.svg" width="1000" height="720" alt="Redrawn Amiga CD32 gamepad with D-pad, four coloured action buttons, Play and shoulder buttons." loading="lazy"></a>
    <figcaption><h4>CD32 gamepad</h4><dl class="control-bindings numbered">
      <div><dt><span>1</span> Move / navigate</dt><dd>D-pad</dd></div><div><dt><span>2</span> Fire / select</dt><dd>Red</dd></div><div><dt><span>3</span> Mine / back</dt><dd>Blue or green</dd></div><div><dt><span>4</span> Radar toggle</dt><dd>Yellow</dd></div><div><dt><span>5</span> Pause / back</dt><dd>Play</dd></div><div><dt>Previous / next</dt><dd>Shoulder buttons</dd></div>
    </dl><p class="control-note">The shoulders move between menu sections and switch the followed tank during a replay. Play, radar and shoulder shortcuts use Player 1's assigned port.</p></figcaption>
  </figure>
</div>
<p class="control-note">All controller illustrations are redrawn guides. Saved bindings and the selected steering mode take precedence.</p>

<h2 id="online">Join an online game</h2>
<p>Open Network Game to browse public servers or add an address under Private. Players and servers need compatible network protocols and game rules. Use builds from the same source release.</p>
<p>In the lobby, choose a team, pick a tank and mark yourself ready. Player cards show avatars, tanks, ready state and latency. The badges identify the host, team leader and authenticated administrator. A host starts a manual match once the other humans are ready; an automatic server starts according to its configuration.</p>
<p>Select Chat before typing. Keyboard opens the on-screen keyboard for a controller. Tab completes a player's name in chat; <code>@Name</code> or <code>Name:</code> addresses a player. A team heading opens its rename control for the team leader, host or administrator.</p>
<p>Back to Server Lobby keeps your network connection. You can rejoin a running match while your tank remains eligible. After elimination, wait for the next match or watch if spectators are enabled.</p>
<p>On AmigaOS, private connections require a working TCP/IP stack. The HTTPS public list also needs AmiSSL 4.x with its certificates; the current build instructions describe the supported setup.</p>
<h2 id="settings">Graphics, audio and saved settings</h2>
<p>Game Options contains radar detail and team colours. Graphics controls view size, marks, combat effects, explosions, ground fires and weather effects where supported. Disabling weather effects leaves time-of-day lighting intact. Audio has separate Music Volume and Effect Volume meters from mute to level 10.</p>
<p>Desktop settings, profiles, saved maps and replays live under:</p>
<ul>
  <li>macOS: <code>~/Library/Application Support/OpenFirePower/</code></li>
  <li>Windows: <code>%APPDATA%\OpenFirePower\</code></li>
  <li>Linux: <code>~/.local/share/OpenFirePower/</code></li>
</ul>
<p>If something fails, see <a href="/documentation/troubleshooting/">Troubleshooting</a>.</p>
<?php documentation_end();
