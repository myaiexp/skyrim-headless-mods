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

	// Readable names for the COL_LAYER values we care about (RE::COL_LAYER).
	// Arrows/bolts in flight are kProjectile(6); spells kSpell(7); an NPC body is
	// kBiped(8) / kCharController(30) / kDeadBip(32) / kBipedNoCC(33).
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

	// The collisions this plugin cares about: a weapon/projectile/spell or an actor
	// body. Used to skip the dominant static-world pair spam in the probe.
	bool IsDynamicLayer(std::uint32_t a_layer)
	{
		return a_layer == 5     // weapon
		    || a_layer == 6     // projectile
		    || a_layer == 7     // spell
		    || a_layer == 8     // biped
		    || a_layer == 30    // charController
		    || a_layer == 32    // deadBip
		    || a_layer == 33;   // bipedNoCC
	}

	// --- Collision-filter probe hooks ---------------------------------------
	//
	// The game's collision filter is the bhkCollisionFilter singleton. Via multiple
	// inheritance it implements several Havok filter interfaces, each a vtable on
	// VTABLE_bhkCollisionFilter:
	//   [1] hkpCollidableCollidableFilter::IsCollisionEnabled(collidableA, collidableB)
	//       — the per-pair filter for DISCRETE broadphase body pairs (slot 0x01).
	//   [4] hkpRayCollidableFilter::IsCollisionEnabled(rayInput, collidable)
	//       — the filter for RAYCAST / linear-cast queries (slot 0x01).
	//
	// We hook the VIRTUALS directly (version-independent; no address-library guess),
	// which catches every caller — unlike a single CompareFilterInfo call-site patch,
	// which (as the first probe proved) only saw the character/ragdoll path and never
	// any arrow. Hooking both interfaces in one go reveals which path fast arrows
	// actually take: if PROJECTILE(6) shows up under "cc" they are discrete pairs; if
	// it shows up under "ray" they are cast-based. The collidable gives us
	// GetCollisionLayer()/GetOwner() directly, richer than raw filterInfo.
	//
	// THIS TASK: behavior-neutral. Always call the original and return its result;
	// only emit throttled probe logging. A later task adds the pass-through decision.

	// [1] discrete collidable-vs-collidable pairs.
	struct CollidableFilterHook
	{
		static bool thunk(RE::hkpCollidableCollidableFilter* a_this, const RE::hkpCollidable* a_a, const RE::hkpCollidable* a_b)
		{
			const bool result = func(a_this, a_a, a_b);

			const std::uint32_t la = a_a ? static_cast<std::uint32_t>(a_a->GetCollisionLayer()) : 0xFF;
			const std::uint32_t lb = a_b ? static_cast<std::uint32_t>(a_b->GetCollisionLayer()) : 0xFF;
			if (IsDynamicLayer(la) || IsDynamicLayer(lb)) {
				static std::atomic<std::uint32_t> n{ 0 };
				const std::uint32_t               i = n.fetch_add(1, std::memory_order_relaxed);
				if (i < 200) {
					SKSE::log::info("cc [{:>3}] A(l={:>2} {:<14}) vs B(l={:>2} {:<14}) -> {}",
						i, la, LayerName(la), lb, LayerName(lb), result ? "collide" : "ignore");
				}
			}
			return result;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// [4] raycast / linear-cast queries. The ray itself carries the caster's layer
	// in rayInput.filterInfo (offset 0x24); the target is the collidable. A
	// projectile fired as a cast shows src layer = PROJECTILE even though the ray has
	// no collidable of its own — so decode both sides.
	struct RayFilterHook
	{
		static bool thunk(RE::hkpRayCollidableFilter* a_this, const RE::hkpWorldRayCastInput* a_rayInput, const RE::hkpCollidable* a_collidable)
		{
			const bool result = func(a_this, a_rayInput, a_collidable);

			const std::uint32_t srcLayer = a_rayInput ? (a_rayInput->filterInfo & 0x7F) : 0xFF;
			const std::uint32_t tgtLayer = a_collidable ? static_cast<std::uint32_t>(a_collidable->GetCollisionLayer()) : 0xFF;
			if (IsDynamicLayer(srcLayer) || IsDynamicLayer(tgtLayer)) {
				static std::atomic<std::uint32_t> n{ 0 };
				const std::uint32_t               i = n.fetch_add(1, std::memory_order_relaxed);
				if (i < 200) {
					SKSE::log::info("ray[{:>3}] src(l={:>2} {:<14}) -> target(l={:>2} {:<14}) -> {}",
						i, srcLayer, LayerName(srcLayer), tgtLayer, LayerName(tgtLayer), result ? "collide" : "ignore");
				}
			}
			return result;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	void InstallHooks()
	{
		// Slot 0x01 = IsCollisionEnabled on each filter interface (slot 0x00 is the
		// destructor). VTABLE_bhkCollisionFilter[1] / [4] are the collidable-collidable
		// and ray-collidable sub-object vtables of the bhkCollisionFilter singleton.
		REL::Relocation<std::uintptr_t> ccVtbl{ RE::VTABLE_bhkCollisionFilter[1] };
		CollidableFilterHook::func = ccVtbl.write_vfunc(0x1, CollidableFilterHook::thunk);

		REL::Relocation<std::uintptr_t> rayVtbl{ RE::VTABLE_bhkCollisionFilter[4] };
		RayFilterHook::func = rayVtbl.write_vfunc(0x1, RayFilterHook::thunk);

		SKSE::log::info("GhostAllies: hooked bhkCollisionFilter IsCollisionEnabled vtables [1] collidable + [4] ray");
	}
}

// Declarative SKSE plugin metadata (CommonLibSSE-NG). Exported as
// SKSEPlugin_Version + SKSEPlugin_Query so SKSE recognises and loads the DLL.
SKSEPluginInfo(
	.Version = REL::Version{ 0, 3, 0 },
	.Name = "GhostAllies",
	.Author = "mase",
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

// Entry point SKSE calls after loading the plugin.
SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();
	SKSE::Init(a_skse);
	SKSE::log::info("GhostAllies {} loaded", REL::Version{ 0, 3, 0 }.string());

	InstallHooks();

	return true;
}
