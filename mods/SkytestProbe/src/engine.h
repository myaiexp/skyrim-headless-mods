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

	// World-readiness snapshot. `inWorld` is the EXACT gate the exec path needs:
	// no Main/Loading menu open AND the player's 3D is loaded. parentCell/gameActive
	// both flip true mid-load (too early); Is3DLoaded is the reliable "fully
	// interactive" signal. Read on the main thread only.
	struct WorldState
	{
		bool mainMenu    = false;  // Main Menu open (pre-load)
		bool loadingMenu = false;  // loading screen up
		bool is3DLoaded  = false;  // player character 3D present
		bool inWorld     = false;  // == !mainMenu && !loadingMenu && is3DLoaded
	};
	WorldState GetWorldState();
	bool       IsInWorld();  // == GetWorldState().inWorld; the exec/console gate

	// Run one console command line, fire-and-forget (ConsoleUtilSSE-NG technique).
	enum class ExecResult
	{
		kOk,          // compiled + ran
		kEmpty,       // empty command line
		kNotInWorld,  // main menu / mid-load: gated out (CompileAndRun would crash)
		kFaulted,     // reached CompileAndRun but it AV'd (caught by SEH; e.g. no console subsystem)
	};
	ExecResult RunConsoleCommand(const std::string& a_line);

	// Headless cast-setup helpers (main-thread only, null-safe). These exist because
	// console exec (CompileAndRun) AVs in a console-less test session — direct engine
	// calls are the reliable headless path for the spell/AV setup a cast test needs.
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

	// Snapshot one actor into a trace "dump"-shaped line (src tags the producer:
	// "dump" for the command, "f11" for the hotkey auto-dump). avs = extra actor
	// values to include beyond health/magicka/stamina.
	void DumpActor(RE::Actor* a_actor, const std::vector<std::string>& a_avs, const char* a_src);

	// MCM reveal (read-only). Main-thread only, null-safe — degrade to an empty result +
	// honest trace, never a crash. WRITES its own trace record (mirrors DumpActor).
	//
	// Enumerate registered SkyUI MCM configs -> writes one record:
	//   {"src":"mcm-list","via":"manager"|"scan"|"none","count":N,
	//    "mods":[{"name":<ModName>,"script":<class>,"pages":[...]}]}
	// Returns the count (>=0); 0 with count:0 written when SkyUI is absent (a successful scan).
	// Returns -1 ONLY when the Papyrus VM itself is unavailable (pre-load) — command acks false.
	int WriteMcmList();

	// Read named properties off a config script class -> writes one record:
	//   {"src":"mcm-get","script":<class>,"values":{<prop>:<bool|int|double|string>},"missing":[...]}
	// Returns false (writes nothing) when no quest binds a_script — the command acks the error.
	// Scalar props only (bool/int/float/string); absent or array/object props go to "missing".
	bool WriteMcmGet(const std::string& a_script, const std::vector<std::string>& a_props);
}
