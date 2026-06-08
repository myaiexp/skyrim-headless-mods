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
	// given projectile can only be made to ignore ONE actor via this read-only stamp —
	// the nearest teammate. (v2 Task 3 lifts this to the whole party via a shared group.)
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

	// --- Ghost-allies stamp -------------------------------------------------------
	//
	// Projectiles don't collide as broadphase body pairs; each frame Projectile::UpdateImpl
	// sweeps the projectile's own bhkSimpleShapePhantom (PROJECTILE_RUNTIME_DATA.unk0E0,
	// offset 0x0E0) via a shape linear-cast. What that cast hits is governed by the phantom
	// collidable's broadPhaseHandle.collisionFilterInfo. Havok's group filter skips bodies
	// sharing a non-zero systemGroup (top 16 bits) — which is exactly how a projectile
	// already ignores its own shooter. Copying a follower's systemGroup onto the projectile's
	// phantom makes it ignore that follower too: it passes through and keeps flying to the
	// enemy behind, and the follower takes no hit.
	//
	// This is read-only — it only reads the follower's existing group and never modifies the
	// follower. Works for arrows and every spell-projectile subclass alike (the phantom and
	// shooter live on the base Projectile), so one routine serves them all.
	//
	// Per-projectile idempotence: we only stamp when the phantom's top-16 group bits don't
	// already equal the follower's group. A fresh projectile may already carry its shooter's
	// group (that's how it ignores the shooter), so we guard against the follower group rather
	// than against 0 — guarding on ==0 could mean we never stamp at all. Overwriting also drops
	// shooter-ignore, which is fine (the player is behind the shot).
	void StampProjectilePhantom(RE::Projectile* a_proj, const char* a_label)
	{
		auto& runtime = a_proj->GetProjectileRuntimeData();
		auto  shooter = runtime.shooter.get();
		if (!shooter || !shooter->IsPlayerRef() || !runtime.unk0E0) {
			return;
		}
		// Reach the underlying hkpShapePhantom's collidable filter info:
		//   bhkSimpleShapePhantom*  unk0E0                              (0x0E0, Projectile.h)
		//     ->referencedObject (hkRefPtr<hkReferencedObject>)         (bhkRefObject, 0x10)
		//       .get() -> hkpShapePhantom*                              (hkpWorldObject subtype)
		//         ->collidable (hkpLinkedCollidable : hkpCollidable)   (hkpWorldObject, 0x20)
		//           .broadPhaseHandle (hkpTypedBroadPhaseHandle)       (hkpCollidable, 0x24)
		//             .collisionFilterInfo (std::uint32_t)             (handle +0x8)
		//
		// CommonLibSSE-NG only forward-declares bhkSimpleShapePhantom (no full def), so we
		// reinterpret_cast unk0E0 to its complete base bhkShapePhantom to reach the inherited
		// referencedObject. Single inheritance, base at offset 0 → safe.
		auto* bhkPhantom = reinterpret_cast<RE::bhkShapePhantom*>(runtime.unk0E0);
		auto* phantom = static_cast<RE::hkpShapePhantom*>(bhkPhantom->referencedObject.get());
		if (!phantom) {
			return;
		}
		std::uint32_t& filterInfo = phantom->collidable.broadPhaseHandle.collisionFilterInfo;

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		auto* follower = FindNearestFollower(player);
		if (!follower) {
			return;
		}
		std::uint32_t followerInfo = 0;
		follower->GetCollisionFilterInfo(followerInfo);
		const std::uint32_t group = followerInfo >> 16;
		if (group != 0 && (filterInfo >> 16) != group) {
			filterInfo &= 0x0000FFFF;
			filterInfo |= (group << 16);
			SKSE::log::info("stamped player {} -> follower {} ({:08X}) group {}",
				a_label, follower->GetName(), follower->GetFormID(), group);
		}
	}

	// --- Unified per-subclass UpdateImpl hook ------------------------------------
	//
	// Every projectile subclass overrides Projectile::UpdateImpl at the same vtable slot
	// (0xAB), and Projectile is each one's first base, so T::VTABLE[0] is the right vtable.
	// We hook that slot on each subclass, all routing to the one shared stamp above. Each
	// subclass carries its OWN original UpdateImpl, so the original must be stored per
	// subclass — the template gives each instantiation its own static `func` (a single
	// shared Relocation, as in v1's arrow-only hook, would call the wrong original).
	//
	// UpdateImpl fires every frame per projectile; the stamp's idempotence guard makes the
	// steady-state cost one compare + the original call after the first stamp.
	template <class T>
	struct StampHook
	{
		static inline const char* label = "projectile";

		static void thunk(RE::Projectile* a_this, float a_delta)
		{
			StampProjectilePhantom(a_this, label);
			func(a_this, a_delta);  // this subclass's original UpdateImpl
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	template <class T>
	void InstallStamp(const char* a_label)
	{
		StampHook<T>::label = a_label;
		REL::Relocation<std::uintptr_t> vtbl{ T::VTABLE[0] };
		StampHook<T>::func = vtbl.write_vfunc(0xAB, StampHook<T>::thunk);
	}

	void InstallHooks()
	{
		// Task 1: arrows only, via the unified UpdateImpl hook (replaces v1's
		// GetPowerSpeedMult 0xB0 hook). Task 2 adds the spell-projectile subclasses.
		InstallStamp<RE::ArrowProjectile>("arrow");
		SKSE::log::info("GhostAllies: hooked ArrowProjectile::UpdateImpl (vtable 0xAB) for systemGroup stamp");
	}
}

// Declarative SKSE plugin metadata (CommonLibSSE-NG). Exported as
// SKSEPlugin_Version + SKSEPlugin_Query so SKSE recognises and loads the DLL.
SKSEPluginInfo(
	.Version = REL::Version{ 0, 5, 0 },
	.Name = "GhostAllies",
	.Author = "mase",
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

// Entry point SKSE calls after loading the plugin.
SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();
	SKSE::Init(a_skse);
	SKSE::log::info("GhostAllies {} loaded", REL::Version{ 0, 5, 0 }.string());

	InstallHooks();

	return true;
}
