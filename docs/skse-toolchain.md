# The headless SKSE C++ toolchain (tier 2)

How a native **SKSE plugin** (a Windows `.dll`) gets built here entirely on Linux: no
Windows, no Visual Studio, no MSVC, no vcpkg. This is the second tier of headless modding
(tier 1 = Papyrus, see [papyrus-toolchain.md](papyrus-toolchain.md)); it exists because some engine behaviour
is unreachable from Papyrus (see [papyrus-limits.md](papyrus-limits.md)).

**Status: working, and shipping a real hook.** `mods/AutoFireBow/` builds a valid SKSE-shaped
`AutoFireBow.dll` against CommonLibSSE-NG, fully cross-compiled, and it loads in-game. It now
implements option 1: a vtable hook on `ArrowProjectile::GetPowerSpeedMult` that forces full bow
charge for the player (verified in-game on 1.6.1170). See
[skse-tier-bringup.md](skse-tier-bringup.md) for the charge → power details.

## The idea

A SKSE plugin is an x86-64 PE DLL using the MSVC C++ ABI. You don't need MSVC to produce that:
`clang-cl` (Clang's MSVC-compatible driver) emits MSVC-ABI objects, `lld-link` links them into a
PE, and the Windows SDK + CRT headers/libs come from `xwin` (which downloads and repacks
Microsoft's redistributable SDK, no Windows install). CommonLibSSE-NG is pulled and built from
source by CMake's FetchContent, so there's no vcpkg either.

```
 .cpp ──clang-cl (--target=x86_64-pc-windows-msvc)──▶ .obj (MSVC ABI)
                       │ uses xwin's CRT + SDK headers (/imsvc)
 CommonLibSSE-NG ──────┤ (FetchContent: built the same way into CommonLibSSE.lib)
 spdlog ───────────────┤ (FetchContent)
                       ▼
                  lld-link  ──uses xwin's CRT + SDK libs (/libpath)──▶  AutoFireBow.dll (PE32+)
```

## Components (and how each was obtained, no root)

| Piece                                                        | What                                         | Source                                                                                                                                                                                                         |
| ------------------------------------------------------------ | -------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `clang` / `clang-cl`                                         | compiler driver, MSVC mode                   | system `clang` package (already installed)                                                                                                                                                                     |
| `lld-link`, `llvm-rc`, `llvm-lib`, `llvm-dlltool`, `llvm-mt` | PE linker + MSVC-style tools                 | `lld` + `llvm` Arch packages, **extracted (no root)** into `~/.local/llvm-extra`. They link against the already-installed `llvm-libs`, so their version **must match** `clang` (here 22.1.5). See note below. |
| `xwin`                                                       | downloads/repacks the MSVC CRT + Windows SDK | prebuilt binary from GitHub releases → `~/.local/bin/xwin`                                                                                                                                                     |
| Windows SDK + CRT                                            | headers + import libs                        | `xwin splat` → `~/.local/xwin-sdk` (x86_64, desktop variant, ~640 MB)                                                                                                                                          |
| CommonLibSSE-NG, spdlog, rapidcsv                            | the SKSE library + its deps                  | CMake FetchContent (pinned), built from source                                                                                                                                                                 |

### Why `lld`/`llvm` are extracted, not pacman-installed

No passwordless sudo in the headless session. Pacman packages are just zstd tarballs, so the
`lld` and `llvm` packages (pinned to the exact installed `clang`/`llvm-libs` version, pulled from
`archive.archlinux.org`) were extracted into `~/.local/llvm-extra/usr/{bin,lib}`. `lld-link`
needs the `liblld*.so` from that prefix's `lib/`, which is why `cross-env.sh` puts it on
`LD_LIBRARY_PATH`. If the system `clang`/`llvm-libs` is later upgraded, re-extract matching
`lld`/`llvm` (mismatched versions won't load).

## The two non-obvious fixes

Building MSVC-targeted C++ on Linux with Clang hits two issues that the toolchain handles:

1. **`-fdelayed-template-parsing`** (set in `cmake/clang-cl-msvc.cmake`). CommonLibSSE-NG has a
   few template methods with typos / missing members that are **never instantiated**. MSVC
   never compiles uninstantiated template bodies, so it never sees them. Clang does conforming
   current-instantiation name lookup at _parse_ time and rejects them. This flag defers body
   parsing to instantiation (MSVC semantics), so the dead code is never checked.

2. **PascalCase `.lib` symlinks** (`setup-sdk-symlinks.sh`, run automatically by `cross-env.sh`).
   `xwin` splats import libs under the exact casings in MS's manifest (`advapi32.lib`,
   `AdvAPI32.Lib`, …). CommonLibSSE references them PascalCase with a lowercase extension
   (`Advapi32.lib`, `Dbghelp.lib`, `Ole32.lib`, `Version.lib`). `lld-link` is case-sensitive on
   Linux, so the script adds the missing "capitalize-first-letter + `.lib`" symlink for every
   SDK lib. Idempotent, stamped at `$XWIN_SDK/.case-symlinks-done`.

## Files

| Path                                 | Role                                                                                                                                                             |
| ------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `tools/skse/cross-env.sh`               | Sourced before building. Puts the LLVM cross tools on `PATH`/`LD_LIBRARY_PATH`, exports `XWIN_SDK`, ensures the `.lib` symlinks exist.                           |
| `tools/skse/setup-sdk-symlinks.sh`      | Creates the PascalCase `.lib` symlinks in the xwin SDK (called by `cross-env.sh`).                                                                               |
| `tools/skse/cmake/clang-cl-msvc.cmake`  | CMake toolchain file: sets the compiler/linker/ar/rc, the `/imsvc` include dirs, the `/libpath` lib dirs, `-fdelayed-template-parsing`, and cross-find settings. |
| `mods/AutoFireBow/CMakeLists.txt` | FetchContent for spdlog (`OVERRIDE_FIND_PACKAGE`), rapidcsv (header, fed via `RAPIDCSV_INCLUDE_DIRS`), and CommonLibSSE-NG (pinned); builds `AutoFireBow.dll`.   |
| `mods/AutoFireBow/src/main.cpp`   | The plugin: declarative `SKSEPluginInfo` + `SKSEPluginLoad`, plus the vtable hook forcing full bow charge for the player.                                        |
| `mods/AutoFireBow/build.sh`       | One-shot configure + build (`--install` copies the DLL into the live game's `Data/SKSE/Plugins`).                                                                |

## Build it

```bash
cd mods/AutoFireBow
./build.sh            # -> build/AutoFireBow.dll (PE32+, x86-64)
./build.sh --install  # also copy into the game's Data/SKSE/Plugins
```

First configure clones CommonLibSSE-NG + spdlog + rapidcsv (~3 min); after that, FetchContent
is cached in `build/_deps` and rebuilds are incremental.

## One-time setup (if starting on a fresh machine)

1. **LLVM cross tools** → extract matching-version `lld` + `llvm` Arch packages into
   `~/.local/llvm-extra` (see "Why extracted" above), or `sudo pacman -S lld llvm` if you have root.
2. **xwin** → download the prebuilt binary to `~/.local/bin/xwin`, then:
   ```bash
   xwin --accept-license --arch x86_64 --cache-dir ~/.cache/xwin splat --output ~/.local/xwin-sdk
   ```
3. That's it: `cross-env.sh` wires the rest and creates the `.lib` symlinks on first build.

## Verifying a built DLL

```bash
source tools/skse/cross-env.sh
llvm-readobj --coff-exports mods/AutoFireBow/build/AutoFireBow.dll   # expect SKSEPlugin_{Load,Query,Version}
file mods/AutoFireBow/build/AutoFireBow.dll                          # expect PE32+ ... (DLL), x86-64
```

A loadable-DLL smoke test under wine (`LoadLibraryA` + `GetProcAddress` on the exports) confirms
the imports resolve. In-game loading needs SKSE + the Address Library installed.

## The CommonLibSSE-NG pin is stale, and 1.7.104 broke it (2026-09-01)

Every mod here pins `https://github.com/CharmedBaryon/CommonLibSSE-NG` at
`b93280e832f263dbef44e44cbe2936622a02f91a` (in each `mods/*/CMakeLists.txt`,
`mods/DBVODialogueTweaks/plugin/CMakeLists.txt` for that one). **That repo is abandoned**: the
pinned commit *is* the tip of its `main` (2024-09-03), and its newest tag, `v3.7.0`, is from 2023.

Skyrim **1.7.104.0** (Steam, 2026-09-01) is the first runtime that pin cannot handle. The reason is
one line in `include/REL/Module.h`:

```cpp
switch (_version[1]) {           // the minor version digit
    case 4:  _runtime = Runtime::VR;  break;
    case 6:  _runtime = Runtime::AE;  break;
    default: _runtime = Runtime::SE;          // <- 1.7.x lands here
}
```

1.7.104 continues the AE lineage but bumped the *minor* version, so an exact match on `6` sends it
to the `SE` branch. Two things then go wrong, neither of them a clean load failure:

- `REL::IDDatabase::load()` (`include/REL/ID.h`) picks `Data/SKSE/Plugins/version-{v}.bin` for SE
  instead of `versionlib-{v}.bin` for AE — and Address Library only ships the AE-style file for
  1.7.104, so every Address-Library lookup misses.
- The `RE::` struct layouts and the `ENABLE_SKYRIM_AE` paths compile to the pre-AE variants, so what
  does resolve reads the wrong offsets. Crashes or silent corruption, not a refusal.

SKSE itself won't catch it: a plugin declaring `UsesAddressLibrary()` is version-independent as far
as `SKSE::PluginVersionData` is concerned, so SKSE 2.3.1 loads the DLL and CommonLib fails after.

**The fix is a dependency move, not a flag.** The maintained fork is
[`alandtse/CommonLibSSE-NG`](https://github.com/alandtse/CommonLibSSE-NG) (branch `ng`), where the
same switch is a floor (`>= 6 → AE`). 1.7.99/1.7.104 support landed in commits
[`7b47c5a`](https://github.com/alandtse/CommonLibSSE-NG/commit/7b47c5a8f1772ed2331aebdb7035fac48d3c19ca)
("support AE 1.7.99 address library format 5") and
[`9b1b041`](https://github.com/alandtse/CommonLibSSE-NG/commit/9b1b041b9686525039e8ec587887ea29b749ab8f)
("model AE 1.7.99 layout changes"), shipped in **v6.7.0** (2026-08-24). DBVO 2's own `DBVO.dll`
links CommonLibSSE-NG **6.7.1** — that fork's numbering, not CharmedBaryon's — which is how the rest
of the ecosystem moved.

So bringing this repo back to a testable state is:

1. **SKSE 2.3.1** (Nexus 30379, file 795992) — the 1.7.104 Steam build.
2. **Address Library v13** (Nexus 32444, file 795954), literally named "All in One (1.7.104.0)".
3. Repoint all six `CommonLibSSE` `FetchContent_Declare`s at `alandtse/CommonLibSSE-NG` ≥ `v6.7.0`
   and **rebuild** `DBVODialogueTweaks`, `AutoFireBow`, `AutoCastSpell`, `GhostAllies`,
   `OneClickTravel`, `SkytestProbe`. Expect real API drift across two years of fork divergence.
4. Re-run each mod's own in-engine test. These mods are timing-sensitive and were verified on
   1.6.1170; a runtime jump plus a two-year library jump is not a no-op, and `SkytestProbe` has to
   come back first or nothing else can be checked.

The alternative is downgrading `SkyrimSE.exe` back to 1.6.1170 and pinning Steam, which keeps every
verified build valid and abandons the new runtime.

`skytest` refuses to launch on a mismatch (`assert_runtime_match`) and `skytest status` prints the
`runtime` line, so this state announces itself instead of looking like a harness bug.
