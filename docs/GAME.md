# OpenFirePower: the game in detail

This is the long guide to the game: modes, screens, maps, controls, sounds and
how it's built. The short version is in the [README](../README.md).

## Everything in the game
* a tank selection screen
* a scrolling destructible desert
* one drivable tank with shooting and tile destruction
* a side panel with score, fuel and radar
* a home base per side: like the original, three garages in a row, one per tank. Your tank starts in its own garage and rolls out of the door, and it respawns there after a loss
* 12 kinds of building (1×1 up to 4×3 tiles) that take damage and explode as a whole
* 4 kinds of gun turret (MG, cannon, homing rockets, heavy gun) that aim and fire at you; tanks have armour and lives
* parked vehicles: cargo trucks, fuel tankers, jeeps, APCs, half-tracks and towed guns in your motor pool, the enemy fortress, convoys along the roads and beside enemy buildings. They block your way and your shots. Shooting them leaves burnt-out wrecks, tankers go up in a big fireball, and the crew bails out and runs
* sound: synthesised effects (cannon, MG, rockets, mortars, explosions, ricochets, splats, a different engine drone per tank, helicopter rotors), or your disk's own sounds via `ofp_adf.py theme`
* track marks: every tank leaves two rows of ruts in sand, earth and snow, fading after half a minute (none on roads)
* run-over soldiers leave a stain that stays for minutes, slowly drying from red to brown
* radar: a nearby scrolling map or nearby tank dots (R); Settings → Game Options chooses the detail
* the engine labours and drops in pitch while a tank pivots on the spot
* light and shadow: buildings, walls and towers cast shadows; fireballs, flashes, tracers and rockets glow; wrecks keep burning, flickering and smoking for a while. As in the original, a fallen building leaves its remains standing: broken wall stubs, corner posts and debris, full of glowing embers that pulse and slowly go dark over a minute and a half, with smoke and sparks drifting off them. **Dusk and night battles** (each map sets its own time of day) dim the lights or turn them off, and then you see by: headlights, sweeping searchlights on the watch towers, lamps over garage doors and at buildings, blinking helipad lights and depot beacons, helicopter spotlights, and everything that burns
* living water: deeper colour away from the shore, drifting wave crests, sun glints, foam along rounded banks, and splashes with rings and spray when shots or shells come down in it (calm and murky in the jungle, glinting ice in winter)
* particle explosions: flash, shock ring, fireballs, rising smoke, sparks, bouncing debris with shadows, embers, scorch marks, screen shake
* four map styles, chosen per game, each with its own map generation: classic (the 1987 look re-imagined), desert, jungle and winter
* original-disk support: `python3 tools/ofp_adf.py theme FirePower.adf --remix` adds styles made from your own Amiga disk (see [docs/THEMES.md](docs/THEMES.md))

## Network play

**NETWORK GAME** on the first page:
* **FIND SERVERS** lists the public servers from the server listing (set its address under LISTING), with a password prompt for locked ones.
* **SERVER** + **PASSWORD** + **JOIN** connects to an address directly.
* **HOST** runs a server in the game itself, for the game chosen on the main page.

Each server has a **lobby** with team columns, avatars, tanks, latency, ready status and chat. Select a team or tank with the direction controls. Select **Chat** before typing, or **Keyboard** for the on-screen keyboard. **F1** toggles Ready; the first human is the lobby host and can start once the others are ready when auto-start is off. Select **Leave** to disconnect. Leaving an active match returns to its lobby; a surviving player can rejoin, while an eliminated player can watch until the next match. The map capacity is at most eight tanks, including bots.

**Dedicated server:** `cmake -S . -B build-server -DOFP_BUILD_SDL2=OFF && cmake --build build-server` builds just `ofp_server` and `ofp_match` (Linux, macOS; SQLite for statistics). Configure it with `server.cfg` (see `server.cfg.example`): name, message of the day, password, max players, public or not, the game, the statistics database, chat log, log file. **Listing service:** `public-website/`, a small PHP + MariaDB site where public servers announce themselves, with a server list page like OpenTTD's.

Play is deterministic lockstep with the server as referee. Only buttons travel (a few bytes a tick), and every 50 ticks each machine's game is compared with the server's. Every match is saved as a replay (`last.ofprep` beside the settings); `ofp_match verify` plays one back and checks it. The details are in [docs/NETWORK.md](docs/NETWORK.md).

## Title screen

The game opens on a title screen with a battle going on behind it: two computer tanks fighting it out on a fresh map, the camera switching between them. Any key, click or button goes to the game-type screen. Left alone there for a minute, the game goes back to the title and its battle.

## The game-type screen

The game starts on a game-type screen, like the original's mode screen:
* **GAME:** 1 player, 2–4 players versus or as a team (only the modes this build can do)
* **ENEMY TANK:** none, easy, normal or hard. Versus games have no computer tank.
* **WORLD:** Classic, Moon Base, Winter, Desert, Jungle, Swamp, Volcano or Wasteland.
  Classic keeps the original style of play and its three original tank classes.
  Enhanced worlds add world vehicles, newer modes, optional pickups and weather.
  Classic has no weather or slippery-surface effects.
* **MAP:** a random battlefield or a bundled/custom map for that world. Imported
  disk maps stay local. The preview shows the selected map and team areas.
* **TIME:** each map has its own time of day (day, dusk or night), shown here. For a random battlefield you pick it. The original levels are under CLASSIC, along with those that come with the game and your own. A picture of the map sits beside the menu, both camps and flags included, and for RANDOM it shows exactly the battlefield you'll get.
* **START GAME**, **MAP EDITOR**, **OPTIONS** and **QUIT**

**OPTIONS** has:
* **CONTROLS:** tank or original
* **VIEW:** wide or close-up
* **AUDIO** has separate **MUSIC VOLUME** and **EFFECT VOLUME** meters. Left and Right adjust each from mute to level 10. Music defaults to 7 on macOS and AmigaOS. On macOS, effects default to 5.
* **GRAPHICS → MARKS** can hide blood while keeping scorch marks.
* **GRAPHICS → COMBAT FX** controls camera shake, flashes and nearby tank engine sounds on desktop. Calm also softens lightning in thunderstorms.
* **GRAPHICS → EXPLOSIONS** offers Full, Simple and Off. Simple uses small fire bursts with fewer particles.
* **GRAPHICS → GROUND FIRES** switches lingering flames, smoke and glow from wrecks and ruins on or off.
* **GRAPHICS → WEATHER EFFECTS** hides rain, snow and other map weather in matches while keeping time-of-day lighting.
* **SCREEN** switches between a window and edge-to-edge fullscreen on desktop.
* **REDEFINE KEYS** for players 1 and 2, with a reset to the defaults. Players 3 and 4 use pads.

Arrows or a pad choose (up/down) and change (left/right). Fire or Enter selects. Esc goes back. After a game, **F3** returns here.

Everything is saved as you go and comes back next time. This includes the hotkeys (Z, N, C, F5, F7) and the last terrain and game type. The settings live in `settings.ini`:
* macOS: `~/Library/Application Support/OpenFirePower/`
* Linux: `~/.local/share/OpenFirePower/`
* Windows: `%APPDATA%\OpenFirePower\`

**Replays.** Every match you play is kept: the last one, and the 30 newest in `replays/`
next to `settings.ini`. **Tools → Replays & Awards** lists them, along with `.ofprep`
files in your Downloads folder. Player 1 can use their keys, D-pad or stick to choose
a replay and Fire to watch it. The Watch, Awards and Back buttons also work with
the mouse and controller. Drop a replay on the window or
start with `--replay FILE` to watch it straight away. Space pauses, Up and Down change
the speed (1x to 16x), Left and Right go 10 seconds back or on, Tab follows the next tank,
Esc goes back.

**Music.** Each world and the menus have a song. Drop tracker modules (`.mod`, `.xm`,
`.s3m`, `.it`) named `classic`, `desert`, `jungle`, `winter`, `moon`, `volcanic`, `wasteland`, `swamp` or `menu` into the
`music` folder beside `settings.ini` and the game plays them instead of its own
synthesised tunes; `data/music/README.md` says more. OPTIONS → MUSIC sets the level.

**Extras.** EXTRAS on the main menu adds pickups (a repair kit, mines, fuel: drive over
them; they come back elsewhere after 45 seconds) and the hill near the middle of the map:
a side that holds it alone, for two minutes all told, wins, just as bringing the flag
home does. The bar at the top shows each side's time; the radar shows where they are.

**Signing in.** As on a games console. The first button on the title screen, on your
keyboard or pad, opens your profile window: pick your profile, or make one: a nickname, then
one page with your tank (the card as on the selection screen; left and right through the
three) and, below it, ten avatars to pick from. Then you're in, and you're the
players' count for every game, local or online. Anyone else presses FIRE on their own
pad or key set, on any menu page, and gets their own window; up to four. The bar along
the first page shows who's in; hold MINE there for a second to sign out (a pad unplugged
signs out too). Each picks their own tank with their own keys or pad, every match (in the
lobby: MINE). The keyboard has four key sets (OPTIONS → KEYS, KEYS FOR: players 1-2 or
3-4; sets 3 and 4 are empty until you give them keys); four pads work as they are.
On a pad, the d-pad and the left stick both move everywhere,
the on-screen keyboard included. Profiles are kept in `profiles.ini` beside the settings.

The game types and worlds follow how many are in (the Original world takes two at most),
and the server list shows which servers have room for all of you: the others say NO ROOM,
and QUICK MATCH leaves them out.

**Local games: modes and the team screen.** LOCAL GAME sets the MODE: CAPTURE THE FLAG
(the classic: bring their flag home), CONQUEST (three capture points; alone on one for five
seconds takes it; every second each point held scores one, and 300 wins), WARLORD (arena fighting, a
boss tank a side: destroying theirs scores, seven wins; computer players escort their own),
TEAM DEATHMATCH
(arena fighting: two sides, each coming in in its own half, no flags; the
first side to 20 enemy tanks wins) , ANNIHILATION
(every tank for itself, the last one standing wins; out of the garage, a tank is safe for
five seconds or until it fires) or WARLORD (each side has a boss tank, the
Warlord: five times the armour, taking a third of the damage, faster and firing twice
as fast, but no mines; destroying theirs scores, the role passes to the next of their
side, and 7 wins). The Original world plays capture the flag only,
two in all, as in 1987. BOTS says how many computer players; AI LEVEL how good they are.
START GAME opens the team screen (in annihilation one list, every tank for itself): the camp on the left, the fortress on the right, people
and computer players spread as evenly as they go. Everyone here, on their own keys or pad,
moves left or right to a side (a computer player on that side swaps with you, so the sides
stay even), picks a tank (up, down) and says ready (FIRE; MINE: not yet). When everyone's
ready, player 1's FIRE starts. A side holds no more than its garages, a match no more than
four. After a match: A (F1) again, X (F2) the tanks and sides, B (F3) the menu.

Conquest maps: Crossfire, Three Rivers, Frostline; any map can have its own points
(`points=x,y x,y x,y` in the map file), and random battlefields get them placed: the middle,
and between it and each camp. Checkpoints within radar range appear as hollow diamonds.
They are white until captured, then take the controlling team's color.

**On a pad,** B is back in the menus; once players are signed in, the menus are player 1's.

**The menu.** The first page: LOCAL GAME, NETWORK GAME, TRAINING (or, left or right,
the DAILY CHALLENGE), the MAP EDITOR, REPLAYS & AWARDS, OPTIONS. LOCAL GAME: everyone signed in
plays; the TYPE for that many (TOGETHER against the
computer; or 1 vs 1, 2 vs 1, 2 vs 2), then the enemy, the WORLD, the MAP, the time and the extras.
The Original world is the 1987 game's, for one or two: with three or four it isn't offered
(nor its levels).

**The daily challenge.** On the first page, TRAINING, left or right: DAILY CHALLENGE.
Every day (UTC) a battlefield of its own, the same for everyone, alone against the
computer in the Bison; bring their flag home as fast as you can. A win is sent to the
web site (openfp.retrocs.org/daily/), which keeps your fastest and says your place.

**More maps.** FIRE on the main menu's MAP row opens the web site's map gallery: a
picture of each, FIRE to download it into your maps (and it's picked).

**Keys and pads.** OPTIONS → KEYS: every key of players 1 and 2 (choose a row, press the
new key), and a pad's FIRE and MINE buttons (A, B, X, Y, LB or RB; the triggers always
fire and lay mines). DEFAULTS puts them back.

**Watching.** Watching a match (F2 in the lobby) or a replay: Tab follows the next tank,
F a free camera (W A S D or drag with the mouse), O the whole map at once. D: the director, the camera goes where the action is. Along the top,
every tank's tally (tanks destroyed / lost; K hides it), and for a few seconds who
destroyed whom.

**Is the map playable?** In the editor, F7 checks: a way from every garage to the other
camp (an error if there's none at all; a warning if only by shooting walls), first aid
on your side and a prison camp on theirs, open ground for the hill. Saving says if
something's wrong.

**The editor.** K and X set the map's own weather and extras (a map that has them is
always played that way); U and R undo and redo, F fills an area of one kind of ground, M at two
corners copies and V pastes, 1 to 5 set the brush, F10 plays the map from the cursor,
F1 lists every key.

**Watching replays.** Tab or 1 to 6 follow a player (by name when the replay knows it);
click or drag along the timeline to go anywhere in the match.

**Training.** New to it? TRAINING on the main menu is a first mission in five steps: drive,
shoot, lay a mine, rescue prisoners, take the enemy's flag home. The instructions are at
the top, an arrow points to the next goal, and there's no enemy tank to hurry you.

**Time of day, weather and an adaptive enemy.** Every battlefield can be fought by day,
at dusk or at night. Enhanced modes can also use the weather available for their world.
A map file can set both (`time=`, `weather=`). A random battlefield gets them from its
seed, and the map panel shows the result. AI LEVEL: ADAPTIVE makes the computer harder when you're ahead and
easier when you're not, and remembers where it got to.

**With a pad.** The on-screen keyboard comes up for names, addresses, passwords and chat:
the d-pad moves, A types, B deletes, X is Shift, Start is done, Back cancels. In the lobby:
A ready, X tank, RB side, LB chat, Y watch, Start join the match that's on. In a network
match, LB chats.

**Awards.** Ten achievements (win without losing a tank, three tanks in one match,
win at night, in all five worlds…), earned at this machine and on servers. New ones show
at the bottom of the match report; REPLAYS & AWARDS, Tab, lists them all.

**Sharing maps.** In the map editor, F8 saves the map and shares it with the web site's
gallery (openfp.retrocs.org/maps/). Maps downloaded from there into your Downloads folder
show up under MAP.

**Colour-blind friendly sides.** OPTIONS → SIDES: BLUE/ORANGE + MARKS swaps the
enemy's red for orange (blue and orange tell apart for the common kinds of colour
blindness) and adds a shape over every tank, a ring over ours and a triangle over theirs,
with enemy tanks on the radar drawn as crosses. `--sides orange` for one run.

**Music.** Each world has its own tune, made by a small synthesiser as it plays, and the
menus have one too. OPTIONS → MUSIC: off, low (the default), medium or high. The faithful
1987 world plays without, as the original did in battle.

Command-line options (`--night` for a random map at night, `--enemy hard`, `--versus`, `--time-limit 10` for 10-minute matches…) apply to that run only. `--skip-menu` goes straight to the tank choice.

## Maps and the map editor

* **Original maps:** your copy of the game comes with the six original levels in `data/maps/original/`. That folder is git-ignored, because it's the 1987 game's own level data and shouldn't go into the public repository without the rights holders' agreement. On a fresh clone, the game brings them in by itself at start-up if it finds your `.adf` (in its folder, Downloads, Desktop or Documents). You can also **drop the `.adf` on the game window**, or choose OPTIONS → IMPORT ORIGINAL MAPS. That looks for an `.adf` in the game's folder, Downloads, Desktop and Documents. The six original levels go to your own maps folder; no Python is needed. `python3 tools/ofp_adf.py theme FirePower.adf` also imports them, along with the disk's graphics and sounds. Then pick WORLD: CLASSIC or DESERT on the game-type screen, and the map.
* **Map editor:** paint the ground, place buildings and guns for either side, move the two home bases, then save (F2) or save and play straight away (F9). A random map makes a good start (G).

See [docs/MAPS.md](docs/MAPS.md) for the controls and the plain-text `.ofpmap` format.

## How to play

* **Win:** find the enemy base, blast open its flag bunker, drive over the flag and bring it back inside your own base walls.
* **Rescue prisoners:** shoot POW camps open and the prisoners run to your tank. Stop, and they climb in. If you drive into them, they die. Park next to your first-aid station, stop, and they get out. Every 15 men you save is worth an extra tank. The heavy tank carries the most.
* **Don't camp:** stand still outside your base for 5 seconds and helicopters come for you. They take off from the nearest helipad, or fly in from off-screen if there is none. They return to the pad once you have kept moving for a while. A shot-down helicopter is replaced after 30 seconds.
* **Fuel and armour:** running dry costs a tank. Your garage refuels, repairs and restocks mines.
* **Soldiers** patrol near their barracks and shoot at you. When you blow up a manned building, its survivors run for their lives. Run them over or shoot them.
* **The final report** at the end counts your tanks and men lost, men rescued, helicopters and soldiers destroyed, and your score; the match stands still behind it. Its buttons: PLAY AGAIN (the same map), NEW TEAMS (or TANKS) and MENU; left/right chooses, FIRE (A) presses, B goes to the menu. Every new game gets a new map unless you pass `--seed`.
* **The enemy tank.** As in the original, a computer-driven tank plays against you by the same rules: a light tank on easy, medium on normal, heavy on hard. It starts out guarding its side of the map and goes for anyone who comes too near its camp. After a while (5 minutes on easy, 2½ on normal, 1½ on hard) it raids your camp, blasts open your flag bunker and runs your flag home. If it gets the flag inside its walls, you lose. It fights whenever it sees you, and it limps home to its garage for repairs when badly hurt or low on fuel. It chases you down if you take its flag, and takes its own flag back if you drop it. On hard it also leads its shots, dodges, and drops mines behind it when chased. Shoot it down for 500 points, and if your flag is lying out in the open, drive over it to send it back home.
* **Soldiers on both sides.** Your camp's barracks have their own squads: khaki uniforms with **blue** armbands, like our flag, shown on the radar in your colour. They never shoot you, your tank doesn't run them over, and they open fire on enemy soldiers and the enemy tank. Enemy soldiers wear green with **red** armbands and do shoot you. When the two sides meet, they fight it out.
* **Bushes** don't stop you: drive through them and they're flattened.
* **Gates:** each base's gates have posts either side and a barrier that lifts as a tank
  comes near, as in the original.
* **Trees** (the big crowns, as in the original) you drive under: they hide you, and the
  computer's tanks don't see you there unless they're close. Shots pass through them.
* **Map sizes:** LOCAL GAME's SIZE for a random battlefield: small (64 x 64), medium (96),
  large (128, the original's) or huge (192); a map file has its own. Servers: `map_size`.
* **Your own buildings** can be shot down too, for no points. The garages are indestructible.
* **Mines** go off the moment something drives over them, then they're gone. The enemy has laid minefields around the map, so watch for the dark discs. Your own mine gives you two seconds to get clear, then it blows up anything, you included. Every 5 men you rescue earns an extra mine. Every 5 men you rescue earns an extra mine.
* **Guns** only fire along the 8 compass directions, as in the original, and only when they have a clear line to you. Approach at an angle, or keep a wall or building between you and them, and they can't hit you. Mortars, which lob over walls, are the exception.
* **Fuel:** destroying an enemy fuel depot tops up your tank.
* **Radar:** it shows the ground around you, moving troops (orange), prisoners (white), helicopters (red) and, once it is in range, the flag you're after (blinking, in its side's colour).
* **Flags:** ours is royal blue with a gold star, the enemy's red with a white bar. Both are black-outlined so they stand out on any ground. A flag lying in the open has a pulsing ring round it, and glows at night.

### Two to four players: split screen

On the tank selection screen, **Tab** (or a pad's View / Create button) switches between the modes:

| Mode | Who | Against whom |
|---|---|---|
| 1 PLAYER | you | the computer tank (F7 sets how good it is) |
| 2P VERSUS | P1 holds our camp, P2 the enemy's | each other, as in the original |
| 2P TEAM | P1 and P2 together | the computer tank |
| 3P: 2 VS 1 | P1 and P3 against P2 | each other |
| 3P TEAM | all three together | the computer tank |
| 4P: 2 VS 2 | P1 and P3 against P2 and P4 | each other |
| 4P TEAM | all four together | the computer tank |

In the "versus" modes the enemy players have the enemy's guns, soldiers and minefields on their side, and helicopters stay out of it.

**Every player starts from a garage of their own**, so a map's garages set how many can play on each side. Our camp has four garages and the enemy fortress three, so 2 vs 2, 2 vs 1 and 4 together all fit. The game-type screen shows the garages under the map picture ("GARAGES 4 + 3") and only offers the game types that fit. If you pick a map with less room, it steps down to the biggest game that fits and says so.

**Choosing tanks:** each player picks their tank with left and right, and everyone presses fire to start (or press Enter).

**The screen:**
* With two players it splits in two, with a window and a compact panel each.
* With three or four, the picture doubles in size each way and every player gets a full one-player layout in a quarter of it. With three, the fourth quarter shows a map of the whole battlefield with both flags and every tank.
* Three or four players are best played on a big screen or in fullscreen (F11).

Sound follows every player's tank, and each pad rumbles for its own player. A side that runs out of tanks loses.

**Controls:** players 1 and 2 can share the keyboard; players 3 and 4 need a pad each.

| | Player 1 | Player 2 | Players 3 and 4 |
|---|---|---|---|
| Drive | Arrow keys | W A S D | pad |
| Fire | `.` (period) | Space or Left Alt | A / Cross, R1, R2 |
| Mine | `,` (comma) | Left Shift or E | B / Circle, X / Square, L2 |

**Which pad is whose:** with a pad for every player, pad 1 is player 1, pad 2 player 2, and so on. With fewer pads, they go to the last players first. For example, 4 players with 2 pads: players 3 and 4 get the pads, and players 1 and 2 share the keyboard.

Start straight into a mode with `--versus`, `--coop` and `--players 3` / `--players 4`.

Three- and four-player split screen is for the modern platforms (macOS, Linux, Windows). The AmigaOS 3.x port splits the screen for at most two players, like the original, and joins larger games over the network. The limit is a build option (`-DOFP_MAX_LOCAL_PLAYERS=2`); see [docs/PORTING.md](docs/PORTING.md).

### Community sounds (OpenGameArt, remixed)

`python3 tools/fetch_community.py` downloads a few freely licensed sounds from OpenGameArt.org:
* Nayckron's tank engine loop (CC-BY 3.0)
* Thimras's cannon fire, wall hit and splash (CC0)
* rubberduck's bangs (CC0)

The tool writes experimental sound effects to `data/sounds/community/`. It trims each sample, shapes its envelope or makes its loop seamless, and mixes it with the game's synthesised effects. The accompanying `CREDITS.txt` records each source, its author and licence, and the changes. Decoding OGG files needs ffmpeg (`brew install ffmpeg`).

### Game controllers

Xbox (One / Series) and PlayStation (DualShock 4, DualSense) controllers work over USB and **Bluetooth**:
1. Pair the controller in your system's Bluetooth settings:
   * Xbox: hold the pair button until the logo flashes fast.
   * DualSense: hold Create + PS until the light bar flashes.
2. Start the game.

Pads can be connected or removed while playing. The game rumbles when you are hit and hard when you lose a tank, and turns a DualSense or DualShock 4 light bar to your colour. `data/gamecontrollerdb.txt` (the SDL community database, zlib licence) adds mappings for hundreds of other pads. The window title shows the connected pad.

## Build

macOS (Apple Silicon or Intel):

    brew install cmake sdl2
    cmake -B build
    cmake --build build
    ./build/openfirepower

Linux:

    sudo zypper in cmake gcc SDL2-devel        # openSUSE
    sudo apt install cmake gcc libsdl2-dev libcurl4-openssl-dev     # Debian/Ubuntu (libcurl: https)
    cmake -B build && cmake --build build && ./build/openfirepower

Tests and a headless smoke run:

    ctest --test-dir build --output-on-failure
    SDL_VIDEODRIVER=dummy ./build/openfirepower --smoke

Options:
* `--seed N` picks the generated map.
* `--style NAME` preselects a map style.
* `--theme NAME` starts with a UI theme.
* `--shot file.bmp` and `--shot-select file.bmp` save frames during `--smoke` (or `--smoke-report`, which ends on the final report).

The game finds `data/` next to the executable, in the source tree, or wherever `OFP_DATA` points.

## Controls

| Action           | Keyboard                 | Controller               |
|------------------|--------------------------|--------------------------|
| Drive / reverse  | ↑ / ↓                    | Left stick / D-pad       |
| Turn             | ← / →                    | Left stick / D-pad       |
| Fire             | `.` (period)             | A / Cross, R1/RB or right trigger |
| Lay a mine       | `,` (comma); with original controls also stop + Down + Fire | B / Circle, X / Square or left trigger |
| Pause            | P                        | Menu / Options           |
| Radar near / far | R (also in OPTIONS)      |                          |
| Enemy tank       | F7 cycles none / easy / normal / hard, from the next game (`--enemy hard`) | |
| One step back    | Esc (game → tanks → game-type screen → quit) | View / Create |
| Close-up view    | Z (or start with `--zoom`) | Y / Triangle           |
| Controls mode    | OPTIONS → CONTROLS: **tank** (the default: up/down drive, left/right turn) or **original** (push where you want to go, as in 1987); `--classic-controls` | |
| Mute all audio   | F6 (`--mute` on startup) |                          |
| Choose tank      | ← / → then Fire or Enter | D-pad / stick, A         |
| Choose terrain   | ↑ / ↓ on the selection screen | D-pad / stick       |
| Fullscreen       | F11, Alt/Cmd+Enter       |                          |

## Architecture

```
src/core/            pure C99 simulation: no OS calls, no floats, no SDL
  fixed.[ch]         Q16.16 fixed point, 256-step sine table
  world.[ch]         tile map, tile properties, destruction, collision
  worldgen.c         procedural maps per biome (until map files and the editor exist)
  game.[ch]          tanks, shots, effects, fixed 50 Hz tick
src/platform/sdl2/   macOS / Linux / Windows frontend
  theme.[ch]         loads map styles (data/styles/) and UI themes (data/themes/)
  render.[ch]        world view (autotiled), sprites, HUD, radar
  select.[ch]        tank selection screen
data/styles/         classic, desert, jungle, winter   } generated by
data/themes/default/ HUD, selection pictures, icons    } tools/gen_styles.py
tools/ofp_adf.py     reads the original Amiga .adf: extract graphics/maps/sounds,
                     or build the classic theme in one step
tools/gen_sounds.py  the built-in sound effects (synthesised)
tools/ofp_import.py  generic importer (IFF ILBM, raw bitplanes, mapping file)
third_party/         stb_image, font8x8 (public domain)
tests/               core tests (determinism, destruction, collision)
```

The rules that keep later phases cheap:

* **Inputs in, state out.** `game_tick()` takes one `InputBits` byte per player.
  Network play only has to exchange those bytes (lockstep). Local split screen
  and AI players feed the same struct.
* **Deterministic.** All game logic is integer/fixed-point and the checksum is
  endian-independent. A Mac, a Linux PC and an Amiga produce identical
  simulations, and `game_checksum()` detects desyncs.
* **Viewports.** `render_view()` draws the world into any rectangle following
  any tank. Split screen means calling it 2–4 times per frame.
* **Art is data.** Tile types, directions and cell sizes are fixed by the
  core; how they look is a theme. The Amiga backend will convert the same
  sheets to planar graphics at build time.
* **Backends are replaceable.** A native AmigaOS 3.x backend replaces only
  `src/platform/`.

## Roadmap

1. ~~Skeleton: map, scrolling, tank, shooting, destructible tiles~~
2. ~~Bases, flag capture, POW camps, fuel/repair, mines, radar, helicopters~~
3. ~~AI opponent (single player)~~
   ~~Game-type screen and remembered settings~~
4. ~~Local split screen, 2–4 players (modern platforms; Amiga: 2)~~
5. Linux and Windows release builds in CI
6. ~~Lockstep network play through a dedicated referee server~~ ✓ done; still to come: resync after a desync, a lobby list, and mixed local + online players (see [docs/NETWORK.md](docs/NETWORK.md))
7. ~~Map editor and map file format; the original maps from your disk~~
8. AmigaOS 3.x backend (68040 or 68060): up to 2 players split screen, more over the network (see [docs/PORTING.md](docs/PORTING.md))

## License

Code and the default theme: MIT, see `LICENSE`. `third_party/` is public
domain. Graphics imported from the original game are not covered and must
never be committed.

**Arenas: supply drops.** Every 40 seconds a crate parachutes in somewhere open (the radar
blinks it yellow, everyone's told): the first tank over it gets armour, fuel and mines.
Only in arenas (the deathmatches), never on the camp maps.

**The computer players' level** matters: easy is slow to react and sloppy; hard reacts at
once, aims tight, and finishes off the weakest enemy it can see.

**Your profile's record.** After every match, each signed-in player's profile counts it:
matches, wins (by mode), tanks destroyed and lost, the tank played most. The profile
window shows it.

**Pads.** A pad rumbles when hit (harder for more), destroyed, and lightly when firing
(`rumble=0` in settings.ini: not). Every screen's hints name the pad's buttons when one is
plugged in: the server list (A join, X favourite, Y refresh, B back), the gallery (Y
again), replays and awards (X switches), the lobby (Y votes for the next map).

**The vote.** On a server with `rotation = vote`, after each match three maps made for its
mode are on the ballot (the lobby shows them and their votes); Y on a pad, or /vote N,
votes; the most votes wins, none: one of the three at random.

**Up to 8 in a match.** The arenas (team deathmatch, annihilation, warlord) hold 8: 4 a
side, each on a spawn point of its own. Capture the flag and conquest hold what the camps'
garages do (4 + 3). BOTS goes up to 7; on one machine, still up to 4 people (split screen),
the rest computer players or people online. Servers fill to what the map holds.

**Pad layouts, and your own controls.** OPTIONS -> KEYS -> PAD: DRIVE picks how a pad
drives: THE STICK (the stick or d-pad drives; A/RB or RT fire, B/X or LT mine), or RT/LT,
STICK TURNS (RT forward, LT back, the stick or d-pad turns; A/RB fire, B/X mine). Signed
in, the KEYS page is yours: your keys (the set you play on), your pad's layout and buttons,
saved with your profile and back whenever you sign in, on whichever machine has the file.

**Online with several of you here,** each signed-in player joins as themselves: every one
counts on the server (results, ratings, the web site), not only player 1.

**Network game.** QUICK MATCH, FIND SERVERS (two tabs: PUBLIC, and MY SERVERS: your own,
not on the public list; LB/RB or TAB switch, X or DEL removes one), and DIRECT CONNECT: a
server by its address (host or host:port), with its password if it asks for one; SAVE TO
MY SERVERS keeps it for next time. (The server list's address is `listing=` in
settings.ini, for running your own.)

**Local game.** MODE, AI BOTS (0 to 7, or FILL: as many as the map holds; a person who
joins takes a bot's place), AI BOTS LEVEL, WORLD, MAP (the maps made for the mode with
room for everyone, people and AI bots alike). The Original world is 1 vs 1. The team
screen shows the map between the sides: where each side starts (the camps, or an arena's
spawn points in its side's colour).

**Worlds.** Classic, Moon Base, Winter, Desert, Jungle, Swamp, Volcano and Wasteland.
Classic keeps the original style of play and excludes enhanced vehicles, power-ups,
slippery surfaces, weather effects and team orders. Enhanced worlds add their own
vehicles and terrain. Moon Base uses craters, not water. Winter contains water and
slippery ice; oil can make tanks slide in other enhanced worlds. Rain, snowstorms,
eruptions and meteor showers are restricted to the appropriate world.

**Even sides.** In the team modes (capture the flag, conquest, team deathmatch, warlord)
both sides are always as many: a camp map 4 vs 4 (its fortress has four garages too), an
arena 4 vs 4, a map made for 4 2 vs 2.
FILL fills to that; an uneven count gets one more AI bot (or one fewer, if there's no room).
Servers do the same. In annihilation, every tank for itself, the map's whole room.

**LOCAL GAME** asks in this order: WORLD, MODE (what the world offers; the Original:
CLASSIC), SIZE, MAP (RANDOM, or the world's maps of that mode and size).

**The team screen.** Everyone with their own keys or pad: LEFT/RIGHT the side, UP/DOWN the
tank, FIRE ready (your row goes green), BACK (B on a pad) not ready, to change it again.
Player 1, the host: FIRE takes you to the buttons in the middle: + BOTS, - BOTS (the sides
kept even), the AI bots' level (FIRE: easy, normal, hard, adaptive) and START (lit when
everyone else is ready and the sides are even; it says why not). UP/DOWN between them,
FIRE presses; BACK, to your side and tank again; BACK there, to the menu.
**The pause menu.** START on player 1's pad, or Escape, during a match: RESUME, SOUND,
MUSIC, SOUNDS, VIEW and QUIT MATCH (back to the menu); left/right changes, A (Enter) does
it, B, START or Escape goes back to the match. Only player 1 has it. A local match stands
still meanwhile; a network one goes on (it can't wait for you), your tank standing.

**Pads in the menus.** Outside a match a pad always works the same, whatever its buttons are
set to for playing: the d-pad or stick chooses, A (or RB) is OK, B is back; A signs a pad in,
and holding B on the first page signs it out. The FIRE and MINE settings apply in a match.

**Your profile window.** START on a pad in the menus: your record, then CONTROLLER (a pad:
its page, PAD: DRIVE, FIRE, MINE and DEFAULTS), NAME (a new one: the on-screen keyboard, or
typed), FACE (left, right), STEERING (TANK, or ORIGINAL: push where you want to go, as in
1987), SIGN OUT and BACK; all saved with the profile. When player 1 signs
out, player 2 becomes player 1; when everyone has, the game goes back to the title screen.

**Pads in a match.** The default: RT forward, LT back, the stick only turns, the d-pad drives
fully; A (Cross on a PlayStation pad) or RB fires, X (Square) or LB lays mines. Or THE STICK:
the stick drives too. Each player's own, in their profile window (START on their pad in the
menus) -> CONTROLLER: PAD: DRIVE, PAD: FIRE, PAD: MINE and DEFAULTS, saved with the profile.
OPTIONS -> KEYBOARD is the keyboard's key sets only.
**Conquest's guns.** On generated maps each half's guns belong to its side.
On maps built in the editor, guns belong to the team whose area contains them.
Guns outside both team areas are independent and can shoot either side.
The top of the screen shows both team scores. A bar appears at a checkpoint
while a team is capturing it.

**The map editor with a pad.** The left stick or d-pad moves the cursor, A paints, B erases,
RT and LT the next and previous tool, the right stick scrolls the map. START opens the map's
menu: SAVE, TRY IT, WORLD, TIME OF DAY, WEATHER, SIZE (left/right change them), NEW RANDOM MAP,
CLEAR, HELP and EXIT EDITOR.

**Thunderstorms.** Open ground is heavy going and the computer sees less far. The swamp's
mud gets worse. Eruptions and meteor showers are visual effects.

**Battle marks.** Scorches where things blew up, debris round buildings that went, small red
marks where soldiers fell (OPTIONS -> MARKS: NO BLOOD leaves those out); they stay for the
match.

**The computer players** defend the conquest points they hold (the nearest of them, when an
enemy's on one), spread out over the open ones, and wait under trees for you. They keep to the
roads (the more so in a storm or mud), go back to mend when badly hurt, and the two nearest
come when an enemy's near their flag.

**Repairs in the field.** Standing still in your own base mends your tank slowly (a point of
armour every two seconds); a garage does it far faster, and refuels and restocks too.

**Changing weather.** A random battlefield is sometimes fought in weather that changes: clear
to begin with, then a storm rolls in after a few minutes and clears again, and so on (the
same for everyone, from the match's seed). A map file can say `weather=changing`; a server's
`weather` setting accepts the map weather names or `random`. Classic mode always plays
clear. Snow is available in Winter, sandstorms in Desert and Wasteland, eruptions in
Volcano, and meteor showers on Moon Base. A meteor shower uses brighter shooting stars
at night. The map editor filters its choices by world and mode.

**Effects.** OPTIONS -> EFFECTS: FULL shakes the view for explosions near you and lets you
hear the engine of the nearest other tank, louder as it comes; CALM, neither.

**Patrols.** A few of the enemy's jeeps and trucks out on the roads drive them (three on a
large map, more on bigger ones): straight on where they can, turning at junctions, back at a
dead end, waiting for a tank in the way. One destroyed on the move is worth 50 points more.

**A side all out.** When every tank on one side has used its lives, the other side has won.

**TRAINING** takes you through six steps (drive, shoot, mines, trees, rescue, the flag), its
prompts showing your own buttons: your pad's layout, the keys, or the original's steering. It's
played on a small training ground of its own, always the same: your camp and their fortress
close together, a grove beside your camp, a prison camp halfway, three buildings to shoot, and
nothing that fights back (no enemy tanks, guns, helicopters or soldiers).

**KILL CONFIRMED.** Team deathmatch in an arena, with a twist: a destroyed tank drops a tag. Its
enemies confirm the kill by driving over it; its own side denies it the same way; tags vanish
after 30 seconds. First to 15 confirmed kills. (Server: `goal = killconfirmed`.)

**HARDPOINT.** On conquest's maps, but only one point counts at a time, and it moves on every
minute. A side alone on it scores a point a second; nobody scores while it's contested. First to
120. (Server: `goal = hardpoint`.)

**Small maps.** A map 64 tiles across gets compact bases, 22 x 16, the same for camp and fortress,
and its country's buildings and guns are shared out by nearness (as conquest does everywhere).

**Rematch.** After an online match, the report offers REMATCH (with the tally) and LOBBY; `/rematch`
in the lobby votes too. If more than half of the match's players still there want one, the next
match is the same map (or random battlefield) with everyone on the same side.

**A server's mode rotation.** `modes = flag, conquest, killconfirmed` (any of flag, hill, conquest,
hardpoint, deathmatch, killconfirmed, annihilation, warlord) plays a match of each in turn; after each
match the server moves to the next mode, then to the next of its maps made for it. The lobby shows the
next match's mode. (`goal` still sets one mode.)

**Leaderboards per mode.** The website's leaderboard has a tab per mode: the last 30 days of each
mode's shared matches, every public server's, the most wins first.

**QUICK MATCH preferences.** Left/right on QUICK MATCH chooses a mode for it to look for first
(ANY MODE, or one); with none open in that mode, it takes the best other game, and says so.


## Match limits and world vehicles

Settings → Game Options provides **Tanks / Player** (1–99 or Unlimited) and
**Match Time** (1–30 minutes). New matches default to five tanks and 20 minutes.
Unlimited tanks still respawn after destruction; every match has a time limit.
The server owns these settings during network play. Old recordings keep the
rules under which they were recorded.

Classic uses the original vehicle balance. In enhanced Volcano maps, the
Salamander, Basilisk and Behemoth trade some speed for more armour, and mine
pickups give five mines instead of three. Moon Base offers the Lynx, Orion and
Titan: lighter armour, more fuel and longer-range shells. Lunar fuel cells also
repair ten armour points. Tank classes still correspond to light, medium and
heavy, so favourite-tank profiles work across worlds.

The radar always shows the nearby area, scrolling with your tank. **Map** shows
roads, water, walls and buildings; **Tank Dots** hides that terrain detail. Both
show nearby tanks in their side colours and nearby mode objectives, such as flags
or Conquest checkpoints. Vegetation and decorative debris are excluded.

**About** rolls the credits upward on desktop and AmigaOS while playing Legion’s
Rally at your saved music volume. Fire, Enter or Back leaves the credits.


## Ice, oil and power-ups

Moon Base uses dry crater trenches instead of water. Trenches block tanks; shells can pass over them.
Winter has both open water and driveable ice. On ice, accelerate and brake early: momentum carries the tank past a turn. Oil spills on roads in other enhanced worlds have the same low grip. Leaving a slippery surface restores normal grip.

Enhanced matches can have repair kits, spare mines, fuel, **Speed Boost** (blue arrow) and **Rapid Fire** (gold twin shells). The two boosts last ten seconds; a blue B (boost) or gold R (rapid fire) badge below the tank shows time remaining, with a distinct pickup tone. Respawning removes boosts. Pickups reappear after 45 seconds.

**Settings → Game Options → Power-ups** controls local games and desktop hosting. Dedicated servers use `pickups = yes` or `pickups = no` (`powerups` is an alias). Disabling them suppresses all pickups and arena supply drops, including those requested by a map. A map with explicit `extras=none` also stays without pickups. Classic always disables power-ups and slippery surfaces.

In the editor, Winter's Ground category offers Water and Ice; Moon Base offers Crater instead of Water. Other enhanced worlds offer Oil Spill in Roads. Switching worlds replaces surfaces that the new world cannot use. Old replays keep their recorded terrain and handling.

## Short field challenges

Desktop: **Tools → Field Challenges**. AmigaOS: **Play → Challenges**.

| Challenge | World | Objective | Limit |
|---|---|---|---|
| Timed Rescue | Jungle | Free and deliver four prisoners to the medic | 3 minutes |
| Bridge Capture | Desert | Hold the bridge checkpoint until captured | 2 minutes |
| One Tank Raid | Volcano | Destroy three marked turrets with one tank | 2 minutes |

The guide arrow points to the next objective. These are local, unranked missions.
The older Desert co-op operation remains hidden and deferred.

Ice and oil leave short skid trails in enhanced worlds. Amiga Graphics has a
separate **Skid Trails** toggle to control their drawing cost. Classic does not
use these slippery-surface trails.

In a network lobby, **Status** shows connection state, latency, input backlog,
map download progress and protocol/rules versions. **Save Report** writes
`network-report.txt` beside your settings. It excludes passwords, keys and chat.

## Controller test and automatic effects

**Tools → Controller Test** shows the selected device, its signed-in player,
buttons, stick axes and triggers. Release the left stick before choosing
**Calibrate Center**. The deadzone is adjustable from 12–50% and is saved;
centre calibration lasts until the controller is unplugged or the app closes.
**Reset Calibration** restores the 37% deadzone. Rumble testing respects the
Rumble setting and requires a controller that supports it. The Amiga's **Tools → Input Test** shows
both digital joystick ports and the mapped player inputs; analogue calibration
and rumble do not apply to those ports.

**Graphics → Auto Effects** defaults to Off. When enabled, sustained slow frames
reduce decorative particles and weather density; Amiga can also use simpler
explosions. Detail returns gradually after performance recovers. This never
changes simulation ticks, damage, collision, projectiles, tanks, objectives,
radar or time-of-day lighting. Manual effects-off settings remain respected.

## Team leader orders — enhanced team games only

Pause an enhanced team match and select **Team Orders** on desktop, or an order
in the Amiga pause menu. Left/Right chooses the local player whose order is being
issued. The player must be that team's human leader. **Attack Objective** sends
bots towards the enemy flag or a hostile checkpoint. **Defend Here** and
**Regroup Here** mark the leader's current position. **Escort Me** follows that
leader. A white cross marks an active order on the team's nearby radar.

Orders last 30 seconds, can be cancelled and stop applying when leadership
changes. Returning a carried flag and urgent repairs take priority. The server
validates leadership, match assignment and the rules, then makes all network bot
decisions. Clients cannot supply arbitrary destinations.

Classic worlds, Classic rules on authored maps, and free-for-all matches have
no team order controls and reject order requests. Existing team names and lobby
administration remain available. An updated dedicated server is needed for
orders; older protocol-38 servers simply do not offer them.
