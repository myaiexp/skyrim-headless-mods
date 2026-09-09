# skyrim-headless-mods

Making Skyrim Special Edition mods **headlessly on Linux**: no SSEEdit, no Creation Kit, no GUI tooling at all. Plugins are authored in code, scripts are compiled from the command line, and everything is debuggable from the Papyrus log.

This repo holds a reusable toolchain, the mods built with it, and a **drivable test launcher**
(`skytest/`) that runs each mod in isolation (visible or headless) and drives it hands-free
(screenshot, inject input, poll engine state). The flagship working mod is
**[GhostAllies](mods/GhostAllies/)**, built on the SKSE C++ tier and verified in-game: player
arrows and aimed spells pass harmlessly through your whole party — and your own summons.

## Which Skyrim this targets

Skyrim **1.7.104** (Bethesda's 2026-09-01 patch), with **SKSE 2.3.1** and **Address Library v13**.
All six SKSE plugins here are built against
[`alandtse/CommonLibSSE-NG`](https://github.com/alandtse/CommonLibSSE-NG) v7.1.0, which is what lets
one DLL load on SE, AE and 1.7.x alike (CharmedBaryon's original upstream is abandoned and misfiles
1.7.x as pre-AE, so don't repin to it — see `docs/skse-toolchain.md`).

"Verified" means different things per mod on that runtime, and the distinction is deliberate:

- **Functionally verified in-engine on 1.7.104**: OneClickTravel, DBVODialogueTweaks.
- **Load-only on 1.7.104 so far**: AutoFireBow, AutoCastSpell, GhostAllies. Each was functionally
  verified on 1.6.1170 and loads cleanly on the new runtime, but its behavior has not been re-run
  there yet.

The runtime is global, so the game cannot sit on 1.6.1170 and 1.7.104 at once. `skytest status`
prints which one is live, and every launch verb checks it before doing anything.

## Why

The "normal" way to make even a trivial script mod is: fight SSEEdit to hand-build a plugin record, then run the Creation Kit's compiler. SSEEdit is cryptic, the CK is heavy, and both are GUI tools that don't fit a scripted/automated workflow. None of it is actually required:

- A plugin (`.esp`) is just a record file. You can build it in code with **Mutagen** (C#/.NET).
- A Papyrus script (`.psc` → `.pex`) compiles with **`PapyrusCompiler.exe`**, a CLI tool, which runs fine under **wine**.

So the whole pipeline is command-line, reproducible, and version-controllable.

## What's here

| Path                             | What                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| -------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `skytest/`                       | **Fast isolated, drivable mod-test launcher.** Swaps the live game's `Data/` between symlink profiles (vanilla / vanilla+1-mod / full) for interference-free testing, injects SkytestProbe + Start On Save, **isolates the Saves folder** (only `SkytestBase` visible, so the autoload boots straight in instead of grabbing a real modded save — though the bundled Start On Save 2.7.0.1 cannot load on 1.7.104, so boot-into-save is currently unavailable and `SKYTEST_NO_AUTOLOAD=1` boots to the menu instead), then runs the mod under **gamescope** as a detached, drivable test session: screenshot it (SIGUSR2→AVIF), inject isolated **libei** input, poll for in-world, **visible** (default) or **`--headless`**. Absorbed the old `headless/` driver (2026-06-12). Manages the live `Data/` symlink the _managing_ repo (`~/Downloads/skyrim-mods/`) relies on. See `skytest/README.md`. |
| `tools/EspGen/`                  | Mutagen program that generates a "script-host" `.esp` (one Start-Game-Enabled quest hosting a Papyrus script). Reusable for any pure-logic script mod.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| `tools/BsaExtract/`              | Mutagen program to extract files from a `.bsa` (used to pull `controlmap.txt`, etc.).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| `tools/papyrus-compiler/`        | `PapyrusCompiler.exe` + DLLs (Bethesda CK), run via wine. Git-ignored, so populate locally; see that dir's `README.md`.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| `tools/papyrus-sources/`         | The vanilla + SKSE Papyrus **source** trees the compiler needs for type resolution. The third-party ones (vanilla, SKSE, `.flg`) are git-ignored, so populate locally; see that dir's `README.md`.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| `tools/compile-papyrus.sh`       | Generic `.psc` → `.pex` wrapper around the compiler + sources.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| `tools/env.sh`                   | Machine paths (dotnet, game install, wine prefix). Edit to match your setup.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| `tools/ghidra/`                  | **Headless Ghidra RE tier.** Disassemble `SkyrimSE.exe` to find non-virtual hook seams the Address-Library tier can't reach (analyse once, query many; PyGhidra in a venv). Run via `tools/ghidra/ghidra.sh`; see `docs/ghidra.md`.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| `tools/nexus`                    | Read-only Nexus Mods API CLI — mod release-status / stats checker. See `docs/nexus-api.md`.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| `mods/RapidBowHold/`             | First mod (Papyrus). Proof-of-concept that validated the toolchain, but it hit a hard engine limit (see below).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| `mods/DBVODialogueTweaks/`       | **Working swf + SKSE C++ mod (v1.1.1), reply timing re-verified in-engine on 1.7.104 — but for DBVO 1.x only, superseded by DBVO 2, and DBVO 1.x's own dependencies need their updated builds on 1.7.104 (verified working there 2026-09-09).** Pacing/control tweaks for Dragonborn Voice Over: the NPC reply fires when your voiced line actually ends (an SKSE plugin watches the line's audio) instead of DBVO's word-count guess, plus manual line-skip, clean audio cuts on skip/interrupt, and a player-voice volume slider, all from a SkyUI MCM. Recompiles DBVO's `dialoguemenu.swf` (ffdec) and ships a CommonLibSSE-NG DLL. DBVO 2 (2026) dropped the swf + Papyrus stack for a native DLL and absorbed every feature here except the clean audio cut on skip — and this mod **softlocks dialogue** if installed over it. The verification is a script: `mods/DBVODialogueTweaks/replyonlineend.steps` replays it headlessly and gates on the swf's own state (reply not yet fired past the backstop, then fired at line-end + the configured gap). Since v1.1.1 the installer also offers **four UI-overhaul menu styles** (Untarnished UI, Dear Diary Dark Mode white/warm, NORDIC UI) — that overhaul's own DBVO-patched swf with our deltas three-way-merged onto it, so a user keeps their layout; all four verified in-engine with the same script (`mods/DBVODialogueTweaks/variants/README.md`). Mod `README.md`; phased design in `docs/plans/dbvo-*`; the supersede analysis in `docs/plans/dbvo-v2-compatibility-analysis.md`.                                                                                                                                                                                                                                 |
| `mods/AutoFireBow/`              | **Working SKSE C++ mod (v2.1.0), AE-tested.** Hold attack with a bow to auto-fire continuously; every auto shot looses at a **genuine full draw** (synthetic input-release through the engine's own pipeline — no power clamp), with a small auto-only DPS bump and a **SkyUI MCM** (master toggle, hotkey, damage + cadence sliders). Full write-up (mechanisms, limitations, build) in the mod **`README.md`**; design in `docs/plans/autofirebow-{mcm,real-charge}-design.md`.                                                                                                                                                                                                                                                  |
| `mods/AutoCastSpell/`            | **Working SKSE C++ mod (v1.0.7), verified in-engine.** Hold a cast control with a **fire-and-forget** spell → auto-fires the instant it's fully charged (no release timing), then auto-recasts in a loop until released. Per hand, independent (hold both to dual-cast). The spell analog of AutoFireBow, driven by polling `RE::MagicCaster::state` for `kReady` (no "spell charged" anim event exists). Full write-up (mechanism, the log-flush pacing gotcha, build) in the mod **`README.md`**; design in `docs/plans/autocastspell-{design,plan}.md`.                                                                                                                                                                         |
| `mods/GhostAllies/`              | **The flagship: working SKSE C++ mod (v0.10.0), verified in-game.** Player arrows + aimed spells pass _through_ your whole party **and your own summons** (conjured atronachs/familiars, reanimated thralls) to hit the enemy behind; the player's hostile magic deals no friendly damage to teammates. Full write-up (mechanisms, limitations, build) in the mod **`README.md`**; design in `docs/plans/ghost-allies-{design,v2-plan}.md`.                                                                                                                                                                                                                                                                                        |
| `mods/OneClickTravel/`           | **Working SKSE C++ mod (v1), verified in-game on 1.7.104.** Click a _discovered_ map marker → instant fast-travel, no confirmation box; every other map box passes through 100% vanilla. A MinHook entry detour of `MessageBoxData::QueueMessage` suppresses the fast-travel confirm before it renders (no flash). The verification is a script: `mods/OneClickTravel/oneclick.steps` replays it headlessly and gates on the player's cell changing. Design/plan: `docs/plans/oneclick-travel-{design,plan}.md`.                                                                                                                                                                                                                                                                                                                                                                |
| `mods/SkytestProbe/`             | **Working SKSE C++ debug toolkit (v0.4.0).** A pre-compiled, runtime-armed probe plugin that kills the probe-recompile-restart loop: CC writes JSON commands to `commands.jsonl`, the running game writes structured traces to `trace.jsonl` (both under the SKSE log dir's `skytest/`). Passive until armed, never crashes on bad input; `skytest` injects it into every test profile (validated headless in the full ~40-mod profile). Full **command reference** (`trace`/`dump`/`watch`/`give-spell`/`set-av`/`mcm-get`/`speak-watch`/`ui-invoke`/`ui-set`/`ui-get`/…) and the `exec`/CompileAndRun caveat are in the mod **`README.md`**; contract in `docs/plans/skytest-probe-design.md`.                                                                               |
| `docs/papyrus-toolchain.md`      | How the headless Papyrus (tier-1) toolchain works, end to end.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| `docs/papyrus-workflow.md`       | Papyrus build / install / **iterate**, including the non-obvious gotchas that waste hours.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| `docs/papyrus-limits.md`         | **What Papyrus can't do**: the bow-charge wall, with evidence. Read before trying anything input/engine-coupled.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| `docs/skse-tier-bringup.md`      | Headless **SKSE C++** tier bring-up (CommonLibSSE-NG cross-compiled on Linux) for engine-level control Papyrus can't reach. **Done**: realized across the SKSE C++ mods (AutoFireBow, AutoCastSpell, GhostAllies, OneClickTravel, SkytestProbe); kept as historical reference.                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| `docs/ghidra.md`                 | The headless Ghidra RE tier: how to disassemble `SkyrimSE.exe` for hook seams the Address-Library tier can't reach (`tools/ghidra/ghidra.sh`, PyGhidra in a venv).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| `docs/nexus-api.md`              | The Nexus Mods read-only API + `tools/nexus` (mod release-status / stats checker).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| `docs/skse-toolchain.md`         | The **SKSE C++ cross-compile** toolchain in detail: clang-cl + lld-link + xwin, the CommonLibSSE-NG pin, and the traps (delayed template parsing, case-sensitive import libs).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| `docs/dbvo-landscape.md`         | The three DBVO frameworks (1.x, DBVO 2, Dragonborn ReVoiced) and where `mods/DBVODialogueTweaks` stands between them. Read before proposing any DBVO work.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| `docs/ideas.md`                  | Deferred features and tech debt: what is worth building next, and what is knowingly left broken.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| `docs/*-nexus-page.md`, `docs/dbvo-page.bbcode` | **Release messaging** for the published mods: what to say on the Nexus page and why. `autofirebow-nexus-page.md` and `oneclicktravel-nexus-page.md` are draft outlines; `dbvo-page.bbcode` is the live page copy.                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |

## Three tiers of headless modding

Each tier is a different surface of the game, with its own headless toolchain:

1. **Papyrus** (`tools/`): data edits and gameplay-logic scripts. Mutagen for `.esp`, wine +
   PapyrusCompiler for `.pex`. The easy layer; great until you need engine internals. Realized in
   `mods/RapidBowHold/` and the SkyUI MCM scripts `mods/DBVODialogueTweaks/` ships. See
   `docs/papyrus-toolchain.md`; its hard limits in `docs/papyrus-limits.md`.
2. **SKSE C++** (`tools/skse/`): a native DLL with full engine access, for what Papyrus
   fundamentally can't reach. Cross-compiled Linux → Windows with **clang-cl + lld-link + xwin**
   (no MSVC, no vcpkg); CommonLibSSE-NG via FetchContent. **Working and verified in-game** across
   all six SKSE plugins here: `mods/AutoFireBow`, `AutoCastSpell`, `GhostAllies`, `OneClickTravel`,
   `SkytestProbe`, and the DLL half of `DBVODialogueTweaks`. See `docs/skse-toolchain.md` /
   `docs/skse-tier-bringup.md`.
3. **Scaleform / swf** (per-mod, via **ffdec**): the game's Flash UI layer. Recompile a menu's
   ActionScript headlessly with JPEXS ffdec (`-importScript`), no Flash IDE. For UI behavior that
   lives in the `.swf` rather than in Papyrus or native code. Realized in
   `mods/DBVODialogueTweaks/`, where the recompiled `dialoguemenu.swf` pairs with an SKSE DLL.

Tiers 1 → 2 are a capability escalation. The RapidBowHold saga proved tier 1's limit: a scripted
full-power rapid bow is impossible in Papyrus because arrow charge is welded to real input. That is
exactly what `mods/AutoFireBow/` then did in native code. Tier 3 isn't higher on that axis; it's a
different surface, reached when the behavior you want lives in the Flash UI.

## Prerequisites (one-time, no root)

**For the Papyrus tier** (`.esp` + `.pex`):

- **.NET 8 SDK** at `~/.dotnet`: `curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 8.0 --install-dir ~/.dotnet`
- **wine** (`wine-mono` runs the .NET-based compiler)

**For the SKSE C++ tier** (the native DLLs, and the tier most of this repo now lives on) — full
setup and the traps are in `docs/skse-toolchain.md`:

- **`clang` / `clang-cl`** from your distro, plus **`lld-link`** and the LLVM binutils, at a version
  matching your `clang`. On Arch without root these are extracted from the pacman tarballs into
  `~/.local/llvm-extra` rather than installed.
- **`xwin`**, which downloads and repacks Microsoft's redistributable CRT + Windows SDK
  (`xwin splat`, ~640 MB) so there is no MSVC and no Windows install anywhere in the loop.
- **CMake + Ninja.** CommonLibSSE-NG, spdlog and rapidcsv are pulled by FetchContent, pinned, and
  built from source, so there is no vcpkg either.

**To run any of it**: a Skyrim SE install + **SKSE**, matched to your game build. Only needed to
_run_ the mods and for the `--install` paths, not to build.

## Quick start

```bash
# Tier 1: build the example Papyrus mod (esp + pex) into mods/RapidBowHold/build/
./mods/RapidBowHold/build.sh

# ...and install it into the live game, activating the plugin
./mods/RapidBowHold/build.sh --install
```

For Papyrus, **fully restart Skyrim** afterwards and, on an existing save, kick the quest from the
console; `docs/papyrus-workflow.md` explains why.

```bash
# Tier 2: cross-compile an SKSE plugin, Linux -> Windows PE
./mods/GhostAllies/build.sh --install

# Then test it in isolation: vanilla + this one mod, in a drivable session.
# `test` takes a path to the mod dir or the DLL itself, not a bare name.
./skytest/skytest test mods/GhostAllies

# The A/B control: the identical rig and save with nothing under test
./skytest/skytest test --vanilla
```

`skytest test` swaps the game's `Data/` to a vanilla+1 profile and launches a detached, drivable
session you can screenshot, inject input into, and poll for engine state. Once you have driven a
setup by hand, persist it as a `.steps` file and `skytest replay` it as a regression test. See
`skytest/README.md`.

## Sources note

The Papyrus compiler needs the vanilla + SKSE base API source trees. The third-party ones
(Bethesda's vanilla stubs + `.flg`, SKSE's sources) are **not redistributed here**. They're
git-ignored, and you populate them locally from your own game + SKSE install. See
`tools/papyrus-sources/README.md`. (SkyUI's MCM base classes are open-source and stay committed.)

## License

This repo's own code is **0BSD** (see [`LICENSE`](LICENSE)). It vendors a few third-party assets that
keep their own separate terms and credits:

- **nlohmann/json** — MIT (header-only JSON, used by the SKSE C++ tier).
- **SkyUI MCM sources** — by the SkyUI team; the open-source MCM base classes, redistributed under
  their terms.
- **Start On Save** ([Nexus](https://www.nexusmods.com/skyrimspecialedition/mods/50054)) — by
  **powerofthree**, bundled in `skytest/base-skse/` by permission (powerofthree's permissive
  reuse-with-credit terms).
- **DBVO `dialoguemenu.swf` / `.as`** — by **MathiewMay** (Dragonborn Voice Over), recompiled and
  redistributed with the author's permission. See `mods/DBVODialogueTweaks/README.md`.
- **UI-overhaul `dialoguemenu.swf`** — the DBVO-patched menus for **Untarnished UI** (Vor),
  **Dear Diary Dark Mode** (uranreactor) and **NORDIC UI** (outobugi), which that mod's
  compatibility variants are built on. Committed under each mod's own permissions (checked
  2026-09-09); the menu designs are their authors' work. See
  `mods/DBVODialogueTweaks/variants/README.md`.

All other Bethesda/SKSE files the toolchain needs (vanilla stubs, `.flg`, the CK Papyrus compiler)
are **not** redistributed here: they are git-ignored and populated locally from your own install.
