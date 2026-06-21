# DBVO dialogue mouth-snap — investigation handoff (v4)

**Status (v4):** the mouth keyframe is **identified and the eased close WORKS in the harness** on the
real skip. v4 overturns v1–v3's core premise: the mouth is **`unk140`**, NOT `transitionTargetKeyFrame`
— `transitionTarget` reads **0.0 during speech**, so every prior session (≈1.5M tokens) was scaling a
dead keyframe, which is exactly why nothing ever moved. Remaining: one **minor one-frame tongue flick**
at the skip instant, and the **port into the product** (`DBVODialogueTweaks::CutNpcReply`).
**Date:** 2026-06-21 (supersedes v3).
**Tier:** SKSE C++. Fix prototyped + proven in **SkytestProbe** (instrumentation); not yet in the product.

## The problem (unchanged)

In DBVO play, when the player **picks a new dialogue topic while the NPC is still mid-reply** ("PC speaks
over NPC"), `DialogueMenu.swf` fires the `CutNpcDBVOReply` mod-event → `DBVODialogueTweaks::CutNpcReply()`
(`mods/DBVODialogueTweaks/plugin/src/main.cpp:238`), which fades the NPC audio + `PauseCurrentDialogue` +
`SetSpeakingDone(true)` + `Reset(0.0)`. The NPC's mouth **snaps shut** instantly. Mase wants it to **ease
shut** over ~150 ms.

## What v4 PROVED (overturns v1–v3)

1. **The visible mouth = `unk140`** (`BSFaceGenAnimationData` offset 0x140, the apply's **channel 0**),
   NOT `transitionTargetKeyFrame@0x18`. Two independent confirmations:
   - **Live sweep (game running, not paused):** during Lydia's speech `unk140` oscillates **0 ↔ 0.99**
     with the viseme index jumping (0,8,13,5,15,…), dropping to 0 between words. `transitionTarget` reads
     a flat **0.0 / null** the whole time. (`facegen-watch` → `src:"face"` lines; the v3 "confirmed
     transitionTarget" was a **paused** snapshot — 12 identical samples — which misled every prior session.)
   - **Causal:** forcing `unk140`→0 every frame via the apply hook (while she talks) **seals her lips**
     (`maxAfter` pinned at 0, lips visibly closed). Forcing `transitionTarget` did nothing.
2. **The apply (`FUN_140432550`, the v3 Ghidra find — still valid) reads THREE mouth channels:**
   `unk0C0` (ch1), `unk140` (ch0), `unk180` (ch3). In the live sweep: `unk140` carries the visemes;
   `unk0C0` is **inverted** (≈0.5 at rest / silence, ≈0 during speech — a "rest pose" channel); `unk180`
   ≈0. The OTHER nonzero channels (`unk100`/`unk120`/`unk0E0`) are interpolation **sources** the apply
   does NOT read — scaling them does nothing to the mesh.
3. **`transitionTarget` is dead for the mouth.** Disregard it (and the whole v1–v3 "transitionTarget"
   avenue) entirely.
4. **The current `CutNpcReply` doesn't even touch the real mouth keyframe.** Its `Reset(0.0,true,true,
   true,false)` resets expression(0x20)/modifier(0x60)/phoneme(0x80) — none of which is `unk140`. The
   snap is the engine releasing `unk140` to 0 when the line is cut, not the `Reset`.

## The eased-close mechanism that WORKS (built in SkytestProbe this session)

All in `mods/SkytestProbe/src/facegen_ramp.{h,cpp}` (+ `commands.cpp`, `main.cpp`). DLL builds clean and is
symlinked into the `full` profile, so `skytest play agent` loads it.

- **`InstallFaceGenHook`** — the proven per-frame seam: vtable detour on `BSFaceGenNiNode::
  UpdateDownwardPass` (idx 0x2C). It scales/sets the target keyframe pre-original, so the deferred morph
  apply renders our value. **This seam works for `unk140`** (the v3 worry that it was the wrong seam does
  not bite — zeroing `unk140` here visibly closes the mouth).
- **`facegen-ramp` command** gained `kf` (which keyframe to ramp; default `unk140`; accepts any dump tag)
  and `snapshot` (ease a captured start vs scale the live value). Single-channel; used for diagnostics.
- **`facegen-skip-ease` command + `CutNpcDBVOReply` mod-event sink (`InstallSkipEaseSink`)** — the actual
  fix prototype. `{"cmd":"facegen-skip-ease","on":true,"ms":150,"holdMs":80}` arms it; on the player's
  REAL skip it eases the speaker's mouth shut. Three hard-won pieces, each fixing a failed attempt:
  1. **Rolling pre-snap capture** — the skip zeroes the keyframes within a frame, *before* a deferred
     (`AddTask`) trigger can read them (every trigger logged `max:0`). So while armed, the ticker samples
     the speaker each tick and keeps the last **OPEN** pose (mid-word) AND the last **REST** pose (a
     silence), per channel. The ease starts from the captured open pose, not a (zero) read at skip time.
  2. **Ease toward the CAPTURED REST pose, not the live value** — during the PC's reply the dialogue menu
     **freezes** the face at the open pose, so blending toward `live` holds the mouth open (a regression
     we hit). Easing `from`=open `to`=rest (`values[i]=from*(1-t)+to*t`) settles it to neutral correctly.
  3. **Ease ALL apply-read channels in lockstep** (`unk0C0`+`unk140`+`unk180`, the `kMouthChans` set) —
     easing only `unk140` left `unk0C0`/`unk180` to snap, flashing the tongue/inner-mouth to rest while
     the lips glided.

## Current state / what's left

- **WORKS:** on the real skip the mouth now eases shut over 150 ms (no snap, no freeze-open). Verified
  in-engine by Mase on the full-profile Lydia save.
- **Remaining nitpick — a ONE-FRAME tongue flick** at the skip instant. Minor (Mase: "someone might not
  even notice"). **Leading hypothesis:** the 1-frame gap between the `CutNpcDBVOReply` event and our
  `AddTask`-deferred `TriggerEaseNow` — for that one frame the engine's snapped tongue shows before our
  ease takes over. Likely fixes: capture+seed the ease **synchronously in the sink's `ProcessEvent`**
  (no `AddTask` delay), or detect the skip at the hook level. Untried.
- **NOT DONE — the product port.** The fix lives in SkytestProbe (test harness). It must move into
  `DBVODialogueTweaks::CutNpcReply` so it ships: ease `unk140`(+`unk0C0`/`unk180`) open→rest over 150 ms
  via the same `UpdateDownwardPass` hook, replacing the ineffective `Reset`-based close. DBVO will need
  its own copy of the hook + rolling capture (or the capture running while in dialogue). Mind the
  process-wide vtable slot if SkytestProbe is also loaded (both detour 0x2C — chained pass-through is OK
  but don't double-scale).

## How to test (Mase drives the character; CC drives the probe)

CC cannot reliably get an NPC talking + perform a mid-reply skip, so **Mase controls the character**; CC
arms the probe and reads the trace. Loop:
1. `bash skytest/skytest play agent` → detached full-profile session; wait ~55–75 s for the probe (poll
   `trace.jsonl` non-empty). Boots to MENU.
2. Mase: load into the Lydia save (clear the notice, Continue — the 120 ms `drive seq` gap is too tight
   for CC to do it reliably).
3. CC: append to `commands.jsonl` (IO dir
   `…/489830/pfx/.../My Games/Skyrim Special Edition/SKSE/skytest/`):
   `{"id":"se","cmd":"facegen-skip-ease","on":true,"ms":150,"holdMs":80}`
4. Mase: talk to Lydia, and **mid-reply pick a new topic** (the real skip). Watch the mouth.
5. CC: read `trace.jsonl` — `skip-ease-trigger` (`openMax`, `haveRest`), `mode:"blend"` per-frame lines.
6. `bash skytest/skytest stop`. Diagnostic sweep of all channels: `facegen-watch on:true` → mine
   `src:"face"` lines (`.kf["unk140@140"].max`, etc.) — but only meaningful while the game is RUNNING
   (a paused/menu snapshot gives identical samples and lies, the v3 trap).

## Methodology notes

- **The breakthrough was data, not more flailing:** a LIVE keyframe sweep (game running) settled which
  keyframe is the mouth. A paused snapshot is worthless here — it sent v1–v3 down the `transitionTarget`
  dead end. Always confirm "running, not paused" (samples must vary frame to frame).
- All probe instrumentation lives in **SkytestProbe** and stays (project norm). `facegen-ramp kf/snapshot`,
  `facegen-skip-ease`, and the `CutNpcDBVOReply` sink are now part of the accreting toolkit.
