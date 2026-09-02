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

## Why the pin is `alandtse`, and what 1.7.104 cost (2026-09-02)

Every mod pins `https://github.com/alandtse/CommonLibSSE-NG` at **`v7.1.0`** (in each
`mods/*/CMakeLists.txt`, `mods/DBVODialogueTweaks/plugin/CMakeLists.txt` for that one).
**Never repin to `CharmedBaryon/CommonLibSSE-NG`** — that repo is abandoned: its `main` HEAD is
`b93280e` (2024-09-03) and its newest tag, `v3.7.0`, is from 2023. Here is why it cannot come back.

Skyrim **1.7.104.0** (Steam, 2026-09-01) is the first runtime CharmedBaryon's code cannot handle.
The reason is one line in its `include/REL/Module.h`:

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

**The fix was a dependency move, not a flag.** The maintained fork is
[`alandtse/CommonLibSSE-NG`](https://github.com/alandtse/CommonLibSSE-NG) (branch `ng`), where the
same switch is a floor (`>= 6 → AE`). 1.7.99/1.7.104 support landed in commits
[`7b47c5a`](https://github.com/alandtse/CommonLibSSE-NG/commit/7b47c5a8f1772ed2331aebdb7035fac48d3c19ca)
("support AE 1.7.99 address library format 5") and
[`9b1b041`](https://github.com/alandtse/CommonLibSSE-NG/commit/9b1b041b9686525039e8ec587887ea29b749ab8f)
("model AE 1.7.99 layout changes"), shipped in **v6.7.0** (2026-08-24). DBVO 2's own `DBVO.dll`
links CommonLibSSE-NG **6.7.1** — that fork's numbering, not CharmedBaryon's — which is how the rest
of the ecosystem moved.

### The three build changes the move forced

All three are in every mod's CMakeLists; copy them if you add a seventh mod.

1. **spdlog ≥ 1.16.0.** NG's `vcpkg.json` floors it there (fmt ≥ 12.1.0). The old `v1.13.0`/fmt-10
   pin, and the comment claiming newer spdlog *breaks* NG, are both obsolete — it's now the reverse.
2. **DirectXTK, headers only.** NG added `find_package(directxtk CONFIG REQUIRED)` and links
   `Microsoft::DirectXTK` for exactly one thing: `<SimpleMath.h>` in `RE/S/State.h`, and only for
   the *field types* `Vector4`/`Matrix` in a layout struct. No SimpleMath function or constant is
   referenced, so no symbol is needed — which is essential, because **DirectXTK cannot be built on
   Linux**: its `Shaders` target shells out to `CompileShaders.cmd` (a Windows batch invoking `fxc`)
   and fails on the first build edge. `SOURCE_SUBDIR Inc` points FetchContent at a directory with no
   `CMakeLists.txt`, the documented way to populate without `add_subdirectory`. The resulting
   INTERFACE target must be `install(TARGETS … EXPORT CommonLibSSE-targets)`'d with
   `$<BUILD_INTERFACE:…>` includes — NG's `install(EXPORT)` refuses to export a target that links
   anything outside an export set (the same trap `SPDLOG_INSTALL ON` already worked around).
3. **`NOMINMAX` per target.** That SimpleMath include drags in the Windows headers, whose `min`/`max`
   *macros* then eat every `std::min(`/`std::max(` in the plugin — the error reads
   `expected unqualified-id`, which looks like anything but a macro collision. NG defines `NOMINMAX`
   only PRIVATEly for its own PCH. A sibling of the same family: `wingdi.h`'s `GetObject` macro turns
   `Variable::GetObject()` into "no member named `GetObjectA`"; fix that one with a local
   `#undef GetObject`, the idiom CommonLib itself uses in `RE/V/Variable.h`.

### Address Library v13 is format 5, and that is a hard wall

`versionlib-1-7-104-0.bin` is **format 5**; `versionlib-1-6-1170-0.bin` was format 2 (check with
`od -An -tu4 -N4 <file>`). A CommonLib older than v6.7.0 only accepts format ≤ 2 and calls
`report_and_fail`, which throws a **modal** — `REL/ID.h(166): Unsupported address library format: 5`
— that parks the boot forever rather than failing soft. So **every** SKSE DLL built before ~2026-08
is dead on this runtime, ours and third-party alike, and a boot that hangs is far more likely to be
a stale DLL than a harness bug. `skytest` scans the fresh SKSE plugin logs for that message and
aborts the readiness wait naming the culprit.

### API drift that actually bit (all mapped against the fetched headers, none guessed)

| Old | New |
| --- | --- |
| `BSFaceGenAnimationData::unk040/0C0/0E0/100/120/140/160/180` | `expressionKeyFrame2` / `expression3` / `modifier1` / `modifier3` / `phoneme1` / `phoneme3` / `custom1` / `custom3` — same offsets, same type. SkytestProbe's **wire** names (`kf:"unk140"`, trace keys) are frozen on purpose so existing `.steps` keep working. |
| `TESObjectREFR/Actor::PauseCurrentDialogue()` | `StopCurrentDialogue()` — same vtable slot `0x4F`, same signature. DBVODialogueTweaks' in-game finding that it only *pauses* still holds; only the name changed. |
| `hkpTypedBroadPhaseHandle::collisionFilterInfo` (`std::uint32_t`), `bhkCharacterController::GetCollisionFilterInfo(std::uint32_t&)` | `RE::CFilter` — a 4-byte struct whose sole member is that `std::uint32_t`; read `.filter` for bit-identical values. |
| `ActorValueOwner::RestoreActorValue(modifier, av, value)` | `RestoreActorValue(av, value)` — `kDamage` is now hardcoded (exactly what we passed) and the amount is `abs()`ed. |
| `RE::DebugNotification(msg)` | `RE::SendHUDMessage::ShowHUDMessage(msg)` — same relocation, same defaults. |
| `ActorValueList::LookupActorValueByName(std::string_view)` | `(const char*)` only. A `string_view` isn't null-terminated: materialise a `std::string`. |
| `IMessageBoxCallback::Message::kUnk1`, `Run(Message)` | `Run(std::uint8_t)`; `kUnk1` was literally `1`. |
| `MapMenu::GetRuntimeData()` returning a reference | returns a **pointer** (null on VR). |

The alternative to all of this was downgrading `SkyrimSE.exe` to 1.6.1170 and pinning Steam, which
would have kept every verified build valid and abandoned the new runtime. We went forward.

`skytest` refuses to launch on a version mismatch (`assert_runtime_match`) and `skytest status`
prints a `runtime` line, so a stale-stack state announces itself instead of looking like a harness bug.

### Still to do here

Upstream v6.6.0 added **Linux-host cross-compile support** of its own
(`cmake/toolchain-linux-clangcl.cmake`, clang-cl + xwin — issue #302). Our hand-rolled
`tools/skse/cross-env.sh` predates it and still works, but that file is now the maintained path for
exactly this setup; it wants an `xwin splat --use-winsysroot-style` sysroot, which ours is not.
Evaluate before extending the local glue further.
