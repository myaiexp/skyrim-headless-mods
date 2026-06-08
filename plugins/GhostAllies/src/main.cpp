#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <cstdint>
#include <unordered_map>

namespace
{
	// Reserved 16-bit "ghost" systemGroup. Havok's group filter skips collision between two
	// bodies that share the same non-zero systemGroup (top 16 bits of collisionFilterInfo) —
	// the same rule that makes a projectile ignore its own shooter. We stamp this single shared
	// value onto every player teammate's char-controller collidable AND onto the player's shot
	// projectile, so the projectile phases through the entire party at once.
	//
	// Value choice: the 16-bit space is 0..0xFFFF. The engine assigns per-object systemGroups
	// incrementally from low numbers as ragdolls/characters spawn; over a play session this
	// counter stays in the low thousands at most and never climbs near the top of the range.
	// 0xFEED (65261) sits just below the 0xFFFF ceiling, far above any value the engine hands
	// out, so it won't clash with an actor's engine-assigned group. The enroll log line below
	// records each teammate's *original* group, giving an in-game no-clash audit trail.
	constexpr std::uint32_t kGhostGroup = 0xFEED;

	// bhkCharacterController vtable slot 0x09 is the engine's own SetCollisionFilterInfo(uint32).
	// CommonLibSSE-NG only labels it Unk_09 (B/bhkCharacterController.h), but two independent
	// Skyrim SE reverse-engineering sources both place a writable filter setter there, directly
	// after GetCollisionFilterInfo at slot 0x08 which CommonLib *does* name:
	//   - adamhynek/activeragdoll  include/RE/havok_behavior.h
	//   - adamhynek/higgs          include/RE/havok.h
	// Both: `virtual void SetCollisionFilterInfo(std::uint32_t filterInfo) = 0; // 09`.
	// This setter is what propagates the filter into the underlying Havok proxy/rigid-body
	// collidable, so it's the correct write path (cleaner and safer than poking the collidable
	// memory by raw offset, which is unk-padded in the headers). We invoke it through the live
	// vtable rather than redeclaring the class.
	constexpr std::size_t kCharCtrlSetFilterInfoSlot = 0x09;

	void CharController_SetCollisionFilterInfo(RE::bhkCharacterController* a_ctrl, std::uint32_t a_filterInfo)
	{
		auto* vtbl = *reinterpret_cast<void***>(a_ctrl);
		auto  fn = reinterpret_cast<void (*)(RE::bhkCharacterController*, std::uint32_t)>(vtbl[kCharCtrlSetFilterInfoSlot]);
		fn(a_ctrl, a_filterInfo);
	}

	// --- Whole-actor rigid-body systemGroup write --------------------------------
	//
	// The char-controller is only ONE of an actor's collision bodies. An actor also carries a
	// ragdoll/biped rigid body per bone plus rigid bodies for equipped shield/weapon — and the
	// continuous Flames stream is stopped by those (verified in-game: the stream is visibly
	// blocked by the follower's shield/body even with the char-controller ghosted). So to make
	// the WHOLE actor transparent to ghost-group projectiles we must stamp the ghost systemGroup
	// onto every one of its rigid bodies, not just the controller.
	//
	// Safe to use one shared group for all of them: every body on an actor normally shares the
	// SAME systemGroup already — that's the rule that stops an actor's own ragdoll parts from
	// colliding with each other. Rewriting only the top-16 systemGroup bits (low-16 layer/
	// subsystem bits preserved) keeps every body's relationship to itself and to the world
	// intact (the world isn't in the group); it only makes the actor phase through projectiles
	// that carry the same group. And because they share one group, restore needs just the one
	// saved original value applied back to every body.
	//
	// Reach a bhkRigidBody's Havok collidable filter the same way StampProjectilePhantom reaches
	// the phantom's: NiAVObject::GetCollisionObject() -> bhkNiCollisionObject::body (the
	// bhkWorldObject) -> AsBhkRigidBody() (NiObject vfunc 0x15) -> referencedObject is the
	// underlying hkpRigidBody (a hkpWorldObject) -> GetCollidableRW()->broadPhaseHandle
	// .collisionFilterInfo. This mirrors powerof3/PapyrusExtenderSSE's actor-collision walk
	// (it traverses Get3D() and edits each attached bhkRigidBody), but we write ONLY the
	// systemGroup rather than using the engine's SetCollisionLayerAndGroup helper, which would
	// also rewrite the collision layer — we must leave the layer untouched.
	void RigidBody_SetSystemGroup(RE::bhkRigidBody* a_body, std::uint16_t a_group)
	{
		auto* hkBody = static_cast<RE::hkpRigidBody*>(a_body->referencedObject.get());
		if (!hkBody) {
			return;
		}
		auto* collidable = hkBody->GetCollidableRW();
		if (!collidable) {
			return;
		}
		std::uint32_t& info = collidable->broadPhaseHandle.collisionFilterInfo;
		if ((info >> 16) == a_group) {
			return;  // idempotent: already at this group
		}
		info = (info & 0x0000FFFF) | (static_cast<std::uint32_t>(a_group) << 16);
	}

	// Recursively walk an NiAVObject tree, writing a_group onto every bhkRigidBody's systemGroup.
	void WriteSubtreeSystemGroup(RE::NiAVObject* a_obj, std::uint16_t a_group)
	{
		if (!a_obj) {
			return;
		}
		if (auto* col = a_obj->GetCollisionObject()) {
			if (auto* body = col->body ? col->body->AsBhkRigidBody() : nullptr) {
				RigidBody_SetSystemGroup(body, a_group);
			}
		}
		if (auto* node = a_obj->AsNode()) {
			for (auto& child : node->GetChildren()) {
				WriteSubtreeSystemGroup(child.get(), a_group);
			}
		}
	}

	// Stamp a_group onto every rigid body in the actor's loaded 3D. No-op (skipped, retried next
	// reconcile) if the actor isn't loaded — Get3D() null. The char-controller is handled
	// separately by the caller; this covers the ragdoll/biped + equipment bodies.
	void WriteActorBodiesSystemGroup(RE::Actor* a_actor, std::uint16_t a_group)
	{
		WriteSubtreeSystemGroup(a_actor->Get3D(), a_group);
	}
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

	// --- Whole-party ghost-group membership --------------------------------------
	//
	// FormID -> the teammate's ORIGINAL systemGroup, saved when we enrolled it into the ghost
	// group. We must always be able to restore an actor's real group when it stops being a
	// teammate, so we never strand an actor in kGhostGroup. Enroll/restore is done lazily from
	// the stamp path (no SKSE event hooking): each player shot reconciles the map against the
	// current teammate set.
	std::unordered_map<RE::FormID, std::uint32_t> g_enrolled;

	// Reconcile g_enrolled with the player's current teammate set:
	//   - new teammates: save their original group, write kGhostGroup, log enrollment;
	//   - actors no longer teammates (or whose handle is gone): restore their saved group, erase.
	// Idempotent: an actor already at kGhostGroup is left untouched; only the top-16 systemGroup
	// bits are changed, the low-16 layer/subsystem bits are preserved.
	void MaintainGhostGroup(RE::PlayerCharacter* a_player)
	{
		auto* lists = RE::ProcessLists::GetSingleton();
		if (!lists) {
			return;
		}

		// Pass 1 — enroll current teammates not yet in the map.
		for (auto& handle : lists->highActorHandles) {
			auto actor = handle.get();
			if (!actor || actor.get() == a_player) {
				continue;
			}
			if (!actor->IsPlayerTeammate()) {
				continue;
			}
			auto* ctrl = actor->GetCharController();
			if (!ctrl) {
				continue;
			}
			std::uint32_t info = 0;
			ctrl->GetCollisionFilterInfo(info);
			const std::uint32_t group = info >> 16;

			// Always (re)ghost the ragdoll/biped + equipment rigid bodies for a current teammate,
			// even when the char-controller is already ghosted from a prior frame. This is what
			// stops a continuous stream (Flames) the char-controller alone doesn't cover, and it
			// must run every reconcile because those bodies may load LATE (Get3D() null on the
			// enroll frame) or be replaced on an equipment change. The per-body write is
			// idempotent (skips a body already at the ghost group), so re-running is cheap; it's a
			// silent no-op while the 3D isn't loaded, retried on the next reconcile.
			WriteActorBodiesSystemGroup(actor.get(), static_cast<std::uint16_t>(kGhostGroup));

			if (group == kGhostGroup) {
				continue;  // char-controller already ghosted (enrolled on a prior frame)
			}
			const RE::FormID id = actor->GetFormID();
			if (g_enrolled.find(id) == g_enrolled.end()) {
				g_enrolled.emplace(id, group);
			}
			const std::uint32_t ghosted = (info & 0x0000FFFF) | (kGhostGroup << 16);
			CharController_SetCollisionFilterInfo(ctrl, ghosted);
			SKSE::log::info("enrolled teammate {} ({:08X}) orig group {}",
				actor->GetName(), id, group);
		}

		// Pass 2 — restore + drop anyone in the map who is no longer a current teammate.
		for (auto it = g_enrolled.begin(); it != g_enrolled.end();) {
			const RE::FormID id = it->first;
			auto*            form = RE::TESForm::LookupByID(id);
			auto*            actor = form ? form->As<RE::Actor>() : nullptr;
			const bool       stillTeammate = actor && actor->IsPlayerTeammate();
			if (stillTeammate) {
				++it;
				continue;
			}
			if (actor) {
				if (auto* ctrl = actor->GetCharController()) {
					std::uint32_t info = 0;
					ctrl->GetCollisionFilterInfo(info);
					const std::uint32_t restored = (info & 0x0000FFFF) | (it->second << 16);
					CharController_SetCollisionFilterInfo(ctrl, restored);
				}
				// Restore every ragdoll/biped + equipment rigid body to the one saved original
				// group (all of an actor's bodies share a single systemGroup, so the one saved
				// value is correct for all of them). No-op if the actor's 3D isn't loaded.
				WriteActorBodiesSystemGroup(actor, static_cast<std::uint16_t>(it->second));
			}
			SKSE::log::info("restored teammate {:08X}", id);
			it = g_enrolled.erase(it);
		}
	}

	// --- Ghost-allies stamp -------------------------------------------------------
	//
	// Projectiles don't collide as broadphase body pairs; each frame Projectile::UpdateImpl
	// sweeps the projectile's own bhkSimpleShapePhantom (PROJECTILE_RUNTIME_DATA.unk0E0,
	// offset 0x0E0) via a shape linear-cast. What that cast hits is governed by the phantom
	// collidable's broadPhaseHandle.collisionFilterInfo. Havok's group filter skips bodies
	// sharing a non-zero systemGroup (top 16 bits) — which is exactly how a projectile
	// already ignores its own shooter. We put the WHOLE party and the projectile into one
	// shared kGhostGroup (MaintainGhostGroup writes kGhostGroup onto each teammate's
	// char-controller collidable); the projectile then ignores every enrolled teammate at once,
	// passing through them and flying on to the enemy behind, and no teammate takes a hit.
	//
	// Works for arrows and every spell-projectile subclass alike (the phantom and shooter live
	// on the base Projectile), so one routine serves them all.
	//
	// Per-projectile idempotence: we only stamp when the phantom's top-16 group bits aren't
	// already kGhostGroup. A fresh projectile may carry its shooter's group (that's how it
	// ignores the shooter); overwriting drops shooter-ignore, which is fine (the player is
	// behind the shot).
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
		// Do the work only on a projectile's FIRST stamp: once its phantom carries the ghost
		// group, the guard skips it on every later frame of its flight. Reconciling teammates
		// here (inside the guard) rather than unconditionally means the full high-actor scan +
		// enroll runs once per shot, not every frame for every projectile in flight — important
		// with the auto-fire bow spamming projectiles.
		if ((filterInfo >> 16) != kGhostGroup) {
			// Ensure every current teammate is enrolled in the ghost group (and any ex-teammate
			// restored) before stamping the projectile with that same group.
			MaintainGhostGroup(player);
			filterInfo &= 0x0000FFFF;
			filterInfo |= (kGhostGroup << 16);
			SKSE::log::info("stamped player {} -> ghost group", a_label);
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

	// --- Hostile-magic refusal for player teammates (continuous streams etc.) ----
	//
	// The systemGroup stamp above gates broadphase body-pair collision, which makes DISCRETE
	// projectiles (arrows, aimed missiles) phase through teammates. But the player's CONTINUOUS
	// concentration spell Flames (FlameProjectile) still DAMAGED the follower even with the
	// stamp on its phantom — and an earlier AddImpact (slot 0xBD) skip hook PROVED in-game it
	// fired and skipped every flame impact on the follower, yet the follower still took damage.
	// Conclusion: Flames damage does NOT flow through Projectile::AddImpact. It is applied as a
	// hostile magic effect through the magic-target system: MagicTarget::AddTarget.
	//
	// So instead we hook MagicTarget::AddTarget (the single entry point every effect application
	// flows through) and refuse the application when ALL of:
	//   - the target actor is a player teammate (IsPlayerTeammate), AND
	//   - the caster is the player (TESObjectREFR* AddTargetData::caster, IsPlayerRef), AND
	//   - the effect being added is hostile/detrimental (Effect::IsHostile on AddTargetData::effect,
	//     falling back to the parent MagicItem's IsHostile).
	// On refusal we return AddTarget's "not added" value (false) without calling the original, so
	// the effect is dropped — no damage on the follower. Every other case (non-teammate target,
	// non-player caster, beneficial effect) calls the original unchanged, so enemies still take
	// damage and the player can still heal/buff their own followers.
	//
	// Hook point — vtable, MagicTarget base of Character:
	//   Actor's polymorphic bases in order are TESObjectREFR (which itself contributes 4 vtable
	//   sub-objects: TESForm, BSHandleRefObject, BSTEventSink<BSAnimationGraphEvent>,
	//   IAnimationGraphManagerHolder), then MagicTarget, then ActorValueOwner, etc. So in
	//   CommonLibSSE-NG's combined VTABLE_Character / VTABLE_Actor arrays the MagicTarget base
	//   vtable is array index 4 (0-based). AddTarget is slot 1 within MagicTarget's own vtable
	//   (M/MagicTarget.h: "virtual bool AddTarget(AddTargetData&); // 01"). This matches the
	//   established NOFF (feelixs/noff-skse) plugin, which hooks the same MagicTarget::AddTarget
	//   at segment index 4 / vfunc 1 (slot confirmed via Ghidra there) for AE 1.6.x. The
	//   REL::ID/address resolution is handled by CommonLib's VTABLE_Character VariantID for the
	//   running runtime (no hardcoded 1.5.97 address). Followers are Character instances, so
	//   hooking VTABLE_Character[4] covers them; the player is never a teammate so PlayerCharacter
	//   need not be hooked (and the caster==player gate plus IsPlayerTeammate target gate make a
	//   stray non-Character teammate harmless — it just isn't covered, never wrongly refused).
	constexpr std::size_t kMagicTargetVtableIdx = 4;
	constexpr std::size_t kAddTargetVfuncSlot = 1;

	// Teammates we've already logged a refusal for — a concentration spell calls AddTarget every
	// tick, so we log the first refusal per teammate and stay quiet after (no per-tick flooding).
	std::unordered_map<RE::FormID, std::uint32_t> g_addTargetSeen;

	struct AddTargetHook
	{
		static bool thunk(RE::MagicTarget* a_this, RE::MagicTarget::AddTargetData& a_data)
		{
			auto* target = a_this ? a_this->GetTargetStatsObject() : nullptr;
			auto* actor = target ? target->As<RE::Actor>() : nullptr;
			if (actor && actor->IsPlayerTeammate()) {
				const bool playerCast = a_data.caster && a_data.caster->IsPlayerRef();
				// Prefer the specific effect being added; fall back to the parent spell.
				const bool hostile =
					(a_data.effect && a_data.effect->IsHostile()) ||
					(a_data.magicItem && a_data.magicItem->IsHostile());
				if (playerCast && hostile) {
					// Throttle: a concentration spell calls AddTarget every tick, so log only
					// the first refusal per teammate (enough to confirm the hook is working).
					if (g_addTargetSeen.emplace(actor->GetFormID(), 0).second) {
						SKSE::log::info("AddTarget refusing player hostile effects on teammate {} ({:08X})",
							actor->GetName(), actor->GetFormID());
					}
					return false;  // drop the effect: no hostile magic applied to the teammate
				}
			}
			return func(a_this, a_data);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	void InstallAddTargetRefusal()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_Character[kMagicTargetVtableIdx] };
		AddTargetHook::func = vtbl.write_vfunc(kAddTargetVfuncSlot, AddTargetHook::thunk);
	}

	void InstallHooks()
	{
		// One unified UpdateImpl (0xAB) stamp hook per in-scope projectile subclass, all
		// routing to StampProjectilePhantom. Arrows (was v1's GetPowerSpeedMult hook) plus
		// the aimed spell projectiles. Runes (GrenadeProjectile) and wall spells
		// (BarrierProjectile) are deliberately NOT hooked — out of scope (docs/ideas.md).
		// Flame/beam/cone are continuous-collision types: the stamp is applied uniformly,
		// but whether each actually phases via the group route is a per-type in-game
		// question (the per-type label below makes the log distinguish them).
		InstallStamp<RE::ArrowProjectile>("arrow");
		InstallStamp<RE::MissileProjectile>("missile");
		InstallStamp<RE::FlameProjectile>("flame");
		InstallStamp<RE::BeamProjectile>("beam");
		InstallStamp<RE::ConeProjectile>("cone");
		SKSE::log::info("GhostAllies: hooked UpdateImpl (vtable 0xAB) on arrow/missile/flame/beam/cone for systemGroup stamp");

		// The stamp doesn't gate the player's continuous Flames damage (it flows through the
		// magic-target system, not AddImpact — proven in-game). Refuse player-shot hostile
		// magic effects on teammates at MagicTarget::AddTarget instead.
		InstallAddTargetRefusal();
		SKSE::log::info("GhostAllies: hooked MagicTarget::AddTarget (Character vtable idx 4, vfunc 1) to refuse player hostile effects on teammates");
	}
}

// Declarative SKSE plugin metadata (CommonLibSSE-NG). Exported as
// SKSEPlugin_Version + SKSEPlugin_Query so SKSE recognises and loads the DLL.
SKSEPluginInfo(
	.Version = REL::Version{ 0, 8, 0 },
	.Name = "GhostAllies",
	.Author = "mase",
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

// Entry point SKSE calls after loading the plugin.
SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();
	SKSE::Init(a_skse);
	SKSE::log::info("GhostAllies {} loaded", REL::Version{ 0, 8, 0 }.string());

	InstallHooks();

	return true;
}
