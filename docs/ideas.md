# Ideas — skyrim-headless-mods

Future work, deferred features, and things worth revisiting. Each entry is WHAT, not HOW.

## 2026-06-08 — GhostAllies (arrows phase through followers)

Deferred out of v1 (see `docs/plans/ghost-allies-design.md`). v1 ships player-fired arrows
phasing through hired followers only; these extend it:

- **Spells / magic projectiles through followers.** The original ask included spells. Magic uses
  a different projectile path than arrows; the same `CompareFilterInfo` hook should cover spell
  projectiles if they carry the projectile collision layer, but it needs its own verification.
  Aimed-spell archers / battlemages benefit.
- **Broaden the "phase through" target set.** Beyond hired teammates: player summons (atronachs,
  reanimated thralls), and optionally any non-hostile actor. Each is a different engine flag;
  decide per-category.
- **MCM configuration.** Toggles for: arrows on/off, spells on/off, and which actor categories to
  phase through (teammates / summons / all non-hostiles). The selectivity sets in the design are
  already category-keyed, so this is mostly surfacing them as config.

**Dropped, not deferred:** two-way phasing (follower-fired arrows through the player). Followers
rarely friendly-fire the player, so it solves a non-problem. Only revisit if real gameplay shows
follower arrows blocking/hitting the player often enough to matter.
