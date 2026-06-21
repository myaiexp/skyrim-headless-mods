# skytest: fast isolated Skyrim SE mod-test launcher (drivable)

`skytest` launches Skyrim with **only one mod active** on an otherwise vanilla+AE baseline,
for fast, interference-free testing of mods built in this repo, and can drive that session
hands-free: **screenshot it, inject input, read its real state**, with the game **visible or
invisible**. It is the profile-swapping core of a mod manager (plain symlinks, no virtual
filesystem) plus a gamescope display/input layer, in one tool.

> **Lives in the _making_ repo, operates on the _live_ game.** The script is here
> (`~/Projects/skyrim-headless-mods/skytest/skytest`, symlinked onto `PATH` as `skytest`), but it
> swaps the real game install's `Data/`. The mod-_managing_ repo (`~/Downloads/skyrim-mods/`) still
> depends on that symlink for its manual installs, so keep both sides in mind when changing behavior.

How it works: the live `Data/` is a **symlink** to `.profiles/full` (the real ~40-mod setup,
renamed once, never modified). A test run retargets that symlink to `.profiles/test`
(= vanilla BSAs/plugins + Address Library + CrashLogger + the one mod), launches SKSE directly,
and restores `Data → full` when you `stop`. The profiles live next to the game at
`<game>/.profiles/` (**not** in this repo).

```
skytest init [--commit]        # one-time: turn Data into a symlink, build vanilla profile
skytest setup-save             # one-time: launch vanilla to BUILD the base test save
skytest test <mod> [--headless]# start a drivable gamescope test session (visible by default)
skytest <mod>                  #   ^ bare shortcut, identical to `test`
skytest replay <mod> <s.steps> # boot, then run a .steps script to snap to a target state
skytest ready [secs]           # block until the session is in-world (probe poll)
skytest shot [out.png]         # screenshot the session       [cropWxH+X+Y] [scaleWxH]
skytest drive <cmd> …          # inject input (tap|seq|key|click|abs|rel|raw)
skytest stop                   # tear down the session + restore Data → full
skytest play                   # launch the FULL modded game over the fast direct path (blocking)
skytest play agent [--headless]# FULL modded profile under a DRIVABLE gamescope session (for the agent)
skytest status                 # show profile + live test session (also the no-arg default)
skytest normal                 # force Data → full (recover after a crash)
skytest uninstall              # revert Data to the original real directory
```

Verb-based CLI (AXI-ergonomic): `skytest help` lists verbs, `skytest <verb> --help` gives
examples, `status`/`init` accept `--json`. Errors carry a `Try:` hint and exit 2.

## Testing a mod is the normal step: reach for it

Building a mod and **not** running it in the engine is the exception, not the other way round.
`skytest test <mod>` is this repo's equivalent of running the test suite: build the artifact →
`skytest test <mod>` → `drive`/probe it → confirm it actually does the thing in-engine. Treat it
as the default close-out for any mod change, the same way you'd run unit tests elsewhere.

It's a real engine launch, not a mock: the only way to know a mod works in the engine, not just
that it compiled. The session is **detached**: fire it off, keep working, and `drive`/`shot`/probe
it once it's in-world. The verbs below make reaching for a test the path of least resistance.

> **Driving a cast / input-state mod headlessly? Read `docs/headless-findings.md` #15–17 first.**
> The hard-won lessons from the AutoCastSpell session: **libei "hold" ≠ a real held mouse/key** for
> anything keyed off the engine's held-input state machine (charge-while-held, auto-repeat). The rig
> can prove a mechanism _exists_ but not that it's _reliable_; validate that on real hardware (#15).
> `ready`/`inWorld` fires **before** the autoloaded save finishes, so wait for a player-loaded signal
> (#16). And the iteration cheat-sheet: stage state via `give-spell`/`set-av` (programmatic `exec`
> faults in the test session), bump `REL::Version` each rebuild, launch from repo root, hold = one
> `drive raw` call (#17).

## Drivable test sessions: visible or headless

A **test session always runs the game under gamescope**, so it is always screenshot-able and
drivable. The only knob is visibility:

| Command                         | gamescope backend | Visible?                          | Drivable / shot-able?                  |
| ------------------------------- | ----------------- | --------------------------------- | -------------------------------------- |
| `skytest test <mod>` (default)  | `wayland`         | yes (a window nested in Hyprland) | yes                                    |
| `skytest test <mod> --headless` | `headless`        | no (offscreen render)             | yes                                    |
| `skytest test <mod> --sdl`      | `sdl`             | yes                               | yes (fallback if `wayland` misbehaves) |

Both share one path: **launch → `ready` (poll the probe for in-world) → `drive`/`shot` → `stop`**.
`--headless` swaps the backend string and nothing else, so "watch CC drive while you look over its
shoulder" is the _same machinery_ as a headless test. You just see it.

A test session is **detached**: `test` returns once the game is in-world, so you issue `shot` /
`drive` / `ready` as separate follow-up commands against the live session, then `stop` to end it.
Because the running game holds `Data` open the whole time, the `Data → full` restore happens on
**`stop`**, not on launch-return. (Bare `play`/`normal` keep the old blocking, restore-on-exit
path: they are the daily-driver fast launch, not under gamescope, not drivable — there is no
reason for a human to drive their own playthrough. When the **agent** needs to drive the full
profile, `skytest play agent [--headless]` boots that same profile under a detached, drivable
gamescope session instead — see below.)

### How the eyes + hands work (folded from the old `headless/` driver)

- **Eyes: `gamescope` + SIGUSR2.** gamescope renders the game on the GPU; `shot` sends it
  `SIGUSR2`, it dumps its composited frame to `/tmp/gamescope_<ts>.avif`, and skytest converts that
  to PNG. (X11 grabbers see only black, because the game is Vulkan/DXVK; gamescope's own dump is the capture
  path.)
- **Hands: libei via `gamescope-0-ei`.** `drive` connects a libei client (`eidriver/`, compiled
  C) to gamescope's EIS socket and sends pointer/keyboard events. It's **relative-native** (what
  raw-input Skyrim consumes; raw libei _absolute_ is inert, so `abs`/`click` are synthesized from
  relative, measured 1:1), **socket-scoped** (can't reach your real seat), and **isolated** by the
  session. Keyboard is the deterministic path; pointer `click x y` works open-loop.
- **Truth: SkytestProbe.** Injected into every test profile (below). `ready` polls its `status` for
  `inWorld:true`; `drive`+probe gives a full blind test loop (act, then read `trace.jsonl`).

### Safety net (the detached model has no auto-restore)

A detached session can't be covered by an exit trap, so "forgot to `stop`, `Data` stuck on test" is
caught by **visibility**, not trapping:

- `skytest status` reports both the parked profile (`Data → test`) **and** the live session
  (`session  live test session (pid N)`), and when both hold it points at `skytest stop`.
- Every verb that would launch a 2nd game over the session, or unpark `Data → full` out from under
  it (`test`, `setup-save`, `play`, `normal`, `uninstall`), **refuses while a session is live**,
  pointing at `skytest stop`. (Unparking `Data` while the live session still reads `Data → test` is
  the cross-profile crash: SKSE plugins from one profile, ESPs/save from another.)

### Don't run a test while another Skyrim is open

A test session **can't spawn the game if any Skyrim is already running**: Steam blocks a 2nd
instance of the appid, so gamescope comes up empty and `shot` is **black** (and `ready` can falsely
pass, because the probe's `commands.jsonl`/`trace.jsonl` is shared prefix-wide and the _other_ game
answers it; see `docs/headless-findings.md` #13). `test`/`play`/`setup-save` now **refuse up front**
with _"Skyrim is already running (pid N), close it first"_, so this shouldn't bite you; if it does,
close the game (or kill a stray `SkyrimSE.exe`) and re-run.

Verified clean (2026-06-12, no game running): `shot` **works headless** (it captured a clear main
menu) and `ready` polls honestly. **Per-test Saves isolation is now shipped**, closing the
autoload-stall (see `docs/headless-findings.md` #13): `isolate_saves` redirects `SLocalSavePath` to
a `Saves_skytest` dir holding **only** the base save, so the test menu's "Continue" can't auto-check
your _main modded_ save and pop the "missing content" modal that blocked po3 StartOnSave from
loading `SkytestBase`. It's applied unconditionally on the test-boot path when a base save exists,
torn down automatically on `stop`/`normal`/`play`/`uninstall`, and surfaced in `skytest status`.
The one genuinely-still-open item: a precise **in-menu mouse click** to dismiss a modal via `drive`
(keyboard `drive` is confirmed in-world, finding #14). The dead-ends behind the whole display/input
layer are in [`docs/headless-findings.md`](docs/headless-findings.md). **Read it before changing the
gamescope/libei approach.**

## Which mode: `test` or `play`?

Use `skytest test <mod>` for a mod that works **standalone** (a new spell, a DLL, a self-contained
esp): vanilla + that one mod, drivable.

For a mod that only manifests **on top of the live load order** (patches, asset overrides of
another mod, e.g. a DBVO `dialoguemenu.swf` edit needing DBVO + a voice pack, or **anything that
depends on SkyUI**, an MCM), the vanilla+1 test profile can't reproduce it. Install into the full
profile (the mod's own `build.sh --install`), then launch it one of two ways:

- **`skytest play`** — the blocking, non-drivable full-profile launch. Play/observe only (the human
  daily-driver path).
- **`skytest play agent [--headless]`** — the **drivable** full-profile counterpart, for when the
  agent needs to drive/shot/probe the modded game. It boots the same `full` profile under a
  detached gamescope session with **SkytestProbe injected and its IO reset**, then returns so you
  issue `drive`/`shot`/probe as follow-ups (it boots to the MENU — `drive e,e` to a save); tear it
  down with `skytest stop`. This is the reintroduced replacement for the old `playtest` verb.
  (`ready`/`gs_wait_ready` is unreliable on `full`; watch `trace.jsonl` instead — finding #19.)

**Verifying an MCM without navigating to it.** Driving SkyUI's journal _tabs_ is unreliable (the
mouse-cursor desync, findings #9b/#14), but you rarely need to: grep the Papyrus log
(`…/Logs/Script/Papyrus.0.log`) for `Registered <ModName> at MCM` (SkyUI logs every page it
discovers) and confirm there are **no** `<Native> is not a valid function` lines (a missing C++↔
Papyrus native would log there). Page registered + zero native errors = the MCM and its bridge work.

## Replay a test setup: `skytest replay <mod> <script.steps>`

The **first** time you verify a mod you drive it live (`test`, then `drive`/`shot`/probe). **Every
time after**, persist that setup as a `.steps` script and re-run it with `replay`: it boots the
same isolated `test` session, runs the script to snap the world to the target state, then leaves
the session **detached and live** for you to probe (`shot`/`drive`/`exec`) or `stop`. The session
is left up on a **step failure too** (a wrong setup state aborts the run with the offending step,
but doesn't tear down: inspect, then `skytest stop`).

Scripts live next to the mod: `mods/<Name>/<name>.steps`. A bare `<script>` resolves there; a
path with `/` (or `-` for stdin) is taken as-is. `--headless`/`--with` work as for `test`;
`--dry-run` prints the normalized step plan and exits (a lint: no boot, no profile change).
`--no-shots` disables the per-step filmstrip (see below).

`.steps` is line-based (`#` comments, blank lines ignored):

| Step                                     | Meaning                                                                                                                                                                                                                                                                           |
| ---------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `cmd <json>`                             | **The staging path.** Send a direct-call probe command (the whole rest of the line, a JSON object) and block on its ack before the next step: `cmd {"cmd":"placeatme","base":"0x..","as":"ally","d":250}`, `make-teammate`, `cast`, `give-spell`, `set-av`. Use this, not `exec`. |
| `exec <console>`                         | Run the rest of the line as a console command. **⚠ Not the staging path**: programmatic `exec` faults in the test session; use `cmd` instead (caveat below).                                                                                                                      |
| `tap <KEY>`                              | One keypress (`gs_keycode` names: `tilde` `m` `q` `e` `up`/`down`/… ).                                                                                                                                                                                                            |
| `key <K1> <K2> …`                        | A sequence of taps.                                                                                                                                                                                                                                                               |
| `hold <LMB\|RMB\|KEY> <dur\|until:COND>` | Press, gate, release (release always runs, even on a timed-out gate).                                                                                                                                                                                                             |
| `wait <dur\|until:COND>`                 | Block on a fixed duration (`500ms`/`2s`) or an observed-state gate.                                                                                                                                                                                                               |
| `shot [name]`                            | Checkpoint screenshot (default `/tmp/sky-shot.png`).                                                                                                                                                                                                                              |

**Per-step filmstrip (default on).** `replay` captures a screenshot after **every** step into
`<probe-io>/replay-shots/NN-verb.png` (plus `00-start.png`), so a run leaves a step-indexed
filmstrip you review in **one batch** instead of the slow take-shot → read → act loop. Best-effort
(a failed capture never aborts the run) and it also snaps the **failing** step (`NN-verb-FAILED.png`)
— often the most diagnostic frame. Verified working under `--headless` (real composited frames, not
black — see `docs/headless-findings.md`). Disable with `--no-shots`.

Gates poll SkytestProbe (never a blind sleep), 180 s default, fast-fail on session death:

- `until:inworld`: fully interactive (no main/loading menu + player 3D loaded).
- `until:menu:<NAME>`: a UI menu open (CommonLib name: `Console`, `MapMenu`, `FavoritesMenu`, …).
- `until:charged`, `until:actorcount`: **not built**; added per the first script that needs them
  (one `resolve_gate` row + one direct-call probe handler, see `mods/SkytestProbe` `is-menu-open`).

> **⚠ Console `exec` is not the staging path**: by design. Programmatic `exec` (the probe's
> `CompileAndRun`) AVs — **pinned (`../skytest/docs/headless-findings.md` #18): this CommonLib build
> predates the 1.6.1170 runtime, so `CompileAndRun`'s bound Address Library id is stale and the call
> lands on the wrong function.** Not a headless or "missing console subsystem" issue (it faults
> with the console menu open too, and would fault in a windowed 1.6.1170 game). The harness model is
> **engine calls for staging, the drive layer for input**: stage world state with **direct-call**
> SkytestProbe commands (`give-spell`/`set-av`, and `coc`/`placeatme` added per-need — every console
> verb is just an engine-call wrapper) and drive anything needing input through `tap`/`hold`/`drive`.
> `exec` stays in the probe (SEH-guarded, harmless) but `replay` does input + gates + shot, not
> console staging. Background: `../docs/plans/skytest-replay-handoff.md`. Example: `examples/format-demo.steps`.

## Driving the probe from the CLI (`io` / `cmd` / `trace` / `wait-probe` / `restart`)

Talking to SkytestProbe used to be raw: every command a hand-built `echo '<json>' >> "<long
path>/commands.jsonl"`, every read a bespoke `jq` over `trace.jsonl`, the IO dir hunted with
`find`, and each restart a hand-rolled poll loop. These thin wrappers remove that friction:

| Verb                             | Does                                                                                                                                                                                                                      |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `skytest io`                     | Print the resolved probe IO dir (`…/SKSE/skytest`). No session needed.                                                                                                                                                    |
| `skytest cmd '<json>' [timeout]` | Append a probe command **and block for its ack**, then print every trace line it produced (its `src:` output + the ack). Injects an `id` if absent. Exit `0` ok, `1` ack failed, `2` bad JSON, `3` no ack (default 15 s). |
| `skytest trace [filters]`        | Filtered view of `trace.jsonl`: `--tail N` (default 40), `--src X` (grep `"src":"X"`), `--since T` (epoch-ms or relative `30s`/`500ms`), `--jq EXPR`, `-f` follow.                                                        |
| `skytest wait-probe [secs]`      | Block until the probe is **answering** (in-world _not_ required). The gate `ready` can't serve on the menu-booting `full` / `play agent` path. Exit `0`/`1`/`2`.                                                          |
| `skytest restart [args…]`        | `stop` the live session **+ relaunch the same mode** in one verb (the spec is saved on each `test`/`play agent` launch). With args, runs `skytest <args>` instead. For the native-DLL stop→relaunch cycle.                |

Typical loop: `skytest cmd '{"cmd":"facegen-watch","ref":"speaker","on":true}'` then
`skytest trace --src face --since 5s --jq '{t,paused,gt}'`. The `drive seq` inter-key gap defaults
to **300 ms** (override `seq --gap MS` or `SKYTEST_SEQ_GAP_MS`); `status --json` carries a pollable
`world` block (`skytest status --json | jq '.world.inWorld'`) when a session is live.

## Boot straight into a test save (v2)

Instead of clicking through the main menu each run, `skytest <mod>` can drop you straight into
a prepared save with the character controllable:

1. **Build the base save once:** `skytest setup-save` launches a vanilla+base session. At the
   menu, console `coc qasmoke` (Bethesda's dev hall: instant load, all-items chest, NPC/weapon
   spawn levers), place any fixtures you want shared across tests (e.g. set a vanilla NPC as
   `setplayerteammate 1` for ally-acting mods like GhostAllies), then `save SkytestBase` and quit.
   **Use only vanilla content** so the save loads under any test.
2. **From then on**, `skytest <mod>` injects **powerofthree's Start On Save** (a DLL-only,
   Address-Library-only SKSE plugin) into the test profile, pinned to `SkytestBase`, and the game
   autoloads it on launch. Hold **SHIFT** while the load gear spins to skip into the menu.

Start On Save lives **only in the test profile**, never in vanilla, so `setup-save` stays at the
menu. If no `SkytestBase.ess` exists yet, `skytest <mod>` just launches to the menu (no autoload).
The DLL + ini template live in `base-skse/`; the source archive is in the managing repo's `01-core/`.

> [**Start On Save**](https://www.nexusmods.com/skyrimspecialedition/mods/56795) is by
> **powerofthree**, redistributed here under its permissive terms (powerofthree's mods may be
> reused/redistributed with credit). All credit for Start On Save goes to powerofthree.

**SkytestProbe (runtime debug toolkit).** `skytest <mod>` also injects **SkytestProbe** into the
test profile **unconditionally** (DLL-only/Address-Library-only, like Start On Save but with no
save condition; ini copied verbatim). It's a pre-compiled probe plugin: write JSON commands to
`…/SKSE/skytest/commands.jsonl` and the running game writes structured traces to `trace.jsonl`
in the same dir: arm engine event sinks (`trace`), dump an actor's state incl. collision group
(`dump`), `watch` an actor value, sample a face's morph keyframes (`facegen-watch`) / log them
per-render-frame read-only (`facegen-observe`) / force a parameterized facegen reset
(`facegen-close`) — these accept a `speaker` ref (the live dialogue NPC), and every facegen line
carries a `paused`/`gt` guard so a frozen sample is never mistaken for live data — run a console
line (`exec`), `anim-trace`, `marker`, `status`; F11 drops a marker + auto-dump. It kills the probe-recompile-restart loop when debugging the C++
mods, and it is what `skytest ready` polls. Passive until armed, never crashes on bad input. Built
from `../mods/SkytestProbe` (`./build.sh`); skytest reads the **DLL** from that build output
(`build/SkytestProbe.dll`) and the **ini** from the source dir (`SkytestProbe.ini`, alongside
`build.sh`): the canonical copies, no vendored duplicate. Contract in
`../docs/plans/skytest-probe-design.md`. skytest degrades gracefully if the DLL hasn't been built.

## Notes

- **Everyday play:** `skytest play`, or the **"Skyrim SE (SKSE, fast)"** app-launcher entry
  (`~/.local/share/applications/skyrim-skse-fast.desktop`), launches the full modded game via the
  direct path, much faster than Steam Play. (skytest no longer shells out to `launch-skse.sh`; it
  runs the same `proton run skse64_loader.exe` itself. That script stays as the desktop-entry target.)
- The base save must depend on **vanilla + base SKSE only**: a modded save crashes in the
  vanilla+1 profile. (That is also why autoload is pinned to `SkytestBase`, not "latest save",
  which would grab a modded autosave from the shared prefix Saves folder — though per-test Saves
  isolation now also removes that shared-folder hazard for test sessions; see #13 above.)
- Per-mod fixtures beyond a generic baked ally (a tiny test `.esp`, or a console batch run at
  load) are deferred; bake what most tests share into `SkytestBase`. (See `../docs/ideas.md`.)
- Startup is ~2 DLLs instead of the full ~51 (+1 for Start On Save), and it launches via the
  direct proton path (much faster than Steam Play, the real cause of the slow startup).
- A `crash-*.log` on quit-to-desktop is the known vanilla Skyrim shutdown crash, not a fault of
  the mod under test (check the call stack, not just the module list, to confirm).
- Steam blocks a 2nd instance of the appid, so a test session won't spawn the game _over_ a real
  Steam-launched Skyrim (the gamescope scaffolding comes up, the game never does). `stop` tears
  down by gamescope **session**, leaving a game you're playing in the same prefix alone
  (`docs/headless-findings.md` #12).
- Full flags + rationale are in the script header (`skytest`), and the v2 bring-up history is in
  `../docs/plans/skytest-v2-handoff.md`.

## Layout

| Path                        | Holds                                                                                                                                                                                                                                                             |
| --------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `skytest`                   | The launcher: dispatch + profile/lifecycle + verb impls (self-documenting via `skytest help`). On `PATH` via `~/.local/bin/skytest`.                                                                                                                              |
| `lib/gamescope.sh`          | Sourced gamescope backend: `gs_launch` / `gs_wait_ready` / `gs_shot` / `gs_drive` / `gs_stop` / `gs_session_alive`: launch-under-gamescope, ready-poll, screenshot, libei input, session teardown. Absorbed the old `headless/{launch,ready,shot,drive,stop}.sh`. |
| `eidriver/`                 | The libei input client (`eidriver.c` + `build.sh`, `gcc` + `pkg-config libei-1.0`, no sudo). Compiled binary is gitignored; build it once with `eidriver/build.sh`.                                                                                               |
| `base-skse/`                | The third-party SKSE plugin skytest injects into every test profile: `po3_StartOnSave.{dll,ini.template}` (vendored, no in-repo source). SkytestProbe is _not_ here; skytest reads it straight from `../mods/SkytestProbe/build/`.                                |
| `docs/headless-findings.md` | Every gamescope/libei dead-end and why it's a wall. **Read before changing the display/input approach.**                                                                                                                                                          |

## Requirements (all already present on this machine, no root)

`gamescope`, `libei` (+ header at `/usr/include/libei-1.0`, resolved by `pkg-config`), `gcc`,
ImageMagick (`magick`), `jq`. A Skyrim SE + SKSE install (Proton Experimental prefix). `sudo` is
**not** available here: everything is user-space/compiled (`docs/headless-findings.md` #7).
