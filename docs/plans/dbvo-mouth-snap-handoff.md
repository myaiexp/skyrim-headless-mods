# DBVO dialogue mouth-snap — investigation handoff (v2)

**Status (v3):** mechanism characterized AND the RE find is **DONE**. The facegen apply pipeline is
mapped (Ghidra, 2026-06-18) — the handoff's hooked seam (`UpdateDownwardPass`) was wrong because it
only *registers* the face for a **deferred** morph pass; the real apply body and the per-frame
interpolator are both found (see "THE RE FIND — RESOLVED"). Next: re-point `InstallFaceGenHook` at the
`Blend` keyframe-vtable slot (0x02) and test on the Lydia save.
**Date:** 2026-06-18 (v3 supersedes v2's "blocked on one RE find" — that find is resolved below).
**Tier:** SKSE C++ (SkytestProbe instrumentation now; fix eventually lands in DBVODialogueTweaks).

## The problem (unchanged)

In DBVO play, when the player **picks a new dialogue topic while the NPC is still mid-reply** ("makes
the PC speak over the NPC"), the swf fires `CutNpcDBVOReply` → `DBVODialogueTweaks` `CutNpcReply()`
(`mods/DBVODialogueTweaks/plugin/src/main.cpp:238`), which fades the NPC audio + `PauseCurrentDialogue`
+ `SetSpeakingDone(true)` + `Reset(0.0)`. The NPC's mouth **snaps shut** instantly — jarring. Mase wants
it to **ease shut** over ~150 ms. (The handler is confirmed correct — this IS the "PC speaks over NPC"
path. There is NO automatic skip in the fix; it runs only on the player's real topic-pick.)

## What is now CONFIRMED (the hard-won mechanism)

1. **The visible mouth = `transitionTargetKeyFrame`** (`BSFaceGenAnimationData` offset 0x18), NOT
   phoneme/expression/modifier (those read ~0 during plain speech). [carried from v1, re-confirmed]
2. **The snap = the lip pump RELEASING `transitionTarget`** (≈0.5 → 0 in one frame) when the cut audio
   finally stops — which is **~200 ms AFTER** `FadeOutAndRelease(30)`, not instantly.
3. **`Reset(0.0)` is a red herring** for the mouth — it resets expression/phoneme/modifier/custom, and
   does **not** touch `transitionTarget`. So tuning `Reset`'s timer (v1 hypothesis B) cannot smooth the
   mouth. Disregard that whole avenue.
4. **`SetSpeakingDone(true)` does NOT stop the pump** while audio is still playing — the audio-driven
   lip-sync keeps rewriting `transitionTarget` every frame (this reproduced v4 dead-end 6 exactly).
5. **`AddTask`-scheduled writes LOSE the per-frame race.** An owned ramp that writes `transitionTarget`
   from a paced ticker via `AddTask` runs too early in the frame; the pump overwrites it afterward.
   Probe trace: our `maxAfter` decays smoothly, but the next frame's `maxBefore` is pinned back at the
   pump's ~0.5. So the rendered mouth follows the pump and snaps on release. **Snapshot-ramp and
   live-damp both fail for this reason** — it is not a tuning issue, it's a write-ordering issue.
6. **A per-frame HOOK can override the keyframe cleanly — but the seam matters.** A vtable detour on
   `BSFaceGenNiNode::UpdateDownwardPass` (idx 0x2C; the node holds its `BSFaceGenAnimationData` at
   runtime-data 0x28) fires per-frame, dedups via `NiUpdateData.time`, and the trace shows `maxAfter`
   decaying perfectly (0.49→0.18→0.01→0, no compounding) and holding at 0 while the pump writes 0.5.
   **Yet the mouth STILL snaps in-game.** [RESOLVED below — `UpdateDownwardPass` only *registers* the
   face for a deferred morph pass; the interpolator overwrites our scale before the deferred apply reads
   it. Right technique, wrong seam **and** wrong frame-phase. See "THE RE FIND — RESOLVED".]

## THE RE FIND — RESOLVED (Ghidra, 2026-06-18)

The facegen morph is a **deferred, registered pass**, not inline in the scene-graph update — that is
why scaling at `UpdateDownwardPass` failed. Pipeline on the unpacked 1.6.1170 exe
(`tools/ghidra/scripts/facegen_apply.py` + `xref_to.py`; full dumps in `tools/ghidra/out/`):

1. **`BSFaceGenNiNode::UpdateDownwardPass` (vtable 0x2C) → `FUN_140432230`** — 18-insn wrapper, calls
   `FUN_1404330c0`.
2. **`FUN_1404330c0` REGISTERS the face** — does NOT read keyframe values. Checks the
   `BSFaceGenAnimationData` flags (node holds it at `param_1[0x2a]`), sets `animData+0x215=1`
   ("queued this frame"), and under global spinlock `DAT_14313fa30` appends the node to a global
   registry array `DAT_14313fa18` / count `DAT_14313fa28`. **Enqueue only.**
3. **`FUN_140432f90` DRAINS the registry** (the xref consumer of `DAT_14313fa18`) — iterates it, per
   node dispatches `FUN_1404334e0` via job-runner `FUN_140cf66b0`, then zeroes the count.
4. **`FUN_1404334e0` → `FUN_140432550`** = the **per-face morph-APPLY body** (263 insns): per face
   geometry it reads keyframe channels and calls `FUN_140430fe0(target, channel, idx, geom, value)` to
   deform the mesh, gated by camera/LOD distance.

**Why the old hook failed (now certain):** the interpolator that produces `transitionTarget` runs
*after* `UpdateDownwardPass` (the register step) but *before* the deferred apply, so it overwrites our
scaled value before the mesh deform reads it. Ordering bug, not tuning.

### Two re-point seams — the CommonLib-vs-raw-address choice

- **(preferred — portable) `BSFaceGenKeyframeMultiple::Blend` = vtable slot 0x02** (`FUN_14042a4f0`).
  Lerps two source keyframes into `this->values` (`(1-t)*srcA[i] + t*srcB[i]`) and clears `isUpdated`
  (+0x1c) — i.e. it **is** the per-frame "pump" write. A keyframe **vtable slot**, so hookable via
  CommonLib `VTABLE_BSFaceGenKeyframeMultiple` (`252277/200252`) → **auto-updates across patches, no
  raw address**. Gate to the ramp's target `transitionTargetKeyFrame`, post-scale after the blend.
  (Slots 0x0B/0x0E are predicates "is any/this value active?", NOT the apply.)
- **(alt — airtight, raw address) `FUN_140432550`** the apply body: scale `transitionTarget` at its
  entry, immediately before the read. No pump window, but a raw address (sig-scan or per-patch re-RE).

**ONE thing to confirm at runtime** (the existing `DumpFaceGen` probe is ground truth — don't re-RE):
my *static* offset read of `FUN_140432550` shows it touching `unk140/unk0C0/unk180`, not
`transitionTarget@0x18` — possibly SE/AE struct-offset ambiguity in the decompile. Confirm which
keyframe carries the mouth in this build, and that the chosen seam writes/reads it, before trusting it.

To re-point: change `engine::InstallFaceGenHook` (`mods/SkytestProbe/src/facegen_ramp.cpp` since the
2026-06-18 engine split) from the `BSFaceGenNiNode` vtable 0x2C detour to a `BSFaceGenKeyframeMultiple`
vtable 0x02 detour (post-scale, gated to the target keyframe). `ApplyRampScale`'s math is already correct.

### Deprioritized: the cheap "longer fade" hypothesis
Now that a seam is found this is no longer the first move, but it's still a cheap sanity-check: bump
`CutNpcReply`'s `FadeOutAndRelease(30)` to ~`300` and see if the mouth tapers with volume. Likely
*worse* (delayed snap) if the morph follows a precomputed FaceFX timeline rather than instantaneous RMS.

## What is BUILT (SkytestProbe — committed this session)

All in `mods/SkytestProbe/src/{engine,commands,probes,main}.{cpp,h}`. The DLL builds clean. The
drivable full-profile launch is **`skytest play agent`** (an `agent` mode on `play`, added 2026-06-18 —
the standalone `playtest` verb was rejected as a dumb name that could blunder into the modded save): it
boots `full` under a detached gamescope session, injects the probe, and resets IO. (Bare `skytest play`
can't substitute: it `exec`s Proton directly, bypassing the Steam client, so it ignores Steam launch
options — incl. any gamescope wrapper — and is blocking + non-drivable.)

- **`facegen-ramp` command** — self-triggering owned ramp of `transitionTarget`. JSON params:
  `{"cmd":"facegen-ramp","ref":"speaker","cut":true,"ms":150,"holdMs":1500,"threshold":0.3,
  "speakingDone":true,"reassert":true,"waitMs":300000}`. Lifecycle: WAIT (resolve `speaker`, fire when
  `transitionTarget` max ≥ `threshold`) → trigger (optional `cut`) → the **hook** scales each frame →
  retire after `ms`+`holdMs`. `on:false` cancels. (`live` is now inert — the hook always live-damps;
  the snapshot approach was refuted.)
- **`cut` mode** — at trigger, replicates `CutNpcReply`'s audio-stop (`ExtraSayToTopicInfo` sound
  `FadeOutAndRelease(30)` + `PauseCurrentDialogue`). This is a DEBUG STAND-IN for the player's real skip
  so the same engine moment can be replayed without driving — it is NOT product behavior.
- **Ticker thread** — paces lifecycle steps via ONE-SHOT `AddTask` (NOT self-re-queue — that freezes
  the game; see `skytest/docs/headless-findings.md` #20). Sleeps 16 ms active / 150 ms idle.
- **`InstallFaceGenHook`** — vtable detour on `BSFaceGenNiNode::UpdateDownwardPass` (idx 0x2C), gates to
  the ramp's target facegen, scales `transitionTarget` by the decay factor pre-original, dedups on
  `NiUpdateData.time`, logs `{"src":"ramp","via":"hook",...}`. **This is the piece to re-point** at the
  real seam once Ghidra finds it. Installed at kDataLoaded (`main.cpp`).
- (from prior session) `facegen-watch`, `speaker` ref keyword.

## How to run the harness (drivable full session — restored 2026-06-18)

1. `bash skytest/skytest play agent` → detached, drivable gamescope session on `full`; boots to the MENU,
   injects the probe + resets IO. Probe loads ~60–75 s in (watch `trace.jsonl` go non-empty).
2. `bash skytest/skytest drive seq e e` to dismiss the Anniversary notice + `Continue`. Newest save is
   **helmet-off Lydia** (Mase made it 2026-06-18 so Continue lands on a face you can see). Wait ~16 s;
   `bash skytest/skytest status` until `"inWorld":true`. (`ready`/`gs_wait_ready` unreliable on full.)
3. IO dir:
   `…/steamapps/compatdata/489830/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/SKSE/skytest/`
   — append commands to `commands.jsonl`, read `trace.jsonl` (both reset by `play agent` on launch).
4. Arm `facegen-ramp` (cut:true), then talk to Lydia and let her speak — the probe auto-cuts + the hook
   scales. Read `"via":"hook"` lines: `maxAfter` = the value we hand the apply; visual = the verdict
   (`bash skytest/skytest shot`).
5. Tear down: `bash skytest/skytest stop`.

**Gotchas:** `ready`/`gs_wait_ready` unreliable on `full` (just wait); autosaves can clobber the test
save (disable in Settings); `drive seq` inter-key gap is 120 ms — tight for menu→Continue (widening it
is a noted skytest TODO); the cut-audio fade keeps the pump alive ~200 ms.

## Methodology note (for the next session)

This investigation drifted into elaborate probe variants (snapshot vs live vs cut vs hook) that lost the
human in the loop. The mechanism is now nailed down — resist re-deriving it. The ONLY open question is
the right hook seam, and that is a focused Ghidra task. Don't rebuild the probe; find the function,
re-point `InstallFaceGenHook`, test, then port the proven hook into `CutNpcReply` (fires on the real
skip, no auto-cut) and have Mase test an actual topic-skip.
