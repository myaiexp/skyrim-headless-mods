# GhostAllies — design

**Status:** v1 ✅ shipped & verified in-game (2026-06-08) — arrows pass through the nearest follower
via launch-time systemGroup stamp. **v2 designed (2026-06-08, approved), not yet built** — extends
the effect to spell projectiles and folds in whole-party multi-follower (see "## v2 design" below).
Pivoted away from the original collision-filter approach (see "Pivot" below).
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
per-frame re-hit suppression. Both add complexity; out of scope for v1. (v2 below adopts (a).)

## v2 design — spells + whole-party multi-follower

**Status:** approved 2026-06-08, not yet built. Supersedes v1's arrow-only, single-nearest,
read-only stamp. v1's mechanism stays documented as the trivially-revertible fallback (below).

### Goal

Extend the phase-through effect from arrows to **spell projectiles**, and from the single nearest
follower to the player's **entire party**. Same core mechanism (Havok systemGroup on the
projectile's phantom), generalized.

### Key structural finding (CommonLibSSE-NG headers)

`ArrowProjectile : MissileProjectile : Projectile`. Every projectile subclass overrides the same
virtuals at the same vtable slots: `Process3D` (0xA9), **`UpdateImpl` (0xAB)**, `Handle3DLoaded`
(0xC0). The phantom (`unk0E0`) and `shooter` live on the base `Projectile`, so v1's stamp body
applies **unchanged** to every projectile type. This is what makes one unified hook the genuinely
best design, not merely the tidy one.

### 1. Unified hook (replaces v1's arrow-only `GetPowerSpeedMult` hook)

- Extract v1's inline stamp into a single `StampProjectilePhantom(RE::Projectile*)`.
- Install that one thunk on **`UpdateImpl` (vtable slot `0xAB`)** of each in-scope subclass vtable.
- **Drop the v1 `GetPowerSpeedMult` (`0xB0`) hook** — arrows now stamp via the same `UpdateImpl`
  path as everything else. (That slot conceptually belonged to AutoFireBow's idiom anyway; v1 only
  borrowed it as a convenient per-arrow launch signal.)
- **Why `UpdateImpl`, not `Handle3DLoaded`:** `UpdateImpl` is the exact frame the phantom
  linear-cast runs, so the phantom is **guaranteed present** (v1's `GetPowerSpeedMult` hook could
  only caveat that it *might* not be ready). The existing idempotence guard — stamp only when the
  phantom's top-16 group bits don't already equal the ghost group — makes per-frame entry one cheap
  compare after the first stamp; no per-frame cost of consequence.
- **Why per-subclass vtables, not one base-`Projectile` hook:** each subclass carries its own
  vtable, so hooking each subclass is precisely the lever that **includes** the wanted types and
  **excludes** runes/walls — by simply not hooking theirs. No type-tag branching in the hot path.

### 2. Scope (which projectile types)

| Type | Hooked? | Notes |
|------|---------|-------|
| `ArrowProjectile`  | ✅ | arrows/bolts (was v1) |
| `MissileProjectile`| ✅ | aimed bolt spells: Firebolt, Ice Spike, Fireball, … (the core spell ask) |
| `FlameProjectile`  | ✅ | Flames stream — continuous; verify pass-through holds (see risks) |
| `BeamProjectile`   | ✅ | Lightning beams — continuous; verify |
| `ConeProjectile`   | ✅ | cone spells — continuous; verify |
| `GrenadeProjectile`| ❌ deferred | runes / lobbed; different (arc, placed) collision feel — out of scope |
| `BarrierProjectile`| ❌ deferred | wall spells — not an aimed flyer |

Flame/beam/cone are continuous-collision types: the stamp is applied uniformly, but whether each
actually phases via the phantom-group route is a **per-type in-game verification** item. Types that
don't phase get documented; fallback for a stubborn type is its `AddImpact` (slot `0xBD`) handler —
skip the impact when the hit ref is a teammate.

### 3. Whole-party multi-follower (folded in — will remain untested)

A fixed reserved **"ghost" `systemGroup` constant** shared by all current teammates. Membership is
maintained **lazily from `main.cpp` with no event hooking**, inside the stamp path:

1. Resolve the current teammate set (`IsPlayerTeammate()` over `ProcessLists` high actors).
2. For each current teammate **not yet enrolled**: save its original systemGroup in a
   `FormID → originalGroup` map, then write the ghost group onto its char-controller filterInfo.
3. For each enrolled actor **no longer a teammate**: restore its saved original group, drop it
   from the map.
4. Stamp the projectile's phantom with the **ghost group** → it phases through the whole party.

Writes are idempotent (only touch an actor whose group isn't already the ghost group), so the
maintenance is near-free after the first projectile and self-heals as the party changes. Single
code path handles 1..N followers identically.

The ghost group must be a value that won't collide with engine-assigned groups (engine groups are
derived per object); pick a high reserved constant and confirm no clash in the probe log. Precedent
for custom systemGroups: `adamhynek/activeragdoll`.

### Trade-offs (accepted)

- **Supersedes the verified single-follower path.** v1 only *read* the follower's own group and
  never modified actors; v2 *writes* a synthetic group onto every teammate. So even the
  single-follower case becomes mechanism-unverified. Accepted: Mase won't install to test
  multi-follower, and v1's read-only nearest stamp is retained in this doc as the trivially
  revertible fallback (restore `StampHook` reading `follower->GetCollisionFilterInfo() >> 16` and
  stamping that, no teammate writes).
- **Same-group followers may visually interpenetrate** each other (group filter suppresses
  intra-group collision). World collision is preserved (world isn't in the group), so no
  fall-through-floor. Cosmetic, accepted.
- **A stamped projectile stops ignoring the player** (its own shooter) — unchanged from v1,
  accepted (player is behind the shot).

### v2 verification (definition of done)

Per in-scope type (arrow, missile, and best-effort flame/beam/cone):

1. Stand one or more teammates between the player and an enemy; cast/fire → projectile passes
   through **all** intervening teammates and strikes the enemy; no teammate takes damage or aggros.
2. No follower in line → vanilla collision.
3. Enemy-cast spells → unaffected (only player-shot projectiles are stamped).
4. Runes (grenade) → still collide normally (confirm out-of-scope unchanged).
5. Frame time with a full party present is not visibly degraded.

(Multi-follower and flame/beam/cone pass-through are expected to ship **unverified** — Mase won't
install to test. Document observed results if/when play data appears.)

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

## Continuing from here (v2 entry points)

All v1 logic is one file: `plugins/GhostAllies/src/main.cpp`. The whole effect is `StampHook::thunk`
(hooked on `ArrowProjectile::GetPowerSpeedMult`, `VTABLE_ArrowProjectile[0]` slot `0xB0`) +
`FindNearestFollower`. Build/iterate with `./plugins/GhostAllies/build.sh --install`, restart the
game, check `<SKSE log dir>/GhostAllies.log` for `stamped player arrow -> follower …`. (Note: the
sibling bow plugin was renamed RapidBow → **AutoFireBow**; in-code comments still say "RapidBow" —
harmless, same idioms.)

The three deferred features (`docs/ideas.md`) and how each plugs in:

- **Multi-follower.** A single arrow's systemGroup can only match one actor, so true "pass through
  all followers" needs a different tack: assign every current teammate a **shared custom
  systemGroup** (write it onto each follower's char-controller filterInfo on follower-change/load),
  then stamp that one shared group on player arrows. Verify the subsystem don't-collide pairing
  still triggers pass-through when several actors share the group (untested — was proven only for
  single-actor stamping).
- **Spells.** Magic projectiles are `MissileProjectile`, **not** `ArrowProjectile`, and have no
  draw power, so there is **no `GetPowerSpeedMult` to hook** — the v1 launch hook does not exist for
  them. Pick a different per-missile launch fire point (e.g. a `MissileProjectile` 3D-loaded / first
  `UpdateImpl` hook), then reuse the exact same phantom-stamp code (`unk0E0` →
  `collisionFilterInfo`). Collision handler for missiles, if needed, is `VTABLE_MissileProjectile[0]`
  slot `190`.
- **MCM / config.** No `.esp` needed; mirror the AutoFireBow plan (`docs/ideas.md`) — INI read by the
  DLL (SimpleIni ships with CommonLib) for arrows/spells/which-categories toggles.

Fallback if the stamp ever regresses (e.g. a game update changes the cast filter semantics): hook
`ArrowProjectile` slot `190` (`OnArrowCollision`), and on a hit whose only target is the follower,
`return;` without the original to skip the impact, stamping the phantom on first contact. Proven in
`vinymayan/Parry-for-all`, `D7ry/EldenParry`.

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
