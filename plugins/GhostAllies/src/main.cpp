#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include <spdlog/sinks/basic_file_sink.h>

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

	// Find the player's teammate (follower) nearest to the player. Returns nullptr if
	// the player has no active follower. The systemGroup is a single 16-bit value, so a
	// given arrow can only be made to ignore ONE actor — v1 picks the nearest teammate.
	RE::Actor* FindNearestFollower(RE::Actor* a_player)
	{
		auto* lists = RE::ProcessLists::GetSingleton();
		if (!lists) {
			return nullptr;
		}
		const RE::NiPoint3 playerPos = a_player->GetPosition();
		RE::Actor*         best = nullptr;
		float              bestDist = 0.0f;
		for (auto& handle : lists->highActorHandles) {
			auto actor = handle.get();
			if (!actor || actor.get() == a_player) {
				continue;
			}
			if (!actor->IsPlayerTeammate()) {
				continue;
			}
			const float dist = actor->GetPosition().GetDistance(playerPos);
			if (!best || dist < bestDist) {
				best = actor.get();
				bestDist = dist;
			}
		}
		return best;
	}

	// --- Ghost-allies stamp hook -------------------------------------------------
	//
	// Arrows don't collide as broadphase body pairs; each frame Projectile::UpdateImpl
	// sweeps the projectile's own bhkSimpleShapePhantom (PROJECTILE_RUNTIME_DATA.unk0E0,
	// offset 0x0E0) via a shape linear-cast. What that cast hits is governed by the
	// phantom collidable's broadPhaseHandle.collisionFilterInfo. Havok's group filter
	// skips bodies sharing a non-zero systemGroup (top 16 bits) — which is exactly how an
	// arrow already ignores its own shooter. Copying a follower's systemGroup onto the
	// arrow's phantom makes the arrow ignore that follower too: it passes through and
	// keeps flying to the enemy behind, and the follower takes no hit.
	//
	// Hook point: ArrowProjectile::GetPowerSpeedMult (AE vtable slot 0xB0; SE 0xAF), the
	// same slot RapidBow uses. It fires once per arrow at launch with the shooter known,
	// so it's a clean place to stamp. We call the original and return it unchanged.
	//
	// Per-arrow idempotence: we only stamp when the phantom's systemGroup bits are still
	// zero (a fresh arrow's phantom carries no group). Once stamped, the group is
	// non-zero and we skip — no separate guard flag needed.
	struct StampHook
	{
		static float thunk(RE::Projectile* a_this)
		{
			auto& runtime = a_this->GetProjectileRuntimeData();
			auto  shooter = runtime.shooter.get();
			if (shooter && shooter->IsPlayerRef() && runtime.unk0E0) {
				// Reach the underlying hkpShapePhantom's collidable filter info:
				//   bhkSimpleShapePhantom*  unk0E0                              (0x0E0, Projectile.h)
				//     ->referencedObject (hkRefPtr<hkReferencedObject>)         (bhkRefObject, 0x10)
				//       .get() -> hkpShapePhantom*                              (hkpWorldObject subtype)
				//         ->collidable (hkpLinkedCollidable : hkpCollidable)   (hkpWorldObject, 0x20)
				//           .broadPhaseHandle (hkpTypedBroadPhaseHandle)       (hkpCollidable, 0x24)
				//             .collisionFilterInfo (std::uint32_t)             (handle +0x8)
				//
				// CommonLibSSE-NG only forward-declares bhkSimpleShapePhantom (no full def),
				// so we reinterpret_cast unk0E0 to its complete base bhkShapePhantom to reach
				// the inherited referencedObject. Single inheritance, base at offset 0 → safe.
				auto* bhkPhantom = reinterpret_cast<RE::bhkShapePhantom*>(runtime.unk0E0);
				auto* phantom = static_cast<RE::hkpShapePhantom*>(bhkPhantom->referencedObject.get());
				if (phantom) {
					std::uint32_t& filterInfo = phantom->collidable.broadPhaseHandle.collisionFilterInfo;
					if (auto* player = RE::PlayerCharacter::GetSingleton()) {
						if (auto* follower = FindNearestFollower(player)) {
							std::uint32_t followerInfo = 0;
							follower->GetCollisionFilterInfo(followerInfo);
							const std::uint32_t group = followerInfo >> 16;
							// Stamp once: skip if the phantom already carries the follower's
							// group. We compare against the follower group rather than 0, because
							// a launched arrow may already hold its shooter's systemGroup (that's
							// how it ignores the shooter) — guarding on ==0 could mean we never
							// stamp at all. Overwriting also drops shooter-ignore, which is fine
							// (the player is behind the bow).
							if (group != 0 && (filterInfo >> 16) != group) {
								filterInfo &= 0x0000FFFF;
								filterInfo |= (group << 16);
								SKSE::log::info("stamped player arrow -> follower {} ({:08X}) group {}",
									follower->GetName(), follower->GetFormID(), group);
							}
						}
					}
				}
			}
			return func(a_this);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	void InstallHooks()
	{
		// AE (1.6.x) vtable slot for GetPowerSpeedMult is 0xB0 (SE is 0xAF); see
		// CommonLibSSE-NG Projectile::GetPowerSpeedMult -> RelocateVirtual(0xAF, 0xB0).
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_ArrowProjectile[0] };
		StampHook::func = vtbl.write_vfunc(0xB0, StampHook::thunk);
		SKSE::log::info("GhostAllies: hooked ArrowProjectile::GetPowerSpeedMult (AE vtable 0xB0) for systemGroup stamp");
	}
}

// Declarative SKSE plugin metadata (CommonLibSSE-NG). Exported as
// SKSEPlugin_Version + SKSEPlugin_Query so SKSE recognises and loads the DLL.
SKSEPluginInfo(
	.Version = REL::Version{ 0, 4, 0 },
	.Name = "GhostAllies",
	.Author = "mase",
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

// Entry point SKSE calls after loading the plugin.
SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();
	SKSE::Init(a_skse);
	SKSE::log::info("GhostAllies {} loaded", REL::Version{ 0, 4, 0 }.string());

	InstallHooks();

	return true;
}
