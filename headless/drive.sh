#!/bin/bash
# Inject input into the headless gamescope via libei (src/eidriver), addressing
# its gamescope-0-ei socket. Isolated to that session — never touches your seat.
#
# Keyboard works (evdev keycodes). Pointer works via RELATIVE motion: `abs`/`click x y`
# below are real absolute positioning (home-to-corner + relative, mapped 1:1 by
# gamescope). Raw libei *absolute* is ignored by Skyrim — don't use it. See findings #9.
#
# Usage:
#   ./drive.sh tap <key>            press+release one key      (e.g. ./drive.sh tap enter)
#   ./drive.sh seq <key> <key> ...  tap several keys in order  (e.g. ./drive.sh seq down down enter)
#   ./drive.sh key <key> <0|1>      hold(1)/release(0) one key
#   ./drive.sh abs <x> <y>          move cursor to pixel (x,y)            (absolute)
#   ./drive.sh click [<x> <y>]      click at (x,y), or at current pos if omitted
#   ./drive.sh rel <dx> <dy>        relative pointer move (1 unit = 1 px)
#   ./drive.sh raw <eidriver args>  pass through verbatim
#
# Key names -> Linux evdev keycodes (KEY_* from input-event-codes.h):
set -euo pipefail
HERE="$(dirname "$(readlink -f "$0")")"
EIDRIVER="$HERE/src/eidriver"
SOCK="${EIS_SOCKET:-${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/gamescope-0-ei}"

[ -x "$EIDRIVER" ] || { echo "build it first: $HERE/src/build.sh" >&2; exit 1; }
[ -S "$SOCK" ]     || { echo "no EIS socket at $SOCK (is the headless session up?)" >&2; exit 1; }

keycode() {
    case "$1" in
        esc|escape) echo 1 ;;  enter|return) echo 28 ;;  tab) echo 15 ;;  space) echo 57 ;;
        backspace) echo 14 ;;  up) echo 103 ;;  down) echo 108 ;;  left) echo 105 ;;  right) echo 106 ;;
        pageup) echo 104 ;;    pagedown) echo 109 ;;  home) echo 102 ;;  end) echo 107 ;;
        tilde|console|grave) echo 41 ;;  # ` — opens the dev console in-game
        e) echo 18 ;;  r) echo 19 ;;  m) echo 50 ;;  w) echo 17 ;;  a) echo 30 ;;  s) echo 31 ;;  d) echo 32 ;;
        f) echo 33 ;;  y) echo 21 ;;  n) echo 49 ;;  j) echo 36 ;;  i) echo 23 ;;
        *) echo "unknown key: $1" >&2; exit 2 ;;
    esac
}

cmd="${1:-}"; shift || true
case "$cmd" in
    tap)   "$EIDRIVER" "$SOCK" tap "$(keycode "$1")" ;;
    seq)   args=(); for k in "$@"; do args+=(tap "$(keycode "$k")" sleep 120); done
           "$EIDRIVER" "$SOCK" "${args[@]}" ;;
    key)   "$EIDRIVER" "$SOCK" key "$(keycode "$1")" "$2" ;;
    click) if [ "$#" -ge 2 ]; then "$EIDRIVER" "$SOCK" clickat "$1" "$2"
           else "$EIDRIVER" "$SOCK" click; fi ;;
    rel)   "$EIDRIVER" "$SOCK" rel "$1" "$2" ;;
    abs|moveto|mv) "$EIDRIVER" "$SOCK" moveto "$1" "$2" ;;
    raw)   "$EIDRIVER" "$SOCK" "$@" ;;
    *) echo "usage: $0 {tap|seq|key|click [x y]|abs x y|rel dx dy|raw} ..." >&2; exit 1 ;;
esac
