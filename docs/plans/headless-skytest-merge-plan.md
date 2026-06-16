# headless/ + skytest/ Merge Implementation Plan

**Goal:** Fold the `headless/` display+input subsystem into `skytest` so one tool launches a chosen
profile as either a visible or headless gamescope **test session** (detached, drivable, screenshot-able,
restore-on-`stop`), while `play`/`normal` keep the bare direct fast path.

**Architecture:** A test session always runs under gamescope; `--headless` only swaps the gamescope
`--backend` string (`wayland` visible / `headless` invisible). The headless scripts' logic moves into a
sourced `skytest/lib/gamescope.sh` backend; a shared `launch_skse_core` (env + proton invocation) is
wrapped by gamescope for `test` and run bare for `play`/`normal`. No formal backend registry — `cmd_test`
sources the lib, `cmd_play`/`normal` use the inline direct path.

**Tech Stack:** Bash (the `skytest` CLI + sourced lib), C (`eidriver` libei client, unchanged), gamescope,
libei, ImageMagick, SkytestProbe (already built). No new dependencies.

**Spec:** `docs/plans/headless-skytest-merge-design.md` — read it first.

**Testing reality:** This is a refactor of working code; the game-touching paths can't be unit-tested.
Verification is three tiers: (1) `bash -n` syntax + shellcheck on every script change; (2) dry-run via
the existing `SKYTEST_NO_LAUNCH` env (builds the profile, skips launch) to exercise profile/restore/guard
logic without a game; (3) explicit **manual functional** steps that launch the game once per path. Manual
steps are called out as such — don't claim them passing without running them.

---

### Task 1: Relocate `eidriver` + the findings doc into `skytest/` [Mode: Direct]

Pure file moves, no logic change. Establishes the new home before the lib references it.

**Files:**
- Move: `headless/src/eidriver.c` → `skytest/eidriver/eidriver.c` (`git mv`)
- Move: `headless/src/build.sh` → `skytest/eidriver/build.sh` (`git mv`)
- Move: `headless/docs/findings.md` → `skytest/docs/headless-findings.md` (`git mv`)
- Modify: `.gitignore` (repo root) — add `skytest/eidriver/eidriver` (the compiled binary; was
  `headless/src/eidriver` in `headless/.gitignore`, which retires in Task 5)

**Contracts:**
- `skytest/eidriver/build.sh` is `cd "$(dirname "$(readlink -f "$0")")"`-relative, so it compiles
  `eidriver.c` in place with no path edits — confirm this holds after the move.
- The compiled `eidriver` binary stays gitignored (rebuilt on demand).

**Constraints:**
- `headless/{launch,ready,shot,drive}.sh` still reference `src/eidriver` and become transiently stale
  after this move — acceptable, nothing invokes them during the merge and they are deleted in Task 5.

**Verification:**
- `skytest/eidriver/build.sh` → prints `built: …/skytest/eidriver/eidriver`, exit 0.
- `git status` shows the three files as renames, `headless/src/` empty/gone.

**Commit after passing.**

---

### Task 2: Shared launch core + `lib/gamescope.sh` backend + path standardization [Mode: Delegated]

The plumbing core. Introduce the shared proton launcher, standardize Steam paths, and create the
gamescope backend by absorbing the five headless scripts.

**Files:**
- Modify: `skytest/skytest` — add `launch_skse_core`; standardize paths; `source` the lib; route
  `cmd_play`/`cmd_normal`'s launch through `launch_skse_core` instead of `bash "$LAUNCH"`.
- Create: `skytest/lib/gamescope.sh` — the gamescope backend (sourced).

**Contracts (`skytest/skytest`):**
- Shared launch core — the env + proton command that all launch paths reuse. Express it so it does
  **NOT internally `exec`** (callers decide to `exec`, background, or wrap it). Concretely: a
  `skse_env_export()` that exports `STEAM_COMPAT_DATA_PATH` / `STEAM_COMPAT_CLIENT_INSTALL_PATH` /
  `SteamAppId=489830` / `SteamGameId=489830`, plus the path vars `PROTON` and
  `LOADER="$SKYDIR/skse64_loader.exe"`, so the invocation is `"$PROTON" run "$LOADER"`. **Resolve the
  spec's divergence:** include `SteamGameId` (headless set it, the direct path didn't — keep it; harmless
  and headless needed it). Why "no internal exec": the gamescope path passes `"$PROTON" run "$LOADER"`
  as the `--` argv to gamescope (a shell function that `exec`s can't be a gamescope argv), and
  `launch_and_hold` must run it without replacing its own shell so it can keep polling. The exact
  decomposition (function vs vars) is the implementer's call — the contract is: one source of the env +
  command, no internal exec.
- Path standardization: one canonical `STEAM` base = `~/.local/share/Steam` (where Proton +
  `launch-skse.sh` already point); derive `SKYDIR`, `PROFILES`, `PREFIX`, `SAVES_DIR`, `PLUGINS_TXT`,
  `PROTON` from it. Replace the current `~/.steam/steam/…` `ROOT` usages. Net behavior identical
  (symlinked same target); the point is one base path, no dual scheme.
- `source "$SCRIPT_DIR/lib/gamescope.sh"` near the top (after `SCRIPT_DIR`/`REPO_ROOT` resolve).
- Reroute the three launch sites off the external `launch-skse.sh` onto the shared core, **preserving
  each one's existing lifecycle** (they differ — don't homogenize them):
  - `cmd_play` (today: `exec env SteamAppId=489830 bash "$LAUNCH"` — restore-any-test-state *inline*,
    then `exec`, so no hold loop and no EXIT trap): keep the inline restore + `exec`, but `exec` the
    shared core (`exec env … "$PROTON" run "$LOADER"`) instead of `bash "$LAUNCH"`.
  - `launch_and_hold` (used by `cmd_setup_save`; today: `bash "$LAUNCH"` then pgrep-and-hold, with the
    caller's EXIT-trap restore): keep the pgrep-hold + trap, just run the shared core instead of `$LAUNCH`.
  - The old `cmd_test` windowed `launch_and_hold` call is removed entirely in Task 3 (test goes detached).
  `cmd_normal` does **NOT** launch — it only flips `Data→full` (line 451-453). Leave its mechanism
  alone; its only merge-relevant change is the live-session guard in Task 4.

**Contracts (`skytest/lib/gamescope.sh`):** sourced helpers, all operating on the gamescope test session.
- `GS_PIDFILE` (default `/tmp/skytest-gamescope.pid` — renamed from the old `/tmp/headless-skyrim.pid`;
  a one-time pre-merge stale pidfile won't be seen by the new guard, which is fine post-merge), `GS_EIS_SOCK`
  (`${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/gamescope-0-ei`), `EIDRIVER="$SCRIPT_DIR/eidriver/eidriver"`,
  `GS_WIDTH`/`GS_HEIGHT` (default 1280×720).
- `gs_launch <backend>` — `backend` ∈ {`wayland`,`headless`}. Uses the `setsid bash -c '…echo $$ >pidfile;
  exec gamescope --backend <backend> -W -H -- <launch_skse_core invocation>'` trick so the recorded pid
  IS the gamescope compositor (SIGUSR2 + teardown target). Guards against an already-live session via
  `gs_session_alive`. Returns once the pidfile is written (detached).
- `gs_session_alive()` — true iff `GS_PIDFILE` holds a live pid whose `/proc/<pid>/cmdline` contains
  `gamescope` (the pidfile-not-cmdline-grep discipline from the current `launch.sh`). Shared by status +
  launch guard (Task 4 + 5).
- `gs_wait_ready [timeout=180]` — poll SkytestProbe `status` via `commands.jsonl`/`trace.jsonl` for
  `inWorld:true` (port `ready.sh` verbatim, incl. the `session_dead` fast-fail keyed on `GS_PIDFILE`).
  Exit 0 in-world / 1 timeout / 2 session died.
- `gs_shot [out=/tmp/sky-shot.png] [crop] [scale]` — `SIGUSR2` → newest `/tmp/gamescope_*.avif` → `magick`
  (port `shot.sh`, targeting `GS_PIDFILE`).
- `gs_drive <tap|seq|key|click|abs|rel|raw> …` — the keycode map + `eidriver` dispatch against
  `GS_EIS_SOCK` (port `drive.sh`, `EIDRIVER` repointed to `skytest/eidriver/`).
- `gs_stop` — kill the gamescope **session** by `GS_PIDFILE` (port `stop.sh`'s targeted path; keep the
  no-pidfile pattern-kill fallback). Does NOT restore the profile — the caller (`cmd_stop`, Task 3) does.

**Constraints:**
- Keep the hard-won discipline from the originals: pidfile (not cmdline-grep) for liveness; session-kill
  (not `pkill SkyrimSE.exe`) for teardown so a real game in the same prefix is untouched.
- `lib/gamescope.sh` references `$SCRIPT_DIR` (set by the parent `skytest`); it is sourced, not executed.

**Verification:**
- `bash -n skytest/skytest && bash -n skytest/lib/gamescope.sh` → OK.
- `shellcheck skytest/skytest skytest/lib/gamescope.sh` → no new errors (source-following may need a
  `# shellcheck source=lib/gamescope.sh` directive).
- Dry-run: `SKYTEST_NO_LAUNCH=1 skytest play` still builds/holds correctly and restores on exit (no game).
- **Manual:** `skytest play` launches the real game via the shared core (no `launch-skse.sh` dependency)
  and quitting restores `Data→full` — proves the launch-core swap didn't break the play path.

**Commit after passing.**

---

### Task 3: Wire the CLI — `cmd_test --headless` + `ready`/`shot`/`drive`/`stop` verbs [Mode: Delegated]

Expose the backend through skytest's verb surface; make `test` a detached gamescope session.

**Files:**
- Modify: `skytest/skytest` — `cmd_test` flag parse + backend selection; new `cmd_ready`/`cmd_shot`/
  `cmd_drive`/`cmd_stop`; dispatch `case` + `cmd_help`.

**Contracts:**
- `cmd_test <mod> [--with a,b] [--headless]` — parse `--headless` → `backend=headless`, else
  `backend=wayland`. Keep the existing profile build + `inject_skytestprobe` + `inject_startonsave` +
  `write_test_plugins` + `point_data test`. Then: `gs_launch "$backend"` → `gs_wait_ready` → **return**
  (detached; print next-step hints: `skytest shot|drive|stop`). **Remove the windowed `launch_and_hold`
  + EXIT-trap restore from the `test` path** — restore now happens in `cmd_stop`. (The EXIT-trap restore
  stays only on the `play`/`normal` blocking path.)
- `cmd_ready [timeout]` → `gs_wait_ready "$@"`.
- `cmd_shot [out] [crop] [scale]` → `gs_shot "$@"`.
- `cmd_drive <args…>` → `gs_drive "$@"`.
- `cmd_stop` → `gs_session_alive || die "no live test session"`; `gs_stop`; then restore:
  `point_data full` + `restore_plugins`; say `restored: Data → full`.
- Dispatch: add `ready) … ; shot) … ; drive) shift; cmd_drive "$@" ; stop) cmd_stop`. These explicit
  cases must sit **before** the existing `*)` fallback (line 623), which treats any unrecognized first
  token as a mod-path shorthand for `test` — explicit cases match first, so this is automatic, but don't
  remove or reorder the `*)` arm. Update `cmd_help` with the new verbs + the `--headless` flag, and a
  one-line "test = drivable gamescope session; `stop` to end + restore" note.

**Constraints:**
- `cmd_test` must NOT block — CC issues `drive`/`shot` as subsequent commands. `play`/`normal` still block.
- `shot`/`drive`/`ready` error cleanly (exit ≠0, clear message) when no session/socket is up — inherited
  from the ported helpers' guards.

**Verification:**
- `bash -n` + `shellcheck` clean.
- Dry-run: `SKYTEST_NO_LAUNCH=1 skytest test mods/<somemod> --headless` builds the test profile and
  injects the probe, then (NO_LAUNCH) stops before gamescope — confirms flag parse + profile path.
- **Manual — headless (regression vs old `headless/`):** `skytest test <mod> --headless` → `skytest ready`
  reports in-world → `skytest shot /tmp/a.png` writes a frame → `skytest drive seq down down enter` →
  `skytest stop` tears down AND restores `Data→full`.
- **Manual — visible (the new path):** `skytest test <mod>` opens a gamescope window in Hyprland; repeat
  ready/shot/drive/stop. **Confirm the `gamescope-0-ei` socket + `SIGUSR2` screenshot both work under
  `--backend wayland`** (the one bit unproven vs headless). If `wayland` fails to expose the socket or a
  usable window, fall back to `--backend sdl` and record which in the design's "open items".

**Commit after passing.**

---

### Task 4: Safety net — `cmd_status` pidfile awareness + launch guard [Mode: Direct]

Net-new behavior that replaces the lost windowed-`test` auto-restore. Per spec §Lifecycle, these are the
only footgun mitigation for the detached model.

**Files:**
- Modify: `skytest/skytest` — extend `cmd_status`; extend the launch guard in `cmd_test`
  (and `cmd_play`/`cmd_normal`).

**Contracts:**
- `cmd_status`: in addition to today's output, report (a) the parked profile — whether `Data` resolves to
  `full`/`vanilla`/`test` — and (b) `gs_session_alive` → "live test session (pid N)" or "no test session".
  When `Data → test` AND a session is live, print the explicit hint: `run 'skytest stop' to end + restore`.
- Live-session guard: extend the existing `game_running && die "Skyrim is running…"` so the verbs that
  **launch over** a session (`cmd_test`, `cmd_setup_save`) and those that **unpark `Data` out from under**
  one (`cmd_play`'s inline restore, `cmd_normal`'s `point_data full`) all first
  `gs_session_alive && die "a test session is live (pid N) — skytest stop first"`. Both failure modes are
  the same hazard: a second launch double-parks `Data`, and unparking `Data→full` while the detached
  session still reads `Data→test` is the cross-profile crash (SKSE plugins from one profile, ESPs/save
  from another). One guard, applied wherever `Data` would move or a launch would start.

**Verification:**
- `bash -n` + `shellcheck` clean.
- Dry-run: with a faked live pidfile (`echo $$ > /tmp/skytest-gamescope.pid` pointing at a `sleep`-backed
  pid whose cmdline contains "gamescope" — or temporarily relax the cmdline check in a scratch test),
  `skytest status` reports the live session and `skytest test …` refuses. (Document the exact repro in the
  commit; this is the cheapest honest check without launching the game.)
- **Manual:** after a real `skytest test <mod>`, `skytest status` shows `Data → test` + live session; a 2nd
  `skytest test` refuses with the stop hint.

**Commit after passing.**

---

### Task 5: Retire `headless/` + rewrite docs (the "testing is normal" framing) [Mode: Direct]

Delete the absorbed scripts and reframe testing as a first-class step — the spec's explicit documentation goal.

**Files:**
- Delete: `headless/` entirely — `launch.sh`, `ready.sh`, `shot.sh`, `drive.sh`, `stop.sh`, `README.md`,
  `.gitignore`, `docs/design.md`, `docs/status.md` (their live content folds into the README rewrite below;
  `findings.md` already moved in Task 1). After deletion `headless/` should not exist.
- Rewrite: `skytest/README.md` — one tool, two display modes, drivable; document `test --headless`,
  `ready`/`shot`/`drive`/`stop`, the detached lifecycle + restore-on-`stop`, and link
  `docs/headless-findings.md`.
- Modify: `README.md` (repo root) — update the toolchain tour / `headless/` references to the merged tool.
- Modify: `CLAUDE.md` (repo root) — the `headless/` + `skytest/` layout rows and the "Testing a mod you
  built — which mode?" section now describe one tool.

**Contracts — the framing goal (spec §Documentation goals):** the README + CLAUDE.md testing guidance must:
- Present "build a mod → `skytest test <mod>` → `drive`/probe → confirm in-engine" as the **default**
  verification path, like running tests elsewhere — not a heavyweight detour.
- State plainly that the ~1–2 min boot is **expected and worth it**; it's how you verify a mod actually
  works in the engine. Explicitly counter the "too heavy to bother" reflex.
- Make the verbs discoverable so reaching for a test is the path of least resistance.

**Constraints:**
- Don't delete `findings.md` content — it's preserved as `skytest/docs/headless-findings.md`. Per the
  global "completed design docs still have value" rule, fold the still-true bits of `design.md`/`status.md`
  into the README rather than dropping them silently; the point-in-time status is superseded by the merge.
- Don't touch the live `Data` symlink scheme — the managing repo depends on it (spec §Open items).
- `prettier --write` any markdown tables edited.

**Verification:**
- `! test -e headless` (directory gone); `git status` shows the deletions.
- `rg -n "headless/" README.md CLAUDE.md skytest/README.md` → only references to the merged tool /
  `--headless` flag / `skytest/docs/headless-findings.md`, no dangling `headless/launch.sh`-style pointers.
- Read the rewritten testing section back: does it read as "this is the normal way to verify a mod," not
  "here's a heavy optional harness"? (The spec's acceptance bar.)

**Commit after passing.**

---

## Execution
**Skill:** superpowers:subagent-driven-development
- Mode A tasks (1, 4, 5): Opus implements directly
- Mode B tasks (2, 3): Dispatched to subagents (real integration across the script + lib; multiple valid
  shapes for the backend functions)
- TDD-where-possible: `bash -n` + `shellcheck` + `SKYTEST_NO_LAUNCH` dry-runs gate every task; the
  game-touching steps are manual and must be actually run before a task is called done.
