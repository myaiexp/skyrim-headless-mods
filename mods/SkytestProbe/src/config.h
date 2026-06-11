#pragma once
// Runtime configuration, loaded once from Data/SKSE/Plugins/SkytestProbe.ini at
// plugin load. inline variables => one definition shared across all TUs; Load()
// is defined in main.cpp. Defaults are the sensible standalone values, so a
// missing/garbled ini degrades to "F11 hotkey, notifications on, 250 ms poll".
#include <cstdint>

namespace config
{
	inline std::uint32_t markerHotkey = 0x57;  // DX scancode, F11. 0 = disabled.
	inline bool          notifications = true;  // on-screen DebugNotification feedback.
	inline int           pollIntervalMs = 250;  // command-file poll + watch cadence.

	void Load();  // reads the ini; defined in main.cpp.
}
