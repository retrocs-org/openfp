#!/bin/sh
# SPDX-License-Identifier: MIT
# The dedicated server as a systemd service, with a user of its own (ofp-server), instead
# of a program run by hand as root. Run it as root, from the source tree:
#
#   tools/install_server_service.sh [--from DIR] [--no-start]
#
#   --from DIR   where the server runs now: its server.cfg, database, log, maps, replays
#                and backups are moved over (default: /root/openfirepower). Nothing there
#                is deleted; whatever it replaces is kept as *.before-service.
#   --no-start   install and move, but don't enable or start it yet
#
# After it:  /usr/bin/ofp_server, /etc/ofp-server/server.cfg (the settings),
#            /var/lib/ofp-server/ (the database, log, replays: the paths in server.cfg are
#            relative to it), and the service:
#   systemctl status ofp-server      journalctl -u ofp-server -f
#   systemctl reload ofp-server      (reads server.cfg again, as kill -HUP did)
set -eu
FROM=/root/openfirepower
START=yes
while [ $# -gt 0 ]; do
    case "$1" in
        --from) FROM=$2; shift 2 ;;
        --no-start) START=no; shift ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "install_server_service.sh: what's $1? (--help)" >&2; exit 2 ;;
    esac
done
[ "$(id -u)" = 0 ] || { echo "run it as root" >&2; exit 2; }
command -v systemctl > /dev/null || { echo "this needs systemd" >&2; exit 2; }
cd "$(dirname "$0")/.."
if pgrep -x ofp_server > /dev/null; then
    echo "ofp_server is running now: stop it first (its console: Ctrl-C; or kill $(pgrep -x ofp_server | tr '\n' ' '))" >&2
    exit 1
fi

# 1. build and install: the server, the tools, the unit, the user's definition
cmake -S . -B build-service -DCMAKE_BUILD_TYPE=Release -DOFP_BUILD_SDL2=OFF -DCMAKE_INSTALL_PREFIX=/usr \
      -DOFP_INSTALL_SYSTEMD=ON > /dev/null
cmake --build build-service -j"$(nproc)"
CFG_BEFORE=""
[ -f /etc/ofp-server/server.cfg ] && CFG_BEFORE=yes && cp -p /etc/ofp-server/server.cfg /etc/ofp-server/server.cfg.before-service
cmake --install build-service > /dev/null
[ -n "$CFG_BEFORE" ] && cp -p /etc/ofp-server/server.cfg.before-service /etc/ofp-server/server.cfg   # (install doesn't win)
systemd-sysusers /usr/lib/sysusers.d/ofp-server.conf
echo "installed: $(/usr/bin/ofp_server --version)"

# 2. what the server has now, moved over (copied: the old place stays as it was)
mkdir -p /var/lib/ofp-server
if [ -f "$FROM/server.cfg" ]; then
    [ -f /etc/ofp-server/server.cfg ] && cp -p /etc/ofp-server/server.cfg /etc/ofp-server/server.cfg.before-service
    cp -p "$FROM/server.cfg" /etc/ofp-server/server.cfg
    echo "settings: $FROM/server.cfg -> /etc/ofp-server/server.cfg"
fi
for f in ofp_server.db ofp_server.db-wal ofp_server.db-shm ofp_server.log; do
    if [ -f "$FROM/$f" ]; then
        [ -f "/var/lib/ofp-server/$f" ] && mv "/var/lib/ofp-server/$f" "/var/lib/ofp-server/$f.before-service"
        cp -p "$FROM/$f" /var/lib/ofp-server/
        echo "kept: $f"
    fi
done
for d in maps replays backups; do
    [ -d "$FROM/$d" ] && cp -rp "$FROM/$d" /var/lib/ofp-server/ && echo "kept: $d/"
done
# (a database or log somewhere else than the server's folder: say so, the service can't reach it)
for k in database log_file backup_dir replays maps; do
    v=$(sed -n "s/^[[:space:]]*${k}[[:space:]]*=[[:space:]]*\([^#]*\).*/\1/p" /etc/ofp-server/server.cfg 2>/dev/null | head -1 | sed 's/[[:space:]]*$//')
    case "$v" in /*) echo "note: $k = $v is outside /var/lib/ofp-server: the service (ProtectSystem=strict) can't write there; make it relative" ;; esac
done
chown -R ofp-server:ofp-server /var/lib/ofp-server
chown root:ofp-server /etc/ofp-server/server.cfg
chmod 640 /etc/ofp-server/server.cfg

# 3. the port, and the service
PORT=$(sed -n 's/^[[:space:]]*port[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' /etc/ofp-server/server.cfg | head -1)
PORT=${PORT:-29870}
if command -v firewall-cmd > /dev/null && firewall-cmd --state > /dev/null 2>&1; then
    firewall-cmd --permanent --add-port="$PORT/udp" > /dev/null && firewall-cmd --reload > /dev/null && echo "firewall: $PORT/udp open"
fi
systemctl daemon-reload
if [ "$START" = yes ]; then
    systemctl enable --now ofp-server
    sleep 2
    systemctl --no-pager status ofp-server | head -n 6
    echo "done. Its log: journalctl -u ofp-server -f  (and /var/lib/ofp-server/ofp_server.log)"
else
    echo "installed; start it with: systemctl enable --now ofp-server"
fi
