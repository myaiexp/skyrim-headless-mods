# DBVO dialogue mouth-snap — investigation handoff

**Status:** instruments built + probe verified in-engine; the A/B experiment is **not yet run**.
**Date:** 2026-06-18. **Tier:** SKSE C++ (SkytestProbe instrumentation; the eventual fix lands in
DBVODialogueTweaks). **Relates to:** `dbvo-v4-voice-cut-on-skip-design.md` (the snap lives in v4's cut).

## The problem (what we're fixing)

The one thing that visibly bugs Mase in DBVO play: when you **skip an NPC's reply by selecting the next
topic**, the NPC's mouth **snaps shut** instantly — jarring. That snap is deliberate, in
`DBVODialogueTweaks/plugin/src/main.cpp` `CutNpcReply()`:

```cpp
actor->SetSpeakingDone(true);
RE::BSSpinLockGuard locker(faceGen->lock);
faceGen->ClearExpressionOverride();
faceGen->Reset(0.0f, true, true, true, false);   // a_timer = 0.0 -> hard SNAP to neutral
```

The hard `0.0` is there because, per **v4 dead-end 6**, an *eased* reset was tried and "did nothing" —
the lip-sync pump re-clobbered the open mouth during the ease window.

## The hypothesis (what we're testing)

Dead-end 6's failed ease was, by its own note, a **0.25 s eased, _unlocked_ `Reset`**. It conflated two
variables (eased **and** unlocked). The clean case — **eased, but under `faceGen->lock`, after
`SetSpeakingDone(true)`** — was **never tested**. If `SetSpeakingDone(true)` truly stops the pump, an
eased close under the lock may ramp the mouth shut smoothly and *hold*.

- **Confirmed** → the fix is just a non-zero `a_timer` in `CutNpcReply` (one-line change).
- **Refuted** (pump still re-clobbers) → fall back to an **owned per-frame ramp**: lerp the morphs to
  neutral ourselves over ~150 ms, re-asserting the target each frame under the lock so there's no
  single-frame window for the pump to win.

## What's built (SkytestProbe — committed)

Three additions, in the **clean-room spirit** (engine behavior only, no DBVO code):

- **`facegen-watch`** — `{"cmd":"facegen-watch","ref":"speaker"|"crosshair"|"0x..","on":true}`.
  Dumps the actor's `phenomeKeyFrame` / `expressionKeyFrame` / `modifierKeyFrame` morphs (count, max,
  maxIdx, full values) + `speaking` (`!QSpeakingDone()`) + `exprOverride` **every MainTick (~4 Hz)**,
  read under `faceGen->lock`. Tag `"src":"face"`. **Verified working in-engine** (captured Lydia).
- **`facegen-close`** — `{"cmd":"facegen-close","ref":"speaker","timer":0.2,"lock":true,"speakingDone":true}`.
  The v4 cut's reset, parameterized on the three hypothesis variables. **Built, NOT yet exercised.**
- **`speaker` ref keyword** — resolves `MenuTopicManager::speaker` (fallback `lastSpeaker`), i.e. the
  talking NPC, exactly as `CutNpcReply` does. Lets you target the speaker without its FormID.

Files: `mods/SkytestProbe/src/{commands,engine,probes}.cpp` + `{engine,probes}.h`. Built DLL is in
`mods/SkytestProbe/build/` **and copied into `.profiles/full/SKSE/Plugins/SkytestProbe.dll`** (the
full-profile copy was a stale Jun-14 build before; refreshed 2026-06-18).

## How to run it (the path that works)

`skytest ready`/`gs_wait_ready` does **not** work for the full profile — don't rely on it. The
**`test`** profile (vanilla+1) can't load the modded save, so we use **`playtest`** (full, drivable):

1. `bash skytest/skytest playtest`  → visible gamescope session (full modded load order).
2. It boots to the **main menu** (full has no StartOnSave). Drive **`drive seq e e`** (first E dismisses
   the Anniversary notice, second E = Continue) → loads the newest save = **Save265, Sleeping Giant Inn**,
   standing **facing Lydia**. **Just wait ~10 s** after Continue; it's reliably in-world by then.
3. Probe IO dir (NOT redirected for playtest):
   `…/steamapps/compatdata/489830/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/SKSE/skytest/`
   - append command JSON lines to `commands.jsonl`, read results from `trace.jsonl`.
   - **playtest does NOT reset these** (only `test` does) — `: > commands.jsonl; : > trace.jsonl`
     before the probe loads, or it re-runs stale history.
4. From "facing Lydia": next **E = talk**; further **E = advance/skip a topic**, which fires the mod's
   `CutNpcReply` → the snap.

## Preliminary finding (the surprise — investigate first next time)

With `facegen-watch speaker` armed on Lydia (`0x000A2C94`), the captured samples showed:

```
speaking:true , phoneme.max:0.0 , expression.max:0.0 , modifier.max:0.0   (every tick)
```

i.e. **`speaking` is true but all three keyframes read 0.0.** Two possibilities, must be disambiguated:

1. We sampled during a **non-vocalizing moment** (speaking-state set but no audio playing that instant /
   dialogue idle). Most likely — we never caught her mid-word with audio.
2. The **live mouth value isn't in `phenomeKeyFrame`** at all — it may live in
   `transitionTargetKeyFrame` (a `BSFaceGenKeyframeMultiple*` at offset 0x18 that the watch does **not**
   currently dump) or be applied to the mesh downstream. v4's "mouth frozen open" note implies
   `phenomeKeyFrame` *does* hold it, but that's unconfirmed by direct observation.

**Action:** next run, catch her **actually mid-word** (nonzero morph), and if it's still 0.0, extend
`DumpFaceGen` to also dump `*transitionTargetKeyFrame`. Without a nonzero "speaking" baseline, the snap
A/B is unreadable.

## The experiment matrix (once a nonzero speaking baseline is captured)

Get Lydia mid-line, then fire `facegen-close speaker` and watch `trace.jsonl` for whether `phoneme.max`
ramps to 0 and **stays** (hold = good) vs **bounces back up** (re-clobber = bad), over the next ~1–2 s:

| variant | timer | lock | speakingDone | meaning |
| --- | --- | --- | --- | --- |
| A baseline | 0.0 | true | true | reproduce the mod's hard snap |
| B hypothesis | 0.2 | true | true | **the untested case** — does locked+eased hold? |
| C dead-end | 0.2 | false | true | replicate dead-end 6 (expected: re-clobbers) |

4 Hz sampling is coarse but enough for hold-vs-bounce (end-state over ~1 s = 4–8 samples). It will NOT
show the fine ramp shape — fine for prove/refute, not for tuning.

## Gotchas (cost real time this session)

- **Autosave teleports Lydia / breaks the scene.** An autosave fired mid-test, kicked us out, Lydia
  teleported. **Disable autosaves** (Settings) before testing, or delete the bad autosave + reload.
- **`ready`/status readiness poll is unreliable on the full profile** — just wait ~10 s after Continue.
- **Live game-driving over the CC round-trip is impractical** — ~3 min latency per action (network /
  provider side) makes interactive input hopeless. **Next time: drive deterministically** via a
  `skytest replay` `.steps` script (boot → wait → talk → arm watch → skip cycles → `facegen-close` A/B →
  done) so the whole sequence runs in one shot, **or** Mase drives locally while CC only reads
  `trace.jsonl`. (Candidate to also note in `skytest/docs/headless-findings.md`.)

## Next steps

1. Disable autosaves; boot via the path above.
2. Capture a **nonzero** speaking baseline (catch her mid-word); if 0.0 persists, dump
   `transitionTargetKeyFrame` and re-test.
3. Run the A/B/C matrix; read `trace.jsonl` `"src":"face"` series for hold-vs-bounce.
4. **Verdict → fix:** B holds → one-line eased `a_timer` in `CutNpcReply`; B bounces → owned per-frame
   ramp. Re-run the same test to prove the fix; restore known-good on any regression.
5. Consider promoting the live-driving lesson + an `.steps` replay for this scene into the repo.
