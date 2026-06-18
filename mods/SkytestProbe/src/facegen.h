#pragma once
// Facegen morph snapshot and parameterized NPC-cut close helpers.
#include <string>

#include <RE/Skyrim.h>

namespace engine
{
	// Snapshot an actor's facegen morphs (phoneme/expression/modifier keyframes) +
	// speaking state into a trace line tagged a_src ("face"). Read under faceGen->lock
	// (the lip-sync writer holds it). Null-safe: writes a line noting absent facegen.
	void DumpFaceGen(RE::Actor* a_actor, const char* a_src);

	// Parameterized facegen reset — the v4 NPC-cut close with the three variables the
	// snap hypothesis turns on exposed: a_timer (0.0 = hard SNAP, >0 = eased), a_lock
	// (run under faceGen->lock), a_speakingDone (SetSpeakingDone(true) first). Mirrors
	// DBVODialogueTweaks CutNpcReply otherwise. false + reason when no facegen data.
	bool CloseFaceGen(RE::Actor* a_actor, float a_timer, bool a_lock, bool a_speakingDone, std::string& a_err);
}