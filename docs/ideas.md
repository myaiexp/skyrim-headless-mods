# Ideas — skyrim-headless-mods

Future work, deferred features, and things worth revisiting. Each entry is WHAT, not HOW.

## 2026-06-21 — GhostAllies autonomous pass-through test (attempt: harness shipped, verdict deferred)

Tried to build a hands-off, CC-driven headless regression for GhostAllies' **projectile
pass-through** (the shipped v2 behavior: the player's aimed shots phase through allies and continue
to the enemy behind). **Shipped and verified — reusable harness:**

- **SkytestProbe staging commands** (direct engine calls, the `give-spell`/`set-av` pattern):
  `placeatme` (spawn an actor base `d` units along the player's forward vector, auto-**freeze** its
  AI via `EnableAI(false)` so the geometry holds, register a **named alias** `as ally`/`enemy`),
  `make-teammate` (set the `kPlayerTeammate` bit → enrolls a spawn as a GhostAllies "ghost ally"
  with no real follow relationship), and `cast` (`MagicCaster::CastSpellImmediate` — fires a **real**
  player projectile, no input). Plus a `spawned`-alias resolver in the probe.
- **`replay` `cmd <json>` step** — sends a direct-call probe command and blocks on its ack: the
  `.steps` **staging path** (replaces the faulting `exec`). `mods/GhostAllies/passthrough.steps`
  stages the scene with it.
- **Per-step auto-shot filmstrip** — `replay` snaps `<probe-io>/replay-shots/NN-verb.png` after every
  step (default on, `--no-shots` to disable), so a run is reviewed as one batch of frames instead of
  the slow live take-shot loop. **Confirmed working headless** (real composited frames; the cast
  frame even caught the explosion). This was the genuinely valuable outcome of the session.

**Why the autonomous _verdict_ is deferred (the rear-witness HP sensor is a dead end):** the
mechanism fires perfectly (control: a non-teammate ally blocks the shot, takes damage; test: the ally
enrolls → `systemGroup` → `0xFEED`, projectile stamped, `AddTarget` refuses, ally unharmed). But
proving the projectile _continued to a witness behind the ally_ via the witness's HP is blocked by
engine reality: probe-driven `CastSpellImmediate` has no crosshair, so it aims at the target's origin
(feet/floor) — precise bolts hit the floor, only AoE spells land, and AoE damages the front ally too
(and won't reach a far witness; projectiles also drop). Full detail: `skytest/docs/headless-findings.md`
#22–24.

- **Deferred robust sensor — a projectile-flight probe.** Hook `Projectile::UpdateImpl` (the seam
  GhostAllies itself uses) in SkytestProbe and record the player projectile's **max distance reached
  before it dies** (control: dies at the ally ~250u; pass-through: travels past). Immune to AoE / aim
  / drop / spell choice, and it's the same instrument that would verify the parked **runes / wall /
  cone** pass-through work — the natural next GhostAllies test once this lands. This is the "watch the
  projectile, not a witness" pivot.
- Smaller follow-ups surfaced: a probe `set-angle`/`face` (set player pitch/yaw — needed for level
  free-casts and to point the camera at spawned actors for the filmstrip); `placeatme` could grow a
  `move`/reposition sibling (no reposition command exists, so re-staging means fresh spawns + corpse
  accumulation along the firing axis across iterations).

## 2026-06-21 — skytest probe-driving ergonomics (from the DBVO mouth-snap session)

> **SHIPPED 2026-06-21 — every item below landed, verified in-engine.** Two commits: the shell
> wrappers (`skytest io`/`cmd`/`trace`/`wait-probe`/`restart` + a pollable `status --json` `world`
> block + `drive seq` gap default 120→300 ms, overridable via `seq --gap MS`/`SKYTEST_SEQ_GAP_MS`)
> and the SkytestProbe instrumentation (`paused`/`gt` guard on every facegen line + the per-frame
> read-only `facegen-observe` command). Verified live in a headless GhostAllies session: `paused`
> flips and `gt` drops to 0 when the console pauses the sim; `facegen-observe` emits per-render-frame
> `face-frame` lines (3 mouth channels, ~60 Hz) vs the 4 Hz `facegen-watch`. Manuals updated
> (`skytest/README.md`, `mods/SkytestProbe/README.md`). Original capture below.

A long co-driven debug session (Mase drives the character, CC drives the probe — the standing pattern
when a test needs an NPC talking, which CC can't do reliably) exposed that skytest is great at
launch/drive but **reading/writing the probe IO is raw**: every command was a hand-built
`echo >> "<long path>/commands.jsonl"`, every read a bespoke `jq` over `trace.jsonl`, and the IO dir had
to be hunted with `find`. Thin CLI wrappers would remove most of that friction:

- **`skytest cmd '<json>'`** — append a command to `commands.jsonl` AND block for its ack, printing the
  result. (Today: manual echo to a long path + manual ack-grep, for every single command.)
- **`skytest trace [--tail N] [--src X] [--since T] [--jq EXPR]`** — tail/filter `trace.jsonl` without
  resolving the path or rebuilding jq pipelines each time.
- **`skytest io`** — print the resolved probe IO dir (had to `find` it this session).
- **`skytest wait-probe`** (or fix `ready` for the `full` profile) — block until the probe is live
  (`trace.jsonl` non-empty / `src:"session"` line). Hand-rolled a 22×5 s poll loop on *every* restart.
- **`skytest restart [agent]`** — stop + relaunch (same mode) in one verb; a native-DLL change forces a
  full stop→play→wait→re-arm cycle, done ~4× in one session.
- **`skytest status --json` / a reliable `inWorld` signal** — the human-readable status can't be polled
  programmatically (an `inWorld` grep silently matched nothing this session).
- Reinforce the **`drive seq` inter-key-gap TODO** — 120 ms is too tight for menu→Continue (the
  double-`e` boot landed wrong; Mase took over the load-in).

**SkytestProbe instrumentation (belongs in the probe, not the launcher):**

- **Paused-vs-running guard** — tag each `facegen-watch` sample with a frame counter / "advancing" flag.
  A paused game emits identical samples that *look* like live data; reading one as live is what sent the
  mouth-snap investigation down a multi-session `transitionTarget` dead end (see the v4 handoff).
- **Per-frame facegen observe mode** — the apply hook logs the target keyframe each frame *without*
  modifying it, to characterize sub-100 ms transitions (the 4 Hz watch was too coarse for the 1-frame
  snap and the residual tongue flick).

## 2026-06-17 — Ghidra RE tier + GhostAllies stream pass-through reopened

Set up a **headless, reproducible Ghidra tier** (`docs/ghidra.md`, `tools/ghidra/ghidra.sh`) to
disassemble `SkyrimSE.exe` for hook points the Address-Library tier can't reach. Two findings:

- **SkyrimSE.exe is SteamStub-DRM encrypted** (`.bind` section, Variant 3.1 x64): the on-disk
  `.text` is garbage until unpacked with Steamless (`ghidra.sh unpack`). Any prior *inference* about
  these functions' internals was made without ever seeing real code — suspect accordingly.
- **The "continuous streams can't pass through" parked reason is DISPROVEN.** Decompiling the
  decrypted `FlameProjectile::UpdateImpl` (vtable slot `0xAB`, VA `0x1407d5760`) shows it is
  **positioning + orientation + homing-target aim + lifetime** — matrix/transform math end to end
  (helpers `0x1401d0ec0` = 3×3 matmul, `0x140d19e40` = build-rotation-from-direction, `0x1407ecbf0`
  /`0x140851dc0` = compute the flame node transform / homing aim-point). **There is no
  layer-filtered world cast and no collision "stop point" anywhere in it** — contrary to
  `ghost-allies-design.md` §2b, which assumed (pre-Ghidra) the stop point was computed there. So the
  systemGroup family didn't fail because "the cast uses layer not group" — it failed because
  `UpdateImpl` does no collision at all. **Stream pass-through is therefore not proven infeasible.**
- **Next step (tractable now the pipeline works):** find where the flame's effective target is
  actually gated — almost certainly the **magic aim / target-acquisition** path (the flame homes
  toward an acquired ref; if the ally is acquired, the enemy behind is never targeted), not the
  projectile's own update. Trace that pick → decide buildable-vs-dead with a real seam, not a guess.
  Verify any candidate hook in-engine with `skytest test GhostAllies --headless`.

## 2026-06-16 — skytest replay follow-ups (feature built; staging model decided)

Replay machinery shipped & verified live; staging model settled in `docs/plans/
skytest-replay-handoff.md`. Deferred:

- **Probe commands for staging that must be _observed_.** Console typing now covers "just change
  the world" (`drive type` / a `.steps` `type` step — ruling in `AGENTS.md`, mechanics in
  `skytest/README.md`), so the remaining case for a new probe command is a test that needs the
  result **acked or read back** the way `placeatme` echoes its FormID. Add those per need; still
  don't build the console surface speculatively, and still don't try to "fix" `exec`.
- **Exterior `SkytestBase` save** — a nice-to-have, not a blocker (this entry used to claim the
  QASmoke base save made a map test impossible; `mods/OneClickTravel/oneclick.steps` disproves
  that by typing `coc riverwood` first, and the `MapMenu` gate is reliable). An exterior save
  would only save ~30 s of per-run staging while changing shared test infra (every test autoloads
  `SkytestBase`), so it stays a deliberate call.
- **Conditional / tolerant `.steps` steps.** `mods/OneClickTravel/oneclick.steps` clicks the AE
  Survival-Mode "No" at fixed coordinates because no step can express "click that box *if* it is
  up". It is harmless there (an in-world click just swings a fist), but a script that must branch
  on observed state has no way to say so. Shapes considered: an `if:<gate>` prefix on a step, or a
  `try` verb whose failure never aborts the run.
- **`replay --vanilla` — a one-command A/B.** The control half of an A/B is currently driven by
  hand against `skytest test --vanilla`. Running the *same* `.steps` against the control and
  reporting which gate failed would make the whole comparison one command — and for an assertion
  script the pass condition inverts: the control is *expected* to fail the final gate (that
  failure IS the vanilla behavior). Needs a way to mark a step "expected to fail under --vanilla".
- **~~Truncate `commands.jsonl` at session start.~~ DONE (2026-06-16).** Folded into a bigger fix:
  `gs_reset_io` now clears **both** `commands.jsonl` and `trace.jsonl` in `_boot_test_session`
  before `gs_launch`. The `trace.jsonl` half fixed a real bug — `gs_wait_ready`/replay gates were
  matching a **prior** session's last `inWorld:true` status line (the probe only truncates trace on
  load, after the readiness poll already ran), faking instant readiness and driving replay input
  before the EIS server was up. See the handoff's "stale-IO readiness" finding.
- **~~Validate key names in `tap`/`key` steps.~~ DONE (2026-06-16).** `gs_drive`'s `tap`/`seq`/`key`
  now capture `gs_keycode`'s rc (`kc=$(gs_keycode …) || return 2`) instead of inlining the
  substitution; an unknown key was producing an empty arg → `eidriver` tapped keycode 0 (a silent
  no-op that still exited 0), so a typo'd key reported `ok` and a downstream gate was what failed.
  `seq` validates every key before driving any (no half-applied sequence). **`hold` (2026-06-16
  audit):** it did the OPPOSITE of capturing the rc — it pre-resolved the name to a keycode and
  passed that to `gs_drive key`, which re-resolves name→code, so it double-resolved (`unknown key:
  18`), the press silently no-op'd, and the unchecked press rc hid it. Now `hold` passes the key
  *name* through and checks the press rc (handoff RESOLVED 4). **Parse-time presence checks: DONE
  (2026-06-16) —** a `tap`/`key`/`hold`/`wait` missing its required argument is now a `--dry-run`
  lint error (`line N`). **Semantic pre-flight lint: DONE (2026-06-21).** The deferred "validate
  key *names*" item shipped — but **without** coupling the pure parser to `gs_keycode` (the reason
  it was parked). Instead a separate `replay_lint` layer (`lib/replay.sh`) reads the normalized
  plan and checks each step against the real vocabulary; `replay --dry-run` runs it after the pure
  parse. Scope grew past just keys to everything the parser structurally can't see: key names
  (`gs_keycode`), `until:` gates (`resolve_gate`), durations (`_replay_dur_secs`, extracted so the
  `*s` arm now validates numerically too), and `cmd` JSON (`jq`). Reports `step N (verb): <problem>`
  exhaustively (not first-fail); verified end-to-end + 20 new `replay.test.sh` checks. The two
  in-repo `.steps` lint clean (no false positives). **Follow-up:** wire the same lint into the
  *real* (non-dry-run) replay path before `_boot_test_session`, so a bad key fails fast instead of
  after a boot — kept dry-run-scoped here to match this item's original framing.
- **`charged` / `actorcount` gates** (from the design) — not built; add per the first script that
  needs them, each as one `resolve_gate` row + one direct-call probe handler (the `is-menu-open`
  commit is the template).

## 2026-06-08 — GhostAllies (projectiles phase through followers)

State: **v1 + v2 shipped & verified in-game (v0.9.0, 2026-06-14)** (`docs/plans/
ghost-allies-design.md`). v2 delivered: arrows + aimed spells (Firebolt) pass through the whole
party; the player's hostile magic deals no friendly damage to teammates. Still deferred / parked:

- **Runes + wall spells.** `GrenadeProjectile` (runes / lobbed) and `BarrierProjectile` (wall
  spells) are explicitly out of v2 scope — different (arc / placed) collision feel. Add later by
  extending the same unified `UpdateImpl` stamp to those subclass vtables.
- **~~Player summons~~ DONE (v0.10.0, 2026-06-17, verified in-engine).** `IsGhostAlly()` now ORs
  `IsPlayerTeammate()` with "commanded by the player" (`GetCommandingActor() == player`), so conjured
  atronachs/familiars + reanimated thralls join the same ghost group (one predicate, both the stamp
  and the AddTarget refusal). Verified headless: a conjured Familiar logged `enrolled ghost-ally …
  orig group 203`, missiles stamped, AddTarget refused its damage. Scoped to OUR summons (an enemy
  conjurer's atronach has a different commanding actor, stays hittable). **Still deferred:** any
  *non-hostile* actor (a different, broader flag) — fold into the same membership set when wanted.
- **MCM / INI configuration.** Toggles for: arrows on/off, spells on/off, per-projectile-type, and
  which actor categories phase (teammates / summons / all non-hostiles). v2's membership set is
  already category-shaped, so this is mostly surfacing it as config (mirror the AutoFireBow INI
  decision below).
- **Continuous spells (Flames/Sparks): damage refused ✅, true pass-through PARKED.**
  > **Premise updated 2026-06-17 — see the Ghidra entry at the top of this file.** The "baked in a
  > layer-filtered cast inside `UpdateImpl`" reasoning below was **disproven**: the decompiled
  > `UpdateImpl` does no collision at all. Pass-through is still parked (the real gating point —
  > likely magic aim / target-acquisition — is untraced), but no longer "structurally infeasible."
  >
  Damage is handled — `MagicTarget::AddTarget` refusal drops the player's hostile effects on
  teammates (verified; `AddImpact` 0xBD was tried first and disproven). But making the *stream pass
  through* the ally to hit the enemy behind was thought **structurally infeasible** (researched 2026-06-14, 4
  failed attempts incl. ghosting all the ally's rigid bodies, reverted in v0.9.0):
  `FlameProjectile`/`BeamProjectile` expose **no collision-point vfunc (slot 0xBE)** — their stop
  point is baked in non-virtual `UpdateImpl` via a **layer**-filtered cast that ignores systemGroup,
  and no mod has ever done it. Only experimental levers remain (flip the ally's collision *layer*
  during the cast — high blast radius; or Ghidra-disassemble `UpdateImpl` to trampoline the internal
  cast). See `docs/plans/ghost-allies-design.md` §2b. Don't retry the systemGroup family for streams.
- **ConeProjectile DOES expose 0xBE — cone pass-through could be feasible later.** Unlike flame/beam,
  `ConeProjectile` overrides the `0xBE` collision-point handler
  (`OnXxxCollision(Projectile*, hkpAllCdPointCollector*)`), so a cone spell could get *true*
  pass-through by hooking 0xBE and erasing teammate contacts (the community-standard discrete
  pass-through hook). Untested (no cone spell on hand). That same 0xBE contact-erase hook is also the
  cleaner/proper mechanism for arrow/missile pass-through than the current systemGroup stamp
  (precedents: D7ry/valhallaCombat, the local co-op mod) — worth knowing if the stamp ever regresses.

**Dropped, not deferred:** two-way phasing (follower-fired projectiles through the player).
Followers rarely friendly-fire the player, so it solves a non-problem. Only revisit if real
gameplay shows follower projectiles blocking/hitting the player often enough to matter.

## 2026-06-09 — OneClickTravel (skip world-map popups)

State: **v1 shipped + verified in-game 2026-06-14** (`docs/plans/oneclick-travel-design.md`).
Discovered-marker click → instant fast-travel, no confirm box; all other boxes pass through
vanilla. Built on a MinHook entry detour of `MessageBoxData::QueueMessage` (replaced the stopgap
`write_branch<5>` build that crashed on non-travel boxes). v1 is unconditional, no config. Deferred:

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

> **SHIPPED — both halves landed (plugin v2.1.0; confirmed in source 2026-06-21).** The SkyUI MCM
> is built and ships: `AutoFireBowMCM extends SKI_ConfigBase` + an EspGen quest + a one-way
> Papyrus→DLL native bridge (`SetEnabled`/`SetDamageBonus`/`SetMinShotDelay`), exposing master
> on/off, a toggle hotkey, a damage-bonus slider, and the min-shot-delay cadence cap. And the
> **real-charge spike landed** (`docs/plans/autofirebow-real-charge-design.md`): there is no
> `PowerSpeedHook`/clamp anymore — auto arrows loose at genuine full draw via synthetic
> input-release, so the "auto-fire vs full-power clamp" split below **collapsed to a single
> auto-fire on/off**, exactly as the "Contingent" note predicted. The `GetPowerSpeedMult` hook
> survives only as the auto-only DPS bump (the damage slider). The notes below are historical (the
> INI-vs-MCM delivery decision); see `mods/AutoFireBow/README.md` for the as-built mod. **Still
> deferred:** SE/VR in-engine verification, and the open question of whether the +10% DPS bump is
> warranted on a clean save.

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
  precise menu `drive` works. The isolation fix is still wanted for vanilla+1 `test` autoload.
- **~~`drive` keyboard didn't move the menu~~ — RESOLVED 2026-06-14 (keyboard).** In-world keyboard
  driving was confirmed end-to-end in the former full-profile gamescope wrapper (main-menu
  `CONTINUE` → confirm → save load → in-world → journal, every `drive tap` registering); #13's failure
  was the no-content _modal_ swallowing keys, not keyboard in general. **Still open:** precise in-menu
  **mouse** clicks — the #9b cursor desync still misses (couldn't click the Journal's SYSTEM tab to
  reach Mod Configuration), so visual MCM screenshots need the cursor-sync fix. Also still pending:
  `shot`/`drive` under `--backend wayland`. Detail: `skytest/docs/headless-findings.md` #14.
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
  SpeakSound; and the facegen freeze — the NPC mouth/face freezes open on cut and is reset to neutral
  in `CutNpcReply()` via a lock-guarded snap `Reset`) live in
  `docs/plans/dbvo-v4-voice-cut-on-skip-design.md`. Still deferred on this tier:
  - **Exact NPC-reply scheduling — v5 SHIPPED & verified in-game 2026-06-14**
    (design `docs/plans/dbvo-v5-reply-on-line-end-design.md`, plan `…-plan.md`). Realized as
    **end-detection** rather than duration-prediction: a detached poll thread watches the retained
    `g_playerLine` handle and fires the reply (via `GFxMovieView::InvokeNoReturn` into the swf's new
    `dbvoOnPlayerLineEnded`) the moment the line stops, after a small configurable gap. Drops v2's
    ms-per-word slider, repurposes the pad slider as the trailing gap, keeps a generous internal swf
    backstop for the missing-audio case. **Two in-game gotchas fixed during bring-up** (both in the
    design's "Dead-ends"): (1) the watcher was first a self-re-arming main-thread `AddTask` loop and
    **froze the game for the whole line** — SKSE drains its task queue to empty, so a self-re-queuing
    task spins the frame; moved the poll to a detached thread (sleep), marshalling only the one-shot
    fire. (2) MCM made **tab-less** to mirror DBVO's own menu — leave `Pages` unset (Papyrus forbids a
    0-length array; `GetVersion` 4 clears persisted tabs via `Pages = None`), render both sliders on the
    landing page. Duration-prediction (read the `.fuz`/`.xwm` length up front) is kept in the design as
    a fallback only if detection proves flaky.
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

  > **DESIGNED 2026-06-16 — the step-file half** (`docs/plans/skytest-replay-design.md`). The
  > CC-authored step-script format + `skytest replay <mod> <script>` interpreter is designed:
  > line-based `.steps` files (`exec`/`tap`/`key`/`hold`/`wait`/`shot`), probe-gated `until:` sync
  > (reuses `IsInWorld`; `menu`/`charged`/`actorcount` gates added to SkytestProbe **permanently**
  > as scripts first need them — no speculative vocabulary), replay reaches a target state and leaves
  > the session detached for live probing (no baked-in verdict). **Still deferred:** _raw
  > human-input capture_ — a person physically plays, raw libei events recorded + wall-clock
  > re-synced; this is the camera/pointer-motion path, its own harder feature (the designed half is
  > discrete input only, CC-driven). A _tee-recorder_ (auto-log my `drive`/`exec` into a draft) was
  > considered and **dropped** — rationale in the design doc's "author, not capture" section.

## 2026-06-14 — SkytestProbe MCM reveal/drive

State: **reveal v1 designed** (`docs/plans/skytest-probe-mcm-reveal-design.md`) — read-only `mcm-list`
(enumerate registered MCMs + pages) + `mcm-get <ConfigScript> <prop…>` (a known mod's live property
values), both headless via the Papyrus VM, targeting the full profile. Deferred:

- **`mcm-scrape` — generic on-screen option labels + values.** Reads whatever MCM page is currently
  **open**, via a Scaleform/GFx scrape of the "Journal Menu" (`_root.ConfigPanelFader.configPanel`).
  Menu-open-only by nature (SkyUI builds option values only while a page renders — there is no central
  value table), and needs a one-time runtime `VisitMembers` dump to discover SkyUI's flash option-array
  path (the `.as` source isn't vendored). It's the generic-values complement to v1's known-mod `mcm-get`.
- **Drive MCMs from C++ (the follow-up phase).** Open a config / select a page / set an option without
  pixel input — via `GFxMovie::Invoke` on SkyUI's flash methods or `SendModEvent` of the `SKICP_*`
  events (`SKICP_optionSelected`, `SKICP_pageSelected`, …). Built on the same open-menu GFx plumbing as
  `mcm-scrape`. This is what fully replaces the unreliable cursor-driving of MCMs (the AutoFireBow MCM
  test wall — `skytest/docs/headless-findings.md` #14).
## 2026-06-14 — skytest test mis-stages split-output mods (e.g. DBVODialogueTweaks)

`skytest test <mod>` expects the mod arg to be a single artifact (`.dll`/`.esp`) **or a Data-shaped
folder** — `build_test_profile` (`skytest:221-229`) just mirrors `find "$mod" -type f` verbatim into
the test profile, assuming every file already sits at its `Data/`-relative path. DBVODialogueTweaks
fits neither: its outputs are split across `build/` (`Interface/`, `Scripts/`, the `.esp`) and
`plugin/build/` (the DLL), and the repo dir itself isn't Data-shaped. So
`skytest test mods/DBVODialogueTweaks` symlinks repo files (`src/`, `docs/`, `build.sh`) into the
profile at junk paths, places the esp at `Data/build/DBVODialogueTweaks.esp` (game can't find it), and
**never stages the DLL** (it lives outside any path skytest mirrors to `SKSE/Plugins/`). Observed
2026-06-14: in a `skytest test mods/DBVODialogueTweaks --headless` run, SkytestProbe/CrashLogger logged
fresh but `DBVODialogueTweaks.dll` never loaded (its SKSE log stayed stale). Even pointing at `build/`
alone would still miss the DLL (`plugin/build/`).

Directions: (a) have `build.sh` emit one **Data-shaped** staging dir (e.g. `build/Data/` with
`SKSE/Plugins/<dll>` + `Interface/` + `Scripts/` + the `.esp`) and point `skytest test` at that;
(b) teach skytest to assemble a mod's Data image from build.sh's known outputs; or (c) pass the DLL via
`--with` plus a Data-shaped dir for the rest. Lower priority for DBVODialogueTweaks itself — it needs
DBVO + a voice pack present, so it's a **full-profile** test regardless — but it bites any future
swf+DLL+esp mod that *is* standalone vanilla+1-testable.

## 2026-06-14 — AutoCastSpell (deferred phases)

State: **v1 SHIPPED & verified in-engine** (`mods/AutoCastSpell/`, always-on standalone SKSE DLL;
spell analog of AutoFireBow). Hold a fire-and-forget spell → auto-fires at full charge, loops until
released, per hand / dual-cast. The loop is driven by polling `RE::MagicCaster::state` (no "spell
charged" anim event exists): kReady→synthetic release, re-arm on the next charge + a release-nudge
fallback. Verified: right/left/both loops, concentration excluded, magicka-out clean stall.
Deferred out of v1:

- **Replace the load-bearing per-cycle logging with explicit pacing.** The loop currently *depends*
  on the per-cycle `SKSE::log::info` calls: their `flush_on(info)` disk-flush spaces the synthetic
  injects from the state re-reads, which the timing-sensitive recharge needs. Stripping the logging
  regressed the loop (7 casts → 2). It works, but it's fragile (a read-only log dir or an spdlog flush
  change would alter timing) and it spams the log every cast. Replace the flush-as-pacing with an
  explicit, deliberate delay/spacing in `CheckCasters` (or spread the inject vs. re-read across poll
  ticks), then drop the verbose logging — and re-validate on real hardware with the *same* hold test.
- **SkyUI MCM (the config follow-up).** Mirror AutoFireBow's MCM: master on/off, a toggle hotkey,
  and a **min-cast-delay** cadence cap. The cadence cap matters more here than for the bow — magicka
  drains fast, so the first MCM knob is the per-cast delay to avoid dumping the whole pool instantly.
  Bolts on cleanly once the v1 loop is proven (AutoFireBow's exact evolution: always-on → MCM).
- **Magicka-out "still-held" watchdog.** v1 stalls the loop mid-stream when a charge can't be
  afforded (the engine never emits the "charged" event, so no release/re-press) — the player releases
  and re-presses to resume once magicka regens. A watchdog that re-attempts the charge while the
  control is still held would auto-resume on regen without the manual re-press. Adds off-thread-safe
  timing (enqueue the retry press on the game thread); deferred as a polish nicety, not v1.
- **Public Nexus release.** Like the other mods, a clean separate pass once the loop is proven
  in-game — write a page, test beyond the Firebolt close-out (a few more FF spell types: runes,
  summons, dual-cast). No architectural change.

## 2026-06-14 — Per-mod READMEs for the remaining code mods

State: **DONE (2026-06-21) — every code mod now has a `README.md`.** GhostAllies was the acute gap
(the root `README.md` headline link called it "the flagship working mod" but `mods/GhostAllies/`
had no README, so on GitHub the link opened a bare source dir). DBVO, RapidBowHold, and
OneClickTravel already had mod READMEs; the last three landed this pass:

- **AutoFireBow** — **DONE** (`mods/AutoFireBow/README.md`). Writing it surfaced that the root row
  + this file's AutoFireBow-config entry were stale (the SkyUI MCM + real-charge spike had both
  shipped as v2.1.0); fixed both in the same pass.
- **AutoCastSpell** — **DONE** (`mods/AutoCastSpell/README.md`, v1.0.7). Documents the log-flush
  pacing fragility + magicka-out stall honestly (both still deferred per this file).
- **SkytestProbe** — **DONE** (`mods/SkytestProbe/README.md`). Doubles as the full command
  reference (the command set previously only lived in `docs/plans/skytest-probe-design.md`),
  including the `exec`/CompileAndRun caveat and the facegen probes.
- **OneClickTravel** — **DONE** (`mods/OneClickTravel/README.md`, 2026-06-14). Release artifacts also
  ready: `package.sh` → `dist/` zip + Nexus page copy (`docs/oneclicktravel-nexus-page.md`); awaiting
  only a header image + the manual Nexus upload.

Each mirrors the DBVO/GhostAllies README shape (what it does / requirements / compatibility /
install / how it works / build) and the detailed prose moved out of the root README table into the
mod README, leaving the root row a brief pointer — exactly as GhostAllies does. For a showcase repo,
consistent per-mod landing pages remove the last "looks half-done" signal once a browser clicks into
each mod dir.

## 2026-08-31 — DBVO 2 supersedes DBVO Dialogue Tweaks

State: **analysed; repo docs repointed at DBVO 1.x (2026-09-02); in-engine test blocked on the
archive.** Full write-up + the 2026-09-02 addendum in
`docs/plans/dbvo-v2-compatibility-analysis.md` (static analysis of the shipped 2.0.1.5 / 2.0.1.6
DLLs — still no in-engine test).

DBVO 2 is a native rewrite: one `DBVO.dll`, no `.esp`, no Papyrus, **no `dialoguemenu.swf`**. It
absorbs the reply-on-line-end fix (base delay = real `.fuz` duration, word-count guess demoted to
fallback), the gap slider (`npc_response_delay`, default 0), the volume slider (`dialogue_volume`,
plus a reverb slider we never had), and — in 2.0.1.6, 2026-08-30 — manual skip (`SkipInputHandler`:
Activate / left-click / gamepad-A, 300 ms debounce → `FireResponse(token, skipped=true)`).

DBVO 2 is now the **main download** on page 84329 (2.0.1.6; the page is titled "Dragonborn Voice
Over 2" and DBVO 1.1.1 is demoted to OLD FILES), so the plain mod link new users follow leads
straight into the softlock.

Three follow-ups:

- **Nexus page notice — the only manual step left, and now evidence-backed.** The mod is live
  (182628, v1.0, 245 downloads) and installing it over DBVO 2 **softlocks dialogue**, confirmed
  in-engine 2026-09-02 by A/B on game 1.7.104: DBVO 2 alone replies in ~5 s, DBVO 2 + this mod was
  still stuck 26 s after the same click. Tab escapes the menu, so it costs the conversation and not
  the save, and **DBVO 2 logs its own `[Conflict]` warning** naming the patched swf — quote that at
  users rather than our reverse-engineering. The repo README, the root README row and the page copy
  `docs/dbvo-page.bbcode` all carry it; **paste `docs/dbvo-page.bbcode` onto the live Nexus page**
  (website-only, no write API). It is **BBCode, not Markdown** — Nexus renders Markdown literally,
  which is how the first paste attempt looked.
- **One link left on the DBVO 2 skip question.** The observer is built and shipped:
  SkytestProbe's **`speak-watch`** (read-only `Actor::SpeakSoundFunction` detour). With it,
  pressing Activate ~600 ms into the player's line **never truncated it** — full ~1.6 s every run,
  four runs, two prompts (table in `docs/plans/dbvo-v2-compatibility-analysis.md`). It also
  confirmed DBVO 2 plays the line at `DBVO/Danagis_KaratVoice/<Sanitized_Prompt>.fuz`.
  What is missing is proof that `SkipInputHandler` **fired** on the synthetic input, so
  "skip fires but can't cut audio" and "skip never fired" both still fit. That needs DBVO 2's
  `[FireResponse]`/`[delay]` lines, gated behind an **"Enable DBVO logging"** toggle that is off by
  default and lives **only in its ImGui menu** (SKSE Menu Framework, F1) — not a key in
  `default_options.json`. Drive the toggle on (finding #25: coordinate `drive click` works), re-run
  the four-row table, done. **Don't cite the claim to MathiewMay before that.**

  The reusable stage is already built: `~/.cache/skytest-dbvo2/` (DBVO 2 alone) and
  `~/.cache/skytest-dbvo2-plus-tweaks/` (with this mod), both Data-shaped so
  `SKYTEST_NO_AUTOLOAD=1 skytest test <dir> --headless` mirrors them straight in. Karat legacy pack
  wired (`use_legacy_voice_over`, `legacy_pack:"Danagis_KaratVoice"`, `always_use_default_options`);
  its 7092 `.fuz` sit at `sound/dbvo/danagis_karatvoice/<sanitized prompt>.fuz`, matching DBVO 2's
  first path pattern. Drive recipe and the `freeze:false` trap: findings #25/#26.

- **DBVO 1.x cannot run on game 1.7.104 at all — nothing here can be tested against it until two
  third-party DLLs update.** Established in-engine 2026-09-03: SKSE 2.3.1 refuses both plugins
  `DBVO_Script_MCM` calls into, before loading them, with its own modal (finding #30) —
  `ConsoleUtilSSE.dll` 1.5.1.0 *"must be recompiled for new address library"* and `JContainers64.dll`
  *"disabled, incompatible with current version of the game"*. ConsoleUtil speaks the player's line;
  JContainers reads the voice-pack settings. (It is JContainers, not PapyrusUtil, that the shipped
  DBVO script actually uses.) Newest upstream: ConsoleUtilSSE NG **1.6.1** (2026-08-22) and
  JContainers SE **4.2.13.1** (2026-07-04) — neither installed here, and the Nexus API is read-only
  so they cannot be fetched by a session. 4.2.13.1 predates the 1.7.104 patch, so it may not be
  enough. Until then, any DBVO 1.x test is a test of the *mod's own tier* with the two Papyrus
  stimuli synthesised (see `mods/DBVODialogueTweaks/replyonlineend.steps`), never end-to-end.
- **Clean audio cut on skip** — the one feature DBVO 2 still lacks, because it never holds a sound
  handle (no sound RTTI in either build; neither references Address Library 36541/37542;
  `FireResponse` makes no audio call). Prefer **upstreaming** it to MathiewMay over a DLL-only
  successor: he now has the input handler and the skip path, and a successor would race his
  `SkipInputHandler` on the same input. Confirm in-engine first that a DBVO 2 skip really does leave
  the player line playing over the NPC — that claim is inferred, not observed.
  **Now also check DBReV** (below) before assuming the gap is still open: it advertises audio effects
  and SmartTalk skip compatibility but says nothing about cutting the player's line.

- **Compare Dragonborn ReVoiced against DBVO 2 and our mod — analysis only, not a patch.** DBReV
  (Nexus 184221, v1.5, 2026-09-02, Raynor1511) is the DBVO 1.x takeover we were about to consider
  building, already shipped and actively maintained. Open questions worth a dedicated session:
  does it cut the player's line audio on skip (the last thing that might be ours)? How does its
  timing engine compare to DBVO 2's `fuz-duration` base and to our line-end watcher? What does its
  lip-sync driver do that DBVO 2's doesn't? Does it need ConsoleUtil/JContainers (its page implies
  not — SKSE plugin + SkyUI MCM only)? Static analysis of its DLL, the same treatment
  `docs/plans/dbvo-v2-compatibility-analysis.md` gave DBVO 2. Landscape + the "don't fork DBVO 1.x"
  ruling: `docs/dbvo-landscape.md`.

## 2026-09-02 — Skyrim 1.7.104 fallout: what the move forward left open

The migration itself is **DONE and verified in-engine** (SKSE 2.3.1, Address Library v13, all six
plugins on `alandtse/CommonLibSSE-NG v7.1.0`) — see `CLAUDE.md` and `docs/skse-toolchain.md`. These
are the loose ends it created, roughly by impact.

- **A six-mod rebuild compiles CommonLibSSE-NG six times.** Each mod's `FetchContent` builds its own
  private copy (~500 TUs, ~10 min each) into its own `build/`; the plugin's own handful of files is
  noise beside it. Two fixes, not exclusive: **`ccache`** (not installed — needs root, and clang-cl
  works with it), and **build CommonLib once** into a shared prefix (it already ships
  `install(EXPORT)` + a `CommonLibSSEConfig.cmake`, so a small `tools/skse/commonlib/` project plus
  `find_package(CommonLibSSE CONFIG)` in the six mods would do it). Would turn ~60 min into ~10 + 6×20s.
- **The full profile is dead until third-party mods update.** Every SKSE DLL in `.profiles/full`
  predates format-5 Address Library, so `skytest play` hits the boot-parking modal. Nothing to fix
  here — it resolves as each mod author ships a 1.7.104 build — but do not plan work around `full`.
- **Restore boot-into-save**: drop Start On Save **2.8.0** (Nexus 50054, file 795157) into
  `skytest/base-skse/`, replacing the v2.7.0.1 DLL that dies on format 5. Until then every test
  needs `SKYTEST_NO_AUTOLOAD=1` plus a manual menu drive, and `replay` scripts that assume an
  in-world boot will not run.
- **Re-verify behaviour, not just loading.** Load + hook-install is proven for AutoFireBow and
  SkytestProbe (probe answers `inWorld:true`). Two behavioural tests are now **done** on 1.7.104:
  OneClickTravel's confirm suppression (2026-09-03) and DBVODialogueTweaks' reply-on-line-end
  (2026-09-03). Still owed: AutoFireBow's full-draw shot, AutoCastSpell's recharge loop (the
  log-flush pacing gotcha), GhostAllies' pass-through, and DBVODialogueTweaks' other three
  features (skip, interrupt-cut, volume — same hook, but each needs its own test). 1.7.99 changed
  struct layouts; treat each as owed, not optional.
- **Upstream now cross-compiles on Linux itself** (`cmake/toolchain-linux-clangcl.cmake`, NG v6.6.0,
  clang-cl + xwin). `tools/skse/cross-env.sh` is our hand-rolled equivalent and still works, but the
  maintained path exists now; it wants an `xwin splat --use-winsysroot-style` sysroot. Evaluate
  before extending the local glue.

## 2026-09-03 — skytest friction left behind by the DBVO 1.7.104 session

- **A test profile no longer writes crash logs.** `CrashLogger.dll` is listed in
  `<game>/.profiles/base-skse.skip` because it dies on the format-5 Address Library and parks every
  boot. Correct, but it means a test session that crashes now leaves *nothing* — one replay boot did
  die at step 8 ("session died") during this session and there was no crash log to explain it, and
  it did not recur. Either find a CrashLogger build for 1.7.104 or accept that a test-session crash
  is currently undiagnosable, and say so rather than hunting a log that was never written.
- **Make the per-step filmstrip cheap enough for a timing test.** Each shot is a SIGUSR2 +
  AVIF→PNG round trip of several seconds, so `replay` scripts whose steps must land inside a time
  window have to run `--no-shots` (finding #34) and lose their visual evidence. Options: capture the
  AVIF and defer the PNG conversion to after the run, or snap only on gate failure. Would let a
  timing test keep its filmstrip.
- **Skyrim may already be past 1.7.104.** Dragonborn ReVoiced's page claims testing against
  **1.7.140**. If that build is real, this repo's whole 1.7.104 baseline (SKSE 2.3.1, Address
  Library v13, the six rebuilt DLLs) is one patch behind again. Verify before assuming 1.7.104 is
  current; `skytest status`'s `runtime` line reports what is actually installed here.
