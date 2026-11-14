#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Refresh an existing systemd installation from the main branch.
set -Eeuo pipefail
umask 077

SOURCE=/root/openfp
BUILD="$SOURCE/build-server"
BINARY=/usr/bin/ofp_server
CONFIG=/etc/ofp-server/server.cfg
STATE=/var/lib/ofp-server
SERVICE=ofp-server.service

[[ $(id -u) == 0 ]] || { echo "Run this script as root." >&2; exit 1; }
for tool in git cmake pkg-config systemctl install flock nproc mktemp; do
    command -v "$tool" >/dev/null || { echo "Missing command: $tool" >&2; exit 1; }
done
pkg-config --exists libcurl sqlite3 || {
    echo "Install libcurl-devel and sqlite3-devel before refreshing." >&2; exit 1;
}
exec 9>/run/lock/ofp-server-refresh.lock
flock -n 9 || { echo "Another server refresh is already running." >&2; exit 1; }
[[ -x "$BINARY" && -f "$CONFIG" && -d "$STATE" ]] || {
    echo "An existing server installation is required." >&2; exit 1;
}
UNIT=$(systemctl show "$SERVICE" -p FragmentPath --value)
case "$UNIT" in
    /etc/systemd/system/ofp-server.service|/usr/lib/systemd/system/ofp-server.service|/lib/systemd/system/ofp-server.service) ;;
    *) echo "Unexpected service location: $UNIT; nothing installed." >&2; exit 1 ;;
esac
[[ -f "$UNIT" && ! -L "$UNIT" && ! -L "$BINARY" ]] || {
    echo "The server binary and service must be regular files." >&2; exit 1;
}
[[ $(git -C "$SOURCE" branch --show-current) == main ]] || {
    echo "$SOURCE must be on the main branch." >&2; exit 1;
}
git -C "$SOURCE" diff --quiet && git -C "$SOURCE" diff --cached --quiet || {
    echo "Commit or stash tracked source changes first." >&2; exit 1;
}

echo "Updating source and building; the current server stays running."
git -C "$SOURCE" pull --ff-only origin main
cmake -S "$SOURCE" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
    -DOFP_BUILD_SDL2=OFF -DOFP_BUILD_TESTS=OFF -DOFP_BUILD_SERVER=ON \
    -DOFP_WITH_CURL=ON -DOFP_SERVER_SQLITE=ON -DOFP_INSTALL_SYSTEMD=OFF
cmake --build "$BUILD" --target ofp_server --parallel "$(nproc)"
"$BUILD/ofp_server" --version

install -d -m 700 /var/backups/ofp-server
BACKUP=$(mktemp -d /var/backups/ofp-server/refresh-XXXXXXXX)
cp -p "$BINARY" "$BACKUP/ofp_server"
cp -p "$UNIT" "$BACKUP/ofp-server.service"
cp -p "$CONFIG" "$BACKUP/server.cfg"
# Relative map paths must be checked from the same directory used by systemd.
if ! (cd "$STATE" && "$BUILD/ofp_server" -c "$CONFIG" --check) >"$BACKUP/config-check.log" 2>&1; then
    echo "Configuration check failed. See $BACKUP/config-check.log; server left running." >&2
    exit 1
fi

STAGED_BINARY=$(mktemp "$(dirname "$BINARY")/.ofp_server.XXXXXXXX")
STAGED_UNIT=$(mktemp "$(dirname "$UNIT")/.ofp-server.service.XXXXXXXX")
INSTALLING=0
finish() {
    result=$?
    trap - EXIT
    if (( result != 0 && INSTALLING == 1 )); then
        echo "Refresh failed; restoring the previous binary and service." >&2
        set +e
        systemctl stop "$SERVICE"
        install -m 755 "$BACKUP/ofp_server" "$STAGED_BINARY" && mv -f "$STAGED_BINARY" "$BINARY"
        install -m 644 "$BACKUP/ofp-server.service" "$STAGED_UNIT" && mv -f "$STAGED_UNIT" "$UNIT"
        systemctl daemon-reload
        systemctl start "$SERVICE" || echo "Previous server could not start; inspect journalctl -u $SERVICE." >&2
    fi
    rm -f "$STAGED_BINARY" "$STAGED_UNIT"
    exit "$result"
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
install -m 755 "$BUILD/ofp_server" "$STAGED_BINARY"
install -m 644 "$SOURCE/packaging/systemd/ofp-server.service" "$STAGED_UNIT"

INSTALLING=1
systemctl stop "$SERVICE"
mv -f "$STAGED_BINARY" "$BINARY"
mv -f "$STAGED_UNIT" "$UNIT"
systemctl daemon-reload
systemctl start "$SERVICE"
sleep 3
systemctl is-active --quiet "$SERVICE"
INSTALLING=0

echo "Server and systemd service refreshed. Configuration and saved data kept."
echo "Backup: $BACKUP"
"$BINARY" --version
systemctl --no-pager --full status "$SERVICE"
