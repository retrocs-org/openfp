# Cloud accounts

Local profiles work in local games and on private dedicated servers (`public = no` or `public = code`). Public servers (`public = yes`) require a cloud account, including spectators. Existing profiles remain local; they are not converted or uploaded automatically.

## Players

On macOS, Windows and Linux, choose **Cloud profile** in the profile picker. Sign in with your username and password, or choose **Create cloud account** and enter a username, email and password. On AmigaOS, create or edit a profile, select **Profile type → Cloud**, then sign in or create an account. Avatar, favorite tank and controls remain local preferences.

Apple TV uses pairing instead of typing credentials on the television. Choose **Cloud profile**, open <https://openfp.retrocs.org/account/pin> on your phone or computer, enter the displayed PIN and sign in. The PIN expires after ten minutes and can only be approved once. A PIN alone cannot sign in to an account.

Usernames contain 3–15 letters, numbers or underscores and start with a letter. Passwords contain 12–72 bytes. Account names are unique without regard to case. The account's username is used on public servers. One account can occupy one connection to a given server at a time; each split-screen player needs their own account.

Clients store a login session and a player identity key, never the password. Sessions last 30 days by default. Protect the profile/settings files as account credentials. To renew an expired session, open **Cloud profile** and sign in again; the existing profile is updated. Deleting a profile on a device removes its local copy; it does not delete the central account.

Cloud sign-in requires verified HTTPS. AmigaOS needs AmiSSL v5 with its current CA certificates and a working TCP/IP stack. There is no unencrypted fallback for account requests. Passwords go to the central account service, never to a game server. Public servers check short-lived, single-use tickets bound to their own connection challenge. The verifier is SHA-256 of the 32-byte player identity key followed by the 16-byte server ID; its key ID is the first 16 bytes of SHA-256 of that verifier. The ticket binds this ID, server registration and challenge nonce together.

## Website setup

Back up the existing MariaDB database, upload the updated website while retaining `config.inc.php`, then run:

```sh
cd /srv/www/vhosts/openfp.retrocs.org
php inc/migrate.php up
```

Schema 14 adds accounts, sessions, pairing PINs and account-linked match results. Existing match history is retained but is not retroactively assigned to accounts. The website needs PHP 8.1+, PDO MySQL, sessions and OpenSSL. Serve `/account/`, `/account/pin` and `/account/api/` over HTTPS. Behind a reverse proxy, configure only the actual proxy addresses in `listing.trusted_proxies` and forward `X-Forwarded-Proto: https`.

Copy the `accounts` and optional `smtp` sections from `public-website/config.inc.example.php` into the site's private configuration. `accounts.enabled` defaults to true; `accounts.session_days` defaults to 30 (range 1–90).

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

Rebuild/update the dedicated server and clients together: this change uses network protocol 40 and keeps release version 0.1. Restart after changing public/private mode or cloud credentials. Public servers refuse to start without registration and HTTPS support. Verification runs in a bounded worker so the match loop keeps running while accounts are checked.

`public = no` needs no central registration and accepts local profiles. `public = code` also accepts local profiles, but requires server registration to use the website's join-code/NAT service. Join-code servers do not publish cloud match statistics or live replays. Public server match reports associate each human with their verified account; private play stays outside those statistics.

## SMTP and recovery

An empty `smtp.host` or invalid/empty `smtp.from` disables password recovery automatically. Account creation and ordinary sign-in still work. The recovery link appears only when SMTP is configured.

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

It deletes all accounts, sessions, pairing requests, account statistics, leaderboards, match history, replays, daily results, community maps and transient server-list data in a transaction. It preserves the schema, configuration and built-in map files. It does not modify databases/logs stored separately by dedicated servers; reset those separately if a complete service reset is intended.

## Verification

`ctest --test-dir build -R 'cloud|lobby|net' --output-on-failure` covers the native account parser and server admission rules. PHP account tests use only an explicitly named disposable MariaDB database:

```sh
OFP_ACCOUNT_TEST_DSN='mysql:host=127.0.0.1;dbname=openfp_account_test;charset=utf8mb4' \
OFP_ACCOUNT_TEST_USER=testuser OFP_ACCOUNT_TEST_PASSWORD=... php tests/test_accounts.php
```

Run `python3 tests/test_account_http.py` with the same database environment to verify browser sign-in, CSRF protection, Apple TV PIN approval, one-use join tickets and SMTP password recovery through a temporary local website. It does not use the deployed site.

Never point the test at a production database. The test refuses database names other than `openfp_account_test`.
