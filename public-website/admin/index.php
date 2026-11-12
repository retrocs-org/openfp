<?php
// SPDX-License-Identifier: MIT
// The admin page: take maps out of the gallery (and back), forget a player. Logged in
// with 'admin_token' from config.inc.php, kept in a session; every change is a POST with
// a per-session check, and too many wrong tries make it wait.
require __DIR__ . '/../inc/site.php';

$token = (string)(ofp_settings()['site']['admin_token'] ?? '');
session_set_cookie_params(['httponly' => true, 'samesite' => 'Strict', 'secure' => !empty($_SERVER['HTTPS']) || ($_SERVER['HTTP_X_FORWARDED_PROTO'] ?? '') === 'https']);
session_start();
$msg = '';
if ($token === '') { page_start('Admin', 'admin'); echo '<section class="wrap"><h1>Admin</h1><p class="notice">There\'s no admin page here: set <code>admin_token</code> in config.inc.php.</p></section>'; page_end(); exit; }
if (empty($_SESSION['csrf'])) $_SESSION['csrf'] = bin2hex(random_bytes(16));
$post = $_SERVER['REQUEST_METHOD'] === 'POST';
if ($post && !hash_equals($_SESSION['csrf'], (string)($_POST['csrf'] ?? ''))) { http_response_code(400); exit('stale form: reload'); }

if ($post && isset($_POST['login'])) {                   // logging in
    $_SESSION['tries'] = ($_SESSION['tries'] ?? 0) + 1;
    if ($_SESSION['tries'] > 5 && time() - ($_SESSION['last_try'] ?? 0) < 60) $msg = 'Too many tries: wait a minute.';
    elseif (hash_equals($token, (string)$_POST['token'])) { session_regenerate_id(true); $_SESSION['admin'] = 1; $_SESSION['tries'] = 0; }
    else { $msg = 'No.'; error_log('openfirepower admin: a wrong password from ' . ofp_client_ip()); }
    $_SESSION['last_try'] = time();
}
if ($post && isset($_POST['logout'])) { $_SESSION = []; session_destroy(); header('Location: /admin/'); exit; }
$admin = !empty($_SESSION['admin']);

if ($admin && $post && isset($_POST['map'])) {           // a map out of the gallery, or back
    ofp_db()->prepare('UPDATE gallery SET hidden = ? WHERE id = ?')->execute([(int)!empty($_POST['hide']), (int)$_POST['map']]);
    error_log('openfirepower admin: map ' . (int)$_POST['map'] . (empty($_POST['hide']) ? ' back in' : ' taken out of') . ' the gallery');
    $msg = !empty($_POST['hide']) ? 'Taken out of the gallery.' : 'Back in the gallery.';
}
if ($admin && $post && isset($_POST['forget']) && trim($_POST['forget']) !== '') {   // forget a player
    $nm = ofp_clean((string)$_POST['forget'], 15);
    $sv = preg_match('/^\[?([A-Za-z0-9.\-:]+?)\]?:(\d{1,5})$/', $_POST['server'] ?? '', $mm) ? [$mm[1], (int)$mm[2]] : [null, null];
    $n = ofp_forget($nm, $sv[0], $sv[1]);
    error_log("openfirepower admin: forgot $nm" . ($sv[0] ? " on {$sv[0]}:{$sv[1]}" : ''));
    $msg = "Forgot $nm: {$n['leaders']} leaderboard places, {$n['matches']} matches, {$n['maps']} maps.";
}
page_start('Admin', 'admin');
$csrf = h($_SESSION['csrf']);
?>
<section class="wrap">
  <h1>Admin</h1>
  <?php if ($msg): ?><p class="notice"><?= h($msg) ?></p><?php endif; ?>
  <?php if (!$admin): ?>
    <form method="post" class="admin-form">
      <input type="hidden" name="csrf" value="<?= $csrf ?>"><input type="hidden" name="login" value="1">
      <label>Password <input type="password" name="token" autocomplete="current-password" autofocus></label>
      <button class="btn">Log in</button>
    </form>
  <?php else: ?>
    <form method="post" style="float:right"><input type="hidden" name="csrf" value="<?= $csrf ?>"><button class="btn small" name="logout" value="1">Log out</button></form>
    <h2>Forget a player</h2>
    <p class="muted small">Takes them off the leaderboards, turns their name in the match history into "a player"
       and takes it off shared maps. On one server, or (leave it empty) on all. Servers keep their own records:
       ask the server's admin too (<code>ofp_server</code>'s database).</p>
    <form method="post" class="admin-form">
      <input type="hidden" name="csrf" value="<?= $csrf ?>">
      <label>Name <input name="forget" maxlength="15" required></label>
      <label>Server <input name="server" placeholder="host:port (or empty: all)"></label>
      <button class="btn" onclick="return confirm('Forget this player? It can\'t be undone.')">Forget</button>
    </form>
    <h2>The gallery</h2>
    <table class="board"><thead><tr><th>Map</th><th>By</th><th>When</th><th class="n">Downloads</th><th></th></tr></thead><tbody>
    <?php foreach (site_safe(fn() => ofp_db()->query('SELECT id, name, author, uploaded, downloads, hidden FROM gallery ORDER BY uploaded DESC LIMIT 300')->fetchAll(), []) as $g): ?>
      <tr class="<?= $g['hidden'] ? 'muted' : '' ?>"><td><?= h($g['name']) ?><?= $g['hidden'] ? ' (taken out)' : '' ?></td><td><?= h($g['author']) ?></td>
        <td><?= h(ago((int)$g['uploaded'])) ?></td><td class="n"><?= (int)$g['downloads'] ?></td>
        <td><form method="post"><input type="hidden" name="csrf" value="<?= $csrf ?>"><input type="hidden" name="map" value="<?= (int)$g['id'] ?>">
            <?php if ($g['hidden']): ?><button class="btn small">Put back</button><?php else: ?><button class="btn small" name="hide" value="1">Take out</button><?php endif; ?></form></td></tr>
    <?php endforeach; ?>
    </tbody></table>
  <?php endif; ?>
</section>
<?php page_end();
