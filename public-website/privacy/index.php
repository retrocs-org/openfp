<?php
// SPDX-License-Identifier: MIT
// What this site keeps, why, and for how long.
require __DIR__ . '/../inc/site.php';
$contact = (string)(site()['contact'] ?? '');
page_start('Privacy', 'privacy', 'What the OpenFP web site keeps, and for how long.');
?>
<section class="wrap prose">
  <h1>Privacy</h1>
  <p class="lead">This site lists game servers and shows how their matches went. It has no accounts, no
     tracking and no cookies for visitors (the admin page sets one, for its admin only).</p>
  <h2>What's kept, and for how long</h2>
  <table class="board"><thead><tr><th>What</th><th>Why</th><th>How long</th></tr></thead><tbody>
    <tr><td>Public game servers: their address, name, settings and who's on them right now (player names)</td><td>the server list</td><td>until 3 minutes after the server stops announcing itself</td></tr>
    <tr><td>Player counts per server</td><td>the charts</td><td>7 days</td></tr>
    <tr><td>The addresses servers announce from</td><td>the limit of 60 announcements in 10 minutes</td><td>10 minutes</td></tr>
    <tr><td>Each server's top ten (player names, ratings, wins)</td><td>the leaderboards</td><td>until the server sends a new one; shown for 30 days</td></tr>
    <tr><td>Finished matches on public servers: map, result, player names, sides, scores, ratings, achievements, the replay</td><td>the match history, player pages, ladders, replays</td><td>each server's last 500 matches, at most 90 days</td></tr>
    <tr><td>Shared maps: the map, the name it was shared under, the address it came from</td><td>the gallery; the address for the limit of ten a day</td><td>the map until it's taken out; the address 1 day</td></tr>
  </tbody></table>
  <p>Player names are whatever players call themselves in the game; the site never learns more about them.
     Game servers keep their own records (statistics, chat, bans), under their own admins.</p>
  <h2>Being forgotten</h2>
  <p>Ask<?= $contact ? ' at <a href="' . h($contact) . '">' . h(preg_replace('/^mailto:/', '', $contact)) . '</a>' : ' the site\'s admin' ?>:
     your name comes off the leaderboards, out of the match history (it becomes "a player") and off shared maps.
     For a server's own records, ask that server's admin.</p>
</section>
<?php page_end();
