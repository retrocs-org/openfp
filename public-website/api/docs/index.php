<?php
// SPDX-License-Identifier: MIT
require_once dirname(__DIR__, 2) . '/inc/site.php';
header('Content-Type: text/html; charset=utf-8');
page_start('API documentation', 'api', 'Use the OpenFP API for public servers, maps, statistics, replays and dedicated-server reporting.');
?>
<section class="wrap prose api-docs">
  <p class="eyebrow">For clients, server operators and integrations</p>
  <h1>API documentation</h1>
  <p class="lead">Find a server, browse maps, read match results or connect your dedicated server to the public listing.</p>
  <nav class="section-nav" aria-label="API sections"><a href="#basics">Getting started</a><a href="#read">Read data</a><a href="#report">Server reporting</a><a href="#accounts">Cloud accounts</a><a href="#uploads">Maps &amp; daily runs</a><a href="#join">Join codes</a><a href="#errors">Errors</a></nav>

  <h2 id="basics">Getting started</h2>
  <p>Base URL: <code>https://openfp.retrocs.org/api/</code>. Public reads need no API key. The API lists servers and shares results; matches themselves use the game’s UDP protocol, not this HTTP API.</p>
  <pre class="code"><code>curl 'https://openfp.retrocs.org/api/?action=list'
curl 'https://openfp.retrocs.org/api/?action=maps&amp;world=desert&amp;mode=flag'
curl 'https://openfp.retrocs.org/api/?action=matches&amp;limit=10'</code></pre>
  <p>Use <code>action</code> to select an endpoint. GET responses are JSON unless the reference below says text or file. Send POST fields as <code>application/x-www-form-urlencoded</code>; JSON request bodies are not supported. All text uses UTF-8. Times such as <code>generated</code>, <code>uploaded</code> and <code>ended</code> are Unix seconds; daily challenge dates use UTC <code>YYYYMMDD</code>.</p>
  <p>A browser visiting <a href="/api/">/api/</a> opens this guide. Scripts should use an explicit action or <code>format=json</code>/<code>format=text</code>. The bare URL still returns the server list to clients without an HTML Accept header. The guide is also always available at <a href="/api/docs/">/api/docs/</a>.</p>
  <p>Use HTTPS where available. HTTP remains supported for older clients. Refresh listings about once a minute and honour cache headers on map previews, replays and version information. JSON helper responses allow cross-origin reads; form posts are intended for game clients and servers.</p>

  <h2 id="read">Read data</h2>
  <p>All endpoints in this table use GET. Parameters shown as “required” must be provided. Other parameters are optional.</p>
  <div class="wrap-table"><table><thead><tr><th>Action</th><th>Parameters</th><th>Response</th></tr></thead><tbody>
    <tr><td><code>list</code> (default)</td><td><code>format=json</code> (default) or <code>text</code></td><td><code>{servers: […], totals: {…}, generated: …}</code>. Only public, current servers are included.</td></tr>
    <tr><td><code>stats</code></td><td>None</td><td>Totals: <code>servers</code>, <code>players</code>, <code>playing</code>, <code>peak_players_24h</code>, <code>matches</code>.</td></tr>
    <tr><td><code>leaders</code></td><td><code>server=HOST:PORT</code>, <code>limit</code> (default 50, 1–200)</td><td><code>{leaders: […], generated: …}</code>. Entries include player name, rating, wins, matches, score and server identity. Without a server filter, returns recently reported leaders across servers.</td></tr>
    <tr><td><code>rating</code></td><td><code>name</code></td><td>Plain-text integer: highest reported rating for that name over the last 30 days; 0 if unknown.</td></tr>
    <tr><td><code>matches</code></td><td><code>server=HOST:PORT</code>, <code>limit</code> (default 50, 1–200), <code>before</code> (Unix seconds)</td><td><code>{matches: […], generated: …}</code>, newest first. Use <code>before</code> to request older results.</td></tr>
    <tr><td><code>replay</code></td><td><code>id</code> (required match ID)</td><td>A downloadable <code>.ofprep</code> replay, or 404 if unavailable.</td></tr>
    <tr><td><code>live</code></td><td><code>server=HOST:PORT</code> (required)</td><td>Plain-text replay of the current match, with the server’s spectator delay. Returns 404 if no upload was received in the last 60 seconds.</td></tr>
    <tr><td><code>daily</code></td><td><code>day=YYYYMMDD</code> (default today, UTC)</td><td><code>{day: …, setup: {…}, results: […]}</code>. The setup identifies the challenge; results include completion times and verification status.</td></tr>
    <tr><td><code>daily_replay</code></td><td><code>id</code> (required result ID), <code>download=1</code></td><td>Plain-text daily replay; <code>download=1</code> supplies an attachment filename.</td></tr>
    <tr><td><code>maps</code></td><td><code>world</code>, <code>mode</code>, <code>format=text</code></td><td><code>{maps: […], generated: …}</code>, up to 300 community maps. Both filters apply together. Includes ID, name, author, world, time of day, file size in bytes, upload time, downloads, description, contact and compatible <code>modes</code>.</td></tr>
    <tr><td><code>map</code></td><td><code>id</code> or <code>name</code> (required), <code>download=1</code></td><td>The <code>.ofpmap</code> text. A name selects the newest visible map with that name. Download mode adds a filename and increments its download count.</td></tr>
    <tr><td><code>map_thumbs</code></td><td><code>ids=1,2,3</code>, up to 60 map IDs</td><td>A JSON object keyed by map ID. Each value is a 4,096-character tile preview, laid out as 64 rows of 64 tiles. Missing IDs are omitted.</td></tr>
    <tr><td><code>join</code></td><td><code>code</code> (required join code)</td><td>Plain text: <code>host&lt;TAB&gt;port</code>, optionally followed by <code>&lt;TAB&gt;public-ip&lt;TAB&gt;public-port</code> for NAT traversal.</td></tr>
    <tr><td><code>version</code></td><td><code>format=json</code> or <code>text</code> (default)</td><td>Text: <code>VERSION&lt;TAB&gt;DOWNLOAD-URL</code>. JSON: <code>{version: …, download: …, protocol: …}</code>. Version is empty before the first stable binary release. This endpoint does not need the listing database.</td></tr>
  </tbody></table></div>
  <h3>Filtering maps</h3>
  <p>World keys currently accepted by the <a href="/maps/">map browser</a>: <code><?= h(implode(', ', array_keys(OFP_MAP_WORLDS))) ?></code>. A gallery may also contain legacy world IDs. Mode keys: <code><?= h(implode(', ', array_keys(OFP_MAP_MODES))) ?></code>.</p>
  <p>Custom maps use their declared game mode. Older map packs group Hardpoint with Conquest, and Kill Confirmed with Team Deathmatch. Legacy Classic maps are labelled Classic. The API gallery contains player uploads; the website also shows bundled maps from its generated catalog.</p>
  <h3>Plain-text formats</h3>
  <p>For <code>action=list&amp;format=text</code>, each server is one line with these 15 tab-separated fields. Empty fields still keep their separators.</p>
  <pre class="code"><code>host  port  name  players  max_players  password  running  mode  world  map  build  ping_ms  people  goal  rating</code></pre>
  <p><code>password</code> and <code>running</code> are 0/1. <code>mode</code> is <code>versus</code> or <code>together</code>; <code>goal</code> identifies the game mode, such as <code>flag</code> or <code>conquest</code>. <code>people</code> is a comma-separated name list. <code>rating</code> is the average of known ratings on that server, or 0 if unknown.</p>
  <p>For <code>action=maps&amp;format=text</code>, each map has seven tab-separated fields: <code>id, name, author, world, time, size, downloads</code>. Compatible modes are available in JSON; no columns have been added to the legacy text format.</p>
  <p>JSON clients should accept additional fields. Match entries include server identity, map, world, duration, winner and player results. Match player results are returned as nested arrays.</p>

  <h2 id="accounts">Cloud accounts</h2>
  <p>Account requests use <code>POST https://openfp.retrocs.org/account/api/</code> with an URL-encoded body. HTTPS is required. Replies are newline-separated <code>key=value</code> fields with <code>Cache-Control: no-store</code>. Never log passwords, session tokens or server secrets.</p>
  <div class="wrap-table"><table><thead><tr><th>Action</th><th>Fields</th><th>Reply</th></tr></thead><tbody>
  <tr><td><code>register</code></td><td><code>username, email, password</code></td><td>202 with <code>status=verification_required</code> and <code>message</code> when activation is required; no credentials until activated. Otherwise 200 with <code>id, username, key, session</code>.</td></tr>
  <tr><td><code>login</code></td><td><code>username, password</code></td><td><code>id, username, key, session</code></td></tr>
  <tr><td><code>device_start</code></td><td>None; Apple TV pairing</td><td><code>device, pin, url</code>; ten-minute lifetime</td></tr>
  <tr><td><code>device_poll</code></td><td><code>device</code>; poll every five seconds</td><td>202 with <code>status=pending</code>, or 200 with account fields once approved; single use</td></tr>
  <tr><td><code>ticket</code></td><td><code>session, server, server_id, nonce, key_id</code></td><td><code>ticket</code>; 60-second, single-use server challenge ticket</td></tr>
  <tr><td><code>verify</code></td><td><code>server, secret, ticket, server_id, nonce, key_id</code>; registered server only</td><td><code>id, username</code></td></tr>
  <tr><td><code>logout</code></td><td><code>session</code></td><td><code>status=signed out</code>; revokes that session</td></tr>
  </tbody></table></div>
  <p>Errors contain <code>error</code>: 400 invalid fields or email activation still required, 401 bad credentials or expired session, 403 invalid server/ticket, 413 oversized body, 426 HTTPS required, 429 rate limit, 503 unavailable. Account bodies are limited to 4 KiB. Client sessions last 30 days by default. The password minimum is configurable (default 12 characters; maximum 72 bytes). Email activation links last 24 hours; confirmation and resending use the <a href="/account/">account page</a>. PIN approval uses the browser page <a href="/account/pin">/account/pin</a>, with account sign-in and CSRF protection. Read the <a href="/documentation/accounts/">account guide</a> for identity-key derivation and operator setup.</p>

  <h2 id="report">Dedicated-server reporting</h2>
  <p>All server reporting and polling requests require HTTPS and <code>cloud_server</code> plus <code>cloud_secret</code>, issued by the site operator. These are separate from player credentials. A registration owns its announced host/port. See <a href="/documentation/accounts/">server registration</a>.</p>
  <p>The game server handles these calls automatically when public listing and sharing are enabled. They are described here for integrations. Requests are bound to the caller’s public IP and UDP port. An optional <code>host</code> must resolve to that IP when hostname announcements are enabled. A listed server must announce before reporting a match or live replay.</p>
  <p>Announce about every 60 seconds. A normal announcement is checked by querying the game server over UDP; verified values replace the submitted status. The game port must be reachable. Entries expire after 180 seconds by default if announcements stop.</p>
  <pre class="code"><code>curl 'https://openfp.retrocs.org/api/' \
  --data-urlencode 'action=announce' \
+  --data-urlencode 'cloud_server=YOUR_REGISTRATION' \
+  --data-urlencode 'cloud_secret=YOUR_SECRET' \
  --data-urlencode 'port=29870' \
  --data-urlencode 'name=My OpenFP server' \
  --data-urlencode 'mode=versus' \
  --data-urlencode 'goal=flag'</code></pre>
  <h3>POST action=announce</h3>
  <div class="wrap-table"><table><thead><tr><th>Field</th><th>Meaning and limits</th></tr></thead><tbody>
    <tr><td><code>port</code></td><td>Required UDP port, 1–65535.</td></tr>
    <tr><td><code>host</code></td><td>Optional public hostname; must resolve to the caller’s public IP.</td></tr>
    <tr><td><code>name, build, players, max_players, password, running, mode, world, map</code></td><td>Status fields, checked against the UDP reply. Name ≤40 characters; map ≤31; world ≤15. Booleans use 0/1; mode is <code>versus</code> or <code>together</code>. Current matches support up to eight tanks, including bots.</td></tr>
    <tr><td><code>motd, matches, uptime, version</code></td><td>Message ≤200 characters; completed match count; uptime in seconds; numeric dotted release version, ≤15 characters.</td></tr>
    <tr><td><code>time_limit, time_rule</code></td><td>Time limit in seconds, clamped to 60–1800 (default 1200; 0 also uses 1200). Time rule is <code>draw</code> or <code>points</code> (default).</td></tr>
    <tr><td><code>bots, watching</code></td><td>Bots: <code>fill</code> or 0–7 (default 0). Watching: <code>off</code> or spectator delay in seconds, 0–999.</td></tr>
    <tr><td><code>maps, rotation, goal</code></td><td>Map count, 0–999; rotation <code>order</code> (default), <code>random</code> or <code>vote</code>; goal <code>flag</code> (default), <code>conquest</code>, <code>hardpoint</code>, <code>deathmatch</code>, <code>killconfirmed</code>, <code>annihilation</code> or <code>warlord</code>. Legacy <code>hill</code> is also accepted.</td></tr>
    <tr><td><code>elapsed, score_camp, score_fortress, people</code></td><td>Current match elapsed seconds, the two team scores, and comma-separated player names (≤15 characters each).</td></tr>
    <tr><td><code>leaders</code></td><td>Up to ten newline-separated leaderboard rows: <code>name&lt;TAB&gt;rating&lt;TAB&gt;wins&lt;TAB&gt;matches&lt;TAB&gt;score</code>. Score is optional; names ≤15, ratings 0–4000.</td></tr>
    <tr><td><code>hidden, stun</code></td><td><code>hidden=1</code> keeps the server out of public lists but allows a join code. Optional STUN value: <code>public-ip public-port</code>, with the same public IP as the caller. A hidden server with a valid STUN endpoint may register without a successful probe.</td></tr>
  </tbody></table></div>
  <p>Success: <code>ok listed as HOST:PORT code CODE</code>, or <code>ok known as HOST:PORT code CODE</code> for a hidden server. Failures include 403 for a mismatched hostname and 409 when the UDP probe gets no answer.</p>
  <h3>POST action=remove</h3>
  <p>Fields: <code>port</code> (required), <code>host</code> (optional). Removes the caller’s server listing. Success: <code>ok removed</code>.</p>
  <h3>POST action=match</h3>
  <p>Fields: <code>port</code>, optional <code>host</code>, <code>seconds</code>, <code>mode</code>, <code>world</code>, <code>map</code>, <code>goal</code>, <code>winner</code>, <code>players</code> and optional <code>replay</code>. Winner is <code>camp</code>, <code>fortress</code>, <code>draw</code> or <code>none</code>. The server must already be listed. At least one player result is required, with a maximum of eight.</p>
  <p>Each player occupies a newline-separated row with these tab-separated fields:</p>
  <pre class="code"><code>name  side  won  kills  score  rating  change  bot  tank  ach  account_id</code></pre>
  <p>Human results require the verified 32-character <code>account_id</code>; the canonical username replaces the submitted name. Bots leave this field empty. Side is <code>camp</code> or <code>fortress</code>; won and bot are 0/1. Name ≤15 characters, rating 0–4000, change −100 to 100. Optional legacy tank IDs are 0–2; <code>ach</code> is a 16-bit achievement mask. Replay text must begin with <code>OFPREPLAY 1</code> or <code>OFPREPLAY 2</code> and be ≤3,000,000 bytes. Invalid optional replay data is omitted while the match result is still stored. Success: <code>ok match ID</code>.</p>
  <h3>POST action=live</h3>
  <p>Fields: <code>port</code>, optional <code>host</code>, and <code>replay</code> (≤3,000,000 bytes, valid replay header). Replaces the listed server’s shared live replay. Send <code>over=1</code> instead of a replay when the match ends. Responses: <code>ok live</code> or <code>ok over</code>.</p>

  <h2 id="uploads">Maps and daily challenge runs</h2>
  <h3>POST action=map_upload</h3>
  <p>Send <code>map</code> as the complete <code>.ofpmap</code> text. Optional <code>author</code> is a fallback when the map has no author. The map header supplies its name, description, author, public contact details, world, time of day and game-mode metadata. Contact information becomes public when uploaded.</p>
  <pre class="code"><code>curl 'https://openfp.retrocs.org/api/' \
  --data-urlencode 'action=map_upload' \
  --data-urlencode 'map@my-map.ofpmap'</code></pre>
  <p>The map must begin with <code>OFPMAP 1</code> and contain a tiles section. Limits: 200,000 bytes, 640 structures, 2,000 line breaks and ten uploads per IP per day. The site runs the native map validator before publishing. A duplicate map returns its existing ID. Success: <code>ok map ID</code>, optionally followed by <code>(it was here already)</code>. Invalid files return 400; an unavailable validator returns 503.</p>
  <h3>POST action=daily_submit</h3>
  <p>Fields: <code>day=YYYYMMDD</code>, <code>name</code> (≤15 characters), <code>replay</code> (≤3,000,000 bytes). Only today’s or yesterday’s UTC challenge is accepted. The replay must pass the challenge checks; when the background verifier is enabled, the result is queued before final verification.</p>
  <pre class="code"><code>curl 'https://openfp.retrocs.org/api/' \
  --data-urlencode 'action=daily_submit' \
  --data-urlencode 'day=YYYYMMDD' \
  --data-urlencode 'name=YourName' \
  --data-urlencode 'replay@daily-run.ofprep'</code></pre>
  <p>Replace <code>YYYYMMDD</code> with the challenge date. Success is <code>ok place N of M</code>, possibly with <code>(being checked)</code>, or <code>ok kept (your earlier win was faster)</code>. Up to 20 queued runs per IP per ten minutes are accepted, subject to the shared request limit.</p>

  <h2 id="join">Join codes and NAT traversal</h2>
  <p>First use GET <code>action=join&amp;code=CODE</code> to resolve a server. These HTTP calls assist a UDP connection; they do not replace the game’s network handshake.</p>
  <dl>
    <dt>POST action=punch</dt><dd>Fields: <code>code</code> and <code>port</code>, or <code>ports</code> containing up to four comma-separated UDP ports (1–65535). The request’s public IP is used; a client cannot supply another IP. Success: <code>ok</code>. At most 20 pending endpoints per IP, removed after 60 seconds.</dd>
    <dt>POST action=punches</dt><dd>Server fields: <code>port</code> and optional <code>host</code>. Returns recent pending peers as <code>IP&lt;TAB&gt;PORT</code> lines, followed by <code>ok</code>. Polling consumes the returned entries. Only requests from the last 30 seconds are returned.</dd>
  </dl>

  <h2 id="errors">Errors, limits and compatibility</h2>
  <p>POST successes and errors use plain-text messages. Read endpoints also return plain text on failure, so check the HTTP status before parsing JSON. Most data endpoints require the listing database; the documentation and release-version endpoint remain available without it.</p>
  <div class="wrap-table"><table><thead><tr><th>Status</th><th>Meaning</th></tr></thead><tbody>
    <tr><td>200</td><td>Success. An empty list is a valid result.</td></tr>
    <tr><td>400</td><td>Invalid fields, unknown action, invalid map or rejected daily run.</td></tr>
    <tr><td>403</td><td>Caller identity does not match the announced host, or a server has not announced yet.</td></tr>
    <tr><td>404</td><td>Map, replay, live match or join code is not available.</td></tr>
    <tr><td>409</td><td>The listing could not reach the game server over UDP.</td></tr>
    <tr><td>429</td><td>Rate limit reached. Wait before retrying.</td></tr>
    <tr><td>503</td><td>The listing database or required validator is unavailable.</td></tr>
  </tbody></table></div>
  <p>The shared POST limit defaults to 60 requests per public IP per ten minutes, excluding punch/punches, which have their own queue limits. Uploads and daily runs have additional limits above. Server operators may configure different listing limits.</p>
  <p>Keep credentials out of API URLs. Gallery moderation and privacy requests use the separate session-protected admin page. There is no public token-in-query moderation endpoint.</p>
  <p>The listing API and the game’s UDP protocol are separate interfaces. Game clients and servers must use compatible protocol and rules versions. See the <a href="/source/">source and build guide</a> for the server configuration and native file formats.</p>
</section>
<?php page_end();
