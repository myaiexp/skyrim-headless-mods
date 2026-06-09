#!/bin/bash
# Kill the headless Skyrim session cleanly.
set -uo pipefail
pkill -9 -f "gamescope --backend headless" 2>/dev/null || true
pkill -9 -f "proton run.*skse64_loader" 2>/dev/null || true
pkill -9 -f "SkyrimSE.exe" 2>/dev/null || true
sleep 1
pkill -9 -f "gamescopereaper.*skse64_loader" 2>/dev/null || true
pkill -9 wineserver 2>/dev/null || true
sleep 1
if pgrep -f "gamescope --backend headless|SkyrimSE.exe" >/dev/null; then
    echo "warning: something is still alive:"; pgrep -af "gamescope|SkyrimSE.exe" | grep -v pgrep
else
    echo "headless session stopped."
fi
