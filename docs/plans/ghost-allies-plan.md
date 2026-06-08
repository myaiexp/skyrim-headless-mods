# GhostAllies Implementation Plan

**Goal:** A standalone SKSE plugin that lets player-fired arrows/bolts physically pass through hired followers and continue to the enemy behind.

**Architecture:** Hook `bhkCollisionFilter::CompareFilterInfo` (the per-pair Havok collision-filter decision) and return **Ignore** for the pair (player-fired projectile × player-teammate body), so the engine never generates a contact — the arrow flies on. Selectivity is driven by two group-id sets maintained *outside* the hot path (current-teammate groups; in-flight player-projectile groups), so the per-pair callback is two set lookups behind a cheap collision-layer pre-filter. New plugin `plugins/GhostAllies/`, independent of RapidBow.

**Tech Stack:** C++23, CommonLibSSE-NG (FetchContent, AE+SE), spdlog; headless clang-cl + lld-link + xwin cross-build (Linux→Windows DLL); Address Library for version-independent offsets.

**Domain note on verification:** this builds a Windows DLL that only does anything inside a running Skyrim. There is no host-side unit harness. So each task's "test cases" are: (a) the build succeeds and emits a PE32+ DLL, (b) specified lines appear in `GhostAllies.log`, and (c) the described in-game observation holds. Treat the log assertions as the executable spec.

---

## Reference material

- Design: `docs/plans/ghost-allies-design.md` (read first — mechanism, scope, risks, open decisions).
- Pattern source in-repo: `plugins/RapidBow/{CMakeLists.txt,build.sh,src/main.cpp}` — copy its CMake, `build.sh`, logging setup, vtable-hook idiom (`REL::Relocation::write_vfunc`), and `SKSEPluginInfo`/`SKSEPluginLoad` boilerplate verbatim where applicable.
- Shared toolchain (do not duplicate): `plugins/cross-env.sh`, `plugins/cmake/clang-cl-msvc.cmake`, `tools/env.sh` (provides `$GAME_DATA` for `--install`).
- External precedent to mine for the hook + group logic: `adamhynek/higgs` (`src/hooks.cpp` — `CompareFilterInfo` hook, `CollisionFilterComparisonResult{Collide,Ignore,Continue}`), `adamhynek/activeragdoll` (`src/main.cpp` — `CollisionFilterComparisonCallback`, `GetCollisionGroup(info)=info>>16`, per-actor ignore set), `powerof3/PapyrusExtenderSSE` (`GetCollisionFilterInfo`, system-group read).

## File structure

| File | Responsibility |
|------|----------------|
| `plugins/GhostAllies/CMakeLists.txt` | Build the `GhostAllies.dll` target; same FetchContent deps as RapidBow (spdlog pin, CommonLibSSE-NG pinned tag). |
| `plugins/GhostAllies/build.sh` | Configure+build via clang-cl toolchain; `--install` copies DLL to `$GAME_DATA/SKSE/Plugins`. Copy of RapidBow's with names changed. |
| `plugins/GhostAllies/src/main.cpp` | Entire plugin: log setup, the `CompareFilterInfo` hook, the two group sets + their maintenance hooks, and the selectivity decision. Single file is appropriate at this size (RapidBow is one 328-line file); split only if it grows past ~400 lines. |

---

### Task 1: Plugin scaffolding that loads in-game [Mode: Direct]

**Files:**
- Create: `plugins/GhostAllies/CMakeLists.txt`
- Create: `plugins/GhostAllies/build.sh`
- Create: `plugins/GhostAllies/src/main.cpp`

**Contracts:**
- `CMakeLists.txt`: identical structure to `plugins/RapidBow/CMakeLists.txt` with `project(GhostAllies ...)` and target renamed; same spdlog v1.13.0 pin, same CommonLibSSE-NG `GIT_TAG`, `cxx_std_23`, `PREFIX ""`/`SUFFIX ".dll"`.
- `build.sh`: copy of RapidBow's with `RapidBow`→`GhostAllies`; same `cross-env.sh` source and `--install` path.
- `main.cpp`: `SetupLog()` writing to `<SKSE log dir>/GhostAllies.log` (RapidBow idiom); `SKSEPluginInfo(.Name="GhostAllies", .RuntimeCompatibility=AddressLibrary, .StructCompatibility=Independent)`; `SKSEPluginLoad` calls `SetupLog()`, `SKSE::Init`, logs `"GhostAllies <ver> loaded"`.

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
  - **Only if** a hookable virtual turns out to exist after inspecting the FetchContent'd `RE/B/bhkCollisionFilter.h` / `RE/H/hkpCollisionFilter.h` headers, prefer `REL::Relocation::write_vfunc` (RapidBow idiom) — but do not spend long here; the branch-hook is the realistic route.
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

### Task 3: Maintain the teammate and player-projectile group sets (off the hot path) [Mode: Delegated]

**Files:**
- Modify: `plugins/GhostAllies/src/main.cpp`

**Contracts:**
- **Group key reconciliation (do this first):** `Projectile::GetCollisionGroup()` returns a `std::uint32_t`, but the design's group is `filterInfo >> 16` (the top 16 bits). Confirm whether the projectile accessor returns the already-shifted group or the full filterInfo, and normalize so the set keys are the **same representation** the Task 4 callback compares (apply `>> 16` consistently on both the projectile side and the actor side). Pick one width/representation and use it everywhere.
- Two module-level sets of system-group ids in that normalized representation, guarded for the physics-thread reads in Task 4 (the hot path only reads; updates happen on game-thread events):
  - `g_teammateGroups` — system groups of current hired followers. Rebuilt (not per frame) on follower-change and load events. Resolve each candidate actor's group via `bhkCharacterController::GetCollisionFilterInfo` (per activeragdoll/po3), include the actor only if `Actor::IsPlayerTeammate()`.
  - `g_playerProjectileGroups` — system groups of in-flight **player-fired** projectiles. Added when a player arrow launches, removed when it is destroyed.
- **Player-projectile capture:** hook a per-arrow launch point where the shooter is known and the projectile's havok body/collision group already exists (reuse RapidBow's proven signal: `ArrowProjectile::GetPowerSpeedMult`, fired once per arrow with `runtime.shooter` available). On a player-shot arrow, read the projectile's collision group (`Projectile::GetCollisionGroup`, research-cited vtable slot ~0xBB — verify against the header) and insert it.
- **Projectile-set lifecycle (resolve the open decision):** pick and implement a concrete removal event so a group id cannot leak and later be recycled to a non-player projectile — e.g. hook the projectile's destroy/`~Projectile` path. State the chosen mechanism in a code comment. **Additionally** add a cheap bounded fallback sweep (e.g. drop ids whose projectile handle is no longer valid, run on a low-frequency event) so a single missed removal cannot silently corrupt selectivity — a leaked id here is the one failure mode that wrongly phases a non-player projectile rather than crashing, so the set must stay self-correcting.
- **Refresh triggers for `g_teammateGroups`:** at minimum `kPostLoadGame`/`kNewGame`, plus a periodic or event-driven rebuild covering follower recruit/dismiss and cell change. Document the chosen trigger set.

**Test Cases (verification):**
- Log set membership on every update (size + the ids).
- In-game: recruiting a follower adds an id to `g_teammateGroups`; dismissing removes it.
- In-game: firing a bow adds an id to `g_playerProjectileGroups`; shortly after the arrow lands/despawns the id is removed (no unbounded growth — fire 30+ arrows and confirm the set returns to empty).

**Constraints:**
- **Critical assumption to validate:** that player-fired projectiles receive a system group that is distinct and stable enough to identify. If projectiles do not get usable distinct groups, the player-vs-enemy distinction breaks (enemy arrows would also phase through your follower) — in that case **stop and report**; the design must be revisited before Task 4.
- All set *mutation* happens on game-thread events, never inside the filter callback.
- No per-frame actor scans for the teammate set — event/triggered rebuilds only.

**Commit after passing.**

---

### Task 4: Wire selectivity into the filter callback — true pass-through [Mode: Delegated]

**Files:**
- Modify: `plugins/GhostAllies/src/main.cpp`

**Contracts:**
- Replace Task 2's log-only thunk body with the decision:
  1. **Layer pre-filter (cheap first gate):** decode both infos' layers; if the pair is not (one `kProjectile` × one biped/char-controller actor layer), return the original result immediately (early-out — protects the hot path).
  2. For a qualifying pair, let `pGroup` = the projectile side's group, `aGroup` = the actor side's group. If `pGroup ∈ g_playerProjectileGroups` **and** `aGroup ∈ g_teammateGroups` → return **Ignore**.
  3. Otherwise → return the original result (vanilla filtering; nothing else changes).
- Remove or gate-off the Task 2 sample probe logging (keep a low-rate "phased" log line for verification, removable later).

**Test Cases (verification — this is the v1 definition of done):**
- Stand a hired follower directly between the player and an enemy; fire arrows → arrows pass through the follower and strike the enemy behind; follower takes **no** damage and does **not** turn hostile. `GhostAllies.log` shows the "phased" line for those shots.
- No follower in the line of fire → arrows hit enemies and the world exactly as vanilla.
- Fire at an enemy while a follower stands off to the side (not in line) → normal hit, follower unaffected.
- Enemy archer firing at your follower → follower is hit normally (enemy projectiles are not in `g_playerProjectileGroups`).
- Frame time with a follower present in a fight is not visibly degraded (eyeball a frame counter in a fixed test fight; record rough before/after).

**Constraints:**
- Hot-path body: layer early-out, then at most two set lookups; no allocation, no locking beyond a cheap read guard.
- On any ambiguity in a pair (can't classify projectile vs actor side), return the original result — never Ignore by default.

**Commit after passing.**

---

## Execution
**Skill:** superpowers:subagent-driven-development
- Mode A tasks: Opus implements directly (Task 1)
- Mode B tasks: Dispatched to subagents (Tasks 2, 3, 4 — each needs CommonLibSSE-NG/header exploration, RE judgement, and has a real stop-and-report risk gate)
