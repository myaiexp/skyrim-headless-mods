# GhostAllies — design

**Status:** approved (brainstorming complete), ready for implementation plan
**Type:** SKSE C++ plugin (tier 2), CommonLibSSE-NG, headless clang-cl toolchain
**Target:** Skyrim SE/AE **v1.6.1170**, SKSE
**Working name:** `GhostAllies` (provisional, rename freely)

## Goal

Let a player-fired arrow/bolt **physically pass through** a hired follower standing in the
line of fire and continue on to whatever is behind them. The archer can spam arrows past a
tanking companion without the companion blocking the shot, taking damage, or aggroing.

This is the **true pass-through** behavior, deliberately distinct from the "no friendly fire"
mods on Nexus (e.g. *No Follower Attack Collision*), which hook the engine **Hit** function and
only zero the damage — the arrow still physically stops on the follower. We intervene one layer
deeper so the arrow never collides with the follower at all.

## v1 behavior (this spec)

- **Trigger:** a projectile whose shooter is the player **and** whose flight path would collide
  with an actor carrying the engine **player-teammate** flag (`Actor::IsPlayerTeammate()`).
- **Result:** that projectile↔teammate collision is suppressed — no contact point, velocity not
  zeroed, no damage, no hit reaction, no aggro. The arrow continues along its original path.
- **Untouched:** enemies, neutral NPCs, the player's summons, and every other actor are hit
  completely normally. Only the (player-projectile × teammate-body) pair phases.
- **Weapons in scope:** arrows and crossbow bolts (`ArrowProjectile`). Magic is **not** in v1.

### Explicitly out of scope for v1

Deferred to a later MCM-configurable version (see `docs/ideas.md`):

- Spells / magic projectiles passing through followers.
- Broadening "who to phase through" beyond teammates (summons, any non-hostile).
- MCM toggles / per-category configuration.

**Dropped entirely** (not deferred — judged not worth building): two-way phasing of
follower-fired arrows through the player. Followers rarely friendly-fire the player, so this
solves a non-problem.

## Mechanism

### Why the collision-filter layer (and not the Hit layer)

The engine resolves a projectile impact in stages: Havok broadphase/narrowphase decides a pair
*may* collide → a contact point is generated and the projectile's velocity is consumed → the
engine's **Hit** function processes damage and reactions. The existing friendly-fire mods hook
that final **Hit** stage, so by the time they run the arrow has *already* collided and stopped —
structurally incapable of producing continuation. True pass-through must prevent the contact from
ever being generated, i.e. act at the **collision-filter** stage.

### The hook

`bhkCollisionFilter::CompareFilterInfo` — the function Havok calls for **every pair of physics
bodies, every frame**, to decide whether they may collide. Its result is one of
`{Collide, Ignore, Continue}`; returning **Ignore** makes the engine treat the pair as
non-existent. This is the same hook HIGGS (`adamhynek/higgs`), activeragdoll
(`adamhynek/activeragdoll`), and "I'm Walkin' Here" use to disable player↔NPC body collision —
a **proven, shipping technique**. We are recombining a known-good hook for a new target pair
(player projectile × teammate), not pioneering an unproven one.

The callback receives `(bhkCollisionFilter*, filterInfoA, filterInfoB)`. Havok's filterInfo
packs: collision **layer** in bits 0–6 (`COL_LAYER`; projectile = `kProjectile`, actor bodies on
biped / char-controller layers) and the actor's **system group** in bits 16–31
(`filterInfo >> 16`).

### Selectivity logic

Because the callback is the hottest function in the physics step, the per-pair decision must be
two cheap set lookups, with all the real work done **outside** the hot path:

- **Teammate group set** — system-group ids of current hired followers. Rebuilt on follower
  add/remove and on cell/load events (not per frame).
- **Player-projectile group set** — system-group ids of in-flight player-fired projectiles.
  Populated by an arrow-launch hook (the RapidBow plugin already hooks arrow launch for its
  charge fix, so this pattern is in hand) and cleared when the projectile is destroyed.

Per-pair callback: if one side's group is in the player-projectile set **and** the other side's
group is in the teammate set → return **Ignore**; otherwise return **Continue** (defer to vanilla
filtering, so nothing else changes).

Resolving a system-group back to its owning `Actor` (to test `IsPlayerTeammate()` when refreshing
the teammate set) follows activeragdoll's template — read each actor's group via
`bhkCharacterController::GetCollisionFilterInfo`.

## Architecture

- **New, standalone plugin:** `plugins/GhostAllies/`, built with the existing headless
  clang-cl + lld-link + xwin + CommonLibSSE-NG (FetchContent) toolchain, mirroring
  `plugins/RapidBow/`'s CMake/cross-env setup. It loads, ships, and disables independently of
  RapidBow — chosen over folding into RapidBow to keep each DLL to one responsibility.
- **Logging:** own `GhostAllies.log` in the SKSE log dir (same pattern as RapidBow), used for the
  proof-point below and for verifying the filter decisions in-game.

## Risks & unknowns

| Risk | Mitigation |
|------|------------|
| **AE address for `CompareFilterInfo`** — HIGGS's `0xE2BA10` is the 1.5.97 SE address; the 1.6.1170 address must be resolved via Address Library. This is the make-or-break dependency; everything hangs off it. | Resolve and verify it **first**, via the proof-point below, before building anything else. |
| **Performance** — the callback runs for every body pair every frame; slow logic tanks FPS. | Two-set membership design keeps the hot path to two lookups; verify frame time in-game with a follower present. Best-effort, accept some overhead. |
| **System-group → Actor resolution** correctness when refreshing the teammate set. | Follow the activeragdoll template; refresh only on follower-change / load events, never per frame. |

## Proof-point (first milestone, before committing to full build)

A throwaway probe plugin that hooks `CompareFilterInfo` at the **resolved AE address** and only
logs that it fires (and a sample of the filterInfo pairs). If it lights up and is stable, the AE
address is confirmed and the rest of the build is well-trodden. If it can't be made to fire/be
stable, stop and reassess before investing in the selectivity logic.

## Verification (definition of done for v1)

1. Stand a hired follower directly between the player and an enemy.
2. Fire arrows: arrows pass through the follower and strike the enemy behind; follower takes no
   damage and does not turn hostile.
3. With no follower in the line, arrows hit enemies and the world exactly as vanilla.
4. Fire at an enemy with a follower *not* in the line: normal hit, no behavior change.
5. Frame time with a follower present is not visibly degraded.

## Precedent (reference implementations)

- `adamhynek/higgs` — `src/hooks.cpp`: hooks `bhkCollisionFilter::CompareFilterInfo`;
  `CollisionFilterComparisonResult {Collide, Ignore, Continue}`.
- `adamhynek/activeragdoll` — `src/main.cpp`: `CollisionFilterComparisonCallback`,
  `GetCollisionGroup(filterInfo) = filterInfo >> 16`, per-actor ignore set — direct template for
  "ignore these actors, still hit those."
- `powerof3/PapyrusExtenderSSE`: `charController->GetCollisionFilterInfo`, system-group
  read/write at runtime.
- `ersh1/Precision` — `IsCharacterControllerHittable`, weapon-vs-charcontroller selective
  collision (reference for the "still hit enemies" gating).
