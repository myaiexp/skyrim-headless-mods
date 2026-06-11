#pragma once
// F11 (configurable) marker hotkey: an input-device sink that, on key-down, drops
// a marker line + auto-dumps the player and the crosshair target into the trace,
// with optional on-screen notification. Register once at kDataLoaded.
namespace hotkey
{
	// Registers the input sink on BSInputDeviceManager. No-op if config::markerHotkey
	// is 0 (hotkey disabled) or the manager singleton isn't ready. Main thread.
	void Register();
}
