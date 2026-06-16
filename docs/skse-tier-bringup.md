# SKSE C++ tier: bring-up (done; historical reference)

The native runtime that _can_ do what Papyrus can't (see
[papyrus-limits.md](papyrus-limits.md)). An SKSE plugin is a Windows DLL
loaded by SKSE with full engine access: it can read/write the bow charge, hook the projectile
launch, or inject input. Precedent: _Manual Crossbow Reload_ is an SKSE plugin that rewrites
bow draw mechanics, so this class of change is proven possible.

This stays on-brand: we already built a **headless C# + wine** toolchain; this is the next
tier: **headless C++ cross-compiled to a Windows DLL on Linux**.

## Target

- Game: **Skyrim SE v1.6.1170** (AE; from the crash log). Use **CommonLibSSE-NG** (the
  maintained fork supporting SE/AE/VR with runtime address resolution).
- Output: `AutoFireBow.dll` → `Data/SKSE/Plugins/`.

## What the plugin would do (pick one once we can build + RE)

1. **Force full charge on player bow release**: hook the function that computes arrow power
   from draw time and clamp the multiplier to max. Then even a quick tap fires full-power, and
   a simple input-rate tweak gives rapid full-power fire. _Smallest, most surgical._
2. **Native hold-to-auto**: detect attack held + bow equipped, and on a cadence trigger a
   real, fully-charged loose (call the engine's fire path with full charge). Full control.
3. **Synthetic input**: inject real attack release+press at the input layer so the engine
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
  (vcpkg manifest or xmake-repo). These must build under clang-cl too, the main risk area.
- **Address Library for SKSE** (AE/1.6.1170 database) for offset resolution at runtime.
- **Linking:** produce a PE DLL; lld-link (bundled with clang) handles it.

Verification points (confirm when we actually build, don't assume):

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
   CommonLibSSE-NG "hello world" plugin headlessly and confirm it loads.~~ **DONE**: the
   toolchain works: `clang-cl` + `lld-link` + `xwin` (no MSVC/vcpkg), CommonLibSSE-NG pulled via
   FetchContent. See [skse-toolchain.md](skse-toolchain.md). Chose CMake over xmake (CommonLibSSE
   ships CMake; FetchContent resolved its deps cleanly). Two fixes were needed:
   `-fdelayed-template-parsing` (MSVC lazy template-body semantics) and PascalCase `.lib`
   symlinks (lld-link is case-sensitive). Both documented and automated.
2. ~~Add a `mods/AutoFireBow/` tree with its own build script.~~ **DONE**: `mods/AutoFireBow/`
   builds `AutoFireBow.dll` (valid PE32+, exports `SKSEPlugin_{Load,Query,Version}`, loads under
   wine). It's a hello-world: it logs on load and hooks nothing yet.
3. ~~RE the bow charge; implement option 1 (force full charge on release); test in-game.~~
   **DONE**: verified in-game on 1.6.1170: a quick tap now fires a full-power, full-speed
   arrow. Implementation notes below.

## How option 1 was implemented (the charge → power path)

The engine never exposes a clean "current draw" float. Instead it bakes the draw-time fraction
into the **launched arrow's `power`** (`PROJECTILE_RUNTIME_DATA::power`, 0x188, range 0.0–1.0).
Both arrow **speed** (`Projectile::GetSpeed` → `GetPowerSpeedMult` → `baseSpeed * mult * …`) and
impact **damage** read that field.

We hook **`ArrowProjectile::GetPowerSpeedMult`**, which is **virtual**, via **vtable replacement**
(`REL::Relocation{ VTABLE_ArrowProjectile[0] }.write_vfunc(0xB0, …)`), _not_ a detour of
`Projectile::Launch`. Reason: CommonLib's trampoline (`write_branch<5>`/`write_call<5>`) only
**redirects an existing call/jmp** (it recovers the original target from the on-site rel32). A raw
function-entry detour on `Launch` would mis-read the prologue as a displacement and return a
garbage "original" → crash. The virtual is the clean, fully-verifiable lever and needs no
sig-scanned call-site address.

In the hook, for **player-fired** arrows (`runtime.shooter->IsPlayerRef()`) we clamp
`runtime.power` up to `1.0`, then defer to the original, which recomputes the genuine full-power
speed multiplier from the now-full charge. NPC archers are untouched.

**Important correction (found by in-game testing): `power` only drives _speed_, not damage.**
`Projectile::GetSpeed` reads `power` _live_ each call, so clamping it gives full speed. But arrow
**damage** is baked at launch into a **separate** field (`PROJECTILE_RUNTIME_DATA::weaponDamage`,
0x198) as `fullDamage * drawMult`, and is _not_ recomputed from `power`. So clamping `power`
alone gave full-speed-but-partial-damage arrows. Fix: in the same hook, also rescale
`weaponDamage *= 1.0f / naturalPower`. Verified exact: every shot's `weaponDamage` rescales to
the full-draw value (it's perfectly linear in `power`). Both writes happen once per arrow (guarded
flag) or the damage rescale would compound on each `GetSpeed` call.

AE vtable slot is **0xB0** (SE is 0xAF), from CommonLibSSE-NG
`Projectile::GetPowerSpeedMult → RelocateVirtual(0xAF, 0xB0)`; `RELOCATION_ID`/vtable lookups
resolve against the Address Library DB for 1.6.1170 (installed in-game). Code:
`mods/AutoFireBow/src/main.cpp`.

## Rapid-fire loop (hold-attack auto-fire): DONE

Layered on top of the full-power hook: **hold the attack button with a bow → auto-loose at full
draw, re-nock, repeat; release to stop.** Verified in-game on 1.6.1170.

How it works (all on the main thread, **event-driven, no per-frame polling task**):

- A `BSTEventSink<BSAnimationGraphEvent>` on the player watches the bow cycle. On the engine's
  **`BowDrawn`** event (its own "fully drawn" marker, adapts to any bow speed / perk / enchant,
  no draw-time assumptions) it schedules a loose; on the resulting **`arrowRelease`** it schedules
  a re-nock. Each is a **one-shot** `SKSE::GetTaskInterface()->AddTask` (deferred so we don't
  re-enter the graph mid-dispatch, and **never self-rescheduling**).
- Loose = `NotifyAnimationGraph("attackRelease")`; re-nock = `NotifyAnimationGraph` of
  `bowAttackStart` + `BowDrawStart` + `bowDraw`. (Same events the shelved Papyrus loop used.)
- **Graph-driven release loses an _uncharged_ (0.350) arrow.** The draw charge is welded to the
  real input-release path, not the graph (confirmed: auto-fired shots logged `natural_power=0.350`
  while manual ones logged `1.000`). That's fine here: the full-power+damage hook above forces every
  player arrow to full regardless, so the graph loose still lands full.
- **Hold detection:** a `BSTEventSink<InputEvent*>` on `BSInputDeviceManager` tracks the
  `"Right Attack/Block"` / `"Left Attack/Block"` button state. (`AttackBlockHandler::heldRight/
heldLeft` did **not** reflect hold reliably; first attempt, abandoned.)

### Hard-won lessons (don't re-derive)

- **NEVER use a self-rescheduling `AddTask` for a per-frame loop.** v1.6.0 did `AddTask(LoopTick)`
  from inside `LoopTick`; SKSE drains the task queue within the frame, so it became an infinite
  loop → **main-thread hang** (black screen, no input, ambient audio still playing, the classic
  signature). Drive timed/looping logic from real engine events + one-shot tasks, or a proper
  per-frame _hook_, never a re-arming task.
- **A fixed-ms release timer is fragile**: bow draw speed varies with the weapon, the Quick Shot
  perk, enchants, etc. The robust trigger is the engine's own `BowDrawn` event.

### Possible follow-ups

- **Reclaim ~220ms/shot:** genuine full power saturates ~220ms _before_ `BowDrawn` fires (measured:
  `natural_power=1.000` at ~1800ms vs `BowDrawn` at ~2065ms). Two ways to recover that DPS, both
  open (not decided):
  - **Fire at saturation**: needs the **live draw charge** (not a named field; a probe would have
    to locate it in `HighProcessData`). Exact, no power-creep, genuinely faster cadence.
  - **Flat damage bump (~10%)** to compensate for the lost cadence. Trivial (rescale target
    `×1.1`). Likely _adaptive_ after all: saturation and `BowDrawn` are both points in the draw
    animation and scale together with draw speed, so the gap is ~a constant fraction → a flat % is
    bow-independent. Caveat: it's mild power-creep (>100% of a real full draw). The ~220ms/shot
    deficit is **real vs skilled manual play**. A player releases at saturation by muscle memory
    (consistent, not reaction-time-limited), so the loop firing at `BowDrawn` is genuinely slower.
- **Next feature, visibility-gated auto-release (TDM integration):** while looping, don't loose if
  the current target isn't actually visible (e.g. behind cover). Hold the draw and only release
  once line-of-sight is clear, so the loop stops wasting arrows into walls. Hook: query **True
  Directional Movement**'s API (`TrueDirectionalMovementAPI`) for the player's locked target, then
  gate `ScheduleRelease()` on a line-of-sight check (engine `Actor::HasLineOfSight`/detection, or
  TDM's own visibility). Falls back to normal behaviour when no target is locked / TDM absent.
- **Known bug (NOT ours, likely vanilla):** tap, then tap again _before_ the arrow looses → bow
  draws but won't release. Predates the loop (seen in the v1.5.0 one-tap tests, before any redraw
  injection existed) and the hook only touches the fired projectile, so this is a vanilla Skyrim
  bow-input quirk, not mod-introduced. Left as-is.
- `power`/`weaponDamage` are forced to exactly full; scaling `>1.0` would over-draw if ever wanted.
- The plugin still carries verbose probe logging + `AnimProbeSink` naming from development, worth
  trimming/renaming.

## References

- CommonLibSSE-NG: https://github.com/CharmedBaryon/CommonLibSSE-NG
- SKSE plugin templates (CMake/xmake): search "CommonLibSSE-NG plugin template"
- `xwin`: https://github.com/Jake-Shadle/xwin
- Address Library for SKSE Plugins (Nexus): AE database
- Precedent for bow-mechanic plugins: _Manual Crossbow Reload SSE_
