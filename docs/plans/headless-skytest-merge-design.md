# Design — merge `headless/` + `skytest/` into one launcher

**Status:** designed 2026-06-12, not yet built.
**Scope:** the merge only. Input **recording/playback** is a deliberate follow-up pass (see
`docs/ideas.md`), not part of this design — but `drive` is shaped to be replay-friendly so that
layer slots in without rework.

## Goal

Today two repo-local subsystems both "launch Skyrim," from opposite ends:

- **`skytest`** — the profile + lifecycle layer. Owns the `Data/` symlink swap between
  `.profiles/{full,vanilla,test}`, builds the vanilla+1 test profile, injects SkytestProbe +
  StartOnSave, then launches the **windowed** game (direct `launch-skse.sh` path), waits for the
  real PID to stabilize, holds until it exits, and restores `Data→full` on exit. No display/input
  concept.
- **`headless/`** — the display + input backend. Wraps a launch in `gamescope --backend headless`
  (no profile management — launches whatever `Data` points at), polls SkytestProbe for `inWorld`,
  screenshots via gamescope `SIGUSR2`, injects isolated input via libei (`gamescope-0-ei` socket),
  tears down by gamescope session.

They overlap on **launch + readiness + teardown** and are complementary on **display + input**.
Unify into **one tool** — `skytest` — that owns profiles/lifecycle and gains a drivable,
screenshot-able display layer, with the game's *visibility* as the only knob.

## Core architecture: "test session" vs "play"

The split is **not** "windowed vs headless." It's:

### A test session always runs under gamescope (so it is always drivable + screenshot-able)

The only difference between the two visibilities is gamescope's `--backend` value:

| Invocation | gamescope backend | Visible? | Drivable / shot-able? |
|---|---|---|---|
| `skytest test <mod>` (default) | `wayland` | yes — a window nested in Hyprland | yes |
| `skytest test <mod> --headless` | `headless` | no | yes |

Both share **one** code path: detached launch → poll probe for `inWorld` → `drive`/`shot` against
the gamescope EIS socket + `SIGUSR2` → `skytest stop` (kill session + restore). `--headless` swaps
the backend string and nothing else. So "debugging together, you watch while CC drives" is the
*same machinery* as a headless test — you just see it.

Why gamescope for the *visible* test too (instead of the old bare windowed path): `drive`/`shot`
exist **because** the game runs under gamescope — libei input via `gamescope-0-ei`, screenshot via
gamescope `SIGUSR2`. A bare windowed game has no gamescope, hence nothing to drive. Running the
visible test under `--backend wayland` reuses the entire headless machinery unchanged; the backend
difference collapses to one flag.

### The bare direct path survives only for `play`/`normal`

`skytest play` / `skytest normal` keep today's direct proton launch (`launch_and_hold`): **blocking,
restore-on-exit, fast, your real fullscreen game, not under gamescope, not drivable.** This is the
"actually play" path — forcing gamescope onto the daily-driver play path would risk the fast-path
win, and there's no reason to drive your own playthrough.

## CLI surface

Display is a flag on the launching verb; the rest is additive.

```
skytest test <mod> [--with a.dll,b.dll] [--headless]   # gamescope test session (visible default)
skytest play [--headless?]                              # NO — play stays bare direct (see note)
skytest ready [timeout]                                 # block until in-world (probe poll)
skytest shot [out.png] [cropWxH+X+Y] [scaleWxH]         # screenshot the running test session
skytest drive <tap|seq|key|click|abs|rel|raw> ...       # libei input into the running test session
skytest stop                                            # tear down the test session + restore profile
```

- `ready` / `shot` / `drive` / `stop` operate on a **running gamescope test session**. They error
  cleanly if no session is up (no pidfile / no EIS socket), exactly as the standalone scripts do today.
- `play` / `normal` are **not** drivable and gain no `--headless` — they are the direct play path.
  (If a drivable *full-profile* session is ever wanted, that's `test` against the full profile, not
  a change to `play`.)
- Existing verbs (`init`, `setup-save`, `uninstall`, `status`, `normal`) keep their behavior;
  `status` is extended (see Lifecycle).

## Launch core

Both paths run the identical proton invocation — verified: `launch-skse.sh` (windowed) and
`headless/launch.sh` both do `proton run skse64_loader.exe` with the same `STEAM_COMPAT_DATA_PATH`
/ `STEAM_COMPAT_CLIENT_INSTALL_PATH` and `SteamAppId=489830`. **One divergence to resolve when
building `launch_skse_core`:** `headless/launch.sh` exports *both* `SteamAppId` and `SteamGameId`,
while skytest's direct path (`launch_and_hold`) sets only `SteamAppId`. The unified helper must
decide deliberately whether `SteamGameId` is needed (headless thought it was) so it isn't silently
dropped for one path. The merge introduces one `launch_skse_core` helper (env + proton invocation):

- **direct (`play`/`normal`)** runs `launch_skse_core` bare;
- **gamescope (`test`)** runs `gamescope --backend <wayland|headless> -W -H -- launch_skse_core`,
  recording the **gamescope** pid to the pidfile via the existing `setsid` inner-shell `exec` trick
  (so the pid survives the exec and is the SIGUSR2 / teardown target).

This lets `skytest` stop depending on the external `launch-skse.sh`. That file **stays** (it's the
target of the "Skyrim SE (SKSE, fast)" desktop entry / Steam non-Steam-game) — `skytest` simply no
longer calls it. No formal multi-hook "backend registry": for two asymmetric paths that is premature
abstraction. `cmd_test` sources `lib/gamescope.sh`; `cmd_play`/`cmd_normal` use the inline direct path.

## Lifecycle & profile restore (the crux)

The two paths have fundamentally different lifecycles, which changes **when `Data→full` restore fires**:

- **`play` / `normal` — blocking, restore-on-exit (unchanged).** Launch → hold the terminal until the
  game PID exits → `EXIT` trap restores `Data→full` + `Plugins.txt`. One command owns the lifecycle.
- **Test session (visible *or* headless) — detached, restore-on-`stop` (new).** Launch gamescope in
  the background, wait until in-world, then **return**, so CC can issue `skytest shot` / `drive` /
  `exec` / `ready` as separate commands against the live session. The running game holds `Data` open
  the whole time, so the restore **cannot** happen on launch-return — it is **deferred to
  `skytest stop`**, which both tears down the gamescope session and restores `Data→full` +
  `Plugins.txt`.

This trades away the old windowed-`test` auto-restore-on-quit (accepted). A detached process can't be
covered by an `EXIT` trap, so the footgun "forgot to `stop`, `Data` stuck on `test`" is mitigated by
**visibility**, not by trapping:

1. `skytest status` reports both the parked profile (`Data → test`) **and** a live test session
   (pidfile alive).
2. The launching verbs already `die` if a game is running; this is extended to detect a live
   **gamescope test session** (live pidfile) and refuse a second launch, pointing at `skytest stop`.

Both are **net-new behavior**, not tweaks: today's `cmd_status` has no pidfile awareness, and the
launch guard only checks `game_running` (a running game), not a live detached gamescope session.
The plan must treat "teach `cmd_status` about the gamescope pidfile" and "extend the launch guard to
detect a live gamescope session" as explicit, separate steps — they are the *only* safety net
replacing the lost windowed-`test` EXIT-trap auto-restore.

A blocking launcher does **not** prevent driving — `drive`/`shot` work via the socket/pidfile
regardless of whether the launching command is still in the foreground. Detached is required only so
that *the agent that launched* can issue the next command; the human-watch case works identically.

## Readiness

Not force-unified, because the lifecycles differ:

- **Test sessions** poll the probe for `inWorld` (today's `ready.sh` logic) — there is no foreground
  game PID, and `drive`/`shot` need the *interactive* signal (not merely "process alive" or "at main
  menu"). Exposed as `skytest ready [timeout]`.
- **`play` / `normal`** keep PID-stabilize — they need the PID to hold-until-exit anyway, and the
  full profile may not carry the probe.

`skytest ready` is the shared scripting primitive for any session that has the probe injected.

## File layout & retirements

`headless/` (top-level) goes away; its mechanics become `skytest`'s backend internals:

```
skytest/
  skytest                        # the one CLI: dispatch + profile/lifecycle + verb impls
  lib/gamescope.sh               # sourced: launch-under-gamescope, ready-poll, shot, drive, stop
                                 #   (absorbs headless launch.sh/ready.sh/shot.sh/drive.sh/stop.sh)
  eidriver/{eidriver.c,build.sh} # libei input client (moved from headless/src/)
  base-skse/                     # unchanged (vendored po3_StartOnSave)
  docs/headless-findings.md      # hard-won gamescope/libei dead-ends (from headless/docs/findings.md)
  README.md                      # rewritten: one tool, two display modes, drivable
```

- The standalone `headless/{launch,ready,shot,drive,stop}.sh` retire — logic absorbed into
  `lib/gamescope.sh`. `eidriver.c` + its `build.sh` move from `headless/src/` to `skytest/eidriver/`;
  `lib/gamescope.sh`'s `EIDRIVER` path must be repointed from `$HERE/src/eidriver` to the new dir.
- `headless/docs/findings.md` is the must-keep (it documents *why each approach failed* — read it
  before changing the gamescope/libei approach). It moves to `skytest/docs/headless-findings.md`.
  `headless/docs/{design,status}.md`: fold the still-live bits into the rewritten `skytest/README.md`
  + this design doc, then retire (per the global "completed design docs still have value" rule, the
  *findings* doc is preserved; the status doc is point-in-time and superseded by the merge).
- `SCRIPT_DIR` already resolves through the PATH symlink via `readlink -f`, so
  `source "$SCRIPT_DIR/lib/gamescope.sh"` works for the `~/.local/bin/skytest` install.

## Path cleanup (folded into the merge)

`skytest` uses `~/.steam/steam/…`; `headless/` uses `~/.local/share/Steam/…`. They
resolve to the same install via symlink, but the merged tool standardizes on **one** set —
`.local/share/Steam`, which is where Proton and `launch-skse.sh` already point. Low-risk (same
target), removes the dual-path confusion. Confirm the EIS socket path
(`$XDG_RUNTIME_DIR/gamescope-0-ei`) and pidfile location are consistent after the move.

## Documentation goals

Headless/gamescope testing must read as a **normal, expected step in the mod-making loop**, not a
heavyweight last resort. Today CC hesitates to reach for it because it's "heavy / takes 1–2 minutes."
The rewritten `skytest/README.md` and the repo `CLAUDE.md`/`README.md` mod-testing guidance must:

- Frame "build a mod → `skytest test <mod>` → drive/probe → confirm in-engine" as the default
  verification path, the same way unit tests are the default elsewhere.
- State plainly that the ~1–2 min boot is **expected and worth it** — it's how you verify a mod
  actually works in the engine, not an exceptional cost to avoid.
- Make the verbs discoverable so reaching for a test is the path of least resistance, not a detour.

This is an explicit, first-class task in the implementation plan's doc step — not an afterthought.

## Verification plan

Mostly re-wiring code that already works, so prove the re-wired paths once:

1. **Regression — `play` / `normal`**: still launch the fast direct path; quitting restores
   `Data→full` + `Plugins.txt`.
2. **New — visible test**: `skytest test <mod>` opens a gamescope window in Hyprland; `skytest ready`
   reports in-world; `skytest shot` captures a frame; `skytest drive seq down down enter` moves the
   menu; `skytest stop` closes the window **and** restores `Data→full` + `Plugins.txt`. **The one bit
   unproven vs headless:** confirm the EIS socket (`gamescope-0-ei`) and `SIGUSR2` screenshot both
   appear under `--backend wayland` (they're documented working under `--backend headless`).
3. **Regression — headless test**: `skytest test <mod> --headless` behaves as today's `headless/`
   flow (launch → ready → shot/drive → stop).
4. **Footgun**: after a test launch, `skytest status` shows `Data→test` + a live session; a second
   `skytest test` refuses, pointing at `skytest stop`.

## Alternatives considered

- **Orchestrator over standalone scripts** (skytest gains `--headless` and shells out to the existing
  `headless/*.sh`). Rejected: keeps two parallel surfaces and duplicates launch/readiness/teardown
  forever. The point of the merge is to erase that split, not bridge it.
- **Bare windowed test + a separate input mechanism** (XTEST / future SKSE input hook to drive a
  non-gamescope window). Rejected: XTEST leaks to the real seat (not isolated); the SKSE hook is a
  future tier. gamescope-`wayland` reuses the proven, isolated libei path with zero new mechanism.
- **Forcing `play`/`normal` under gamescope too** (one universal launch path). Rejected: risks the
  fast-path win on the daily-driver play path for no benefit — play needs neither drive nor screenshot.
- **A formal multi-hook backend registry** (`launch`/`wait_ready`/`hold`/`stop` as pluggable hooks).
  Rejected as premature abstraction for exactly two asymmetric paths; a sourced `lib/gamescope.sh` +
  the inline direct path is enough.

## Open items to resolve during implementation

- Confirm `--backend wayland` (vs `sdl`) is the right visible backend under Hyprland — both nest a
  window; pick whichever gives the EIS socket + `SIGUSR2` + a usable window. (`sdl` is the fallback.)
- Decide the exact home for `headless/docs/{design,status}.md` content (fold vs keep) when rewriting
  the README — findings.md is preserved regardless.
- The managing repo (`~/Downloads/skyrim-mods/`) relies on skytest's `Data` symlink scheme; the path
  standardization must not change the live symlink behavior (covered by `skytest uninstall` revert).
