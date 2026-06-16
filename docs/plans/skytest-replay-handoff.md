# skytest replay — session handoff (2026-06-16)

Mid-feature handoff. The replay **machinery is built, committed, and verified live**. The two
items once flagged as "blockers" — console `exec` and the qasmoke map demo — are **resolved**
(2026-06-16), see below. No open decision blocks the feature.

**Resolution in one line:** `exec` is *not a bug to fix* — the harness stages world state through
**direct engine-call probe commands** (`give-spell`/`set-av`, with `coc`/`placeatme` added
per-need) and drives input through the **headless drive layer**. Programmatic `exec` faults in the
test session, but the *interactive* console works there, so it's the call path that faults, not a
missing subsystem — and it's moot, because staging goes through engine calls by design. The map
demo isn't blocked either: it just needs a per-need `coc` direct-call command + an exterior to
travel to. (The forensic detail below is kept as the record of how this was diagnosed.)

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

## RESOLVED 1 — console `exec` is not the staging path (use direct-call)

> **Resolution (2026-06-16):** not a bug — by design. Programmatic `exec` faults in the test
> session, but the *interactive* console works there (Mase hand-typed `coc qasmoke` in the same
> session and it loaded), so the "console subsystem absent / console-less environment" theory in
> the forensic notes below is **wrong** — it's the *programmatic* `CompileAndRun` call path that
> faults, cause unpinned. Moot anyway: stage via direct engine-call probe commands, drive input
> via the drive layer. Confirmed against the AutoCastSpell session (2026-06-14), which hit the
> identical `exec` fault two days before replay existed and resolved it the same way (it built
> `give-spell`/`set-av` after `exec player.addspell` AV'd). Replay didn't break `exec`; it was the
> first feature whose *design* assumed `exec` works. The notes below stay as the diagnosis record.

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

## RESOLVED 2 — Map demo just needs a per-need `coc` + exterior (not blocked)

> **Resolution (2026-06-16):** not a blocker — deferred polish. A real Map demo needs an exterior;
> with the direct-call staging model that's just a per-need `coc`-equivalent command
> (`PlayerCharacter::MoveTo` an exterior cell) built when a OneClickMap/Travel replay actually
> wants it, plus an exterior fixture. Until then the committed `format-demo.steps` proves the
> mechanism via the Console menu. The detail below stays for whoever builds the map demo.

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

**Decision (made 2026-06-16):** ship without console `exec`; staging is direct-call probe commands
added per-need (don't "fix" `exec`, don't pre-build the whole console surface). Demo path: keep the
Console mechanism-proof (`format-demo.steps`); build the exterior Map demo later when a
OneClickMap/Travel replay needs it (per-need `coc` command + exterior fixture).

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

Both "blockers" are decided (see above) — what remains is per-need build-out:

1. When a GhostAllies / OneClickMap replay needs it: add `coc`/`placeatme`/`addspell` direct-call
   probe commands (+ exterior fixture for a Map demo), then route replay staging to them (or add a
   `stage` step). Per-need, not speculative.
2. Truncate `commands.jsonl` at boot (small, do anytime — in `docs/ideas.md`).
3. The exec-caveat wording in the docs (README replay section + caveat box, finding #17,
   probe-design as-built, this handoff) was corrected 2026-06-16 to the direct-call/drive model —
   keep new docs consistent with it.

## git

Commits above are on **local master, not pushed** — and **interleaved with another session's
commits** (it was committing to master concurrently: `nexus` CLI, `dbvo` docs). Do NOT
`git push` blind; that would push the other session's possibly-unfinished work too. Coordinate
with Mase before pushing. Staging was precise per-file throughout (never `git add -A`).
