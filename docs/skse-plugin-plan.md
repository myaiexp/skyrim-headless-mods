# SKSE C++ plugin — plan (next phase)

The native runtime that *can* do what Papyrus can't (see
[findings-papyrus-limits.md](findings-papyrus-limits.md)). An SKSE plugin is a Windows DLL
loaded by SKSE with full engine access — it can read/write the bow charge, hook the projectile
launch, or inject input. Precedent: *Manual Crossbow Reload* is an SKSE plugin that rewrites
bow draw mechanics, so this class of change is proven possible.

This stays on-brand: we already built a **headless C# + wine** toolchain; this is the next
tier — **headless C++ cross-compiled to a Windows DLL on Linux**.

## Target

- Game: **Skyrim SE v1.6.1170** (AE; from the crash log). Use **CommonLibSSE-NG** (the
  maintained fork supporting SE/AE/VR with runtime address resolution).
- Output: `RapidBow.dll` → `Data/SKSE/Plugins/`.

## What the plugin would do (pick one once we can build + RE)

1. **Force full charge on player bow release** — hook the function that computes arrow power
   from draw time and clamp the multiplier to max. Then even a quick tap fires full-power, and
   a simple input-rate tweak gives rapid full-power fire. *Smallest, most surgical.*
2. **Native hold-to-auto** — detect attack held + bow equipped, and on a cadence trigger a
   real, fully-charged loose (call the engine's fire path with full charge). Full control.
3. **Synthetic input** — inject real attack release+press at the input layer so the engine
   charges/looses naturally. Closest to "real", but input injection on the engine side is
   fiddly.

Option 1 is the recommended first target: least code, and it fixes the root cause (charge)
rather than working around it.

## Headless build toolchain (Linux → Windows DLL)

CommonLibSSE-NG normally builds on Windows with MSVC + vcpkg. Headless on Linux is doable:

- **Compiler:** `clang-cl` (MSVC-compatible driver) targeting `x86_64-pc-windows-msvc`.
- **Windows SDK + CRT headers/libs:** [`xwin`](https://github.com/Jake-Shadle/xwin) downloads
  and splats them locally (no Windows, no MSVC install).
- **Build system:** CMake (CommonLibSSE-NG ships CMake) or **xmake** (many SKSE templates use
  it; has good clang-cl/xwin support). Pick whichever resolves CommonLibSSE-NG cleanly.
- **Dependencies:** spdlog/fmt and CommonLibSSE-NG itself via the template's package manager
  (vcpkg manifest or xmake-repo). These must build under clang-cl too — the main risk area.
- **Address Library for SKSE** (AE/1.6.1170 database) for offset resolution at runtime.
- **Linking:** produce a PE DLL; lld-link (bundled with clang) handles it.

Verification points (confirm when we actually build — don't assume):
- CommonLibSSE-NG + its deps compile under clang-cl/xwin (some MSVC-isms can bite).
- The version database / Address Library path for 1.6.1170.
- Test loading the built DLL via the existing wine prefix or in-game.

## Reverse-engineering the bow charge (for option 1)

- Identify the value/struct holding the player's current bow draw (the "attack data" /
  high-process draw timer) and the function that turns it into projectile power.
- Tools: Address Library, existing open-source plugins that touch archery (e.g. Manual
  Crossbow Reload, Bow Charge Plus equivalents, SmoothCam's aim code) as references for
  offsets/patterns. Ghidra/IDA on `SkyrimSE.exe` if needed.

## Next steps

1. Stand up the cross-compile toolchain (`xwin` + clang-cl + xmake/CMake); build the
   canonical CommonLibSSE-NG "hello world" plugin headlessly and confirm it loads.
2. Add a `plugins/RapidBow/` tree to this repo with its own build script (mirroring
   `mods/<name>/build.sh`).
3. RE the bow charge; implement option 1; test.

## References

- CommonLibSSE-NG: https://github.com/CharmedBaryon/CommonLibSSE-NG
- SKSE plugin templates (CMake/xmake): search "CommonLibSSE-NG plugin template"
- `xwin`: https://github.com/Jake-Shadle/xwin
- Address Library for SKSE Plugins (Nexus) — AE database
- Precedent for bow-mechanic plugins: *Manual Crossbow Reload SSE*
