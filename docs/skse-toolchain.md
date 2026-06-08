# The headless SKSE C++ toolchain (tier 2)

How a native **SKSE plugin** (a Windows `.dll`) gets built here entirely on Linux — no
Windows, no Visual Studio, no MSVC, no vcpkg. This is the second tier of headless modding
(tier 1 = Papyrus, see [toolchain.md](toolchain.md)); it exists because some engine behaviour
is unreachable from Papyrus (see [findings-papyrus-limits.md](findings-papyrus-limits.md)).

**Status: working.** `plugins/RapidBow/` builds a valid SKSE-shaped `RapidBow.dll` against
CommonLibSSE-NG, fully cross-compiled, and it loads (entry points resolve under wine). It's a
hello-world — it doesn't hook anything yet. The next step is RE'ing the bow charge
([skse-plugin-plan.md](skse-plugin-plan.md)).

## The idea

A SKSE plugin is an x86-64 PE DLL using the MSVC C++ ABI. You don't need MSVC to produce that —
`clang-cl` (Clang's MSVC-compatible driver) emits MSVC-ABI objects, `lld-link` links them into a
PE, and the Windows SDK + CRT headers/libs come from `xwin` (which downloads and repacks
Microsoft's redistributable SDK — no Windows install). CommonLibSSE-NG is pulled and built from
source by CMake's FetchContent, so there's no vcpkg either.

```
 .cpp ──clang-cl (--target=x86_64-pc-windows-msvc)──▶ .obj (MSVC ABI)
                       │ uses xwin's CRT + SDK headers (/imsvc)
 CommonLibSSE-NG ──────┤ (FetchContent: built the same way into CommonLibSSE.lib)
 spdlog ───────────────┤ (FetchContent)
                       ▼
                  lld-link  ──uses xwin's CRT + SDK libs (/libpath)──▶  RapidBow.dll (PE32+)
```

## Components (and how each was obtained, no root)

| Piece                                                        | What                                         | Source                                                                                                                                                                                                         |
| ------------------------------------------------------------ | -------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `clang` / `clang-cl`                                         | compiler driver, MSVC mode                   | system `clang` package (already installed)                                                                                                                                                                     |
| `lld-link`, `llvm-rc`, `llvm-lib`, `llvm-dlltool`, `llvm-mt` | PE linker + MSVC-style tools                 | `lld` + `llvm` Arch packages, **extracted (no root)** into `~/.local/llvm-extra` — they link against the already-installed `llvm-libs`, so their version **must match** `clang` (here 22.1.5). See note below. |
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
   few template methods with typos / missing members that are **never instantiated** — MSVC
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

| Path                                | Role                                                                                                                                                             |
| ----------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `plugins/cross-env.sh`              | Sourced before building. Puts the LLVM cross tools on `PATH`/`LD_LIBRARY_PATH`, exports `XWIN_SDK`, ensures the `.lib` symlinks exist.                           |
| `plugins/setup-sdk-symlinks.sh`     | Creates the PascalCase `.lib` symlinks in the xwin SDK (called by `cross-env.sh`).                                                                               |
| `plugins/cmake/clang-cl-msvc.cmake` | CMake toolchain file: sets the compiler/linker/ar/rc, the `/imsvc` include dirs, the `/libpath` lib dirs, `-fdelayed-template-parsing`, and cross-find settings. |
| `plugins/RapidBow/CMakeLists.txt`   | FetchContent for spdlog (`OVERRIDE_FIND_PACKAGE`), rapidcsv (header, fed via `RAPIDCSV_INCLUDE_DIRS`), and CommonLibSSE-NG (pinned); builds `RapidBow.dll`.      |
| `plugins/RapidBow/src/main.cpp`     | The hello-world plugin (declarative `SKSEPluginInfo` + `SKSEPluginLoad`).                                                                                        |
| `plugins/RapidBow/build.sh`         | One-shot configure + build (`--install` copies the DLL into the live game's `Data/SKSE/Plugins`).                                                                |

## Build it

```bash
cd plugins/RapidBow
./build.sh            # -> build/RapidBow.dll (PE32+, x86-64)
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
3. That's it — `cross-env.sh` wires the rest and creates the `.lib` symlinks on first build.

## Verifying a built DLL

```bash
source plugins/cross-env.sh
llvm-readobj --coff-exports plugins/RapidBow/build/RapidBow.dll   # expect SKSEPlugin_{Load,Query,Version}
file plugins/RapidBow/build/RapidBow.dll                          # expect PE32+ ... (DLL), x86-64
```

A loadable-DLL smoke test under wine (`LoadLibraryA` + `GetProcAddress` on the exports) confirms
the imports resolve. In-game loading needs SKSE + the Address Library installed.
