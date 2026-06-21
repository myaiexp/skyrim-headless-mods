#pragma once
// Owned facegen transition-target ramp with per-frame apply hook.
#include <string>

#include <RE/Skyrim.h>

namespace engine
{
	// Install the facegen-ramp hook (BSFaceGenNiNode::UpdateDownwardPass vtable detour). Call once
	// at kDataLoaded, on the main thread. Inert until a ramp is armed+triggered; see StartFaceGenRamp.
	void InstallFaceGenHook();

	// The mouth-close fix, made observable: an OWNED ramp of the target keyframe's ->values toward 0
	// (selected by `kf`, default unk140 — the live-mouth driver candidate; transitionTarget@0x18 reads
	// 0.0 during speech so scaling it is a no-op, the root cause of every prior failed attempt). Reset
	// doesn't touch these, so the eased close must lerp it ourselves. The actual per-frame scaling is
	// done by the InstallFaceGenHook hook — it runs at
	// the morph-APPLY point (after the lip pump writes, before the mesh reads), which is the only
	// seam that beats the pump (an AddTask-scheduled write runs too early and the pump overwrites it).
	// A ticker thread (NOT a self-re-queuing task — that hard-freezes the game; see engine.cpp) only
	// drives the lifecycle: resolve the speaker, self-trigger on speech, do the cut, retire the ramp.
	//
	// Lifecycle, all on the main thread:
	//   1. WAIT  — re-resolve a_ref each frame ("speaker" tracks the live talker); once
	//      transitionTarget max >= threshold (= mid-word) capture the current values as the
	//      ramp's start, optionally SetSpeakingDone(true), and begin. Aborts after waitMs.
	//   2. RAMP  — each frame lerp values[i] = start[i] * (1 - t), t = elapsed/ms (ms=0 snaps).
	//   3. HOLD  — for holdMs after reaching 0, keep observing; if reassert, keep forcing 0.
	// Every RAMP/HOLD frame logs {src:"ramp", maxBefore, maxAfter, t, elapsed}: maxBefore is
	// the value the engine's lip pump LEFT at frame start — if it tracks our descending ramp
	// the close HOLDS; if it bounces back up the pump RE-CLOBBERS (the hold-vs-bounce verdict
	// at per-frame resolution the 4 Hz facegen-watch can't give). speakingDone/reassert are the
	// A/B knobs (does stopping the pump suffice? does the fix need per-frame re-assertion?).
	struct FaceGenRampParams
	{
		std::string ref          = "speaker";  // re-resolved each frame
		std::string kf           = "unk140";   // WHICH keyframe to ramp — the leading mouth-driver
		                                       // candidate (animData+0x140, ch0; the Ghidra apply reads
		                                       // it and the live sweep saw it dominant during speech).
		                                       // transitionTarget@0x18 reads 0.0 mid-speech — scaling it
		                                       // does nothing, which is why every prior attempt failed.
		                                       // Accepts the same tags the facegen dump prints
		                                       // (transitionTarget, expression, modifier, phoneme,
		                                       // custom, unk040/0C0/0E0/100/120/140/160/180).
		float       ms           = 150.0F;     // ramp duration; 0 => snap straight to 0
		float       holdMs       = 1500.0F;    // observe (+ optionally re-assert 0) after the ramp
		float       threshold    = 0.3F;       // self-trigger when transitionTarget max >= this
		bool        speakingDone = true;       // SetSpeakingDone(true) at ramp start (mirrors the cut)
		bool        cut          = false;      // at trigger, do CutNpcReply's audio-stop first (fade the
		                                       // ExtraSayToTopicInfo sound + PauseCurrentDialogue) so the
		                                       // pump goes quiet — WITHOUT this, mid-speech audio re-drives
		                                       // transitionTarget every frame and any ramp loses (dead-end 6).
		bool        reassert     = true;       // re-write the target every frame (own it) vs ramp-then-release
		bool        live         = true;       // INERT now: the hook always damps the live values (current[i]*
		                                       // factor) — the snapshot approach was refuted (the pump moves to
		                                       // phonemes a snapshot never ramps). Kept for command-schema compat.
		bool        snapshot     = false;      // ease mode. false = scale the LIVE value each frame (value[i]*
		                                       // factor) — works while the pump keeps the keyframe alive (e.g.
		                                       // during the audio fade). true = ease a SNAPSHOT captured at
		                                       // trigger (snap[i]*factor) — robust when the engine zeroes the
		                                       // keyframe out from under us (the snap), since we drive an
		                                       // absolute decaying curve, not a scale of a value that may be 0.
		float       waitMs       = 10000.0F;   // self-trigger timeout -> abort if no speech
	};
	// Bumps the ramp generation (cancelling any active ramp) and schedules the first frame.
	void StartFaceGenRamp(const FaceGenRampParams& a_params);
	// Bump the generation so the active ramp's next frame self-stops (facegen-ramp on:false).
	void CancelFaceGenRamp();

	// ---- per-frame facegen OBSERVE (read-only) --------------------------------------------------
	// Arm the per-frame apply hook to LOG the target actor's mouth keyframe(s) EVERY frame WITHOUT
	// modifying them — the sub-100 ms characterization the 4 Hz facegen-watch is too coarse for (the
	// 1-frame mouth snap, the residual tongue flick). a_kf empty => all mouth channels
	// (unk0C0/unk140/unk180); else just that one keyframe (same tags as the dump). Resolves a_ref to
	// its facegen at arm time and PINS to it (re-arm if the speaker changes / 3D reloads). a_on=false
	// disarms. Each armed frame emits {src:"face-frame", kf, max, maxIdx, time, gt, paused} — `time`
	// is the engine frame clock (per-frame dedup), `gt`/`paused` the sim-advance guard (GetSimClock).
	// Returns false + a_err if a_ref or its facegen won't resolve, or a_kf is unknown. Main-thread
	// only (call via the command's EnqueueMain), like the ramp — the hook reads this state on the
	// same thread, so no lock is needed.
	bool ArmFaceGenObserve(const std::string& a_ref, const std::string& a_kf, bool a_on, std::string& a_err);

	// ---- react to the REAL skip (the product trigger) -------------------------------------------
	// The mouth-snap fix's real home: instead of the test-only speech-onset auto-cut, ease the mouth
	// shut when the player actually skips an NPC reply. DBVO's DialogueMenu.swf fires the
	// "CutNpcDBVOReply" mod event on that skip (the same event DBVODialogueTweaks' CutNpcReply sinks);
	// we sink it too and, while armed, start an eased close of the speaker's mouth keyframe — driven by
	// the proven per-frame apply hook, triggered IMMEDIATELY (no WAIT/threshold phase). This lets the
	// fix be iterated in the isolated harness against a real skip before it's ported into CutNpcReply.
	struct SkipEaseParams
	{
		std::string kf       = "unk140";  // mouth keyframe to ease (see FaceGenRampParams::kf)
		float       ms       = 150.0F;    // ease duration
		float       holdMs   = 300.0F;    // re-assert 0 this long after the ease (then release)
		bool        snapshot = false;     // ease mode (see FaceGenRampParams::snapshot)
	};
	// Arm/disarm reacting to the next CutNpcDBVOReply with an eased close. Stores the params.
	void ArmSkipEase(bool a_on, const SkipEaseParams& a_params);
	// Install the ModCallbackEvent sink for "CutNpcDBVOReply". Call once at kDataLoaded.
	void InstallSkipEaseSink();
}