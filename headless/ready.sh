#!/bin/bash
# Block until the headless game is FULLY INTERACTIVE (in-world), then exit 0.
#
# Why this exists: there's no cheap process signal for "ready". SkyrimSE.exe spawns
# minutes into the Proton boot (so an early `pgrep SkyrimSE.exe` false-negatives),
# and being at the main menu isn't ready either. The reliable signal is the
# SkytestProbe `status` command reporting `inWorld:true` (no Main/Loading menu AND
# the player's 3D loaded) — the exact gate the probe's `exec` uses. This polls it.
#
# Usage:
#   ./ready.sh [timeout_seconds]      default 180
# Exit: 0 = in-world; 1 = timed out; 2 = session died / not running.
set -uo pipefail

STEAM="${STEAM:-/home/mse/.local/share/Steam}"
APPID="${APPID:-489830}"
PIDFILE="${PIDFILE:-/tmp/headless-skyrim.pid}"
TIMEOUT="${1:-180}"

PREFIX="$STEAM/steamapps/compatdata/$APPID/pfx/drive_c/users/steamuser"
SKYTEST="$PREFIX/Documents/My Games/Skyrim Special Edition/SKSE/skytest"
CMDS="$SKYTEST/commands.jsonl"
TRACE="$SKYTEST/trace.jsonl"

# Session liveness: if launch.sh wrote a pidfile and that pid is dead, the session
# crashed/stopped — fail fast instead of polling a corpse for the full timeout.
session_dead() {
    [ -s "$PIDFILE" ] || return 1   # no pidfile -> can't judge; don't fast-fail
    local pid; read -r pid < "$PIDFILE" || return 1
    [ -n "$pid" ] && ! kill -0 "$pid" 2>/dev/null
}

echo "waiting for in-world (timeout ${TIMEOUT}s)…" >&2
i=0
last_state=""
deadline=$((SECONDS + TIMEOUT))
while [ "$SECONDS" -lt "$deadline" ]; do
    if session_dead; then
        echo "session died (pidfile pid not alive). ./launch.sh again." >&2
        exit 2
    fi

    # Ask the probe for fresh world state (no-op until the trace writer exists).
    if [ -d "$SKYTEST" ]; then
        i=$((i + 1))
        printf '{"id":"ready-%d","cmd":"status"}\n' "$i" >> "$CMDS" 2>/dev/null || true
    fi

    state="booting"  # default until the probe is loaded and answering
    if [ -f "$TRACE" ]; then
        line="$(grep -F '"src":"status"' "$TRACE" 2>/dev/null | tail -1)"
        if [ -n "$line" ]; then
            read -r inworld is3d main load < <(
                printf '%s' "$line" | jq -r '.world | "\(.inWorld) \(.is3DLoaded) \(.mainMenu) \(.loadingMenu)"' 2>/dev/null
            )
            if   [ "$inworld" = "true" ]; then state="in-world"
            elif [ "$main"    = "true" ]; then state="main-menu"
            elif [ "$load"    = "true" ]; then state="loading"
            elif [ "$is3d"    = "true" ]; then state="in-world"   # 3D up, menus closed
            else                               state="loaded (no 3D yet)"
            fi
        fi
    fi

    [ "$state" != "$last_state" ] && { echo "  -> $state" >&2; last_state="$state"; }
    if [ "$state" = "in-world" ]; then
        echo "ready: in-world (interactive)" >&2
        exit 0
    fi
    sleep 1
done

echo "timed out after ${TIMEOUT}s (last state: ${last_state:-booting})." >&2
exit 1
