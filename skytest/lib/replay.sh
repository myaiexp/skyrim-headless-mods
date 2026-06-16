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
# An unknown verb prints `replay: line N: unknown step '<verb>'` to stderr and makes
# the whole parse exit non-zero (a script lint). Pure text — no I/O beyond the read.
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
  local target gate key name
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
        printf 'STEP tap key=%s\n' "$key"
        ;;
      key)
        # a sequence of taps -> comma list (subshell contains the IFS change).
        printf 'STEP key keys=%s\n' \
          "$(IFS=' '; read -ra _k <<<"$rest"; IFS=','; printf '%s' "${_k[*]}")"
        ;;
      hold)
        # `hold <TARGET> <GATE>` — GATE is one token (a duration or until:<COND>),
        # NOT another key. read peels target then gate; any trailing junk is ignored.
        read -r target gate _ <<<"$rest"
        printf 'STEP hold target=%s gate=%s\n' "$target" "$gate"
        ;;
      wait)
        read -r gate _ <<<"$rest"                 # one gate token
        printf 'STEP wait gate=%s\n' "$gate"
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
