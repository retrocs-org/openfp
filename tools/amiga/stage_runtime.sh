#!/bin/sh
# SPDX-License-Identifier: MIT
# Stage runnable files separately from compiler output.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
INPUT=${1:?compiled ECS directory}
OUTPUT=${2:?ready-to-test directory}
mkdir -p "$OUTPUT"
cp "$INPUT/OpenFirePower" "$INPUT/OpenFirePowerOnline" "$INPUT/ofp68k" "$OUTPUT/"
cp "$ROOT/LICENSE" "$ROOT/THIRD-PARTY-NOTICES.md" "$OUTPUT/"
sh "$ROOT/tools/amiga/stage_maps.sh" "$OUTPUT"
sh "$ROOT/tools/amiga/stage_music.sh" "$OUTPUT"
