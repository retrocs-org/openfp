<?php
// SPDX-License-Identifier: MIT
// What this site keeps, why, and for how long.
require __DIR__ . '/../inc/site.php';
$contact = (string)(site()['contact'] ?? '');
page_start('Privacy', 'privacy', 'What the OpenFP web site keeps, and for how long.');
?>
<section class="wrap prose">
  <h1>Privacy</h1>
  <p class="lead">This site lists game servers, records public matches and offers cloud player accounts. It uses essential session cookies for account and administration pages, with no advertising or tracking cookies.</p>
  <h2>What's kept, and for how long</h2>
  <table class="board"><thead><tr><th>What</th><th>Why</th><th>How long</th></tr></thead><tbody>
    <tr><td>Cloud account: username, email, password hash, identity key and creation date</td><td>Sign-in, recovery and consistent public player identity</td><td>Until the account is deleted</td></tr>
    <tr><td>Hashed client sessions, pairing PINs, activation and reset tokens</td><td>Remember sign-in, confirm email addresses, connect Apple TV and recover passwords</td><td>Sessions: 30 days by default; pairing: 10 minutes; activation links: 24 hours; reset links: 30 minutes. Expired rows are pruned when account services run.</td></tr>
    <tr><td>Hashed rate-limit buckets</td><td>Limit repeated account requests</td><td>At most one day after the last request, pruned when account services run</td></tr>
    <tr><td>Public game servers: their address, name, settings and who's on them right now (player names)</td><td>the server list</td><td>until 3 minutes after the server stops announcing itself</td></tr>
    <tr><td>Player counts per server</td><td>the charts</td><td>7 days</td></tr>
    <tr><td>The addresses servers announce from</td><td>the limit of 60 announcements in 10 minutes</td><td>10 minutes</td></tr>
    <tr><td>Each server's top ten (player names, ratings, wins)</td><td>the leaderboards</td><td>until the server sends a new one; shown for 30 days</td></tr>
    <tr><td>Finished matches on public servers: map, result, player names, sides, scores, ratings, achievements, the replay</td><td>the match history, player pages, ladders, replays</td><td>each server's last 500 matches, at most 90 days</td></tr>
    <tr><td>Shared maps: the map, the name it was shared under, the address it came from</td><td>the gallery; the address for the limit of ten a day</td><td>the map until it's taken out; the address 1 day</td></tr>
  </tbody></table>
  <p>Public matches use the registered account username. Emails, password hashes, identity keys and login sessions are not published. Clients never store your password; game servers receive a short-lived account ticket instead.
     Game servers keep their own records (statistics, chat, bans), under their own admins.</p>
  <h2>Being forgotten</h2>
  <p>Ask<?= $contact ? ' at <a href="' . h($contact) . '">' . h(preg_replace('/^mailto:/', '', $contact)) . '</a>' : ' the site\'s admin' ?>:
     your name comes off the leaderboards, out of the match history (it becomes "a player") and off shared maps.
     For a server's own records, ask that server's admin.</p>
</section>
<?php page_end();
