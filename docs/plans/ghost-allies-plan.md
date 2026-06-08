# GhostAllies Implementation Plan

**Goal:** A standalone SKSE plugin that lets player-fired arrows/bolts physically pass through hired followers and continue to the enemy behind.

**Architecture (revised after the Task 2 proof-point — see design doc "Pivot"):** arrows are *not*
filtered as broadphase body pairs; they collide via a phantom linear-cast. So instead of a global
collision-filter hook, at **player-arrow launch** we copy the target follower's Havok
**systemGroup** onto the arrow's phantom collidable (`unk0E0` → `collidable.broadPhaseHandle.collisionFilterInfo`).
The arrow then ignores that follower exactly as it already ignores its own shooter, sweeping
through and continuing to the enemy behind. No per-frame hook, no hot path. New plugin
`plugins/GhostAllies/`, independent of AutoFireBow.

**Tech Stack:** C++23, CommonLibSSE-NG (FetchContent, AE+SE), spdlog; headless clang-cl + lld-link + xwin cross-build (Linux→Windows DLL); Address Library for version-independent offsets.

**Domain note on verification:** this builds a Windows DLL that only does anything inside a running Skyrim. There is no host-side unit harness. So each task's "test cases" are: (a) the build succeeds and emits a PE32+ DLL, (b) specified lines appear in `GhostAllies.log`, and (c) the described in-game observation holds. Treat the log assertions as the executable spec.

---

## Reference material

- Design: `docs/plans/ghost-allies-design.md` (read first — mechanism, scope, risks, open decisions).
- Pattern source in-repo: `plugins/AutoFireBow/{CMakeLists.txt,build.sh,src/main.cpp}` — copy its CMake, `build.sh`, logging setup, vtable-hook idiom (`REL::Relocation::write_vfunc`), and `SKSEPluginInfo`/`SKSEPluginLoad` boilerplate verbatim where applicable.
- Shared toolchain (do not duplicate): `plugins/cross-env.sh`, `plugins/cmake/clang-cl-msvc.cmake`, `tools/env.sh` (provides `$GAME_DATA` for `--install`).
- External precedent to mine for the hook + group logic: `adamhynek/higgs` (`src/hooks.cpp` — `CompareFilterInfo` hook, `CollisionFilterComparisonResult{Collide,Ignore,Continue}`), `adamhynek/activeragdoll` (`src/main.cpp` — `CollisionFilterComparisonCallback`, `GetCollisionGroup(info)=info>>16`, per-actor ignore set), `powerof3/PapyrusExtenderSSE` (`GetCollisionFilterInfo`, system-group read).

## File structure

| File | Responsibility |
|------|----------------|
| `plugins/GhostAllies/CMakeLists.txt` | Build the `GhostAllies.dll` target; same FetchContent deps as AutoFireBow (spdlog pin, CommonLibSSE-NG pinned tag). |
| `plugins/GhostAllies/build.sh` | Configure+build via clang-cl toolchain; `--install` copies DLL to `$GAME_DATA/SKSE/Plugins`. Copy of AutoFireBow's with names changed. |
| `plugins/GhostAllies/src/main.cpp` | Entire plugin: log setup, the `CompareFilterInfo` hook, the two group sets + their maintenance hooks, and the selectivity decision. Single file is appropriate at this size (AutoFireBow is one 328-line file); split only if it grows past ~400 lines. |

---

### Task 1: Plugin scaffolding that loads in-game [Mode: Direct]

**Files:**
- Create: `plugins/GhostAllies/CMakeLists.txt`
- Create: `plugins/GhostAllies/build.sh`
- Create: `plugins/GhostAllies/src/main.cpp`

**Contracts:**
- `CMakeLists.txt`: identical structure to `plugins/AutoFireBow/CMakeLists.txt` with `project(GhostAllies ...)` and target renamed; same spdlog v1.13.0 pin, same CommonLibSSE-NG `GIT_TAG`, `cxx_std_23`, `PREFIX ""`/`SUFFIX ".dll"`.
- `build.sh`: copy of AutoFireBow's with `AutoFireBow`→`GhostAllies`; same `cross-env.sh` source and `--install` path.
- `main.cpp`: `SetupLog()` writing to `<SKSE log dir>/GhostAllies.log` (AutoFireBow idiom); `SKSEPluginInfo(.Name="GhostAllies", .RuntimeCompatibility=AddressLibrary, .StructCompatibility=Independent)`; `SKSEPluginLoad` calls `SetupLog()`, `SKSE::Init`, logs `"GhostAllies <ver> loaded"`.

**Test Cases (verification):**
- `./plugins/GhostAllies/build.sh` exits 0; `file build/GhostAllies.dll` reports `PE32+ executable (DLL) (console) x86-64, for MS Windows`.
- `./plugins/GhostAllies/build.sh --install` copies to `$GAME_DATA/SKSE/Plugins/GhostAllies.dll`.
- After launching the game: `GhostAllies.log` exists and contains `GhostAllies ... loaded`.

**Constraints:**
- No behavior yet — this task only proves the new plugin builds on the shared toolchain and loads.

**Commit after passing.**

---

### Task 2: Resolve + hook `CompareFilterInfo` (the make-or-break proof-point) [Mode: Delegated]

**Files:**
- Modify: `plugins/GhostAllies/src/main.cpp`

**Contracts:**
- Install a hook on the engine's per-pair collision-filter decision, `bhkCollisionFilter::CompareFilterInfo`, resolved **version-independently** for **AE 1.6.1170** — never hardcode HIGGS's `0xE2BA10` (that is the 1.5.97 SE address).
  - **Expected primary path — Address Library branch-hook.** `CompareFilterInfo` is *not* exposed as a hookable virtual in CommonLibSSE-NG (verified: `RE/H/hkpCollisionFilter.h` only models `~dtor`/`Init`/`NumShapeKeyHitsLimitBreached`; the collide-comparison filter lives in the `hkpCollidableCollidableFilter` base at offset 0x08 and isn't a modeled slot). So branch-hook the function via an Address Library `REL::ID` resolved for the **AE** build (do not assume the SE id), using a CommonLib trampoline `write_branch`. Document which ID was used and where it came from (e.g. meh321 AE DB / cross-referenced from HIGGS's SE address).
  - **Only if** a hookable virtual turns out to exist after inspecting the FetchContent'd `RE/B/bhkCollisionFilter.h` / `RE/H/hkpCollisionFilter.h` headers, prefer `REL::Relocation::write_vfunc` (AutoFireBow idiom) — but do not spend long here; the branch-hook is the realistic route.
- Hooked thunk signature mirrors the engine's: receives the filter and the two `uint32` filterInfos, returns the original enum result. For this task it must **always defer to the original** (behavior-neutral) and only log.
- Add a throttled probe log: decode and log `layer = info & 0x7F` and `group = info >> 16` for a small sample of pairs (rate-limit so it doesn't flood — e.g. first N pairs after load, or 1-in-M), enough to confirm the hook fires and to eyeball real projectile/biped/charcontroller layer + group values.

**Test Cases (verification):**
- Build succeeds; DLL installs.
- In-game, during normal movement/combat, `GhostAllies.log` shows the probe firing with plausible decoded `layer`/`group` values (e.g. char-controller layer for actors).
- Game is **stable** — no crash, no visible physics change (behavior-neutral defer). Play for a couple of minutes near NPCs.

**Constraints:**
- This is the critical dependency for the whole plugin. If the function cannot be hooked stably at the AE runtime, **stop and report** rather than proceeding — the design's fallback (per-projectile system-group override at launch) would need to be revisited instead.
- The thunk must be fast and must not allocate; the probe log must be throttled because this is the hottest callback in the physics step.

**Commit after passing.**

---

> **Tasks 3 & 4 below replace the original group-set + filter-callback tasks**, which the Task 2
> proof-point invalidated (arrows don't go through the discrete filter). The new single task
> implements the launch-time systemGroup stamp. The probe hooks added in Task 2 are removed by it.

### Task 3: Stamp the follower's systemGroup onto the player-arrow phantom at launch [Mode: Delegated]

**Files:**
- Modify: `plugins/GhostAllies/src/main.cpp` (remove the Task 2 dual-virtual probe hooks; add the launch stamp)

**Contracts:**
- **Pick the launch hook.** Reuse AutoFireBow's proven per-arrow signal, `ArrowProjectile::GetPowerSpeedMult` (`VTABLE_ArrowProjectile[0]`, AE vtable slot `0xB0`), where `GetProjectileRuntimeData().shooter` is available. Gate on shooter `IsPlayerRef()`. **Verify the phantom exists at this point**: `GetProjectileRuntimeData().unk0E0` (the `bhkSimpleShapePhantom`, offset `0x0E0`). If it's reliably null at GetPowerSpeedMult time, fall back to a projectile 3D-loaded / first-`UpdateImpl` hook. Stamp **once** per arrow (guard like AutoFireBow's `logged` flag).
- **Resolve the follower to ignore (v1 = single):** find the player's current teammate. Iterate the appropriate actor source (e.g. `ProcessLists` high actors, or `PlayerCharacter` follower data) and select an `Actor` with `IsPlayerTeammate()`. If more than one, pick the **nearest to the player**. If none, do nothing (arrow behaves vanilla).
- **Read the follower's systemGroup:** `Actor::GetCollisionFilterInfo(uint32_t&)` then `>> 16` (or `Actor::GetCharController()` → `bhkCharacterController::GetCollisionFilterInfo`, slot `0x08`).
- **Stamp the phantom collidable:** reach `unk0E0` → `bhkShapePhantom`'s `hkpShapePhantom` → `collidable.broadPhaseHandle.collisionFilterInfo`, then:
  ```
  info &= 0x0000FFFF;                  // keep arrow's own layer + subsystem bits
  info |= (followerSystemGroup << 16); // adopt the follower's systemGroup
  ```
  (Precedents: `vinymayan/Parry-for-all`, `D7ry/EldenParry`, `Soaringwhale` RfaD `_set_proj_collision_layer`.)
- Keep a low-rate log line on each stamp (`stamped arrow -> follower <name/id>, group <g>`) for verification; removable later.

**Test Cases (verification — v1 definition of done):**
- Stand your follower directly between you and an enemy; fire → **arrows pass through the follower and hit the enemy behind**; follower takes no damage and doesn't aggro. Log shows the stamp line.
- No follower present → arrows hit the world/enemies exactly as vanilla.
- Follower off to the side (not in line) → enemy still hit normally; follower unaffected.
- Enemy archer shooting your follower → follower hit normally (only *player* arrows are stamped).

**Constraints:**
- **Stop-and-report gates:** (1) if `unk0E0` is null at the chosen hook and no working fire point exists, report before guessing; (2) if stamping the follower's systemGroup does **not** produce pass-through in-game (e.g. the subsystem don't-collide pairing doesn't apply to the cast as the precedents imply), report — the fallback is the `ArrowProjectile` slot-190 hook (skip impact for the follower hit + stamp on first contact).
- Only ever stamp **player-fired** arrows. Never modify non-player projectiles.
- Single-follower per arrow is an accepted v1 limitation (systemGroup is one 16-bit value) — do not attempt multi-follower here.

**Commit after passing.**

---

## Execution
**Skill:** superpowers:subagent-driven-development
- Mode A: Opus implements directly (Task 1 — done).
- Mode B: Dispatched to subagents (Task 2 proof-point — done, drove the pivot; Task 3 launch-stamp — each needs CommonLibSSE-NG/header exploration, RE judgement, and has stop-and-report gates).
