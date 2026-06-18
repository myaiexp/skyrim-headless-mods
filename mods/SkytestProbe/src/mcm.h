#pragma once
// SkyUI MCM config enumeration and property read helpers.
#include <string>
#include <vector>

namespace engine
{
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