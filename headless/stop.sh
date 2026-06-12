#!/bin/bash
# Kill the headless Skyrim session cleanly.
#
# Prefer the pidfile. gamescope is a session leader, so killing everything in its
# SESSION takes down the whole tree (gamescope, Xwayland, reaper, proton, and the
# game if it spawned) WITHOUT touching a Skyrim you're playing in the same prefix.
# That matters: Steam blocks a 2nd game instance, so the usual thing to clean up is
# a *blocked* headless launch (gamescope + proton idling) sitting next to your real
# game — and a blind `pkill SkyrimSE.exe` / `pkill wineserver` would kill YOUR game
# and its wineserver instead. The broad pattern-kill is only the no-pidfile fallback.
set -uo pipefail
PIDFILE="${PIDFILE:-/tmp/headless-skyrim.pid}"

mode=""
pid=""
if [ -s "$PIDFILE" ] && read -r pid < "$PIDFILE" && [ -n "$pid" ] \
   && tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null | grep -q gamescope; then
    # Targeted: kill the gamescope process group, then sweep any straggler still in
    # its session (proton spawns its own sub-groups). `pid` is the session leader.
    kill -9 -- "-$pid" 2>/dev/null || true
    for p in $(ps -e -o pid=,sid= | awk -v s="$pid" '$2==s {print $1}'); do
        kill -9 "$p" 2>/dev/null || true
    done
    mode="pid $pid"
else
    # No pidfile (manual / pre-pidfile launch): broad pattern-kill.
    # WARNING: this also kills a Skyrim you're playing in this prefix.
    pkill -9 -f "gamescope --backend headless"   2>/dev/null || true
    pkill -9 -f "proton run.*skse64_loader"      2>/dev/null || true
    pkill -9 -f "SkyrimSE.exe"                    2>/dev/null || true
    sleep 1
    pkill -9 -f "gamescopereaper.*skse64_loader" 2>/dev/null || true
    pkill -9 wineserver                          2>/dev/null || true
    mode="pattern"
fi
rm -f "$PIDFILE"
sleep 1

# Liveness re-check WITHOUT a cmdline grep that could self-match (finding #11: a
# caller whose argv contains "gamescope --backend headless" gets matched). Targeted
# mode tests the exact pid; pattern mode pgreps but drops our own shell + parent.
if [ -n "$pid" ]; then
    kill -0 "$pid" 2>/dev/null && alive=1 || alive=""
else
    alive="$(pgrep -f 'gamescope --backend headless' | grep -vw "$$" | grep -vw "${PPID:-0}" | head -1)"
fi
if [ -n "$alive" ]; then
    echo "warning: headless gamescope still alive after stop ($mode)." >&2
    exit 1
fi
echo "headless session stopped ($mode)."
exit 0
