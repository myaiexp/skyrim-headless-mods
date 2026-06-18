#pragma once
// Owned facegen transition-target ramp with per-frame apply hook.
#include <string>

#include <RE/Skyrim.h>

namespace engine
{
	// Install the facegen-ramp hook (BSFaceGenNiNode::UpdateDownwardPass vtable detour). Call once
	// at kDataLoaded, on the main thread. Inert until a ramp is armed+triggered; see StartFaceGenRamp.
	void InstallFaceGenHook();

	// The mouth-close fix, made observable: an OWNED ramp of transitionTargetKeyFrame->values toward
	// 0 (the confirmed live-mouth keyframe; Reset doesn't touch it, so the eased close must lerp it
	// ourselves). The actual per-frame scaling is done by the InstallFaceGenHook hook — it runs at
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
		float       waitMs       = 10000.0F;   // self-trigger timeout -> abort if no speech
	};
	// Bumps the ramp generation (cancelling any active ramp) and schedules the first frame.
	void StartFaceGenRamp(const FaceGenRampParams& a_params);
	// Bump the generation so the active ramp's next frame self-stops (facegen-ramp on:false).
	void CancelFaceGenRamp();
}