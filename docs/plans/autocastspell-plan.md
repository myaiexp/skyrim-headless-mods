# AutoCastSpell Implementation Plan

**Goal:** A standalone SKSE C++ DLL that auto-recasts a held fire-and-forget spell in a loop
(charge → release → recharge), per hand, until the cast control is released — the spell analog of
AutoFireBow.

**Architecture:** One `main.cpp`, mirroring `mods/AutoFireBow/src/main.cpp`. Two `BSTEventSink`s —
`CastInputSink` (held state of `Right/Left Attack/Block` from raw input) and `SpellLoopSink`
(animation-graph events on the player) — drive a per-hand state machine that injects synthetic
`ButtonEvent`s (release to cast on full charge, press to recharge on fire) through
`BSInputDeviceManager`. **Probe-first:** the exact magic animation-event tags and whether synthetic
input even fires a charged spell are unproven, so the first two tasks resolve those in-game before
the loop is built (fail-fast — if injection doesn't fire a spell, the whole approach is dead).

**Tech Stack:** clang-cl + lld-link + xwin cross-build (Linux→Windows) via `tools/skse`;
CommonLibSSE-NG (FetchContent); spdlog logging; verified in-engine with `skytest`.

**Design:** `docs/plans/autocastspell-design.md`.

---

## File structure

| File | Responsibility |
| ---- | -------------- |
| `mods/AutoCastSpell/CMakeLists.txt` | Build config — copy of GhostAllies/AutoFireBow CMakeLists, project renamed `AutoCastSpell`, one `src/main.cpp` target. |
| `mods/AutoCastSpell/build.sh` | DLL-only headless build — copy of `mods/GhostAllies/build.sh` (no Papyrus/esp in v1), renamed. |
| `mods/AutoCastSpell/src/main.cpp` | The whole plugin: log setup, the two sinks, per-hand loop state, synthetic-input helper, registration. Grows across Tasks 1→3. |

No `.esp`, no Papyrus, no MCM in v1 (always-on) — those are deferred (`docs/ideas.md`).

---

### Task 1: Scaffold + animation-event probe build  [Mode: Direct]

Stand up the mod and use it as a pure **logger** to pin the real magic animation-event tags. No loop,
no injection yet.

**Files:**
- Create: `mods/AutoCastSpell/CMakeLists.txt` (copy `mods/GhostAllies/CMakeLists.txt`; rename project
  + target + DLL to `AutoCastSpell`. **Keep the `rapidcsv` stanza** — this mod reads no CSV, but
  CommonLibSSE-NG's own CMake unconditionally uses `RAPIDCSV_INCLUDE_DIRS` as an include dir, so
  dropping it makes NG fail to configure with `RAPIDCSV_INCLUDE_DIRS-NOTFOUND`. AutoFireBow keeps it
  for the same reason.)
- Create: `mods/AutoCastSpell/build.sh` (copy `mods/GhostAllies/build.sh`; rename `GhostAllies`→
  `AutoCastSpell` throughout)
- Create: `mods/AutoCastSpell/src/main.cpp`

**Contracts (`main.cpp` v0):**
- `void SetupLog()` — spdlog basic_file_sink to `<My Games>/SKSE/AutoCastSpell.log` (copy AutoFireBow).
- `class CastInputSink : RE::BSTEventSink<RE::InputEvent*>` — on each `kButton` event whose
  `QUserEvent()` is `"Right Attack/Block"` or `"Left Attack/Block"`, log
  `"{hand} held={IsPressed}"`. (No `g_injectingSynthetic` guard needed yet — no injection in Task 1.)
- `class SpellLoopSink : RE::BSTEventSink<RE::BSAnimationGraphEvent>` — for **every** event, log the
  raw `a_event->tag.c_str()` (and `a_event->payload` if non-empty). This is the probe.
- `void RegisterSinks()` — add `CastInputSink` to `BSInputDeviceManager`, add `SpellLoopSink` to the
  player via `AddAnimationGraphEventSink`; idempotent (static `registered` flag), called from
  `OnMessage` on `kPostLoadGame` + `kNewGame` (copy AutoFireBow's `RegisterBowLoop`/`OnMessage`).
- `SKSEPluginInfo(.Version = {0,1,0}, .Name = "AutoCastSpell", .Author = "mase",
  .RuntimeCompatibility = AddressLibrary)` + `SKSEPluginLoad` (SetupLog, SKSE::Init, register the
  messaging listener). No Papyrus interface in v1.

**Verification (in-engine, `skytest`):**
1. `cd mods/AutoCastSpell && ./build.sh` → succeeds; `file build/AutoCastSpell.dll` reports
   `PE32+ ... (DLL)`.
2. `skytest test mods/AutoCastSpell/build/AutoCastSpell.dll` (standalone, single DLL artifact — no
   split-output staging issue). Boot the drivable session.
3. Give the player Firebolt and equip it to the **right** hand (acceptable routes: SkytestProbe `exec`
   a Papyrus snippet `Game.GetPlayer().AddSpell(...)` + `EquipSpell(... , 1)`, or boot a save with a
   mage already holding it). Draw magic, hold the right cast control, let it charge, release. Repeat
   2–3×. Then repeat for the **left** hand.
4. Read `<My Games>/SKSE/AutoCastSpell.log`.

**Acceptance — record the confirmed tags** for each phase, per hand, into a `// Confirmed anim events`
comment block at the top of `main.cpp` (and the design table can be corrected if the guesses were
wrong):
- charge-start (arm) — candidate `BeginCastRight`/`BeginCastLeft`
- charged-ready (→ release) — candidate `MRh_SpellReadyOut`/`MLh_SpellReadyOut`
- fired (→ re-press) — candidate `MRh_SpellFire_Event`/`MLh_SpellFire_Event`

If the real tags differ from the candidates, the confirmed strings win — they are the source of truth
for Tasks 2–3.

**Commit after the tags are confirmed and recorded.**

---

### Task 2: Single-shot synthetic-release confirmation (the make-or-break gate)  [Mode: Direct]

Prove the core gamble in isolation: **does a synthetic release `ButtonEvent` actually fire a
genuinely-charged spell**, the way it looses a charged bow in AutoFireBow? One shot per hold — no
re-press, no loop yet.

**Files:**
- Modify: `mods/AutoCastSpell/src/main.cpp`

**Contracts:**
- `void SendSyntheticCast(Hand hand, bool pressed)` — build a `ButtonEvent` via
  `RE::ButtonEvent::Create(RE::INPUT_DEVICE::kMouse, control, 0, value, heldSecs)` where `control` is
  the hand's user-event string, `value = pressed ? 1.0f : 0.0f`, `heldSecs = pressed ? 0.0f : 0.5f`;
  set `g_injectingSynthetic = true` around `idm->SendEvent(&head)`; `RE::free(be)` after. Enqueue the
  call on the game thread via `SKSE::GetTaskInterface()->AddTask`. (Direct port of AutoFireBow's
  `SendSyntheticAttack`, parameterized by hand.)
- Add the `g_injectingSynthetic` skip-guard to `CastInputSink` (so the injected release doesn't move
  the tracked held-state).
- In `SpellLoopSink`, on the **confirmed charged-ready tag** for a hand, if that hand is currently
  held and not yet fired this hold, call `SendSyntheticCast(hand, /*pressed=*/false)` exactly once,
  then latch a `firedThisHold` flag (cleared on the hand's release in `CastInputSink`). Log it.

**Verification (in-engine, `skytest`):**
- Equip Firebolt (right hand), draw, hold the cast control. **Expected:** the spell *fires on its own*
  the moment it reaches full charge (the synthetic release), without the player releasing the button.
  Screenshot the launched projectile (`skytest shot`) and/or the log line.
- Repeat for left hand.

**Acceptance — GATE:**
- **Fires** → the approach is validated. Proceed to Task 3.
- **Does not fire** (engine ignores synthetic input for the cast path / charge welded to physical
  device state) → **STOP.** The loop cannot work via input injection. Record the result in the design
  doc and `docs/ideas.md`, and re-open the rejected `ActorMagicCaster::CastSpellImmediate` route (or
  shelve, as RapidBowHold was). Do not build Task 3 on a failed gate.

**Commit after the gate result is known and recorded.**

---

### Task 3: Full per-hand auto-recast loop  [Mode: Direct]

Turn the one-shot into the looping state machine, per hand, gated on fire-and-forget casting type and
held-state.

**Files:**
- Modify: `mods/AutoCastSpell/src/main.cpp`

**Contracts:**
- `enum class Hand { kRight, kLeft };` + helper `const char* ControlFor(Hand)` →
  `"Right Attack/Block"` / `"Left Attack/Block"`.
- Per-hand state (array indexed by `Hand`): `struct HandLoop { bool held; bool firedThisCycle; };`
  This **supersedes Task 2's per-hold `firedThisHold` latch** — `firedThisCycle` is re-armed every
  charge-start (so each charge cycle in a hold can cast once), not just once per button hold.
- `bool HandSpellIsFireAndForget(RE::Actor*, Hand)` — read that hand's equipped magic form and return
  `castingType == RE::MagicSystem::CastingType::kFireAndForget`. **Integration point — confirm the
  accessor against the vendored CommonLibSSE-NG headers** (`mods/AutoCastSpell/build/_deps/
  commonlibsse-src/include/RE/`): candidate routes are
  `actor->GetMagicCaster(RE::MagicSystem::CastingSource::kRightHand/kLeftHand)->currentSpell` →
  `MagicItem::GetCastingType()`, or `actor->GetEquippedObject(/*leftHand*/bool)` cast to
  `RE::MagicItem*`. Pick whichever reliably yields the hand's spell; return `false` when null.
- `SpellLoopSink::ProcessEvent` (using the **confirmed** tags from Task 1):
  - on **charge-start (H)**: `loop[H].firedThisCycle = false` (re-arm). The FF gate is evaluated here
    per hand — if `!HandSpellIsFireAndForget(player, H)`, leave the cycle disarmed so the loop never
    arms for concentration/instant casts.
  - on **charged-ready (H)**: if `loop[H].held && !loop[H].firedThisCycle && <FF-armed>` →
    `loop[H].firedThisCycle = true; SendSyntheticCast(H, false)`.
  - on **fired (H)**: **re-check `loop[H].held`** (a release landing mid-cast must end the loop
    cleanly — AutoFireBow's `AttackHeld()` gate); if still held → `SendSyntheticCast(H, true)` to
    recharge. Enqueue on the game thread.
- `CastInputSink` maintains `loop[H].held` from raw input (skipping injected events via
  `g_injectingSynthetic`); on release, clear `firedThisCycle`/any per-hold latch.

**Verification (in-engine, `skytest` — these scenarios are the spec):**
- **Right-hand loop:** equip Firebolt right, hold cast → Firebolts auto-launch repeatedly; release →
  stops. (screenshot the stream of projectiles)
- **Left-hand loop:** equip Firebolt left, hold left cast → same, independently.
- **Dual-cast:** equip Firebolt both hands, hold both → looped dual-cast Firebolts.
- **Concentration excluded:** equip Flames, hold cast → normal continuous stream, **no** auto-recast
  behavior change (the FF gate keeps it disarmed).
- **Magicka-out:** drain magicka mid-loop → loop **stalls** cleanly (no input/animation spam); after
  regen, release + re-press resumes it.
- **Sheathed / menu open:** loop does not fire (sinks don't arm).

**Constraints:**
- No off-thread game-state access — all injection enqueued via the task interface.
- Manual (non-FF, non-held) casting must be completely unaffected.

**Commit after the loop is verified in-engine.**

---

### Task 4: Wire into repo docs  [Mode: Direct]

Register the new mod in the repo's indexes (doc-update trigger: new mod = structure change).

**Files:**
- Modify: `README.md` (repo root) — add `AutoCastSpell` to the per-mod table.
- Modify: `CLAUDE.md` (repo root) — add `AutoCastSpell` to the `mods/` row in the Layout table
  (alongside AutoFireBow/GhostAllies as an SKSE C++ mod).
- (No per-mod `README.md` — AutoFireBow has none; keep v1 lean. Add later if released publicly.)

**Verification:** `git status` shows only the intended files; the per-mod table lists AutoCastSpell
with a one-line description and its tier.

**Commit.**

---

## Mode rationale

All tasks are **Mode: Direct**. This is a single small `main.cpp` that is *probe-driven and
iterated in-engine* — each task's real work is building, running `skytest` on Mase's runtime, reading
logs/screenshots, and adapting (especially the Task 1 tag discovery and the Task 2 gate). That
in-engine loop can't be hand off to a one-shot subagent, and the code itself is a close port of
AutoFireBow (held in context), so delegation adds overhead without benefit.

---
## Execution
**Skill:** superpowers:subagent-driven-development
- Mode A tasks: Opus implements directly
- Mode B tasks: Dispatched to subagents
