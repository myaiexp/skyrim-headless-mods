# shellcheck shell=bash
# skytest gamescope backend — sourced by `skytest`, never executed on its own.
#
# Absorbs the old headless/{launch,ready,shot,drive,stop}.sh. A *test session* always
# runs the game under gamescope, so it is always drivable (libei) + screenshot-able
# (SIGUSR2). The only knob is gamescope's --backend value:
#   wayland   visible — a window nested in Hyprland (the default `skytest test`)
#   headless  invisible — offscreen render, no window (`skytest test --headless`)
#   sdl       fallback if wayland fails to expose the EIS socket under Hyprland
# Everything else is identical, so "watch CC drive a visible test" is the same
# machinery as a headless test — you just see it.
#
# All helpers operate on ONE detached gamescope session, tracked by GS_PIDFILE — the
# real compositor pid (recorded via the setsid inner-shell `exec` trick, so the pid
# survives the exec and is the SIGUSR2 + teardown target). Two disciplines are
# load-bearing (see skytest/docs/headless-findings.md #11/#12) — do NOT regress them:
#   - liveness is judged by the PIDFILE + a /proc cmdline check, never `pgrep -f`
#     (which self-matches an editor/shell whose argv holds "gamescope …").
#   - teardown kills the gamescope SESSION, never `pkill SkyrimSE.exe` — so a real
#     game you're playing in the same Proton prefix is left untouched.
#
# Depends on the parent `skytest` for: SCRIPT_DIR, STEAM, APPID, PROTON, LOADER,
# SKYDIR, PREFIX, the skse_env_export() launch core, and say()/die()/usage_err().

GS_PIDFILE="${GS_PIDFILE:-/tmp/skytest-gamescope.pid}"
GS_LOG="${GS_LOG:-/tmp/skytest-gamescope.log}"
GS_EIS_SOCK="${EIS_SOCKET:-${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/gamescope-0-ei}"
GS_WIDTH="${GS_WIDTH:-1280}"
GS_HEIGHT="${GS_HEIGHT:-720}"
EIDRIVER="$SCRIPT_DIR/eidriver/eidriver"

# --- session liveness --------------------------------------------------------

# True iff GS_PIDFILE holds a live pid that is the real gamescope EXECUTABLE. The
# pidfile-not-cmdline-grep discipline (finding #11): `pgrep -f gamescope` matches
# any process whose argv contains that word, including this very script. Even the
# old narrower `grep -q gamescope` over /proc/<pid>/cmdline matched the launcher
# bash shell — whose argv is the inner-shell script TEXT containing "gamescope" —
# in the window BEFORE its `exec gamescope` ran (and would falsely confirm a corpse
# whose pid got reused by a same-named shell). Match argv[0] (first NUL-delimited
# field) instead, and prefer the exact exe path via /proc/<pid>/exe when resolvable.
# Shared by the launch guard, status, shot, and stop.
gs_session_alive() {
  local pid
  [ -s "$GS_PIDFILE" ] || return 1
  read -r pid < "$GS_PIDFILE" || return 1
  [ -n "$pid" ] || return 1
  kill -0 "$pid" 2>/dev/null || return 1
  local gs_bin exe argv0
  gs_bin="$(command -v gamescope 2>/dev/null || true)"
  exe="$(readlink "/proc/$pid/exe" 2>/dev/null || true)"
  if [ -n "$exe" ]; then
    # Strip a kernel " (deleted)" suffix (gamescope upgraded mid-session).
    exe="${exe% (deleted)}"
    [ -n "$gs_bin" ] && [ "$exe" = "$gs_bin" ] && return 0
    [ "$(basename "$exe")" = gamescope ] && return 0
    return 1
  fi
  # /proc/<pid>/exe unreadable (e.g. permissions): fall back to argv[0] only — the
  # FIRST NUL-delimited cmdline field, never an argv substring (which catches the shell).
  argv0="$(tr '\0' '\n' < "/proc/$pid/cmdline" 2>/dev/null | head -1 || true)"
  [ "$(basename "$argv0")" = gamescope ]
}

# Fast-fail for the readiness poll: pidfile present but its pid dead => the session
# crashed; stop polling a corpse. No pidfile => can't judge => not "dead".
gs_session_dead() {
  [ -s "$GS_PIDFILE" ] || return 1
  local pid; read -r pid < "$GS_PIDFILE" || return 1
  [ -n "$pid" ] && ! kill -0 "$pid" 2>/dev/null
}

# Echo the live gamescope pid (empty if none). For status/messages.
gs_session_pid() { local pid; read -r pid < "$GS_PIDFILE" 2>/dev/null && printf '%s' "$pid"; }

# --- launch ------------------------------------------------------------------

# gs_launch <wayland|headless|sdl> — start a detached gamescope test session running
# the shared SKSE launch core, record the compositor pid, return once it's up.
gs_launch() {
  local backend="${1:?gs_launch: backend required}"
  case "$backend" in wayland|headless|sdl) ;; *) die "gs_launch: bad backend '$backend'" ;; esac

  if gs_session_alive; then
    die "a gamescope test session is already live (pid $(gs_session_pid))" "skytest stop"
  fi
  if [ -n "${SKYTEST_NO_LAUNCH:-}" ]; then
    say "SKYTEST_NO_LAUNCH set — skipping gamescope launch ($backend)."
    return 0
  fi

  skse_env_export   # cd $SKYDIR + export the Steam-compat env (no exec — we wrap it)
  say "launching $backend gamescope test session (${GS_WIDTH}x${GS_HEIGHT}); log -> $GS_LOG"
  rm -f "$GS_PIDFILE"   # any leftover is a dead session (the guard above cleared a live one)

  # `setsid foo &` makes $! the setsid wrapper, which forks then exits — a corpse,
  # useless for a pidfile (finding #11). Run an inner shell that records its own pid
  # then `exec`s gamescope: the pid survives the exec, so $$ here IS the compositor
  # (and the SIGUSR2 / session-kill target). Positionals sidestep quoting the
  # space-bearing PROTON/SKYDIR paths. `9>&-` closes the cross-run lock fd (skytest
  # acquire_lock) in the detached child: without it the live session inherits the flock
  # and the next `skytest stop`/`test`/`drive` would deadlock on its own session's lock.
  # shellcheck disable=SC2016  # $$/$1.. MUST stay literal — they expand in the inner shell
  setsid bash -c '
      echo "$$" > "$1"
      exec gamescope --backend "$2" -W "$3" -H "$4" -- "$5" run "$6"
  ' _ "$GS_PIDFILE" "$backend" "$GS_WIDTH" "$GS_HEIGHT" "$PROTON" "$LOADER" \
      > "$GS_LOG" 2>&1 < /dev/null 9>&- &
  disown

  local _i
  for _i in {1..40}; do [ -s "$GS_PIDFILE" ] && break; sleep 0.05; done   # inner shell writes its pid (<2s)
  gs_session_alive || { say "WARNING: gamescope pid not recorded — check $GS_LOG"; return 1; }
  say "started (pid $(gs_session_pid)). Poll for readiness with: skytest ready"
}

# --- probe IO (shared by gs_wait_ready + replay_wait_gate) -------------------

# The probe IO dir (…/SKSE/skytest): CC appends commands to commands.jsonl, the
# running game writes trace lines to trace.jsonl. Single source of truth for the
# path — both the readiness poll below and replay's gate poll (lib/replay.sh) derive
# it here instead of re-spelling the literal. Built from the parent's $MYGAMES.
_skytest_io_dir() { printf '%s' "$MYGAMES/SKSE/skytest"; }

# Append one probe-command JSON line to commands.jsonl. Best-effort: the file (and
# its dir) may not exist until the probe loads — a dropped early line is harmless,
# the poll retries. Callers inject their own unique "id".
_probe_send() { printf '%s\n' "$1" >> "$(_skytest_io_dir)/commands.jsonl" 2>/dev/null || true; }

# gs_reset_io — clear the probe IO files BEFORE a launch so a new session can never
# read the PRIOR session's state. Two distinct stale-state bugs, one fix:
#   trace.jsonl     — gs_wait_ready / replay gates grep the LAST "src":"status" line.
#                     The probe only truncates trace.jsonl when it LOADS (seconds into
#                     boot); the readiness poll runs in the window BEFORE that, so it
#                     matches the prior session's last status. If that was inWorld:true
#                     (true whenever a session ended in-world), readiness returns INSTANTLY
#                     — a false positive — and replay drives input before the EIS server
#                     is up. Clearing it here means "no status line yet" => keep waiting.
#   commands.jsonl  — a fresh probe re-reads it from offset 0 and re-runs the whole
#                     history (re-faulting old execs, flooding the new trace).
# Called by _boot_test_session before gs_launch — guards already ensured no live probe
# holds these files, so the truncate can't race a writer.
gs_reset_io() {
  local dir; dir="$(_skytest_io_dir)"
  mkdir -p "$dir" 2>/dev/null || true
  : > "$dir/commands.jsonl" 2>/dev/null || true
  : > "$dir/trace.jsonl"    2>/dev/null || true
}

# Epoch milliseconds — the same clock SkytestProbe stamps every trace line's "t" with
# (trace::NowMs = system_clock epoch-ms). Used by gs_trace --since to threshold on .t.
_now_ms() { date +%s%3N; }

# gs_cmd '<json>' [timeout=15] — the one-liner that replaces the hand-built
# `echo '<json>' >> <long path>/commands.jsonl` + manual ack-grep cycle. Appends a probe
# command and BLOCKS for its ack, then prints every trace line emitted since the send (the
# command's own src: output AND its {"ack":…} line). Injects a unique "id" if the JSON lacks
# one (the probe acks by id). Exit: 0 = ack ok:true, 1 = ack ok:false, 2 = bad JSON,
# 3 = no ack within the timeout. Needs jq (to read/inject id and the ack's ok field).
gs_cmd() {
  command -v jq >/dev/null 2>&1 || die "gs_cmd: jq required"
  local json="${1:-}" timeout="${2:-15}"
  [ -n "$json" ] || usage_err "cmd: missing JSON" "skytest cmd '{\"cmd\":\"status\"}'"
  # Validate the JSON parses FIRST (jq empty), then read .id separately — `.id // empty`
  # under `jq -e` exits 1 for valid JSON that simply lacks an id, which is NOT an error.
  printf '%s' "$json" | jq empty 2>/dev/null \
    || usage_err "cmd: invalid JSON: $json" "skytest cmd '{\"cmd\":\"status\"}'"
  local id; id="$(printf '%s' "$json" | jq -r '.id // empty' 2>/dev/null)"
  if [ -z "$id" ]; then
    id="cli-$$-${EPOCHSECONDS:-0}-$RANDOM"
    json="$(printf '%s' "$json" | jq -c --arg id "$id" '. + {id:$id}')"
  fi
  local dir cmds trace start
  dir="$(_skytest_io_dir)"; cmds="$dir/commands.jsonl"; trace="$dir/trace.jsonl"
  mkdir -p "$dir" 2>/dev/null || true
  # Record the current trace length so we print ONLY lines this command produces. A rare race
  # (the probe appends between this count and our send) shows one extra unrelated line — harmless.
  start=0; [ -f "$trace" ] && start="$(wc -l < "$trace" 2>/dev/null || echo 0)"
  printf '%s\n' "$json" >> "$cmds" 2>/dev/null || die "cmd: cannot append to $cmds"
  local deadline=$((SECONDS + timeout)) ack=""
  while [ "$SECONDS" -lt "$deadline" ]; do
    if [ -f "$trace" ]; then
      ack="$(tail -n "+$((start + 1))" "$trace" 2>/dev/null | grep -F "\"ack\":\"$id\"" | tail -1 || true)"
      [ -n "$ack" ] && break
    fi
    sleep 0.2
  done
  [ -f "$trace" ] && tail -n "+$((start + 1))" "$trace" 2>/dev/null   # the delta: src output + ack
  if [ -z "$ack" ]; then
    echo "skytest: cmd: no ack for '$id' within ${timeout}s — is the probe live? (skytest wait-probe)" >&2
    return 3
  fi
  [ "$(printf '%s' "$ack" | jq -r '.ok // false' 2>/dev/null)" = true ]
}

# gs_trace [--tail N] [--src X] [--since T] [--jq EXPR] [-f] — filtered view of trace.jsonl,
# so a read no longer means re-resolving the path and rebuilding a jq pipeline by hand.
#   --src X     keep only lines tagged "src":"X" (cheap grep, no jq)
#   --since T   keep lines with .t >= T; T is epoch-ms, or relative (30s / 500ms ago)
#   --jq EXPR   pipe each surviving line through `jq -c EXPR`
#   --tail N    last N lines AFTER filtering (default 40); ignored under -f
#   -f          follow (tail -f) and apply the filters to new lines live
gs_trace() {
  local tail_n=40 src="" since="" jqexpr="" follow=""
  while [ "$#" -gt 0 ]; do
    case "$1" in
      --tail)      tail_n="${2:-40}"; shift 2 ;;
      --src)       src="${2:-}"; shift 2 ;;
      --since)     since="${2:-}"; shift 2 ;;
      --jq)        jqexpr="${2:-}"; shift 2 ;;
      -f|--follow) follow=1; shift ;;
      *) usage_err "trace: unknown arg: $1" "skytest trace --tail 20 --src ramp" ;;
    esac
  done
  local trace; trace="$(_skytest_io_dir)/trace.jsonl"
  [ -f "$trace" ] || die "no trace.jsonl yet at $trace" "skytest test <mod>"
  if { [ -n "$since" ] || [ -n "$jqexpr" ]; } && ! command -v jq >/dev/null 2>&1; then
    die "trace: --since/--jq need jq"
  fi
  local since_ms=""
  if [ -n "$since" ]; then
    case "$since" in
      *ms) since_ms=$(( $(_now_ms) - ${since%ms} )) ;;
      *s)  since_ms=$(( $(_now_ms) - ${since%s} * 1000 )) ;;
      *)   since_ms="$since" ;;   # bare number = absolute epoch-ms
    esac
  fi
  # Apply src/since/jq to stdin, each stage a no-op `cat` when its flag is unset. Dynamic
  # scoping: this nested function sees gs_trace's locals (src/since_ms/jqexpr).
  _trace_apply() {
    { if [ -n "$src" ]; then grep -F "\"src\":\"$src\""; else cat; fi; } \
    | { if [ -n "$since_ms" ]; then jq -c --argjson s "$since_ms" 'select((.t // 0) >= $s)'; else cat; fi; } \
    | { if [ -n "$jqexpr" ]; then jq -c "$jqexpr"; else cat; fi; }
  }
  if [ -n "$follow" ]; then
    tail -n "$tail_n" -f "$trace" | _trace_apply
  else
    _trace_apply < "$trace" | tail -n "$tail_n"
  fi
}

# gs_wait_probe [timeout=120] — block until the probe is LOADED and answering (a src:"status"
# line appears in response to a ping), independent of in-world. gs_wait_ready waits for
# inWorld:true and so never returns on the menu-booting full / `play agent` path; this only
# needs the probe alive — the right gate after `play agent` or a native-DLL restart. Best run
# right after a launch (which gs_reset_io'd the trace, so any status line is fresh, not stale).
gs_wait_probe() {
  local timeout="${1:-120}"
  local dir cmds trace; dir="$(_skytest_io_dir)"; cmds="$dir/commands.jsonl"; trace="$dir/trace.jsonl"
  mkdir -p "$dir" 2>/dev/null || true
  echo "waiting for the probe to answer (timeout ${timeout}s)…" >&2
  local i=0 deadline=$((SECONDS + timeout))
  while [ "$SECONDS" -lt "$deadline" ]; do
    if gs_session_dead; then echo "session died (gamescope pid not alive)." >&2; return 2; fi
    i=$((i + 1))
    printf '{"id":"waitprobe-%d","cmd":"status"}\n' "$i" >> "$cmds" 2>/dev/null || true
    if [ -f "$trace" ] && grep -qF '"src":"status"' "$trace" 2>/dev/null; then
      echo "probe live (answering status)." >&2
      return 0
    fi
    sleep 1
  done
  echo "timed out after ${timeout}s — probe never answered." >&2
  return 1
}

# --- readiness ---------------------------------------------------------------

# gs_wait_ready [timeout=180] — block until the probe reports inWorld:true.
# Exit 0 = in-world, 1 = timed out, 2 = session died. Ported from headless/ready.sh:
# there is no foreground game PID and "at the main menu" is not ready, so the only
# reliable signal is SkytestProbe's status.world.inWorld (finding #11).
gs_wait_ready() {
  local timeout="${1:-180}"
  local skytest_dir; skytest_dir="$(_skytest_io_dir)"
  local cmds="$skytest_dir/commands.jsonl" trace="$skytest_dir/trace.jsonl"

  echo "waiting for in-world (timeout ${timeout}s)…" >&2
  local i=0 last_state="" state line inworld is3d main load
  local deadline=$((SECONDS + timeout))
  while [ "$SECONDS" -lt "$deadline" ]; do
    if gs_session_dead; then
      echo "session died (gamescope pid not alive). relaunch: skytest test <mod>" >&2
      return 2
    fi
    # Ask the probe for fresh world state (no-op until the trace writer exists).
    if [ -d "$skytest_dir" ]; then
      i=$((i + 1))
      printf '{"id":"ready-%d","cmd":"status"}\n' "$i" >> "$cmds" 2>/dev/null || true
    fi
    state="booting"   # default until the probe is loaded and answering
    if [ -f "$trace" ]; then
      line="$(grep -F '"src":"status"' "$trace" 2>/dev/null | tail -1 || true)"
      if [ -n "$line" ]; then
        read -r inworld is3d main load < <(
          printf '%s' "$line" | jq -r '.world | "\(.inWorld) \(.is3DLoaded) \(.mainMenu) \(.loadingMenu)"' 2>/dev/null
        ) || true
        if   [ "${inworld:-}" = "true" ]; then state="in-world"
        elif [ "${main:-}"    = "true" ]; then state="main-menu"
        elif [ "${load:-}"    = "true" ]; then state="loading"
        elif [ "${is3d:-}"    = "true" ]; then state="in-world"   # 3D up, menus closed
        else                                   state="loaded (no 3D yet)"
        fi
      fi
    fi
    [ "$state" != "$last_state" ] && { echo "  -> $state" >&2; last_state="$state"; }
    if [ "$state" = "in-world" ]; then
      echo "ready: in-world (interactive)" >&2
      return 0
    fi
    sleep 1
  done
  echo "timed out after ${timeout}s (last state: ${last_state:-booting})." >&2
  return 1
}

# --- screenshot --------------------------------------------------------------

# gs_shot [out=/tmp/sky-shot.png] [cropWxH+X+Y] [scaleWxH] — SIGUSR2 the compositor,
# convert its newest AVIF dump to PNG. X11 grabbers see only black (Vulkan/DXVK), so
# gamescope's own SIGUSR2 dump is the only capture path.
gs_shot() {
  local out="${1:-/tmp/sky-shot.png}" crop="${2:-}" scale="${3:-}"
  local gs=""
  if gs_session_alive; then read -r gs < "$GS_PIDFILE"; fi
  [ -n "$gs" ] || gs="$(pgrep -f 'gamescope --backend' | head -1 || true)"   # fallback: no/old pidfile
  [ -n "$gs" ] || die "no gamescope test session running" "skytest test <mod>"

  kill -USR2 "$gs"
  sleep 1.5
  local avif
  # shellcheck disable=SC2012  # gamescope names these files itself (no spaces); -t = newest by mtime
  avif="$(ls -t /tmp/gamescope_*.avif 2>/dev/null | head -1 || true)"
  [ -n "$avif" ] || die "no gamescope screenshot appeared in /tmp (is the session up?)"

  local args=("$avif")
  [ -n "$crop" ]  && args+=(-crop "$crop" +repage)
  [ -n "$scale" ] && args+=(-scale "$scale")
  args+=("$out")
  magick "${args[@]}"
  echo "$out"
}

# --- input (libei) -----------------------------------------------------------

# Key name -> Linux evdev keycode (KEY_* from input-event-codes.h).
gs_keycode() {
  case "$1" in
    esc|escape) echo 1 ;;  enter|return) echo 28 ;;  tab) echo 15 ;;  space) echo 57 ;;
    backspace) echo 14 ;;  up) echo 103 ;;  down) echo 108 ;;  left) echo 105 ;;  right) echo 106 ;;
    pageup) echo 104 ;;    pagedown) echo 109 ;;  home) echo 102 ;;  end) echo 107 ;;
    tilde|console|grave) echo 41 ;;  # ` — opens the dev console in-game
    q) echo 16 ;;  # default Favorites-menu key
    e) echo 18 ;;  r) echo 19 ;;  m) echo 50 ;;  w) echo 17 ;;  a) echo 30 ;;  s) echo 31 ;;  d) echo 32 ;;
    f) echo 33 ;;  y) echo 21 ;;  n) echo 49 ;;  j) echo 36 ;;  i) echo 23 ;;
    *) echo "unknown key: $1" >&2; return 2 ;;
  esac
}

# gs_drive <tap|seq|key|click|abs|rel|raw> … — inject input via the eidriver client
# against gamescope's gamescope-0-ei socket. Isolated to the session — never touches
# your seat. Pointer rides RELATIVE motion (abs/click are home+relative, 1:1); raw
# libei absolute is inert to Skyrim by design (finding #9).
gs_drive() {
  [ -x "$EIDRIVER" ] || die "eidriver not built" "skytest/eidriver/build.sh"
  [ -S "$GS_EIS_SOCK" ] || die "no EIS socket at $GS_EIS_SOCK (is a test session up?)" "skytest test <mod>"
  local cmd="${1:-}"; shift || true
  # tap/seq/key resolve key NAMES to codes; gs_keycode returns 2 + a stderr breadcrumb on an
  # unknown name. Capture its rc (`|| return 2`) instead of inlining the substitution: an
  # unresolved name otherwise yields an empty arg -> eidriver taps keycode 0 (a SILENT no-op
  # that still exits 0), so a typo'd key in a .steps would report `ok` and a downstream gate
  # would be what fails. seq validates EVERY key before driving any, so a bad key never leaves
  # a sequence half-applied. (hold, in lib/replay.sh, already guards the same way.)
  local kc
  case "$cmd" in
    tap)   kc="$(gs_keycode "$1")" || return 2; "$EIDRIVER" "$GS_EIS_SOCK" tap "$kc" ;;
    seq)   # Inter-key gap: default 300 ms (120 ms was too tight for menu→Continue — the boot
           # double-`e` landed wrong). Override per call with `seq --gap MS …` or globally via
           # SKYTEST_SEQ_GAP_MS. No replay timings depend on the old default.
           local gap="${SKYTEST_SEQ_GAP_MS:-300}"
           [ "${1:-}" = "--gap" ] && { gap="${2:-300}"; shift 2; }
           local args=() k; for k in "$@"; do kc="$(gs_keycode "$k")" || return 2; args+=(tap "$kc" sleep "$gap"); done
           "$EIDRIVER" "$GS_EIS_SOCK" "${args[@]}" ;;
    key)   kc="$(gs_keycode "$1")" || return 2; "$EIDRIVER" "$GS_EIS_SOCK" key "$kc" "$2" ;;
    btn)   "$EIDRIVER" "$GS_EIS_SOCK" btn "$1" "$2" ;;   # mouse button hold/release (272=L 273=R), $2: 1=down 0=up
    click) if [ "$#" -ge 2 ]; then "$EIDRIVER" "$GS_EIS_SOCK" clickat "$1" "$2"
           else "$EIDRIVER" "$GS_EIS_SOCK" click; fi ;;
    rel)   "$EIDRIVER" "$GS_EIS_SOCK" rel "$1" "$2" ;;
    abs|moveto|mv) "$EIDRIVER" "$GS_EIS_SOCK" moveto "$1" "$2" ;;
    raw)   "$EIDRIVER" "$GS_EIS_SOCK" "$@" ;;
    *) usage_err "drive: usage: skytest drive {tap|seq [--gap MS]|key|btn|click [x y]|abs x y|rel dx dy|raw} …" "skytest drive seq down down enter" ;;
  esac
}

# --- teardown ----------------------------------------------------------------

# gs_stop — kill the gamescope SESSION (not the game). Does NOT restore the profile;
# the caller (cmd_stop) does. Returns 1 if something survived the kill.
gs_stop() {
  local mode="" pid=""
  if gs_session_alive; then
    read -r pid < "$GS_PIDFILE"
    # gamescope is the session leader; kill its process group, then sweep any
    # straggler still in its session (proton spawns its own sub-groups).
    kill -9 -- "-$pid" 2>/dev/null || true
    local p
    for p in $(ps -e -o pid=,sid= | awk -v s="$pid" '$2==s {print $1}'); do
      kill -9 "$p" 2>/dev/null || true
    done
    mode="pid $pid"
  else
    # No usable pidfile (manual / pre-pidfile launch): broad pattern-kill.
    # WARNING: the SkyrimSE.exe line also kills a game you're playing in this prefix —
    # which is why the targeted path above is preferred (finding #12).
    pkill -9 -f "gamescope --backend"            2>/dev/null || true
    pkill -9 -f "proton run.*skse64_loader"      2>/dev/null || true
    pkill -9 -f "SkyrimSE.exe"                   2>/dev/null || true
    sleep 1
    pkill -9 -f "gamescopereaper.*skse64_loader" 2>/dev/null || true
    pkill -9 wineserver                          2>/dev/null || true
    mode="pattern"
  fi
  rm -f "$GS_PIDFILE"
  sleep 1

  # Liveness re-check without a self-matching cmdline grep (finding #11): targeted
  # mode must confirm BOTH the gamescope leader pid AND the actual game child are gone.
  # The leader (gamescope) can die while a wine/SkyrimSE.exe child lives on in the same
  # session — and cmd_stop swaps Data -> full on a clean return, so a surviving child
  # would then read SKSE plugins from the WRONG profile. So check the whole session:
  # any process still carrying SID == the session pid means a straggler (the game child)
  # survived. (kill -9 -- -PGID + the per-pid sweep above should have cleared it; if not,
  # report it — Data must NOT be swapped.) Pattern mode pgreps but drops our shell+parent.
  local alive=""
  if [ -n "$pid" ]; then
    kill -0 "$pid" 2>/dev/null && alive="$pid"
    if [ -z "$alive" ]; then
      alive="$(ps -e -o pid=,sid= | awk -v s="$pid" '$2==s {print $1; exit}')"
    fi
  else
    alive="$(pgrep -f 'gamescope --backend' | grep -vw "$$" | grep -vw "${PPID:-0}" | head -1 || true)"
  fi
  if [ -n "$alive" ]; then
    printf 'skytest: WARNING: gamescope test session still alive after stop (%s, straggler pid %s).\n' "$mode" "$alive" >&2
    return 1
  fi
  say "test session stopped ($mode)."
}
