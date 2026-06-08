# RapidBowHold

Hold the attack button with a bow or crossbow equipped → it auto-fires **full-strength**
shots in a loop, at the bow's natural full-draw cadence, for as long as you hold. Release to
stop. No custom animations; reacts to the game's own animation events.

- **Requires:** SKSE.
- **Plugin:** `RapidBowHoldQuest.esp` (one Start-Game-Enabled quest hosting the script).
- **Script:** `RapidBowHoldScript.pex`.

## How it works

1. `OnControlDown` for the attack control sets "rapid mode" on. The control bound to **LMB**
   is the user-event **`Right Attack/Block`** (right hand = main weapon — counterintuitive but
   confirmed from `controlmap.txt`); both attack controls are registered for robustness.
2. When the bow reaches full draw, the game emits the `BowDrawn` animation event.
3. On that event the script sends `attackRelease` (looses a full-strength arrow), waits
   briefly, then sends draw-restart events so the next draw begins immediately.
4. A safety timer forces a cycle if a full-draw event is ever missed, so "hold to rapid"
   never gets stuck.

Guards block firing in menus, dialogue, while sitting/mounted, etc.

## Build & install

```bash
./build.sh            # -> build/RapidBowHoldQuest.esp + build/Scripts/RapidBowHoldScript.pex
./build.sh --install  # also installs into the live game + activates the plugin
```

After `--install`: **fully restart Skyrim**, then console (`~`):
`stopquest RapidBowHoldQuest` / `startquest RapidBowHoldQuest`. See `../../docs/workflow.md`.

## Status / notes

- **Working, verified in-game.** The loop self-sustains at the bow's full-draw rate (~2.5 s
  per shot for a standard bow — every shot full power, which is the intent).
- The current `src/RapidBowHoldScript.psc` is the **instrumented (debug) build**: it traces
  every animation event and sends three candidate redraw events (`bowAttackStart`,
  `BowDrawStart`, `bowDraw`) because we confirmed the loop works but didn't isolate which
  single event is load-bearing. It's noisy in the Papyrus log.
- **TODO (release cleanup):** strip the `[anim]`/`FIRE`/`REDRAW` traces and narrow the redraw
  to the one event that actually triggers the draw. Functionally safe (doesn't change what's
  sent), but verify with one more full-restart test before considering it final.

## Tuning

- `safetyMaxDrawSeconds` (top of the script) — fallback timeout if `BowDrawn` is missed.
- If a different setup looses on a different event, the candidates are in
  `FireFullDrawAndRestart()`; the `[anim]` trace in the Papyrus log shows the real cycle.
