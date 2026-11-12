#!/bin/sh
# SPDX-License-Identifier: MIT
# Cross-build the Windows x64 game and a portable zip. Pass --msi to also build an installer.
# Requires mingw-w64, CMake, Python 3, and wixl (from msitools) for --msi.
# SDL2_MINGW_DIR can point to an existing x86_64-w64-mingw32 SDK directory.
set -eu
PYTHON=${PYTHON:-python3}
msi=0
case "${1:-}" in
    --msi) msi=1 ;;
    "") ;;
    *) echo "Usage: $0 [--msi]" >&2; exit 2 ;;
esac
cd "$(dirname "$0")/.."
SDL_VER=2.32.10
SDL_SHA=83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2
WORK=${WORK:-work/package/windows}
BUILD_DIR=${OFP_BUILD_DIR:-"$HOME/Development/builds/OpenFirePower/Windows"}
PACKAGE_DIR=${OFP_PACKAGE_DIR:-"$HOME/Development/packages/OpenFirePower/Windows"}
VER=$(sed -n 's/^#define OFP_VERSION "\(.*\)"/\1/p' src/common/net.h)
mkdir -p "$WORK" "$BUILD_DIR" "$PACKAGE_DIR"
WORK=$(cd "$WORK" && pwd)
BUILD_DIR=$(cd "$BUILD_DIR" && pwd)
PACKAGE_DIR=$(cd "$PACKAGE_DIR" && pwd)
SDK_CACHE=${SDL2_CACHE_DIR:-"$HOME/Development/tools/windows/SDL2"}
mkdir -p "$SDK_CACHE"
SDK_CACHE=$(cd "$SDK_CACHE" && pwd)
SDL=${SDL2_MINGW_DIR:-"$SDK_CACHE/SDL2-$SDL_VER/x86_64-w64-mingw32"}
if [ ! -f "$SDL/bin/SDL2.dll" ] && [ -z "${SDL2_MINGW_DIR:-}" ]; then
    curl -fLsS -o "$SDK_CACHE/sdl.tgz" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-devel-$SDL_VER-mingw.tar.gz"
    if command -v sha256sum >/dev/null 2>&1; then
        printf '%s  %s\n' "$SDL_SHA" "$SDK_CACHE/sdl.tgz" | sha256sum -c -
    else
        printf '%s  %s\n' "$SDL_SHA" "$SDK_CACHE/sdl.tgz" | shasum -a 256 -c -
    fi
    tar xzf "$SDK_CACHE/sdl.tgz" -C "$SDK_CACHE"
fi
SDL=$(cd "$SDL" && pwd)
test -f "$SDL/bin/SDL2.dll"
cmake -S . -B "$WORK/build" -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release \
      -DOFP_BUILD_SERVER=OFF -DOFP_BUILD_TESTS=OFF -DOFP_WITH_CURL=OFF -DOFP_DATA_DIR=data \
      -DSDL2_DIR="$SDL/lib/cmake/SDL2"
cmake --build "$WORK/build" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" --target openfirepower
OUT="$BUILD_DIR/OpenFirePower"
rm -rf "$OUT" && mkdir -p "$OUT/data"
cp "$WORK/build/openfirepower.exe" "$SDL/bin/SDL2.dll" "$OUT/"
for d in styles themes sounds maps music; do cp -r "data/$d" "$OUT/data/"; done
cp data/gamecontrollerdb.txt data/avatars.png "$OUT/data/"
cp LICENSE CREDITS.md THIRD-PARTY-NOTICES.md "$OUT/"
cp packaging/windows/README.txt "$OUT/README.txt"
cp tools/windows-qa.ps1 packaging/windows/WINDOWS-TESTS.md "$OUT/"
cp "$SDL/../LICENSE.txt" "$OUT/SDL2-LICENSE.txt"
ZIP="$PACKAGE_DIR/$("$PYTHON" scripts/release_assets.py name client windows x86_64 "$VER" zip)"
( cd "$BUILD_DIR" && "$PYTHON" - "$ZIP" <<'PY'
import os, sys, zipfile
with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED) as z:
    for root, _, files in os.walk("OpenFirePower"):
        for f in files: z.write(os.path.join(root, f))
PY
)
"$PYTHON" scripts/release_assets.py checksum "$ZIP"
echo "built $ZIP"
echo "Ready game: $OUT"
if [ "$msi" -eq 1 ]; then
    MSI="$PACKAGE_DIR/$("$PYTHON" scripts/release_assets.py name client windows x86_64 "$VER" msi)"
    "$PYTHON" tools/package_windows_msi.py "$OUT" "$VER" "$MSI"
fi
