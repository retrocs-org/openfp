<?php
// SPDX-License-Identifier: MIT
require __DIR__ . '/../../inc/documentation.php';
documentation_start('clients');
?>
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
<div class="documentation-table"><table><thead><tr><th>Action</th><th>Desktop Player 1</th><th>Desktop Player 2</th><th>AmigaOS Player 1</th></tr></thead><tbody>
<tr><td>Move and turn</td><td>Arrow keys</td><td>W A S D</td><td>Arrow keys</td></tr>
<tr><td>Fire / select</td><td>Period (<code>.</code>)</td><td>Space</td><td>Left Amiga</td></tr>
<tr><td>Mine / back</td><td>Comma (<code>,</code>)</td><td>E</td><td>Left Alt</td></tr>
</tbody></table></div>
<p>Settings → Controls provides separate keymaps for Players 1 and 2, controller or joystick selection and Reset to Default. Player 1's directions navigate menus; Fire selects and Mine goes back. Enter and Escape also work where shown. Desktop Players 3 and 4 use controllers.</p>
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
