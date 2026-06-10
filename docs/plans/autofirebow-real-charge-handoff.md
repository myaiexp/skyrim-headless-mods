# AutoFireBow real-charge — session handoff (2026-06-09)

Ephemeral. Picks up from `autofirebow-real-charge-{design,plan}.md`, which are now **partially
superseded** (see below). Delete once the open question is resolved and docs are reconciled.

## What shipped this session (committed, installed, verified working)

The spike's core question — *will the engine honor a synthetic input-release for the real charge
path?* — is **answered: yes.**

- **Loose = synthetic input-release.** `SendSyntheticAttack(false)` builds a `"Right Attack/Block"`
  `ButtonEvent` and replays it through `BSInputDeviceManager::SendEvent` (the same source the engine
  fans out to `PlayerControls` each frame). Replaces the old cosmetic `NotifyAnimationGraph` loose.
  `AttackInputSink` is guarded with `g_injectingSynthetic` so our fake event doesn't move the real
  held-state. (`main.cpp` — `SendSyntheticAttack`.)
- **Re-nock needs an explicit synthetic press.** *(Corrected 2026-06-10 — the original "re-nock is
  free, the engine re-presses itself" claim below was a false positive.)* The "sustained loop" this
  session verified was actually a **stale duplicate plugin, `RapidBow.dll`** (the pre-rename build,
  never removed from the live game), silently doing the re-nock via `NotifyAnimationGraph`. With both
  DLLs loaded they shared the same vtable slot + anim sinks; removing the duplicate exposed that
  AutoFireBow alone looses **once and stops**. The design doc was right all along: a held button never
  self-redraws. Fixed by injecting a synthetic **press** (`SendSyntheticAttack(true)`) to start the
  next draw, fired from `AutoArrowHook` at the auto arrow's launch (not chained at loose — a press
  before the launch completes trips the vanilla double-tap stuck-bow state). **Gotcha:** the engine's
  nock event is capital-**`BowDraw`** (RapidBow injected lowercase `bowDraw` itself, so its guard
  re-arm matched; the input-driven redraw emits the engine's casing). Verified in-game: sustained
  loop, stops on release.

  *Original (incorrect) note, kept for the record:* ~~The synthetic release desyncs the engine's
  button state from the still-held physical button → the engine re-presses itself → next draw starts
  on its own. No re-nock injection.~~
- **Old `PowerSpeedHook` clamp is gone.** No `runtime.power = 1.0`, no `weaponDamage *= 1/natural`.
- **Auto arrows get a flat damage bump** (`kAutoDamageMult`, currently `1.10`) via the
  `AutoArrowHook` on `ArrowProjectile::GetPowerSpeedMult`, gated by `g_boostNextArrow` (set at
  auto-loose, cleared on first apply). **Verified correct via trace** (see below): boost lands on
  exactly the held/full-draw arrow, once, no off-by-one leak, manual quick-shots untouched.

Current `main.cpp` is the clean playable build (v2.0.0, trace logging stripped, one info line per
auto boost). Installed to the live `Data/SKSE/Plugins`.

## Pivots from the original plan (why design/plan are superseded)

- **Plan Task 1's "remove PowerSpeedHook entirely" was internally inconsistent** with keeping the
  `natural_power` readout (which lived inside it). Resolved by keeping the hook as observe-only, then
  ultimately repurposing it for the damage bump.
- **The cadence/"release at 0.9 power" direction was abandoned.** A timed early-release (fire N ms
  after nock) *broke re-nock* (firing mid-draw doesn't trigger the engine's self-re-press) and is
  bow-speed-dependent (cross-bow is a hard requirement) — dead end. Mase's real goal turned out to be
  **not faster cadence but ~10% more auto-arrow damage to compensate DPS** vs manual play. Hence the
  bump, not the timer.

## The open question for next session (the real crux)

**Removing the clamp changed nothing about damage, and one-taps still deal full damage.** Evidence
gathered this session:

- A pure-observation probe showed `runtime.power` reads **1.000 on every release** at the
  `GetPowerSpeedMult` hook — even a clearly partial ~1.2s draw. It is **not** the live draw fraction
  at that site.
- Mase reports **no damage *or distance* difference between early and full draws** in his game.
  Distance is charge-driven, so equal distance ⇒ **draw time isn't scaling the shot at all** in his
  setup.

→ Working hypothesis: **bow draw→damage/power scaling is simply not active in Mase's game**
(load-order mod, a `fBow*`/archery game setting, or the specific bow), so the clamp was always a
no-op for damage and removing it was invisible. The old 0.350-uncharged problem was specific to the
*graph-release* path; the *real-input-release* path we now use launches full-power regardless.

**If that holds, the whole "compensate lost DPS" premise dissolves** — every shot is already full
power, so the +10% is a plain buff, not compensation, and the "honest charge" framing is moot.

### First tests to run next session
1. Clearly-partial shot on a **clean/vanilla save** (or minimal load order): does distance differ
   from a full draw? If no → confirms draw-scaling is off and it's not our code.
2. If draw *does* scale on vanilla but not here → bisect the load order / check archery-related game
   settings and any archery overhaul mod.
3. Confirm whether `runtime.weaponDamage` on `ArrowProjectile` actually maps to **real enemy
   damage** (Mase's quick damage test was ambiguous). If real damage comes from a different path,
   the bump (and the old clamp) were operating on a field that doesn't drive outcomes.

## Gotchas / facts established (don't re-derive)
- `GetPowerSpeedMult` is called **~2× per arrow at launch** (state `BowReleased`), **not** per-frame
  in flight, and **each shot gets a fresh arrow pointer** — so "boost the next arrow" flag targeting
  is sound; immediate-clear handles the 2× call.
- The engine's `BowDrawn` event (the auto-loose trigger) fires at **~2.05s** after nock. Manual
  "full charge" releases often land *before* it (~1.6s) and so are treated as partial → no auto-loose,
  no boost. There is **no way to fire a manual full-draw shot without it auto-firing**, since holding
  to full *is* the auto trigger.
- Cross-bow consistency is a **hard requirement** — any time-based approach is rejected.
- Vtable slot for `GetPowerSpeedMult`: runtime-aware `REL::Relocate<std::size_t>(0xAF, 0xB0)`.
