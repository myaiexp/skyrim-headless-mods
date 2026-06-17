# Ghidra — headless reverse-engineering tier

Disassembling `SkyrimSE.exe` to find hook points the SKSE tier can't reach any other way.
Driver: `tools/ghidra/ghidra.sh`. **Map, not a manual** — the driver's `help` is the usage surface.

## Why this exists

The SKSE C++ tier hooks engine functions by **Address-Library ID** (a stable per-build
address for a known function/vtable). That works when the thing you want to change is a
*function* or *vtable slot*. It fails when the behaviour lives **inside a non-virtual
function body** — there's no ID for "the call 40 instructions into `UpdateImpl`".

The forcing case is GhostAllies' parked feature: making **channeled damaging spells**
(Flames/Sparks = `FlameProjectile`/`BeamProjectile`) physically pass through allies. Their
stop point is computed inside the non-virtual body of `UpdateImpl` via a layer-filtered
world cast that ignores systemGroup — no Address-Library seam, so every systemGroup trick
failed (`docs/plans/ghost-allies-design.md` §2b). The only way forward is to **disassemble
that function** and see whether the cast is a hookable `call` (trampoline candidate) or
fully inlined. Ghidra is that tool. This tier exists so any session can **reproduce a
finding** instead of re-deriving the whole toolchain.

## ⚠️ SkyrimSE.exe is SteamStub-encrypted — unpack it FIRST

The Steam `SkyrimSE.exe` is wrapped in **SteamStub DRM (Variant 3.1, x64)**: a `.bind`
section, a second stub `.text`, a `steam_api64` import, and — critically — the **main
`.text` is encrypted on disk** and only decrypted at runtime. Disassemble the raw exe and
you get pure garbage (`INT 0x94`, `OUT 0x22,EAX`, random `MOV moffs`) while `.rdata`
(RTTI, vtables) reads fine — a maddening half-works failure. This is why RE folks never
analyse the Steam exe directly, and it's almost certainly why earlier inference-only
analysis of these functions went wrong.

`ghidra.sh unpack` fixes it: it fetches **Steamless** (via `gh`), copies the exe (the live
game exe is never touched), and runs `Steamless.CLI.exe` under wine to decrypt `.text` →
`tools/ghidra/.steamless/SkyrimSE.exe.unpacked.exe`. Everything downstream defaults to that
unpacked exe; `status` warns loudly if you're about to analyse the encrypted one.

## Install

```
sudo pacman -S --noconfirm ghidra      # official extra repo, no AUR; ~740 MB installed
tools/ghidra/ghidra.sh setup           # install check + build the PyGhidra venv
tools/ghidra/ghidra.sh unpack          # Steamless-decrypt the exe (REQUIRED — see above)
```

Ghidra 12.1 runs fine on this machine's OpenJDK 26 (no version refusal). `setup`/`unpack`
are idempotent — safe to re-run.

## Two ways to query

**Fast path (preferred for targeted work) — no auto-analysis at all.** `find_via_rtti.py`
imports the unpacked exe with `analyze=False` (seconds), walks MSVC RTTI by hand to find a
class's vtable, reads a vfunc slot, and disassembles that function on demand. This is how
we read `FlameProjectile::UpdateImpl` without ever paying for whole-program analysis:

```
GHIDRA_INSTALL_DIR=/opt/ghidra GHIDRA_BINARY=<unpacked> \
  tools/ghidra/.venv/bin/python tools/ghidra/scripts/find_via_rtti.py
tools/ghidra/ghidra.sh query decompile_at.py 0x1407ecbf0 0x140851dc0   # chase the callees
```

Both write full dumps to `tools/ghidra/out/` (gitignored) and print a compact index. They
disassemble single functions via `DisassembleCommand`/`CreateFunctionCmd` in a transaction
(standalone PyGhidra write ops need an explicit `prog.startTransaction`).

**Full path (broad exploration) — analyse once, query many.**

```
tools/ghidra/ghidra.sh analyze         # import + auto-analyse the unpacked exe (long; run detached)
tools/ghidra/ghidra.sh query dump_updateimpl.py
```

`analyze` writes a project under `tools/ghidra/projects/` (gitignored). Useful when you want
whole-program xrefs/symbols; overkill when you already know the class+slot you're after.

## The two gotchas (both cost real time — don't rediscover them)

1. **Heap: use 8 GB, not Arch's 2 GB default.** Arch ships `analyzeHeadless` with
   `MAXMEM_DEFAULT=2G`. A 37 MB PE GC-thrashes that heap (thousands of young GCs, multi-second
   pauses) and analysis crawls — ~7× slower (461 thunks in 27 min @ 2 G vs ~600 in 8 min @ 8 G).
   The driver forces `GHIDRA_HEADLESS_MAXMEM=8G`. Symptom of a starved heap: `jstat -gcutil
   <pid>` shows Old gen pinned high and a huge `YGC` count.
2. **PyGhidra is NOT enabled in the stock headless launcher.** `ghidra-analyzeHeadless
   -postScript foo.py` fails with *"Ghidra was not started with PyGhidra. Python is not
   available."* The stock launcher is Java-only. So query scripts are **standalone PyGhidra
   programs** run from a venv (`pyghidra.start()` + `pyghidra.open_program(..., analyze=False)`),
   NOT `-postScript` hooks — and the `analyze` preScript (`PreLean.java`) is **Java** for the
   same reason. The venv is Python **3.12** because `jpype1` (PyGhidra's JNI bridge) has no wheel
   for newer Pythons and won't source-build on 3.14. `setup` handles all of this.
3. **Trim the analysis — default analyzers waste tens of minutes here.** `analyze` runs the
   `PreLean.java` preScript to disable three passes: **Aggressive Instruction Finder** (it
   speculatively disassembles data gaps, spawning hundreds of bogus external thunks that feed an
   O(n²) function-body fixup — the single worst offender: ~600 thunks in 12 min with it on vs ~9
   with it off) and **Decompiler Parameter ID / Switch Analysis** (whole-program decompiler passes;
   query scripts decompile their own targets on demand). RTTI + basic disassembly stay on — that's
   all the queries need. If a future query needs recovered signatures, drop PreLean and accept the
   longer run.

## Writing a new query script

Drop a `.py` under `tools/ghidra/scripts/` and run it with `ghidra.sh query <name>`. The
driver exports `GHIDRA_INSTALL_DIR` (for `pyghidra.start()`) and `GHIDRA_PROJ_LOC` /
`GHIDRA_PROJ_NAME` / `GHIDRA_OUT` so the script carries no hardcoded paths. Skeleton:

```python
import os, pyghidra
pyghidra.start()
from ghidra.base.project import GhidraProject
gp = GhidraProject.openProject(os.environ["GHIDRA_PROJ_LOC"], os.environ["GHIDRA_PROJ_NAME"], True)
root = gp.getProject().getProjectData().getRootFolder()
prog = gp.openProgram("/", list(root.getFiles())[0].getName(), True)   # read-only
# ... currentProgram-equivalent is `prog`; use FunctionManager / DecompInterface / Listing ...
gp.close()
```

Convention: dump bulky output (full decompilation / disassembly) to `tools/ghidra/out/`
(gitignored) and print only a **compact index** to stdout — keeps large dumps out of an
agent's context. `dump_updateimpl.py` is the worked example (RTTI → vtable slot `0xAB` →
decompile `UpdateImpl`, with a per-function call-site index).

## Layout

| Path                          | Holds                                                          |
| ----------------------------- | ------------------------------------------------------------- |
| `tools/ghidra/ghidra.sh`      | the driver (verbs: `setup`/`analyze`/`query`/`gui`/`status`)   |
| `tools/ghidra/scripts/`       | PyGhidra query scripts (committed)                            |
| `tools/ghidra/.venv/`         | PyGhidra venv (gitignored; rebuilt by `setup`)               |
| `tools/ghidra/projects/`      | Ghidra project data (gitignored — third-party disassembly)   |
| `tools/ghidra/out/`           | query dumps (gitignored)                                      |
| `tools/ghidra/*.log`          | analysis logs (gitignored)                                   |
