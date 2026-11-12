#!/bin/sh
# SPDX-License-Identifier: MIT
# The game's core as WebAssembly, for the web site's replay viewer
# (public-website/assets/ofp.wasm). Needs clang with the wasm32 target, wasm-ld and
# wasi-libc: on Debian/Ubuntu  apt install clang lld wasi-libc ; on openSUSE
# zypper install clang lld wasi-libc ; on macOS  brew install llvm wasi-libc.
set -e
cd "$(dirname "$0")/.."
SYSROOT=${WASI_SYSROOT:-/usr}
CC=${CC:-clang}
OUT=public-website/assets/ofp.wasm
$CC --target=wasm32-wasi --sysroot="$SYSROOT" -isystem "$SYSROOT/include/wasm32-wasi" -L"$SYSROOT/lib/wasm32-wasi" \
    -O2 -std=c99 -Isrc -DOFP_MAX_TANKS=10 -mexec-model=reactor \
    src/core/fixed.c src/core/world.c src/core/worldgen.c src/core/game.c src/core/ai.c \
    src/core/mapfile.c src/core/match.c src/common/rules.c src/web/ofp_web.c \
    -Wl,--strip-all -o "$OUT"
echo "built $OUT ($(wc -c < "$OUT") bytes)"
