#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <cstring>

namespace
{
	// Send spdlog (what SKSE::log::* uses) to <My Games>/SKSE/AutoFireBow.log so the
	// plugin leaves a visible trace that it loaded and hooked successfully.
	void SetupLog()
	{
		auto logDir = SKSE::log::log_directory();
		if (!logDir) {
			return;
		}
		auto path = *logDir / "AutoFireBow.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
		auto logger = std::make_shared<spdlog::logger>("global", std::move(sink));
		logger->set_level(spdlog::level::info);
		logger->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(logger));
	}

	// Per-draw-cycle guard so the loop auto-looses at most once per BowDrawn (reset at
	// nock). The loop keys off the engine's own BowDrawn event, so it's robust to any
	// bow speed / perk / enchantment — no draw-time assumptions.
	bool g_firedThisCycle = false;

	// Whether the attack control is currently held, tracked from raw input events (the
	// AttackBlockHandler held-flags didn't reflect this reliably).
	bool g_attackHeld = false;

	// Loop helpers (defined below; the animation-event sink drives them).
	bool AttackHeld() { return g_attackHeld; }
	void ScheduleRelease();
	void ScheduleRedraw();

	// Force full bow charge for the player. (Crossbow bolts are also ArrowProjectiles,
	// so they pass through this hook too — but a crossbow always fires at full draw, so
	// the clamp below is a no-op for them. The rapid-fire loop further down is bow-only:
	// it keys off the bow draw/release animation events, which crossbows don't emit.)
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
			if (shooter && shooter->IsPlayerRef()) {
				const float natural = runtime.power;  // 0x188: draw mult, drives speed (read live)
				// Per-arrow and self-idempotent: once `power` is clamped to 1.0,
				// `natural < 1.0f` is false on this arrow's later GetSpeed calls, so the
				// damage rescale cannot compound — no guard flag needed. Every player
				// arrow is boosted independently, so multishot/barrage arrows all land full.
				if (natural < 1.0f && natural > 0.0f) {
					runtime.power = 1.0f;                    // → full speed
					runtime.weaponDamage *= 1.0f / natural;  // 0x198: undo partial-draw scaling → full damage
				}
			}
			return func(a_this);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// Drives the rapid-fire loop off the player's bow animation graph: on bowDraw
	// (re-)arm the per-cycle fire guard; on the engine's own BowDrawn (genuine full
	// draw, any bow/perk/speed) auto-loose; on the resulting arrowRelease re-nock.
	class BowLoopSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
	{
	public:
		static BowLoopSink* GetSingleton()
		{
			static BowLoopSink singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(
			const RE::BSAnimationGraphEvent*               a_event,
			RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source) override
		{
			(void)a_source;
			const char* tag = a_event ? a_event->tag.c_str() : nullptr;
			if (!tag) {
				return RE::BSEventNotifyControl::kContinue;
			}
			if (std::strcmp(tag, "bowDraw") == 0) {
				// Nock start: re-arm the loop's per-cycle fire guard.
				g_firedThisCycle = false;
			} else if (std::strcmp(tag, "BowDrawn") == 0) {
				if (AttackHeld() && !g_firedThisCycle) {
					g_firedThisCycle = true;
					ScheduleRelease();  // loose at genuine full draw (hook forces full power+damage)
				}
			} else if (std::strcmp(tag, "arrowRelease") == 0) {
				// Continue the loop ONLY after our own auto-loose (g_firedThisCycle), never
				// after a manual shot — otherwise a quick re-tap during the follow-through
				// briefly reads as "held" and injects an unwanted draw (stuck-drawn bug).
				if (g_firedThisCycle && AttackHeld()) {
					ScheduleRedraw();  // re-nock for the next shot
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	// Tracks whether the attack control is held, from raw input. The bow's right-hand
	// attack uses "Right Attack/Block"; track left too for completeness.
	class AttackInputSink : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static AttackInputSink* GetSingleton()
		{
			static AttackInputSink singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(
			RE::InputEvent* const*                  a_event,
			RE::BSTEventSource<RE::InputEvent*>*    a_source) override
		{
			(void)a_source;
			if (!a_event) {
				return RE::BSEventNotifyControl::kContinue;
			}
			for (auto* e = *a_event; e; e = e->next) {
				if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
					continue;
				}
				auto* btn = e->AsButtonEvent();
				if (!btn) {
					continue;
				}
				const char* ue = btn->QUserEvent().c_str();
				if (ue && (std::strcmp(ue, "Right Attack/Block") == 0 || std::strcmp(ue, "Left Attack/Block") == 0)) {
					g_attackHeld = btn->IsPressed();
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	void RegisterBowLoop()
	{
		static bool registered = false;
		if (registered) {
			return;
		}
		if (auto* idm = RE::BSInputDeviceManager::GetSingleton()) {
			idm->AddEventSink(AttackInputSink::GetSingleton());
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (player && player->AddAnimationGraphEventSink(BowLoopSink::GetSingleton())) {
			registered = true;
			SKSE::log::info("AutoFireBow: bow rapid-fire loop registered on player");
		}
	}

	// --- Rapid-fire loop (event-driven; no persistent task) ------------------
	// While the attack button is held with a bow: on the engine's BowDrawn event
	// (genuine full draw, any bow/perk/speed) loose, then on the resulting
	// arrowRelease re-nock — repeating while held. Power+damage are forced full by
	// the hook above, so the graph-driven release lands full.
	//
	// Every action is a ONE-SHOT deferred task triggered by a real animation event.
	// There is no self-rescheduling per-frame task (that hung the main thread in
	// v1.6.0), and deferral keeps us from re-entering the graph mid-dispatch.
	void ScheduleRelease()
	{
		if (auto* task = SKSE::GetTaskInterface()) {
			task->AddTask([]() {
				if (auto* p = RE::PlayerCharacter::GetSingleton()) {
					p->NotifyAnimationGraph("attackRelease");
				}
			});
		}
	}

	void ScheduleRedraw()
	{
		if (auto* task = SKSE::GetTaskInterface()) {
			task->AddTask([]() {
				if (auto* p = RE::PlayerCharacter::GetSingleton()) {
					p->NotifyAnimationGraph("bowAttackStart");
					p->NotifyAnimationGraph("BowDrawStart");
					p->NotifyAnimationGraph("bowDraw");
				}
			});
		}
	}

	void InstallHooks()
	{
		// AE (1.6.x) vtable slot for GetPowerSpeedMult is 0xB0 (SE is 0xAF); see
		// CommonLibSSE-NG Projectile::GetPowerSpeedMult -> RelocateVirtual(0xAF, 0xB0).
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_ArrowProjectile[0] };
		PowerSpeedHook::func = vtbl.write_vfunc(0xB0, PowerSpeedHook::thunk);
		SKSE::log::info("AutoFireBow: hooked ArrowProjectile::GetPowerSpeedMult (AE vtable 0xB0)");
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			RegisterBowLoop();
			break;
		default:
			break;
		}
	}
}

// Declarative SKSE plugin metadata (CommonLibSSE-NG). Exported as
// SKSEPlugin_Version + SKSEPlugin_Query so SKSE recognises and loads the DLL.
SKSEPluginInfo(
	.Version = REL::Version{ 1, 11, 0 },
	.Name = "AutoFireBow",
	.Author = "mase",
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

// Entry point SKSE calls after loading the plugin.
SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();
	SKSE::Init(a_skse);
	SKSE::log::info("AutoFireBow {} loaded — full power+damage + event-driven rapid-fire loop",
		REL::Version{ 1, 11, 0 }.string());
	InstallHooks();
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
