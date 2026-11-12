#!/bin/sh
# The online client (menus, worlds, the editor) on the desktop: ./build_online.sh [out]
set -e
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../.." && pwd); OUT=${1:-$HERE/amiga-preview-online}
cc -std=gnu99 -O1 -g -w -DOFP_ART_PREVIEW -DOFP_MAP_MAX=128 -DOFP_AMIGA_ONLINE -I"$HERE/include" -I"$ROOT/src" ${PREVIEW_CFLAGS} -o "$OUT" \
   "$ROOT"/src/core/fixed.c "$ROOT"/src/core/world.c "$ROOT"/src/core/worldgen.c "$ROOT"/src/core/game.c \
   "$ROOT"/src/core/ai.c "$ROOT"/src/core/mapfile.c "$ROOT"/src/core/match.c "$ROOT"/src/common/fx_budget.c "$ROOT"/src/common/recovery.c "$ROOT"/src/common/mapcheck.c "$ROOT"/src/common/operation.c "$ROOT"/src/common/net.c \
   "$ROOT"/src/common/client.c "$ROOT"/src/common/http.c "$ROOT"/src/common/rules.c "$ROOT"/src/common/sha256.c "$ROOT"/src/common/training.c \
   "$ROOT"/src/common/replay.c "$ROOT"/src/platform/amiga/menu_settings.c \
   "$ROOT"/src/platform/amiga/local.c "$ROOT"/src/platform/amiga/local/art.c "$ROOT"/src/platform/amiga/local/world.c "$ROOT"/src/platform/amiga/local/hud.c "$ROOT"/src/platform/amiga/local/system.c "$ROOT"/src/platform/amiga/local/menus.c "$ROOT"/src/platform/amiga/local/replays.c "$ROOT"/src/platform/amiga/local/editor.c "$ROOT"/src/platform/amiga/local/title.c "$ROOT"/src/platform/amiga/local/online.c "$HERE/shim.c"
echo "$OUT"
