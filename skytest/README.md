# skytest — fast isolated Skyrim SE mod-test launcher (drivable)

`skytest` launches Skyrim with **only one mod active** on an otherwise vanilla+AE baseline —
for fast, interference-free testing of mods built in this repo — and can drive that session
hands-free: **screenshot it, inject input, read its real state**, with the game **visible or
invisible**. It is the profile-swapping core of a mod manager (plain symlinks, no virtual
filesystem) plus a gamescope display/input layer, in one tool.

> **Lives in the _making_ repo, operates on the _live_ game.** The script is here
> (`~/Projects/skyrim-headless-mods/skytest/skytest`, symlinked onto `PATH` as `skytest`), but it
> swaps the real game install's `Data/`. The mod-_managing_ repo (`~/Downloads/skyrim-mods/`) still
> depends on that symlink for its manual installs — keep both sides in mind when changing behavior.

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
skytest ready [secs]           # block until the session is in-world (probe poll)
skytest shot [out.png]         # screenshot the session       [cropWxH+X+Y] [scaleWxH]
skytest drive <cmd> …          # inject input (tap|seq|key|click|abs|rel|raw)
skytest stop                   # tear down the session + restore Data → full
skytest play                   # launch the FULL modded game over the fast direct path (blocking)
skytest status                 # show profile + live test session (also the no-arg default)
skytest normal                 # force Data → full (recover after a crash)
skytest uninstall              # revert Data to the original real directory
```

Verb-based CLI (AXI-ergonomic): `skytest help` lists verbs, `skytest <verb> --help` gives
examples, `status`/`init` accept `--json`. Errors carry a `Try:` hint and exit 2.

## Testing a mod is the normal step — reach for it

Building a mod and **not** running it in the engine is the exception, not the other way round.
`skytest test <mod>` is this repo's equivalent of running the test suite: build the artifact →
`skytest test <mod>` → `drive`/probe it → confirm it actually does the thing in-engine. Treat it
as the default close-out for any mod change, the same way you'd run unit tests elsewhere.

Yes, the boot is **~1–2 minutes**. That is expected and worth it — it is the only way to know a
mod works in the real engine, not just that it compiled. Don't skip the test because it "feels
heavy"; the boot cost buys real-engine ground truth, and the session is **detached** so you spend
those minutes doing other things, not blocked at a terminal. The verbs below make reaching for a
test the path of least resistance.

## Drivable test sessions: visible or headless

A **test session always runs the game under gamescope**, so it is always screenshot-able and
drivable. The only knob is visibility:

| Command                         | gamescope backend | Visible?                          | Drivable / shot-able?                  |
| ------------------------------- | ----------------- | --------------------------------- | -------------------------------------- |
| `skytest test <mod>` (default)  | `wayland`         | yes — a window nested in Hyprland | yes                                    |
| `skytest test <mod> --headless` | `headless`        | no — offscreen render             | yes                                    |
| `skytest test <mod> --sdl`      | `sdl`             | yes                               | yes — fallback if `wayland` misbehaves |

Both share one path: **launch → `ready` (poll the probe for in-world) → `drive`/`shot` → `stop`**.
`--headless` swaps the backend string and nothing else, so "watch CC drive while you look over its
shoulder" is the _same machinery_ as a headless test — you just see it.

A test session is **detached**: `test` returns once the game is in-world, so you issue `shot` /
`drive` / `ready` as separate follow-up commands against the live session, then `stop` to end it.
Because the running game holds `Data` open the whole time, the `Data → full` restore happens on
**`stop`**, not on launch-return. (`play`/`normal` keep the old blocking, restore-on-exit path —
they are the daily-driver fast launch, not under gamescope, not drivable; there is no reason to
drive your own playthrough.)

### How the eyes + hands work (folded from the old `headless/` driver)

- **Eyes — `gamescope` + SIGUSR2.** gamescope renders the game on the GPU; `shot` sends it
  `SIGUSR2`, it dumps its composited frame to `/tmp/gamescope_<ts>.avif`, and skytest converts that
  to PNG. (X11 grabbers see only black — the game is Vulkan/DXVK; gamescope's own dump is the capture
  path.)
- **Hands — libei via `gamescope-0-ei`.** `drive` connects a libei client (`eidriver/`, compiled
  C) to gamescope's EIS socket and sends pointer/keyboard events. It's **relative-native** (what
  raw-input Skyrim consumes; raw libei _absolute_ is inert — `abs`/`click` are synthesized from
  relative, measured 1:1), **socket-scoped** (can't reach your real seat), and **isolated** by the
  session. Keyboard is the deterministic path; pointer `click x y` works open-loop.
- **Truth — SkytestProbe.** Injected into every test profile (below). `ready` polls its `status` for
  `inWorld:true`; `drive`+probe gives a full blind test loop (act, then read `trace.jsonl`).

### Safety net (the detached model has no auto-restore)

A detached session can't be covered by an exit trap, so "forgot to `stop`, `Data` stuck on test" is
caught by **visibility**, not trapping:

- `skytest status` reports both the parked profile (`Data → test`) **and** the live session
  (`session  live test session (pid N)`), and when both hold it points at `skytest stop`.
- Every verb that would launch a 2nd game over the session, or unpark `Data → full` out from under
  it (`test`, `setup-save`, `play`, `normal`, `uninstall`), **refuses while a session is live** —
  pointing at `skytest stop`. (Unparking `Data` while the live session still reads `Data → test` is
  the cross-profile crash: SKSE plugins from one profile, ESPs/save from another.)

### Don't run a test while another Skyrim is open

A test session **can't spawn the game if any Skyrim is already running** — Steam blocks a 2nd
instance of the appid, so gamescope comes up empty and `shot` is **black** (and `ready` can falsely
pass, because the probe's `commands.jsonl`/`trace.jsonl` is shared prefix-wide and the _other_ game
answers it — see `docs/headless-findings.md` #13). `test`/`play`/`setup-save` now **refuse up front**
with _"Skyrim is already running (pid N) — close it first"_, so this shouldn't bite you; if it does,
close the game (or kill a stray `SkyrimSE.exe`) and re-run.

Still genuinely open: a clean, game-free run hasn't yet confirmed `shot` captures a real **in-world**
frame under either backend (`headless` and `wayland`) — both verification attempts were blocked by a
running game. Until then, for headless runs lean on the **probe** (`trace.jsonl` via `status` /
`dump` / `watch`) as ground truth. The dead-ends behind the whole display/input layer are in
[`docs/headless-findings.md`](docs/headless-findings.md) — **read it before changing the
gamescope/libei approach.**

## Which mode — `test` or `play`?

Use `skytest test <mod>` for a mod that works **standalone** (a new spell, a DLL, a self-contained
esp). For a mod that only manifests **on top of the live load order** — patches, or overrides of
another mod's assets (e.g. a DBVO `dialoguemenu.swf` edit, which needs DBVO + a voice pack present)
— install into the full profile and test with `skytest play`; the vanilla+1 test profile can't
reproduce it.

## Boot straight into a test save (v2)

Instead of clicking through the main menu each run, `skytest <mod>` can drop you straight into
a prepared save with the character controllable:

1. **Build the base save once:** `skytest setup-save` launches a vanilla+base session. At the
   menu, console `coc qasmoke` (Bethesda's dev hall — instant load, all-items chest, NPC/weapon
   spawn levers), place any fixtures you want shared across tests (e.g. set a vanilla NPC as
   `setplayerteammate 1` for ally-acting mods like GhostAllies), then `save SkytestBase` and quit.
   **Use only vanilla content** so the save loads under any test.
2. **From then on**, `skytest <mod>` injects **powerofthree's Start On Save** (a DLL-only,
   Address-Library-only SKSE plugin) into the test profile, pinned to `SkytestBase`, and the game
   autoloads it on launch. Hold **SHIFT** while the load gear spins to skip into the menu.

Start On Save lives **only in the test profile**, never in vanilla, so `setup-save` stays at the
menu. If no `SkytestBase.ess` exists yet, `skytest <mod>` just launches to the menu (no autoload).
The DLL + ini template live in `base-skse/`; the source archive is in the managing repo's `01-core/`.

**SkytestProbe (runtime debug toolkit).** `skytest <mod>` also injects **SkytestProbe** into the
test profile **unconditionally** (DLL-only/Address-Library-only, like Start On Save but with no
save condition; ini copied verbatim). It's a pre-compiled probe plugin: write JSON commands to
`…/SKSE/skytest/commands.jsonl` and the running game writes structured traces to `trace.jsonl`
in the same dir — arm engine event sinks (`trace`), dump an actor's state incl. collision group
(`dump`), `watch` an actor value, run a console line (`exec`), `anim-trace`, `marker`, `status`;
F11 drops a marker + auto-dump. It kills the probe-recompile-restart loop when debugging the C++
mods, and it is what `skytest ready` polls. Passive until armed, never crashes on bad input. Built
from `../mods/SkytestProbe` (`./build.sh`); skytest reads the DLL + `SkytestProbe.ini` straight from
that build output — the canonical copy, no vendored duplicate. Contract in
`../docs/plans/skytest-probe-design.md`. skytest degrades gracefully if the DLL hasn't been built.

## Notes

- **Everyday play:** `skytest play`, or the **"Skyrim SE (SKSE, fast)"** app-launcher entry
  (`~/.local/share/applications/skyrim-skse-fast.desktop`), launches the full modded game via the
  direct path — much faster than Steam Play. (skytest no longer shells out to `launch-skse.sh`; it
  runs the same `proton run skse64_loader.exe` itself. That script stays as the desktop-entry target.)
- The base save must depend on **vanilla + base SKSE only** — a modded save crashes in the
  vanilla+1 profile. (That is also why autoload is pinned to `SkytestBase`, not "latest save",
  which would grab a modded autosave from the shared prefix Saves folder.)
- Per-mod fixtures beyond a generic baked ally (a tiny test `.esp`, or a console batch run at
  load) are deferred; bake what most tests share into `SkytestBase`. (See `../docs/ideas.md`.)
- Startup is ~2 DLLs instead of the full ~51 (+1 for Start On Save), and it launches via the
  direct proton path (much faster than Steam Play, the real cause of the slow startup).
- A `crash-*.log` on quit-to-desktop is the known vanilla Skyrim shutdown crash, not a fault of
  the mod under test (check the call stack, not just the module list, to confirm).
- Steam blocks a 2nd instance of the appid, so a test session won't spawn the game _over_ a real
  Steam-launched Skyrim (the gamescope scaffolding comes up, the game never does) — `stop` tears
  down by gamescope **session**, leaving a game you're playing in the same prefix alone
  (`docs/headless-findings.md` #12).
- Full flags + rationale are in the script header (`skytest`), and the v2 bring-up history is in
  `../docs/plans/skytest-v2-handoff.md`.

## Layout

| Path                        | Holds                                                                                                                                                                                                                                                              |
| --------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `skytest`                   | The launcher: dispatch + profile/lifecycle + verb impls (self-documenting via `skytest help`). On `PATH` via `~/.local/bin/skytest`.                                                                                                                               |
| `lib/gamescope.sh`          | Sourced gamescope backend: `gs_launch` / `gs_wait_ready` / `gs_shot` / `gs_drive` / `gs_stop` / `gs_session_alive` — launch-under-gamescope, ready-poll, screenshot, libei input, session teardown. Absorbed the old `headless/{launch,ready,shot,drive,stop}.sh`. |
| `eidriver/`                 | The libei input client (`eidriver.c` + `build.sh`, `gcc` + `pkg-config libei-1.0`, no sudo). Compiled binary is gitignored; build it once with `eidriver/build.sh`.                                                                                                |
| `base-skse/`                | The third-party SKSE plugin skytest injects into every test profile: `po3_StartOnSave.{dll,ini.template}` (vendored — no in-repo source). SkytestProbe is _not_ here; skytest reads it straight from `../mods/SkytestProbe/build/`.                                |
| `docs/headless-findings.md` | Every gamescope/libei dead-end and why it's a wall — **read before changing the display/input approach.**                                                                                                                                                          |

## Requirements (all already present on this machine, no root)

`gamescope`, `libei` (+ header at `/usr/include/libei-1.0`, resolved by `pkg-config`), `gcc`,
ImageMagick (`magick`), `jq`. A Skyrim SE + SKSE install (Proton Experimental prefix). `sudo` is
**not** available here — everything is user-space/compiled (`docs/headless-findings.md` #7).
