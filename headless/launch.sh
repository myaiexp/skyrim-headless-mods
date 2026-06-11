#!/bin/bash
# Launch Skyrim SE + SKSE inside a HEADLESS gamescope session.
#
#   - invisible: no window, no output on any monitor (--backend headless)
#   - GPU-accelerated: renders via DRM render node into an offscreen buffer
#   - isolated: its own inner Xwayland (:N) + its own libei socket (gamescope-0-ei);
#     input we inject never touches your real seat
#
# Screenshot it with ./shot.sh, drive it with ./drive.sh, stop it with ./stop.sh.
#
# The two non-obvious things that make this work (see docs/findings.md):
#   1. SteamAppId / SteamGameId env — without it SkyrimSE.exe can't init steam_api
#      when launched outside Steam's app wrapper and exits instantly.
#   2. We launch skse64_loader.exe directly via `proton run` (no Steam), so we get
#      SKSE without Steam owning the process.
set -euo pipefail

# ---- machine paths (edit to match your install) ----------------------------
STEAM="${STEAM:-/home/mse/.local/share/Steam}"
SKYDIR="${SKYDIR:-$STEAM/steamapps/common/Skyrim Special Edition}"
PROTON="${PROTON:-$STEAM/steamapps/common/Proton - Experimental/proton}"
APPID="${APPID:-489830}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
LOG="${LOG:-/tmp/headless-skyrim.log}"
PIDFILE="${PIDFILE:-/tmp/headless-skyrim.pid}"
# ----------------------------------------------------------------------------

# "Is a session already up?" — trust a live pid in the pidfile, NOT a cmdline
# grep. `pgrep -f "gamescope --backend headless"` substring-matches ANY process
# whose argv contains that phrase (an editor on this file, a shell command you
# typed) — it self-matched and cost a confused minute. The pidfile holds the real
# gamescope pid (see the launch trick below); verify it's alive AND actually
# gamescope before refusing.
if [ -s "$PIDFILE" ] && read -r oldpid < "$PIDFILE" && [ -n "$oldpid" ] \
   && kill -0 "$oldpid" 2>/dev/null \
   && tr '\0' ' ' < "/proc/$oldpid/cmdline" 2>/dev/null | grep -q gamescope; then
    echo "A headless gamescope is already running (pid $oldpid). ./stop.sh first." >&2
    exit 1
fi

cd "$SKYDIR"
export STEAM_COMPAT_DATA_PATH="$STEAM/steamapps/compatdata/$APPID"
export STEAM_COMPAT_CLIENT_INSTALL_PATH="$STEAM"
export SteamAppId="$APPID"
export SteamGameId="$APPID"

echo "launching headless Skyrim (gamescope ${WIDTH}x${HEIGHT}); log -> $LOG"
rm -f "$PIDFILE"  # any leftover here is a dead session (the guard above cleared a live one)
# `setsid foo &` makes $! the setsid WRAPPER, which forks the real process and
# exits — so $! is a corpse, useless for a pidfile. Run an inner shell that
# records its own pid and then `exec`s into gamescope: the pid survives the exec,
# so $$ written here IS the gamescope compositor (and the SIGUSR2 screenshot
# target). Positional args ($1..) sidestep quoting the space-bearing paths.
setsid bash -c '
    echo "$$" > "$1"
    exec gamescope --backend headless -W "$2" -H "$3" -- "$4" run "$5"
' _ "$PIDFILE" "$WIDTH" "$HEIGHT" "$PROTON" "$SKYDIR/skse64_loader.exe" \
    > "$LOG" 2>&1 < /dev/null &
disown

echo "started (pid $(cat "$PIDFILE" 2>/dev/null)). Skyrim takes ~1-2 min to reach the main menu."
echo "wait for interactive:  ./ready.sh   |   capture: ./shot.sh /tmp/sky.png"
