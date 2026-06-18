# DBVO dialogue mouth-snap — investigation handoff (v2)

**Status:** mechanism fully characterized; the fix is blocked on ONE reverse-engineering find. The
eased-close *technique* is proven to work; it's attached to the wrong engine function. **Next session's
job is a Ghidra dive** to find the right seam, then re-point an already-built hook at it.
**Date:** 2026-06-18 (supersedes v1 — v1's "ramp `transitionTargetKeyFrame` via an owned per-frame
write" plan was tried and is now understood to be insufficient by itself; see below).
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
   **Yet the mouth STILL snaps in-game.** Conclusion: `UpdateDownwardPass` is a *transform* pass, not
   the morph apply — the head mesh is deformed from the pump's value by a different, earlier code path,
   so our override lands in the keyframe too late to be read. **Right technique, wrong frame-function.**

## THE OPEN WORK — the one RE find (do this next)

Use the Ghidra tier (`tools/ghidra/ghidra.sh`; `SkyrimSE.exe` is already unpacked + analyzed,
`tools/ghidra/projects/SkyrimSE.rep`; read `docs/ghidra.md`). Find ONE of:

- **(preferred) The morph-APPLY site:** the function that reads `BSFaceGenAnimationData`'s
  `transitionTargetKeyFrame->values` (i.e. `*(this+0x18)` then `+0x10`) and pushes them into the head
  geometry's morpher/mesh. Hook it; scale the values it reads (or scale them right before it runs).
- **(alt) The pump WRITE site:** the function that writes `transitionTarget->values` each frame (the
  audio-driven lip-sync). Hooking it to post-scale its output also works, regardless of where the apply
  is — robust. Likely owned by **`BGShkPhonemeController`** (RTTI `685545/393330`, VTABLE
  `252031/200149`) or the `LipSynchAnimDB__LipAudioInterface` path. Disassemble its update/apply method.

Search strategy in Ghidra: find xrefs that touch `BSFaceGenAnimationData+0x18`, or start from
`BGShkPhonemeController`'s vtable and trace its per-frame apply. The goal is a hookable `call`/function
entry (Address-Library-style) where scaling `transitionTarget` is read by the SAME frame's mesh deform.

Then **re-point the existing hook** (`engine::InstallFaceGenHook` in `mods/SkytestProbe/src/engine.cpp`)
from `UpdateDownwardPass` to the found function and re-test (harness below). The scaling logic in
`ApplyRampScale` is already correct — only the install target changes.

### Cheaper hypothesis to try FIRST (5 min, no RE)

Before the Ghidra dive, test whether the lip-sync mouth amplitude tracks **audio volume**: in a probe
or a throwaway mod build, change `CutNpcReply`'s `FadeOutAndRelease(30)` to a much longer fade (e.g.
`300`). If Skyrim's lip-sync scales mouth-openness by real-time audio RMS, a longer fade would taper the
mouth for free (one-line fix, no hook). Prior reasoning says FaceFX phonemes are precomputed (won't
track volume), so this probably fails — but it's cheap and would obviate everything. Confirm/kill it.

## What is BUILT (SkytestProbe — committed this session)

All in `mods/SkytestProbe/src/{engine,commands,probes,main}.{cpp,h}`. The DLL builds clean and is
symlinked into the `full` profile by `playtest`.

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

## How to run the harness (verified working this session)

1. `bash skytest/skytest playtest` → boots `full` to the MAIN MENU. Probe loads ~60–75 s in (watch
   `trace.jsonl` go non-empty).
2. `bash skytest/skytest drive seq e e` (dismiss Anniversary notice + Continue). Newest save is now
   **helmet-off Lydia** (Mase made it 2026-06-18 so `Continue` lands on a face you can see). Wait ~16 s;
   poll `status` for `"inWorld":true`.
3. IO dir (NOT redirected for playtest):
   `…/steamapps/compatdata/489830/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/SKSE/skytest/`
   — append commands to `commands.jsonl`, read `trace.jsonl`. `playtest` resets both on launch.
4. Arm `facegen-ramp` (cut:true), then talk to Lydia and let her speak — the probe auto-cuts + the hook
   scales. Read `"via":"hook"` lines: `maxAfter` = the value we hand the apply; visual = the verdict.
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
