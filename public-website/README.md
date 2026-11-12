# OpenFP: the web site and the server listing

This folder is the whole web side of OpenFP:

* **The web site:** `/` (About, screenshots and downloads), `/server/` (the live server list), `/statistics/`,
  `/source/` and `/maps/`. Screenshots are on the home page.
* **The listing API:** `/api/`. Dedicated servers announce themselves to it, and
  the game's FIND SERVERS reads from it.

It is self-contained and is itself the document root. The listing keeps its
data in **MariaDB**; the site stores nothing of its own.

```
index.php  server/  statistics/  source/  maps/  assets/
api/                                 the listing API
inc/                                 the shared code; migrations.php, migrate.php   (never served)
config.inc.example.php               the settings: copy to config.inc.php   (never served)
dev-router.php                       for trying it all locally              (never served)
.htaccess                            keeps the private parts private
```

**Needs:** Apache 2.4 and PHP 8 with `pdo_mysql`, plus a MariaDB 10.5+ server
(local or remote). There's no Composer, no framework and no build step.

The steps, in order:

1. [Put the files in place](#1-put-the-files-in-place)
2. [Set up the database in MariaDB](#2-set-up-the-database-in-mariadb)
3. [Configure config.inc.php](#3-configure-configincphp)
4. [Set up Apache](#4-set-up-apache)
5. [Check that it works](#5-check-that-it-works)
6. [Fill the database: servers announcing themselves](#6-fill-the-database-servers-announcing-themselves)

The examples use the names from the real setup: `openfp.retrocs.org` served
from `/srv/www/vhosts/openfp.retrocs.org` on an openSUSE / SLES host, behind
HAProxy at `172.16.1.1`, with TLS ending at HAProxy and Apache on port 6669.
Swap in your own names where they differ.


## 1. Put the files in place

The vhost directory holds the contents of this folder. If you have the website
release archive, its top-level files already match the web root.

```sh
webroot=/srv/www/vhosts/openfp.retrocs.org
mkdir -p "$webroot"
tar -xzf OpenFirePower-0.1-website-update.tar.gz -C "$webroot"
```

If you have the source archive instead, unpack it and copy the contents of
`public-website/` into the active Apache `DocumentRoot`.

```sh
webroot=/srv/www/vhosts/openfp.retrocs.org
tar -xzf OpenFirePower-0.1-source.tar.gz
rsync -a openfirepower-0.1/public-website/ "$webroot/"
install -Dm644 OpenFirePower-0.1-source.tar.gz "$webroot/assets/releases/OpenFirePower-0.1-source.tar.gz"
```

Keep the live `config.inc.php`, database and existing downloads. Check that
`index.php`, `source/index.php`, `maps/index.php` and `assets/bundled-maps.json`
are directly under the active document root. Verify `/source/`, `/maps/` and
`/assets/bundled-maps.json` through the public hostname. If the files are in
the right place and Apache still serves old PHP, check its document root and
PHP opcode-cache settings.

On openSUSE / SLES, PHP for Apache and its database driver come from:

```sh
zypper install apache2-mod_php8 php8-pdo php8-mysql
a2enmod php8
```

(On Debian / Ubuntu: `apt install libapache2-mod-php php-mysql`.)


### Refresh an existing site from GitHub

The updater in `scripts/refresh-openfp.sh` copies the latest `main` branch into
`/srv/www/vhosts/openfp.retrocs.org`. Run it on the web server as root; it needs
`git`, `rsync` and PHP. From a source checkout:

```sh
install -m750 scripts/refresh-openfp.sh /srv/www/vhosts/refresh-openfp.sh
/srv/www/vhosts/refresh-openfp.sh --dry-run
/srv/www/vhosts/refresh-openfp.sh
cd /srv/www/vhosts/openfp.retrocs.org
php inc/migrate.php up
```

It checks the incoming PHP before updating the site. `config.inc.php` keeps its
contents, ownership and permissions; local `data/`, `uploads/` and
`assets/releases/` are also preserved. Obsolete website files are removed.
The database is left untouched until you run the migration command. Push website
changes to GitHub's `main` branch before refreshing.

### Tagged downloads

The home page's Get section (`/#get`) reads published stable releases
from GitHub (`retrocs-org/openfp`). No access token or database change is needed.
The web host needs
outbound HTTPS and either PHP cURL or HTTPS streams with `allow_url_fopen`.

Only published releases with numeric tags such as `0.1` or `v0.1` are shown.
Drafts, prereleases, future releases and untagged builds are excluded. Each
platform shows **Not released yet** until a matching asset is attached. The
separate Linux server panel uses `openfp-server-linux-ARCH-VERSION.tar.gz`; client
downloads use `openfp-client-PLATFORM-ARCH-VERSION.EXT`. A desktop client archive
is never listed as a server download. Checksums are linked only when attached to
that release.
The full-source button also follows tagged release assets.

Public metadata is cached in the PHP temporary directory for five minutes, with
one-minute retries after errors. Recently confirmed links can remain available
for up to 24 hours during an outage, with a notice. A first-request failure is
shown as unavailable, rather than claiming there is no release. Cache files
contain public metadata only and are kept outside the document root.

Uploading a tagged release through `scripts/release.py` updates the downloads
automatically after the cache refresh. Copying files into `assets/releases/`
alone does not advertise them as released. Existing local mirrors can remain
there, but the download list and server panel use the tagged GitHub metadata.

The client update endpoint (`/api/?action=version`) uses the same release data,
returns an empty version before any binary release, and works without the listing
database. The old `site.latest_version` setting is no longer used.

For offline previews and tests, set `OFP_RELEASE_OFFLINE=1`. Run the offline
regression checks with `php tests/test_website_releases.php` from the source root.

## 2. Set up the database in MariaDB

The listing uses one database. Its main tables include:

| Table | What it holds | How long |
|---|---|---|
| `servers` | every public server right now: address, name, players, game, world, map, ping, matches, uptime, message of the day | until 3 minutes after its last announcement |
| `samples` | a player count per server every 4 minutes | 7 days; feeds the 24-hour charts and "peak today" |
| `hits` | the address of each announcement | 10 minutes; only for the rate limit |
| `leaders` | each server's top 10 (name, rating, wins, matches), as it announces them | replaced on every announcement; shown for 30 days |
| `matches` | every finished match on a public server: map, mode, winner, everyone's side, kills, score and rating change | each server's last 500, at most 90 days |
| `replays` | the match's replay (the `.ofprep` text, a few to a few hundred KB) | with its match |

### 2.1 The MariaDB server

If the database is on another machine than the web server, it must accept
connections from the web server:

```ini
# /etc/my.cnf.d/server.cnf (SUSE) or /etc/mysql/mariadb.conf.d/50-server.cnf (Debian), [mysqld]
bind-address = 0.0.0.0            # or the database host's internal address
```

Open port 3306 only to the web server, for example with firewalld:

```sh
firewall-cmd --permanent --add-rich-rule='rule family="ipv4" source address="WEB.SERVER.IP" port port="3306" protocol="tcp" accept'
firewall-cmd --reload
```

If the connection between the two crosses anything you don't control, turn
on TLS in MariaDB and require it:

```ini
[mysqld]
ssl_cert = /etc/my.cnf.d/certs/db.crt
ssl_key  = /etc/my.cnf.d/certs/db.key
ssl_ca   = /etc/my.cnf.d/certs/ca.crt
require_secure_transport = ON
```

Then set the same CA in `config.inc.php` (`ssl_ca`, see step 3). On a private
network or through a WireGuard / SSH tunnel you can leave TLS off.

### 2.2 The database and its user

As MariaDB's administrator (`mariadb -u root -p`, or `sudo mariadb` on the
database host):

```sql
CREATE DATABASE openfirepower CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

-- the web server's address as MariaDB sees it ('localhost' if it's the same machine)
CREATE USER 'openfirepower'@'WEB.SERVER.IP' IDENTIFIED BY 'a long random password';
GRANT SELECT, INSERT, UPDATE, DELETE ON openfirepower.* TO 'openfirepower'@'WEB.SERVER.IP';

-- only if the connection must use TLS:
ALTER USER 'openfirepower'@'WEB.SERVER.IP' REQUIRE SSL;
```

These four rights are all the listing needs once the tables exist. To get a
good password: `openssl rand -base64 24`.

### 2.3 Create the tables (and keep them up to date)

The site's tables are made, and later upgraded, by `inc/migrate.php`. It keeps the
database's version in a small table (`ofp_meta`) and applies what's missing, in order.
It logs in as `db_admin` from `config.inc.php` (only what differs from `db`), so the
web user can keep just SELECT, INSERT, UPDATE and DELETE:

```php
'db_admin' => ['user' => 'root', 'password' => '...'],   // in config.inc.php (step 3)
```

```sh
cd /srv/www/vhosts/openfp.retrocs.org
php inc/migrate.php        # where the database is, and what's to do
php inc/migrate.php up     # do it
```

Run `php inc/migrate.php up` again after every update; it does nothing when the
database is already current, and it recognises databases made by older versions, from
before there was a version number. While the database is behind, the site still works,
hides what needs the new tables, and says so in a line at the bottom of each page.

The other way: `'create_tables' => true` lets the site do it by itself on the first
request, as the web user, which then needs `CREATE, ALTER, INDEX` too.

## 2.4 Filling it

There is nothing to import: the tables start empty. Dedicated servers fill
them by announcing themselves (step 6), and the listing tidies up after itself:
servers that go quiet drop off, and old samples and hits are deleted.

To see the pages with something on them before any real server exists, add a
**demo row**. It shows up at once on `/server/` and in the API, and it
disappears by itself 3 minutes later (or delete it, below):

```sql
INSERT INTO servers (host, port, name, build, players, max_players, password, running, match_players,
                     mode, world, map, motd, ping_ms, matches, started, first_seen, last_seen)
VALUES ('demo.example.org', 29870, 'Demo server (test row)', 132609, 3, 8, 0, 1, 2,
        'versus', 'desert', 'random', 'A test row: it disappears by itself.', 12, 7,
        UNIX_TIMESTAMP() - 7200, UNIX_TIMESTAMP(), UNIX_TIMESTAMP());

-- a day of player counts for its chart (MariaDB's built-in seq_1_to_23 table)
INSERT INTO samples (host, port, at, players, running)
SELECT 'demo.example.org', 29870, UNIX_TIMESTAMP() - seq * 3600, 1 + seq % 4, 0 FROM seq_1_to_23;

-- and away again
DELETE FROM samples WHERE host = 'demo.example.org';
DELETE FROM servers WHERE host = 'demo.example.org';
```


## 3. Configure config.inc.php

```sh
cd /srv/www/vhosts/openfp.retrocs.org
cp config.inc.example.php config.inc.php
chown root:www config.inc.php && chmod 640 config.inc.php    # SUSE's Apache runs as wwwrun:www
$EDITOR config.inc.php
```

The file returns one PHP array in three parts. A complete one for
openfp.retrocs.org:

```php
<?php
return [
    'site' => [
        'name'     => 'OpenFP',
        'tagline'  => 'An open remake of the 1987 Amiga tank classic Fire Power',
        'base_url' => 'https://openfp.retrocs.org',
        'game_api' => 'https://openfp.retrocs.org/api/',
        'repo_url' => 'https://github.com/retrocs-org/openfp',
        'contact'  => 'mailto:support@retrocs.org',
    ],
    'db' => [
        'dsn'      => 'mysql:host=db.example.org;port=3306;dbname=openfirepower;charset=utf8mb4',
        'user'     => 'openfirepower',
        'password' => 'the password from step 2.2',
        'ssl_ca'   => '',
        'create_tables' => false,
    ],
    'listing' => [
        'expire_after'    => 180,
        'probe'           => true,
        'probe_timeout'   => 1.5,
        'rate_limit'      => 60,
        'allow_host'      => true,
        'trusted_proxies' => ['172.16.1.1'],
        'history_hours'   => 24,
        'sample_every'    => 240,
    ],
];
```

### site

| Key | What it does |
|---|---|
| `name` | the site's name, in titles and the top bar |
| `tagline` | the description search engines and link previews show |
| `base_url` | the site's own address, used for links to images in previews; no slash at the end |
| `game_api` | the HTTPS listing address for servers and games; keep the HTTP API available too for clients without TLS |
| `repo_url` | the source repository; defaults to https://github.com/retrocs-org/openfp. A GitHub link appears in the menu and footer |
| `contact` | a `mailto:` or a web address. When set, a "Contact" link appears in the footer |

### db

| Key | What it does |
|---|---|
| `dsn` | where the database is: `mysql:host=…;port=3306;dbname=openfirepower;charset=utf8mb4`. Use the host name or address MariaDB is reached at, and keep `charset=utf8mb4` |
| `user`, `password` | the user from step 2.2 |
| `ssl_ca` | `''` for a plain connection. For TLS, the path to the CA file that signed the database's certificate, readable by Apache, for example `/etc/pki/trust/anchors/db-ca.pem`. The server's certificate is then also checked against its host name, so `host=` in the DSN must match the certificate |
| `create_tables` | `false`: you run `php inc/migrate.php up` (2.3). `true`: the site makes and upgrades its tables itself, as this user (who then needs CREATE, ALTER, INDEX) |

The listing runs on MariaDB (or MySQL) only. To try it out without one of your own, see
[Trying it locally](#trying-it-locally): a MariaDB in a container.

### listing

| Key | Default | What it does |
|---|---|---|
| `expire_after` | `180` | seconds after its last announcement before a server leaves the list (servers announce every 60) |
| `probe` | `true` | ask each announcing server over UDP whether it's really there, and list what *it* answers. Keep this on: it stops made-up addresses and player counts |
| `probe_timeout` | `1.5` | seconds to wait for that answer |
| `rate_limit` | `60` | announcements per address per 10 minutes |
| `allow_host` | `true` | let a server give the name players should use (`public_host` in its `server.cfg`), for servers behind NAT or with a DNS name. The name must point at the address the server announces from; otherwise the listing refuses it (for announcing, removing and matches alike) |
| `trusted_proxies` | `[]` | **behind HAProxy, its address:** `['172.16.1.1']`. The listing then reads each announcing server's real address from `X-Forwarded-For`. Without this, every server would appear to come from HAProxy, the UDP check would go there, and no server would ever be listed |
| `history_hours` | `24` | hours shown in each server's chart |
| `sample_every` | `240` | seconds between the player counts the charts are made of |


## 4. Set up Apache

### 4.1 Modules

```sh
a2enmod php8 alias dir rewrite authz_core headers expires remoteip
a2enmod -l          # each module listed once; names without "mod_"
```

`headers`, `expires` and `remoteip` are optional (the vhost below wraps them
in `<IfModule>`), but they're worth having.

### 4.2 The vhost

`/etc/apache2/vhosts.d/openfp.retrocs.org.conf`, for Apache behind HAProxy
(TLS at the proxy, plain HTTP on port 6669 inside):

```apache
<VirtualHost *:6669>
    ServerName   openfp.retrocs.org
    ServerAdmin  support@retrocs.org

    DocumentRoot /srv/www/vhosts/openfp.retrocs.org

    # PHP. SUSE keeps the .php mapping in conf.d/mod_php8.conf and expects each vhost
    # to include it; the explicit mapping covers builds where its <IfModule> doesn't match.
    Include /etc/apache2/conf.d/mod_php8.conf
    <FilesMatch "\.ph(p[3457]?|tml)$">
        SetHandler application/x-httpd-php
    </FilesMatch>
    <FilesMatch "\.phps$">
        Require all denied
    </FilesMatch>
    DirectoryIndex index.php

    # .htaccess in the folder denies the private parts too
    <Directory /srv/www/vhosts/openfp.retrocs.org>
        Options -Indexes +FollowSymLinks
        AllowOverride All
        Require all granted
    </Directory>

    # the private parts: never served, whatever an .htaccess says
    <DirectoryMatch "^/srv/www/vhosts/openfp\.retrocs\.org/(inc|data)(/|$)">
        Require all denied
    </DirectoryMatch>
    <FilesMatch "^(config\.inc(\.example)?\.php|dev-router\.php|.*\.(md|sql))$">
        Require all denied
    </FilesMatch>

    # the API is always fresh (it says so itself too); the site's images, styles and
    # scripts may be cached for a week
    <IfModule mod_headers.c>
        <LocationMatch "^/api/">
            Header set Cache-Control "no-store"
        </LocationMatch>
    </IfModule>
    <IfModule mod_expires.c>
        <Directory /srv/www/vhosts/openfp.retrocs.org/assets>
            ExpiresActive On
            ExpiresDefault "access plus 7 days"
        </Directory>
    </IfModule>

    # logs with the client's address instead of HAProxy's. (%b, not %O: %O needs mod_logio.)
    # The listing itself uses trusted_proxies in config.inc.php, which must list the same proxy.
    <IfModule mod_remoteip.c>
        RemoteIPHeader        X-Forwarded-For
        RemoteIPTrustedProxy  172.16.1.1
        LogFormat "%a %l %u %t \"%r\" %>s %b \"%{Referer}i\" \"%{User-Agent}i\"" proxy
        CustomLog /var/log/apache2/openfp.retrocs.org-access_log proxy
    </IfModule>
    <IfModule !mod_remoteip.c>
        CustomLog /var/log/apache2/openfp.retrocs.org-access_log combined
    </IfModule>
    ErrorLog /var/log/apache2/openfp.retrocs.org-error_log
    LogLevel warn

    <IfModule mod_deflate.c>
        AddOutputFilterByType DEFLATE text/html text/css text/plain application/javascript \
                                      application/json image/svg+xml
    </IfModule>

    HostnameLookups Off
    UseCanonicalName Off
    ServerSignature Off
</VirtualHost>
```

Port 6669 needs a `Listen 6669` somewhere in the Apache configuration
(on SUSE: `/etc/apache2/listen.conf`).

**Without HAProxy**, with Apache doing TLS itself, use `<VirtualHost *:443>`
with `SSLEngine on` and the certificate lines, leave out the `mod_remoteip`
block (keep the `combined` log), and leave `trusted_proxies` empty. Add a
`<VirtualHost *:80>` with the same document root and PHP lines that redirects
everything to https *except* `/api/`:

```apache
    RewriteEngine On
    RewriteCond %{REQUEST_URI} !^/api(/|$)
    RewriteCond %{REQUEST_URI} !^/\.well-known/
    RewriteRule ^ https://%{HTTP_HOST}%{REQUEST_URI} [R=301,L]
```

### 4.3 Behind Cloudflare

Cloudflare answers before HAProxy does, so two things are set there as well:

1. **Plain http for `/api/`.** Turn off *SSL/TLS → Edge Certificates → Always Use HTTPS*
   (it would send `/api` to https too, with a 301), and make the redirect a rule instead:
   *Rules → Redirect Rules*, expression
   `(not ssl and not starts_with(http.request.uri.path, "/api"))`, dynamic redirect to
   `concat("https://", http.host, http.request.uri.path)`, 301, keep the query string.
2. **Cloudflare's addresses as trusted proxies,** so the listing sees the real caller
   (an announcing server's address is checked): its ranges from
   https://www.cloudflare.com/ips/ go into `listing → trusted_proxies` beside HAProxy's.

### 4.4 A game server on the same network

A game server inside your own network reaches the listing the inside way, so the listing
sees an inside address (often the router's, 172.16.1.1 here) and can't list or check it.
Tell it which public address those stand for, and turn on NAT reflection for the game
port's port forward on the router, so the check from the inside gets through:

```php
'local_public' => ['from' => ['172.16.0.0/16'], 'address' => '178.174.216.246'],   // under 'listing'
```

### 4.5 HAProxy: keep /api/ on plain http as well

The game and the dedicated servers use `https://openfp.retrocs.org/api/` when they
are built with libcurl (macOS always, Linux with `libcurl-devel`). Builds without it,
and old machines that can't do modern TLS (a AmigaOS client), ask
`http://openfp.retrocs.org/api/` instead, and announcements (POSTs) don't follow
redirects. So `/api/` has to keep answering plain http too. On HAProxy's port-80
frontend, redirect everything else to https but leave `/api` alone:

```
http-request redirect scheme https code 301 if !{ ssl_fc } !{ path_beg /api }
```

Both the http and the https frontend send `openfp.retrocs.org` to the backend
at this host's port 6669, and HAProxy adds `X-Forwarded-For`
(`option forwardfor`).

### 4.4 Start

```sh
apachectl configtest      # "Syntax OK"
systemctl restart apache2
```

If configtest complains:

| Message | Meaning |
|---|---|
| `Invalid command 'Header'` | mod_headers isn't loaded: `a2enmod headers`, or keep it inside `<IfModule mod_headers.c>` as above |
| `Unrecognized LogFormat directive %O` | that needs mod_logio: use `%b` (as above) or `a2enmod logio` |
| `DocumentRoot … does not exist` | the files are one folder too deep: unpack with `--strip-components=2` as in step 1 |
| `module … is already loaded, skipping` | a module is listed twice in `APACHE_MODULES` in `/etc/sysconfig/apache2`; harmless, but remove the duplicate |


## 5. Check that it works

On the web server:

```sh
H='Host: openfp.retrocs.org'
curl -s -H "$H" http://127.0.0.1:6669/api/?action=stats
# {"servers": 0, "players": 0, "playing": 0, "peak_players_24h": 0, "matches": 0}

for u in / /server/ /source/ /api/ /assets/site.css; do
    curl -s -o /dev/null -w "%{http_code} $u\n" -H "$H" "http://127.0.0.1:6669$u"; done      # all 200
for u in /config.inc.php /inc/listing.php /inc/migrations.php /README.md; do
    curl -s -o /dev/null -w "%{http_code} $u\n" -H "$H" "http://127.0.0.1:6669$u"; done      # all 403
```

From outside, once HAProxy is set up:

```sh
curl -s http://openfp.retrocs.org/api/?action=stats       # over http, no redirect
curl -sI https://openfp.retrocs.org/ | head -n 1          # 200
```

If something's wrong:

| Symptom | Look at |
|---|---|
| `/api/` answers **503** "the listing database is unavailable" | the reason is in `/var/log/apache2/openfp.retrocs.org-error_log`: wrong `dsn` host or port, the user's host part in MariaDB doesn't match the web server's address, a firewall on 3306, the password, or `ssl_ca` / `REQUIRE SSL` |
| `Table 'openfirepower.servers' doesn't exist` in that log | the tables aren't made yet: `php inc/migrate.php up` (step 2.3) |
| pages show PHP source | PHP isn't active for the vhost: `a2enmod php8`, and the `Include` / `SetHandler` lines |
| `/inc/` or `config.inc.php` answer 200 | the deny blocks are missing, or `AllowOverride` is off **and** the vhost lacks them |
| servers announce but never appear | `trusted_proxies` isn't set behind HAProxy (step 3), or the server's UDP port isn't open to the internet: the listing must be able to reach it |


## 6. Fill the database: servers announcing themselves

The list fills itself. Each dedicated server that should appear needs these
in its `server.cfg`:

```ini
public      = yes
listing_url = https://openfp.retrocs.org/api/
```

Its UDP port (29870 by default) must be open to the internet. Every minute
the server announces itself. The listing asks it over UDP whether it's really
there, then writes it to `servers` and, every 4 minutes, a player count to
`samples`. The server appears on https://openfp.retrocs.org/server/ and in
every game's FIND SERVERS. When it stops, it removes itself; if it just
vanishes, it drops off after 3 minutes.

To watch it happen:

```sh
mariadb -h DB.HOST -u openfirepower -p openfirepower \
    -e 'SELECT host, port, name, players, max_players, FROM_UNIXTIME(last_seen) FROM servers'
tail -f ofp_server.log           # on the game server: "listing: …" lines if something's refused
```

The answers a server can get, and what they mean:

| Answer | Meaning |
|---|---|
| `ok listed as HOST:PORT` | in the list |
| `409 no answer from HOST:PORT (UDP)` | the listing couldn't reach the server: open the UDP port, or set `trusted_proxies` if every server shows HAProxy's address |
| `429 too many announcements` | more than `rate_limit` in 10 minutes from one address |
| `503` | the listing can't reach its database (step 5) |


## The API, for reference

| Request | Answer |
|---|---|
| `GET /api/?action=list` | JSON: every server with its numbers and last 24 hours, plus totals |
| `GET /api/?format=text` | one tab-separated line a server, which is what the game reads |
| `GET /api/?action=stats` | the totals only |
| `POST /api/` with `action=announce` | a dedicated server announcing itself |
| `POST /api/` with `action=remove` | a dedicated server leaving |
| `POST /api/` with `action=match` | a listed server's finished match (only from that server's address) |
| `GET /api/?action=matches` | the match history (`&server=HOST:PORT`, `&limit`, `&before`) |
| `GET /api/?action=replay&id=N` | a match's replay, as a file |
| `GET /api/?action=leaders` | the best-rated players across the servers (`&server=HOST:PORT` for one) |
| `GET /api/?action=version` | the newest release, `VERSION<tab>URL` (or `&format=json`), for the games' update notice |

The complete endpoint, field and response reference is at `/api/docs/`. Browser visits to
the bare `/api/` URL show that guide; explicit actions and format queries keep
the machine API responses. Clients with no HTML Accept header retain the default JSON list.


## Upgrading

Unpack the new version as in step 1, then `php inc/migrate.php up`. New settings in
`config.inc.example.php` are worth copying over; the old ones keep working without them.

## Ladders

A ladder is a season on chosen servers, set up in `config.inc.php`, with its standings
under `/statistics/?view=ladders` worked out from the match history: a win is 3 points, ties go to more
wins, then the bigger rating gain. With `teams`, fixed teams are ranked too (a team
plays when one of its members does, and wins when they do). Nothing is stored for it:
change the dates or the servers and the table follows.

```php
'ladders' => [
    ['slug' => 'winter-2026', 'name' => 'Winter Ladder 2026', 'about' => 'Versus on the RetroCS servers.',
     'servers' => ['game.retrocs.org:29870'], 'from' => '2026-12-01', 'to' => '2026-12-31',
     'min_matches' => 5, 'teams' => ['Steel Wolves' => ['Anna', 'Bert'], 'Night Owls' => ['Cleo', 'Dora']]],
],
```

The Ladders menu item only appears when there's at least one.

## The map gallery

`/maps/` lists the maps included with the game alongside the maps shared by players.
Run `python3 tools/build_map_gallery.py` from the project root after changing the
bundled maps, and deploy the refreshed `assets/bundled-maps.json` with the site.
The catalog uses `data/maps/` and `data/maps/classic/` and excludes private disk imports.
Both bundled and shared maps can be filtered by world and compatible game mode.
Shared maps show a preview, the world, compatible modes, author, and downloads. Uploads are checked (an `.ofpmap` the game can read,
up to 200,000 bytes), the same map twice is kept once, and one address may share ten a day.
For uploads, install the current `ofp_match` on the web host and set its absolute path
as `site.ofp_match` in `config.inc.php`. The PHP worker must be able to run it with
`exec` so it can check that a match starts before the map is published.
Servers can play them: `maps = gallery:NAME` in `server.cfg`. To take one down, set
`admin_token` in `config.inc.php` and sign in to `/admin/`; moderation uses checked forms and a session.

## The daily challenge

`/statistics/?view=daily` shows each day's board. Games send a win's replay; the site checks that it is
that day's battlefield and keeps each player's fastest. To count only real wins, put
`ofp_match` on the web host (`cmake --build build-server`, copy `build-server/ofp_match`
to e.g. `/usr/local/bin/`) and set `'ofp_match' => '/usr/local/bin/ofp_match'` under
`site` in `config.inc.php`: replays are then played back, and a win that doesn't check
out is refused. Without it, results are shown as unchecked.

### Checking in the background

With `ofp_match` set, wins wait in a queue and the game gets its answer at once; a
cron line plays them back, every minute, as the web server's user:

```
* * * * *  wwwrun  php /srv/www/vhosts/openfp.retrocs.org/inc/daily_worker.php
```

Times under 30 seconds, or under half the best of three or more, are marked "unusually
fast" and score no points in the week's league. A webhook with the event `daily` hears
when someone takes the lead.

## Watching live

Public servers with `allow_watching = yes` (and `share_replays = yes`) send the match
that's on every 10 seconds, cut by their `spectator_delay`; the server cards get a
"Watch live" link to `/replay/?live=HOST:PORT`, which plays it about 10 seconds behind.
Nothing to set up on the site (migration 8 makes the table).

## The replay viewer

`/replay/?id=N` (the Watch links on the matches and player pages) plays a match in the
browser with the game's core as WebAssembly, `assets/ofp.wasm`, built by
`tools/build_web.sh` (clang, lld and wasi-libc). It ships built; rebuild it after a change
to the game's rules. It needs nothing on the server: the browser does the work.

## The admin page and privacy

`/admin/` takes maps out of the gallery (and back) and forgets a player. Its password is
`admin_token` in `config.inc.php`; log in once and a session keeps you in (an HttpOnly,
SameSite cookie, the only one the site sets), every change is a checked form, and five
wrong tries make it wait a minute. From the command line: `php inc/forget.php NAME
[HOST:PORT]`. `/privacy/` tells visitors what's kept and for how long; set `contact` in
`config.inc.php` so it says whom to ask.

## Player pages

`/player/?name=Anna` (and `&server=HOST:PORT` for one server): rating over time, matches
and wins, favourite tank, achievements, how they do in each world, recent matches with
replays. Names on the leaderboard, the matches, the ladders and the server list lead
there. It's all from the match history: nothing extra to set up.

## Webhooks

The site can tell a chat room when a new server appears on the list, when someone shares
a map, and when a ladder gets a new leader:

```php
'webhooks' => [
    ['url' => 'https://chat.retrocs.org/hooks/...', 'format' => 'text', 'events' => ['server_new', 'map', 'ladder']],
],
```

`text` posts `{"text": ...}` (Rocket.Chat, Slack, Mattermost, Matrix's hookshot);
`discord` posts `{"content": ...}`. Dedicated servers have their own (`webhook_url` in
`server.cfg`) for their matches and achievements.

## Trying it locally

A MariaDB in a container (Podman; `docker` takes the same arguments), and PHP's own web
server instead of Apache:

```sh
podman run -d --name ofp-db -p 127.0.0.1:3306:3306 -e MARIADB_ROOT_PASSWORD=dev \
    -e MARIADB_DATABASE=openfirepower -e MARIADB_USER=ofp -e MARIADB_PASSWORD=ofp docker.io/library/mariadb:11
cp config.inc.example.php config.inc.php
#   'dsn' => 'mysql:host=127.0.0.1;port=3306;dbname=openfirepower;charset=utf8mb4',
#   'user' => 'ofp', 'password' => 'ofp', 'ssl_ca' => '', 'create_tables' => true
php -S 127.0.0.1:8080 dev-router.php
```

(`create_tables => true`: the first request makes the tables, as `php inc/migrate.php up` would.
`podman rm -f ofp-db` throws it all away again.)

The site is then at http://127.0.0.1:8080/ and the API at
http://127.0.0.1:8080/api/. A dedicated server lists itself there with
`listing_url = http://127.0.0.1:8080/api/`, and the game shows it with
NETWORK GAME → LISTING set to the same address.

## Public information and screenshots

The canonical website is https://openfp.retrocs.org and the designated public
repository is https://github.com/retrocs-org/openfp. A blank legacy `site.repo_url`
falls back to that repository, so an existing deployment gains the link without
replacing its database configuration.

Current screenshots live in `assets/img/20261111-*.png`, with captions and platform
labels in `inc/screenshots.php`. The About section shows three highlights and an expandable gallery with the rest.
All thumbnails share an equal-sized frame and retain the complete image; controller
diagnostics and graphics settings are left out. The former `/screenshots/` route
redirects to the home page gallery. These are rendered game/editor frames,
not concept artwork. Keep the full game frame and leave out desktop window borders.

Deploy the complete website update archive, including `inc/`, `statistics/`,
`source/`, `assets/`, and `maps/`, plus the compatibility redirect folders. Keep the server’s existing `config.inc.php` and uploaded
files. The full deployment instructions above describe database migrations.


## Navigation

The site uses a shared paper-and-ink layout with orange accents, a gameplay-led
home page and responsive navigation. The home page introduces OpenFP and the original game's history, followed by
screenshots and an open game-mode list, before **Get the game**, with a compact client list, per-platform requirements and a separate Linux server panel.
The hero labels the game unreleased until a stable client release is discovered. Setup instructions and keyboard mappings are
omitted from the home page. `/statistics/` groups Leaderboard, Daily challenge and
Matches into bookmarkable tabs; configured ladders also appear there. Mode, date,
server and pagination filters stay in each tab's URL. `/source/` contains the source
downloads and expandable build instructions for each platform; direct section links open
the matching instructions. The header is limited to Home, Servers, Statistics,
Maps and Source; GitHub remains in the footer.

Old `/activity/`, `/play/`, `/build/`, `/leaders/`, `/daily/`, `/matches/`, `/ladders/` and `/screenshots/`
URLs redirect to their new locations, preserving relevant query parameters.
`/play/` redirects to `/` without replacing existing download fragments.
Deploy the redirect folders too so existing bookmarks keep working.
