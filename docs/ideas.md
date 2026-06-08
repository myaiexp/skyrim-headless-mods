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
