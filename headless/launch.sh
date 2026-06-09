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
# ----------------------------------------------------------------------------

if pgrep -f "gamescope --backend headless" >/dev/null; then
    echo "A headless gamescope is already running. ./stop.sh first." >&2
    exit 1
fi

cd "$SKYDIR"
export STEAM_COMPAT_DATA_PATH="$STEAM/steamapps/compatdata/$APPID"
export STEAM_COMPAT_CLIENT_INSTALL_PATH="$STEAM"
export SteamAppId="$APPID"
export SteamGameId="$APPID"

echo "launching headless Skyrim (gamescope ${WIDTH}x${HEIGHT}); log -> $LOG"
setsid gamescope --backend headless -W "$WIDTH" -H "$HEIGHT" -- \
    "$PROTON" run "$SKYDIR/skse64_loader.exe" \
    > "$LOG" 2>&1 < /dev/null &
disown

echo "started. Skyrim takes ~1-2 min to reach the main menu."
echo "watch:  pgrep -f SkyrimSE.exe   |   capture: ./shot.sh /tmp/sky.png"
