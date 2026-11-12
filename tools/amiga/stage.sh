#!/bin/sh
# SPDX-License-Identifier: MIT
# Build ofp68k and stage the native clients, assets and tests for transfer to an Amiga.
# Copy the folder over the network or on removable storage and assign it as OFP:.
# Then, on the Amiga: execute OFP:Test-OFP68k
#
#   tools/amiga/stage.sh [FOLDER] [CPU]     FOLDER: ~/Amiga/OFP (the default); CPU: 68040 (or 68060 for a CPU-specific build)
#
# The compiler: vbcc (vc) if it's there, else Bebbo's m68k-amigaos-gcc. vbcc needs the NDK's C headers
# (NDK=/path/to/NDK3.2/Include_H, or found in the usual places); no Roadshow SDK: bsdsocket_vbcc.h.
set -e
cd "$(dirname "$0")/../.."
DEST=${1:-$HOME/Amiga/OFP}
CPU=${2:-68040}
CPU=${CPU#-m}
if [ -z "$COMPILER" ]; then
    if command -v vc >/dev/null 2>&1; then COMPILER=vbcc
    elif command -v m68k-amigaos-gcc >/dev/null 2>&1; then COMPILER=gcc
    else
        echo "no Amiga compiler found: vbcc (vc, with the NDK and the Roadshow SDK) or Bebbo's m68k-amigaos-gcc" >&2
        echo "(see docs/PORTING.md: Building for AmigaOS)" >&2
        exit 1
    fi
fi
if [ "$COMPILER" = vbcc ]; then
    # vbcc's vc finds its target settings (config/aos68k) through $VBCC: without it, "No config file!"
    # (Homebrew's tditlu/amiga/vbcc: VBCC=$(brew --prefix vbcc))
    if [ -z "$VBCC" ] || [ ! -d "$VBCC/config" ]; then
        vc=$(command -v vc)
        real=$(readlink "$vc" 2>/dev/null || true)
        case "$real" in /*) ;; "") real=$vc ;; *) real=$(dirname "$vc")/$real ;; esac
        bp=$(command -v brew >/dev/null 2>&1 && brew --prefix vbcc 2>/dev/null || true)
        for d in "$VBCC" "$bp" "$(dirname "$real")/.." "$(dirname "$vc")/.." /opt/vbcc /usr/local/vbcc "$HOME/vbcc"; do
            if [ -n "$d" ] && [ -d "$d/config" ]; then VBCC=$(cd "$d" && pwd); break; fi
        done
    fi
    if [ -z "$VBCC" ] || [ ! -d "$VBCC/config" ]; then
        echo "vbcc: can't find its installation (the folder with config/ and targets/): set VBCC, e.g." >&2
        echo "  VBCC=\$(brew --prefix vbcc) tools/amiga/stage.sh ~/Amiga/OFP" >&2
        exit 1
    fi
    export VBCC
    VCONFIG=${VCONFIG:-aos68k}
    if [ ! -f "$VBCC/config/$VCONFIG" ]; then
        echo "vbcc: no target config $VBCC/config/$VCONFIG; these are there: $(ls "$VBCC/config" | tr '\n' ' ')" >&2
        echo "  pick one: VCONFIG=name tools/amiga/stage.sh ~/Amiga/OFP" >&2
        exit 1
    fi
    # the NDK's C headers (exec/types.h ...): NDK=, or found where they usually are
    if [ -z "$NDK" ]; then
        for d in "$VBCC"/NDK3.2*/Include_H "$VBCC"/NDK_3.9/Include/include_h "$HOME"/Amiga/NDK3.2*/Include_H "$HOME"/Amiga/NDK*/Include_H \
                 "$HOME"/Amiga/SDK/NDK3.2*/Include_H "$HOME"/NDK3.2*/Include_H "$HOME"/Amiga/NDK_3.9/Include/include_h /opt/amiga/NDK3.2*/Include_H; do
            if [ -f "$d/exec/types.h" ]; then NDK=$d; break; fi
        done
    fi
    if [ -z "$NDK" ]; then                  # (not in the usual places: look for it, a few levels deep)
        echo "looking for the NDK (its exec/types.h) ..."
        for root in "$HOME" /opt /usr/local; do
            [ -d "$root" ] || continue
            # (a real NDK: its exec/types.h with clib/exec_protos.h and devices/timer.h beside it; not a test's
            #  fake one, not vbcc's own target headers; NDK 3.2 first)
            hit=$(find "$root" -maxdepth 7 \( -name Library -o -name .Trash -o -name node_modules -o -name .git \) -prune \
                       -o -path "*/exec/types.h" -print 2>/dev/null | grep -viE "fake|stub|mock|/targets/m68k-" |
                  while read -r f; do
                      d=$(dirname "$(dirname "$f")")
                      [ -f "$d/clib/exec_protos.h" ] && [ -f "$d/devices/timer.h" ] && echo "$d"
                  done | awk '{ print (index($0, "NDK3.2") ? 0 : 1) "\t" $0 }' | sort -n | cut -f2 | head -1)
            if [ -n "$hit" ]; then NDK=$hit; break; fi
        done
    fi
    if [ -n "$NDK" ] && [ ! -f "$NDK/exec/types.h" ]; then
        echo "NDK=$NDK: no exec/types.h there: it should be the NDK's C include folder (NDK3.2/Include_H)" >&2
        echo "  leave NDK= out and this script looks for it" >&2
        exit 1
    fi
    if [ -z "$NDK" ]; then
        echo "no NDK found on this Mac. Get NDK 3.2 from Aminet (dev/misc/NDK3.2R4.lha) and unpack it:" >&2
        echo "  brew install lhasa; mkdir -p ~/Amiga && cd ~/Amiga && lha x ~/Downloads/NDK3.2R4.lha" >&2
        echo "then run this again: it finds ~/Amiga/NDK3.2R4/Include_H by itself" >&2
        exit 1
    fi
    missing=""                               # (vbcc hands its output to vasm, then vlink: separate tools)
    command -v vasmm68k_mot >/dev/null 2>&1 || missing="$missing vasm"
    command -v vlink >/dev/null 2>&1 || missing="$missing vlink"
    if [ -n "$missing" ]; then
        echo "vbcc needs its assembler and linker too:$missing. With Homebrew:" >&2
        echo "  brew install$(for m in $missing; do printf ' tditlu/amiga/%s' "$m"; done)" >&2
        exit 1
    fi
    echo "vbcc: $VBCC, target $VCONFIG; NDK: $NDK"
fi
if [ -n "$NETINC" ] && [ ! -f "$NETINC/proto/socket.h" ]; then
    echo "NETINC=$NETINC: no proto/socket.h there: it should be the Roadshow SDK's netinclude folder" >&2
    exit 1
fi
export COMPILER
OUT=${OUT:-build-amiga/$COMPILER-$CPU}
MK="make -s -f src/platform/amiga/Makefile CPU=$CPU OUT=$OUT ${NETINC:+NETINC=$NETINC} ${VCONFIG:+VCONFIG=$VCONFIG} ${NDK:+NDK=$NDK} ${AMISSLINC:+AMISSLINC=$AMISSLINC} ${AGA:+AGA=$AGA}"
built=""
failed=""
for target in all online local; do                       # shell tool, online client, local client
    if $MK "$target"; then built="$built $target"; else failed="$failed $target"; fi
done
mkdir -p "$DEST/replays" "$DEST/logs"
for f in ofp68k OpenFirePowerOnline OpenFirePower; do [ -f "$OUT/$f" ] && cp "$OUT/$f" "$DEST/$f"; done
cp tests/replays/*.ofprep "$DEST/replays/"              # (the games' REPLAYS page lists PROGDIR:replays/: these to watch)
[ "$CPU" = 68000 ] || tools/amiga/stage_maps.sh "$DEST"
sh tools/amiga/stage_music.sh "$DEST"
cp tools/amiga/Test-OFP68k "$DEST/Test-OFP68k"
echo "staged in $DEST ($COMPILER, $CPU): $(for f in ofp68k OpenFirePowerOnline OpenFirePower; do [ -f "$DEST/$f" ] && printf '%s ' "$f"; done)"
echo "  and $(ls tests/replays/*.ofprep | wc -l | tr -d ' ') replays (REPLAYS lists them), Test-OFP68k"
[ -n "$failed" ] && echo "NOT BUILT:$failed (its errors above)" >&2
echo "on the Amiga:  OFP:OpenFirePowerOnline   OFP:OpenFirePower   execute OFP:Test-OFP68k"
[ -z "$failed" ]
