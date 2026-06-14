# AutoCastSpell — auto-recast held fire-and-forget spells — design

**Goal:** hold the cast control with a fire-and-forget spell equipped → the engine auto-recasts it in
a loop (charge → release → recharge → release …), exactly like AutoFireBow does for the bow, until
the control is released. Per hand, independent, so a right-hand loop, a left-hand loop, and held-both
**dual-cast** loops all work.

This is the **spell analog of AutoFireBow**, a deliberately separate standalone mod (Mase's call —
not folded into AutoFireBow). It reuses AutoFireBow's proven synthetic-input loop verbatim, rekeyed
from bow events to spell events.

## Background — the mechanic is the same axis as the bow

A fire-and-forget spell charges off the **held** `Right/Left Attack/Block` control and holds "ready"
until release — the identical charge-then-loose shape AutoFireBow drives for a bow draw. Holding the
button **is** the charge; releasing **is** the cast; they are the same input axis, so a held button
never self-recasts (no button-up to fire, no fresh button-down to recharge). Therefore, exactly as in
AutoFireBow, the loop must inject a synthetic **release** to fire and a synthetic **press** to
recharge each cycle.

AutoFireBow already established the load-bearing fact this mod depends on: the engine **honors
synthetic `ButtonEvent`s** for the real charged-attack path (`SendSyntheticAttack` looses
genuinely-charged arrows, verified in-game, v2.1.0). Magic casting uses the same controls and the
same `BSInputDeviceManager` fan-out, so the same injection should drive a charged spell cast. "Should"
is the one thing still to prove in-game (see §The make-or-break unknown).

## Approach — drive the real cast pipeline via synthetic input (not a direct cast API)

While the cast control is held, the engine is **already doing a real, charged cast** each cycle. The
only fake steps are the release (to fire on the genuine full charge) and the re-press (to recharge):

- **Cast** = inject a synthetic **release** `ButtonEvent` for the hand's control
  (`"Right Attack/Block"` / `"Left Attack/Block"`, value `0.0`, held duration set) when the spell
  reaches full charge. The engine runs its own cast on a genuinely-charged spell → honest magnitude,
  dual-cast scaling, and projectile/perk effects (e.g. Impact stagger) all intact — because it is the
  engine's real cast path, not a synthesized projectile.
- **Recharge** = inject a synthetic **press** to start the next charge, fired off the "spell fired"
  signal (the cast actually happened), mirroring how AutoFireBow re-nocks off the arrow's launch
  rather than blind-chaining the press (a press fired mid-cast trips the engine's own double-tap
  state).

**Why not `ActorMagicCaster::CastSpellImmediate` / a direct cast call:** faster to write, but it
**bypasses the charge-and-release mechanic Mase explicitly wants kept** — it skips the charge
animation/feel and risks not applying dual-cast scaling or the projectile's perk effects the way a
genuine charged cast does. Routing through real synthetic input gives honest, perk-correct casts.
Rejected for the same reason AutoFireBow looses real charged arrows instead of spawning clamped ones.

## The loop, per hand

State is **per hand** (two small structs — held flag + per-cycle fire guard). The right-hand and
left-hand loops are independent and run off the same machinery; holding both hands naturally produces
looped **dual-casts** (the engine dual-casts when both are charged, the loop just recasts it).

```
hold cast (hand H)                  → engine begins charging H's spell
  ↳ "begin cast (H)"  anim event    → re-arm H's per-cycle fire guard
  ↳ "spell charged (H)" anim event  → inject synthetic RELEASE (H)  → spell fires on full charge
  ↳ "spell fired (H)"  anim event   → inject synthetic PRESS (H)    → recharge
  … repeat while H held …
release cast (H)                    → H's loop ends
```

## Components (one `main.cpp`, mirroring AutoFireBow)

- **`CastInputSink`** (`BSTEventSink<InputEvent*>` on `BSInputDeviceManager`) — tracks the held state
  of `Right/Left Attack/Block` per hand from raw input. Skips our own injected events via the
  `g_injectingSynthetic` guard so synthetic release/press never move the real held-state the loop
  gates on.
- **`SpellLoopSink`** (`BSTEventSink<BSAnimationGraphEvent>` on the player) — on "begin cast (H)"
  re-arm H's per-cycle guard; on "spell charged (H)" while held + armed → cast; on "spell fired (H)"
  → re-press.
- **`SendSyntheticCast(hand, pressed)`** — the `ButtonEvent` injection helper, parameterized by hand
  (selects the control string). Press: `value 1.0`, `heldSecs 0` (IsDown). Release: `value 0.0`,
  `heldSecs > 0` (IsUp). Wrapped in `g_injectingSynthetic` and enqueued on the game thread via the
  task interface — no off-thread game-state access.
- **Per-hand loop state** — `struct HandLoop { bool held; bool firedThisCycle; }` ×2.

## Scope — gate on cast *type*, derived, not a hardcoded list

The loop arms for a hand only when that hand's equipped spell is **fire-and-forget**
(`MagicSystem::CastingType::kFireAndForget`), read from the equipped form each cycle. This is the
list-free definition of "charge then release to cast" and is future-proof — any modded FF spell just
works. It automatically excludes:

- **Concentration** spells (Flames, Sparks, wards, Healing-stream) — holding already streams them;
  nothing to auto-recast.
- **Staves, scrolls, instant casts** — no charge phase; they fire on a single press.

## The make-or-break unknown — the exact animation events

The one genuinely unproven piece (AutoFireBow flagged the same about its synthetic-attack route). The
bow's `BowDraw`/`BowDrawn` have magic analogs; the **likely** candidate tags are:

| Bow event   | Likely magic analog (confirm in-game)                       | Role               |
| ----------- | ----------------------------------------------------------- | ------------------ |
| `BowDraw`   | `BeginCastRight` / `BeginCastLeft`                          | charge start (arm) |
| `BowDrawn`  | `MRh_SpellReadyOut` / `MLh_SpellReadyOut`                   | charged → release  |
| arrow-launch hook | `MRh_SpellFire_Event` / `MLh_SpellFire_Event`         | fired → re-press   |

The exact tags/casing are **not trusted** until logged in-game. The plan's **first task** logs every
animation-graph event during a manual charged cast (via SkytestProbe) and pins down the real strings
before wiring the loop. That same probe answers the deeper gamble: **does a synthetic release
actually fire a charged spell** the way it looses a bow, or is the cast path welded to physical device
state? If injection doesn't take, no variant of this loop works — fail fast there, exactly as
AutoFireBow's design sequenced its probe.

## Edges & behavior

- **Magicka-out (v1):** when a charge can't be afforded, the engine never emits the "charged" event,
  so the loop **stalls** mid-stream — no input/animation spam. Player releases and re-presses to
  resume once magicka regens. Same shape as AutoFireBow stalling out of arrows. A "still-held"
  watchdog to auto-resume on regen is deferred (see `docs/ideas.md`).
- **Arm guards:** the sinks simply don't arm when no FF spell is equipped in that hand, magic is
  sheathed, or a menu is open.
- **No off-thread game access:** all injection enqueued on the game thread via the task interface
  (AutoFireBow's pattern).
- **Re-registration on load:** register the input + animation sinks on `kPostLoadGame`/`kNewGame`
  (AutoFireBow's `OnMessage`).

## Config — none in v1 (always-on)

v1 ships always-on, like AutoFireBow before its MCM, to prove the cast loop in-engine. A SkyUI MCM
(master toggle, toggle hotkey, **min-cast-delay** cadence cap) is the deferred follow-up — see
`docs/ideas.md`. The min-cast-delay matters more here than for the bow (magicka drains fast), so it is
the first MCM knob when config lands.

## Testing

AutoCastSpell is **standalone** (a FF spell + the DLL; no dependency on other mods), so the close-out
is a clean `skytest test mods/AutoCastSpell` — isolated vanilla+1, drivable gamescope session: equip
Firebolt, hold cast, screenshot/probe the repeated casts and the animation-event log. Not a
full-profile test.

## Why this shape (alternatives considered)

- **Fold into AutoFireBow as a unified "auto-attack" mod** — rejected by Mase; keep a clean separate
  release and zero risk to the shipped bow mod. ~90% machinery overlap is duplicated rather than
  shared (small `main.cpp`; the repo norm is self-contained per-mod DLLs).
- **Right hand only** — rejected; both-hands-independent is barely more work (the input sink already
  tracks both controls) and covers dual-cast + left-hand/off-hand casters without special-casing.
- **Direct cast API (`CastSpellImmediate`)** — rejected; bypasses the charge-release mechanic and
  risks dropping dual-cast scaling / perk effects (see §Approach).
- **MCM from v1** — deferred; the make-or-break unknown is the loop itself, and the MCM is proven
  territory that bolts on cleanly afterward (AutoFireBow's exact evolution).
