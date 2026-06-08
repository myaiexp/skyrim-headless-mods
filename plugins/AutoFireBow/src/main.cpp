#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <cctype>
#include <chrono>
#include <cstring>
#include <string>

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

	// PROBE: short label for the player's current attack state, so the log reads clearly.
	// Bow sequence: kBowDraw(8) -> kBowAttached(9) -> kBowDrawn(10) -> kBowReleasing(11)
	// -> kBowReleased(12) -> kBowNextAttack(13) -> kBowFollowThrough(14).
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

	// PROBE timing anchor: set when "bowDraw" fires (nock start, once per cycle). Lets a
	// release log report the natural power at "+N ms since nock", locating the genuine
	// full-power instant relative to the BowDrawn event.
	std::chrono::steady_clock::time_point g_drawStart{};
	bool                                  g_drawStartValid = false;

	long long MsSinceDrawStart()
	{
		if (!g_drawStartValid) {
			return -1;
		}
		auto delta = std::chrono::steady_clock::now() - g_drawStart;
		return std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();
	}

	// Per-draw-cycle guard so the loop auto-looses at most once per BowDrawn (reset at
	// nock). The loop keys off the engine's own BowDrawn event, so it's robust to any
	// bow speed / perk / enchantment — no draw-time assumptions.
	bool g_firedThisCycle = false;

	// Whether the attack control is currently held, tracked from raw input events (the
	// AttackBlockHandler held-flags didn't reflect this reliably).
	bool g_attackHeld = false;

	// Set true only while we synchronously dispatch a synthetic attack event through the
	// input pipeline (see SendSyntheticAttack). AttackInputSink is itself a sink on the
	// same BSInputDeviceManager source, so without this guard our own fake release would
	// flip g_attackHeld and break the hold-gated loop. SendEvent notifies sinks inline on
	// the main thread, so a plain bool is sufficient (no reentrancy across frames).
	bool g_injectingSynthetic = false;

	// Loop helpers (defined below; the animation-event sink drives them).
	bool AttackHeld() { return g_attackHeld; }

	// Drive the engine's real attack pipeline with a synthetic "Right Attack/Block" button
	// event, so the loose runs on the genuinely-charged draw the held button already built
	// (honest power/damage — no projectile clamp). pressed=false => release (value 0, IsUp);
	// pressed=true => fresh press (value 1, IsDown) to start the next draw.
	//
	// Route: BSInputDeviceManager is the BSTEventSource<InputEvent*> the engine's own input
	// poll fans out to every frame; PlayerControls' attack/block handler is one of its sinks.
	// SendEvent(&head) replays that exact fan-out with our event, synchronously. QUserEvent is
	// set directly to the control string so the handler routes it regardless of device/idCode.
	//
	// The make-or-break unknown (design doc): the attack handler may consume this queued event
	// (works) or poll live device state (won't charge/loose) — Task 1 probes that in-game.
	void SendSyntheticAttack(bool pressed)
	{
		auto* idm = RE::BSInputDeviceManager::GetSingleton();
		if (!idm) {
			return;
		}
		const float value = pressed ? 1.0f : 0.0f;
		// Press: heldDownSecs 0 => IsDown(). Release: value 0 with heldDownSecs>0 => IsUp().
		const float heldSecs = pressed ? 0.0f : 0.5f;
		auto* be = RE::ButtonEvent::Create(RE::INPUT_DEVICE::kMouse, "Right Attack/Block", 0, value, heldSecs);
		if (!be) {
			return;
		}
		RE::InputEvent* head = be;
		g_injectingSynthetic = true;
		idm->SendEvent(&head);
		g_injectingSynthetic = false;
		RE::free(be);
	}

	// PROBE (observe-only): read the launched arrow's honest draw charge and log it — no
	// rewrite. The old clamp (runtime.power = 1.0; weaponDamage *= 1/natural) is GONE; this
	// hook now only reports the engine's genuine value so we can see whether the synthetic
	// input-release actually charges the shot (~1.0) or not (~0.35).
	//
	// The engine never exposes a clean "current draw" float; it bakes the draw-time fraction
	// into the launched arrow's `power` (PROJECTILE_RUNTIME_DATA, 0.0–1.0). Both arrow *speed*
	// (via Projectile::GetSpeed -> GetPowerSpeedMult) and impact *damage* read that field.
	// GetPowerSpeedMult is virtual, so we hook the ArrowProjectile vtable to observe it.
	//
	// `logged` (reset on bowDraw) gates the readout to once per draw cycle, not per GetSpeed
	// call. The >>> RELEASE line records natural_power + weaponDamage + ms-since-nock + state.
	struct ChargeProbeHook
	{
		static float thunk(RE::Projectile* a_this)
		{
			auto& runtime = a_this->GetProjectileRuntimeData();
			auto  shooter = runtime.shooter.get();
			if (shooter && shooter->IsPlayerRef() && !logged) {
				logged = true;
				const float natural = runtime.power;  // 0x188: draw mult, drives speed (read live)
				SKSE::log::info(">>> RELEASE  natural_power={:.3f}  weaponDamage={:.1f}  +{}ms  attackState={}",
					natural, runtime.weaponDamage, MsSinceDrawStart(), AttackStateName(PlayerAttackState()));
			}
			return func(a_this);
		}

		static inline bool logged = false;

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
				// Nock start: anchor the draw timer, re-arm the per-cycle fire guard and the
				// once-per-cycle charge readout.
				g_drawStart = std::chrono::steady_clock::now();
				g_drawStartValid = true;
				g_firedThisCycle = false;
				ChargeProbeHook::logged = false;
			} else if (std::strcmp(tag, "BowDrawn") == 0) {
				SKSE::log::info("    BowDrawn at +{}ms since nock  attackHeld={}", MsSinceDrawStart(), AttackHeld());
				if (AttackHeld() && !g_firedThisCycle) {
					g_firedThisCycle = true;
					// PROBE loose: synthetic input-release on the genuinely-charged draw the
					// held button built. Deferred so we never re-enter the input pipeline
					// mid-dispatch. No projectile clamp — ChargeProbeHook reads the honest power.
					if (auto* task = SKSE::GetTaskInterface()) {
						task->AddTask([]() { SendSyntheticAttack(false); });
					}
				}
			} else if (std::strcmp(tag, "arrowRelease") == 0) {
				// PROBE: log only — inject nothing for re-nock. We are measuring whether a
				// new draw cycle starts on its own while the button stays held.
				SKSE::log::info("    arrowRelease  attackHeld={}  state={}", AttackHeld(), AttackStateName(PlayerAttackState()));
			}
			if (IsRelevant(tag)) {
				SKSE::log::info("anim: {:<22} state={}", tag, AttackStateName(PlayerAttackState()));
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
			if (!a_event || g_injectingSynthetic) {
				// Skip our own injected events (SendSyntheticAttack) — they must not move
				// the real held-state the loop gates on.
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

	void InstallHooks()
	{
		// GetPowerSpeedMult's vtable slot differs by runtime: SE/VR 0xAF, AE 0xB0 — the
		// exact mapping CommonLibSSE-NG itself uses for Projectile::GetPowerSpeedMult
		// (RelocateVirtual(0xAF, 0xB0)). REL::Relocate picks per the live runtime, so one
		// NG-built DLL hooks the correct slot on SE, AE, and VR. AE is tested; SE/VR
		// untested (no runtime here) but use CommonLib's own authoritative index.
		const auto idx = REL::Relocate<std::size_t>(0xAF, 0xB0);
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_ArrowProjectile[0] };
		ChargeProbeHook::func = vtbl.write_vfunc(idx, ChargeProbeHook::thunk);
		SKSE::log::info("AutoFireBow: charge-probe hook on ArrowProjectile::GetPowerSpeedMult (vtable slot {:#x})", idx);
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
	SKSE::log::info("AutoFireBow {} loaded — PROBE: synthetic input-release loose, no clamp, charge readout",
		REL::Version{ 1, 11, 0 }.string());
	InstallHooks();
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
