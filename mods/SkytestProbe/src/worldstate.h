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

	// Sim-advance snapshot — the paused-vs-running guard for facegen samples. A frozen sim
	// emits samples IDENTICAL to live data (reading one as live is what sent the mouth-snap
	// chase down the transitionTarget dead end). Two corroborating signals:
	//   paused — the sim is frozen right now (UI::GameIsPaused() — menu/console — OR
	//            Main::freezeTime). The authoritative "don't read this as live" flag.
	//   gt     — Main::QFrameAnimTime, the per-frame game-time DELTA. Measured in-engine: ~0.0167
	//            (1/60 s) while the sim steps, exactly 0.0 while frozen. So gt==0 ⟺ this frame
	//            advanced no game time (frozen); gt>0 ⟺ the sim is actively stepping. (It is a
	//            per-frame delta, NOT an accumulator — consecutive live frames share the same gt.)
	// Cheap global reads; main-thread (matches the facegen read sites).
	struct SimClock
	{
		bool  paused = false;
		float gt     = 0.0F;
	};
	SimClock GetSimClock();

	// Is the named UI menu currently open? Null-safe: false when the UI singleton is
	// unavailable (pre-load). Main-thread only (UI access). Mirrors the IsMenuOpen
	// calls in GetWorldState.
	bool IsMenuOpen(const std::string& a_menu);
}