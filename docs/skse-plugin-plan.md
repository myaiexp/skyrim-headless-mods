# SKSE C++ plugin — plan (next phase)

The native runtime that _can_ do what Papyrus can't (see
[findings-papyrus-limits.md](findings-papyrus-limits.md)). An SKSE plugin is a Windows DLL
loaded by SKSE with full engine access — it can read/write the bow charge, hook the projectile
launch, or inject input. Precedent: _Manual Crossbow Reload_ is an SKSE plugin that rewrites
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
   a simple input-rate tweak gives rapid full-power fire. _Smallest, most surgical._
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

1. ~~Stand up the cross-compile toolchain (`xwin` + clang-cl + CMake); build the canonical
   CommonLibSSE-NG "hello world" plugin headlessly and confirm it loads.~~ **DONE** — the
   toolchain works: `clang-cl` + `lld-link` + `xwin` (no MSVC/vcpkg), CommonLibSSE-NG pulled via
   FetchContent. See [skse-toolchain.md](skse-toolchain.md). Chose CMake over xmake (CommonLibSSE
   ships CMake; FetchContent resolved its deps cleanly). Two fixes were needed:
   `-fdelayed-template-parsing` (MSVC lazy template-body semantics) and PascalCase `.lib`
   symlinks (lld-link is case-sensitive). Both documented and automated.
2. ~~Add a `plugins/RapidBow/` tree with its own build script.~~ **DONE** — `plugins/RapidBow/`
   builds `RapidBow.dll` (valid PE32+, exports `SKSEPlugin_{Load,Query,Version}`, loads under
   wine). It's a hello-world: it logs on load and hooks nothing yet.
3. ~~RE the bow charge; implement option 1 (force full charge on release); test in-game.~~
   **DONE** — verified in-game on 1.6.1170: a quick tap now fires a full-power, full-speed
   arrow. Implementation notes below.

## How option 1 was implemented (the charge → power path)

The engine never exposes a clean "current draw" float. Instead it bakes the draw-time fraction
into the **launched arrow's `power`** (`PROJECTILE_RUNTIME_DATA::power`, 0x188, range 0.0–1.0).
Both arrow **speed** (`Projectile::GetSpeed` → `GetPowerSpeedMult` → `baseSpeed * mult * …`) and
impact **damage** read that field.

We hook **`ArrowProjectile::GetPowerSpeedMult`**, which is **virtual**, via **vtable replacement**
(`REL::Relocation{ VTABLE_ArrowProjectile[0] }.write_vfunc(0xB0, …)`) — _not_ a detour of
`Projectile::Launch`. Reason: CommonLib's trampoline (`write_branch<5>`/`write_call<5>`) only
**redirects an existing call/jmp** (it recovers the original target from the on-site rel32). A raw
function-entry detour on `Launch` would mis-read the prologue as a displacement and return a
garbage "original" → crash. The virtual is the clean, fully-verifiable lever and needs no
sig-scanned call-site address.

In the hook, for **player-fired** arrows (`runtime.shooter->IsPlayerRef()`) we clamp
`runtime.power` up to `1.0`, then defer to the original — which recomputes the genuine full-power
speed multiplier from the now-full charge. So speed and range come from the original formula, and
the now-full `power` field makes impact damage full too. NPC archers are untouched.

AE vtable slot is **0xB0** (SE is 0xAF) — from CommonLibSSE-NG
`Projectile::GetPowerSpeedMult → RelocateVirtual(0xAF, 0xB0)`; `RELOCATION_ID`/vtable lookups
resolve against the Address Library DB for 1.6.1170 (installed in-game). Code:
`plugins/RapidBow/src/main.cpp`.

### Possible follow-ups (option 1 polish)

- The fix governs the _fired_ arrow (power/speed/damage). The draw **animation** still plays its
  normal release; cosmetic only. A _rapid_ full-power fire (the original "RapidBow" intent) would
  layer an input/cadence tweak on top — separate from this charge fix.
- `power` is clamped to exactly `1.0`; setting `>1.0` would over-draw (more speed/damage) if ever
  wanted.

## References

- CommonLibSSE-NG: https://github.com/CharmedBaryon/CommonLibSSE-NG
- SKSE plugin templates (CMake/xmake): search "CommonLibSSE-NG plugin template"
- `xwin`: https://github.com/Jake-Shadle/xwin
- Address Library for SKSE Plugins (Nexus) — AE database
- Precedent for bow-mechanic plugins: _Manual Crossbow Reload SSE_
