#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>

#include "commands.h"
#include "config.h"
#include "hotkey.h"
#include "probes.h"
#include "trace.h"

namespace
{
	// spdlog (what SKSE::log::* writes through) -> <Documents>/My Games/
	// Skyrim Special Edition/SKSE/SkytestProbe.log — plugin diagnostics, separate
	// from the structured trace.jsonl.
	void SetupLog()
	{
		auto logDir = SKSE::log::log_directory();
		if (!logDir) {
			return;
		}
		auto path = *logDir / "SkytestProbe.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
		auto logger = std::make_shared<spdlog::logger>("global", std::move(sink));
		logger->set_level(spdlog::level::info);
		logger->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(logger));
	}

	std::string Trim(std::string s)
	{
		auto notSpace = [](unsigned char c) { return !std::isspace(c); };
		s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
		s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
		return s;
	}

	// Deferred setup that needs loaded game data. kDataLoaded is on the main thread.
	void OnDataLoaded()
	{
		trace::Init();
		probes::RegisterEventSinks();
		hotkey::Register();
		commands::Start();
		SKSE::log::info("SkytestProbe: kDataLoaded setup complete");
	}

	// MessagingInterface callback is a plain function pointer -> must be non-capturing.
	void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
	{
		if (a_msg && a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
			OnDataLoaded();
		}
	}
}

// Reads Data/SKSE/Plugins/SkytestProbe.ini (relative to the game's working dir,
// where skytest/install places it). Missing/garbled keys keep their defaults.
void config::Load()
{
	std::ifstream f("Data/SKSE/Plugins/SkytestProbe.ini");
	if (!f.is_open()) {
		return;  // defaults stand
	}
	std::string line;
	while (std::getline(f, line)) {
		const auto hash = line.find_first_of(";#");
		if (hash != std::string::npos) {
			line.erase(hash);
		}
		const auto eq = line.find('=');
		if (eq == std::string::npos) {
			continue;  // section header / blank
		}
		std::string key = Trim(line.substr(0, eq));
		std::string val = Trim(line.substr(eq + 1));
		if (key.empty() || val.empty()) {
			continue;
		}
		try {
			if (key == "MarkerHotkey") {
				config::markerHotkey = static_cast<std::uint32_t>(std::stoul(val, nullptr, 0));
			} else if (key == "Notifications") {
				config::notifications = (val == "true" || val == "1");
			} else if (key == "PollIntervalMs") {
				config::pollIntervalMs = std::max(50, std::stoi(val));
			}
		} catch (const std::exception&) {
			// keep the default for this key
		}
	}
}

SKSEPluginInfo(
	.Version = REL::Version{ 0, 2, 0 },
	.Name = "SkytestProbe",
	.Author = "mase",
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();
	config::Load();
	SKSE::Init(a_skse);  // before GetMessagingInterface/GetTaskInterface

	auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(MessageHandler)) {
		SKSE::log::error("SkytestProbe: failed to register messaging listener");
		return false;
	}
	SKSE::log::info("SkytestProbe 0.2.0 loaded (hotkey DX 0x{:02X}, poll {} ms)",
		config::markerHotkey, config::pollIntervalMs);
	return true;
}
