# skytest replay — Implementation Plan

**Goal:** Add a `skytest replay <mod> <script>` verb that runs a CC-authored `.steps` file to snap a
headless test session to a target state (console staging + real discrete input, probe-gated), then
leaves it detached for live probing.

**Architecture:** A new sourced lib `skytest/lib/replay.sh` holds the parser, the step interpreter,
and a generic probe-gated `wait` (cloned from `gs_wait_ready`'s commands.jsonl→trace.jsonl poll). The
`replay` verb reuses the existing `cmd_test` boot path (profile build + probe inject + StartOnSave +
`gs_wait_ready`), then runs the interpreter. Sync gates are data-driven: `inworld` reuses the probe's
existing `status`; `menu:<NAME>` is the first **permanent** SkytestProbe query added (the recipe for
all future gates). `charged`/`actorcount` are deferred to the GhostAllies-scripting session per the
design's "gates land when a script first needs them".

**Tech Stack:** Bash (skytest + libs), the existing `eidriver` (libei input), SkytestProbe (SKSE C++
via CommonLibSSE-NG, cross-compiled), `jq` for trace parsing.

Spec: `docs/plans/skytest-replay-design.md`.

---

## File structure

| File | Create/Modify | Responsibility |
| --- | --- | --- |
| `skytest/lib/replay.sh` | **Create** | Parse `.steps`, interpret steps, probe-gated `wait`. One concern: replay. Sourced by `skytest` like `lib/gamescope.sh`. |
| `skytest/skytest` | Modify | Source `replay.sh`; `cmd_replay`; dispatch `replay)`; `verb_help replay`; extract `_boot_test_session`; discoverability line in `cmd_test`. |
| `skytest/lib/gamescope.sh` | Modify | Add `btn` case to `gs_drive` (button hold) + any keycodes scripts need (e.g. `q`=16). |
| `mods/SkytestProbe/src/commands.cpp` | Modify | Add `is-menu-open` command handler (the first accreted gate query). |
| `mods/SkytestProbe/src/engine.{h,cpp}` | Modify | Add `bool IsMenuOpen(const std::string&)`. |
| `skytest/examples/format-demo.steps` | **Create** | A runnable format example + the live-verification fixture (console + inworld + input + menu gate + shot). |
| `skytest/README.md` | Modify | `replay` verb reference + first-vs-subsequent-test guidance. |
| `CLAUDE.md` | Modify | "Testing a mod you built" steer (drive once → author `.steps` → `replay` after). |

**Scope note (YAGNI, per design):** this plan ships the format + interpreter + `inworld` + `menu`
gates. `charged` and `actorcount` are NOT built here — they're added (each as one probe query + one
gate-table row) when the GhostAllies summons script first needs them. Task 5 is the template for that.

---

### Task 1: `.steps` parser + `--dry-run` [Mode: Direct]

**Files:**
- Create: `skytest/lib/replay.sh` (parser portion)

**Contracts:**
- `replay_parse <file>`: reads a `.steps` file (or `-` for stdin), emits one normalized line per
  step to stdout: `STEP <verb> <k=v>…`. Skips blank lines and `#` comments. Unknown verb → non-zero
  exit + `replay: line N: unknown step '<verb>'` on stderr.
- Tokenization rules (from the design's parser note):
  - `exec <rest>` → `STEP exec line=<entire rest of line, verbatim>` (console commands contain spaces).
  - `tap <KEY>` → `STEP tap key=<KEY>`
  - `key <K1> <K2>…` → `STEP key keys=<K1,K2,…>`
  - `hold <TARGET> <GATE>` → `STEP hold target=<TARGET> gate=<GATE>` where `<GATE>` is one token,
    either a duration (`500ms`/`2s`) or `until:<COND>`.
  - `wait <GATE>` → `STEP wait gate=<GATE>`
  - `shot [name]` → `STEP shot name=<name|>` (empty = default path)
- `replay --dry-run <file>` (wired in Task 4) prints the `replay_parse` output and exits 0 without
  launching/injecting — the parser's test surface and a script lint.

**Test Cases** (shell assertions; no game needed — run `bash skytest/lib/replay.sh`-sourced harness
or `skytest replay --dry-run -`):

```
# exec keeps the whole line verbatim (spaces preserved)
in : 'exec player.placeatme 0x1A2B3 1'
out: 'STEP exec line=player.placeatme 0x1A2B3 1'

# hold splits target from gate, does NOT treat the gate as another key
in : 'hold LMB until:charged'
out: 'STEP hold target=LMB gate=until:charged'

# key sequence collapses to a comma list
in : 'key Down Down'
out: 'STEP key keys=Down,Down'

# comments and blank lines are dropped
in : $'# setup\n\nexec coc WhiterunOrigin'
out: 'STEP exec line=coc WhiterunOrigin'

# unknown verb fails with the line number
in : 'frobnicate 3'
exit: non-zero, stderr contains "line 1: unknown step 'frobnicate'"
```

**Constraints:** pure text; no I/O beyond reading the file. Deterministic line numbering for errors.

**Verification:** `skytest replay --dry-run skytest/examples/format-demo.steps` prints the expected
normalized plan; the five cases above pass.

**Commit after passing.**

---

### Task 2: Probe-gated `wait` mechanism + `inworld` gate [Mode: Direct]

**Files:**
- Modify: `skytest/lib/replay.sh` (gate portion)

**Contracts:**
- A data-driven gate table. Adding a gate = one probe-query handler (Task 5) + one row here:
  | cond | probe cmd (commands.jsonl) | trace `src` | predicate (jq, true = satisfied) |
  | --- | --- | --- | --- |
  | `inworld` | `{"cmd":"status"}` | `status` | `.world.inWorld == true` |
  | `menu:<NAME>` | `{"cmd":"is-menu-open","menu":"<NAME>"}` | `menu` | `.menu=="<NAME>" and .open==true` |
- `replay_wait_gate <cond> [timeout=180]`: clone of `gs_wait_ready`'s loop —
  `deadline=$((SECONDS+timeout))`; each iteration: `gs_session_dead` → return 2; append the gate's
  probe cmd with a unique incrementing `id` to `commands.jsonl`; `tail -1` the matching `"src"` line
  in `trace.jsonl`; evaluate the jq predicate; satisfied → return 0; else `sleep 1`. On deadline →
  return 1. Reuses the `$skytest_dir` paths from `gs_wait_ready` (factor a shared
  `_skytest_io_dir`/`_probe_send` helper rather than duplicating the path literal).
- Durations (`<N>ms`/`<N>s`) are handled by the caller (Task 3) as a plain `sleep`, not a gate.

**Test Cases:**
```
# unknown gate condition is a clear error, not a silent hang
replay_wait_gate "bogus" → non-zero, stderr "replay: unknown gate condition 'bogus'"

# gate-table resolution maps cond → (cmd,src,predicate) — assert via a resolver unit fn
resolve_gate "menu:FavoritesMenu" → cmd='{"cmd":"is-menu-open","menu":"FavoritesMenu"}' src=menu
```
(The live poll behavior is covered by the Task 6 functional run; unit-test only the resolver + the
unknown-cond path, which need no game.)

**Constraints:** never `sleep`-poll without a deadline. Session-death fast-fail (return 2) is
mandatory — a crashed boot must not hang replay for the full timeout. Reuse `gs_wait_ready`'s 180s
default; do not invent a new timeout convention.

**Verification:** resolver cases pass; `replay_wait_gate bogus` errors immediately.

**Commit after passing.**

---

### Task 3: Step interpreter + input wiring [Mode: Direct]

**Files:**
- Modify: `skytest/lib/replay.sh` (interpreter portion)
- Modify: `skytest/lib/gamescope.sh` (add `btn` case to `gs_drive`; add keycodes as needed)

**Contracts:**
- `replay_run <script_file> [timeout]`: parse via `replay_parse`, then execute each `STEP` in order;
  on any step failure, **abort** with `replay: step N (<verb>) failed: <reason>` and return non-zero
  (never proceed past a failed gate — a wrong state yields a bogus test).
- Per-step execution:
  - `exec`: append `{"id":"<n>","cmd":"exec","line":"<line>"}` to `commands.jsonl`; **wait for its
    Ack** before the next step, so spawns/grants complete before later gates read state. The Ack is
    `trace::Ack`'s record — `{"ack":"<n>","ok":true|false}` (match on `.ack=="<n>"`, NOT `src`; this
    is what distinguishes exec-acks from gate records). Bounded by a short ack-timeout (default 10s).
    `ok:false` → abort with the probe's error text (the record's message field).
  - `tap`: `gs_drive tap <KEY>`. `key`: `gs_drive seq <K1> <K2>…`.
  - `hold target=<T> gate=<G>`: press → gate → release.
    - press: `T∈{LMB,RMB}` → `gs_drive btn <272|273> 1`; else `gs_drive key <kc> 1`.
    - gate: `until:<COND>` → `replay_wait_gate <COND>`; `<N>ms|<N>s` → `sleep`.
    - release: matching `gs_drive btn … 0` / `gs_drive key <kc> 0`. Release MUST run even if the gate
      times out (don't leave a button stuck) — release, then abort.
  - `wait gate=<G>`: `until:<COND>` → `replay_wait_gate`; duration → `sleep`.
  - `shot name=<n>`: `gs_shot <n|default>`; print the path.
- `gs_drive` gains a `btn) "$EIDRIVER" "$GS_EIS_SOCK" btn "$1" "$2" ;;` case (eidriver already
  implements `btn <code> <0|1>`). Add `LMB`/`RMB`→`272`/`273` mapping in the interpreter (not a
  keycode). Add any missing `gs_keycode` entries a real script needs (e.g. `q) echo 16`).

**Test Cases** (dry-run + a stubbed `gs_drive`/`_probe_send` to assert the emitted command stream,
no game):
```
# hold LMB until:charged emits press, a charged-gate poll, then release — in that order
steps: 'hold LMB until:charged'
emitted: ['drive btn 272 1', 'gate charged', 'drive btn 272 0']

# a failed gate still releases the button before aborting
gate 'charged' times out → emitted ends with 'drive btn 272 0' AND replay_run returns non-zero

# exec waits for its ack before the next step runs
steps: $'exec player.additem 0xf 100\ntap e'
order: exec-ack observed BEFORE 'drive tap e'
```

**Constraints:** stuck-input safety (always release held buttons/keys on abort). `exec` is
synchronous on its Ack; input steps are synchronous on the eidriver returning. No fixed sleeps except
explicit `<duration>` gates.

**Verification:** stubbed-stream cases pass; `gs_drive btn 272 1` injects a left-button press in a
live session (manual one-liner check).

**Commit after passing.**

---

### Task 4: `replay` verb + boot reuse + arg resolution [Mode: Direct]

**Files:**
- Modify: `skytest/skytest` (`_boot_test_session` extraction, `cmd_replay`, dispatch, source line)

**Contracts:**
- Extract the boot body of `cmd_test` (lines ~542–567: `build_test_profile` → `inject_skytestprobe`
  → `backup_plugins` → StartOnSave/isolate_saves → `write_test_plugins` → `point_data test` →
  `gs_launch` → `gs_wait_ready`) into `_boot_test_session <mod> <deps> <backend>`. `cmd_test` calls
  it and keeps its existing trailing "drive it" messages. **Re-run a normal `skytest test` after the
  refactor to confirm no regression** (the boot path is load-bearing and timing-sensitive).
- `cmd_replay`: parse `replay <mod> <script> [--headless] [--with …] [--dry-run]`.
  - `--dry-run`: `replay_parse <resolved-script>`; exit. No mod/profile needed (resolve script only).
  - else: resolve `<mod>` exactly as `cmd_test` (realpath, `-e` check); resolve `<script>` — a bare
    name (no `/`) → `mods/<basename-of-mod-dir>/<script>`; a path with `/` → as-is; `-e` check with a
    clear error. Then `_boot_test_session "$mod" "$deps" "$backend"`; on ready, `replay_run
    "$script"`; on replay failure, leave the session up and tell the user (`skytest stop` to tear
    down) — do NOT auto-stop (mirrors the detached model). On success, print "replayed to target
    state; session live — probe it (skytest shot/drive/exec) or skytest stop".
- Dispatch: add `replay) shift; cmd_replay "$@" ;;`. Source `replay.sh` near the `gamescope.sh`
  source line.

**Test Cases:**
```
# bare script name resolves under the mod dir
replay mods/GhostAllies/build/GhostAllies.dll smoke.steps
  → looks for mods/GhostAllies/smoke.steps   (assert via --dry-run error path / echo)

# explicit path is taken as-is
replay <mod> skytest/examples/format-demo.steps  → uses that path

# missing script → clean usage error, no profile mutation
replay <mod> nope.steps → exit 2, "script not found", Data symlink unchanged
```

**Constraints:** `cmd_replay` must honor the same guards as `cmd_test`
(`guard_no_live_session`/`guard_no_running_game`/`skse_launch_ready`) via the shared boot helper.
Never `git add`-style broad profile writes before the guards pass.

**Verification:** `skytest test <mod>` still boots normally post-refactor; `--dry-run` and the
resolution cases behave as specified.

**Commit after passing.**

---

### Task 5: First accreted probe gate — `is-menu-open` [Mode: Delegated]

**Files:**
- Modify: `mods/SkytestProbe/src/engine.h` (declare `bool IsMenuOpen(const std::string&);`)
- Modify: `mods/SkytestProbe/src/engine.cpp` (define it — `RE::UI::GetSingleton()->IsMenuOpen(name)`,
  null-safe, mirroring the existing `GetWorldState` UI usage at `engine.cpp:245`)
- Modify: `mods/SkytestProbe/src/commands.cpp` (add `if (c == "is-menu-open")` handler)

**Contracts:**
- Command request: `{"id":…,"cmd":"is-menu-open","menu":"<MenuName>"}`. Missing `menu` →
  `trace::Ack(id,false,"is-menu-open: missing menu")`.
- Handler: `EnqueueMain` (UI access is main-thread), write a trace record
  `{"src":"menu","menu":"<MenuName>","open":<bool>}` then `trace::Ack(id,true)`. Follow the exact
  shape/threading of the `status` handler (`commands.cpp:303`) and the trace-writer helpers used by
  `WriteStatus`.
- `engine::IsMenuOpen`: null-safe (UI singleton may be null pre-load) → false when unavailable.
- Rebuild the DLL via `mods/SkytestProbe/build.sh`; skytest reads it from the build output (no
  staging copy — see `docs/ideas.md` 2026-06-12).

**Test Cases:**
- Live (functional, in Task 6's run): with the Favorites menu open, `is-menu-open
  menu=FavoritesMenu` traces `open:true`; closed → `open:false`. (No unit harness for SKSE; verified
  in-engine.)
- Build: `mods/SkytestProbe/build.sh` produces `SkytestProbe.dll` with no new warnings/errors.

**Constraints:** this is a **permanent** SkytestProbe capability (per the CLAUDE.md norm) — not
test-scoped scaffolding. Keep it null-safe and main-thread-correct like the surrounding handlers.

**Verification:** DLL builds; the `menu:FavoritesMenu` gate resolves true/false correctly in the
Task 6 live run.

**Commit after passing.**

---

### Task 6: Discoverability + format example + live verification [Mode: Direct]

**Files:**
- Create: `skytest/examples/format-demo.steps`
- Modify: `skytest/skytest` (`cmd_test` trailing output; `verb_help replay`)
- Modify: `skytest/README.md` (replay verb reference + first-vs-subsequent guidance)
- Modify: `CLAUDE.md` ("Testing a mod you built" section)

**Contracts:**
- `format-demo.steps`: a small standalone-runnable script exercising every wired step on a vanilla+1
  profile — `exec coc <openCell>` → `wait until:inworld` → `exec player.placeatme <vanillaRef> 1` →
  `tap <key-to-open-a-menu>` → `wait until:menu:<MenuName>` → `shot /tmp/replay-demo.png`. Uses only
  vanilla content (so any test profile can run it) and keys present in `gs_keycode`. While authoring,
  confirm the chosen `tap`-key actually opens the chosen menu from the `SkytestBase`/`qasmoke` save
  (an empty Favorites still opens, so the `menu` gate satisfies either way — but verify the keybind
  reaches it in that context before committing the fixture).
- `cmd_test` trailing message gains one line: *drove this by hand? save it as
  `mods/<mod>/<name>.steps` and re-run with `skytest replay <mod> <name>.steps`.*
- `verb_help replay`: heredoc block mirroring the other verbs (synopsis, `--headless`/`--dry-run`,
  two examples).
- `skytest/README.md`: a `replay` subsection — the verb, the `.steps` step table, the gate list
  (`inworld`, `menu:<NAME>`; note `charged`/`actorcount` arrive when a script needs them), and the
  norm "*first* test = drive live + author `.steps`; *after* = `skytest replay`".
- `CLAUDE.md` "Testing a mod you built": add the same first-vs-subsequent steer pointing at the verb.

**Verification (functional — the whole feature, end to end):**
Run `skytest replay <an existing standalone mod, e.g. mods/GhostAllies/build/GhostAllies.dll>
skytest/examples/format-demo.steps --headless`. Expect: boots headless, reaches in-world, spawns the
ref, opens the menu, the `menu` gate satisfies, `shot` writes `/tmp/replay-demo.png`, session left
detached. Confirm the screenshot shows the open menu (`skytest shot` / inspect). Then `skytest stop`
restores `Data -> full`. This exercises parse + exec(+ack) + inworld gate + input + menu gate(+probe
rebuild) + shot + verb + teardown in one run.

**Commit after passing.**

---

## Execution
**Skill:** superpowers:subagent-driven-development
- Mode A (Direct) tasks: Opus implements directly — 1, 2, 3, 4, 6.
- Mode B (Delegated) task: 5 (SkytestProbe C++ / cross-compile) dispatched to a subagent.
- TDD per task; commit after each passes; functional verification (Task 6) is the gate for "done".
