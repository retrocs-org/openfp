#!/bin/sh
# Build the desktop preview of the Amiga frontend's local client (README.md): ./build.sh [out]
set -e
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../.." && pwd); OUT=${1:-$HERE/amiga-preview}
cc -std=gnu99 -O1 -g -w -DOFP_ART_PREVIEW -DOFP_MAP_MAX=128 -I"$HERE/include" -I"$ROOT/src" ${PREVIEW_CFLAGS} -o "$OUT" \
   "$ROOT"/src/core/fixed.c "$ROOT"/src/core/world.c "$ROOT"/src/core/worldgen.c "$ROOT"/src/core/game.c \
   "$ROOT"/src/core/ai.c "$ROOT"/src/core/mapfile.c "$ROOT"/src/core/match.c "$ROOT"/src/common/fx_budget.c "$ROOT"/src/common/recovery.c "$ROOT"/src/common/mapcheck.c "$ROOT"/src/common/operation.c "$ROOT"/src/common/net.c \
   "$ROOT"/src/platform/amiga/local.c "$ROOT"/src/platform/amiga/local/art.c "$ROOT"/src/platform/amiga/local/world.c "$ROOT"/src/platform/amiga/local/hud.c "$ROOT"/src/platform/amiga/local/system.c "$ROOT"/src/platform/amiga/local/duel.c "$HERE/shim.c"
echo "$OUT"
