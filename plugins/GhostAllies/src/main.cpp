#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <atomic>
#include <cstdint>

namespace
{
	// Send spdlog (what SKSE::log::* uses) to <My Games>/SKSE/GhostAllies.log so the
	// plugin leaves a visible trace when the game loads it.
	void SetupLog()
	{
		auto logDir = SKSE::log::log_directory();
		if (!logDir) {
			return;
		}
		auto path = *logDir / "GhostAllies.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
		auto logger = std::make_shared<spdlog::logger>("global", std::move(sink));
		logger->set_level(spdlog::level::info);
		logger->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(logger));
	}

	// --- Collision-filter probe hook ----------------------------------------
	//
	// Hooks the engine's per-pair collision-filter decision. Havok asks this for
	// every pair of physics bodies, every frame, to decide whether they may
	// collide; it returns a bool (true = collide, false = ignore). This is the
	// same function HIGGS calls bhkCollisionFilter::CompareFilterInfo
	// (adamhynek/higgs, src/hooks.cpp, hardcoded SE 1.5.97 address 0xE2BA10,
	// hooked via Write6Branch) — but resolved version-independently for AE here.
	//
	// We do NOT branch-hook the function entry. Instead we patch one of its
	// CALL SITES via the SKSE trampoline (write_call<5>), exactly as the
	// CommonLibSSE-NG mod Precision does (ersh1/Precision, src/Hooks.h /
	// Hooks.cpp, HavokHooks). Precision patches all seven call sites; for this
	// behavior-neutral probe we hook the primary one:
	//   hkpCollidableCollidableFilter::isCollisionEnabled
	//     RELOCATION_ID(76676, 78548)  // SE 0xDD6780, AE 0xE17640
	//   call-site offset +0x17 (same for SE and AE), from Precision's
	//     collisionFilterHook2 / _bhkCollisionFilter_CompareFilterInfo2.
	//
	// ABI: the engine target is fastcall
	//   bool (*)(RE::bhkCollisionFilter* a_this, std::uint32_t a_filterInfoA,
	//            std::uint32_t a_filterInfoB)
	// confirmed against RE/H/hkpCollidableCollidableFilter.h
	// (IsCollisionEnabled, the per-pair filter) and Precision's
	// bhkCollisionFilter_CompareFilterInfoN thunk signatures, which all read
	// (RE::bhkCollisionFilter*, uint32_t, uint32_t) -> bool.
	//
	// THIS TASK: behavior-neutral. Always call the original and return its
	// result unchanged; only emit throttled probe logging. A later task adds the
	// real pass-through decision.
	// Readable names for the COL_LAYER values we care about (RE::COL_LAYER).
	// Arrows/bolts in flight are kProjectile(6); spells kSpell(7); an NPC body is
	// kBiped(8) / kCharController(30) / kDeadBip(32) / kBipedNoCC(33). The rest are
	// labelled where common so the probe log reads at a glance.
	const char* LayerName(std::uint32_t a_layer)
	{
		switch (a_layer) {
		case 1:  return "static";
		case 4:  return "clutter";
		case 5:  return "weapon";
		case 6:  return "PROJECTILE";
		case 7:  return "SPELL";
		case 8:  return "biped";
		case 13: return "terrain";
		case 17: return "ground";
		case 30: return "charController";
		case 32: return "deadBip";
		case 33: return "bipedNoCC";
		default: return "?";
		}
	}

	struct CompareFilterInfoHook
	{
		static bool thunk(RE::bhkCollisionFilter* a_this, std::uint32_t a_filterInfoA, std::uint32_t a_filterInfoB)
		{
			const bool result = func(a_this, a_filterInfoA, a_filterInfoB);

			// filterInfo layout: low 7 bits = collision layer, high 16 bits = system group.
			const std::uint32_t layerA = a_filterInfoA & 0x7F;
			const std::uint32_t layerB = a_filterInfoB & 0x7F;

			// Only log pairs that involve a PROJECTILE(6) or SPELL(7) layer — i.e. the
			// collisions this plugin will eventually intercept. The naive "first N of
			// anything" throttle burned out on ground geometry at load before any arrow
			// flew; gating on the layer skips that spam and captures real shots.
			// Projectile pairs are infrequent, but cap anyway so a stuck arrow can't flood.
			const bool interesting = layerA == 6 || layerA == 7 || layerB == 6 || layerB == 7;
			if (interesting) {
				static std::atomic<std::uint32_t> probeCount{ 0 };
				constexpr std::uint32_t            kProbeLimit = 200;
				const std::uint32_t                n = probeCount.fetch_add(1, std::memory_order_relaxed);
				if (n < kProbeLimit) {
					SKSE::log::info(
						"probe[{:>3}] A(layer={:>2} {} group={}) vs B(layer={:>2} {} group={}) -> {}",
						n, layerA, LayerName(layerA), a_filterInfoA >> 16,
						layerB, LayerName(layerB), a_filterInfoB >> 16,
						result ? "collide" : "ignore");
				}
			}

			return result;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	void InstallHooks()
	{
		// hkpCollidableCollidableFilter::isCollisionEnabled — the per-pair filter.
		// SE 0xDD6780 / AE 0xE17640; call site at +0x17 (same offset both runtimes).
		// Source: ersh1/Precision src/Hooks.h, collisionFilterHook2.
		REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(76676, 78548) };
		const std::size_t               callSiteOffset = REL::Relocate<std::size_t>(0x17, 0x17);

		CompareFilterInfoHook::func =
			SKSE::GetTrampoline().write_call<5>(target.address() + callSiteOffset, CompareFilterInfoHook::thunk);

		SKSE::log::info(
			"GhostAllies: hooked bhkCollisionFilter::CompareFilterInfo call site ({}, {:#x}+{:#x})",
			REL::Module::IsAE() ? "AE id 78548" : "SE id 76676",
			target.address(),
			callSiteOffset);
	}
}

// Declarative SKSE plugin metadata (CommonLibSSE-NG). Exported as
// SKSEPlugin_Version + SKSEPlugin_Query so SKSE recognises and loads the DLL.
SKSEPluginInfo(
	.Version = REL::Version{ 0, 2, 1 },
	.Name = "GhostAllies",
	.Author = "mase",
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

// Entry point SKSE calls after loading the plugin.
SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();
	SKSE::Init(a_skse);
	SKSE::log::info("GhostAllies {} loaded", REL::Version{ 0, 2, 1 }.string());

	// Trampoline must be allocated before any write_call/write_branch. One 5-byte
	// call patch needs little space; 64 bytes is comfortable headroom.
	SKSE::AllocTrampoline(64);
	InstallHooks();

	return true;
}
