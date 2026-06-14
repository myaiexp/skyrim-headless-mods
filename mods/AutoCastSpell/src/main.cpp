#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <cstring>

// AutoCastSpell — auto-recast a held fire-and-forget spell, the spell analog of
// AutoFireBow. v0 (Task 1) is a PURE PROBE: no injection, no loop, no hooks. It
// only logs the held-state of the cast controls and every animation-graph event
// on the player, so a manual charged cast in-engine reveals the real magic tags
// (charge-start / charged-ready / fired) that Tasks 2-3 will key the loop off.
//
// Confirmed anim events (in-engine probe, 2026-06-14, vanilla+AutoCastSpell, Firebolt):
//   charge-start (arm)         right: "BeginCastRight"        left: "BeginCastLeft"
//   fired (-> re-press)        right: "MRh_SpellFire_Event"   left: "MLh_SpellFire_Event"
//   charged-ready (-> release): NO SUCH EVENT. The design's candidate "MRh_SpellReadyOut"
//     does not exist; the whole charge period is animation-SILENT (BeginCast -> [~1.8s
//     hold, no event] -> SpellFire on release). "MLh_PreAimedOut" appears only on the
//     left hand and only ~270ms before fire (a release/aim tag, not charge-complete), so
//     it is NOT a usable charged trigger. => the loop cannot learn "spell is charged" from
//     an anim event the way AutoFireBow used "BowDrawn". Instead poll the hand's
//     RE::MagicCaster::state for kReady (Actor::GetMagicCaster(source)->state @0x30);
//     that is the engine's real charge-complete signal. SpellFire_Event stays the
//     re-press trigger (precise, fires at projectile launch — AutoFireBow's pattern).

namespace
{
	// Send spdlog (what SKSE::log::* uses) to <My Games>/SKSE/AutoCastSpell.log so the
	// plugin leaves a visible trace that it loaded and a readable probe transcript.
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

	// Logs the held-state of the cast controls from raw input. No g_injectingSynthetic
	// guard yet — v0 injects nothing. The control strings are the same axis the bow uses
	// ("Right/Left Attack/Block"); magic casting charges off the held control identically.
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
					SKSE::log::info("input: {} held={}", ue, btn->IsPressed());
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	// The probe: dumps EVERY animation-graph event on the player so a manual charged
	// cast reveals the real magic tags (and any payload) to key the loop off.
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
			const char* payload = a_event->payload.c_str();
			if (payload && payload[0] != '\0') {
				SKSE::log::info("anim: {} (payload: {})", tag ? tag : "<null>", payload);
			} else {
				SKSE::log::info("anim: {}", tag ? tag : "<null>");
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	// The player's animation-graph manager loads with their 3D, which on a StartOnSave
	// autoload (skytest's path) is NOT ready when kPostLoadGame fires — so
	// AddAnimationGraphEventSink returns false and a one-shot registration silently never
	// attaches (the latent trap in AutoFireBow's copied pattern). Retry on the game thread
	// until the graph accepts the sink, bounded so a never-loading player can't spin forever.
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
		// Re-attach the anim sink for the freshly loaded player graph (deferred-safe).
		g_animSinkAdded = false;
		EnsureAnimSink(600);  // ~10s of game-thread ticks
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

// Declarative SKSE plugin metadata (CommonLibSSE-NG). Exported as
// SKSEPlugin_Version + SKSEPlugin_Query so SKSE recognises and loads the DLL.
SKSEPluginInfo(
	.Version = REL::Version{ 0, 1, 0 },
	.Name = "AutoCastSpell",
	.Author = "mase",
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

// Entry point SKSE calls after loading the plugin.
SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();
	SKSE::Init(a_skse);
	SKSE::log::info("AutoCastSpell {} loaded — probe build (logs cast-control held-state + anim events)",
		REL::Version{ 0, 1, 0 }.string());
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
