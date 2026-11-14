<?php
// SPDX-License-Identifier: MIT
require __DIR__ . '/../inc/site.php';
require_once __DIR__ . '/../inc/accounts.php';
require_once __DIR__ . '/../inc/smtp.php';
header('Cache-Control: no-store'); header('Referrer-Policy: no-referrer');
$secure=ofp_account_https();
session_name('ofp_account'); session_set_cookie_params(['secure'=>true,'httponly'=>true,'samesite'=>'Lax','path'=>'/account/']);
ini_set('session.use_strict_mode','1'); session_start();
if (empty($_SESSION['csrf'])) $_SESSION['csrf']=bin2hex(random_bytes(32));
if (!empty($_SESSION['account'])) {
    try {
        $q=ofp_db()->prepare('SELECT username, password_hash FROM accounts WHERE id = ?');
        $q->execute([$_SESSION['account']]); $current=$q->fetch(PDO::FETCH_ASSOC);
        if (!$current || !hash_equals(hash('sha256',$current['password_hash']),$_SESSION['password_version']??'')) {
            $_SESSION=[]; session_regenerate_id(true); $_SESSION['csrf']=bin2hex(random_bytes(32));
        }
    } catch (Throwable $e) { $_SESSION=[]; $_SESSION['csrf']=bin2hex(random_bytes(32)); }
}
$message=''; $error=''; $mode=$_GET['mode']??'login';
$reset=is_string($_GET['reset']??null)?$_GET['reset']:'';
if (isset($_POST['reset_token'])) $reset=(string)$_POST['reset_token'];
if ($reset!=='') $mode='reset';
$pinPage=defined('OFP_PIN_PAGE');
if (($_SERVER['REQUEST_METHOD']??'GET')==='POST') {
    try {
        if (!$secure) throw new InvalidArgumentException('Use HTTPS to sign in.');
        if (!ofp_account_config()['enabled']) throw new InvalidArgumentException('Cloud accounts are disabled.');
        if (!hash_equals($_SESSION['csrf'],(string)($_POST['csrf']??''))) throw new InvalidArgumentException('Reload the page and try again.');
        $db=ofp_db(); $action=$_POST['action']??'';
        if (!ofp_account_rate($db,'web:'.ofp_client_ip(),20,600)) throw new InvalidArgumentException('Too many attempts. Try again later.');
        ofp_account_prune($db);
        if ($action==='logout') { $_SESSION=[]; session_regenerate_id(true); header('Location: /account/'); exit; }
        if ($action==='recover') {
            if (!ofp_smtp_ready()) throw new InvalidArgumentException('Password recovery is disabled.');
            try { ofp_account_reset_request($db,(string)($_POST['email']??'')); } catch (Throwable $e) { error_log('OpenFP recovery mail failed.'); }
            $message='If that email is registered, a recovery link will be sent.';
        } elseif ($action==='reset') {
            if (!ofp_smtp_ready() || !ofp_account_reset($db,$reset,(string)($_POST['password']??''))) throw new InvalidArgumentException('This reset link has expired or was already used.');
            $_SESSION=[]; session_regenerate_id(true); $_SESSION['csrf']=bin2hex(random_bytes(32));
            $message='Password changed. Sign in again on your devices.'; $mode='login'; $reset='';
        } else {
            if (empty($_SESSION['account'])) {
                if ($action==='register') ofp_account_register($db,(string)($_POST['username']??''),(string)($_POST['email']??''),(string)($_POST['password']??''));
                $a=ofp_account_login($db,(string)($_POST['username']??''),(string)($_POST['password']??''));
                if (!$a) throw new InvalidArgumentException('Username or password is incorrect.');
                session_regenerate_id(true); $_SESSION['account']=$a['id']; $_SESSION['username']=$a['username']; $_SESSION['password_version']=hash('sha256',$a['password_hash']);
            }
            if (!empty($_POST['pin'])) {
                if (!ofp_device_approve($db,(string)$_POST['pin'],$_SESSION['account'])) throw new InvalidArgumentException('That PIN has expired, was already used, or is incorrect.');
                $message='Apple TV connected. You can return to the game.';
            } else $message='You are signed in.';
        }
    } catch (InvalidArgumentException $e) { $error=$e->getMessage(); }
      catch (Throwable $e) { error_log('OpenFP account page failed: '.$e->getMessage()); $error='Account service unavailable. Try again later.'; }
}
page_start($pinPage?'Connect Apple TV':'Cloud account','account');
?>
<section class="wrap section"><p class="eyebrow">OPENFP ACCOUNT</p><h1><?= $pinPage?'Connect Apple TV':'Your cloud profile' ?></h1>
<p>Cloud profiles play on public servers and keep your identity and match statistics together. Local profiles work offline and on private servers.</p>
<?php if (!$secure): ?><p class="notice">Open this page over HTTPS to use your account.</p><?php endif; ?>
<?php if ($message): ?><p class="notice"><?= h($message) ?></p><?php endif; ?>
<?php if ($error): ?><p class="notice" role="alert"><?= h($error) ?></p><?php endif; ?>
<form method="post" class="admin-form" style="max-width:38rem">
<input type="hidden" name="csrf" value="<?= h($_SESSION['csrf']) ?>">
<?php if ($pinPage): ?><label>PIN shown on Apple TV <input name="pin" maxlength="9" autocomplete="off" value="<?= h($_POST['pin']??'') ?>" required></label><?php endif; ?>
<?php if ($mode==='recover'): ?>
  <?php if (ofp_smtp_ready()): ?><label>Email <input type="email" name="email" maxlength="254" autocomplete="email" required></label><button class="btn" name="action" value="recover">Send recovery link</button>
  <?php else: ?><p>Password recovery is disabled until the site administrator configures SMTP.</p><?php endif; ?>
<?php elseif ($mode==='reset'): ?>
  <input type="hidden" name="reset_token" value="<?= h($reset) ?>"><label>New password <input type="password" name="password" minlength="12" maxlength="72" autocomplete="new-password" required></label><button class="btn" name="action" value="reset">Change password</button>
<?php elseif (empty($_SESSION['account'])): ?>
  <label>Username <input name="username" minlength="3" maxlength="15" pattern="[A-Za-z][A-Za-z0-9_]{2,14}" autocomplete="username" required></label>
  <?php if ($mode==='register'): ?><label>Email <input type="email" name="email" maxlength="254" autocomplete="email" required></label><?php endif; ?>
  <label>Password <input type="password" name="password" <?= $mode==='register'?'minlength="12"':'' ?> maxlength="72" autocomplete="<?= $mode==='register'?'new-password':'current-password' ?>" required></label>
  <button class="btn" name="action" value="<?= $mode==='register'?'register':'login' ?>"><?= $mode==='register'?'Create cloud account':'Sign in' ?></button>
<?php else: ?><p>Signed in as <strong><?= h($_SESSION['username']) ?></strong>.</p>
  <?php if ($pinPage): ?><button class="btn" name="action" value="pair">Connect Apple TV</button><?php endif; ?>
  <button class="btn" name="action" value="logout" formnovalidate>Sign out</button>
<?php endif; ?></form>
<?php if (!empty($_SESSION['account']) && !$pinPage):
try {
    $q=ofp_db()->prepare('SELECT COUNT(*) AS played, COALESCE(SUM(won),0) AS won, COALESCE(SUM(kills),0) AS kills, COALESCE(SUM(score),0) AS score FROM account_results WHERE account_id = ?');
    $q->execute([$_SESSION['account']]);$stats=$q->fetch(PDO::FETCH_ASSOC);
?><h2>Your public matches</h2><p><?= (int)$stats['played'] ?> played · <?= (int)$stats['won'] ?> wins · <?= (int)$stats['kills'] ?> tanks destroyed · <?= (int)$stats['score'] ?> points. Match statistics follow the site's history retention.</p><?php
} catch (Throwable $e) { } endif; ?>
<p><a href="<?= $pinPage?'/account/pin':'/account/' ?>?mode=register">Create an account</a> · <a href="/account/">Sign in</a>
<?php if (ofp_smtp_ready()): ?> · <a href="/account/?mode=recover">Forgot password?</a><?php else: ?> · Password recovery is disabled.<?php endif; ?></p>
</section>
<?php page_end();
