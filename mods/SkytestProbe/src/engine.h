#pragma once
// Main-thread engine helpers. EVERYTHING here touches live engine state and must
// run on the main thread (via SKSE::GetTaskInterface()->AddTask) — never from the
// command-poll thread. All paths are null-safe: an unresolvable ref / unloaded
// actor degrades to nullptr/empty/an error line, never a crash.
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

	// Run one console command line, fire-and-forget (ConsoleUtilSSE-NG technique).
	enum class ExecResult
	{
		kOk,          // compiled + ran
		kEmpty,       // empty command line
		kNotInWorld,  // main menu / mid-load: gated out (CompileAndRun would crash)
		kFaulted,     // reached CompileAndRun but it AV'd (caught by SEH; e.g. no console subsystem)
	};
	ExecResult RunConsoleCommand(const std::string& a_line);

	// Snapshot one actor into a trace "dump"-shaped line (src tags the producer:
	// "dump" for the command, "f11" for the hotkey auto-dump). avs = extra actor
	// values to include beyond health/magicka/stamina.
	void DumpActor(RE::Actor* a_actor, const std::vector<std::string>& a_avs, const char* a_src);
}
