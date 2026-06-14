# GhostAllies

> **Status: working, verified in-game (v0.9.0).** Player projectiles stop harming your
> followers — and discrete shots physically **pass through** them.

Fire a bow or an aimed spell past a companion who's standing in your line of fire and the shot
**passes straight through them** to hit the enemy behind. Your party never blocks the shot, takes
the damage, or turns hostile. It's the engine layer below the usual "no friendly fire" mods: the
arrow never collides with your ally at all, instead of stopping on them with the damage zeroed.

## What it does

- **Arrows & bolts pass through your whole party** — physically. The projectile sweeps past every
  intervening teammate and continues to whatever's behind them.
- **Aimed spells pass through too** — bolt-type projectiles (Firebolt, Ice Spike, Fireball and the
  like) phase through the party exactly like arrows.
- **No friendly fire from continuous streams** — Flames, Sparks, and other concentration streams
  deal **no damage** to your teammates. (They don't *pass through*, though — see Limitations.)
- **Everyone else is untouched.** Only player-fired projectiles aimed at a teammate phase. Enemies,
  neutral NPCs, and your own summons are hit completely normally; enemy spells are unaffected.

## Requirements

- Skyrim Special Edition or Anniversary Edition + **SKSE**
- **Address Library for SKSE Plugins**

No `.esp`, no scripts, no Papyrus — a single SKSE DLL. It takes no load-order slot.

## Compatibility

- **SE + AE — one DLL for both.** Built on CommonLibSSE-NG; every engine address is resolved at
  runtime through the Address Library, so the same file runs on every SE and AE build (Steam or
  GOG) as long as Address Library is installed. **Verified in-game on AE** (v1.6.1170).
- **VR — untested.** No VR-specific build is provided.
- Plays fine alongside the "no follower collision / no friendly fire" mods, but it makes them
  redundant for player projectiles.

## Installation

1. Install with a mod manager and enable it (or drop `GhostAllies.dll` into
   `Data/SKSE/Plugins/`).
2. **Restart Skyrim.**

That's all — it's active immediately, always on, no configuration.

## How it works

Two complementary mechanisms, both verified in-game:

**1. systemGroup stamp → true pass-through (discrete projectiles).** Skyrim already makes every
arrow ignore its own shooter via Havok's *system-group* rule: bodies sharing a non-zero systemGroup
don't collide. GhostAllies reuses that. It reserves one "ghost group" constant, stamps it onto each
current teammate's character-controller, and stamps the same group onto a player-fired projectile's
collision phantom at launch. The projectile's per-frame collision cast then treats every ghost-group
teammate the way it treats the player — it never reports a contact, so the shot sweeps through and
keeps flying, still hitting everyone else (different group → normal collision). One unified hook on
each projectile subclass's `UpdateImpl` covers arrows and aimed spells with the same code; teammate
membership is maintained lazily, so it self-heals as the party changes and costs almost nothing after
the first shot.

**2. `MagicTarget::AddTarget` refusal → no friendly damage (continuous streams).** Stream damage
(Flames/Sparks) flows through the magic-effect system, not the broadphase collision filter, so the
stamp can't gate it. GhostAllies instead refuses to apply a **hostile** player magic effect to a
**teammate** at `MagicTarget::AddTarget`. Beneficial effects (heals, buffs on followers), enemy
damage, and non-player casters are all left alone.

A `GhostAllies.log` in the SKSE log dir records the stamp/enroll decisions for in-game verification.

## Limitations

- **Continuous streams don't pass through — they just don't hurt allies.** For Flames/Sparks the
  shipped outcome is "no friendly damage," *not* the stream reaching the enemy behind your ally.
  True pass-through for streams is **parked as structurally infeasible**: unlike discrete
  projectiles, `FlameProjectile`/`BeamProjectile` expose no collision-point hook, and their stop
  point is computed by a *layer*-filtered cast that ignores systemGroup entirely. No existing mod
  does it. Full evidence and the four rejected attempts: `docs/plans/ghost-allies-design.md` §2b.
- **Whole-party pass-through ships unverified.** Single-follower arrows and aimed spells (Firebolt)
  are confirmed in-game; the multi-follower path uses the same code but hasn't been play-tested with
  a full party.
- **A stamped projectile also ignores the player.** systemGroup is a single value, so while a shot
  is phasing your party it stops ignoring you too — harmless, since you're behind the shot.

## Building from source

Linux, headless — no Creation Kit or SSEEdit.

```bash
./build.sh            # configure + build -> build/GhostAllies.dll
./build.sh --install  # also copy the DLL into the live game's SKSE/Plugins
```

Cross-compiled Linux → Windows with the in-repo `tools/skse` toolchain (clang-cl + lld-link + xwin;
CommonLibSSE-NG fetched and pinned by CMake). See `../../docs/skse-toolchain.md`.

## Design notes

The full design — the pivot away from the global collision-filter approach, why `UpdateImpl` is the
right hook, the continuous-stream dead-ends, and the multi-follower scheme — lives in
`../../docs/plans/ghost-allies-design.md` (with `ghost-allies-plan.md` and `ghost-allies-v2-plan.md`
for the build steps). Deferred work (runes/wall spells, broadening the phase-through set, an MCM)
is in `../../docs/ideas.md`.
