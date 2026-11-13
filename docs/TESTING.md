# Testing OpenFirePower

`ctest --test-dir build` runs everything quick: the core, the network (also with the
clock at 25 days of uptime and just before it wraps), fuzzing (packets, and `fuzz_files`: every real map and replay, damaged the way files go wrong), the music, a virtual
game pad, the training, and `same_game_*`: replays recorded by a GCC build on Linux that
every build has to play back to the same checksums.

## The rules fingerprint

Every build measures what its game computes: at start it plays a few short fixed matches
(computer players, a storm, pickups and the hill, versus, a map file) and their checksums
make the rules fingerprint. `openfirepower --version` and `ofp_server --version` show it.
A game sends it when it joins; a server with another one refuses it with "this server's
game plays by different rules": the two would get out of step. It changes when the rules
change, or when a compiler or a platform computes something differently (as GCC and
clang once did); not for a comment or for building on another machine.

## When someone gets out of step

The server notices (every second it compares each player's checksum with its own), plays
that player's match again from the start, and the first time in a match writes:

- `desync-DATE-NAME-tickT.ofprep`: the match's replay, beside the server's log
- `desync-DATE-NAME-tickT-server.txt`: its state at that tick, as text

and the player's game writes, in its settings folder:

- `desync-DATE-tickT-game.txt`: the state there by its own rules (the match played again)
- `desync-DATE-tickT-game-live.txt`: the state as it really was in that run

Then `ofp_match diff SERVER.txt GAME.txt` shows the first lines that differ, which name
the thing that went apart (a tank's position, a shot, a row of the map, a computer
player's mind). The `-game.txt` differing means that machine computes another game
(rules, compiler, platform); only `-game-live.txt` differing means a one-off in that run.
By hand, on any machine: `ofp_match dump REPLAY TICK OUT` (or `openfirepower --dump
REPLAY TICK OUT`, on Windows too) and `ofp_match diff A B`.

## The soak test

On a server, for hours: `tools/soak.sh` starts `ofp_server` and a few `ofp_bot`
(headless players that play like the game does, in lockstep, with checksums), and
every minute writes the server's memory, CPU and open files to `samples.csv`. At the end,
`report.txt`: matches played, anyone out of step, errors, a crash, memory and open files
from the first hour to the end; exit status 0 only if all is well.

```sh
cmake --build build-server -j"$(nproc)"          # ofp_server and ofp_bot
tmux new -s soak 'tools/soak.sh --hours 24 --bots 6 --extras --near-wrap'
```

`--near-wrap` starts the clock 30 minutes before it wraps (day 49.7 of uptime), so the
soak goes through it; `--listing URL` announces on a listing too; `--help` lists the rest.

## Manual checks

Automatic repository jobs are disabled for now. Run the build and release tests
manually using [BUILDING.md](BUILDING.md) and [RELEASING.md](RELEASING.md).
To compare rules across platforms, save each build's `ofp_server --version`
output as `rules-NAME.txt`, then run `sh tools/compare_rules.sh rules-*.txt`.

## The balance test

Is one side favoured, by the maps, the rules or the computer players? `ofp_match balance`
plays equal sides (the same tanks, every one the computer's, at one level, side against side as
the team screen and the server play: no extra enemy tank from the computer) on the maps you give
it and on random battlefields, and counts each side's wins:

```sh
# capture the flag on every bundled map made for it, 4 seeds each, and 40 random battlefields
./build/ofp_match balance --goal flag --seeds 4 --random 40 --jobs "$(nproc)" --csv balance-flag.csv data/maps/*.ofpmap data/maps/original/*.ofpmap
# the other modes
./build/ofp_match balance --goal conquest --seeds 4 --random 20 data/maps/*.ofpmap
./build/ofp_match balance --goal hill --random 30
```

Options: `--goal` (flag, conquest, hill, annihilation, deathmatch, warlord), `--level`
(easy, normal, hard), `--side N` (tanks a side, 4; fewer if a map hasn't room), `--seeds N`
(matches a map), `--random N`, `--world NAME` (random battlefields in one world; else every
world in turn), `--minutes M` (a match's limit, 20), `--jobs N` (N matches at a time, each in a
process of its own: use the machine's cores; the results are the same), `--csv FILE` (a line a match). A map
whose `modes=` leaves the mode out is skipped. The last line says whether the difference is
more than chance gives (more than about twice the square root of the decided matches).
A match takes a few seconds on one core; a hundred, ten minutes or so, divided by `--jobs`.


## Bot strategy tournament

Repeat a completed run after a gameplay change with its exact saved maps and seeds:

```sh
python3 tools/ai_tournament.py --engine build-ai/ofp_match \
  --baseline /path/to/previous-results --matches 1236 --jobs 4
```

`--baseline` reuses completed scenarios in match order, including their team-size
and match-time limits. `--matches` selects a smaller paired sample; omit it to
repeat every completed scenario. Map hashes are checked before running.
`comparison.md` and `comparison.json` compare objective points, flag pickups,
natural finishes, base time, attack time and low-progress time. Compare the same
sample before drawing conclusions; self-play statistics do not prove strength
against human players.


`tools/ai_tournament.py` plays private, accelerated bot-versus-bot matches and
saves statistics for strategy work. It does not connect to a public server,
change the bots or train them automatically. Python 3.9+ is required.

Build the headless match tool on Linux or macOS:

```sh
cmake -S . -B build-ai -DCMAKE_BUILD_TYPE=Release -DOFP_BUILD_SDL2=OFF -DOFP_BUILD_TESTS=OFF
cmake --build build-ai --target ofp_match --parallel
```

Start with a short check, then run for up to eight hours:

```sh
python3 tools/ai_tournament.py --engine build-ai/ofp_match --matches 12 --minutes 6
python3 tools/ai_tournament.py --engine build-ai/ofp_match --hours 8 --jobs 2
```

On this development Mac, the existing evaluation build can be used directly:

```sh
caffeinate -i python3 tools/ai_tournament.py \
  --engine $HOME/Development/builds/OpenFirePower/AI/Tools/ofp_match \
  --hours 8 --jobs 2
```

By default, results go into a new dated directory under
`~/Development/builds/OpenFirePower/AI/`. Use `--out PATH` to choose another
new directory. Leave the command running, or use a terminal session manager
such as `tmux` on Linux. Ctrl-C stops the run and keeps completed results.

The runner covers the currently supported bundled maps and their allowed
modes, normal and hard bots, and two bot presets: `server` matches the dedicated
server's level-only setup; `local` uses the opening raider/siege roles of an
all-bot local Capture the Flag game. Classic retains the original classes and
does not gain the enhanced raider/siege preset. Team sizes are clamped to the
map's capacity, with at most eight tanks total. Each new round uses a fixed seed.

Use repeatable `--map FILE` arguments to narrow the run, `--levels normal`,
`--profiles server`, `--side 1` for two tanks, or `--minutes 20` for the simulated
match limit. The match limit can never exceed 30 minutes. `--timeout` limits
wall time per match; `--replays N` controls how many initial matches are saved.
Matches run faster than real time; `--hours` is a wall-clock budget.

The output directory contains:

- `report.md`: finishes, time limits, base/attack/repair time, flag pickups and
  matches with little movement to inspect.
- `matches.csv`: one row per team per match, suitable for a spreadsheet.
- `results.jsonl`: full per-match statistics and exact reproduction commands.
- `manifest.json`, `engine/` and `maps/`: a snapshot of the executable and maps,
  their hashes, source revision and run settings.
- `replays/`: the first N recorded matches, with resolved authoritative bot inputs.

Attack time measures AI intent, not every moment spent fighting. Low progress
is a routing investigation aid: turning or combat may also explain it. Base
time can be legitimate defence or repair. These measures overlap. Time-limit
points wins are reported separately from finishes before the limit. A self-play
win rate alone cannot show that a strategy improved; compare the same maps,
seeds, team sizes, presets and match lengths before and after a change.

For a single reproducible run and its playback check:

```sh
build-ai/ofp_match evaluate --map data/maps/ctf-duel-at-dawn.ofpmap \
  --profile server --level normal --side 1 --seed 7919 --minutes 20 --out bots.ofprep
build-ai/ofp_match verify bots.ofprep
OFP_MATCH="$PWD/build-ai/ofp_match" python3 tests/test_ai_tournament.py
```

The integration checks cover deterministic statistics, Classic restrictions,
replay checksums, input bounds, parallel reporting, timeouts and budget shutdown.

## The same on every compiler

The game's rules must give the same result whatever compiles them: gcc (Linux, Windows), clang (macOS,
the website's WebAssembly), vbcc and gcc for the Amiga. C leaves some things to the compiler, such as the
order in which a call's arguments are worked out, so two random draws as two arguments of one call land
differently under gcc and clang (that was the arenas' supply drop, until protocol 30). Draws go one to a
statement. To check, play the replay fixtures (two of them arena matches) with a clang build:

```sh
clang -std=c99 -O2 -Isrc -o /tmp/clang_match src/core/*.c src/common/replay.c src/common/dump.c src/tools/ofp_match.c
for f in tests/replays/*.ofprep; do /tmp/clang_match verify "$f"; done      # every checksum should match
```

Run the CTest `same_game_*` fixtures after native builds. Check the WebAssembly
core with `node tools/replays_wasm.js tests/replays/*.ofprep` after building it
with `sh tools/build_web.sh`. Run the native replay checks below to validate an
Amiga build. A new mode or rule worth guarding gets a fixture:
`ofp_match bots --seed N --extras X --out tests/replays/NAME.ofprep`.

## Windows test kit

Use the portable ZIP's `windows-qa.ps1` and `WINDOWS-TESTS.md`. The script runs
engine, training and virtual-controller tests, checks their completion messages,
and writes separate logs plus `results.json` on the Desktop. It uses isolated
test profiles. The manual checklist covers MSI install/uninstall, display scaling,
fullscreen, physical controllers, explicit chat focus and mixed-client reconnects.
A successful cross-build does not complete this real Windows testing.

## Recovery, controllers and enhanced orders

`ctest -R '^(polish|editor|direct|stick|pad)$' --output-on-failure` covers checked,
rotating recovery generations (including a damaged newest draft), unfinished-map
restoration, controller calibration and menu navigation. Normal map loading and
playability checks still reject incomplete maps. Draft recovery accepts unfinished
work without bypassing parser bounds or structural checks.

For a manual recovery check, edit a disposable unfinished map, wait 30 seconds,
end the process, and relaunch. Restore should preserve its placed objects and
leave it unsaved. Discard should clear the recovery offer. A manually saved map
must remain untouched. In the Amiga Shell, `OpenFirePowerOnline
--editor-recovery-test` exercises four draft generations, corrupt-latest fallback
and unfinished-map loading using its own temporary test files.

The `polish` test also checks automatic-effects hysteresis, leader transfer,
order cancellation, forged/non-leader and stale-match requests, and identical
replay checksums for resolved server bot inputs. **Classic worlds, Classic rules
and free-for-all games reject team orders.** On every client, confirm the pause
menu hides them in Classic and offers them only to a human leader in an enhanced
team game. Network orders need the updated server; older protocol-38 servers do
not advertise this optional feature.

Use upstream SDL2 2.32.10, the framework used by the release packaging, for the
controller suite. If a system SDL compatibility package fails synthetic input
tests, repeat with the release framework before attributing it to the game.

Physical Amiga and Apple TV tests, and the Windows checklist, remain on
the release validation checklist in `docs/TODO.md`. Keep detailed local run
logs outside the source release.

## Amiga performance and reconnect checks

In the Amiga Shell, from the game drawer:

```
OpenFirePowerOnline --benchmark-detailed
OpenFirePowerOnline --stability-test PRIVATE-SERVER PORT 20
```

`benchmark.txt` compares library, blitter and CPU copies in solo, split-screen
and snow/combat scenes. It reports average and worst frame time, an approximate
95th percentile, simulation, terrain, objects, HUD, display wait and copy timings.
The histogram uses millisecond buckets. Use the same scene, CPU, memory and
display configuration before comparing results.

The stability run joins, readies, reaches a running match, disconnects twice
(idempotent cleanup), then loads a different local world and opens/closes music.
Use a private server with auto-start enabled, one required human, bots to fill
remaining seats, and a supported fixed map. `stability.txt` records map bytes,
Chip/Fast memory, sampled stack margin and untouched stack below a 4 KB guard.
Inspect the post-warmup memory trend; a single transient allocation is not a leak.

Local AI tests verify bounded route work and that recorded resolved inputs replay
with identical checksums. The virtual-controller test covers rename/chat keyboards,
profile focus, match reports, replay/awards buttons, editor menus and unplug/reconnect.

## Linux and Apple TV

Use openSUSE Leap 16 as the preferred Linux build and test platform for both
ARM64 and x86_64 packages. Test the desktop client and dedicated server on their
matching target architecture. The portable client must launch from an unrelated
current directory. Include desktop display and controller checks on the target
hardware. Optional local DEB packages need separate Debian/Ubuntu runtime checks.

Apple TV simulator checks cover the engine and controller-driven menus, keyboard,
chat, replays and editor. Device output requires your signing team. A physical
Apple TV/controller test and cloud-backed saves are pending before public release.
