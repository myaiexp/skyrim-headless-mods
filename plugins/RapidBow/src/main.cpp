#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <cctype>
#include <chrono>
#include <cstring>
#include <string>

namespace
{
	// Send spdlog (what SKSE::log::* uses) to <My Games>/SKSE/RapidBow.log so the
	// plugin leaves a visible trace when the game loads it. spdlog stamps every line
	// to the millisecond, which is what the rapid-fire timing probe relies on.
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

	// Short label for the player's current attack state, so the probe log reads
	// clearly. The bow sequence is kBowDraw(8) -> kBowAttached(9) -> kBowDrawn(10)
	// -> kBowReleasing(11) -> kBowReleased(12) -> kBowNextAttack(13) -> kBowFollowThrough(14).
	const char* AttackStateName(RE::ATTACK_STATE_ENUM a_state)
	{
		using S = RE::ATTACK_STATE_ENUM;
		switch (a_state) {
		case S::kNone:             return "None";
		case S::kDraw:             return "Draw";
		case S::kSwing:            return "Swing";
		case S::kHit:              return "Hit";
		case S::kNextAttack:       return "NextAttack";
		case S::kFollowThrough:    return "FollowThrough";
		case S::kBash:             return "Bash";
		case S::kBowDraw:          return "BowDraw(8)";
		case S::kBowAttached:      return "BowAttached(9)";
		case S::kBowDrawn:         return "BowDrawn(10)";
		case S::kBowReleasing:     return "BowReleasing(11)";
		case S::kBowReleased:      return "BowReleased(12)";
		case S::kBowNextAttack:    return "BowNextAttack(13)";
		case S::kBowFollowThrough: return "BowFollowThrough(14)";
		default:                   return "?";
		}
	}

	RE::ATTACK_STATE_ENUM PlayerAttackState()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		return player ? player->AsActorState()->GetAttackState() : RE::ATTACK_STATE_ENUM::kNone;
	}

	// PROBE timing anchor: set when "bowDraw" fires (nock start, once per cycle).
	// The draw-strength ramp is measured relative to this, so a release log can report
	// the natural power at "+N ms since nock" — that locates the genuine full-power
	// instant and tells us whether it lands before/at the BowDrawn event.
	std::chrono::steady_clock::time_point g_drawStart{};
	bool                                  g_drawStartValid = false;

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

	long long MsSinceDrawStart()
	{
		if (!g_drawStartValid) {
			return -1;
		}
		auto delta = std::chrono::steady_clock::now() - g_drawStart;
		return std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();
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
	//
	// PROBE: the clamp branch runs exactly once per arrow (first GetSpeed call, while
	// power is still < 1.0), so logging there marks the instant the arrow is launched —
	// together with the player's attack state at that instant. That pins down the
	// earliest draw point at which a real arrow is actually produced.
	struct PowerSpeedHook
	{
		static float thunk(RE::Projectile* a_this)
		{
			auto& runtime = a_this->GetProjectileRuntimeData();
			auto  shooter = runtime.shooter.get();
			if (shooter && shooter->IsPlayerRef() && !logged) {
				// Once per arrow — CRITICAL: the weaponDamage rescale must not repeat, or
				// it would compound on every GetSpeed call and inflate damage without bound.
				logged = true;
				const float natural = runtime.power;          // 0x188: draw mult, drives speed (read live)
				const float dmgBefore = runtime.weaponDamage;  // 0x198: damage baked at launch as base*drawMult
				if (natural < 1.0f && natural > 0.0f) {
					runtime.power = 1.0f;                        // → full speed
					runtime.weaponDamage *= 1.0f / natural;      // undo partial-draw scaling → full damage
				}
				SKSE::log::info(">>> RELEASE  natural_power={:.3f}  weaponDamage {:.1f}->{:.1f}  +{}ms  attackState={}",
					natural, dmgBefore, runtime.weaponDamage, MsSinceDrawStart(),
					AttackStateName(PlayerAttackState()));
			}
			return func(a_this);
		}

		static inline bool logged = false;

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// PROBE: logs bow-relevant animation-graph events on the player with the live
	// attack state, so we can read the real event names + timing of a draw/release
	// cycle and find the earliest loosable point. Removed once the rapid-fire design
	// is settled.
	class AnimProbeSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
	{
	public:
		static AnimProbeSink* GetSingleton()
		{
			static AnimProbeSink singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(
			const RE::BSAnimationGraphEvent*               a_event,
			RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source) override
		{
			(void)a_source;
			const char* tag = a_event ? a_event->tag.c_str() : nullptr;
			if (tag) {
				if (std::strcmp(tag, "bowDraw") == 0) {
					// Nock start: (re)anchor the draw timer, arm the per-arrow release log,
					// and re-arm the loop's per-cycle fire guard.
					g_drawStart = std::chrono::steady_clock::now();
					g_drawStartValid = true;
					g_firedThisCycle = false;
					PowerSpeedHook::logged = false;
				} else if (std::strcmp(tag, "BowDrawn") == 0) {
					SKSE::log::info("    BowDrawn reached at +{}ms since nock  attackHeld={}", MsSinceDrawStart(), AttackHeld());
					if (AttackHeld() && !g_firedThisCycle) {
						g_firedThisCycle = true;
						ScheduleRelease();  // loose at genuine full draw (hook forces full power+damage)
					}
				} else if (std::strcmp(tag, "arrowRelease") == 0) {
					if (AttackHeld()) {
						ScheduleRedraw();  // continue the loop: re-nock for the next shot
					}
				}
				if (IsRelevant(tag)) {
					SKSE::log::info("anim: {:<22} payload='{}'  attackState={}",
						tag,
						a_event->payload.c_str() ? a_event->payload.c_str() : "",
						AttackStateName(PlayerAttackState()));
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}

	private:
		static bool IsRelevant(std::string a_tag)
		{
			for (auto& c : a_tag) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			static const char* kKeys[] = { "bow", "arrow", "attack", "draw", "release", "shoot", "weap" };
			for (const auto* key : kKeys) {
				if (a_tag.find(key) != std::string::npos) {
					return true;
				}
			}
			return false;
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

	void RegisterAnimProbe()
	{
		static bool registered = false;
		if (registered) {
			return;
		}
		if (auto* idm = RE::BSInputDeviceManager::GetSingleton()) {
			idm->AddEventSink(AttackInputSink::GetSingleton());
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (player && player->AddAnimationGraphEventSink(AnimProbeSink::GetSingleton())) {
			registered = true;
			SKSE::log::info("RapidBow probe: animation-graph event sink registered on player");
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
		SKSE::log::info("RapidBow: hooked ArrowProjectile::GetPowerSpeedMult (AE vtable 0xB0)");
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			RegisterAnimProbe();  // also drives the event-driven rapid-fire loop
			break;
		default:
			break;
		}
	}
}

// Declarative SKSE plugin metadata (CommonLibSSE-NG). Exported as
// SKSEPlugin_Version + SKSEPlugin_Query so SKSE recognises and loads the DLL.
SKSEPluginInfo(
	.Version = REL::Version{ 1, 10, 0 },
	.Name = "RapidBow",
	.Author = "mase",
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

// Entry point SKSE calls after loading the plugin.
SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();
	SKSE::Init(a_skse);
	SKSE::log::info("RapidBow {} loaded — full power+damage + event-driven rapid-fire loop",
		REL::Version{ 1, 10, 0 }.string());
	InstallHooks();
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
