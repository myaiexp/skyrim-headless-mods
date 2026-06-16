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
