#!/bin/sh
# SPDX-License-Identifier: MIT
# Native Linux packages. Default: separate client and headless-server tar.gz files.
# --server-only needs no SDL development package. --deb adds an optional local DEB.
set -eu
cd "$(dirname "$0")/.."
PYTHON=${PYTHON:-python3}
CLIENT=1
DEB=0
case "${1:-}" in
  '') ;;
  --server-only) CLIENT=0 ;;
  --deb) DEB=1 ;;
  *) echo "Usage: $0 [--server-only|--deb]" >&2; exit 2 ;;
esac
[ "$(uname -s)" = Linux ] || { echo 'Build Linux packages on Linux.' >&2; exit 2; }
normalize_arch() {
  case "$1" in
    aarch64|arm64) echo aarch64 ;;
    x86_64|amd64) echo x86_64 ;;
    *) echo 'Supported Linux architectures: x86_64 and aarch64' >&2; return 2 ;;
  esac
}
HOST_ARCH=$(normalize_arch "$(uname -m)")
ARCH=$(normalize_arch "${ARCH:-$HOST_ARCH}")
[ "$ARCH" = "$HOST_ARCH" ] || { echo 'ARCH must match the native Linux build host.' >&2; exit 2; }
ROLE=linux
[ "$CLIENT" -eq 1 ] || ROLE=linux-server
WORK=${WORK:-work/package/$ROLE-$ARCH}
BUILD_DIR=${OFP_BUILD_DIR:-"$HOME/Development/builds/OpenFirePower/Linux/$ARCH"}
PACKAGE_DIR=${OFP_PACKAGE_DIR:-"$HOME/Development/packages/OpenFirePower/Linux/$ARCH"}
mkdir -p "$WORK" "$BUILD_DIR" "$PACKAGE_DIR"
WORK=$(cd "$WORK" && pwd)
BUILD_DIR=$(cd "$BUILD_DIR" && pwd)
PACKAGE_DIR=$(cd "$PACKAGE_DIR" && pwd)
VER=$(sed -n 's/^#define OFP_VERSION "\(.*\)"/\1/p' src/common/net.h)
SDL=OFF
[ "$CLIENT" -eq 0 ] || SDL=ON
cmake -S . -B "$WORK/build" -DCMAKE_BUILD_TYPE=Release -DOFP_DATA_DIR=data \
  -DOFP_BUILD_SDL2="$SDL" -DOFP_BUILD_SERVER=ON -DOFP_BUILD_TESTS=ON ${CMAKE_ARGS:-}
cmake --build "$WORK/build" --parallel "${JOBS:-4}"
SERVER="$BUILD_DIR/OpenFirePowerServer"
rm -rf "$SERVER"
mkdir -p "$SERVER/data/maps" "$SERVER/docs"
cp "$WORK/build/ofp_server" "$WORK/build/ofp_match" "$WORK/build/ofp_bot" "$SERVER/"
cp -R data/maps/. "$SERVER/data/maps/"
cp server.cfg.example LICENSE CREDITS.md THIRD-PARTY-NOTICES.md "$SERVER/"
cp docs/BUILDING.md docs/NETWORK.md "$SERVER/docs/"
cat > "$SERVER/README.txt" <<'TXT'
OpenFirePower dedicated server for Linux
Copy server.cfg.example to server.cfg, review it, then run:
  ./ofp_server -c server.cfg --check
  ./ofp_server -c server.cfg
Run from this directory so relative map and log paths work. The example enables
public listing and match/replay sharing; disable these for a private server.
Requires libcurl, SQLite and the C runtime; no SDL or desktop session is needed.
See docs/BUILDING.md and docs/NETWORK.md for dependencies and service setup.
TXT
SERVER_TAR="$PACKAGE_DIR/$("$PYTHON" scripts/release_assets.py name server linux "$ARCH" "$VER" tar.gz)"
tar -czf "$SERVER_TAR" -C "$BUILD_DIR" OpenFirePowerServer
"$PYTHON" scripts/release_assets.py checksum "$SERVER_TAR"
if [ "$CLIENT" -eq 1 ]; then
  OUT="$BUILD_DIR/OpenFirePower"
  rm -rf "$OUT"
  mkdir -p "$OUT/data"
  cp "$WORK/build/openfirepower" "$OUT/"
  for d in styles themes sounds music maps; do cp -R "data/$d" "$OUT/data/"; done
  cp data/gamecontrollerdb.txt data/avatars.png "$OUT/data/"
  cp LICENSE CREDITS.md THIRD-PARTY-NOTICES.md packaging/linux/README.txt "$OUT/"
  cat > "$OUT/Play" <<'TXT'
#!/bin/sh
cd "$(dirname "$0")"
exec ./openfirepower "$@"
TXT
  chmod +x "$OUT/Play"
  CLIENT_TAR="$PACKAGE_DIR/$("$PYTHON" scripts/release_assets.py name client linux "$ARCH" "$VER" tar.gz)"
  tar -czf "$CLIENT_TAR" -C "$BUILD_DIR" OpenFirePower
  "$PYTHON" scripts/release_assets.py checksum "$CLIENT_TAR"
fi
if [ "$DEB" -eq 1 ]; then
  command -v dpkg-deb >/dev/null 2>&1 || { echo '--deb needs dpkg-deb.' >&2; exit 2; }
  case "$ARCH" in aarch64) DEB_ARCH=arm64 ;; *) DEB_ARCH=amd64 ;; esac
  PKG="$WORK/deb"
  rm -rf "$PKG"
  mkdir -p "$PKG/DEBIAN" "$PKG/usr/games" "$PKG/usr/share/openfirepower" "$PKG/usr/share/applications" "$PKG/usr/share/icons/hicolor/256x256/apps" "$PKG/usr/share/doc/openfirepower"
  cp "$OUT/openfirepower" "$PKG/usr/games/"
  cp -R "$OUT/data/"* "$PKG/usr/share/openfirepower/"
  cp LICENSE CREDITS.md THIRD-PARTY-NOTICES.md packaging/linux/README.txt "$PKG/usr/share/doc/openfirepower/"
  cp packaging/linux/org.openfirepower.OpenFirePower.desktop "$PKG/usr/share/applications/"
  cp packaging/linux/openfirepower.png "$PKG/usr/share/icons/hicolor/256x256/apps/org.openfirepower.OpenFirePower.png"
  cat > "$PKG/DEBIAN/control" <<EOF
Package: openfirepower
Version: $VER
Architecture: $DEB_ARCH
Maintainer: OpenFirePower contributors
Section: games
Priority: optional
Depends: libc6 (>= 2.39), libsdl2-2.0-0 (>= 2.0.18), libcurl4t64
Description: OpenFirePower tank combat
 Classic-inspired tank combat, enhanced worlds, local and network play,
 map editor, replays and training.
EOF
  LOCAL_DEB="$PACKAGE_DIR/openfp-client-linux-$ARCH-$VER.deb"
  dpkg-deb --build --root-owner-group "$PKG" "$LOCAL_DEB"
  "$PYTHON" scripts/release_assets.py checksum "$LOCAL_DEB"
fi
echo "Ready Linux $ARCH files: $BUILD_DIR"
echo "Packages and checksums: $PACKAGE_DIR"
