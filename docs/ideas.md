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
- **~~Continuous spells (flame/beam/cone) don't phase via the stamp~~ — RESOLVED 2026-06-08.**
  Fixed at the magic layer in v2 Task 5: hook `MagicTarget::AddTarget` and refuse player hostile
  effects on teammates (Flames/Sparks verified dealing no friendly damage). `AddImpact` (0xBD) was
  tried first and disproven (it fired/skipped but damage persisted — streams don't damage via
  AddImpact). See `docs/plans/ghost-allies-design.md` §2b.
- **Cosmetic: continuous-stream visual clips a teammate's shield/weapon collidable.** The ghost-group
  write only stamps the **char-controller**, so a stream's _visual_ can still stop on the follower's
  equipped shield/weapon or ragdoll collision body (no damage — that's refused at AddTarget). Fixing
  would require walking each teammate's 3D to stamp every equipment/ragdoll rigid body and redoing it
  on equip changes — judged not worth it for a cosmetic clip. Revisit only if it looks bad enough in
  normal play to matter.

**Dropped, not deferred:** two-way phasing (follower-fired projectiles through the player).
Followers rarely friendly-fire the player, so it solves a non-problem. Only revisit if real
gameplay shows follower projectiles blocking/hitting the player often enough to matter.

## 2026-06-09 — OneClickMap (skip world-map popups)

State: **v1 designed** (`docs/plans/oneclick-map-design.md`), not yet built. v1 is unconditional,
no config. Deferred:

- **Modifier-key escape hatch.** v1 permanently trades away two vanilla options: "Place Marker" on
  a _discovered_ location (you always travel instead), and "Place marker? Yes/No" on an undiscovered
  location when a marker already exists (you get Move/Leave/Remove instead). Both were accepted, but
  a held modifier (e.g. Shift-click) could restore the old behavior on demand — Shift-click a
  discovered marker to place a marker on it; Shift-click an undiscovered location to place/relocate
  there directly. Adds a key-state check in the hook branch.
- **MCM / INI configuration.** Per-behavior toggles: instant-travel on/off, instant-place on/off,
  and whether to remap box #3 (undiscovered-location + marker-exists → management menu) or leave it
  vanilla. Mirror the AutoFireBow INI decision below (SimpleIni in the DLL, no esp/Papyrus).
- **Confirm-on-condition.** Optional re-introduction of a confirm only for long-distance travel, or
  only when carrying a quest-relevant timer — niche, revisit only if play shows accidental travel is
  a real annoyance.

## 2026-06-08 — AutoFireBow config (deferred)

Make the mod configurable instead of always-on. Settings worth exposing:

- **Master on/off**, plus a configurable **toggle hotkey**.
- **Auto-fire** on/off independently of **full-power clamp** on/off — they're separate mechanisms
  in the code (`BowLoopSink` vs `PowerSpeedHook`), so some users will want one without the other.
  **Contingent:** the real-charge spike (`docs/plans/autofirebow-real-charge-design.md`) aims to
  _delete_ `PowerSpeedHook` outright. If it lands, there's no clamp left to toggle — auto-fire just
  looses honestly-charged shots, and this split collapses to a single auto-fire on/off.
- _(maybe)_ min delay between auto-shots — a cadence cap.

Gating is cheap: a few `bool` globals checked in `PowerSpeedHook::thunk` and the `BowDrawn` handler.

**Decision (how):** ship config as an **INI read by the DLL** (SimpleIni ships with CommonLib) +
the toggle **hotkey** wired into the existing `AttackInputSink`. Zero new user dependencies, no
`.esp`, stays in the pure-C++ tier. Settings are global (not per-save) — correct for an on/off mod.

**Considered and rejected for v1:**

- _Classic SkyUI MCM (Papyrus)_ — needs an esp + `.pex` extending `SKI_ConfigBase` + SkyUI + a
  C++↔Papyrus bridge. Drags the Papyrus tier back into a pure-C++ plugin. Worst fit.
- _MCM Helper_ — no Papyrus authoring (generic script), familiar Esc→Mod Configuration UX, but
  pulls in SkyUI + MCM Helper as required mods and still needs a tiny ESL. Revisit only if release
  comments ask for a real in-game menu.
- _SKSE Menu Framework (ImGui)_ — pure C++, no esp/SkyUI/Papyrus, menu rendered from the DLL;
  architecturally the cleanest fit, but niche UX and small install base. Skipped for familiarity.

## 2026-06-09 — headless driver (testing harness)

New top-level `headless/` subsystem: run Skyrim invisibly in headless `gamescope`, screenshot it
(SIGUSR2→AVIF), inject isolated input via **libei**. Full rationale + dead-ends + status live in
`headless/docs/{design,findings,status}.md` — this is just the index pointer.

Open work (detail in `headless/docs/status.md`):

- **libei pointer doesn't land in-game** (keyboard does). Chase: gamescope's unbounded abs-region
  scaling; confirm pointer/button caps on the bound device; start_emulating/frame timing; whether
  Skyrim needs the pointer "entered" before it reacts.
- **End-to-end keyboard run**: nav → Load → save → in-game → `M` map → dismiss the confirmation box
  (likely solves the `OneClickMap` chore without a mouse).
- **SKSE ground-truth tie-in** (endgame): in-process plugin reports real state (`UI::IsMenuOpen`,
  player pos, menu stack) and activates menus via engine calls — gamescope = eyes, SKSE = deterministic
  hands. Removes pixel-reading and the OS-input problem entirely.

## 2026-06-10 — DBVODialogueTweaks v2 / v3 (deferred phases)

The mod (renamed from `DBVOResponseGap`) ships in phases. v1 (manual player-line skip) shipped; **v2
is now building** (configurable response gap — design in `docs/plans/dbvo-v2-configurable-gap-design.md`,
self-first scope). Deferred:

- **v2 → public Nexus release (post-v2 follow-up).** v2 builds self-first; releasing it publicly is a
  clean separate pass once the mechanism is proven in-game. **Unlocked:** DBVO's page grants
  modify-and-release with credit, and DBVO is a frozen target (~3 yr, won't bitrot) — see mod README
  "Permissions". Release pass = ship the built modified swf + ESL + MCM, credit the DBVO author, write a
  Nexus page, and test beyond the DBVO+Karat setup (a few more voice packs). No architectural change.
- **v3 — cut the player voice on skip.** SKSE C++ plugin (sibling to `plugins/`): console
  `Player.SpeakSound` gives no handle, so hook/track the player's voice instance and stop it when v1's
  skip fires — removes the audio-tail overlap v1 accepts. Same plugin could also do the README's
  exact-`.fuz`/`.xwm`-duration NPC-reply scheduling (eliminates the wpm guess entirely, supersedes v2's
  heuristic).
- **v1 fallback to fold in:** if E/activate can't be routed from the swf during `TOPIC_CLICKED`, v1
  ships left-click-only and the keyboard skip moves to a v3 SKSE input hook.
