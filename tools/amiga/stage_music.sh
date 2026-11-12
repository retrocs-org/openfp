#!/bin/sh
# SPDX-License-Identifier: MIT
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$1/music"
cp "$ROOT"/data/music/amiga/*.pcm "$1/music/"
cp "$ROOT/CREDITS.md" "$1/Credits"
