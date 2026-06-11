#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <atomic>

namespace
{
	constexpr auto kVersion = REL::Version{ 1, 0, 0 };

	// Volume scale applied to the player's own DBVO voice line, pushed in from Papyrus via the
	// SetPlayerVoiceVolume native (MCM slider). 1.0 = vanilla (the hook is a pure pass-through
	// at this value), 0.0 = silent, 2.0 = double. atomic because the Papyrus VM thread writes it
	// while the game thread (inside the speak-sound hook) reads it.
	static std::atomic<float> g_dbvoVolume{ 1.0f };

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

	// case-INSENSITIVE: true iff a_path begins with "DBVO/" or "DBVO\". DBVO voice lines are
	// dispatched as Player.SpeakSound "DBVO/<file>.fuz"; this is the gate that limits the volume
	// scale to that voice pack and leaves all other speak-sound calls untouched. Tiny manual
	// compare — no heavy string deps.
	bool is_dbvo_path(const char* a_path)
	{
		if (!a_path) {
			return false;
		}
		static constexpr char kPrefix[4] = { 'd', 'b', 'v', 'o' };
		for (int i = 0; i < 4; ++i) {
			const char c = a_path[i];
			if (c == '\0') {
				return false;  // shorter than "DBVO" — bail before reading the separator
			}
			const char lc = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
			if (lc != kPrefix[i]) {
				return false;
			}
		}
		const char sep = a_path[4];
		return sep == '/' || sep == '\\';
	}

	// Papyrus native: DBVOTweaks.SetPlayerVoiceVolume(Float factor). Just stores the factor; the
	// speak-sound hook reads it. The class string "DBVOTweaks" MUST match the Papyrus script
	// (Scriptname DBVOTweaks Hidden / Function SetPlayerVoiceVolume(Float factor) Global Native).
	void SetPlayerVoiceVolume(RE::StaticFunctionTag*, float factor)
	{
		g_dbvoVolume = factor;
	}

	bool RegisterFuncs(RE::BSScript::IVirtualMachine* vm)
	{
		vm->RegisterFunction("SetPlayerVoiceVolume", "DBVOTweaks", SetPlayerVoiceVolume);
		SKSE::log::info("DBVOTweaks.SetPlayerVoiceVolume native registered");
		return true;
	}

	// Trampoline branch hook on the NON-virtual Actor::SpeakSoundFunction (the one the console's
	// Player.SpeakSound dispatches into — it fills a_handle and STARTS it synchronously inside the
	// call). We call the original FIRST so the engine builds + plays the handle (lip-sync intact),
	// then — only for the player's own DBVO line — scale the live handle's volume. a4–a14 are
	// emotion/2D/lip/queue flags carried through untouched (widths per the TiltedEvolution id 37542
	// reference). We never retain a_handle past the call; the engine owns it.
	struct SpeakSoundHook
	{
		static bool thunk(RE::Actor* a_this, const char* a_path, RE::BSSoundHandle* a_handle,
			std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
			std::uint64_t a7, std::uint64_t a8, std::uint64_t a9,
			bool a10, std::uint64_t a11, bool a12, bool a13, bool a14)
		{
			const bool r = func(a_this, a_path, a_handle, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14);
			if (a_this && a_this->IsPlayerRef() && a_path && a_handle && a_handle->IsValid() && is_dbvo_path(a_path)) {
				const float vol = g_dbvoVolume.load();
				a_handle->SetVolume(vol);
				// Bring-up trace, throttled to the first few player+DBVO hits so it can't flood the
				// log. A later task quiets/removes this; steady state is silent.
				static std::atomic<int> s_logged{ 0 };
				if (s_logged.load() < 5) {
					++s_logged;
					SKSE::log::info("DBVO hit: path='{}' volume={:.2f}", a_path, vol);
				}
			}
			return r;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// Install the speak-sound branch hook at load (matching the sibling mods' at-load InstallHooks
	// idiom). Addressed ONLY via Address Library id (SE 36541 / AE 37542) — no hardcoded offset, so
	// one NG-built DLL resolves the right target per runtime.
	void InstallHooks()
	{
		REL::Relocation<std::uintptr_t> target{ REL::RelocationID(36541, 37542) };
		SKSE::AllocTrampoline(14);
		SpeakSoundHook::func = SKSE::GetTrampoline().write_branch<5>(target.address(), SpeakSoundHook::thunk);
		SKSE::log::info("DBVODialogueTweaks: speak-sound trampoline hook installed");
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

	InstallHooks();

	if (auto* papyrus = SKSE::GetPapyrusInterface()) {
		if (!papyrus->Register(RegisterFuncs)) {
			SKSE::log::error("DBVODialogueTweaks: Papyrus Register returned false");
		}
	} else {
		SKSE::log::error("DBVODialogueTweaks: Papyrus interface is null");
	}

	return true;
}
