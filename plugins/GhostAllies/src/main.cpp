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

			// Assumed filterInfo layout: low 7 bits = collision layer, high 16 bits =
			// system group (the HIGGS/activeragdoll convention). We are NOT fully sure
			// this decode is right on 1.6.1170, so we also log the RAW filterInfo hex
			// below — if the layer names look wrong, the raw values let us re-derive the
			// true bit layout.
			const std::uint32_t layerA = a_filterInfoA & 0x7F;
			const std::uint32_t layerB = a_filterInfoB & 0x7F;

			// Capture any pair that (by the assumed decode) involves a weapon/projectile/
			// spell or an actor body — i.e. the collisions this plugin cares about. Gating
			// on a SET of dynamic layers (not just projectile) means that even if the
			// decode is slightly off, an arrow-vs-NPC event is still caught via the actor
			// side, and the raw hex reveals the projectile's real encoding. This skips the
			// dominant static-world pair spam that filled the previous naive throttle.
			auto isDynamic = [](std::uint32_t a_layer) {
				return a_layer == 5    // weapon
				    || a_layer == 6    // projectile
				    || a_layer == 7    // spell
				    || a_layer == 8    // biped
				    || a_layer == 30   // charController
				    || a_layer == 32   // deadBip
				    || a_layer == 33;  // bipedNoCC
			};
			if (isDynamic(layerA) || isDynamic(layerB)) {
				static std::atomic<std::uint32_t> probeCount{ 0 };
				constexpr std::uint32_t            kProbeLimit = 300;
				const std::uint32_t                n = probeCount.fetch_add(1, std::memory_order_relaxed);
				if (n < kProbeLimit) {
					SKSE::log::info(
						"probe[{:>3}] A(l={:>2} {:<14} g={} raw={:#010x}) vs B(l={:>2} {:<14} g={} raw={:#010x}) -> {}",
						n, layerA, LayerName(layerA), a_filterInfoA >> 16, a_filterInfoA,
						layerB, LayerName(layerB), a_filterInfoB >> 16, a_filterInfoB,
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
	.Version = REL::Version{ 0, 2, 2 },
	.Name = "GhostAllies",
	.Author = "mase",
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

// Entry point SKSE calls after loading the plugin.
SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();
	SKSE::Init(a_skse);
	SKSE::log::info("GhostAllies {} loaded", REL::Version{ 0, 2, 2 }.string());

	// Trampoline must be allocated before any write_call/write_branch. One 5-byte
	// call patch needs little space; 64 bytes is comfortable headroom.
	SKSE::AllocTrampoline(64);
	InstallHooks();

	return true;
}
