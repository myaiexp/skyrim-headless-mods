# AutoCastSpell

> **Status: working, verified in-engine (v1.0.7).** Hold a cast control with a fire-and-forget
> spell equipped and it auto-fires the instant it's fully charged, then auto-recasts in a loop
> until you release.

Hold the cast button with a Firebolt (or any fire-and-forget spell) equipped and it casts the
instant it reaches full charge — no release timing to learn — then recharges and casts again, over
and over, until you let go. It's the spell analog of AutoFireBow: the same "hold to keep loosing"
feel, rekeyed from the bow to magic. Per hand and independent, so holding both hands runs two loops
at once and naturally **dual-casts**.

## What it does

- **Auto-fires at full charge.** While a cast control is held, the spell fires the moment it's fully
  charged — you never have to time the release yourself.
- **Auto-recasts in a loop.** After each cast the held button recharges and the spell fires again,
  repeating until you release the control.
- **Per hand, independent.** The right-hand and left-hand loops run separately off the same
  machinery; **hold both** and you get looped **dual-casts** (the engine dual-casts when both hands
  are charged, the loop just keeps recasting).
- **Only fire-and-forget spells.** Bolt/projectile spells that charge then loose (Firebolt, Ice
  Spike, Fireball, and the like) loop. **Concentration streams** (Flames, Sparks, wards, healing
  streams) and **instant casts** are left completely alone — see Limitations.

## Requirements

- Skyrim Special Edition or Anniversary Edition + **SKSE**
- **Address Library for SKSE Plugins**

No `.esp`, no scripts, no Papyrus: a single SKSE DLL. It takes no load-order slot.

## Compatibility

- **SE + AE, one DLL for both.** Built on CommonLibSSE-NG; every engine address is resolved at
  runtime through the Address Library, so the same file runs on every SE and AE build (Steam or
  GOG) as long as Address Library is installed. **Verified in-engine on AE** (v1.6.1170).
- **VR, untested.** No VR-specific build is provided.
- Manual (un-held, non-fire-and-forget) casting is completely unaffected, so it sits cleanly
  alongside other magic mods.

## Installation

1. Install with a mod manager and enable it (or drop `AutoCastSpell.dll` into
   `Data/SKSE/Plugins/`).
2. **Restart Skyrim.**

That's all. It's active immediately, always on, no configuration.

## How it works

A fire-and-forget spell charges off the **held** cast control and holds "ready" until release — the
same charge-then-loose shape AutoFireBow drives for a bow draw. Holding the button _is_ the charge
and releasing _is_ the cast, so a held button never self-recasts. The loop therefore injects a
synthetic **release** to fire on the genuine full charge, then lets the still-held button recharge
for the next cycle — routing through the engine's real cast pipeline so the cast keeps honest
magnitude, perk effects, and dual-cast scaling (it is the engine's own cast, not a synthesized
projectile).

The detection mechanism is the one surprise: unlike AutoFireBow, which keys off a bow anim event,
**there is no "spell charged" animation event** — the charge period is animation-silent. So
AutoCastSpell **polls `RE::MagicCaster::state` (~25 Hz)** per held hand instead of using an event
sink:

- On **`kReady`** (fully charged) → inject a synthetic **release** for that hand's control → the
  spell fires.
- On the **next charge** after a fire → re-arm that hand's per-cycle guard (a still-held button
  auto-recharges much like a held bow re-draws).
- **Release-nudge fallback:** if the cast winds down to idle without a fresh charge starting, a
  two-step synthetic **release → press** re-taps the control to kick off the next charge.

An off-thread poller (it only enqueues a game-thread check while a control is held, and touches no
game state itself) drives the cycle; all input injection runs on the game thread via the task
interface. An input sink tracks the real held-state of each cast control and skips the mod's own
injected events, so a physical release ends the loop cleanly. A `AutoCastSpell.log` in the SKSE log
dir records each state transition and loop decision for in-game verification.

Verified in-engine: right-hand, left-hand, and dual-cast (both held) loops all work;
concentration spells stay excluded; and running out of magicka stalls the loop cleanly with no
input/animation spam.

## Limitations

- **The loop's timing currently depends on its per-cycle logging.** This is a known fragility: the
  `flush_on(info)` disk-flush in the logger is what spaces the synthetic injects from the
  state re-reads, and the recharge cadence relies on that spacing. Stripping the logging regressed
  the loop badly (7 casts dropped to 2). It works reliably as shipped, but a read-only log dir or an
  spdlog flush change could alter the timing. Replacing flush-as-pacing with explicit, deliberate
  pacing is deferred — see `../../docs/ideas.md`.
- **Out of magicka, the loop stalls.** When a charge can't be afforded the engine never reaches the
  "charged" state, so the loop stops mid-stream (no input/animation spam). You release and re-press
  to resume once magicka regenerates; an automatic "still-held" watchdog that resumes on regen is
  deferred (`../../docs/ideas.md`).
- **No configuration yet.** v1 is always-on with no MCM. A SkyUI MCM (master toggle, toggle hotkey,
  and a **min-cast-delay** cadence cap — which matters here because magicka drains fast) is the
  deferred follow-up (`../../docs/ideas.md`).

## Building from source

Linux, headless: no Creation Kit or SSEEdit.

```bash
./build.sh            # configure + build -> build/AutoCastSpell.dll
./build.sh --install  # also copy the DLL into the live game's SKSE/Plugins
```

Cross-compiled Linux → Windows with the in-repo `tools/skse` toolchain (clang-cl + lld-link + xwin;
CommonLibSSE-NG fetched and pinned by CMake). See `../../docs/skse-toolchain.md`.

## Design notes

The full design (the synthetic-input cast loop, why it routes through the real cast pipeline rather
than a direct cast API, the corrected mechanism — polling `MagicCaster::state` because there is no
"spell charged" anim event — and the fire-and-forget type gate) lives in
`../../docs/plans/autocastspell-design.md`, with `autocastspell-plan.md` for the build steps.
Deferred work (explicit pacing to replace the log-flush timing, an MCM with a min-cast-delay cadence
cap, the magicka-out watchdog, and a public Nexus release) is in `../../docs/ideas.md`.
