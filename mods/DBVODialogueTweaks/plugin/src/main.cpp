#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace
{
	constexpr auto kVersion = REL::Version{ 1, 0, 0 };

	// Route spdlog (what SKSE::log::* uses) to <My Games>/SKSE/DBVODialogueTweaks.log so the
	// plugin leaves a visible trace that it loaded.
	void SetupLog()
	{
		auto logDir = SKSE::log::log_directory();
		if (!logDir) {
			return;
		}
		auto path = *logDir / "DBVODialogueTweaks.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
		auto logger = std::make_shared<spdlog::logger>("global", std::move(sink));
		logger->set_level(spdlog::level::info);
		logger->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(logger));
	}
}

// Declarative SKSE plugin metadata (CommonLibSSE-NG). Exported as SKSEPlugin_Version +
// SKSEPlugin_Query so SKSE recognises and loads the DLL.
SKSEPluginInfo(
	.Version = kVersion,
	.Name = "DBVODialogueTweaks",
	.Author = "mase",
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

// Entry point SKSE calls after loading the plugin.
SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();
	SKSE::Init(a_skse);
	SKSE::log::info("DBVODialogueTweaks {} loaded", kVersion.string());
	return true;
}
