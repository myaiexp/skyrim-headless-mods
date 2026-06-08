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
| `mods/RapidBowHold/` | First mod: hold the attack button with a bow to auto-fire full-strength shots. |
| `docs/toolchain.md` | How the headless toolchain works, end to end. |
| `docs/workflow.md` | Build / install / **iterate** — including the non-obvious gotchas that waste hours. |

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
