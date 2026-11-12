<?php
// SPDX-License-Identifier: MIT
// Every public OpenFP server right now, with its numbers.
require __DIR__ . '/../inc/site.php';

// the match that's on: "On now: 3:12 in · camp 1200 – fortress 900" (empty: none)
function live_line(array $s): string
{
    if (!$s['running'] || $s['elapsed'] === null) return '';
    $t = (int)$s['elapsed'];
    $line = '<span class="tag on">On now</span> ' . sprintf('%d:%02d in', intdiv($t, 60), $t % 60);
    if ($s['time_limit']) $line .= sprintf(' of %d:%02d', intdiv((int)$s['time_limit'], 60), (int)$s['time_limit'] % 60);   /* (seconds) */
    if ($s['score_camp'] !== null)
        $line .= ' · <b>camp ' . (int)$s['score_camp'] . '</b> – <b>fortress ' . (int)$s['score_fortress'] . '</b>';
    if (($s['watching'] ?? '') !== 'off')
        $line .= ' · <a href="/replay/?live=' . h(urlencode($s['host'] . ':' . $s['port'])) . '">Watch live</a>';
    return $line;
}
$cfg = site();
$live = site_servers();
$t = $live['totals'];
page_start('Servers', 'servers', 'Public OpenFP servers right now: players, matches, worlds and maps.');
?>
<section class="wrap">
  <div class="page-head">
    <div>
      <h1>Servers</h1>
      <p class="muted">Public servers announce themselves every minute; the list checks each one is really there.
        Join from the game: <b>NETWORK GAME → QUICK MATCH</b> or <b>FIND SERVERS</b>.</p>
    </div>
    <ul class="stats" data-live>
      <li><b data-k="servers"><?= (int)$t['servers'] ?></b><span>servers</span></li>
      <li><b data-k="players"><?= (int)$t['players'] ?></b><span>players online</span></li>
      <li><b data-k="playing"><?= (int)$t['playing'] ?></b><span>matches on</span></li>
      <li><b data-k="peak_players_24h"><?= (int)$t['peak_players_24h'] ?></b><span>peak today</span></li>
      <li><b data-k="matches"><?= (int)$t['matches'] ?></b><span>matches played</span></li>
    </ul>
  </div>

  <div class="toolbar">
    <input type="search" placeholder="Search servers, worlds, maps" data-filter aria-label="Search">
    <label><input type="checkbox" data-hide-full> Hide full</label>
    <label><input type="checkbox" data-hide-locked> Hide locked</label>
    <select data-limit aria-label="Time limit">
      <option value="">Any match length</option>
      <option value="none">No time limit</option>
      <option value="timed">With a time limit</option>
    </select>
    <select data-bots aria-label="Computer players">
      <option value="">Bots or not</option>
      <option value="none">No computer players</option>
      <option value="some">With computer players</option>
    </select>
    <span class="muted small" data-updated>Updated just now</span>
  </div>

  <?php if (!$live['ok']): ?>
    <div class="empty">The server list can't be reached right now. Try again in a moment.</div>
  <?php endif; ?>
  <div class="servers" data-servers data-api="/api/">
    <?php if ($live['ok'] && !$live['servers']): ?>
      <div class="empty">No public servers right now. <a href="/#host-game">Start one</a>: it takes a minute.</div>
    <?php endif; ?>
    <?php foreach ($live['servers'] as $s):
        $full = $s['players'] >= $s['max_players']; ?>
      <article class="server<?= $full ? ' full' : '' ?><?= $s['password'] ? ' locked' : '' ?>"
               data-limit="<?= $s['time_limit'] ? 'timed' : 'none' ?>" data-bots="<?= ($s['bots'] ?? '0') === '0' ? 'none' : 'some' ?>"
               data-text="<?= h(strtolower($s['name'] . ' ' . $s['world'] . ' ' . $s['map'] . ' ' . $s['mode'])) ?>"
               data-server="<?= h($s['host'] . ':' . $s['port']) ?>">
        <div class="s-main">
          <h3><?= h($s['name']) ?></h3>
          <?php if (!empty($s['motd'])): ?><p class="motd"><?= h($s['motd']) ?></p><?php endif; ?>
          <div class="tags">
            <span class="tag <?= $s['running'] ? 'on' : '' ?>"><?= $s['running'] ? 'In a match' . ($s['match_players'] ? ' · ' . (int)$s['match_players'] . ' playing' : '') : 'Lobby open' ?></span>
            <span class="tag"><?= h($s['mode'] === 'versus' ? 'Versus' : 'Co-op') ?></span>
            <span class="tag mode"><?= h(['conquest' => 'Conquest', 'deathmatch' => 'Team deathmatch', 'annihilation' => 'Annihilation: every tank for itself',
                                          'warlord' => 'Warlord', 'hill' => 'The flag or the hill'][$s['goal'] ?? 'flag'] ?? 'Capture the flag') ?></span>
            <span class="tag"><?= h(['scifi' => 'Moon Base', 'moon' => 'Moon Base', 'volcanic' => 'Volcano'][$s['world']] ?? ucfirst($s['world'])) ?></span>
            <?php if ($s['password']): ?><span class="tag lock">Password</span><?php endif; ?>
            <span class="tag"><?= h($s['maps'] > 1 ? $s['maps'] . ' maps' . ($s['rotation'] === 'vote' ? ', voted' : ($s['rotation'] === 'random' ? ', random' : ''))
                                    : ($s['map'] === 'random' ? 'Random maps' : $s['map'])) ?></span>
            <?php if ($s['time_limit']): ?>
              <span class="tag" title="<?= $s['time_rule'] === 'draw' ? 'When time runs out: a draw' : 'When time runs out: the higher score wins' ?>">
                <?= (int)round($s['time_limit'] / 60) ?> min<?= $s['time_rule'] === 'draw' ? ', then a draw' : '' ?></span>
            <?php endif; ?>
            <?php if (($s['bots'] ?? '0') !== '0' && $s['bots'] !== null): ?>
              <span class="tag"><?= $s['bots'] === 'fill' ? 'Topped up with bots' : (int)$s['bots'] . ' bot' . ((int)$s['bots'] === 1 ? '' : 's') ?></span>
            <?php endif; ?>
            <?php if ($s['watching'] === 'off'): ?><span class="tag">No watching</span>
            <?php elseif ($s['watching'] !== null && (int)$s['watching'] > 0): ?><span class="tag">Watching <?= (int)$s['watching'] ?> s late</span><?php endif; ?>
            <?php if ($s['version']): ?><span class="tag muted">v<?= h($s['version']) ?></span><?php endif; ?>
          </div>
        </div>
        <div class="s-players">
          <div class="big"><?= (int)$s['players'] ?><small>/<?= (int)$s['max_players'] ?></small></div>
          <div class="bar"><i style="width:<?= $s['max_players'] ? min(100, (int)round($s['players'] / $s['max_players'] * 100)) : 0 ?>%"></i></div>
          <div class="muted small">players</div>
        </div>
        <dl class="s-numbers">
          <div><dt>Peak today</dt><dd><?= (int)$s['peak_24h'] ?></dd></div>
          <div><dt>Average</dt><dd><?= h($s['average_24h']) ?></dd></div>
          <div><dt>Matches</dt><dd><?= (int)$s['matches'] ?></dd></div>
          <div><dt>Up for</dt><dd><?= h(duration($s['uptime'])) ?></dd></div>
          <div><dt>Ping (list)</dt><dd><?= $s['ping_ms'] !== null ? (int)$s['ping_ms'] . ' ms' : '-' ?></dd></div>
          <div><dt>Listed</dt><dd><?= h(ago($s['first_seen'])) ?></dd></div>
        </dl>
        <div class="s-chart" title="Players, last 24 hours"><?= sparkline($s['history'], (int)$s['max_players']) ?><span class="muted small">last 24 h</span></div>
        <div class="s-live small"><?= live_line($s) ?></div>
        <?php if (!empty($s['people'])): ?>
          <div class="s-people small"><span class="muted">On now:</span>
            <?php foreach (explode(',', $s['people']) as $i => $pn): ?><?= $i ? ', ' : '' ?><a href="/player/?name=<?= h(urlencode($pn)) ?>&amp;server=<?= h(urlencode($s['host'] . ':' . $s['port'])) ?>"><?= h($pn) ?></a><?php endforeach; ?>
          </div>
        <?php endif; ?>
        <?php $top = site_safe(fn() => ofp_leaders(3, $s['host'], (int)$s['port']), []); ?>
        <div class="s-leaders">
          <?php if ($top): ?>
            <span class="muted small">Best here:</span>
            <?php foreach ($top as $i => $l): ?>
              <span class="leader"><b><?= $i + 1 ?></b> <?= h($l['name']) ?> <span class="muted"><?= (int)$l['rating'] ?></span></span>
            <?php endforeach; ?>
          <?php else: ?><span class="muted small">No rated matches yet.</span><?php endif; ?>
          <a class="small" href="/activity/?view=matches&amp;server=<?= h(urlencode($s['host'] . ':' . $s['port'])) ?>">Matches &rarr;</a>
        </div>
        <div class="s-addr">
          <code><?= h($s['host']) ?>:<?= (int)$s['port'] ?></code>
          <button class="copy" data-copy="<?= h($s['host']) ?>:<?= (int)$s['port'] ?>">Copy</button>
        </div>
      </article>
    <?php endforeach; ?>
  </div>

  <details class="api">
    <summary>For developers: the listing API</summary>
    <p><code>GET <?= h($cfg['base_url']) ?>/api/</code> gives JSON (servers, their last 24 hours, totals),
       <code>?format=text</code> one tab-separated line a server (what the game reads), and
       <code>?action=stats</code> just the totals, <code>?action=matches</code> the match history,
       <code>?action=replay&amp;id=N</code> a match's replay and <code>?action=leaders</code> the ratings.
       Servers announce with <code>POST action=announce</code> and report matches with <code>action=match</code>; see
       <code>public-website/api/index.php</code>.</p>
  </details>
</section>
<script>
// live: every 15 seconds, what's on now (the match, the players) without reloading the page
(function () {
  function line(s) {
    if (!s.running || s.elapsed === null || s.elapsed === undefined) return '';
    var t = +s.elapsed, l = '<span class="tag on">On now</span> ' + Math.floor(t / 60) + ':' + ('0' + t % 60).slice(-2) + ' in';
    if (+s.time_limit) l += ' of ' + Math.floor(+s.time_limit / 60) + ':' + ('0' + (+s.time_limit) % 60).slice(-2);   // (seconds)
    if (s.score_camp !== null && s.score_camp !== undefined) l += ' · <b>camp ' + (+s.score_camp) + '</b> – <b>fortress ' + (+s.score_fortress) + '</b>';
    if (s.watching !== 'off') l += ' · <a href="/replay/?live=' + encodeURIComponent(s.host + ':' + s.port) + '">Watch live</a>';
    return l;
  }
  function refresh() {
    fetch('/api/?format=json').then(function (r) { return r.json(); }).then(function (d) {
      (d.servers || []).forEach(function (s) {
        var card = document.querySelector('article.server[data-server="' + s.host + ':' + s.port + '"]');
        if (!card) return;
        var live = card.querySelector('.s-live'), big = card.querySelector('.s-players .big');
        if (live) live.innerHTML = line(s);
        if (big) big.innerHTML = (+s.players) + '<small>/' + (+s.max_players) + '</small>';
      });
    }).catch(function () {});
  }
  setInterval(refresh, 15000);
})();
</script>
<?php page_end();
