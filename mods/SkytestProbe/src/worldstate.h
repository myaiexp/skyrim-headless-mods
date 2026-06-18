#pragma once
// World-readiness snapshot and UI menu-open queries (main-thread only).
#include <string>

#include <RE/Skyrim.h>

namespace engine
{
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

	// Is the named UI menu currently open? Null-safe: false when the UI singleton is
	// unavailable (pre-load). Main-thread only (UI access). Mirrors the IsMenuOpen
	// calls in GetWorldState.
	bool IsMenuOpen(const std::string& a_menu);
}