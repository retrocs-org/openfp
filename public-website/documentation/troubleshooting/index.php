<?php
// SPDX-License-Identifier: MIT
require __DIR__ . '/../../inc/documentation.php';
documentation_start('troubleshooting');
?>
<h2 id="connection">The server is listed, but I cannot join</h2>
<p>Check that the server and client use compatible protocols and rules. Compare their version information and update them together. Then verify the address, join password and UDP port; the default is 29870. Firewalls and router forwarding must allow the game traffic, not only the website connection.</p>
<p>For AmigaOS, verify IPv4 connectivity and the TCP/IP stack. If joining by address works but the Public list fails, check AmiSSL and its certificates.</p>
<h2 id="logs">There is nothing in the service journal</h2>
<p>The dedicated service normally runs with <code>--quiet</code>. Routine activity goes to <code>log_file</code>, usually <code>/var/lib/ofp-server/ofp_server.log</code>. The journal still contains service startup failures and restarts. See <a href="/documentation/server/#service">the service guide</a>.</p>
<h2 id="version">Unknown server settings after an update</h2>
<p>Confirm the version of the installed executable and the path in the service's <code>ExecStart</code>. The freshly compiled file in <code>build-server/</code> can be newer than the copy the service actually runs. Keep your existing configuration when installing the replacement.</p>
<h2 id="controls">Keys or a controller operate the wrong player</h2>
<p>Open Settings → Controls and check the separate Player 1 and Player 2 assignments. Reset to Default restores both keymaps. Sign additional players in with their own controls. Select the Chat field before typing; outside text fields, your Fire and Mine bindings operate menu buttons.</p>
<h2 id="performance">The game feels slow</h2>
<p>Run Tools → Benchmark to compare the same scene and configuration. On AmigaOS, use a 68040 or 68060 with enough Fast RAM. Try simpler explosions, disable lingering ground fires and reduce weather or combat effects where available. The ECS edition stays at 16 colours.</p>
<p>Keep the client, assets and maps from the same build. If a world looks wrong, check for mixed files from older packages before replacing your saved settings.</p>
<h2 id="reporting">Report a problem</h2>
<p>Use <a href="https://github.com/retrocs-org/openfp/issues">GitHub Issues</a>. Include your platform, version, map, game mode and steps to reproduce it. A screenshot, relevant log excerpt or replay can help. Remove passwords, tokens and private addresses before sharing files.</p>
<?php documentation_end();
