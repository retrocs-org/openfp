#!/bin/sh
# SPDX-License-Identifier: MIT
# The Amiga release as one .lha for Aminet: an OpenFirePower drawer with the Installer script, icons,
# a ReadMe, the maps and the 16-colour 68040 builds. Needs lha and Python 3 with Pillow.
#
#   tools/amiga/package_lha.sh [--uploader EMAIL] VERSION [ECS-dir]
#
# ECS-dir defaults to build-amiga-ecs and holds OpenFirePower and OpenFirePowerOnline. Writes
# openfp-client-amigaos-m68k-VERSION.lha and its Aminet readme and checksum.
set -e
PYTHON=${PYTHON:-python3}
LHA=${LHA:-lha}
if [ "${1:-}" = --uploader ]; then
    UPLOADER=${2:?provide the uploader email after --uploader}
    shift 2
fi
VERSION=${1:?version, e.g. 0.1}
if [ "$#" -gt 2 ]; then
    echo "Usage: $0 [--uploader EMAIL] VERSION [ECS-dir]" >&2
    exit 2
fi
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../.." && pwd)
DECS=${2:-build-amiga-ecs}
STAGE=$(mktemp -d); TOP=$STAGE/OpenFirePower
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$TOP/bin/ECS" "$STAGE/icons"
"$PYTHON" "$HERE/make_icons.py" "$STAGE/icons" >/dev/null
for game in OpenFirePower OpenFirePowerOnline; do
    binary="$DECS/$game"
    if [ ! -f "$binary" ] ||
       ! LC_ALL=C grep -aFq 'OpenFP requires a 68040 or 68060 CPU.' "$binary" ||
       LC_ALL=C grep -aFq 'This OpenFP is for AGA Amigas' "$binary"; then
        echo "Expected a 16-colour 68040 release build at $binary" >&2
        exit 1
    fi
    cp "$binary" "$STAGE/icons/$game.info" "$TOP/bin/ECS/"
done
sh "$HERE/stage_maps.sh" "$TOP"
sh "$HERE/stage_music.sh" "$TOP"
cp "$ROOT/LICENSE" "$TOP/LICENSE"
cp "$ROOT/THIRD-PARTY-NOTICES.md" "$TOP/ThirdPartyNotices"
cp "$ROOT/packaging/amiga/Install" "$TOP/Install"; cp "$STAGE/icons/Install.info" "$TOP/Install.info"
cp "$STAGE/icons/ReadMe.info" "$TOP/ReadMe.info"; cp "$STAGE/icons/OpenFirePower.drawer.info" "$STAGE/OpenFirePower.info"
cat > "$TOP/ReadMe" <<TXT
OpenFP $VERSION for the Amiga
====================================

Classic and enhanced tank battles: flags, prisoners
to rescue, for two players at one Amiga or on the network.

Requires AmigaOS 3.0 or newer and a 68040 or 68060 CPU.
Double-click Install to install the 16-colour game on OCS, ECS or AGA.
For a manual install, copy the contents of bin/ECS alongside maps/ and music/.

Playing: Player 1 uses the arrow keys, Amiga to fire and Alt to lay mines.
Player 2 uses W A S D, Q to fire and E to lay mines. Return confirms menus.
Player 1's Fire and Mine bindings also select and go back in the menus.
Player 1 uses joystick port 1. Player 2 uses port 0 (the mouse port).
F2 toggles Player 1's joystick-only mode during play. In OpenFirePowerOnline,
Settings > Controls lets each player choose input and remap their keys.
Settings > Graphics lets you simplify or hide explosions and switch off
ground fires, weather effects and blood marks individually.
Auto Effects can reduce decorative detail when frames stay slow.
Tools > Input Test shows both players' keyboard and joystick inputs.
The editor offers recovery from three rotating drafts after an interrupted session.
Enhanced worlds add icy Winter lakes, oil spills and timed speed/rapid-fire boosts.
Moon Base has dry crater trenches. Power-ups can be switched off in Game Options.
Classic keeps its original handling with no power-ups or slippery surfaces.
Settings > Game Options selects a nearby map or tank-dot radar, tanks per
player (including unlimited) and a match length of 1 to 30 minutes.
Volcano and Moon Base have their own tank designs, handling and pickups.
Classic retains its original tank rules.
Only enhanced team games offer Attack, Defend, Escort and Regroup orders to
the human team leader. Classic never offers or accepts these commands.
At sign-in, select a name to sign in, or choose Edit or Delete to its right.
Use the two player cards in the main menu to sign in or out.
OpenFirePowerOnline also has the map editor, replays and network games (it needs a TCP/IP
stack: bsdsocket.library). A CD32 pad: PLAY pauses, green lays a mine, yellow
switches the radar.
The Public server list also needs AmiSSL 4.10 or later in the 4.x series.
Install the library for your CPU. Private servers can be joined by address.
Select Chat in the lobby to type, or Keyboard for the on-screen keyboard.
Tab and the CD32 shoulder buttons move focus. Use Player 1's directions and
Fire on the on-screen keyboard for chat and team renaming. T opens chat in
the lobby or match. Use Shift+T if T is bound to Player 1. Enter sends the
message and Escape closes chat.

The game ensures a 64 KB stack from both the Shell and Workbench.

The soundtrack credits and licences are in Credits and Main Menu > About.
About has scrolling credits with Legion's Rally by Li-Hsin Huang.
The game code is MIT. The skull laugh is Evil Laugh by AntumDeluge (CC0).
https://opengameart.org/content/evil-laugh
Source, desktop versions and servers: https://openfp.retrocs.org/
TXT
OUTPUT_DIR=${OFP_PACKAGE_DIR:-"$HOME/Development/packages/OpenFirePower/AmigaOS"}
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR=$(cd "$OUTPUT_DIR" && pwd)
BASE=$("$PYTHON" "$ROOT/scripts/release_assets.py" name client amigaos m68k "$VERSION" lha)
OUT="$OUTPUT_DIR/$BASE"; rm -f "$OUT"
( cd "$STAGE" && rm -rf icons && "$LHA" aq "$OUT" OpenFirePower.info OpenFirePower )
SIZE=$(du -k "$OUT" | cut -f1)
{
    printf '%s\n' 'Short:        OpenFP classic and enhanced tank combat' 'Author:       OpenFP contributors'
    if [ -n "${UPLOADER:-}" ]; then printf 'Uploader:     %s\n' "$UPLOADER"; fi
    cat <<TXT
Type:         game/actio
Version:      $VERSION
Architecture: m68k-amigaos >= 3.0.0
Requires:     68040 or 68060, bsdsocket.library for network play
Distribution: Aminet
Kurz:         Freier Nachbau von Fire Power (1987), Panzer

OpenFP is an open-source tank combat game (MIT code):
Capture the enemy's flag, rescue prisoners, two players at one Amiga or online
against players on Linux, macOS and Windows.

 - 16 colours on OCS, ECS and AGA
 - eight worlds, game modes from capture the flag to warlord, a map editor,
   replays, music, CD32 pad support
 - network play through any bsdsocket.library TCP/IP stack

Source and more: https://openfp.retrocs.org/  ($SIZE KB)
TXT
} > "$OUTPUT_DIR/${BASE%.lha}.readme"
"$PYTHON" "$ROOT/scripts/release_assets.py" checksum "$OUT"
echo "$OUT"
