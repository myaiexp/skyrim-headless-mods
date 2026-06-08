# skyrim-headless-mods

Making Skyrim Special Edition mods **headlessly on Linux** — no SSEEdit, no Creation Kit, no GUI tooling at all. Plugins are authored in code, scripts are compiled from the command line, and everything is debuggable from the Papyrus log.

This repo holds a reusable toolchain plus the mods built with it.

## Why

The "normal" way to make even a trivial script mod is: fight SSEEdit to hand-build a plugin record, then run the Creation Kit's compiler. SSEEdit is cryptic, the CK is heavy, and both are GUI tools that don't fit a scripted/automated workflow. None of it is actually required:

- A plugin (`.esp`) is just a record file — you can build it in code with **Mutagen** (C#/.NET).
- A Papyrus script (`.psc` → `.pex`) compiles with **`PapyrusCompiler.exe`**, a CLI tool, which runs fine under **wine**.

So the whole pipeline is command-line, reproducible, and version-controllable.

## What's here

| Path | What |
|------|------|
| `tools/EspGen/` | Mutagen program that generates a "script-host" `.esp` (one Start-Game-Enabled quest hosting a Papyrus script). Reusable for any pure-logic script mod. |
| `tools/BsaExtract/` | Mutagen program to extract files from a `.bsa` (used to pull `controlmap.txt`, etc.). |
| `tools/papyrus-compiler/` | `PapyrusCompiler.exe` + DLLs, run via wine. Self-contained. |
| `tools/papyrus-sources/` | The vanilla + SKSE Papyrus **source** trees the compiler needs for type resolution. |
| `tools/compile-papyrus.sh` | Generic `.psc` → `.pex` wrapper around the compiler + sources. |
| `tools/env.sh` | Machine paths (dotnet, game install, wine prefix). Edit to match your setup. |
| `mods/RapidBowHold/` | First mod (Papyrus). Proof-of-concept that validated the toolchain — but hit a hard engine limit (see below). |
| `plugins/GhostAllies/` | **Working SKSE C++ mod.** Player arrows pass *through* a follower to hit the enemy behind (stamps the follower's Havok systemGroup onto the arrow's cast phantom at launch). v1: single follower, arrows only. See `docs/plans/ghost-allies-{design,plan}.md`. |
| `docs/toolchain.md` | How the headless Papyrus toolchain works, end to end. |
| `docs/workflow.md` | Build / install / **iterate** — including the non-obvious gotchas that waste hours. |
| `docs/findings-papyrus-limits.md` | **What Papyrus can't do** — the bow-charge wall, with evidence. Read before trying anything input/engine-coupled. |
| `docs/skse-plugin-plan.md` | **Next phase:** headless **SKSE C++** (CommonLibSSE-NG cross-compiled on Linux) for engine-level control Papyrus can't reach. |

## Two tiers of headless modding

1. **Papyrus** (working, this repo's `tools/`) — data edits and logic scripts. Mutagen for
   `.esp`, wine + PapyrusCompiler for `.pex`. Great until you need engine internals.
2. **SKSE C++** (toolchain working, `plugins/`) — a native DLL with full engine access, for
   things Papyrus fundamentally can't (e.g. bow draw charge). Cross-compiled Linux → Windows DLL
   with **clang-cl + lld-link + xwin** (no MSVC, no vcpkg); CommonLibSSE-NG via FetchContent.
   `plugins/AutoFireBow/` builds a valid, loadable SKSE hello-world DLL today. See
   `docs/skse-toolchain.md`. Next: RE the bow charge and actually hook it
   (`docs/skse-plugin-plan.md`).

The RapidBowHold saga proved tier 1's limit: a scripted full-power rapid bow is impossible in
Papyrus because arrow charge is welded to real input. That's what pushes us to tier 2.

## Prerequisites (one-time, no root)

- **.NET 8 SDK** at `~/.dotnet` — `curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 8.0 --install-dir ~/.dotnet`
- **wine** (`wine-mono` runs the .NET-based compiler)
- A Skyrim SE install + **SKSE** (only needed to *run* the mods, and for the live `--install` path)

## Quick start

```bash
# Build the example mod (esp + pex) into mods/RapidBowHold/build/
./mods/RapidBowHold/build.sh

# Build and install into the live game + activate the plugin
./mods/RapidBowHold/build.sh --install
```

Then **fully restart Skyrim** and (for an existing save) kick the quest from the console — see `docs/workflow.md` for why.

## Sources note

`tools/papyrus-sources/` contains Bethesda's vanilla Papyrus API stubs and SKSE's script sources. They're here so the repo is self-contained for compiling. This repo is **private** for that reason — don't make it public without removing/regating those.
