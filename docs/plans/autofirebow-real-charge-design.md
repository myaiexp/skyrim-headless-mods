# AutoFireBow — real charge (drop the clamps) — design

**Goal:** make AutoFireBow public-worthy by removing the two power/damage clamps and producing
**genuinely engine-charged** auto-fired arrows instead. Today the auto-fire loop fakes a full shot
by rewriting the projectile; a Nexus release should loose real, honestly-charged arrows.

This is a **reverse-engineering spike**, not a clean edit. The central unknown can only be resolved
in-game (build here, test on Mase's runtime). Sequenced probe-first so we fail fast if a route
doesn't take.

## Background — why the clamps exist (the wall)

Bow arrow power (speed, damage, range) comes from the engine's **draw charge**, and charge is
**welded to the real input-release path** — not to animation-graph events. Established twice:

- Papyrus saga (`docs/findings-papyrus-limits.md`): `Debug.SendAnimationEvent("attackRelease")`,
  scripted draw, `Weapon.Fire`, and "script only the release" all loose **uncharged** arrows.
- SKSE loop (`docs/skse-plugin-plan.md:109-130`): the current loop looses via
  `NotifyAnimationGraph("attackRelease")` and logs `natural_power = 0.350` (uncharged) on
  auto-fired shots vs `1.000` on real manual ones.

The current mod papers over this one layer up: `PowerSpeedHook` (a vtable hook on
`ArrowProjectile::GetPowerSpeedMult`) clamps each player arrow's `runtime.power → 1.0` and rescales
`runtime.weaponDamage *= 1/natural`. That rescue is **structurally load-bearing** — delete it with
the graph-release design intact and every auto shot becomes a 0.350 wet noodle.

**Therefore: "drop the clamps" and "drive the real charge path" are the same task.** You cannot do
one without the other.

## Approach — synthesize real input-release, not a graph event

The physical attack button is already held while the loop runs, so the engine is *already* doing a
**real, charged draw** each cycle (that is why `BowDrawn` fires for real). The only fake step is the
**release**. So:

- **Loose** = inject a synthetic **input-release** `ButtonEvent` for `"Right Attack/Block"`
  (value `0.0`, held duration set) into the input pipeline, in place of
  `NotifyAnimationGraph("attackRelease")`. The engine runs its own loose on a genuinely-charged
  draw → honest power/damage, **no clamp**.
- **Re-nock** = the open question (below). After a *real* release with the button still physically
  held, the engine may auto-start a new real draw on its own. If so, the loop deletes its graph
  re-nock (`bowAttackStart`/`BowDrawStart`/`bowDraw`) entirely and just waits for the next
  `BowDrawn`. If not, inject a synthetic **press** to kick the next draw.

If this lands, **both clamps, the `PowerSpeedHook` vtable hook, and all `NotifyAnimationGraph` calls
disappear**, replaced by input-event injection. The ~220ms saturation gap and the "graph release is
uncharged" caveat both vanish because we are back on the engine's real path.

### The one unknown that decides cleanliness

**Does the engine auto-redraw when the attack button is still held after a real loose?**

- Can only be answered in-game.
- The current design's manual graph re-nock suggests "maybe not" — but that re-nock follows a
  *cosmetic* release, which leaves the attack state machine in an odd spot. After a *real* release
  the behavior may differ. Don't assume; probe.

## Sequencing (probe-first)

1. **Probe held-button redraw.** Minimal build: keep the loop trigger on `BowDrawn`, replace the
   graph release with synthetic input-release, **inject nothing for re-nock**, and **remove the
   `PowerSpeedHook` clamp** so the logged power is the engine's honest value (not the clamped 1.0).
   Log the next arrow's `natural_power` and whether a new draw/`BowDrawn` occurs while held.
   - Charge reads `~1.0` **and** it re-draws on its own → best case; clamps gone, re-nock gone.
   - Charge reads `~1.0` but it does **not** re-draw → add synthetic press for re-nock; clamps gone.
   - Charge still reads `~0.350` → input-release injection isn't reaching the charge path; fall back.
2. **Wire the resolved re-nock path**, delete `PowerSpeedHook` and the `NotifyAnimationGraph` calls.
3. **Verify**: in-game, auto-fired arrows log `natural_power ≈ 1.0` with **no clamp present**, land
   at full damage/speed, cadence acceptable, no main-thread hang, manual single shots unaffected.

## Fallbacks (if input-release injection doesn't reach the charge path)

In rough order of preference — each is a separate probe, not a guaranteed step:

- **Direct high-process release call** — find and call the function the real input-release invokes to
  loose a charged arrow (the `HighProcessData` attack-release path), passing the charge the real draw
  already built. Skips the input pipeline entirely.
- **Set-charge-then-loose** — locate the live draw-charge value in `HighProcessData`, write it to
  full (or its real current value), then trigger the loose. Precedent: *Manual Crossbow Reload*
  rewrites bow draw mechanics via SKSE.
- **Full press/hold/release synthesis** — fabricate the entire input sequence rather than relying on
  the physical hold, if partial injection proves to confuse the state machine.

## Risks / constraints

- **In-game iteration required.** I can build the DLL headlessly; only Mase can test loose behavior
  on the live runtime. This is an iterative RE loop, not a one-shot edit.
- **Input injection is engine-fiddly** (the plan's own caveat). The engine may read device state, not
  just queued events — synthetic events might not fully drive the attack handler. That is exactly
  what probe step 1 tests before we invest further.
- **Construct/route of a synthetic `ButtonEvent`** must be verified against CommonLibSSE-NG
  (`InputEvent`/`ButtonEvent` ctors, and where to enqueue — `BSInputDeviceManager` vs player
  controls). Confirm the API before relying on it.
- **Vanilla double-tap bug** (tap, re-tap before loose → bow won't release; `skse-plugin-plan.md:160`)
  is pre-existing and out of scope; don't conflate any new stuck-draw with it.

## Success criteria

- Auto-fired arrows are **genuinely charged** (`natural_power ≈ 1.0` logged) with **no
  `power`/`weaponDamage` clamp anywhere in the codebase.
- Hold-to-auto-fire still works: looses repeatedly while held, stops on release, no hang.
- Manual single shots behave 100% vanilla.
- Nexus messaging updates from "every tap forced to full power via engine hook" to the honest
  "hold to auto-fire genuinely-charged shots" framing.

## Out of scope (this spike)

- INI/hotkey config (`docs/ideas.md` — still deferred).
- Fire-at-saturation cadence reclaim and TDM visibility-gating (`skse-plugin-plan.md` follow-ups) —
  revisit only after honest charge works.
