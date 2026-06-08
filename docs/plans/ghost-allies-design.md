# GhostAllies — design

**Status:** ✅ shipped — v1 working in-game (2026-06-08). Arrows pass through the nearest follower
via launch-time systemGroup stamp. Pivoted away from the original collision-filter approach (see
"Pivot" below). Multi-follower / spells deferred to v2 (`docs/ideas.md`).
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

> **Pivot (2026-06-08).** The original design targeted the global collision **filter**
> (`bhkCollisionFilter::CompareFilterInfo` / `IsCollisionEnabled`), on the theory that arrows are
> filtered as broadphase body pairs. In-game probing (Task 2 proof-point) **disproved that**:
> arrows are not simulated rigid bodies in the broadphase and never reach the discrete
> collidable-collidable filter. They collide via a **phantom linear-cast** (CCD sweep) whose cast
> input carries no projectile-identifying layer at the filter level — so a global-filter decision
> can't cleanly say "this is a player arrow vs a follower." The filter approach is **abandoned**.
> The mechanism below is its replacement, and is simpler (no hot-path callback at all). The probe
> findings that drove the pivot are preserved in `00-docs`/commit history.

### How arrows actually collide

Each frame, `Projectile::UpdateImpl` drives the projectile's own **`bhkSimpleShapePhantom`**
(`Projectile` runtime field `unk0E0`, offset `0x0E0`) and performs a **shape linear-cast** along
the arrow's movement, collecting contacts. What that cast sweeps through is governed by the
phantom collidable's `broadPhaseHandle.collisionFilterInfo`. The cast result is delivered to the
arrow's collision handler (`ArrowProjectile` vtable **slot 190**), which runs `AddImpact` and
sticks/destroys the arrow.

Crucially, the engine already makes every arrow **ignore its own shooter** through Havok's
system-group rule (`hkpGroupFilter::isCollisionEnabled`): bodies in the same non-zero
**systemGroup** (filterInfo bits 16–31) with a matching subsystem don't-collide pairing are
skipped. The arrow is launched with exactly that relationship to its shooter.

### The mechanism: stamp the follower's systemGroup onto the arrow at launch

At player-arrow launch, copy the **target follower's systemGroup** onto the arrow's phantom
collidable, so the arrow treats that follower the same way it treats its own shooter — the cast
never reports a contact against the follower, so the arrow sweeps **through** and keeps flying,
still hitting everyone else (different systemGroups → normal collision):

```
info  = phantom.collidable.broadPhaseHandle.collisionFilterInfo
info &= 0x0000FFFF                  // keep the arrow's own layer + subsystem bits
info |= (followerSystemGroup << 16) // adopt the follower's systemGroup
```

- The follower's systemGroup is read via `Actor::GetCollisionFilterInfo(uint32&)` (`>> 16`), or
  `Actor::GetCharController()` → `bhkCharacterController::GetCollisionFilterInfo`.
- Fire point: a per-arrow launch hook where the shooter is known and the phantom exists. The
  repo already hooks `ArrowProjectile::GetPowerSpeedMult` (AutoFireBow) with `runtime.shooter`
  available; reuse that idiom (null-check `unk0E0`; if the phantom isn't ready there, move to a
  projectile 3D-loaded / first-update hook).

This is the community-standard idiom for making a projectile pass through a specific actor and
continue — proven in `vinymayan/Parry-for-all`, `D7ry/EldenParry`, `Soaringwhale` RfaD
(`_set_proj_collision_layer`), and others.

### Selectivity & scope

- **Who to ignore (v1):** the player's single current follower (`IsPlayerTeammate`). With one
  arrow able to carry only one systemGroup, **single-follower per arrow is a hard constraint** of
  this mechanism — and it matches the "my companion tanks" use case. If multiple teammates are
  present, v1 stamps the nearest one; documented limitation.
- **Only player-fired arrows** are stamped (shooter check in the launch hook). Everything else is
  untouched.

### Known trade-offs (accepted for v1)

- The stamped arrow stops ignoring the **player** (its own shooter), since systemGroup is a single
  value we overwrite — acceptable, the player is behind the bow.
- The arrow also passes through anything sharing the follower's systemGroup (e.g. that follower's
  mount/summon) — rare, accepted.

### Multi-follower (deferred to v2)

Either (a) assign all "ghost ally" followers to one shared custom systemGroup and stamp that group
on player arrows, or (b) hook `ArrowProjectile` slot 190 with a set of follower IDs and manage
per-frame re-hit suppression. Both add complexity; out of scope for v1.

## Architecture

- **New, standalone plugin:** `plugins/GhostAllies/`, built with the existing headless
  clang-cl + lld-link + xwin + CommonLibSSE-NG (FetchContent) toolchain, mirroring
  `plugins/AutoFireBow/`'s CMake/cross-env setup. It loads, ships, and disables independently of
  AutoFireBow — chosen over folding into AutoFireBow to keep each DLL to one responsibility.
- **Logging:** own `GhostAllies.log` in the SKSE log dir (same pattern as AutoFireBow), used for the
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
