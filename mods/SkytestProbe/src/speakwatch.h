#pragma once
// Read-only observer for the player's DBVO voice line.
//
// DBVO (1.x via ConsoleUtil, 2.x natively) plays the player's line as a free-standing
// BSSoundHandle through Actor::SpeakSoundFunction. Nothing in the engine hands that handle
// out afterwards, so "is the player's line still playing right now?" is unanswerable from
// Papyrus, from the UI, and from a screenshot — the question is about audio. This module
// answers it by detouring that one function, copying the handle by value, and SAMPLING it.
//
// It is an OBSERVER: it never calls SetVolume, FadeOutAndRelease, or anything else that
// mutates the handle. That distinction is the whole point — DBVODialogueTweaks hooks the
// same function to *cut* the line, and mixing the two would measure our own interference
// instead of the mod under test.
//
// The measurement it exists for: does a DBVO 2 skip leave the player's line audible over
// the NPC's reply? Arm this, click a topic, skip mid-line, and read the trace — if the
// handle is still playing after the skip, it does.
#include <string>

#include <RE/Skyrim.h>

namespace engine
{
	// Install the SpeakSoundFunction entry detour (Address Library 36541 SE / 37542 AE, via
	// MinHook — the same target and idiom as DBVODialogueTweaks). Call once at kDataLoaded on
	// the main thread. Idle until armed.
	//
	// Returns false with a_err set when the target is ALREADY hooked. MinHook allows one hook
	// per target, so in a profile that also contains DBVODialogueTweaks whichever plugin loads
	// second loses. That is not a bug to work around: the observer is for measuring DBVO
	// *without* our mod present, which is exactly the profile where it wins.
	bool InstallSpeakWatchHook(std::string& a_err);

	// Arm/disarm the observer. a_prefix gates which paths count, matched case-insensitively
	// against the start of the sound path; "DBVO/" is every DBVO player line (1.x and 2.x both
	// build paths under it). Arming clears any previously retained handle.
	bool ArmSpeakWatch(const std::string& a_prefix, bool a_on, std::string& a_err);

	// Poll-cadence sample (main thread, from probes::MainTick). Emits src:"speak" lines on
	// state changes only: "playing" once the retained handle is first observed playing, then
	// "stopped" with the elapsed ms when it goes quiet. No line per tick — the interesting
	// signal is the transition and its timestamp, which is what you compare against the NPC
	// reply / the skip input.
	void SampleSpeakWatch();

	// True while armed — drives probes::HasMainTickWork() so the poll thread only schedules a
	// main-thread tick when there is something to sample.
	bool SpeakWatchArmed();
}
