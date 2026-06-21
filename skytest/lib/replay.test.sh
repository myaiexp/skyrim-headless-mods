#!/usr/bin/env bash
# Unit tests for lib/replay.sh — the no-game-needed surface of `skytest replay`:
#   Task 1  the .steps parser (replay_parse)            — pure text
#   Task 2  the gate resolver + unknown-cond path       — pure text
#   Task 3  the interpreter's emitted command stream    — stubbed gs_drive/_probe_send
# Run:  bash skytest/lib/replay.test.sh        (exit 0 = all pass, non-zero = a failure)
#
# replay.sh is normally sourced by `skytest` (which supplies say/die/usage_err and the
# gamescope helpers). Here we stub just enough of that environment so the lib sources
# standalone and its pure-logic functions can be exercised without launching the game.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# --- stubs for the parent-skytest symbols replay.sh references ----------------
say()       { :; }
die()       { printf 'die: %s\n' "$1" >&2; return 1; }
usage_err() { printf '%s\n' "$1" >&2; return 2; }

# shellcheck source=lib/replay.sh
source "$HERE/replay.sh"

# --- tiny assertion harness ---------------------------------------------------
pass=0 fail=0
check() {  # check <name> <expected> <actual>
  if [ "$2" = "$3" ]; then
    pass=$((pass + 1))
  else
    fail=$((fail + 1))
    printf 'FAIL: %s\n  expected: %q\n  actual:   %q\n' "$1" "$2" "$3"
  fi
}
check_rc() {  # check_rc <name> <expected-rc> <actual-rc>
  if [ "$2" = "$3" ]; then pass=$((pass + 1)); else
    fail=$((fail + 1)); printf 'FAIL: %s\n  expected rc: %s\n  actual rc:   %s\n' "$1" "$2" "$3"
  fi
}
contains() {  # contains <name> <needle> <haystack>
  case "$3" in
    *"$2"*) pass=$((pass + 1)) ;;
    *) fail=$((fail + 1)); printf 'FAIL: %s\n  expected substring: %q\n  in: %q\n' "$1" "$2" "$3" ;;
  esac
}

# =============================================================================
# Task 1 — replay_parse
# =============================================================================

# exec keeps the whole line verbatim (spaces preserved)
check "exec verbatim" \
  'STEP exec line=player.placeatme 0x1A2B3 1' \
  "$(replay_parse - <<<'exec player.placeatme 0x1A2B3 1')"

# hold splits target from gate, does NOT treat the gate as another key
check "hold target/gate split" \
  'STEP hold target=LMB gate=until:charged' \
  "$(replay_parse - <<<'hold LMB until:charged')"

# key sequence collapses to a comma list
check "key sequence -> comma list" \
  'STEP key keys=Down,Down' \
  "$(replay_parse - <<<'key Down Down')"

# comments and blank lines are dropped; line numbering still counts them
check "comments + blanks dropped" \
  'STEP exec line=coc WhiterunOrigin' \
  "$(printf '# setup\n\nexec coc WhiterunOrigin\n' | replay_parse -)"

# tap takes one key; wait/shot normalize as specified
check "tap one key"  'STEP tap key=e'              "$(replay_parse - <<<'tap e')"
check "wait gate"    'STEP wait gate=until:inworld' "$(replay_parse - <<<'wait until:inworld')"
check "shot named"   'STEP shot name=/tmp/x.png'    "$(replay_parse - <<<'shot /tmp/x.png')"
check "shot default" 'STEP shot name='              "$(replay_parse - <<<'shot')"

# leading/extra whitespace around the verb is tolerated (design uses aligned columns)
check "aligned columns" \
  'STEP exec line=coc WhiterunOrigin' \
  "$(replay_parse - <<<'exec   coc WhiterunOrigin')"

# unknown verb fails with the line number, non-zero exit
unknown_err="$(replay_parse - <<<'frobnicate 3' 2>&1 >/dev/null)"; rc=$?
check_rc  "unknown verb non-zero"   2 "$rc"
contains  "unknown verb line number" "line 1: unknown step 'frobnicate'" "$unknown_err"

# a step missing a required argument is ALSO a parse error (line N), caught by --dry-run —
# not a silent no-op or a confusing runtime failure. tap/key need a key; hold needs a gate.
tap_err="$(replay_parse - <<<'tap' 2>&1 >/dev/null)"; rc=$?
check_rc  "tap no key non-zero"   2 "$rc"
contains  "tap no key message"    "line 1: 'tap' needs a key" "$tap_err"

key_err="$(replay_parse - <<<'key' 2>&1 >/dev/null)"; rc=$?
check_rc  "key no keys non-zero"  2 "$rc"
contains  "key no keys message"   "line 1: 'key' needs at least one key" "$key_err"

hold_err="$(replay_parse - <<<'hold LMB' 2>&1 >/dev/null)"; rc=$?
check_rc  "hold no gate non-zero" 2 "$rc"
contains  "hold no gate message"  "line 1: 'hold' needs" "$hold_err"

wait_err="$(replay_parse - <<<'wait' 2>&1 >/dev/null)"; rc=$?
check_rc  "wait no gate non-zero" 2 "$rc"
contains  "wait no gate message"  "line 1: 'wait' needs" "$wait_err"

# =============================================================================
# Task 2 — gate resolver + unknown-condition path
# =============================================================================

# inworld resolves to the status query + the world.inWorld predicate
gc='' gs='' gp=''
resolve_gate "inworld" gc gs gp
check "resolve inworld cmd"  '{"cmd":"status"}'        "$gc"
check "resolve inworld src"  'status'                  "$gs"
check "resolve inworld pred" '.world.inWorld == true'  "$gp"

# menu:<NAME> resolves to the is-menu-open query, src=menu, name-bound predicate
gc='' gs='' gp=''
resolve_gate "menu:FavoritesMenu" gc gs gp
check "resolve menu cmd"  '{"cmd":"is-menu-open","menu":"FavoritesMenu"}'  "$gc"
check "resolve menu src"  'menu'                                          "$gs"
check "resolve menu pred" '.menu=="FavoritesMenu" and .open==true'        "$gp"

# an empty menu name is rejected (not a silent {"menu":""})
gc='' gs='' gp=''
resolve_gate "menu:" gc gs gp 2>/dev/null; rc=$?
check_rc "resolve menu: empty name non-zero" 2 "$rc"

# unknown gate condition is a clear error, not a silent hang — replay_wait_gate
# must fail immediately (it resolves before touching any IO), so no session needed.
bogus_err="$(replay_wait_gate "bogus" 2>&1)"; rc=$?
check_rc  "wait bogus non-zero"     2 "$rc"
contains  "wait bogus message" "replay: unknown gate condition 'bogus'" "$bogus_err"

# =============================================================================
# Task 3 — step interpreter (assert the emitted IO stream, no game)
# These stubs OVERRIDE the real IO boundary; they must come AFTER the Task 2 cases,
# which need the real resolve_gate / replay_wait_gate.
# =============================================================================
EMITTED=()
gs_drive()         { EMITTED+=("drive $*"); }
gs_shot()          { EMITTED+=("shot $*"); printf '/tmp/shot.png\n'; }
_probe_send()      { EMITTED+=("send $*"); }
GATE_RC=0 ACK_RC=0
replay_wait_gate() { EMITTED+=("gate $1"); return "$GATE_RC"; }
_replay_wait_ack() { EMITTED+=("ack $1"); _replay_ack_err=""; return "$ACK_RC"; }

emitted_str() { local IFS='|'; printf '%s' "${EMITTED[*]}"; }
index_of() { local i; for i in "${!EMITTED[@]}"; do [ "${EMITTED[$i]}" = "$1" ] && { printf '%s' "$i"; return; }; done; printf -- '-1'; }

# hold LMB until:charged -> press, gate poll, release, in that exact order
EMITTED=(); GATE_RC=0
replay_run - <<<'hold LMB until:charged' >/dev/null 2>&1
check "hold emits press/gate/release" 'drive btn 272 1|gate charged|drive btn 272 0' "$(emitted_str)"

# a failed gate STILL releases the button before aborting, and replay_run is non-zero
EMITTED=(); GATE_RC=1
replay_run - <<<'hold LMB until:charged' >/dev/null 2>&1; rc=$?
check_rc "failed gate -> non-zero"  1 "$rc"
check    "failed gate still releases" 'drive btn 272 0' "${EMITTED[-1]}"

# exec waits for its ack BEFORE the next step's input goes out
EMITTED=(); GATE_RC=0 ACK_RC=0
replay_run - <<<$'exec player.additem 0xf 100\ntap e' >/dev/null 2>&1
ack_i="$(index_of 'ack exec-1')"; tap_i="$(index_of 'drive tap e')"
check "exec acked before next input" 1 \
  "$([ "$ack_i" -ge 0 ] && [ "$tap_i" -gt "$ack_i" ] && echo 1 || echo 0)"

# a non-LMB/RMB hold target passes the key NAME to gs_drive (NOT a pre-resolved keycode):
# gs_drive resolves+validates the name itself, exactly like tap/seq. Pre-resolving here
# double-resolves — gs_drive would re-run gs_keycode on "18" ("unknown key: 18") and the
# press would silently no-op. So the emitted stream must carry the name, not the code.
EMITTED=(); GATE_RC=0
replay_run - <<<'hold e 200ms' >/dev/null 2>&1
check "hold key passes name not code" 'drive key e 1|drive key e 0' "$(emitted_str)"

# a press that FAILS (bad key name / dead socket) aborts BEFORE the gate and is non-zero —
# it must not be swallowed (a gate on a key that never went down would burn the timeout).
# Stub gs_drive to fail the press; the gate must NOT run and replay_run must be non-zero.
EMITTED=(); GATE_RC=0
gs_drive() { EMITTED+=("drive $*"); return 2; }                    # press fails
replay_run - <<<'hold e until:inworld' >/dev/null 2>&1; rc=$?
check_rc "hold press failure -> non-zero"   2 "$rc"
check    "hold press failure skips the gate" 'drive key e 1' "$(emitted_str)"
gs_drive() { EMITTED+=("drive $*"); }                             # restore the passing stub

# =============================================================================
# Task 4 — replay_lint (the --dry-run SEMANTIC pre-flight: keys / gates / durations / JSON)
# replay_lint reaches into gs_keycode, which lives in lib/gamescope.sh (not sourced here, since
# it needs the parent skytest's $SCRIPT_DIR etc.). Stub it with the REAL function's contract: a
# known key name -> a code (rc 0), an unknown name -> rc 2. resolve_gate / _replay_dur_secs are
# the genuine ones from replay.sh. The real gs_keycode wiring is covered by the end-to-end
# `skytest replay --dry-run` check in the commit's manual verification.
# =============================================================================
gs_keycode() { case "$1" in e|q|w|a|s|d|up|down|enter|tilde|escape|space) echo 1 ;; *) return 2 ;; esac; }

# a clean script lints clean: every key/gate/duration is valid (JSON checked only if jq present)
clean='tap e
key w a s d
wait until:inworld
wait until:menu:Console
hold LMB 200ms
hold e until:inworld
wait 1s'
command -v jq >/dev/null 2>&1 && clean+=$'\ncmd {"cmd":"status"}'
lint_out="$(printf '%s\n' "$clean" | replay_parse - | replay_lint 2>&1)"; rc=$?
check_rc "lint clean -> 0"          0  "$rc"
check    "lint clean -> no output"  "" "$lint_out"

# a typo'd key in `tap` is caught with the step number + verb (the deferred ideas item)
e="$(replay_parse - <<<'tap jouurnal' | replay_lint 2>&1)"; rc=$?
check_rc "lint bad tap key -> 2"  2 "$rc"
contains "lint bad tap key msg"   "step 1 (tap): unknown key 'jouurnal'" "$e"

# a typo'd key INSIDE a `key` sequence is caught per-key (not just the first)
e="$(replay_parse - <<<'key w foo d' | replay_lint 2>&1)"; rc=$?
check_rc "lint bad key-seq -> 2"  2 "$rc"
contains "lint bad key-seq msg"   "step 1 (key): unknown key 'foo'" "$e"

# an unknown until: gate (resolve_gate says no) is caught — distinct from an unknown verb
e="$(replay_parse - <<<'wait until:inwrld' | replay_lint 2>&1)"; rc=$?
check_rc "lint bad gate -> 2"     2 "$rc"
contains "lint bad gate msg"      "step 1 (wait): bad gate 'until:inwrld'" "$e"

# a malformed duration is caught (the *s arm now validates the number too, via _replay_dur_secs)
e="$(replay_parse - <<<'wait 500' | replay_lint 2>&1)"; rc=$?
check_rc "lint bad duration -> 2" 2 "$rc"
contains "lint bad duration msg"  "step 1 (wait): bad gate '500'" "$e"

# a hold with a bad key target is caught (LMB/RMB exempt; a key name must resolve)
e="$(replay_parse - <<<'hold zzz until:inworld' | replay_lint 2>&1)"; rc=$?
check_rc "lint bad hold key -> 2" 2 "$rc"
contains "lint bad hold key msg"  "step 1 (hold): unknown hold key 'zzz'" "$e"

# a hold with a valid key but a bad gate is caught (the gate half of the step)
e="$(replay_parse - <<<'hold e 5x' | replay_lint 2>&1)"; rc=$?
check_rc "lint bad hold gate -> 2" 2 "$rc"
contains "lint bad hold gate msg"  "step 1 (hold): bad gate '5x'" "$e"

# multiple bad steps all report, and the overall rc is still 2 (lint is exhaustive, not first-fail)
e="$(printf 'tap nope\nwait until:bogus\n' | replay_parse - | replay_lint 2>&1)"; rc=$?
check_rc "lint multi-bad -> 2"    2 "$rc"
contains "lint multi-bad step1"   "step 1 (tap): unknown key 'nope'" "$e"
contains "lint multi-bad step2"   "step 2 (wait): bad gate 'until:bogus'" "$e"

# exec + shot carry no semantic obligation the parser didn't already meet -> clean
e="$(printf 'exec coc WhiterunOrigin\nshot /tmp/x.png\n' | replay_parse - | replay_lint 2>&1)"; rc=$?
check_rc "lint exec/shot -> 0" 0 "$rc"

# malformed cmd JSON is caught up front (jq-gated, like the interpreter's own runtime check)
if command -v jq >/dev/null 2>&1; then
  e="$(replay_parse - <<<'cmd {not json}' | replay_lint 2>&1)"; rc=$?
  check_rc "lint bad cmd json -> 2" 2 "$rc"
  contains "lint bad cmd json msg"  "step 1 (cmd): payload is not a valid JSON object" "$e"
fi

printf '\nreplay.test.sh: %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
