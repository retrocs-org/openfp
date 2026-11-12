#!/bin/sh
# SPDX-License-Identifier: MIT
# Compile both 16-colour clients, stage runnable files, and package the installer.
set -eu
cd "$(dirname "$0")/.."
PYTHON=${PYTHON:-python3}
WORK=${WORK:-work/package/amiga}
BUILD_DIR=${OFP_BUILD_DIR:-"$HOME/Development/builds/OpenFirePower/AmigaOS/ECS"}
PACKAGE_DIR=${OFP_PACKAGE_DIR:-"$HOME/Development/packages/OpenFirePower/AmigaOS"}
mkdir -p "$WORK" "$BUILD_DIR" "$PACKAGE_DIR"
WORK=$(cd "$WORK" && pwd)
BUILD_DIR=$(cd "$BUILD_DIR" && pwd)
PACKAGE_DIR=$(cd "$PACKAGE_DIR" && pwd)
VER=$(sed -n 's/^#define OFP_VERSION "\(.*\)"/\1/p' src/common/net.h)
make -f src/platform/amiga/Makefile COMPILER=gcc CPU=68040 "OUT=$WORK/ECS" -j"${JOBS:-4}" all local online
sh tools/amiga/stage_runtime.sh "$WORK/ECS" "$BUILD_DIR"
OFP_PACKAGE_DIR="$PACKAGE_DIR" PYTHON="$PYTHON" sh tools/amiga/package_lha.sh "$VER" "$WORK/ECS"
echo "Ready 16-colour clients: $BUILD_DIR"
echo "Installer and checksum: $PACKAGE_DIR"
