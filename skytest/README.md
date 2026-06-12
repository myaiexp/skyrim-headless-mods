# skytest — fast isolated Skyrim SE mod-test launcher

`skytest` launches Skyrim with **only one mod active** on an otherwise vanilla+AE baseline —
for fast, interference-free testing of mods built in this repo. It is the profile-swapping
core of a mod manager, done with plain symlinks (no virtual filesystem).

> **Lives in the *making* repo, operates on the *live* game.** The script is here
> (`~/Projects/skyrim-headless-mods/skytest/skytest`, symlinked onto `PATH` as `skytest`), but it
> swaps the real game install's `Data/`. The mod-*managing* repo (`~/Downloads/skyrim-mods/`) still
> depends on that symlink for its manual installs — keep both sides in mind when changing behavior.

How it works: the live `Data/` is a **symlink** to `.profiles/full` (the real ~40-mod setup,
renamed once, never modified). A test run retargets that symlink to `.profiles/test`
(= vanilla BSAs/plugins + Address Library + CrashLogger + the one mod), launches SKSE directly,
and restores `Data -> full` when the game exits. The profiles live next to the game at
`<game>/.profiles/` (**not** in this repo).

    skytest init [--commit]     # one-time: turn Data into a symlink, build vanilla profile
    skytest setup-save          # one-time: launch vanilla to BUILD the base test save
    skytest test <mod-dir|file> # launch vanilla + that mod; boots into the base save if present
    skytest <mod-dir|file>      #   ^ bare shortcut, identical to `test`
    skytest play                # launch the FULL modded game over the fast direct path
    skytest status              # show current profile (also the no-arg default)
    skytest normal              # force Data -> full (recover after a crash)
    skytest uninstall           # revert Data to the original real directory

**Which mode?** Use `skytest test <mod>` for a mod that works **standalone** (a new spell, a
DLL, a self-contained esp). For a mod that only manifests **on top of the live load order** —
patches, or overrides of another mod's assets (e.g. a DBVO `dialoguemenu.swf` edit, which needs
DBVO + a voice pack present) — install into the full profile and test with `skytest play`; the
vanilla+1 test profile can't reproduce it.

Verb-based CLI (AXI-ergonomic): `skytest help` lists verbs, `skytest <verb> --help` gives
examples, `status`/`init` accept `--json`. Errors carry a `Try:` hint and exit 2.

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
mods. Passive until armed, never crashes on bad input. Built from `../mods/SkytestProbe`
(`./build.sh`); skytest reads the DLL + `SkytestProbe.ini` straight from that build output — the
canonical copy, no vendored duplicate. Contract in `../docs/plans/skytest-probe-design.md`.
skytest degrades gracefully if the DLL hasn't been built.

## Notes

- **Everyday play:** `skytest play`, or the **"Skyrim SE (SKSE, fast)"** app-launcher entry
  (`~/.local/share/applications/skyrim-skse-fast.desktop`), launches the full modded game via the
  direct path — much faster than Steam Play.
- The base save must depend on **vanilla + base SKSE only** — a modded save crashes in the
  vanilla+1 profile. (That is also why autoload is pinned to `SkytestBase`, not "latest save",
  which would grab a modded autosave from the shared prefix Saves folder.)
- Per-mod fixtures beyond a generic baked ally (a tiny test `.esp`, or a console batch run at
  load) are deferred; bake what most tests share into `SkytestBase`. (See `../docs/ideas.md`.)
- Startup is ~2 DLLs instead of the full ~51 (+1 for Start On Save), and it launches via the
  direct `launch-skse.sh` path (much faster than Steam Play, the real cause of the slow startup).
- A `crash-*.log` on quit-to-desktop is the known vanilla Skyrim shutdown crash, not a fault of
  the mod under test (check the call stack, not just the module list, to confirm).
- Full flags + rationale are in the script header (`skytest`), and the v2 bring-up history is in
  `../docs/plans/skytest-v2-handoff.md`.

## Layout

| Path | Holds |
|------|-------|
| `skytest` | The launcher (626-line bash; self-documenting via `skytest help`). On `PATH` via `~/.local/bin/skytest`. |
| `base-skse/` | The third-party SKSE plugin skytest injects into every test profile: `po3_StartOnSave.{dll,ini.template}` (vendored — no in-repo source). SkytestProbe is *not* here; skytest reads it straight from `../mods/SkytestProbe/build/` (the canonical build output). |
