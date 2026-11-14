# Cloud accounts

Local profiles work in local games and on private dedicated servers (`public = no` or `public = code`). Public servers (`public = yes`) require a cloud account, including spectators. Existing profiles remain local; they are not converted or uploaded automatically.

## Players

On macOS, Windows and Linux, choose **Cloud profile** in the profile picker. Sign in with your username and password, or choose **Create cloud account** and enter a username, email and password. On AmigaOS, create or edit a profile, select **Profile type → Cloud**, then sign in or create an account. Avatar and favorite tank are shared cloud preferences. Steering syncs automatically for each client type: macOS, Windows, Linux, AmigaOS and Apple TV have separate preferences. Keyboard and controller bindings stay on each device.

Apple TV uses pairing instead of typing credentials on the television. Choose **Cloud profile**, open <https://openfp.retrocs.org/account/pin> on your phone or computer, enter the displayed PIN and sign in. The PIN expires after ten minutes and can only be approved once. A PIN alone cannot sign in to an account.

Usernames contain 3–15 letters, numbers or underscores and start with a letter. Passwords need at least 12 characters by default, with a maximum of 72 bytes. The site operator can lower or raise the minimum. The form shows the current rule and displays validation errors on the page. Account names are unique without regard to case. The account's username is used on public servers. One account can occupy one connection to a given server at a time; each split-screen player needs their own account.

New accounts require email activation by default. Open the emailed link and choose **Activate account**. This signs you into the website automatically and opens your cloud profile; game clients still use your username and password. Links expire after 24 hours and work once. **Resend activation email** issues a replacement link, at most once per minute. Pending accounts cannot sign in, join a public server or approve Apple TV pairing. Account creation in a game client displays an instruction to check your email. An expired link can be replaced on the website.

Clients store a login session and a player identity key, never the password. Sessions last 30 days by default. Protect the profile/settings files as account credentials. To renew an expired session, open **Cloud profile** and sign in again; the existing profile is updated. Deleting a profile on a device removes its local copy; it does not delete the central account.

Cloud sign-in requires verified HTTPS. AmigaOS needs AmiSSL v5 with its current CA certificates and a working TCP/IP stack. There is no unencrypted fallback for account requests. Passwords go to the central account service, never to a game server. Public servers check short-lived, single-use tickets bound to their own connection challenge. The verifier is SHA-256 of the 32-byte player identity key followed by the 16-byte server ID; its key ID is the first 16 bytes of SHA-256 of that verifier. The ticket binds this ID, server registration and challenge nonce together.

## Player settings and local caching

Sign in at <https://openfp.retrocs.org/account/> to choose your avatar and favorite tank with a visual picker and base tank stats. Selections save automatically; the status below the picker confirms when they are saved and offers a retry if the connection fails. Without JavaScript, a save button is available. Steering is changed in the client profile editor and automatically saved for that client platform; signing in on another platform does not change it. Rhino remains limited to enhanced worlds; Classic matches use the original tank classes.

A cloud profile is cached locally with its identity, settings and session, so it can be selected without an internet connection for local split-screen and private servers. This does not bypass public-server authentication. Desktop clients refresh settings in the background when a cached profile is selected. Local edits are saved immediately and queued for cloud sync; pending local edits take priority over a remote refresh. On AmigaOS, explicit cloud sign-in refreshes preferences, and saving a profile uploads changes; an unavailable service leaves a pending local copy for the next sign-in/save. Sessions and identity keys are credentials: protect cached profile files. Account deletion cannot erase cached files on disconnected devices.

## Email and password

The username is permanent. Under **Account details**, choose **Change email** and enter the new address and current password. The current address remains active until you open the confirmation email and choose **Confirm email change**. Links last 24 hours and work once. Replacing or cancelling a pending request invalidates its old link. Email changes always require confirmation, even when registration activation is disabled; SMTP must be configured. Confirming an address invalidates outstanding password recovery links. A pending change is cancelled when the password is changed or reset.

**Change password** requires the current password and two matching copies of the new password. The current browser stays signed in; other browser and game sessions and pending pairing requests are revoked. Avatar, tank, per-client steering and match history remain unchanged.

## Delete an account

On the account page, choose **Permanently delete account**, enter the current password and confirm the checkbox. This deletes the username, email, password hash, identity key, preferences, sessions, activation/recovery links and pairing credentials. Existing browser and client sessions stop authenticating.

Scores, wins, sides and team results stay in match history under **Deleted player**. The website removes the old name from its leaderboard rows, live server lists and hosted replay player headers. Uploaded map author/contact metadata associated with the account is removed; personal daily challenge entries are removed. Replay inputs, terrain and verification checksums remain unchanged. Anonymous result IDs and ladder team associations remain so late match reports can preserve the team result without restoring the account. Normal match-history retention still applies.

Downloaded replays, independent dedicated-server records, logs and backups are separate copies. They cannot be remotely erased by account deletion; server operators must handle their own retention and backup policies. Public free-text content such as team names or map descriptions is not searched for personal information automatically.

## Website setup

Back up the existing MariaDB database, upload the updated website while retaining `config.inc.php`, then run:

```sh
cd /srv/www/vhosts/openfp.retrocs.org
php inc/migrate.php up
```

Schema 18 adds pending email confirmations; schema 17 added steering per client platform; schema 16 added shared preferences and anonymous match-history support; schema 15 added email activation to the accounts introduced in schema 14. Existing accounts remain active during the upgrade. Existing match history is retained but is not retroactively assigned to accounts. The website needs PHP 8.1+, PDO MySQL, sessions and OpenSSL. Serve `/account/`, `/account/pin` and `/account/api/` over HTTPS. Behind a reverse proxy, configure only the actual proxy addresses in `listing.trusted_proxies` and forward `X-Forwarded-Proto: https`.

Copy the `accounts` and optional `smtp` sections from `public-website/config.inc.example.php` into the site's private configuration. `accounts.enabled` defaults to true; `accounts.session_days` defaults to 30 (range 1–90).

```php
'accounts' => [
    'enabled' => true,
    'session_days' => 30,
    'password_min_length' => 12,
    'require_email_verification' => true,
    'servers' => [ /* preserve your registered server hashes here */ ],
],
```

`password_min_length` accepts 4–72 characters (values outside that range are clamped); the 72-byte maximum still applies. Set it to `4` to permit a password such as `test`; the default remains `12`. The minimum applies to new accounts and password changes, not existing sign-ins. Set `require_email_verification` to `false` to allow immediate sign-in without activation or SMTP. New accounts created with verification disabled remain active if verification is enabled later. Disabling verification also allows currently pending accounts to sign in while it is off. With verification enabled, SMTP must be configured; registration shows a clear error if mail is unavailable instead of silently activating the account. If delivery fails after creation, the account stays pending and the player can request another email. Changing the minimum or activation policy takes effect on the next request.

## Register a dedicated server

On the website host:

```sh
php inc/cloud-server.php my-server
```

The command prints a hash for `config.inc.php` → `accounts.servers`, plus `cloud_server` and `cloud_secret` for the dedicated server's `server.cfg`. Copy them to the indicated places. Keep the secret private and restrict access to both configuration files. Use a distinct registration for each dedicated server.

```ini
public = yes
listing_url = https://openfp.retrocs.org/api/
cloud_server = my-server
cloud_secret = PASTE_THE_GENERATED_SECRET
```

Rebuild/update the dedicated server and clients together: this change uses network protocol 40 and keeps release version 0.1. Restart after changing public/private mode or cloud credentials. A configured server password is always required in addition to any cloud authentication, on public and private servers. Public servers refuse to start without registration and HTTPS support. Verification runs in a bounded worker so the match loop keeps running while accounts are checked.

`public = no` needs no central registration and accepts local profiles. `public = code` also accepts local profiles, but requires server registration to use the website's join-code/NAT service. Join-code servers do not publish cloud match statistics or live replays. Public server match reports associate each human with their verified account; private play stays outside those statistics.

## SMTP and recovery

An empty `smtp.host` or invalid/empty `smtp.from` disables password recovery automatically. Existing active accounts can still sign in. Creating an account without SMTP requires `accounts.require_email_verification = false`. The recovery link appears only when SMTP is configured.

```php
'smtp' => [
    'host' => 'mail.example.org',
    'port' => 587,
    'encryption' => 'starttls', // starttls, tls, or none
    'username' => '',          // optional relay authentication
    'password' => '',
    'from' => 'openfp@example.org',
    'from_name' => 'OpenFP',
    'ehlo' => 'openfp.example.org', // sending host name or [IP]
    'bind' => '',              // optional local source IP:port, e.g. 192.0.2.10:0
    'timeout' => 10,
],
```

Use `tls` for implicit TLS (commonly port 465), or `starttls` for a TLS upgrade. Certificates are verified. `none` supports a trusted unencrypted relay when explicitly configured. Recovery links expire after 30 minutes, are single-use, and revoke all client sessions and pending device pairings when used. The website's base URL must be the public HTTPS address.

## Reset all website user data

This tool is CLI-only and previews affected row counts by default:

```sh
php inc/reset-data.php
```

To deliberately start over, stop dedicated servers and website background workers, take a database backup, then run:

```sh
php inc/reset-data.php --confirm ERASE-ALL-USER-DATA
```

It deletes all accounts, sessions, activation links, pairing requests, account statistics, leaderboards, match history, replays, daily results, community maps and transient server-list data in a transaction. It preserves the schema, configuration and built-in map files. It does not modify databases/logs stored separately by dedicated servers; reset those separately if a complete service reset is intended.

## Verification

`ctest --test-dir build -R 'cloud|lobby|net' --output-on-failure` covers the native account parser and server admission rules. PHP account tests use only an explicitly named disposable MariaDB database:

```sh
OFP_ACCOUNT_TEST_DSN='mysql:host=127.0.0.1;dbname=openfp_account_test;charset=utf8mb4' \
OFP_ACCOUNT_TEST_USER=testuser OFP_ACCOUNT_TEST_PASSWORD=... php tests/test_accounts.php
```

Run `python3 tests/test_account_http.py` with the same database environment to verify shared preferences, confirmed account deletion, anonymous results and replay metadata, visible form errors, configurable passwords, email activation and expiry/resending, browser sign-in, CSRF protection, Apple TV PIN approval, one-use join tickets and SMTP password recovery through a temporary local website. It does not use the deployed site.

Never point the test at a production database. The test refuses database names other than `openfp_account_test`.
