<?php
// SPDX-License-Identifier: MIT
require __DIR__ . '/../inc/site.php';
require_once __DIR__ . '/../inc/accounts.php';
require_once __DIR__ . '/../inc/smtp.php';
header('Cache-Control: no-store'); header('Referrer-Policy: no-referrer');
$secure = ofp_account_https();
session_name('ofp_account');
session_set_cookie_params(['secure'=>true, 'httponly'=>true, 'samesite'=>'Lax', 'path'=>'/account/']);
ini_set('session.use_strict_mode', '1'); session_start();
if (empty($_SESSION['csrf'])) $_SESSION['csrf'] = bin2hex(random_bytes(32));
function ofp_account_web_sign_in(array $account): void {
    $_SESSION = []; session_regenerate_id(true);
    $_SESSION['csrf'] = bin2hex(random_bytes(32));
    $_SESSION['account'] = $account['id']; $_SESSION['username'] = $account['username'];
    $_SESSION['password_version'] = hash('sha256', $account['password_hash']);
}
if (!empty($_SESSION['account'])) {
    try {
        $q = ofp_db()->prepare('SELECT username, password_hash, email_verified FROM accounts WHERE id = ?');
        $q->execute([$_SESSION['account']]); $current = $q->fetch(PDO::FETCH_ASSOC);
        if (!$current || !ofp_account_active($current) ||
            !hash_equals(hash('sha256', $current['password_hash']), $_SESSION['password_version'] ?? '')) {
            $_SESSION = []; session_regenerate_id(true); $_SESSION['csrf'] = bin2hex(random_bytes(32));
        }
    } catch (Throwable $e) { $_SESSION = []; $_SESSION['csrf'] = bin2hex(random_bytes(32)); }
}
$get = fn($key) => is_string($_POST[$key] ?? null) ? $_POST[$key] : '';
$query = fn($key) => is_string($_GET[$key] ?? null) ? $_GET[$key] : '';
$message = is_string($_SESSION['notice'] ?? null) ? $_SESSION['notice'] : '';
unset($_SESSION['notice']);
$error = ''; $mode = $query('mode') ?: 'login';
if (!in_array($mode, ['login', 'register', 'recover', 'activation'], true)) $mode = 'login';
$reset = $get('reset_token') ?: $query('reset');
$verify = $get('verification_token') ?: $query('verify');
if ($reset !== '') $mode = 'reset';
elseif ($verify !== '') $mode = 'verify';
$pinPage = defined('OFP_PIN_PAGE');
if (($_SERVER['REQUEST_METHOD'] ?? 'GET') === 'POST') {
    $action = $get('action');
    if ($action === 'register') $mode = 'register';
    if ($action === 'resend') $mode = 'activation';
    if ($action === 'recover') $mode = 'recover';
    try {
        if (!$secure) throw new InvalidArgumentException('Use HTTPS to sign in.');
        if (!ofp_account_config()['enabled']) throw new InvalidArgumentException('Cloud accounts are disabled.');
        if (!hash_equals($_SESSION['csrf'], $get('csrf'))) throw new InvalidArgumentException('Reload the page and try again.');
        $db = ofp_db();
        if (!ofp_account_rate($db, 'web:' . ofp_client_ip(), 20, 600)) throw new InvalidArgumentException('Too many attempts. Try again later.');
        ofp_account_prune($db);
        if (in_array($action, ['profile_save', 'delete_account'], true)) {
            if (empty($_SESSION['account'])) throw new InvalidArgumentException('Sign in to manage your profile.');
            if ($action === 'profile_save') {
                ofp_account_save_preferences($db, $_SESSION['account'], ['favorite_tank'=>$get('favorite_tank'), 'avatar'=>$get('avatar')]);
                $_SESSION['notice'] = 'Profile saved. Your game clients receive these settings when you next sign in.';
            } else {
                if ($get('confirm_delete') !== 'yes') throw new InvalidArgumentException('Confirm that you want to permanently delete your account.');
                ofp_account_delete($db, $_SESSION['account'], $get('password'));
                $_SESSION = []; session_regenerate_id(true); $_SESSION['csrf'] = bin2hex(random_bytes(32));
                $_SESSION['notice'] = 'Your account and personal details have been deleted. Match results remain anonymous.';
            }
            header('Location: /account/', true, 303); exit;
        } elseif ($action === 'logout') {
            $_SESSION = []; session_regenerate_id(true); header('Location: /account/'); exit;
        } elseif ($action === 'recover') {
            if (!ofp_smtp_ready()) throw new InvalidArgumentException('Password recovery is disabled.');
            if (!filter_var($get('email'), FILTER_VALIDATE_EMAIL)) throw new InvalidArgumentException('Enter a valid email address.');
            try { ofp_account_reset_request($db, $get('email')); }
            catch (Throwable $e) { error_log('OpenFP recovery mail failed.'); }
            $message = 'If that email is registered, a recovery link will be sent.';
        } elseif ($action === 'reset') {
            if (!ofp_smtp_ready() || !ofp_account_reset($db, $reset, $get('password')))
                throw new InvalidArgumentException('This reset link has expired or was already used.');
            $_SESSION = []; session_regenerate_id(true); $_SESSION['csrf'] = bin2hex(random_bytes(32));
            $message = 'Password changed. Sign in again on your devices.'; $mode = 'login'; $reset = '';
        } elseif ($action === 'activate') {
            $a = ofp_account_activate($db, $verify);
            if (!$a) throw new InvalidArgumentException('This activation link has expired or was already used. Request a new activation email.');
            ofp_account_web_sign_in($a);
            $_SESSION['notice'] = 'Email confirmed. Your account is active and you are signed in.';
            header('Location: /account/', true, 303); exit;
        } elseif ($action === 'resend') {
            if (!filter_var($get('email'), FILTER_VALIDATE_EMAIL)) throw new InvalidArgumentException('Enter a valid email address.');
            if (!ofp_smtp_ready()) throw new InvalidArgumentException('Activation email is unavailable. Contact the site administrator.');
            try { ofp_account_activation_request($db, $get('email')); }
            catch (Throwable $e) { error_log('OpenFP activation mail failed.'); throw new InvalidArgumentException('The activation email could not be sent. Try again in a minute.'); }
            $message = 'If that address has an account awaiting activation, an email will be sent. You can request another email after one minute.';
        } elseif (in_array($action, ['register', 'login', 'pair'], true)) {
            $awaitingActivation = false;
            if (empty($_SESSION['account'])) {
                if ($action === 'register') {
                    ofp_account_register($db, $get('username'), $get('email'), $get('password'));
                    $awaitingActivation = (bool)ofp_account_config()['require_email_verification'];
                }
                if ($awaitingActivation) {
                    $message = 'Account created. Check your email and use the activation link before signing in.';
                    $mode = 'activation';
                } else {
                    $a = ofp_account_login($db, $get('username'), $get('password'));
                    if (!$a) throw new InvalidArgumentException('Username or password is incorrect.');
                    ofp_account_web_sign_in($a);
                }
            }
            if (!$awaitingActivation) {
                if ($get('pin') !== '') {
                    if (!ofp_device_approve($db, $get('pin'), $_SESSION['account'])) throw new InvalidArgumentException('That PIN has expired, was already used, or is incorrect.');
                    $message = 'Apple TV connected. You can return to the game.';
                } else $message = 'You are signed in.';
            }
        } else throw new InvalidArgumentException('Choose an account action and try again.');
    } catch (InvalidArgumentException $e) { $error = $e->getMessage(); }
      catch (Throwable $e) { error_log('OpenFP account page failed: ' . $e->getMessage()); $error = 'Account service unavailable. Try again later.'; }
}
$titles = ['login'=>'Sign in', 'register'=>'Create your account', 'recover'=>'Reset your password',
    'reset'=>'Choose a new password', 'activation'=>'Activate your account', 'verify'=>'Confirm your email'];
$heading = !empty($_SESSION['account']) ? 'Your cloud profile' : ($titles[$mode] ?? 'Sign in');
if ($pinPage && in_array($mode, ['login', 'register'], true)) $heading = 'Connect Apple TV';
$minimum = ofp_account_config()['password_min_length'];
page_start($pinPage ? 'Connect Apple TV' : 'Cloud account', 'account');
?>
<section class="wrap section"><p class="eyebrow">OPENFP ACCOUNT</p><h1><?= h($heading) ?></h1>
<p>Cloud profiles play on public servers and keep your identity and match statistics together. Local profiles work offline and on private servers.</p>
<?php if (!$secure): ?><p class="notice">Open this page over HTTPS to use your account.</p><?php endif; ?>
<?php if ($message): ?><p class="notice" role="status"><?= h($message) ?></p><?php endif; ?>
<?php if ($error): ?><p class="notice account-error" role="alert"><?= h($error) ?></p><?php endif; ?>
<?php // Server validation always shows a visible error, including when a password is too short. ?>
<form method="post" class="account-form" novalidate>
<input type="hidden" name="csrf" value="<?= h($_SESSION['csrf']) ?>">
<?php if ($pinPage && in_array($mode, ['login', 'register'], true)): ?>
  <label>PIN shown on Apple TV <input name="pin" maxlength="9" autocomplete="off" value="<?= h($get('pin')) ?>" required></label>
<?php endif; ?>
<?php if ($mode === 'verify'): ?>
  <input type="hidden" name="verification_token" value="<?= h($verify) ?>">
  <p>Choose Activate account to confirm your email address and sign in to the website.</p>
  <button class="btn primary" name="action" value="activate">Activate account</button>
<?php elseif ($mode === 'activation'): ?>
  <p>Activation links are valid for 24 hours. If the email has not arrived, check your spam folder or request a new link below.</p>
  <label>Email <input type="email" name="email" maxlength="254" autocomplete="email" value="<?= h($get('email')) ?>" required></label>
  <button class="btn" name="action" value="resend">Resend activation email</button>
<?php elseif ($mode === 'recover'): ?>
  <?php if (ofp_smtp_ready()): ?>
    <label>Email <input type="email" name="email" maxlength="254" autocomplete="email" value="<?= h($get('email')) ?>" required></label>
    <button class="btn" name="action" value="recover">Send recovery link</button>
  <?php else: ?><p>Password recovery is disabled until the site administrator configures SMTP.</p><?php endif; ?>
<?php elseif ($mode === 'reset'): ?>
  <input type="hidden" name="reset_token" value="<?= h($reset) ?>">
  <label>New password <input type="password" name="password" minlength="<?= $minimum ?>" maxlength="72" autocomplete="new-password" aria-describedby="password-help" required></label>
  <p id="password-help" class="account-hint">At least <?= $minimum ?> characters; maximum 72 bytes.</p>
  <button class="btn" name="action" value="reset">Change password</button>
<?php elseif (empty($_SESSION['account'])): ?>
  <label>Username <input name="username" minlength="3" maxlength="15" pattern="[A-Za-z][A-Za-z0-9_]{2,14}" autocomplete="username" <?= $mode === 'register' ? 'aria-describedby="username-help"' : '' ?> value="<?= h($get('username')) ?>" required></label>
  <?php if ($mode === 'register'): ?>
    <p id="username-help" class="account-hint">3–15 letters, numbers or underscores. Start with a letter.</p>
    <label>Email <input type="email" name="email" maxlength="254" autocomplete="email" value="<?= h($get('email')) ?>" required></label>
  <?php endif; ?>
  <label>Password <input type="password" name="password" <?= $mode === 'register' ? 'minlength="' . $minimum . '" aria-describedby="password-help"' : '' ?> maxlength="72" autocomplete="<?= $mode === 'register' ? 'new-password' : 'current-password' ?>" required></label>
  <?php if ($mode === 'register'): ?>
    <p id="password-help" class="account-hint">At least <?= $minimum ?> characters; maximum 72 bytes.</p>
    <?php if (ofp_account_config()['require_email_verification']): ?><p class="account-hint">We will email you an activation link. Activate your account before signing in.</p><?php endif; ?>
  <?php endif; ?>
  <button class="btn primary" name="action" value="<?= $mode === 'register' ? 'register' : 'login' ?>"><?= $mode === 'register' ? 'Create cloud account' : 'Sign in' ?></button>
<?php else: ?>
  <p>Signed in as <strong><?= h($_SESSION['username']) ?></strong>.</p>
  <?php if ($pinPage): ?><button class="btn primary" name="action" value="pair">Connect Apple TV</button><?php endif; ?>
  <button class="btn" name="action" value="logout" formnovalidate>Sign out</button>
<?php endif; ?>
</form>
<?php if (!empty($_SESSION['account']) && !$pinPage):
try {
    $preferences = ofp_account_preferences(ofp_db(), $_SESSION['account']) + ['avatar'=>0, 'favorite_tank'=>1, 'steering'=>0];
    $avatars = ['Helmet', 'Red beret', 'Field hat', 'Pilot', 'Headset', 'Green beret', 'Hood', 'Visor', 'Mohawk', 'Officer'];
?>
<div class="account-settings">
<section class="account-panel"><h2>Player settings</h2>
<p>Your avatar and favorite tank follow you across clients. A local copy lets you play offline and on private servers.</p>
<form method="post" class="account-form">
<input type="hidden" name="csrf" value="<?= h($_SESSION['csrf']) ?>">
<fieldset class="avatar-picker"><legend>Avatar</legend>
<?php foreach ($avatars as $index=>$label): ?>
<label><input type="radio" name="avatar" value="<?= $index ?>" <?= $preferences['avatar']===$index?'checked':'' ?>>
<span class="profile-avatar" style="--avatar:<?= $index ?>" aria-hidden="true"></span><span><?= h($label) ?></span></label>
<?php endforeach; ?></fieldset>
<fieldset class="tank-picker" aria-describedby="tank-help">
<legend>Favorite tank</legend>
<div class="tank-picker-nav" hidden>
<button type="button" data-tank-step="-1" aria-label="Previous tank">←</button>
<span class="tank-picker-position" aria-live="polite" aria-atomic="true"></span>
<button type="button" data-tank-step="1" aria-label="Next tank">→</button>
</div>
<div class="tank-options">
<?php
$tanks = [
    ['Jackal','Fast, light scout',0,0.7,12,30,10],
    ['Bison','The all-rounder',1,0.8,10,50,15],
    ['Mammoth','Slow, heavy armour',2,1,7,80,20],
    ['Rhino','Heavy rocket launcher',2,1,7,80,20],
];
$rack = ['1111111111','1232323231','1232323231','1454545451','1454545451','1454545451','1454545451','1454545451','1454545451','1454545451','1454545451','1454545451','1422222241','1111111111'];
$rackColors = ['','#16181c','#9c9d8b','#393c37','#5d6556','#c7af65'];
foreach ($tanks as $index=>[$name,$description,$chassis,$scale,$speed,$armour,$rescue]): ?>
<label class="tank-choice">
<span class="tank-choice-title"><input type="radio" name="favorite_tank" value="<?= $index ?>" <?= $preferences['favorite_tank']===$index?'checked':'' ?>> <strong><?= h($name) ?></strong></span>
<span class="tank-visual" aria-hidden="true"><span class="tank-sprite" style="--chassis:<?= $chassis ?>;--scale:<?= $scale ?>">
<?php if ($index===3): ?><svg class="tank-rack" viewBox="0 0 32 32"><?php foreach ($rack as $y=>$row) for ($x=0;$x<10;$x++): ?><rect x="<?= 11+$x ?>" y="<?= 5+$y ?>" width="1" height="1" fill="<?= $rackColors[(int)$row[$x]] ?>"/><?php endfor; ?></svg><?php endif; ?>
</span></span>
<span class="tank-description"><?= h($description) ?></span>
<span class="tank-stat"><span>Speed <b><?= $speed ?></b></span><meter min="0" max="12" value="<?= $speed ?>" aria-label="Speed <?= $speed ?> out of 12"><?= $speed ?>/12</meter></span>
<span class="tank-stat"><span>Armour <b><?= $armour ?></b></span><meter min="0" max="80" value="<?= $armour ?>" aria-label="Armour <?= $armour ?> out of 80"><?= $armour ?>/80</meter></span>
<span class="tank-capacity">Rescue capacity: <?= $rescue ?> soldiers</span>
</label>
<?php endforeach; ?></div>
</fieldset>
<p id="tank-help" class="account-hint">Base tank stats; appearance and performance can vary by world. Rhino is available in enhanced worlds only.</p>
<p class="account-hint">Steering syncs automatically for each client type. macOS, Windows, Linux, AmigaOS and Apple TV keep separate steering preferences. Change steering in the game's profile settings; key and controller bindings stay on each device.</p>
<script src="/assets/account.js" defer></script>
<button class="btn primary" name="action" value="profile_save">Save player settings</button>
</form></section>
<section class="account-panel account-delete"><h2>Delete account</h2>
<p>Permanently removes your username, email, password, cloud preferences and sign-in credentials. Match scores and team results remain under <strong>Deleted player</strong>. Your name is removed from the site's leaderboards and hosted replays.</p>
<p>Other people's downloaded copies and independent game server records cannot be changed by this website. Remove cached profiles from your devices separately.</p>
<form method="post" class="account-form">
<input type="hidden" name="csrf" value="<?= h($_SESSION['csrf']) ?>">
<label>Current password <input type="password" name="password" maxlength="72" autocomplete="current-password" required></label>
<label class="account-confirm"><input type="checkbox" name="confirm_delete" value="yes" required> I understand this permanently deletes my account.</label>
<button class="btn" name="action" value="delete_account">Permanently delete account</button>
</form></section></div>
<?php
    $q = ofp_db()->prepare('SELECT COUNT(*) AS played, COALESCE(SUM(won),0) AS won, COALESCE(SUM(kills),0) AS kills, COALESCE(SUM(score),0) AS score FROM account_results WHERE account_id = ?');
    $q->execute([$_SESSION['account']]); $stats = $q->fetch(PDO::FETCH_ASSOC);
?><h2>Your public matches</h2><p><?= (int)$stats['played'] ?> played · <?= (int)$stats['won'] ?> wins · <?= (int)$stats['kills'] ?> tanks destroyed · <?= (int)$stats['score'] ?> points. Match statistics follow the site's history retention.</p><?php
} catch (Throwable $e) { } endif; ?>
<?php if (empty($_SESSION['account'])): ?>
<nav class="account-links" aria-label="Account options">
  <a href="<?= $pinPage ? '/account/pin' : '/account/' ?>?mode=register">Create an account</a>
  <a href="<?= $pinPage ? '/account/pin' : '/account/' ?>">Sign in</a>
  <?php if (ofp_account_config()['require_email_verification']): ?><a href="/account/?mode=activation">Resend activation email</a><?php endif; ?>
  <?php if (ofp_smtp_ready()): ?><a href="/account/?mode=recover">Forgot password?</a><?php else: ?><span>Password recovery is disabled.</span><?php endif; ?>
</nav>
<?php endif; ?>
</section>
<?php page_end();
