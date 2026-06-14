#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

// AutoCastSpell — auto-recast a held fire-and-forget spell, the spell analog of
// AutoFireBow.
//
// Confirmed anim events (in-engine probe, 2026-06-14, vanilla+AutoCastSpell, Firebolt):
//   charge-start (arm)         right: "BeginCastRight"        left: "BeginCastLeft"
//   fired (-> re-press)        right: "MRh_SpellFire_Event"   left: "MLh_SpellFire_Event"
//   charged-ready (-> release): NO SUCH ANIM EVENT. The whole charge period is
//     animation-silent (BeginCast -> [hold, no event] -> SpellFire on release). So the
//     loop can't learn "spell is charged" from an anim event the way AutoFireBow used
//     "BowDrawn". Instead we POLL the hand's RE::MagicCaster::state for kReady — the
//     engine's real charge-complete signal — and inject the synthetic release then.
//
// Task 2 (this build) is the make-or-break GATE: detect kReady, inject ONE synthetic
// release per hold, and confirm the engine actually fires a genuinely-charged spell on
// synthetic input. No re-press / loop yet (that is Task 3).

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

	// Held state of each cast control, from raw input (read by the poll thread → atomic).
	std::atomic<bool> g_held[kHandCount] = {};
	// One synthetic release per hold (game-thread only; re-armed on physical release).
	bool g_firedThisHold[kHandCount] = {};
	// True only while we dispatch our own synthetic ButtonEvent, so CastInputSink skips it
	// (else our fake release would flip the held-state the loop gates on). SendEvent fans
	// out inline on the main thread, so a plain flag is enough.
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
	// magnitude/perks). pressed=false => release (value 0, IsUp) to fire; pressed=true =>
	// fresh press (value 1, IsDown) to recharge. Mirrors AutoFireBow's SendSyntheticAttack,
	// parameterized per hand. Call on the game thread.
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

	// Game thread: for each held hand whose spell has reached full charge (caster state
	// kReady) and that hasn't fired yet this hold, inject the synthetic release. This is
	// the kReady-poll replacement for the missing "charged" anim event.
	void CheckCasters()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		for (int i = 0; i < kHandCount; ++i) {
			if (!g_held[i].load() || g_firedThisHold[i]) {
				continue;
			}
			const Hand hand = static_cast<Hand>(i);
			auto* caster = player->GetMagicCaster(SourceFor(hand));
			if (!caster) {
				continue;
			}
			if (caster->state.get() == RE::MagicCaster::State::kReady) {
				g_firedThisHold[i] = true;
				SKSE::log::info("AutoCastSpell: {} reached kReady -> injecting synthetic release",
					ControlFor(hand));
				SendSyntheticCast(hand, false);
			}
		}
	}

	// Pace the caster-state poll off-thread (SKSE task re-enqueue drains within a single
	// frame, so it can't space work across frames). The thread only enqueues a game-thread
	// CheckCasters while a cast control is actually held; it touches no game state itself.
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
	// events. Re-arms the per-hold fire guard on physical release.
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
						g_firedThisHold[idx] = false;  // re-arm for the next hold
					}
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	// Logs the confirmed cast lifecycle tags so the gate result is visible: a
	// SpellFire_Event after our "injecting synthetic release" line — while the button is
	// still physically held — is proof the synthetic release fired a charged spell.
	class SpellLoopSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
	{
	public:
		static SpellLoopSink* GetSingleton()
		{
			static SpellLoopSink singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(
			const RE::BSAnimationGraphEvent*               a_event,
			RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source) override
		{
			(void)a_source;
			if (!a_event) {
				return RE::BSEventNotifyControl::kContinue;
			}
			const char* tag = a_event->tag.c_str();
			if (!tag) {
				return RE::BSEventNotifyControl::kContinue;
			}
			if (std::strcmp(tag, "BeginCastRight") == 0 || std::strcmp(tag, "BeginCastLeft") == 0 ||
				std::strcmp(tag, "MRh_SpellFire_Event") == 0 || std::strcmp(tag, "MLh_SpellFire_Event") == 0 ||
				std::strcmp(tag, "CastStop") == 0) {
				SKSE::log::info("anim: {}", tag);
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	bool g_inputSinkAdded = false;
	bool g_animSinkAdded = false;

	void EnsureAnimSink(int a_attemptsLeft)
	{
		if (g_animSinkAdded) {
			return;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (player && player->AddAnimationGraphEventSink(SpellLoopSink::GetSingleton())) {
			g_animSinkAdded = true;
			SKSE::log::info("AutoCastSpell: anim-graph sink attached on player");
			return;
		}
		if (a_attemptsLeft > 0) {
			if (auto* task = SKSE::GetTaskInterface()) {
				task->AddTask([a_attemptsLeft]() { EnsureAnimSink(a_attemptsLeft - 1); });
			}
		} else {
			SKSE::log::warn("AutoCastSpell: gave up attaching anim-graph sink (player 3D never ready)");
		}
	}

	void RegisterSinks()
	{
		if (!g_inputSinkAdded) {
			if (auto* idm = RE::BSInputDeviceManager::GetSingleton()) {
				idm->AddEventSink(CastInputSink::GetSingleton());
				g_inputSinkAdded = true;
				SKSE::log::info("AutoCastSpell: input sink registered");
			}
		}
		g_animSinkAdded = false;
		EnsureAnimSink(600);
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
	.Version = REL::Version{ 0, 2, 0 },
	.Name = "AutoCastSpell",
	.Author = "mase",
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();
	SKSE::Init(a_skse);
	SKSE::log::info("AutoCastSpell {} loaded — caster-state-poll synthetic-release gate (Task 2)",
		REL::Version{ 0, 2, 0 }.string());
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
