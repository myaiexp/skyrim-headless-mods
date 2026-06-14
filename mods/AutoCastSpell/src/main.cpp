#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

// AutoCastSpell — auto-recast a held fire-and-forget spell, the spell analog of
// AutoFireBow. Hold a cast control with a fire-and-forget spell equipped and the engine
// loops charge -> fire -> recharge until the control is released. Per hand, independent
// (holding both naturally dual-casts when the perk applies).
//
// Loop, driven off RE::MagicCaster::state polled ~25 Hz (there is NO "spell charged" anim
// event — the charge period is animation-silent, so anim events can't drive it):
//   kReady (fully charged)  -> inject a synthetic RELEASE -> the spell fires
//   new charge after a fire  -> re-arm (mirrors AutoFireBow re-arming on each BowDraw); a
//                               held button usually auto-recharges like a held bow re-draws
//   stuck idle after a fire  -> two-step release->press re-tap nudges the next charge
// The per-cycle logging is intentionally kept: besides the trace it paces the recharge
// (the flush spaces the synthetic injects from the state re-reads); stripping it
// regressed the loop. Keep it unless you replace it with explicit pacing.

namespace
{
	enum class Hand
	{
		kRight = 0,
		kLeft  = 1
	};
	constexpr int kHandCount = 2;

	const char* ControlFor(Hand a_hand)
	{
		return a_hand == Hand::kRight ? "Right Attack/Block" : "Left Attack/Block";
	}
	RE::MagicSystem::CastingSource SourceFor(Hand a_hand)
	{
		return a_hand == Hand::kRight ? RE::MagicSystem::CastingSource::kRightHand
		                              : RE::MagicSystem::CastingSource::kLeftHand;
	}

	std::atomic<bool> g_held[kHandCount] = {};
	bool g_firedThisCycle[kHandCount] = {};
	int  g_ticksSinceFire[kHandCount] = {};
	bool g_releasedForRecharge[kHandCount] = {};
	int  g_ticksSinceRelease[kHandCount] = {};
	int  g_lastState[kHandCount] = { -1, -1 };
	std::atomic<bool> g_injecting{ false };
	std::atomic<bool> g_pollerStarted{ false };

	void SetupLog()
	{
		auto logDir = SKSE::log::log_directory();
		if (!logDir) {
			return;
		}
		auto path = *logDir / "AutoCastSpell.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
		auto logger = std::make_shared<spdlog::logger>("global", std::move(sink));
		logger->set_level(spdlog::level::info);
		logger->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(logger));
	}

	// Drive the engine's real cast pipeline with a synthetic cast-control button event, so
	// the cast runs on the genuinely-charged spell the held button already built (honest
	// magnitude/perks/dual-cast). pressed=false => release (value 0, IsUp) to fire on full
	// charge; pressed=true => fresh press (value 1, IsDown) to recharge. Game thread only.
	void SendSyntheticCast(Hand a_hand, bool a_pressed)
	{
		auto* idm = RE::BSInputDeviceManager::GetSingleton();
		if (!idm) {
			return;
		}
		const float value    = a_pressed ? 1.0f : 0.0f;
		const float heldSecs = a_pressed ? 0.0f : 0.5f;  // release = value 0 + heldSecs>0 => IsUp()
		auto* be = RE::ButtonEvent::Create(RE::INPUT_DEVICE::kMouse, ControlFor(a_hand), 0, value, heldSecs);
		if (!be) {
			return;
		}
		RE::InputEvent* head = be;
		g_injecting = true;
		idm->SendEvent(&head);
		g_injecting = false;
		RE::free(be);
	}

	// Game thread: the loop, per held hand, off the caster state.
	void CheckCasters()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		for (int i = 0; i < kHandCount; ++i) {
			if (!g_held[i].load()) {
				continue;
			}
			const Hand hand   = static_cast<Hand>(i);
			auto*      caster = player->GetMagicCaster(SourceFor(hand));
			if (!caster) {
				continue;
			}
			const auto state = caster->state.get();
			const int  st    = static_cast<int>(state);
			if (st != g_lastState[i]) {
				SKSE::log::info("state: {} {} -> {}", ControlFor(hand), g_lastState[i], st);
				g_lastState[i] = st;
			}

			if (g_firedThisCycle[i]) {
				++g_ticksSinceFire[i];
			}
			if (g_releasedForRecharge[i]) {
				++g_ticksSinceRelease[i];
			}

			constexpr int kRechargeDelayTicks  = 25;  // ~1s: let the cast fully wind down
			constexpr int kReleaseToPressTicks = 5;   // ~200ms gap so the engine registers "up"
			const bool idle = state == RE::MagicCaster::State::kNone ||
			                  state == RE::MagicCaster::State::kUnk01;
			const bool charging = state == RE::MagicCaster::State::kUnk02 ||
			                      state == RE::MagicCaster::State::kCharging;

			// Re-arm as soon as a fresh charge starts after our fire — whether the engine
			// auto-recharged the still-held button or the two-step fallback did. Without it
			// the loop fires once then sits charged (the "only fires once when held" bug).
			if (g_firedThisCycle[i] && charging) {
				g_firedThisCycle[i]      = false;
				g_releasedForRecharge[i] = false;
				SKSE::log::info("loop: {} new charge -> re-armed", ControlFor(hand));
			}

			if (state == RE::MagicCaster::State::kReady && !g_firedThisCycle[i]) {
				auto* spell = caster->currentSpell;
				const bool ff = spell && spell->GetCastingType() == RE::MagicSystem::CastingType::kFireAndForget;
				if (ff) {
					g_firedThisCycle[i]      = true;
					g_releasedForRecharge[i] = false;
					g_ticksSinceFire[i]      = 0;
					SKSE::log::info("loop: {} kReady -> release (fire)", ControlFor(hand));
					SendSyntheticCast(hand, false);
				}
			} else if (g_firedThisCycle[i] && !g_releasedForRecharge[i] &&
					   g_ticksSinceFire[i] >= kRechargeDelayTicks && idle) {
				// Cast wound down without a fresh charge: release to break the post-cast state,
				// then (step 2) a fresh press a few frames later.
				g_releasedForRecharge[i] = true;
				g_ticksSinceRelease[i]   = 0;
				SKSE::log::info("loop: {} idle (state {}) -> recharge step1: release",
					ControlFor(hand), st);
				SendSyntheticCast(hand, false);
			} else if (g_releasedForRecharge[i] && g_ticksSinceRelease[i] >= kReleaseToPressTicks) {
				g_firedThisCycle[i]      = false;
				g_releasedForRecharge[i] = false;
				SKSE::log::info("loop: {} recharge step2: press", ControlFor(hand));
				SendSyntheticCast(hand, true);
			}
		}
	}

	// Pace the caster-state poll off-thread (SKSE task re-enqueue drains within a single
	// frame, so it can't space work across frames). Only enqueues a game-thread check while
	// a cast control is held; touches no game state itself.
	void StartPoller()
	{
		if (g_pollerStarted.exchange(true)) {
			return;
		}
		std::thread([]() {
			for (;;) {
				std::this_thread::sleep_for(std::chrono::milliseconds(40));
				if (g_held[0].load() || g_held[1].load()) {
					if (auto* task = SKSE::GetTaskInterface()) {
						task->AddTask([]() { CheckCasters(); });
					}
				}
			}
		}).detach();
	}

	// Tracks held-state of the two cast controls from raw input, skipping our own injected
	// events. Clears the per-cycle guards on physical release so the loop ends cleanly.
	class CastInputSink : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static CastInputSink* GetSingleton()
		{
			static CastInputSink singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(
			RE::InputEvent* const*               a_event,
			RE::BSTEventSource<RE::InputEvent*>* a_source) override
		{
			(void)a_source;
			if (!a_event || g_injecting.load()) {
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
				int idx = -1;
				if (ue && std::strcmp(ue, "Right Attack/Block") == 0) {
					idx = static_cast<int>(Hand::kRight);
				} else if (ue && std::strcmp(ue, "Left Attack/Block") == 0) {
					idx = static_cast<int>(Hand::kLeft);
				}
				if (idx >= 0) {
					const bool pressed = btn->IsPressed();
					g_held[idx].store(pressed);
					if (!pressed) {
						g_firedThisCycle[idx]      = false;
						g_releasedForRecharge[idx] = false;
					}
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	void RegisterSinks()
	{
		static bool registered = false;
		if (registered) {
			return;
		}
		if (auto* idm = RE::BSInputDeviceManager::GetSingleton()) {
			idm->AddEventSink(CastInputSink::GetSingleton());
			registered = true;
			SKSE::log::info("AutoCastSpell: cast-control input sink registered");
		}
		StartPoller();
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			RegisterSinks();
			break;
		default:
			break;
		}
	}
}

SKSEPluginInfo(
	.Version = REL::Version{ 1, 0, 7 },
	.Name = "AutoCastSpell",
	.Author = "mase",
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();
	SKSE::Init(a_skse);
	SKSE::log::info("AutoCastSpell {} loaded — hold a fire-and-forget spell to auto-recast (per hand)",
		REL::Version{ 1, 0, 7 }.string());
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
