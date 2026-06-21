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

	// Spawn a_baseID (an actor base / placeable bound object) a_distance units along the
	// player's forward vector at the player's elevation, facing back toward the player.
	// a_freeze => disable the spawned actor's AI so it stays put for a deterministic test,
	// while keeping its collision body present (EnableAI(false) — there is no SetDontMove in
	// this CommonLib checkout). Returns the spawned ref's FormID in a_outID. false + a_err if
	// the player / base form / spawn is unavailable. Main-thread only.
	bool PlaceActorForward(RE::FormID a_baseID, float a_distance, bool a_freeze,
		RE::FormID& a_outID, std::string& a_err);

	// Set/clear the kPlayerTeammate flag on an actor. No SetPlayerTeammate member exists in this
	// CommonLib checkout, so we write the BOOL_BIT directly; IsPlayerTeammate() — and thus
	// GhostAllies' IsGhostAlly — read exactly this bit. false if null. Main-thread only.
	bool SetTeammate(RE::Actor* a_actor, bool a_on);

	// Cast a_spellID from a_caster at a_target via the hand's MagicCaster::CastSpellImmediate —
	// spawns a real aimed projectile (the exact path GhostAllies' UpdateImpl stamp hooks) with
	// NO player input, so a pass-through test needs no camera/aim driving. a_target may be null
	// (free cast along the caster's facing). a_blame is set to the caster, so GhostAllies'
	// caster==player gate sees the player. false + a_err if caster/spell/caster-slot missing.
	bool CastAt(RE::Actor* a_caster, RE::FormID a_spellID, RE::TESObjectREFR* a_target,
		Hand a_hand, std::string& a_err);
}