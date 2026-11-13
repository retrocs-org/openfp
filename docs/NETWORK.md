# Network play

**Status: working.** Lockstep network play through a match server, a lobby
with chat, statistics and a leaderboard, password-protected and publicly
listed servers, a server listing service, replays and the headless match
runner are in. See "What's built" and "Dedicated servers" at the end.

# Network play: the plan

Not built yet. This is the design to build it to, written down so the work
can start from it later. See also [PORTING.md](PORTING.md) for how it fits the
Amiga port.

## Choice: deterministic lockstep, with a dedicated server as referee

There are three standard ways to run a networked game:

| | Peer-to-peer lockstep | **Server-relayed lockstep ("referee")** | Authoritative state server |
|---|---|---|---|
| Who simulates | every player | every player **and** the server | only the server; clients display |
| Over the wire | inputs | inputs | full game state, many times a second |
| Bandwidth | a few bytes per tick | a few bytes per tick | kilobytes/s per player |
| Faking state (armour, flags, scores) | caught by checksum, but who's right? | **impossible**: the server's simulation decides | impossible |
| Seeing hidden information | possible | possible | preventable |
| Host advantage, NAT trouble | yes | no | no |
| Works on an Amiga | yes | **yes** | hardly |

Fast shooters (CS, Valorant) use authoritative state, because hidden
information is their main cheat. RTS and fighting games, whose simulations
are deterministic like ours, use lockstep. **OpenFirePower uses lockstep
through a dedicated server that runs the match and referees it.**

### Why this fits

* `src/core/` is pure C99 with no floats and no OS calls: the same seed plus
  the same buttons give the same game everywhere, on any CPU and byte order.
  `game_checksum()` proves it.
* `game_tick()` takes nothing but one byte of buttons per tank. Local
  players, the computer opponent and network players all use that one
  interface.
* The dedicated server runs the online computer opponents in `ai.c`, including
  pathfinding. Clients receive their commands with the player inputs for each tick.

The current wire protocol is 39, with team-order-independent arena starts, world-specific vehicles, configurable tank allowances, slippery surfaces and power-ups. All clients and dedicated servers must be updated together; older protocols are refused with an explanation. New online recordings use
`OFPREPLAY 2` and store the official commands for both players and bots. The game and
web viewer still play version 1 replays with the original local AI behavior. Recordings
without `arena_spawns=1` retain the previous arena starting positions.

## How a match runs

1. **A dedicated server** (`ofp_server`, headless, no SDL; a small Linux
   binary or a Docker image) runs the very same core as the clients.
2. **The server owns the match:**
   * it picks the seed, game mode, map and tanks, and sends them at the start
   * it decides every online bot's commands, using a separate random stream for AI
3. **Every tick, clients send only their players' buttons.** The server
   accepts them for the assigned human slots and enforces a deadline. The server
   adds the bot commands and broadcasts the complete input set for that tick.
4. **Clients apply the official inputs in lockstep.** They simulate movement, hits,
   terrain damage and the rest of the match from the commands they receive.
5. **Every few seconds clients send `game_checksum()`.** A mismatch means a
   modified client or a bug. The server sends official tick history for a
   resync or drops the player after repeated mismatches.
6. **Scores, wins and statistics come from the server's simulation only,**
   never from what a client reports.

## What this protects against, and what it doesn't

**Stopped:**
* Faking state: armour, mines, fuel, flag captures, hits, scores. Any of it
  desyncs from the server, which notices.
* Invalid inputs: only the six button bits, at most one input per tick, and
  inputs only for your own tanks.
* Host advantage: no player's machine is "the game".

**Not stopped, and how we handle it:**
* **Information cheats.** A hacked client knows the whole state, so it could
  show the whole map or where the hidden flag is. In Fire Power this matters
  little: the map is visible anyway, and only the radar range limits what you
  see. With lockstep every client must know everything to simulate; that's
  the trade-off.
* **Input bots** (auto-aim, auto-dodge). No architecture fully stops these.
  The answer is server-side statistics and **replays**.
* **Lag tricks.** Handled by the fixed input delay and the server's deadlines.

## Replays

A replay is the match settings plus every tick's inputs: a few KB per match.
It replays exactly, thanks to determinism. The server keeps one for every
match, which gives:

* dispute and anti-cheat review
* "watch match" and spectators
* regression tests: a recorded match must reproduce its recorded checksums

## Feel: input delay, then rollback

* **Plain lockstep** needs an input delay of about 3–4 ticks (60–80 ms at
  50 Hz) to hide latency. That's fine on a LAN and regionally, and it's what
  the Amiga uses.
* **Rollback** (GGPO-style) for long-distance matches on modern clients:
  predict the other players' inputs, then re-simulate when the real ones
  arrive. The whole game state is one plain struct that can be copied and
  restored, so this is feasible. It's purely a client-side upgrade; the
  server needs no change.

## Building blocks to add

1. **`game_save` / `game_load`:** serialise and restore the full state, for
   resyncs, rejoining and rollback.
2. **Replay recorder and headless match runner.** These are the heart of the
   server, testable without any network.
3. **`src/net/`:** the protocol, shared by the SDL client, the server and the
   Amiga client.
   * UDP; every packet repeats the last few inputs, so a lost packet costs
     nothing.
   * Messages: hello/version, match setup, inputs, the tick's input set,
     checksum, snapshot, bye.
4. **`src/server/`:** the headless server, with the match loop, deadlines,
   checksum verification and replay writing.
5. **Lobby / matchmaking / accounts** as a separate service, handing out
   tokens. This keeps the match server small and stateless between matches.

## Rules to build in from the start

* **Version handshake.** The core version, `OFP_MAX_TANKS` and the map's
  hash must match, or the match doesn't start. A mismatch would only desync
  later anyway.
* **Mixed games are normal.** Local split-screen players and remote players
  can share one match: a machine simply sends several input bytes per tick.
  Local and network players together stay within `OFP_MAX_TANKS`.
* **The server never trusts a client** for results, timing or anything that
  isn't that client's own buttons.
* **Platforms mix.** Any client that builds the same core can play: macOS,
  Linux, Windows, and the Amiga over `bsdsocket.library` (plain lockstep, no
  rollback). A null-modem link via `serial.device` could also serve two
  machines without a server, as in the original.


## What's built

| Piece | Where | Notes |
|---|---|---|
| Match setup | `src/core/match.[ch]` | everything that decides how a match begins; `match_start()` builds the same game anywhere |
| Replays | `src/common/replay.[ch]` | setup + map + every tick's buttons (run-length coded) + a checksum every 250 ticks; text, a few KB a match |
| Match runner | `ofp_match` (`src/tools/ofp_match.c`) | `bots` plays the computer against itself and saves the replay; `verify` plays a replay back and checks every checksum |
| Protocol | `src/common/net.[ch]` | UDP, byte-packed little-endian messages, `'O' 'F' version type` header |
| Server | `src/common/server.[ch]`, `ofp_server` | lobby, map transfer in 1 KB chunks, official tick sets, checksum referee, replay |
| Client | `src/common/client.[ch]` | join, load, send buttons a few ticks ahead, play official ticks |
| In the game | NETWORK GAME | QUICK MATCH, FIND SERVERS, or JOIN an address |
| Tests | `tests/test_net.c` | replays round-trip and catch tampering; a real 2-client match over 127.0.0.1 stays in step with the server; a lying client is caught |

### How a match runs

1. The server knows the game type (players, sides, enemy, world, map, time).
   Players join in order and get slots: player 1 is tank 0, and so on. With
   `--versus`, odd players hold the enemy fortress.
2. When everyone's in, the server sends the final setup. Each client fetches
   the map in chunks, checks the setup's hash, and says READY.
3. Tick by tick, each client sends its buttons `NET_DELAY` (4) ticks ahead,
   repeating the last 8 in every packet. The server makes a tick official
   once everyone's buttons are in, or after 250 ms (late buttons count as
   none). It runs the tick itself and broadcasts the official sets: from
   whatever each client last confirmed, so lost packets are made good.
4. Every 50 ticks each client reports its checksum. If one differs from the
   server's, the match stops with "out of step". The server's game is the one
   that counts.
5. A player silent for 5 s is dropped (their tank stands still); the match
   ends when a side wins or everyone has left. The server writes the replay.

### Playing

* **In the game:** NETWORK GAME on the first page. Choose the game on the main
  page first (for example 2P VERSUS, a world, a map), then **HOST**. The
  others type your address under SERVER and pick **JOIN**. Port 29870 (UDP)
  must be reachable on the host.
* **Dedicated server:** `ofp_server --players 2 --versus --world desert`
  (see the top of `src/server/main.c` for all options), and everyone JOINs it.
* Each machine plays one tank, full screen. Mixing local split screen with
  network players is still to come.
* Every match, local or networked, is recorded as `last.ofprep` beside the
  settings (`hosted.ofprep` for the referee's copy when you host). Check one
  with `ofp_match verify last.ofprep`.

### Still to come

* Joining a match that's already on as a newcomer, as a player (watching and rejoining work).
* Several local players on one networked machine.
* NAT traversal (for now a server's UDP port has to be reachable).
* TLS for talking to the listing service.
* Kicking and banning from the server console; chat during a match.
* A replay viewer in the game.
* The Amiga as a lockstep peer.


## Dedicated servers

### Building only the server

    cmake -S . -B build-server -DOFP_BUILD_SDL2=OFF
    cmake --build build-server          # ofp_server and ofp_match; no SDL needed

openSUSE Leap 16 is the preferred Linux server platform; the server also builds
on macOS. SQLite 3 is used for the statistics, leaderboard and chat log. On
openSUSE, install `sqlite3-devel`; macOS includes it. See [BUILDING.md](BUILDING.md)
for the complete dependency and build commands.

**https.** The game and the server talk to the listing over https when they're built
with libcurl: macOS has it in its SDK; openSUSE Leap 16 needs `libcurl-devel`
and `ca-certificates`. Other distributions need equivalent packages. cmake says `https for the listing: libcurl …`. Without it
(`-DOFP_WITH_CURL=OFF`), the desktop client and server use the built-in HTTP
transport for listing requests. `ofp_server --check` reports the available transport.
For a listing with a certificate from a private CA, set `OFP_CA_FILE=/path/ca.pem`.
The native AmigaOS Public list uses AmiSSL 4.x instead of libcurl; build it with
the AmiSSL SDK and install its libraries and certificates on the Amiga. Private
connections by address need only a running TCP/IP stack.

Without SQLite the server still builds and runs, without stored statistics or chat
history (`-DOFP_SERVER_SQLITE=OFF` disables that storage explicitly).

### Running it

    cp server.cfg.example server.cfg    # and edit
    ./build-server/ofp_server -c server.cfg

`server.cfg` sets everything: the name and message of the day, the UDP port,
a password (or none), the most people in the lobby (`max_players`) and how
many ready players start a match (`min_players`), the countdown, the game
(versus or together, the computer tank, world, time, a map file or random,
the original's camp), whether to publish the server (`public`, `listing_url`,
`public_host`), the SQLite database, whether to keep the chat in it
(`chat_log`), the log file and level, and a folder for replays. Logs go to
the log file (timestamped, by level) and the console (`--quiet` for only the
file). Stop it with Ctrl-C or SIGTERM; a public server takes itself off the
listing as it goes.

### The lobby

The lobby shows each player's portrait, tank, readiness, and latency in
milliseconds. Use **Left/Right** to choose a team and **Up/Down** to pick a
tank. Players move to the Camp, Auto, or Fortress column as they choose.
Each map sets the match capacity, up to eight tanks including bots.

Bot rows show the slots the server expects to fill. They adjust as humans
join or leave.

The first human to choose Camp or Fortress leads that team. The roster uses
a cap for the team leader, a flag for the host and a shield for an authenticated
administrator. A pilot can show several badges. Click the team heading or select it with **Tab** and
press **Enter** or **Fire** to open the rename popup. On a gamepad, use the
right shoulder button (Forward on CD32) to focus buttons and **A** or
**Fire** to select. **F3** opens the popup for your chosen team. In the desktop
popup, use the arrow keys or your movement bindings to move and **Fire** to
select a key. **Enter** saves the name. Names can be up to 11 characters.

Team leaders can rename their own team. The lobby host and an authenticated
admin can rename either team. From chat, use `/teamname NAME` for your
current team or `/teamname #1 NAME` and `/teamname #2 NAME` to target Camp
and Fortress. If a leader leaves or changes teams, the next human who
joined that team takes over.

On desktop, click **Chat** or focus it with **Tab** and activate it with
**Enter** or **Fire**. Press **Enter** to send or **Esc** to leave the field.
Select **Keyboard** or press the left shoulder button to open the on-screen
keyboard. Press **F1** to toggle ready and **F6** to see the leaderboard.
On AmigaOS, **Fire** or **Enter** toggles ready when no button is focused.
Use **Tab** or the CD32 shoulder buttons to focus **Chat** or **Keyboard**.
Select **Chat** with **Enter** or the keyboard Fire key to type, or press **T**
(**Shift+T** if T is bound to Player 1). Select **Keyboard**, or use joystick
Fire on **Chat**, for the on-screen keyboard. The chat and team rename keyboards
both use Player 1's directions and Fire.

Type a player's name or its prefix and press **Tab** to complete it. Messages
addressed with `@Name` or starting with `Name:` are highlighted for that
player. Use `/slap NAME` for a playful chat message. On desktop you can also
double-click a player's name in the roster.

The first human to connect is the lobby host. With `auto_start = no`, the
host or an authenticated admin can choose **Start**, press **F7**, or send
`/start` once the lobby meets `min_players` and every other human is ready.
The player who starts the match counts as ready. If the host disconnects,
the longest-connected player takes over.

With `auto_start = yes` (the default), the countdown starts when everyone
is ready and there are at least `min_players`. The server records the
results and sends players back to the lobby.

In an online match, choose **Back to Server Lobby** in the pause menu to
keep your connection. Use **Join** or **F5** to return to your reserved
tank while the match is still running. If you've lost all your tanks, you
can wait in the lobby or choose **Watch** (**F2**) on servers that allow
spectators. You can play again when a new match begins.

### Running a server: the console, the chat, reloading

The terminal `ofp_server` runs in takes commands; type `help`:

| Command | What it does |
|---|---|
| `status`, `players` | the server and who's on it (with their round trip) |
| `say TEXT` | a line in everyone's chat |
| `kick NAME`, `ban NAME [REASON]`, `unban NAME` | out, or out for good (by key and address, kept in the database) |
| `bots N` | computer players from the next match |
| `start` | start now with whoever is ready |
| `restart` | end the match that's on; back to the lobby |
| `maps`, `map N`, `map next` | the rotation, and the next map |

With `admin_password` set, a player who types `/login PASSWORD` in the chat can use the
same commands there (`/kick Bob`); everybody can use `/maps` and `/vote N`. The replies
go to that player alone. `kill -HUP` makes the server read `server.cfg` again (all but the
port), and the maps with it, between matches.

The password itself never crosses the network: the game asks the server for a challenge
(`/login`), the server sends a fresh random one, and the game answers with the SHA-256 of
`challenge:password` (`/login-proof ...`), good once and for a minute; wrong answers count
like wrong server passwords (blocked for a while after a few). A game from before this sends
the password in the clear; the server refuses it unless `admin_plain_login = yes`.

### Monitoring

`status_file` gets a small JSON document every 10 seconds (people, playing, watching,
matches, the map, the input delay, packets and bytes in and out), and `metrics_file`
the same in Prometheus' text format, ready for node_exporter's textfile collector.
(`ofp_desyncs` counts the players who went out of step.)

**Replays and desyncs on disk:** `replays = folder` keeps every match's replay; the newest
`replays_keep = 500` and no more than `replays_max_mb = 1000` stay, the oldest are deleted
as new ones come. A player out of step leaves `desync-*.ofprep` (the match) and
`desync-*-server.txt` (the server's state at that tick) in the server's folder, and the log
says which build their game is (the same as the server's or not: a different build is a
rules difference, not a bug in either); the newest `desyncs_keep = 40` are kept. Their own
game writes its state too (`desync-...-game.txt`): `ofp_match diff` shows where they part.

**IPv6:** the game and the dedicated server open one socket for IPv4 and IPv6 both (a
dual-stack socket; on a system without IPv6, an IPv4 one as before; `OFP_NO_IPV6=1` forces
that). A name with both kinds of address is reached over IPv4, one with only IPv6 over IPv6.
An IPv6 address is typed `[2001:db8::1]:29870` (or bare, without a port); the web site takes
servers by IPv6 as well. Rate limits and bans count an IPv6 player by their /64 network (one
household), an IPv4 one by their address. The Amiga stays on IPv4. A dedicated server
announces itself to the listing over IPv4 when it can (then any way): the listing lists a
server by the address its announcement comes from, and IPv4 is the one every player reaches;
give it a `public_host` name with both A and AAAA records and IPv6-only players reach it too.
`test_net` plays an IPv6 player and an IPv4 one on one server over `::1` where the system has
IPv6, and checks the address handling everywhere.

**A full server has a queue:** a player who finds it full waits in line (the game says which
place, and asks again every five seconds; Esc gives up); when a place frees, the first in line
gets it, then the next. Someone not heard from for 15 seconds leaves the line.

**Join codes:** the listing gives every server a six-character code (a letter first, a digit
fourth: never mistaken for a name), the same for the same address. Type it in JOIN instead of an
address. `public = code` in `server.cfg` keeps a server off the list but still gives it a code (the
server's log says it): for friends only. (`action=join&code=...` on the listing; a server needs to
be reachable from the internet, as for the list.)

**Through routers (NAT traversal):** a server or a game hosted at home, behind a router with no port
forwarded, can still be joined by its code. It asks a STUN server (`stun_server`, Google's public one by
default) how the internet sees its socket, and tells the listing; a player joining by the code asks the
same of their own socket and leaves that with the listing (`action=punch`; their address is the one their
request comes from, so nobody can make a server send to someone else). Every three seconds the server
fetches who wants in (`action=punches`) and sends each a few packets: that opens its router to them while
their game knocks, and the two meet. Most home routers allow it; two of the strictest kind (symmetric NAT,
some mobile networks) don't, and then a forwarded port is still the way. A dedicated server does this with
`public = code`; a game hosted in the game always, showing its JOIN CODE in the lobby
(`OFP_NO_TRAVERSAL=1` turns it off). Website: migration 13.

**Sides by round trip:** players who don't ask for a side are placed the farthest first, each onto
the side with fewer players or, as many, the lower total round trip: slow connections spread over
both sides.

**An empty server sleeps:** with nobody on it, it waits for a packet (or a tenth of a second)
instead of waking 500 times a second.

**The game closed in a match** (a crash, a power cut): started again within 15 minutes, it
says "BACK INTO YOUR MATCH ON ..." and joins that server again; the profile's key gets its
place back, as after a dropout.

### Maps: one, a rotation, a vote

`map = a.ofpmap` plays one map; `maps = maps/` (every `.ofpmap` in the folder, by name) or
`maps = a.ofpmap, b.ofpmap` a rotation, `in order`, `random` (never the same twice in a
row) or by `vote` (players type `/vote N`; most votes wins, no votes means the next in
order). The lobby shows the next map. Games keep every map they download in their own
maps folder (`net-<content id>.ofpmap`): the next time, nothing is downloaded, and the
map shows up under MAP for playing alone and in the editor.

### Starting, sides, watching

`start_after = 30` with automatic starts: once `min_players` are ready, the match starts 30 seconds later
whether or not the rest are (the lobby counts down). In team modes, **Left/Right**
selects Camp, Auto or Fortress. The server balances both teams, including bots,
and honors preferences as far as the map's garages and team sizes allow.
On desktop, someone in the lobby while a match is on can watch it (F2): the game
catches up on the match and follows it live, Tab switching between the tanks, Esc going back.

### In the match

T (or Enter) opens the chat line during a match. The last lines
show at the bottom of the view for a few seconds. The round trip to the server is shown in
the top right. The **input delay is chosen per match** from the players' round trips:
2 ticks (40 ms) on a LAN, up to 12 over a long distance; the old fixed 4 was a compromise
for both.

### Two players at one machine

NETWORK GAME → HERE: 2 PLAYERS. The game then joins the server twice (the second player
named after the first with "+2", with a key of its own made from the first), and plays
split screen. In the desktop lobby, **Tab** switches which local player the arrows and
**F1** control. A signed-in player's gamepad changes that player's choices directly.
A server's `max_per_address` (4) counts both players.

### A time limit per match

`time_limit = 10` in `server.cfg` ends each match after ten minutes.
The allowed range is **1–30 minutes**, with a default of **20**. Zero selects the
default; values above 30 are capped. Matches cannot have unlimited time.
`on_time_up = points` awards the win to the side with more points (equal points
are a draw); `draw` always declares a draw. These settings travel with the match
and are recorded in its replay.

`lives = 5` sets the starting tanks per player, from 1 to 99. Use
`lives = unlimited` for unlimited respawns (the value `0` is accepted as an alias).
The same allowance applies to humans and bots. Unlimited lives do not remove the
time limit. The lobby shows the time limit and the in-game clock counts down.
For local desktop games, use Settings → Game Options or `--time-limit 10`.

### Ratings, the web site's leaderboard and match history

Every finished match with a winner is **rated** (Elo, side against side). A side's
strength is its members' average rating; computer players count by their level (easy
1200, normal 1500, hard 1800), as does the enemy tank in co-op. Beating a stronger side
earns more than beating a weaker one, losing to a weaker side costs more, and new players
move faster (K 40 for their first ten matches, then 24). Everyone starts at 1500; a match
without a winner changes nothing. The lobby's leaderboard is sorted by rating.

A public server sends its top 10 with every announcement, and with `share_matches = yes`
(the default) every finished match: map, mode, winner, everyone's side, kills, score and
rating change, and, with `share_replays = yes`, the replay. The web site shows them on
`/server/` (each server's best three), `/leaders/` (the best across servers) and
`/matches/` (the history, with replays to download; `ofp_match verify` plays one back).
The listing only takes a match from a server it lists, from that server's own address.

Admins can also end a match with a winner: `win camp` or `win fortress` (it counts for
the ratings).

### Computer players

`allow_bots = no` in `server.cfg` turns them off altogether. Otherwise `bots = 2` adds
two computer players to every match; `fill_bots = yes`
tops every match up to four players instead, so one person can always play a full
two-against-two. `bot_level` is easy, normal or hard. In versus they take places like
people do (the odd places hold the fortress); together, they're on your side against the
enemy tank. Each gets a garage of its own (a side's garages set the limit), and they
don't appear in the statistics or the leaderboard. Their tanks are driven by the same
deterministic computer opponent as everywhere else, inside the simulation, so every
machine agrees on what they do and nothing about them travels over the network. The
lobby shows how many will come along. When hosting in the game, NETWORK GAME → BOTS sets
it.

### Who's who: player keys

Every game makes itself a random 32-byte **player key** the first time it goes
online and keeps it in `settings.ini` (`player_key`). From it, and each server's own
id, it makes a **verifier** for that server; servers never see the key, and no two
servers see the same verifier. The first time a key visits a server it registers its
verifier; after that it proves itself on every visit with SHA-256(cookie, verifier),
so an eavesdropper can't replay it.

A **name belongs to the first key that used it** on a server. Someone else calling
themselves "Ace" there gets "Ace2", so nobody can collect another player's statistics
or leaderboard place. The dedicated server keeps keys and names in its database (tables
`keys` and `names`) along with its own id and secret (`meta`), so they last across
restarts. Without a database, or when hosting in the game, they last as long as the
server runs. Keep `settings.ini` if you care about your name on a server: a new key
is a new player.

### Rejoining after a dropout, and resync

A player who drops out of a match keeps their place. The tank stands still, and the
match doesn't wait. When the same key comes back (the game retries on its own for 90
seconds and shows RECONNECTING), the server puts them back in their tank. The game
then plays the whole match again from the start, at full speed, from the server's
record of every official tick (HISTREQ / HISTORY), until it's caught up. Because the
simulation is deterministic, that rebuilds exactly the same game: no snapshot has to
travel, and machines of any kind agree.

A player whose checksum disagrees with the server's is **resynced** the same way:
their game starts over from tick 0 and catches up, while the match goes on for
everyone else. Twice is forgiven; out of step a third time in one match, and that
player is dropped from the match, but the match isn't stopped.

### Not an amplifier, not easy to flood

* **Queries must be padded** to 160 bytes (`NET_QUERY_MIN`) and are answered with
  less than that, at most 5 a second per address and 300 a second in all. A forged
  sender address can't turn a server into a flood aimed at someone else.
* **Joining starts with a cookie.** A HELLO from an address the server hasn't checked
  gets only a 29-byte COOKIE (smaller than the 160-byte HELLO): a token made from a
  server secret, the address and the time. Only a HELLO carrying that token is taken
  further, so forged addresses can't take lobby places. Cookies change every 30 seconds.
* **At most `max_per_address` people** (4) from one address at once.

### Backups, webhooks, achievements

* **Backups:** `ofp_server --backup FILE`, `backup [FILE]` in the console, and with
  `backup_dir` a copy every `backup_every` hours (24), the last `backup_keep` (7) kept.
  SQLite's online backup: a consistent copy while the server runs.
* **Webhooks:** `webhook_url` posts to a chat room (Rocket.Chat, Slack, Mattermost, Matrix's
  hookshot: `webhook_format = text`; Discord: `discord`) when the server opens and closes,
  and when a match ends: who beat whom, the ratings, and achievements earned.
* **Achievements:** ten of them (`src/common/achieve.c`), the same in the game and on
  servers. A server keeps everyone's totals (table `awards`), announces new ones in the
  lobby's chat and the webhook, and sends them to the listing with the match, where they
  show on the player's page. The game keeps your own at this machine.

### Favourites, recent servers, friends

In FIND SERVERS, F marks a server as a favourite (+); favourites come first, then
servers you joined lately (>). Servers announce who's on them, so the list also shows
where the people you've played with are: a green dot on the row, FRIENDS HERE when you
select it, and how many are online at the top. It's all kept in `settings.ini`.

### QUICK MATCH

NETWORK GAME → QUICK MATCH asks the server list and joins the best open game: the right
version, no password, room; people waiting in a lobby first, then a match on with room,
then the fullest open server; a friend there counts for more, a long way away for less.
If you're rated (a server's leaderboard knows your name: the listing's `action=rating`
gives the best of them), games whose players are near your rating come first: the listing
sends each server's players' average rating as the last column of its text list.

### A server's last match, in the game

FIND SERVERS → **L** fetches the selected server's newest match that has a replay from the
listing (`action=matches`, then `action=replay`), keeps it in `replays/` (as WEB ... in
REPLAYS) and plays it.

### Watching replays on the web site

`/replay/?id=N` plays a match from the history in the browser, with the game's own core
built as WebAssembly (`tools/build_web.sh`: clang, lld and wasi-libc), so it goes exactly
as the match went. A replay file of your own can be dropped on the page.

### The same game everywhere

Lockstep needs every build to compute the same game. The core is plain C99 with no
floating point; arguments with side effects (random draws) are never put two to a call,
since compilers may evaluate them in any order. `tests/replays/` holds replays recorded
by a GCC build on Linux, and every build's ctest plays them back to the same checksums:
on macOS (clang) that's the check that a Mac and a Linux server agree.

### Versions

A game and a server talk when each one's protocol is at least the other's *oldest*
(`NET_PROTOCOL`, `NET_PROTOCOL_OLDEST` in `src/common/net.h`; both 39 in this build). From
protocol 8 on, a change that only adds fields at the end of a message keeps the oldest:
older builds read what they know and skip the rest. Only a change that breaks a message
raises it. When they can't talk, the server says so in a BYE every build can read (its
protocol, its oldest, its version), and the game explains which side needs updating:
"this server needs a newer OpenFirePower 0.1: openfp.retrocs.org", or "this server
runs an older OpenFirePower: it needs updating". The server list marks old servers.

The game also asks the listing once at start for the newest release
(`/api/?action=version`; discovered from published tagged GitHub releases) and says so in
the menu when it's older. `check_updates=0` in `settings.ini` turns that off.

### Joining from the game

The game is a client: games are hosted by the dedicated server, `ofp_server` (Linux,
macOS). NETWORK GAME → QUICK MATCH and FIND SERVERS use the public listing; SERVER
(an address, `host` or `host:port`), PASSWORD if it asks for one, and JOIN reach any
server directly, listed or not, on the internet or your own network. Everyone signed in (see GAME.md) joins, each with their own profile's name and key, on the
same machine: two side by side, three or four a
quarter each (an empty quarter shows the whole map), each with their own keys or pad,
each a player of their own on the server. In the lobby use **Tab** to select a
local player, the arrows to choose their team and tank, and **F1** to toggle
ready. The server's `max_per_address` (4) has to allow them. Local split screen
for up to four, with no server, is LOCAL GAME on the first page.

### Joining a match that's on

While a match is on, the lobby shows the free places: computer players' tanks, and the
tanks of players who left more than 30 seconds ago. F5 joins: the server hands the tank
over in the official ticks (a takeover bit, so every machine and every replay switches
it from the computer to the person on the same tick), and the game catches up from the
start and plays on. Someone who joins late is counted in the statistics but not rated.

### Limits

* **Guessing passwords:** five wrong passwords (the server's or the admin's) from one
  address within a minute, and the server doesn't answer that address for a minute.
* **Flooding the chat:** five lines at once, then one every 1.5 seconds. Lines beyond that
  aren't passed on (the sender sees "too fast: not sent"); keep at it and it's 30 seconds
  of being muted.
* **Watching:** `allow_watching = no` turns it off; `spectator_delay = 30` shows watchers
  the match 30 seconds late, so a teammate in the lobby can't tell their side where the
  enemy is. Players who rejoin after a dropout aren't delayed.
* **The listing:** a server may announce itself under a DNS name (`public_host`), but the
  name has to point at the address the announcement comes from. The same goes for removing
  a server and for reporting its matches, so nobody can do either for someone else's.
* **Your player key** sits in `settings.ini`, which the game writes readable by you alone
  (mode 600 on macOS and Linux, and tightened on an older file).

### Passwords

A server with a password expects SHA-256(cookie, password) in the HELLO. The
cookie (see above) changes every 30 seconds and is different for every address,
so the password never travels in the clear and an answer can't be replayed. A
wrong one is refused and logged.

### The statistics

The SQLite database holds `players` (matches, wins, losses, kills, score,
first and last seen), `matches` (when, how long, how many, the world and map,
who won), `match_players` (each player's part in each match) and, with
`chat_log = yes`, `chat`. The leaderboard is the top ten by wins, then score.

### Being listed

With `public = yes` the server announces itself to `listing_url` every
`announce_every` seconds (and says goodbye when it stops). The listing
service (`public-website/api/`, PHP and MariaDB, at `/api/`) checks each announcement by
asking the server itself over UDP, so only real, reachable servers are listed,
with what they say about themselves. The game's **FIND SERVERS** shows that
list. Set the listing's address under NETWORK GAME → LISTING. See
`public-website/README.md` for setting it up.

Talking to the listing is plain `http://`: there's no TLS in the game or
server yet.

### Core match messages

Client to server: HELLO (build, name, cookie, key id, key proof, password answer,
the verifier when registering; padded to 160 bytes), PING (ready / watch, tank, chat seen,
side, a time stamp, its round trip, the ticks it has),
CHAT, LEADREQ, MAPREQ, LOADED, INPUT, CHECK, HISTREQ, BYE, QUERY (padded). Server
to client: COOKIE, NEEDKEY, WELCOME (id, name, motd, the game), PONG, LOBBY (state,
countdown, players with side, round trip and whether they play or watch, computer players,
the next map, the rotation and its votes, the start_after clock), CHAT (numbered, sent
until acknowledged; private lines are empty for the others), LEADERS, MATCH (slot or 255
for a watcher, setup, map size and hash, the tick the match is at, the input delay, the
map's content id and name), MAPDATA, TICKS, HISTORY, RESYNC, END, BYE (reason), INFO.
The server carries the bot-command flag in the match setup. `TICKS` and `HISTORY`
carry the official inputs for every tank, including bots. See `src/common/net.h`
for the little-endian wire format.


### Protocol 38: surfaces and power-ups

Build and install the server and every client from this source release together. The match setup carries the authoritative power-up switch; all clients simulate the same ice/oil momentum and boost timers. Set `pickups = no` to disable pickups and supply drops; `yes` permits them in enhanced worlds. Classic always disables them. Older saved replays keep their original rules.

### Enhanced team orders

Protocol 38 has an optional `NM_TEAMORDER` notification for a player's own team.
A running client sends `/order ASSIGNMENT KIND` through the acknowledged chat
channel. The server verifies the current assignment and first-human team
leadership, derives the destination, and runs bot pathfinding itself. Orders
are never accepted in Classic or free-for-all. Older protocol-38 servers do
not advertise this permission, so clients hide the action there. Update the
server binary to enable the feature; no website database migration is needed.
