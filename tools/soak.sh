#!/bin/sh
# SPDX-License-Identifier: MIT
# The soak test: a dedicated server and a few headless players (ofp_bot) for hours, to
# find what only shows over time: memory or open files that keep growing, matches that
# get out of step, the server or a player falling over, the clock wrapping.
#
#   tools/soak.sh [--hours H] [--bots N] [--port P] [--build DIR] [--out DIR]
#                 [--time-limit MIN] [--extras] [--storm] [--near-wrap] [--listing URL]
#   tools/soak.sh start [the same]   in the background (nohup: logging out doesn't stop it)
#   tools/soak.sh stop               stop it (its players and server too); it writes its report
#   tools/soak.sh status             running? where its logs are, the server's status, the last sample
#
#   --hours H        how long (12)
#   --bots N         players (4; one server takes up to 8)
#   --port P         the server's UDP port (29960)
#   --build DIR      where ofp_server and ofp_bot are (build-server, else build)
#   --out DIR        where the logs and the report go (soak-YYYYmmdd-HHMMSS)
#   --time-limit M   minutes a match (5): many matches, many starts and ends
#   --extras         pickups and the hill in every match; --storm a storm
#   --near-wrap      the server's millisecond clock starts 30 minutes before it wraps
#                    (day 49.7), so the soak goes through the wrap
#   --listing URL    announce on a listing too (public = yes): leave it out for a quiet test
#   --goal G         the mode: flag, conquest, deathmatch, annihilation, warlord, hill (flag;
#                    more than 6 players: deathmatch, whose arenas hold 8)
#
# Every minute it writes a line to samples.csv: the server's memory (RSS), CPU, open
# files, and whether everything is still running. At the end report.txt says what it
# found, and the exit status is 0 only if nothing went wrong: no crash, nobody out of
# step, memory and open files not growing (more than 20% from the first hour to the end).
# Run it inside tmux or screen (or nohup) on a server: it takes as long as you ask.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PIDF="$ROOT/soak.pid"                 # (start, stop, status: the test running in the background)
CURF="$ROOT/soak.current"             # (its folder)
running() { [ -f "$PIDF" ] && kill -0 "$(cat "$PIDF")" 2>/dev/null; }
case "${1:-}" in
    start)
        shift
        if running; then echo "soak: one is running already (pid $(cat "$PIDF")): tools/soak.sh stop first" >&2; exit 1; fi
        nohup "$0" "$@" > "$ROOT/soak-last.out" 2>&1 < /dev/null &
        echo $! > "$PIDF"
        sleep 3
        cat "$ROOT/soak-last.out"
        running && echo "soak: started in the background (pid $(cat "$PIDF")); tools/soak.sh status, tools/soak.sh stop" \
                || { echo "soak: it didn't start (above)" >&2; rm -f "$PIDF"; exit 1; }
        exit 0 ;;
    stop)
        if ! running; then echo "soak: none running"; rm -f "$PIDF"; exit 0; fi
        P=$(cat "$PIDF")
        kill -TERM "$P"
        i=0; while kill -0 "$P" 2>/dev/null && [ $i -lt 30 ]; do sleep 1; i=$((i + 1)); done
        kill -0 "$P" 2>/dev/null && { kill -9 "$P"; echo "soak: had to kill it (pid $P)"; }
        rm -f "$PIDF"
        echo "soak: stopped; its report: $(cat "$CURF" 2>/dev/null)/report.txt"
        exit 0 ;;
    status)
        if running; then echo "soak: running (pid $(cat "$PIDF")), in $(cat "$CURF" 2>/dev/null)"; else echo "soak: not running"; fi
        D=$(cat "$CURF" 2>/dev/null || true)
        if [ -n "$D" ] && [ -d "$D" ]; then
            [ -f "$D/status.json" ] && grep -E '"(state|uptime|people|playing|matches|tick)"' "$D/status.json" | tr -d ' ,'
            [ -f "$D/samples.csv" ] && echo "last sample: $(tail -n 1 "$D/samples.csv")"
            [ -f "$D/report.txt" ] && echo "report: $D/report.txt"
        fi
        exit 0 ;;
esac
HOURS=12; BOTS=4; PORT=29960; BUILD=""; OUT=""; TLIMIT=5; EXTRAS=no; STORM=no; WRAP=no; LISTING=""; GOAL=""
while [ $# -gt 0 ]; do
    case "$1" in
        --hours) HOURS=$2; shift 2 ;;
        --bots) BOTS=$2; shift 2 ;;
        --port) PORT=$2; shift 2 ;;
        --build) BUILD=$2; shift 2 ;;
        --out) OUT=$2; shift 2 ;;
        --time-limit) TLIMIT=$2; shift 2 ;;
        --extras) EXTRAS=yes; shift ;;
        --storm) STORM=yes; shift ;;
        --near-wrap) WRAP=yes; shift ;;
        --listing) LISTING=$2; shift 2 ;;
        --goal) GOAL=$2; shift 2 ;;
        -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
        *) echo "soak.sh: what's $1? (--help)" >&2; exit 2 ;;
    esac
done
cd "$(dirname "$0")/.."
if [ -z "$BUILD" ]; then
    for d in build-server build; do [ -x "$d/ofp_server" ] && [ -x "$d/ofp_bot" ] && BUILD=$d && break; done
fi
[ -n "$BUILD" ] && [ -x "$BUILD/ofp_server" ] && [ -x "$BUILD/ofp_bot" ] || {
    echo "soak.sh: no ofp_server and ofp_bot built (cmake --build build-server; or --build DIR)" >&2; exit 2; }
BUILD=$(cd "$BUILD" && pwd)
[ -n "$OUT" ] || OUT="soak-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
echo "$OUT" > "$CURF"                 # (for status and stop)
cd "$OUT"

# the mode: as asked; the hill with --extras; more than 6 players: team deathmatch (its arenas hold
# 8, 4 a side; capture the flag's camp maps 6), else capture the flag
[ -n "$GOAL" ] || GOAL=$( [ "$EXTRAS" = yes ] && echo hill || { [ "$BOTS" -gt 6 ] && echo deathmatch || echo flag; } )
# the server: matches end often (time limit), computer players fill the places; the countdown
# outlasts the players' start (one a second), so the first match has them all
cat > server.cfg <<EOF
name           = Soak test
port           = $PORT
max_players    = 8
min_players    = 1
countdown      = $((BOTS + 3))
max_per_address = $( [ "$BOTS" -gt 4 ] && echo "$BOTS" || echo 4 )
time_limit     = $TLIMIT
fill_bots      = yes
bot_level      = normal
weather        = $( [ "$STORM" = yes ] && echo storm || echo random )
pickups        = $EXTRAS
goal           = $GOAL
public         = $( [ -n "$LISTING" ] && echo yes || echo no )
listing_url    = ${LISTING:-http://127.0.0.1:1/}
share_matches  = no
database       = soak.db
log_file       = server.log
log_level      = info
status_file    = status.json
EOF
CLOCK=""
true
[ "$WRAP" = yes ] && CLOCK="OFP_CLOCK_OFFSET=$((4294967296 - 30 * 60 * 1000))"
MINUTES=$(awk "BEGIN { printf \"%d\", $HOURS * 60 }")
echo "soak: $HOURS h, $BOTS bots, port $PORT, $TLIMIT-minute matches of $GOAL; logs in $OUT"

env $CLOCK "$BUILD/ofp_server" -c server.cfg --quiet < /dev/null > server.out 2>&1 &
SERVER=$!
sleep 2
kill -0 $SERVER 2>/dev/null || { echo "soak: the server didn't start (server.out)" >&2; exit 1; }
BOTPIDS=""
i=1
while [ $i -le "$BOTS" ]; do
    env $CLOCK "$BUILD/ofp_bot" "127.0.0.1:$PORT" --name "Soak$i" --minutes "$MINUTES" > "bot-$i.log" 2>&1 &
    BOTPIDS="$BOTPIDS $!"
    i=$((i + 1))
    sleep 1
done
stop() {
    for p in $BOTPIDS; do kill "$p" 2>/dev/null || true; done
    kill -INT $SERVER 2>/dev/null || true
    sleep 3
    kill -9 $SERVER 2>/dev/null || true
}
STOPPED=""
trap 'STOPPED=1' INT TERM             # (a stop: out of the loop, the players and server stopped, the report written)

echo "minute,time,server_rss_kb,server_cpu,server_fds,server_alive,bots_alive" > samples.csv
END=$(( $(date +%s) + MINUTES * 60 ))
MIN=0
CRASH=""
while [ "$(date +%s)" -lt "$END" ] && [ -z "$STOPPED" ]; do
    sleep 60 & wait $! 2>/dev/null || true    # (a stop needn't wait out the minute)
    [ -n "$STOPPED" ] && break
    MIN=$((MIN + 1))
    if kill -0 $SERVER 2>/dev/null; then
        RSS=$(ps -o rss= -p $SERVER | tr -d ' ')
        CPU=$(ps -o %cpu= -p $SERVER | tr -d ' ')
        FDS=$(ls /proc/$SERVER/fd 2>/dev/null | wc -l | tr -d ' ')
        ALIVE=1
    else
        RSS=0; CPU=0; FDS=0; ALIVE=0
        CRASH="the server stopped after $MIN minutes"
    fi
    UP=0
    for p in $BOTPIDS; do kill -0 "$p" 2>/dev/null && UP=$((UP + 1)); done
    echo "$MIN,$(date +%H:%M:%S),$RSS,$CPU,$FDS,$ALIVE,$UP" >> samples.csv
    [ -n "$CRASH" ] && break
done
stop

# ---- the report ----------------------------------------------------------------------
{
    echo "OpenFirePower soak test: $(date)"
    echo "asked for $HOURS h with $BOTS bots; ran $MIN minutes$( [ -n "$STOPPED" ] && echo " (stopped early)" ); $( "$BUILD/ofp_server" --version )"
    echo
    MATCHES=$(grep -c "match over\|won the match\|a draw" server.log 2>/dev/null || true)
    command -v sqlite3 > /dev/null && [ -f soak.db ] && MATCHES=$(sqlite3 soak.db "SELECT COUNT(*) FROM matches" 2>/dev/null || echo "$MATCHES")
    DESYNC=$(grep -c "out of step" server.log 2>/dev/null || true)
    ERRS=$(grep -c "\[error\]" server.log 2>/dev/null || true)
    WARNS=$(grep -c "\[warn\]" server.log 2>/dev/null || true)
    echo "matches played:      ${MATCHES:-0}"
    echo "out of step:         ${DESYNC:-0}$( [ "${DESYNC:-0}" -gt 0 ] && echo "   (desync-*.ofprep and -server.txt here: ofp_match diff)" )"
    echo "server errors/warns: ${ERRS:-0} / ${WARNS:-0}"
    echo "crash:               ${CRASH:-none}"
    awk -F, 'NR > 1 && $3 > 0 { n++; r[n] = $3; f[n] = $5; c += $4 }
             END { if (!n) { print "memory:              no samples"; exit }
                   first = 0; k = (n < 60 ? n : 60); for (i = 1; i <= k; i++) if (r[i] > first) first = r[i];
                   last = r[n]; peak = 0; for (i = 1; i <= n; i++) if (r[i] > peak) peak = r[i];
                   printf "memory (RSS):        first hour %d KB, peak %d KB, at the end %d KB (%+.0f%%)\n", first, peak, last, (last - first) * 100.0 / first
                   printf "open files:          %d at the start, %d at the end\n", f[1], f[n]
                   printf "CPU, on average:     %.1f%%\n", c / n }' samples.csv
    echo
    echo "the bots' last lines:"
    for f in bot-*.log; do tail -n 1 "$f" | sed 's/^/  /'; done
} > report.txt
cat report.txt

# ---- the verdict -----------------------------------------------------------------------
FAIL=""
[ -n "$CRASH" ] && FAIL="$FAIL crash"
[ "${DESYNC:-0}" -gt 0 ] && FAIL="$FAIL out-of-step"
awk -F, 'NR > 1 && $3 > 0 { n++; r[n] = $3; f[n] = $5 }
         END { if (n < 2) exit 0; first = 0; k = (n < 60 ? n : 60); for (i = 1; i <= k; i++) if (r[i] > first) first = r[i]
               if (r[n] > first * 1.2) exit 1; if (f[n] > f[1] + 8) exit 1; exit 0 }' samples.csv || FAIL="$FAIL growing"
[ -f "$PIDF" ] && [ "$(cat "$PIDF")" = "$$" ] && rm -f "$PIDF"   # (this one's done)
if [ -n "$FAIL" ]; then echo "soak: FAILED:$FAIL"; exit 1; fi
echo "soak: ok"
