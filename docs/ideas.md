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

> **Superseded 2026-06-14 by `docs/plans/autofirebow-mcm-design.md`.** The "how" below chose an
> **INI read by the DLL** to stay zero-dependency; the user has since chosen a real in-game **SkyUI
> MCM** (no MCM Helper) instead, accepting SkyUI as a hard dependency. The settings list below still
> stands (master on/off, toggle hotkey, damage-bonus slider, min-shot-delay cadence cap) — only the
> delivery mechanism changed. The INI route and the other menu options are recorded as "Alternatives
> considered" in the design doc.

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

New `headless/` subsystem: run Skyrim invisibly in headless `gamescope`, screenshot it
(SIGUSR2→AVIF), inject isolated input via **libei**.

> **Superseded 2026-06-12:** `headless/` was merged into `skytest/` — one tool, where `test` runs a
> drivable gamescope session (visible by default, `--headless` for no window). The dead-ends doc moved
> to `skytest/docs/headless-findings.md`; the design/status content folded into `skytest/README.md`.
> The libei **pointer** dead-end is resolved (relative motion, measured 1:1 — findings #9), and the
> end-to-end keyboard run works (`skytest ready`/`drive`).

Still open:

- **Isolate the Saves folder per test (shared-folder autoload blocker).** The Saves dir lives in the
  prefix and is shared across profiles, so a vanilla+1 `test` game's "Continue" auto-checks the newest
  save (your _main modded_ save) and pops a "missing content" modal that blocks po3 StartOnSave from
  autoloading `SkytestBase` (which is itself clean — vanilla + Creation Club only). Fix direction:
  point Saves at an isolated dir for the test (only `SkytestBase` visible), or dismiss the modal once
  precise menu `drive` works. **Note (2026-06-14):** `skytest playtest` (drivable _full_ profile)
  sidesteps this for MCM / load-order-dependent tests — the full load order matches the real saves, so
  `CONTINUE` loads with no modal. The isolation fix is still wanted for vanilla+1 `test` autoload.
- **~~`drive` keyboard didn't move the menu~~ — RESOLVED 2026-06-14 (keyboard).** In-world keyboard
  driving is confirmed end-to-end via `playtest` (main-menu `CONTINUE` → confirm → save load →
  in-world → journal, every `drive tap` registering); #13's failure was the no-content _modal_
  swallowing keys, not keyboard in general. **Still open:** precise in-menu **mouse** clicks — the
  #9b cursor desync still misses (couldn't click the Journal's SYSTEM tab to reach Mod Configuration),
  so visual MCM screenshots need the cursor-sync fix. Also still pending: `shot`/`drive` under
  `--backend wayland`. Detail: `skytest/docs/headless-findings.md` #14.
- **SKSE ground-truth tie-in** (endgame): in-process plugin reports real state (`UI::IsMenuOpen`,
  player pos, menu stack) and activates menus via engine calls — gamescope = eyes, SKSE = deterministic
  hands. Removes pixel-reading and the OS-input problem entirely.

## 2026-06-10 — DBVODialogueTweaks v2 / v3 (deferred phases)

The mod (renamed from `DBVOResponseGap`) ships in phases. v1 (manual player-line skip), **v2**
(configurable response gap — `docs/plans/dbvo-v2-configurable-gap-design.md`), and **v3** (player-voice
volume slider — `docs/plans/dbvo-v3-player-voice-volume-design.md`, the first SKSE-tier feature) all
shipped, verified in-game. Deferred:

- **v2 → public Nexus release (post-v2 follow-up).** v2 builds self-first; releasing it publicly is a
  clean separate pass once the mechanism is proven in-game. **Unlocked:** DBVO's page grants
  modify-and-release with credit, and DBVO is a frozen target (~3 yr, won't bitrot) — see mod README
  "Permissions". Release pass = ship the built modified swf + ESL + MCM, credit the DBVO author, write a
  Nexus page, and test beyond the DBVO+Karat setup (a few more voice packs). No architectural change.
- **v3+ → v4 — cut the voice on skip: SHIPPED (2026-06-14, verified in-game).** Both halves done:
  the **player line** is cut on skip (the v3 `Actor::SpeakSoundFunction` hook now retains the line's
  `BSSoundHandle` → `FadeOutAndRelease` on skip), and the **NPC reply** is cut on new-topic interrupt —
  including multi-segment replies — via the speaker's `ExtraSayToTopicInfo.sound` +
  `Actor::PauseCurrentDialogue`. Full architecture **and the dead-ends** (`PauseCurrentDialogue` only
  _pauses_; `HighProcessData::soundHandles` aren't the topic voice; the NPC reply isn't a DBVO
  SpeakSound) live in `docs/plans/dbvo-v4-voice-cut-on-skip-design.md`. Still deferred on this tier:
  - **NPC neutral expression on cut.** When the NPC reply is cut, the actor's face freezes in its last
    speaking frame for ~1–2 s until the engine resets it — looks a bit stupid. Reset the speaker's
    expression to neutral in `CutNpcReply()` when the cut fires (the speaker actor is already resolved
    there; needs the right face/facegen-anim reset call — research the expression-reset path). Small
    but noticeable.
  - **Exact `.fuz`-duration NPC-reply scheduling.** Unrelated to cutting: the same SpeakSound hook that
    retains the player handle can also read the line's `.fuz`/`.xwm` duration and schedule the NPC
    reply to land exactly when it ends — eliminates v2's wpm guess, supersedes the heuristic.
- **v1 fallback to fold in:** if E/activate can't be routed from the swf during `TOPIC_CLICKED`, v1
  ships left-click-only and the keyboard skip moves to a v3 SKSE input hook.

## 2026-06-11 — DBVODialogueTweaks v3 volume-slider follow-ups

- **Boost-clamp: RESOLVED → slider capped at 0–100%.** In-game 150% testing confirmed
  `BSSoundHandle::SetVolume(>1.0)` does **not** amplify — 150% was indistinguishable from 100% (engine
  handle volumes are 0.0–1.0 multipliers). Attenuation works fully (50% quieter, 0% silent, NPC reply
  unchanged, survives save/reload). The slider now caps at 100%. If voice _boost_ is ever wanted it needs
  a different gain path (re-encode the Karat pack louder offline, or a custom audio output model) — not
  worth it for the "tame it" goal. See `docs/plans/dbvo-v3-player-voice-volume-design.md` → "Value
  mapping & the boost caveat".

## 2026-06-11 — SkytestProbe (runtime-commandable debug instrumentation)

State: **v1 designed** (`docs/plans/skytest-probe-design.md`), no separate plan — implementation
works straight from the design doc. Deferred beyond v1:

- **Full command bridge (socket/RPC).** v1's file protocol already makes the running game
  externally scriptable; a socket would add request/response semantics and lower latency. Only
  worth it if file polling proves limiting.
- **Curated runtime-toggleable hook probes.** Pre-compiled trampoline hooks on commonly-debugged
  engine functions (damage application, projectile spawn, …), armed via command — the safe
  subset of "dynamic tracing". Arbitrary-address hooking stays out (crash-prone).
- **Console output capture for `exec`.** v1 is fire-and-forget; capturing what the command
  printed (hook `ConsoleLog::Print`?) would make `exec` a query tool (`GetAV`, `GetStage`, …).
- **Papyrus script-variable peeking.** Visibility into running Papyrus state from the C++ side.
- **Per-mod fixture autoexec convention.** `exec` covers the mechanics; define where a
  mod-under-test's fixture script lives so `skytest test <mod>` arms it automatically
  (the deferred per-mod-fixtures item from the skytest v2 handoff).
- **Per-frame `watch` sampling** via a `Main::Update` hook, if 4 Hz poll cadence proves too
  coarse for spiky values.
- **Editor-ID / plugin-relative ref addressing** (`"MyMod.esp|0xD62"`) in addition to runtime
  FormIDs.
- **DLL hot-reload of the mod-under-test.** Would kill the remaining restart-on-fix cost;
  generally unsafe (static state, irreversible hooks) — research only if restarts become the
  bottleneck again.

## 2026-06-12 — skytest relocation follow-ups

`skytest` moved from `~/Downloads/skyrim-mods/1-skytest/` into this repo (`skytest/`) so that
mod-_making_ and mod-_managing_ are cleanly separated by directory. The move was pure (no script
changes). Follow-ups it opened up:

- **~~Merge `headless/` + `skytest/` into one launcher~~ — DESIGNED 2026-06-12**
  (`docs/plans/headless-skytest-merge-design.md`), not yet built. A _test session_ runs under
  gamescope (visible `--backend wayland` or `--headless`) — detached, drivable (`skytest shot`/
  `drive`/`ready`/`stop`), restore-on-`stop`; `play`/`normal` keep the bare direct fast path
  (blocking, restore-on-exit, not drivable). The visible test runs under gamescope so `drive`/`shot`
  reuse the exact headless machinery; `--headless` just swaps the backend string. `headless/` retires
  into `skytest/lib/gamescope.sh` + `skytest/eidriver/`. Implementation plan is the next step.
- **~~De-duplicate `SkytestProbe.dll`~~ — RESOLVED 2026-06-12.** `skytest` now reads the probe DLL +
  ini straight from its build output (`mods/SkytestProbe/build/SkytestProbe.dll` + `mods/SkytestProbe/
SkytestProbe.ini`); the committed copy under `skytest/base-skse/` and `build.sh --stage` are both
  gone, so the build output is the single canonical copy — no tracked binary duplicate.
  (`po3_StartOnSave.{dll,ini.template}` stays vendored in `base-skse/` — genuinely third-party, no
  in-repo source.)
- **CI-style headless mod check** (was item 4 of the v2 handoff). A thin layer: boot a test profile
  headless → run a console batch (coc + spawn + assert, via SkytestProbe `exec`) → read `trace.jsonl`
  → quit, for non-interactive mod smoke tests. **Unblocked once the merge (now DESIGNED, above)
  lands** — it composes the merge's detached headless launch + `skytest ready`/`exec`/`stop`
  primitives into one blocking smoke verb (e.g. `skytest smoke <mod> <batch>`). (Per-mod fixture
  wiring for this is the "Per-mod fixture autoexec convention" item under the 2026-06-11 SkytestProbe
  section.)

## 2026-06-12 — headless+skytest merge follow-ups

Deferred out of the merge design (`docs/plans/headless-skytest-merge-design.md`, "merge only" scope):

- **Input recording / playback.** The motivating use case for `shot`/`drive` being first-class
  skytest verbs. CC drives a (visible) gamescope test session to a target state — e.g. navigate
  menus, click a discovered map marker — using screenshots as the authoring aid (_see_ where the
  marker is, decide the click). The input step sequence is **recorded** to a file, then **replayed**
  deterministically to re-reach that state for testing/probing (replay → probe the result, no human
  in the loop). Its own design space, deferred deliberately: the step-file format, how playback
  re-times/re-syncs steps, how it tolerates the game booting slower one run to the next, and the
  screenshot-assisted authoring loop. The merge shapes `drive` to stay replay-friendly so this layer
  bolts on without reworking the input path. Pairs with the SKSE ground-truth tie-in (probe `status`
  as the per-step sync gate instead of fixed sleeps).
