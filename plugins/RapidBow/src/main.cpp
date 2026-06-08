#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace
{
	// Send spdlog (what SKSE::log::* uses) to <My Games>/SKSE/RapidBow.log so the
	// plugin leaves a visible trace when the game loads it.
	void SetupLog()
	{
		auto logDir = SKSE::log::log_directory();
		if (!logDir) {
			return;
		}
		auto path = *logDir / "RapidBow.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
		auto logger = std::make_shared<spdlog::logger>("global", std::move(sink));
		logger->set_level(spdlog::level::info);
		logger->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(logger));
	}

	// Force full bow/crossbow charge for the player.
	//
	// The engine never exposes a clean "current draw" float; instead it bakes the
	// draw-time fraction into the launched arrow's `power` (PROJECTILE_RUNTIME_DATA,
	// 0.0–1.0). Both arrow *speed* (via Projectile::GetSpeed -> GetPowerSpeedMult)
	// and impact *damage* read that field. GetPowerSpeedMult is virtual, so we hook
	// the ArrowProjectile vtable rather than detouring Projectile::Launch (CommonLib's
	// trampoline only redirects existing call sites, not raw function entries).
	//
	// On each call for a player-fired arrow we clamp `power` up to 1.0, then defer to
	// the original — which recomputes the genuine full-power speed multiplier from the
	// now-full charge. So a quick tap fires a fully-drawn, full-speed, full-damage shot.
	struct PowerSpeedHook
	{
		static float thunk(RE::Projectile* a_this)
		{
			auto& runtime = a_this->GetProjectileRuntimeData();
			auto  shooter = runtime.shooter.get();
			if (shooter && shooter->IsPlayerRef() && runtime.power < 1.0f) {
				runtime.power = 1.0f;
				if (!logged) {
					logged = true;
					SKSE::log::info("RapidBow: clamped a player arrow to full charge");
				}
			}
			return func(a_this);
		}

		static inline REL::Relocation<decltype(thunk)> func;
		static inline bool                             logged = false;
	};

	void InstallHooks()
	{
		// AE (1.6.x) vtable slot for GetPowerSpeedMult is 0xB0 (SE is 0xAF); see
		// CommonLibSSE-NG Projectile::GetPowerSpeedMult -> RelocateVirtual(0xAF, 0xB0).
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_ArrowProjectile[0] };
		PowerSpeedHook::func = vtbl.write_vfunc(0xB0, PowerSpeedHook::thunk);
		SKSE::log::info("RapidBow: hooked ArrowProjectile::GetPowerSpeedMult (AE vtable 0xB0)");
	}
}

// Declarative SKSE plugin metadata (CommonLibSSE-NG). Exported as
// SKSEPlugin_Version + SKSEPlugin_Query so SKSE recognises and loads the DLL.
SKSEPluginInfo(
	.Version = REL::Version{ 1, 1, 0 },
	.Name = "RapidBow",
	.Author = "mase",
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

// Entry point SKSE calls after loading the plugin.
SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();
	SKSE::Init(a_skse);
	SKSE::log::info("RapidBow {} loaded — forcing full bow charge for the player",
		REL::Version{ 1, 1, 0 }.string());
	InstallHooks();
	return true;
}
