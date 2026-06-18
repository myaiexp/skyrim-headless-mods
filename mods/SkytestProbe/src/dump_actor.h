#pragma once
// Snapshot one actor into a trace dump-shaped JSON line.
#include <string>
#include <vector>

#include <RE/Skyrim.h>

namespace engine
{
	// Snapshot one actor into a trace "dump"-shaped line (src tags the producer:
	// "dump" for the command, "f11" for the hotkey auto-dump). avs = extra actor
	// values to include beyond health/magicka/stamina.
	void DumpActor(RE::Actor* a_actor, const std::vector<std::string>& a_avs, const char* a_src);
}