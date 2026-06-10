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

	// Auto-fired arrows loose at full draw (honest natural_power ~1.0), so each shot matches a
	// manual full-draw arrow. But the auto-fire CADENCE is slower than skilled manual play, so
	// sustained DPS lags. Compensate with a flat damage bump on auto arrows only (manual shots
	// stay vanilla). Tune to close the DPS gap; ~10% is the starting point.
	constexpr float kAutoDamageMult = 1.10f;

	// Set when we auto-loose; consumed once by the next arrow's GetPowerSpeedMult hook to apply
	// kAutoDamageMult. A flat multiply isn't idempotent, so it's cleared on first application to
	// avoid compounding across the arrow's repeated speed queries.
	bool g_boostNextArrow = false;

	// Per-draw-cycle guard so the loop auto-looses at most once per cycle (reset at nock).
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

	// Fire the auto-loose once per draw cycle, at full draw. Deferred so we never re-enter the
	// input pipeline mid-dispatch.
	//
	// The engine does NOT self-redraw a still-held button (holding IS the draw, releasing IS the
	// loose — same axis, so a held button never emits the button-up that looses nor the fresh
	// button-down that redraws). An earlier build assumed the synthetic release "desyncs" the
	// engine into re-pressing itself; that was a false positive masked by a stale duplicate plugin
	// (RapidBow.dll) silently doing the re-nock. So re-nock needs an explicit synthetic press.
	//
	// The press is NOT chained here: the engine defers the actual loose/launch by a few frames, so
	// a press fired now lands mid-loose and trips the vanilla double-tap stuck-bow state (looses
	// once, then won't release). Instead AutoArrowHook fires the re-nock once the auto arrow has
	// actually launched — the real "loose complete" signal.
	void LooseNow()
	{
		g_firedThisCycle = true;
		g_boostNextArrow = true;  // tags the arrow for the DPS bump AND triggers re-nock at its launch
		if (auto* task = SKSE::GetTaskInterface()) {
			task->AddTask([]() { SendSyntheticAttack(false); });  // loose on the genuine full draw
		}
	}

	// Applies the DPS-compensation damage bump to auto-fired arrows, and logs every player
	// arrow's honest charge. There is NO power clamp — auto arrows loose at genuine full draw
	// (natural_power ~1.0); we only multiply weaponDamage by kAutoDamageMult, and only on the
	// arrow our auto-loose tagged (g_boostNextArrow). Manual arrows pass through untouched.
	//
	// Site: arrow speed/damage both read PROJECTILE_RUNTIME_DATA; modifying weaponDamage here
	// reaches impact damage (the old clamp used this same hook). GetPowerSpeedMult is virtual,
	// so we hook the ArrowProjectile vtable. The boost is gated by g_boostNextArrow + cleared
	// on first apply (flat multiply isn't idempotent); `logged` gates the readout once/cycle.
	// Applies the DPS-compensation damage bump to auto-fired arrows only. The boost is gated by
	// g_boostNextArrow (set at auto-loose) and cleared on first application — verified safe: the
	// engine calls GetPowerSpeedMult ~2x per arrow at launch, so the immediate clear prevents the
	// second call from compounding, and each shot gets a fresh arrow pointer (no stale targeting).
	// Manual arrows pass through untouched. NOTE: runtime.power reads 1.0 here regardless of draw,
	// so it is NOT logged as "charge" — the meaningful value is the weaponDamage delta.
	struct AutoArrowHook
	{
		static float thunk(RE::Projectile* a_this)
		{
			auto& runtime = a_this->GetProjectileRuntimeData();
			auto  shooter = runtime.shooter.get();
			if (shooter && shooter->IsPlayerRef() && g_boostNextArrow) {
				g_boostNextArrow = false;
				const float before = runtime.weaponDamage;
				runtime.weaponDamage *= kAutoDamageMult;  // 0x198: DPS-compensation bump (auto only)
				SKSE::log::info("AutoFireBow: auto arrow damage {:.1f} -> {:.1f} (x{:.2f})",
					before, runtime.weaponDamage, kAutoDamageMult);
				// The auto arrow has now launched (this hook fires at launch), so the loose is
				// complete — safe to re-nock without tripping the double-tap stuck state. Deferred
				// synthetic press starts the next draw, gated on the button still being held.
				if (auto* task = SKSE::GetTaskInterface()) {
					task->AddTask([]() {
						if (AttackHeld()) {
							SendSyntheticAttack(true);
							SKSE::log::info("AutoFireBow: re-nock press injected (loop continues)");
						}
					});
				}
			}
			return func(a_this);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// Drives the rapid-fire loop off the player's bow animation graph: on BowDraw
	// (re-)arm the per-cycle fire guard; on the engine's own BowDrawn (genuine full
	// draw, any bow/perk/speed) auto-loose, which also injects the re-nock press.
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
			if (std::strcmp(tag, "BowDraw") == 0) {
				// Nock start: re-arm the per-cycle fire guard so the next BowDrawn can loose.
				// The engine's native nock event is capital-B "BowDraw" — NOT the lowercase
				// "bowDraw" RapidBow used to inject itself. With the input-driven re-draw the
				// engine emits its own casing; matching the wrong case left the guard stuck
				// true after the first loose, so the loop died after one shot.
				g_firedThisCycle = false;
			} else if (std::strcmp(tag, "BowDrawn") == 0) {
				// Auto-loose at genuine full draw while held.
				if (AttackHeld() && !g_firedThisCycle) {
					LooseNow();
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
		AutoArrowHook::func = vtbl.write_vfunc(idx, AutoArrowHook::thunk);
		SKSE::log::info("AutoFireBow: auto-arrow hook on ArrowProjectile::GetPowerSpeedMult (vtable slot {:#x})", idx);
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
	.Version = REL::Version{ 2, 0, 0 },
	.Name = "AutoFireBow",
	.Author = "mase",
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

// Entry point SKSE calls after loading the plugin.
SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();
	SKSE::Init(a_skse);
	SKSE::log::info("AutoFireBow {} loaded — hold-to-auto-fire full-draw shots + {:.0f}% auto-arrow damage bump",
		REL::Version{ 2, 0, 0 }.string(), (kAutoDamageMult - 1.0f) * 100.0f);
	InstallHooks();
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
