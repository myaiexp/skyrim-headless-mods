#pragma once
// FormID/ref resolution and crosshair/actor-value lookup helpers.
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <RE/Skyrim.h>

namespace engine
{
	// "0x0014BEEF" for a FormID; "0x.. (Name)" when the form resolves to a named form.
	std::string HexID(RE::FormID a_id);
	std::string FormLabel(RE::FormID a_id);

	// ref keyword resolution (lazy, at use time):
	//   "player" | "crosshair" | hex FormID ("0x14") -> one ref (nullptr if unresolvable)
	RE::TESObjectREFR* ResolveOne(const std::string& a_ref);
	//   "teammates" -> the live player-teammate set (empty for anything else)
	std::vector<RE::Actor*> ResolveSet(const std::string& a_ref);

	// Resolve a list of ref strings (single keywords, "teammates", or hex) to the set
	// of FormIDs used as an engine-event filter. "teammates" expands to each member.
	std::unordered_set<RE::FormID> ResolveToFormIDs(const std::vector<std::string>& a_refs);

	// Current crosshair-highlighted reference, or nullptr (nothing targeted / pre-load).
	RE::TESObjectREFR* GetCrosshairRef();

	// Arbitrary AV name ("health","onehanded",…) -> enum; ActorValue::kNone if unknown.
	RE::ActorValue ResolveActorValue(std::string_view a_name);

	// Named aliases for spawned refs: `placeatme … as <name>` registers <name> -> FormID,
	// then ResolveOne("<name>") resolves it just like player/crosshair. Lets a .steps script
	// address an actor it spawned (whose runtime FormID it can't know in advance) by a stable
	// name. Main-thread only (written from the placeatme task, read from other command tasks —
	// all run on the main thread, so no lock). Reserved keywords win over an alias of the same
	// name; an alias never shadows player/crosshair/speaker/teammates.
	void SetAlias(const std::string& a_name, RE::FormID a_id);
	void ClearAliases();
}