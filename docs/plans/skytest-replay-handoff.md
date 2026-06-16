# skytest replay — session handoff (2026-06-16)

Mid-feature handoff. The replay **machinery is built, committed, and verified live**; one
**design-level blocker** (console `exec`) and a **demo limitation** (qasmoke has no map)
remain open and need a decision before the feature is "done" for its headline use case.

Spec: `skytest-replay-design.md`. Plan: `skytest-replay-plan.md`.

---

## What shipped (Tasks 1–5 + the verb) — committed to master

All verified against a running **headless** session (one full `skytest replay` run, plus
isolated probes). Commits (local-only, not pushed — see "git" below):

- `feat(skytest): .steps parser for replay (replay_parse)` — parser + `lib/replay.test.sh`
- `feat(skytestprobe): is-menu-open query — first accreted replay gate` (Task 5)
- `feat(skytest): probe-gated replay waits + data-driven gate table` (Task 2)
- `feat(skytest): replay step interpreter + button-hold input wiring` (Task 3)
- `feat(skytest): replay verb — boot reuse + arg/script resolution` (Task 4)
- `fix(skytest): absolutize replay script path before boot's cd`

**Verified working end-to-end** (`skytest replay <mod> skytest/examples/format-demo.steps --headless`):
parse → `_boot_test_session` (boots identically to `test`, reached in-world) → `until:inworld`
gate → `tap tilde` (libei input lands) → `until:menu:Console` gate (the Task 5 `is-menu-open`
probe; confirmed `Console open:true`) → `shot` (wrote the frame, console-open warning visible)
→ exit 0, session left live. `--dry-run`, bare/path/`-`-stdin script resolution, and
"abort leaves the session live" all confirmed. `skytest test` still boots to in-world post
`_boot_test_session` refactor (no regression).

`SkytestProbe.dll` rebuilt with `is-menu-open` (clean PE32+; warnings are pre-existing
CommonLib header noise).

---

## OPEN BLOCKER 1 — console `exec` does not work in the test session

`exec <console>` faults: `exec: CompileAndRun faulted (console subsystem unavailable?)`.
Confirmed in **both headless AND visible** gamescope, **fully in-world** (`status` acks ok,
`inWorld:true`). Opening the console first (tilde → `Console open:true`) does **not** help —
the console *UI* opens, but the script-*compiler* subsystem is absent.

This is a **documented, pre-existing** engine limitation, not a replay bug:
- `skytest/docs/headless-findings.md:320` — "Set up game state with direct-call probe
  commands, not console `exec`. `exec`/CompileAndRun AVs in a console-less test session."
- `docs/plans/skytest-probe-design.md:176` — "in a console-less environment (e.g. headless
  gamescope) the compiler subsystem is absent and `CompileAndRun` AVs even in-world."
- `mods/SkytestProbe/src/engine.cpp` `SafeCompileAndRun` already SEH-guards it; `GiveSpell`/
  `SetAV` exist precisely **because** console exec can't be used for cast setup.

**Why it bites replay specifically:** the design's world-staging premise is `exec coc` /
`exec player.placeatme` / `exec player.addspell` (the GhostAllies summons setup). None of
those run via console here. Earlier mods never hit this because they staged via direct-call
probe commands, not console.

**Recommended fix (per the repo's own pattern): direct-call staging, added per-need.**
Add SkytestProbe commands that do the staging via engine calls on the main thread, mirroring
`GiveSpell`/`SetAV`. Needed for the GhostAllies test:
- `addspell` → already have `engine::GiveSpell` (wire a command to it).
- `placeatme` → `RE::TESDataHandler`/`PlaceObjectAtMe` or `TESObjectREFR::PlaceObjectAtMe`.
- `coc`/cell-travel → `PlayerCharacter::MoveTo` a cell's COC marker, or `CenterOnCell`-style.
Then `replay`'s `exec` either routes recognized verbs to these, OR add a new step type
(e.g. `stage <cmd> …`) that maps to direct-call commands. Build these **when the GhostAllies
session needs them** (YAGNI, same as gates) — do NOT speculatively build the whole console
surface. Until then, replay does input + gates + shot, not console staging.

(Alternative, lower-confidence: make `CompileAndRun` work in-session by initializing the
console compiler subsystem. The probe team apparently couldn't, hence the SEH workaround.
Not recommended without new information.)

---

## OPEN BLOCKER 2 — the demo menu (qasmoke has no map)

`SkytestBase` autoloads into **qasmoke** (interior test cell, no world map). So:
- `tap m` there renders a **black** map and the `MapMenu` gate was unreliable (`open:false`).
- `tap q` (Favorites) also probed `open:false`.
- Only **Console** (tilde) opened reliably (`open:true`) — that's what the committed
  `format-demo.steps` uses, as a mechanism proof.

Mase wanted a **Map** demo (genuinely useful for OneClickTravel/OneClickMap). A meaningful
map needs an **exterior** location, which needs either:
- (a) an **exterior `SkytestBase`** save (re-run `skytest setup-save`, `coc`/walk outside a
  city, `save SkytestBase`) — but it changes shared test infra (every test autoloads it); or
- (b) direct-call cell-travel (Blocker 1's `coc` equivalent) to move to an exterior at replay
  time.

**Decision pending** (Mase, end of this session): ship without console exec + add direct-call
staging per-need? build direct-call `coc`/`placeatme` now? and which demo path (Console
mechanism-proof, exterior save, or drop the menu step)? Mase's lean was unstated — he chose
to end the session and continue debugging later.

---

## Smaller finding — `commands.jsonl` is never truncated

Every new test session spawns a fresh probe that re-reads `…/SKSE/skytest/commands.jsonl`
from offset 0, so it **re-runs the entire command history** (was 571 lines after a few test
runs). Harmless-ish (old `exec`s just re-fault, gates match on `src`/`.ack` with per-run ids)
but it lags probe responses and pollutes traces. **Fix:** truncate `commands.jsonl` at session
start (e.g. `: > "$(_skytest_io_dir)/commands.jsonl"` in `_boot_test_session` before
`gs_launch`, after `mkdir -p` the dir). In `docs/ideas.md`. Not done this session.

---

## Gotchas for the next session

- **`_boot_test_session` cd's to `$SKYDIR`** (via `gs_launch`→`skse_env_export`) before
  `replay_run` reads the script — that's why script paths are realpath'd up front in
  `cmd_replay`. Any new path the interpreter reads at run time must likewise be absolute.
- **`set -e` + non-zero return:** `_boot_test_session` returns 1 (timed out) / 10 (NO_LAUNCH);
  callers use `|| brc=$?` so set -e doesn't exit on it. Keep that idiom for any new such call.
- **`lib/replay.test.sh`** is the no-game unit harness (parser + gate resolver + interpreter
  stream via stubbed `gs_drive`/`_probe_send`/`replay_wait_gate`/`_replay_wait_ack`). Run
  `bash skytest/lib/replay.test.sh` — 25 assertions, all green. Extend it when you add steps.
- **Adding a gate = one row in `resolve_gate` (lib/replay.sh) + one direct-call probe handler**
  (the `is-menu-open` commit is the template). `charged`/`actorcount` from the design are NOT
  built — add them when a script first needs them.
- **`is-menu-open` menu names:** `Console`, `MapMenu`, `FavoritesMenu`, etc. (CommonLib
  `RE::*::MENU_NAME`). Verified the probe reports them correctly.

---

## Next steps (suggested order)

1. Decide Blocker 1 (direct-call staging now vs per-need) and Blocker 2 (demo path) with Mase.
2. If per-need: leave as-is; the GhostAllies session adds `coc`/`placeatme`/`addspell` direct
   calls + (maybe) an exterior save + swaps the demo to Map.
3. Truncate `commands.jsonl` at boot (small, do anytime).
4. The discoverability docs (README replay section, CLAUDE.md steer, `verb_help replay`,
   `cmd_test` trailing line) were written this session and reflect the exec caveat — revisit
   their wording if Blocker 1 changes the story.

## git

Commits above are on **local master, not pushed** — and **interleaved with another session's
commits** (it was committing to master concurrently: `nexus` CLI, `dbvo` docs). Do NOT
`git push` blind; that would push the other session's possibly-unfinished work too. Coordinate
with Mase before pushing. Staging was precise per-file throughout (never `git add -A`).
