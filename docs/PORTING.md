# Porting OpenFirePower

The shared game and each platform's frontend are built separately:

- `src/core/` contains rules, maps, physics and computer opponents. It uses C99,
  fixed-point arithmetic and a fixed 50-tick simulation, without OS calls.
- `src/common/` contains networking, replays, map checks and shared UI-independent
  services. The dedicated server decides bot inputs, routes and match results.
- `src/platform/sdl2/` contains the desktop frontend for macOS, Windows and Linux.
  The tvOS preview uses that frontend with its own platform entry point.
- `src/platform/amiga/` contains the native AmigaOS screen, input and audio code.

A frontend supplies input for each simulation tick and draws the resulting state.
`game_checksum()` and the replay fixtures check that different builds compute the
same game. Network clients reproduce the server's accepted inputs.

## Supported targets

| Target | Local players | Graphics | Requirements |
|---|---|---|---|
| macOS, Windows, Linux | Up to four | SDL2 desktop renderer | See the source README |
| AmigaOS 3.x | Up to two | Four-plane, 16-colour native display | 68040 or 68060, 2 MB Chip RAM, at least 8 MB Fast RAM; 16 MB Fast RAM recommended |
| tvOS preview | Up to four controllers | SDL2 with Metal/UIKit | tvOS 15, extended controller; signing required for a device build |

Network matches support up to eight tanks, including bots, within the selected
map's capacity. The native Amiga client supports one local seat in a network
connection. The dedicated server runs on Linux or macOS without SDL.

See [README.md](../README.md#building-from-source) for the platform build and
packaging commands. OCS, ECS and AGA Amigas all use the same 16-colour release.
Higher-colour graphics are deferred.

## Building the native Amiga client

Use Bebbo's `m68k-amigaos-gcc` with libnix and the Amiga headers, or vbcc with
vasm, vlink and the NDK headers. GCC uses the toolchain's Roadshow headers;
vbcc uses the declarations in `bsdsocket_vbcc.h`.

```sh
make -f src/platform/amiga/Makefile COMPILER=gcc CPU=68040 online local all
sh tools/amiga/stage_maps.sh build-amiga-ecs
sh tools/amiga/stage_music.sh build-amiga-ecs
```

The output folder contains:

- `OpenFirePowerOnline`: the full client, including menus, profiles, local and
  network matches, replays and the map editor.
- `OpenFirePower`: the standalone local client.
- `ofp68k`: the Shell replay, benchmark and network diagnostic tool.

The default 68040 binaries also run on a 68060. Use `CPU=68060` to build a
CPU-specific version; its default output is `build-amiga-ecs-68060/`.

For vbcc, set `VBCC` to its installation and `NDK` to the C include directory:

```sh
export VBCC=/path/to/vbcc
make -f src/platform/amiga/Makefile COMPILER=vbcc CPU=68040 \
  NDK=/path/to/NDK3.2/Include_H online local all
```

For the HTTPS Public server list, add
`AMISSLINC=/path/to/AmiSSL/Developer/include` from the AmiSSL 4.x SDK when
building. On the Amiga, install AmiSSL 4.10–4.12 and its certificates under
`AmiSSL:Certs`. A Private server connection by address only needs a working
`bsdsocket.library` TCP/IP stack such as Roadshow, AmiTCP or Miami.

Copy the executables and their `maps/` and `music/` folders to the Amiga.
The clients ensure a 64 KB stack when launched from the Shell or Workbench.
Settings, profiles and saved servers are kept beside the online executable in
`OpenFirePower.cfg`.

### Staging and packaging

`tools/amiga/stage.sh DESTINATION [CPU]` builds the clients and diagnostics and
stages their assets and replay tests. Transfer the destination folder to the
Amiga over the network or on removable storage. Assign it as `OFP:` to use the
included `Test-OFP68k` Shell script.

To make the Workbench installer and LHA archive, install `lha` and Python 3 with
Pillow, then run:

```sh
sh tools/amiga/package_lha.sh 0.1 build-amiga-ecs
```

Supply `--uploader EMAIL` before the version when preparing an Aminet upload.
The installer checks the CPU and installs the 16-colour client on all supported
chipsets.

## Native controls and menus

| | Steer and drive | Fire / Select | Mine / Back |
|---|---|---|---|
| Player 1 | Arrow keys | Amiga | Alt |
| Player 2 | W A S D | Q | E |

Return and Escape also select and go back. Change bindings in
Settings → Controls → Change Keymap. The two keyboard layouts and joystick
assignments are separate; choosing the other player's port swaps assignments.
Reset to Default restores both players.

The first joystick uses Amiga port 1; the second uses port 0, the mouse port.
With `lowlevel.library`, both work alongside the keyboard layouts. F2 toggles
Player 1's joystick-only mode during a standalone match. On a CD32 pad, red
fires, blue or green lays a mine, Play pauses or goes back, yellow switches the
radar mode, and the shoulder buttons move horizontally in menus.

The server browser has Public and Private tabs. Tab switches between them.
In the lobby, select Chat before typing, or use Keyboard to open the on-screen
keyboard. Team-name buttons open rename dialogs for the host, an authenticated
administrator or that team's human leader. Leaving a network match returns to
its lobby. A player with tanks remaining can rejoin; an eliminated player can
wait or spectate until the next match.

Local team matches use two tanks. The original Jackal, Bison and Mammoth are
available in Classic. Enhanced worlds add the Rhino and world-specific vehicle
profiles. Classic rejects tactical team orders, weather and newer power-ups.

## Renderer and audio

The native renderer draws planar objects with the blitter. The final display
copy is separate; the default CPU copy suits the current 68040/68060 release.
Terrain, objects, HUD, display wait and copy time can be measured independently.
The nearby radar updates changed HUD rows and omits decorative vegetation.
Tree canopies draw over tanks and shots, hiding only the pixels beneath them.

Settings → Graphics provides independent options for blood marks, explosion
visuals, ground fires, smoke, particles and weather. The optional Auto Effects
setting reduces decoration during sustained slow frames. It does not change
physics, projectiles, damage or simulation timing.

The native artwork lives in generated headers. Regenerate it with:

```sh
python3 tools/amiga/gen_classic_art.py
python3 tools/amiga/gen_world_art.py
python3 tools/amiga/gen_faces.py
python3 tools/gen_surface_art.py
```

These tools need Pillow and NumPy. The checked-in headers are sufficient to
compile the release. `tools/amiga/preview/` can render art and menu frames on a
desktop through lightweight AmigaOS API stand-ins; it is not a performance test.

Amiga music streams signed 8-bit mono at 11,025 Hz to Paula through
`audio.device`, using a 24 KiB Chip RAM buffer. The remaining channels handle
sound effects and the tank engine. A ProTracker score provides a fallback when
a loop is missing. Music Volume and Effect Volume are independent meters in
Settings → Audio. Full soundtrack files, adaptations and licences are listed in
[CREDITS.md](../CREDITS.md).

The native code is split across `local.c` and `local/`: `world.c` draws the map,
`hud.c` draws panels and radar, `system.c` manages the screen and controls, and
`duel.c` runs the standalone client. The online client adds `menus.c`, `replays.c`,
`editor.c`, `title.c` and `online.c`. Their shared declarations are in
`local/internal.h`. Generated art and sound data are compiled once.

The standalone game and Shell tool use a 128 × 128 map grid. The full online
client and editor use a 192 × 192 grid, including Huge maps. Route building in
local matches is spread across ticks; network bot routing runs on the server.
Local replays record the resolved bot inputs.

## Native tests

Copy `ofp68k` and the replay fixtures to the Amiga and run:

```
ofp68k verify #?.ofprep
ofp68k bench 30
ofp68k net SERVER-ADDRESS 29870
```

Every recorded checksum should match. Simulation needs 50 ticks per second;
rendering speed is measured separately in the full client's benchmark:

```
OpenFirePowerOnline --benchmark-pacing
OpenFirePowerOnline --benchmark-detailed
OpenFirePowerOnline --stability-test PRIVATE-SERVER PORT 20
OpenFirePowerOnline --editor-recovery-test
```

Use a private test server for reconnect tests. Compare results using the same
CPU, memory, display mode, scene and sound settings. Check memory after warmup
and across repeated reconnects. See [docs/TESTING.md](TESTING.md) for the shared
core, controller, recovery and network checks.

Physical 68040/68060 and Apple TV validation, Windows installer/controller checks
and Linux x86_64 desktop checks remain on the release checklist. The tvOS device
app is unsigned and uses cache storage that the OS may erase; persistent cloud
saves and distribution setup are pending.
