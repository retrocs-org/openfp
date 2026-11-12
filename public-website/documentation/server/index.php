<?php
// SPDX-License-Identifier: MIT
require __DIR__ . '/../../inc/documentation.php';
documentation_start('server');
?>
<nav class="documentation-contents" aria-label="On this page"><a href="#start">Start a server</a><a href="#configuration">Configuration</a><a href="#network">Network access</a><a href="#service">System service</a><a href="#administration">Administration</a><a href="#updating">Updates</a></nav>
<h2 id="start">Start a server</h2>
<p>The Linux dedicated server runs without SDL or a desktop. openSUSE Leap 16 is the preferred Linux platform. Download the server archive for your architecture from <a href="/#server-downloads">Get the game</a>, or follow <a href="/source/#server">the compilation instructions</a>.</p>
<pre class="code"><code>sudo zypper install libcurl4 libsqlite3-0 ca-certificates
cd OpenFirePowerServer
cp -n server.cfg.example server.cfg
${EDITOR:-vi} server.cfg
./ofp_server -c server.cfg --check
./ofp_server -c server.cfg</code></pre>
<p>The copy command is for a new installation and leaves an existing configuration alone. Keep running from this directory: map, database and log paths are relative to the server's working directory. Stop the foreground server with Ctrl+C.</p>
<h2 id="configuration">Configure your game</h2>
<p>The supplied example enables public listing, match sharing and replay sharing. For a private game, begin with:</p>
<pre class="code"><code>name = My OpenFP server
public = no
share_matches = no
share_replays = no
max_players = 8
min_players = 1
auto_start = no
start_after = 0
allow_bots = yes
bots = 0
fill_bots = yes
time_limit = 20
lives = 5</code></pre>
<p>Add a <code>password</code> to restrict joining. For a public server, set <code>public = yes</code> and decide whether to enable result and replay sharing. Run <code>--check</code> after changes.</p>
<div class="documentation-table"><table><thead><tr><th>Settings</th><th>Purpose</th></tr></thead><tbody>
<tr><td><code>name</code>, <code>motd</code>, <code>password</code></td><td>Server identity, welcome message and optional join password.</td></tr>
<tr><td><code>port</code>, <code>public</code>, <code>public_host</code>, <code>listing_url</code></td><td>UDP port and public listing. The default port is 29870. <code>public = code</code> provides an unlisted join code.</td></tr>
<tr><td><code>max_players</code>, <code>min_players</code></td><td>Lobby capacity and humans needed to start. A match has at most eight tanks including bots, constrained by the map.</td></tr>
<tr><td><code>auto_start</code>, <code>countdown</code>, <code>start_after</code></td><td>Automatic start and waiting times. With auto-start disabled, the host or administrator starts once the other humans are ready.</td></tr>
<tr><td><code>allow_bots</code>, <code>bots</code>, <code>fill_bots</code>, <code>bot_level</code></td><td>Enable bots, reserve a fixed number or fill unused match seats, and choose their difficulty.</td></tr>
<tr><td><code>world</code>, <code>goal</code>, <code>map</code>, <code>maps</code>, <code>rotation</code></td><td>World and game objective, one fixed map or a map folder, and ordered, random or voted rotation. A fixed map supplies its own world and rules.</td></tr>
<tr><td><code>time_limit</code>, <code>lives</code>, <code>pickups</code></td><td>Matches last 1–30 minutes. Lives can be 1–99 or <code>unlimited</code>. Pickups apply only to enhanced worlds; Classic disables them.</td></tr>
<tr><td><code>allow_watching</code>, <code>spectator_delay</code></td><td>Permit spectators and delay their view.</td></tr>
<tr><td><code>database</code>, <code>log_file</code>, <code>log_level</code>, <code>chat_log</code></td><td>Statistics, logging and chat history. Retention and backup controls are in the complete example.</td></tr>
<tr><td><code>replays</code>, <code>replays_keep</code>, <code>replays_max_mb</code></td><td>Replay directory and storage limits. An empty replay directory setting disables server replay storage.</td></tr>
</tbody></table></div>
<p>The <a href="https://github.com/retrocs-org/openfp/blob/main/server.cfg.example">complete configuration example</a> documents all settings, including logging, backups, metrics, webhooks and network traversal.</p>
<h2 id="network">Network access</h2>
<p>Allow the configured UDP port through the host firewall. For internet players behind a router, forward that port to the server. Public listing uses outbound HTTPS to <code>https://openfp.retrocs.org/api/</code>. Set <code>public_host</code> when the advertised address must differ from the detected address.</p>
<p>AmigaOS uses IPv4. To accept AmigaOS clients, make sure the server has a reachable IPv4 address. A visible public listing does not by itself prove that inbound game traffic can reach the UDP port.</p>
<h2 id="service">Run as a system service</h2>
<p>The source package includes a service installer. After building and configuring the server, run these commands from the source root. The <code>--from</code> directory must contain the configuration and data you intend to use.</p>
<pre class="code"><code>sudo sh tools/install_server_service.sh --from "$PWD" --no-start
sudo systemctl enable --now ofp-server
sudo systemctl status ofp-server
sudo tail -F /var/lib/ofp-server/ofp_server.log</code></pre>
<p>The service uses <code>/etc/ofp-server/server.cfg</code> and stores runtime data under <code>/var/lib/ofp-server/</code>. With <code>--quiet</code>, routine game messages go to the configured log file. Service startup failures appear in <code>journalctl -u ofp-server</code>.</p>
<h2 id="administration">Lobby and administration</h2>
<p>The first connected human is the lobby host. If that player leaves, the longest-connected human takes over. The first human to choose a team leads that team; leadership passes to the next human on that team when they leave or switch.</p>
<p>The host or an authenticated administrator can start a manual match. Team leaders can rename their own team; the host and administrator can rename either team. Enhanced team-order features do not apply under Classic rules.</p>
<p>Set <code>admin_password</code> to enable remote administration, then use <code>/login PASSWORD</code> in client chat. The console's <code>help</code> command lists commands such as <code>status</code>, <code>players</code>, <code>say</code>, <code>kick</code>, <code>maps</code> and <code>restart</code>.</p>
<h2 id="updating">Update the running server</h2>
<p>Use matching client and server releases. Rebuilding in a source directory does not replace the service's executable. Check its actual path before installing an update:</p>
<pre class="code"><code>systemctl show -p ExecStart --value ofp-server
/usr/bin/ofp_server --version</code></pre>
<p>Install the newly built executable at the path shown by the service, preserve your configuration and database, and restart the service. Confirm the installed version and check the log. Use <code>systemctl reload ofp-server</code> for configuration changes supported by reload; changing the UDP port requires a restart.</p>
<?php documentation_end();
