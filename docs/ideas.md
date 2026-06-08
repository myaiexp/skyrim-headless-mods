# Ideas — skyrim-headless-mods

Future work, deferred features, and things worth revisiting. Each entry is WHAT, not HOW.

## 2026-06-08 — GhostAllies (projectiles phase through followers)

State: **v1 shipped** (player arrows through nearest follower). **v2 designed** (`docs/plans/
ghost-allies-design.md` → "## v2 design") and consumes the former "spells" item plus most of
"broaden target set" via whole-party multi-follower. Still deferred after v2:

- **Runes + wall spells.** `GrenadeProjectile` (runes / lobbed) and `BarrierProjectile` (wall
  spells) are explicitly out of v2 scope — different (arc / placed) collision feel. Add later by
  extending the same unified `UpdateImpl` stamp to those subclass vtables.
- **Broaden the "phase through" target set beyond teammates.** Player summons (atronachs,
  reanimated thralls) and optionally any non-hostile actor. v2 keys off `IsPlayerTeammate()`; each
  extra category is a different engine flag — fold them into the same "ghost group" membership set.
- **MCM / INI configuration.** Toggles for: arrows on/off, spells on/off, per-projectile-type, and
  which actor categories phase (teammates / summons / all non-hostiles). v2's membership set is
  already category-shaped, so this is mostly surfacing it as config (mirror the AutoFireBow INI
  decision below).
- **Continuous spells (flame/beam/cone) don't phase via the stamp — needs a different fallback.**
  Confirmed in-game 2026-06-08: a FlameProjectile *is* stamped (the phantom exists, the follower's
  group is written onto it) but the follower still takes damage — continuous streams apply their
  effect through per-frame hit detection that the broadphase systemGroup filter doesn't gate.
  Beam/cone expected to match (untested, no spell). Fallback to try: hook `AddImpact` (slot `0xBD`)
  on those subclasses and skip teammate hits — but the stream's damage may not even flow through
  `AddImpact`, in which case the real fix is hooking the magic-effect/hit application and skipping
  teammates (the "no friendly fire" approach — pragmatic for streams, which visually touch anyway).
  Discrete projectiles (arrows, aimed missile spells) are unaffected — they phase via the stamp.

**Dropped, not deferred:** two-way phasing (follower-fired projectiles through the player).
Followers rarely friendly-fire the player, so it solves a non-problem. Only revisit if real
gameplay shows follower projectiles blocking/hitting the player often enough to matter.

## 2026-06-08 — AutoFireBow config (deferred)

Make the mod configurable instead of always-on. Settings worth exposing:
- **Master on/off**, plus a configurable **toggle hotkey**.
- **Auto-fire** on/off independently of **full-power clamp** on/off — they're separate mechanisms
  in the code (`BowLoopSink` vs `PowerSpeedHook`), so some users will want one without the other.
  **Contingent:** the real-charge spike (`docs/plans/autofirebow-real-charge-design.md`) aims to
  *delete* `PowerSpeedHook` outright. If it lands, there's no clamp left to toggle — auto-fire just
  looses honestly-charged shots, and this split collapses to a single auto-fire on/off.
- *(maybe)* min delay between auto-shots — a cadence cap.

Gating is cheap: a few `bool` globals checked in `PowerSpeedHook::thunk` and the `BowDrawn` handler.

**Decision (how):** ship config as an **INI read by the DLL** (SimpleIni ships with CommonLib) +
the toggle **hotkey** wired into the existing `AttackInputSink`. Zero new user dependencies, no
`.esp`, stays in the pure-C++ tier. Settings are global (not per-save) — correct for an on/off mod.

**Considered and rejected for v1:**
- *Classic SkyUI MCM (Papyrus)* — needs an esp + `.pex` extending `SKI_ConfigBase` + SkyUI + a
  C++↔Papyrus bridge. Drags the Papyrus tier back into a pure-C++ plugin. Worst fit.
- *MCM Helper* — no Papyrus authoring (generic script), familiar Esc→Mod Configuration UX, but
  pulls in SkyUI + MCM Helper as required mods and still needs a tiny ESL. Revisit only if release
  comments ask for a real in-game menu.
- *SKSE Menu Framework (ImGui)* — pure C++, no esp/SkyUI/Papyrus, menu rendered from the DLL;
  architecturally the cleanest fit, but niche UX and small install base. Skipped for familiarity.
