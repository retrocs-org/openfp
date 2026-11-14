<?php
// SPDX-License-Identifier: MIT
require __DIR__.'/../../inc/documentation.php';
documentation_start('accounts');
?>
<h2>Local or cloud</h2>
<p>Local profiles play offline and on private servers. Public servers require a cloud account with a unique username, email and password. Account names identify your public match results across servers; avatars and favorite tanks sync across clients. Steering syncs automatically within each client platform, separately for macOS, Windows, Linux, AmigaOS and Apple TV. Keyboard and controller bindings stay on each device.</p>
<h2>Sign in on a client</h2>
<p>On macOS, Windows or Linux, choose <strong>Cloud profile</strong> in the profile picker and sign in with your username and password. You can create an account from the same window. On AmigaOS, choose <strong>Profile type → Cloud</strong> when creating or editing a profile.</p>
<p>Usernames contain 3–15 letters, numbers or underscores and start with a letter. Passwords need at least 12 characters by default, with a 72-byte maximum; the account form shows the current minimum. Clients remember a login session, never the password. When a session expires, sign in again through Cloud profile. Each player needs their own account.</p>
<h2>Activate your account</h2>
<p>New accounts require email activation unless the site operator disables it. Opening the emailed link immediately verifies the address, signs you into that account and opens your cloud profile. Sign in on game clients with your username and password. The link expires after 24 hours and can be used once. If it expires or does not arrive, check your spam folder or choose <a href="/account/?mode=activation">Resend activation email</a>. A pending account cannot sign in, join a public server or pair Apple TV.</p>
<h2>Connect Apple TV</h2>
<p>Choose <strong>Cloud profile</strong> on Apple TV. On your phone or computer, open <a href="/account/pin">openfp.retrocs.org/account/pin</a>, enter the displayed PIN and sign in. PINs expire after ten minutes and can be used once. Other clients use username and password directly.</p>
<h2>Player settings and offline play</h2>
<p>Sign in to <a href="/account/">your account page</a> to choose an avatar and browse favorite tanks with pictures and base stats. Choices save automatically, with a saved indicator and a retry option if needed. Set steering in the game profile editor; it syncs automatically for that client platform. Clients keep a local copy for offline matches, split-screen and private servers. Desktop clients refresh it in the background; AmigaOS refreshes when you sign in to the cloud account. Locally edited settings are kept if the service is unavailable and retried on the next sync or save. Rhino is available only in enhanced worlds.</p>
<p>Public servers still require valid cloud sign-in. A configured game server password applies to everyone, including cloud accounts, on public and private servers.</p>
<h2>Email and password</h2>
<p>Your username cannot be changed. Under <strong>Account details</strong>, choose <strong>Change email</strong> and enter your current password and new address. Open the link sent to the new address within 24 hours to confirm it and sign in automatically; the old address stays active until then. You can cancel a pending change. Confirmation is always required, even if registration activation is disabled, and needs the site mail service.</p>
<p><strong>Change password</strong> requires your current password and matching new passwords. This browser stays signed in; other browser and game sessions need to sign in again. Changing or resetting the password also cancels pending email changes.</p>
<h2>Delete your account</h2>
<p>The account page lets you permanently delete your account after entering your current password and confirming the checkbox. Your username, email, password, preferences and sign-in credentials are erased. Match scores and team results remain under <strong>Deleted player</strong>; hosted replay player names are replaced, associated map author/contact details are removed, and personal daily entries are deleted.</p>
<p>Downloaded replays, independent game server records and cached profiles on your devices are separate copies. This website cannot erase them remotely. Logs and backups follow their operators’ retention policies. See <a href="/privacy/">Privacy</a> for details.</p>
<h2>Password recovery</h2>
<p><a href="/account/">Manage sign-in and recovery</a> on the website. Password recovery is available only when the site administrator has configured SMTP. A used recovery link signs out existing client sessions. Deleting a device's local profile does not delete the central account.</p>
<?php documentation_end();
