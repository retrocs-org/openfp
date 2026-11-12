#!/bin/sh
# SPDX-License-Identifier: MIT
# Copy the desktop's bundled maps into an Amiga program directory.
set -e
MAPS=$(cd "$(dirname "$0")/../../data/maps" && pwd)
DEST=$1/maps/catalog
mkdir -p "$DEST"
for map in "$MAPS/"*.ofpmap; do
    if ! grep -Eq '^(size=huge|biome=scifi)([[:space:]]|$)' "$map"; then
        cp "$map" "$DEST/${map#"$MAPS/"}"
    fi
done
