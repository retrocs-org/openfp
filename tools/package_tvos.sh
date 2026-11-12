#!/bin/sh
# SPDX-License-Identifier: MIT
# Build an Apple TV simulator app or an unsigned device app. Xcode signs device releases.
set -eu
cd "$(dirname "$0")/.."
MODE=${1:-simulator}
case "$MODE" in
  simulator) SDK=appletvsimulator ;;
  device) SDK=appletvos ;;
  *) echo "Usage: $0 [simulator|device]" >&2; exit 2 ;;
esac
TOOLS=${TVOS_TOOLS:-"$HOME/Development/tools/tvos"}
WORK=${WORK:-build-tvos/$MODE}
SDL_VER=2.32.10
SDL_SHA=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
SDL="$TOOLS/SDL2-$SDL_VER"
PREFIX="$TOOLS/sdk/$MODE"
mkdir -p "$TOOLS" "$WORK"
WORK=$(cd "$WORK" && pwd)
if [ ! -f "$SDL/CMakeLists.txt" ]; then
  curl -fLsS "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-$SDL_VER.tar.gz" -o "$TOOLS/SDL2-$SDL_VER.tar.gz"
  printf '%s  %s\n' "$SDL_SHA" "$TOOLS/SDL2-$SDL_VER.tar.gz" | shasum -a 256 -c -
  tar -xzf "$TOOLS/SDL2-$SDL_VER.tar.gz" -C "$TOOLS"
fi
if [ ! -f "$PREFIX/lib/cmake/SDL2/SDL2Config.cmake" ]; then
  cmake -S "$SDL" -B "$WORK/sdl" -DCMAKE_SYSTEM_NAME=tvOS -DCMAKE_OSX_SYSROOT="$SDK" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DCMAKE_BUILD_TYPE=Release \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DSDL_TESTS=OFF -DCMAKE_INSTALL_PREFIX="$PREFIX"
  cmake --build "$WORK/sdl" --parallel 8
  cmake --install "$WORK/sdl"
fi
cmake -S . -B "$WORK/build" -DCMAKE_SYSTEM_NAME=tvOS -DCMAKE_OSX_SYSROOT="$SDK" \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DCMAKE_BUILD_TYPE=Release \
  -DSDL2_DIR="$PREFIX/lib/cmake/SDL2" -DOFP_DATA_DIR=data
cmake --build "$WORK/build" --parallel 8
ditto "$WORK/build/OpenFirePower.app" "$WORK/OpenFirePower.app"
cp LICENSE CREDITS.md "$WORK/"
cp packaging/tvos/README.txt "$WORK/README.txt"
if [ "$MODE" = simulator ]; then
  codesign --force --sign - "$WORK/OpenFirePower.app"
  ditto -c -k --keepParent "$WORK/OpenFirePower.app" "$WORK/OpenFirePower-tvOS-Simulator.zip"
else
  cmake -S . -B "$WORK/xcode" -G Xcode -DCMAKE_SYSTEM_NAME=tvOS -DCMAKE_OSX_SYSROOT=appletvos \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DSDL2_DIR="$PREFIX/lib/cmake/SDL2" -DOFP_DATA_DIR=data
  echo "Unsigned device build. Open $WORK/xcode/openfirepower.xcodeproj, select your signing team, then Archive."
fi
echo "Apple TV $MODE build: $WORK/OpenFirePower.app"
