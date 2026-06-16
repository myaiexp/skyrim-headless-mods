# The headless Papyrus toolchain (tier 1)

How a Skyrim SE mod gets built here without any GUI tools. Two independent halves: the
**plugin** (`.esp`) and the **script** (`.pex`).

## 1. The plugin: Mutagen (`tools/EspGen`)

A `.esp`/`.esm`/`.esl` is a binary record file. For a pure-logic script mod you need exactly
one record: a **Quest** that is _Start Game Enabled_ and has a Papyrus script attached (via
the quest's `VMAD` subrecord), with no properties, no aliases, no masters. The script does
everything itself (`Game.GetPlayer()` etc.).

[Mutagen](https://github.com/Mutagen-Modding/Mutagen) is a .NET library that reads/writes
Bethesda plugins. `EspGen` builds the record in ~10 lines and writes a guaranteed-valid file:

```bash
dotnet run --project tools/EspGen -- <out.esp> <QuestEditorID> <ScriptName> [FullName]
```

This is the headless replacement for SSEEdit's "New → add a Quest → attach script → save".
The output is a tiny (~200 byte) plugin. Mutagen is the same engine the Synthesis patcher
framework is built on, so this scales to real record edits (items, leveled lists, patches),
not just script hosts.

## 2. The script: `PapyrusCompiler.exe` under wine

Papyrus source (`.psc`) compiles to bytecode (`.pex`) with `PapyrusCompiler.exe`, a CLI
tool that ships with the Creation Kit (and bundled inside some mods, e.g. Nemesis). It's a
.NET Framework app; **wine** (with `wine-mono`) runs it fine headlessly.

`tools/compile-papyrus.sh` wraps it:

```bash
tools/compile-papyrus.sh <ScriptName> <src-dir> <out-dir>
```

### The source-tree requirement (the non-obvious part)

To compile _any_ script, the compiler must resolve the **entire transitive type graph** of
everything the script references. `extends Quest` + a call to `Actor.GetEquippedWeapon()`
drags in `Actor` → `GlobalVariable`, `Package`, `Idle`, `Light`, `Projectile`, …, most of
the game's script API. So you need the full vanilla source tree, **plus** SKSE's augmented
versions (for `IsBow()`, `RegisterForControl`, `Debug.SendAnimationEvent`, etc.).

`tools/papyrus-sources/` provides both (the third-party trees are git-ignored, populate them
locally from your own game + SKSE install, see `tools/papyrus-sources/README.md`):

- `vanilla/`: 77 vanilla base API scripts (every native type: `Actor`, `Form`, `Weapon`,
  `GlobalVariable`, `Light`, …). The other ~1200 vanilla scripts are quest/scene logic that
  type resolution never needs. *(Bethesda IP, git-ignored.)*
- `skse/`: 62 SKSE source scripts (the ones SKSE extends). *(SKSE IP, git-ignored.)*
- `TESV_Papyrus_Flags.flg`: the user-flags file the compiler requires. *(Bethesda IP, git-ignored.)*
- `skyui/`: SkyUI MCM base classes. *(Open-source, committed.)*

**Import order matters** and is set in `compile-papyrus.sh`: `mod-src ; skse ; vanilla`. The
mod's own source wins first; SKSE versions override vanilla for the scripts SKSE extends (so
SKSE-only functions resolve); vanilla fills everything else.

## 3. Extracting game assets: Mutagen (`tools/BsaExtract`)

Vanilla assets live in `.bsa` archives. Mutagen reads them too, so when you need a real
reference (e.g. the exact control-event names in `interface/controls/pc/controlmap.txt`),
extract it instead of guessing:

```bash
dotnet run --project tools/BsaExtract -- "<Skyrim - Interface.bsa>" controlmap /tmp/controlmap.txt
```

## How the pieces were obtained

- **.NET 8 SDK** → `dotnet-install.sh` into `~/.dotnet` (no root).
- **Mutagen** → NuGet (`Mutagen.Bethesda.Skyrim`).
- **PapyrusCompiler.exe + DLLs** → copied from a Skyrim install (here, the one bundled with
  Nemesis) into `tools/papyrus-compiler/`.
- **vanilla source stubs** → the `SkyrimSE/vanilla` set from the `BellCubeDev/papyrus-index`
  repo (the base API subset, not the full 1300-file dump).
- **SKSE sources** → extracted from the SKSE64 archive's `Data/Scripts/Source/`.
