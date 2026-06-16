# shellcheck shell=bash
# skytest replay — .steps parser + step interpreter + probe-gated waits.
# Sourced by `skytest` (never executed on its own), exactly like lib/gamescope.sh.
#
# A `.steps` file is a CC-authored, line-based test-setup script: console world-
# staging + real discrete input, with sync gates that poll SkytestProbe instead of
# sleeping. `skytest replay <mod> <script>` boots the normal `test` session, runs the
# script to snap the world to a target state, then leaves it detached for live probing.
# Design: docs/plans/skytest-replay-design.md.
#
# Three concerns, in order:
#   replay_parse        text -> normalized `STEP <verb> <k=v>…` lines (pure; Task 1)
#   replay_wait_gate    probe-gated `until:<COND>` waits + the gate table (Task 2)
#   replay_run          execute the parsed steps (console + input + gates) (Task 3)
#
# Depends on the parent `skytest` for: say/die/usage_err, PREFIX, and the gamescope
# helpers (gs_drive, gs_session_dead). Its functions only run after those exist.

# --- parser -------------------------------------------------------------------

# replay_parse <file|-> — read a .steps file (or `-` for stdin) and emit one
# normalized line per step to stdout: `STEP <verb> <k=v>…`. Blank lines and `#`
# comments are dropped (but still counted, so error line numbers match the file).
# An unknown verb, or a step missing a required argument (tap/key need a key, hold needs
# <target> <gate>, wait needs a gate), prints `replay: line N: …` to stderr and makes the
# whole parse exit non-zero — a script lint caught by `--dry-run`. Pure text, no I/O beyond the read.
replay_parse() {
  local src="${1:?replay_parse: file ('-' for stdin) required}"
  if [ "$src" = "-" ]; then
    _replay_parse_stream
  else
    [ -f "$src" ] || { printf 'replay: file not found: %s\n' "$src" >&2; return 2; }
    _replay_parse_stream < "$src"
  fi
}

# Internal: parse lines from stdin. Kept separate so replay_parse can feed it either a
# file redirect or the inherited stdin without duplicating the loop.
_replay_parse_stream() {
  local line trimmed verb rest lineno=0 rc=0
  local target gate key keys name
  while IFS= read -r line || [ -n "$line" ]; do
    lineno=$((lineno + 1))
    line="${line%$'\r'}"                          # tolerate CRLF
    trimmed="${line#"${line%%[![:space:]]*}"}"    # left-trim
    [ -z "$trimmed" ] && continue                 # blank
    case "$trimmed" in '#'*) continue ;; esac     # comment
    verb="${trimmed%%[[:space:]]*}"               # first token
    rest="${trimmed#"$verb"}"
    rest="${rest#"${rest%%[![:space:]]*}"}"       # left-trim the remainder
    case "$verb" in
      exec)
        # the ENTIRE rest of the line, verbatim — console commands contain spaces.
        printf 'STEP exec line=%s\n' "$rest"
        ;;
      tap)
        read -r key _ <<<"$rest"                  # one key (first token)
        if [ -z "$key" ]; then
          printf "replay: line %d: 'tap' needs a key\n" "$lineno" >&2; rc=2
        else
          printf 'STEP tap key=%s\n' "$key"
        fi
        ;;
      key)
        # a sequence of taps -> comma list (subshell contains the IFS change).
        keys="$(IFS=' '; read -ra _k <<<"$rest"; IFS=','; printf '%s' "${_k[*]}")"
        if [ -z "$keys" ]; then
          printf "replay: line %d: 'key' needs at least one key\n" "$lineno" >&2; rc=2
        else
          printf 'STEP key keys=%s\n' "$keys"
        fi
        ;;
      hold)
        # `hold <TARGET> <GATE>` — GATE is one token (a duration or until:<COND>),
        # NOT another key. read peels target then gate; any trailing junk is ignored.
        read -r target gate _ <<<"$rest"
        if [ -z "$target" ] || [ -z "$gate" ]; then
          printf "replay: line %d: 'hold' needs <LMB|RMB|key> <dur|until:COND>\n" "$lineno" >&2; rc=2
        else
          printf 'STEP hold target=%s gate=%s\n' "$target" "$gate"
        fi
        ;;
      wait)
        read -r gate _ <<<"$rest"                 # one gate token
        if [ -z "$gate" ]; then
          printf "replay: line %d: 'wait' needs <dur|until:COND>\n" "$lineno" >&2; rc=2
        else
          printf 'STEP wait gate=%s\n' "$gate"
        fi
        ;;
      shot)
        read -r name _ <<<"$rest"                 # optional name (empty = default path)
        printf 'STEP shot name=%s\n' "$name"
        ;;
      *)
        printf "replay: line %d: unknown step '%s'\n" "$lineno" "$verb" >&2
        rc=2
        ;;
    esac
  done
  return "$rc"
}

# --- gates --------------------------------------------------------------------

# The gate table, data-driven: each `until:<COND>` maps to a probe query + the trace
# `src` it produces + a jq predicate that is true once the condition holds. Adding a
# gate is ONE row here plus ONE probe-query handler in SkytestProbe — nothing else.
#
#   cond          probe cmd (sans id)                          src      predicate (jq, true = satisfied)
#   inworld       {"cmd":"status"}                             status   .world.inWorld == true
#   menu:<NAME>   {"cmd":"is-menu-open","menu":"<NAME>"}        menu     .menu=="<NAME>" and .open==true
#
# resolve_gate <cond> <cmd-var> <src-var> <pred-var> — fill the three named vars (via
# nameref) with this gate's pieces. Unknown cond -> message on stderr + return 2 (so
# replay_wait_gate fails fast and loud rather than polling a condition nobody answers).
resolve_gate() {
  local __cond="$1"
  local -n __cmd_ref="${2:?resolve_gate: cmd var}" \
           __src_ref="${3:?resolve_gate: src var}" \
           __pred_ref="${4:?resolve_gate: pred var}"
  case "$__cond" in
    inworld)
      __cmd_ref='{"cmd":"status"}'
      __src_ref='status'
      __pred_ref='.world.inWorld == true'
      ;;
    menu:*)
      local __n="${__cond#menu:}"
      [ -n "$__n" ] || { printf "replay: gate 'menu:<NAME>' needs a menu name\n" >&2; return 2; }
      __cmd_ref="{\"cmd\":\"is-menu-open\",\"menu\":\"$__n\"}"
      __src_ref='menu'
      __pred_ref=".menu==\"$__n\" and .open==true"
      ;;
    *)
      printf "replay: unknown gate condition '%s'\n" "$__cond" >&2
      return 2
      ;;
  esac
}

# replay_wait_gate <cond> [timeout=180] — poll SkytestProbe until <cond> holds.
# Exit 0 = satisfied, 1 = timed out, 2 = session died (or unknown cond, surfaced by
# resolve_gate). A clone of gs_wait_ready's loop, generalized over the gate table:
# each iteration fast-fails on a dead session, re-issues the gate's probe query with a
# fresh id, tails the matching trace `src` line, and evaluates the jq predicate. Never
# polls without a deadline (the design's hard rule — a missed gate aborts, never hangs).
replay_wait_gate() {
  local cond="${1:?replay_wait_gate: condition required}" timeout="${2:-180}"
  local cmd src pred
  resolve_gate "$cond" cmd src pred || return 2   # unknown/empty cond already messaged

  local trace; trace="$(_skytest_io_dir)/trace.jsonl"
  local deadline=$((SECONDS + timeout)) i=0 line ok
  printf 'replay: waiting for gate %s (timeout %ss)…\n' "$cond" "$timeout" >&2
  while [ "$SECONDS" -lt "$deadline" ]; do
    if gs_session_dead; then
      printf 'replay: session died while waiting for gate %s\n' "$cond" >&2
      return 2
    fi
    i=$((i + 1))
    # Splice a unique id into the resolved query: {"cmd":..} -> {"id":"gate-N","cmd":..}
    # (string-built JSON, same idiom as gs_wait_ready's printf'd status command).
    _probe_send "{\"id\":\"gate-$i\",${cmd#\{}"
    if [ -f "$trace" ]; then
      line="$(grep -F "\"src\":\"$src\"" "$trace" 2>/dev/null | tail -1 || true)"
      if [ -n "$line" ]; then
        ok="$(printf '%s' "$line" | jq -r "if ($pred) then 1 else 0 end" 2>/dev/null || echo 0)"
        if [ "$ok" = 1 ]; then
          printf 'replay: gate %s satisfied\n' "$cond" >&2
          return 0
        fi
      fi
    fi
    sleep 1
  done
  printf 'replay: gate %s timed out after %ss\n' "$cond" "$timeout" >&2
  return 1
}

# --- interpreter --------------------------------------------------------------

# Minimal JSON-string escaping for an embedded console line (backslash + quote). Console
# commands rarely carry either, but the line goes into a JSON object the probe parses.
_json_escape() { local s="$1"; s="${s//\\/\\\\}"; s="${s//\"/\\\"}"; printf '%s' "$s"; }

# Sleep a `<N>ms` / `<N>s` duration (the only non-gate wait the design permits). awk does
# the ms→s divide since bash has no float math; sleep takes the fractional seconds.
_replay_sleep_dur() {
  local d="$1" secs
  case "$d" in
    *ms) secs="$(awk "BEGIN{printf \"%.3f\", ${d%ms}/1000}")" ;;
    *s)  secs="${d%s}" ;;
    *)   printf "replay: bad duration '%s' (use Nms or Ns)\n" "$d" >&2; return 2 ;;
  esac
  sleep "$secs"
}

# Wait for an exec command's Ack (matched on `.ack`, NOT a trace `src` — that's what
# tells an exec-ack apart from a gate record). 0 = ok:true, 1 = ack-timeout, 2 = ok:false
# or session death (with the probe's error text in $_replay_ack_err). Short bound (10s):
# an exec spawn/grant completes within a couple of probe poll ticks or never.
_replay_ack_err=""
_replay_wait_ack() {
  local id="$1" timeout="${2:-10}"
  local trace; trace="$(_skytest_io_dir)/trace.jsonl"
  local deadline=$((SECONDS + timeout)) line ok
  _replay_ack_err=""
  while [ "$SECONDS" -lt "$deadline" ]; do
    if gs_session_dead; then _replay_ack_err="session died"; return 2; fi
    if [ -f "$trace" ]; then
      line="$(grep -F "\"ack\":\"$id\"" "$trace" 2>/dev/null | tail -1 || true)"
      if [ -n "$line" ]; then
        ok="$(printf '%s' "$line" | jq -r '.ok' 2>/dev/null || echo null)"
        [ "$ok" = "true" ] && return 0
        if [ "$ok" = "false" ]; then
          _replay_ack_err="$(printf '%s' "$line" | jq -r '.err // "exec failed"' 2>/dev/null || echo 'exec failed')"
          return 2
        fi
      fi
    fi
    sleep 0.5
  done
  _replay_ack_err="ack timeout (${timeout}s)"
  return 1
}

# exec one console line through the probe and BLOCK on its ack, so a spawn/grant has
# actually applied before a later gate reads world state. Returns _replay_wait_ack's code.
_replay_step_exec() {
  local id="$1" line="$2" esc
  esc="$(_json_escape "$line")"
  _probe_send "{\"id\":\"exec-$id\",\"cmd\":\"exec\",\"line\":\"$esc\"}"
  _replay_wait_ack "exec-$id"
}

# hold <target> <gate>: press, gate, release. The release ALWAYS runs — even when the
# gate times out — so a failed gate never leaves a button/key stuck down. Returns the
# gate's exit code (0 satisfied, non-zero -> caller aborts after the release).
_replay_step_hold() {
  local target="$1" gate="$2" gate_timeout="$3"
  local press release rc=0
  case "$target" in
    LMB) press=(btn 272 1); release=(btn 272 0) ;;
    RMB) press=(btn 273 1); release=(btn 273 0) ;;
    # A keyboard key: pass the NAME to `gs_drive key`, which resolves+validates it (same
    # as tap/seq). Pre-resolving to a keycode HERE double-resolves — gs_drive then re-runs
    # gs_keycode on the numeric code, which is not a known key name ("unknown key: 18"),
    # so the press silently no-ops and the held key never lands. Names only.
    *)   press=(key "$target" 1); release=(key "$target" 0) ;;
  esac
  # Abort BEFORE the gate if the press itself fails (bad key name / dead EIS socket): a
  # gate waiting on a key that never went down would just burn the whole timeout. Nothing
  # is held on a failed press, so no release is owed. rc unchecked on press would swallow
  # this — the exact footgun gs_drive's gs_keycode guard exists to prevent (tap/seq).
  gs_drive "${press[@]}" || return 2
  case "$gate" in
    until:*) replay_wait_gate "${gate#until:}" "$gate_timeout" || rc=$? ;;
    *)       _replay_sleep_dur "$gate" || rc=$? ;;
  esac
  gs_drive "${release[@]}"   # best-effort; the key IS down, so always attempt the release
  return "$rc"
}

# replay_run <script_file|-> [gate_timeout=180] — parse the script, then run each step in
# order. Aborts on the FIRST failure with `replay: step N (<verb>) failed: <reason>` and a
# non-zero return — a wrong setup state must never bleed into the live mod test. Held
# input is always released before an abort (see _replay_step_hold).
replay_run() {
  local script="${1:?replay_run: script required}" gate_timeout="${2:-180}"
  local plan
  plan="$(replay_parse "$script")" || { printf 'replay: parse failed — not running %s\n' "$script" >&2; return 2; }

  local stepno=0 execid=0 stepline body verb args rc reason
  while IFS= read -r stepline; do
    case "$stepline" in 'STEP '*) ;; *) continue ;; esac
    stepno=$((stepno + 1))
    body="${stepline#STEP }"
    verb="${body%%[[:space:]]*}"
    args="${body#"$verb"}"; args="${args#"${args%%[![:space:]]*}"}"
    rc=0 reason=""
    case "$verb" in
      exec)
        execid=$((execid + 1))
        _replay_step_exec "$execid" "${args#line=}" || { rc=$?; reason="${_replay_ack_err:-exec failed}"; }
        ;;
      tap)
        gs_drive tap "${args#key=}" || { rc=$?; reason="input failed"; }
        ;;
      key)
        local keys="${args#keys=}"
        # shellcheck disable=SC2086  # intentional split: seq takes each key as its own arg
        gs_drive seq ${keys//,/ } || { rc=$?; reason="input failed"; }
        ;;
      hold)
        local f1 f2 ht hg
        read -r f1 f2 _ <<<"$args"
        ht="${f1#target=}"; hg="${f2#gate=}"
        _replay_step_hold "$ht" "$hg" "$gate_timeout" || { rc=$?; reason="gate '$hg' not reached (input released)"; }
        ;;
      wait)
        local wg="${args#gate=}"
        case "$wg" in
          until:*) replay_wait_gate "${wg#until:}" "$gate_timeout" || { rc=$?; reason="gate '$wg' not reached"; } ;;
          *)       _replay_sleep_dur "$wg" || { rc=$?; reason="bad duration '$wg'"; } ;;
        esac
        ;;
      shot)
        # Capture gs_shot's stdout (the written path) and report it through replay's own
        # stderr log, so a `shot` step doesn't splice a bare path into stdout mid-run.
        local sn="${args#name=}" out=""
        if [ -n "$sn" ]; then out="$(gs_shot "$sn")"; else out="$(gs_shot)"; fi || { rc=$?; reason="screenshot failed"; }
        [ "$rc" = 0 ] && printf 'replay: shot -> %s\n' "$out" >&2
        ;;
      *)
        rc=2; reason="unhandled verb (parser/interpreter mismatch)"
        ;;
    esac
    if [ "$rc" -ne 0 ]; then
      printf 'replay: step %d (%s) failed: %s\n' "$stepno" "$verb" "$reason" >&2
      return "$rc"
    fi
    printf 'replay: step %d ok: %s\n' "$stepno" "$verb" >&2
  done <<<"$plan"
  return 0
}
