#pragma once
// Spell equip and actor-value staging helpers (main-thread only).
#include <string>

#include <RE/Skyrim.h>

namespace engine
{
	// Cast-setup helpers (main-thread only, null-safe). Direct engine calls are the
	// reliable staging path for the spell/AV setup a cast test needs — and the GENERAL
	// staging path: console exec (CompileAndRun) is mis-bound on 1.6.1170 (stale CommonLib
	// id), so don't route staging through it. Add a helper here per need instead.
	enum class Hand
	{
		kRight,
		kLeft,
		kBoth
	};
	// Add a_spellID to a_actor's spell list (if missing) and equip it to the hand(s).
	// Returns false with a reason in a_err if the actor/spell/equip-manager is unavailable.
	bool GiveSpell(RE::Actor* a_actor, RE::FormID a_spellID, Hand a_hand, std::string& a_err);
	// Set an actor value's base AND refill its current to that value (so e.g. magicka
	// 1000 is immediately castable; 0 drains it for the magicka-out test). false if null.
	bool SetAV(RE::Actor* a_actor, RE::ActorValue a_av, float a_value);
}