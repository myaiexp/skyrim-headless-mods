#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <MinHook.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace
{
	constexpr auto kVersion = REL::Version{ 1, 0, 0 };

	// Volume scale applied to the player's own DBVO voice line, pushed in from Papyrus via the
	// SetPlayerVoiceVolume native (MCM slider). 1.0 = vanilla (the hook is a pure pass-through
	// at this value), 0.0 = silent, 2.0 = double. atomic because the Papyrus VM thread writes it
	// while the game thread (inside the speak-sound hook) reads it.
	static std::atomic<float> g_dbvoVolume{ 1.0f };

	// Retained copy of the player's CURRENTLY-PLAYING DBVO line handle, captured in the speak-sound
	// hook below. DBVO plays the player line via console Player.SpeakSound, a free-standing
	// BSSoundHandle the swf/Papyrus can't reach — so to cut it on skip we keep a by-value copy here
	// (the handle is a 12-byte POD whose identity is soundID; every method re-resolves the live sound
	// by that id in BSAudioManager, so a copy made earlier still controls the line). Each new player
	// line overwrites it, so only the most-recent line is cuttable — exactly the skip scope. Guarded
	// by a plain mutex (POD copy, not lock-free): the game thread writes it in the hook, the
	// mod-event sink thread reads+cuts it on skip.
	static std::mutex g_playerLineMtx;
	static RE::BSSoundHandle g_playerLine;

	// v5 — reply-on-line-end watcher state. The speak-sound hook ARMS when it captures a player DBVO
	// line; a single detached poll thread (ReplyWatchThread, started at kDataLoaded) watches the
	// retained handle and, on the playing→stopped transition, marshals the swf "fire reply" call to
	// the main thread via ONE AddTask. The poll lives OFF the main thread with an explicit sleep —
	// an earlier main-thread self-re-arming AddTask loop FROZE the game for the whole line, because
	// SKSE drains its task queue to empty, so a task that re-queues itself spins the frame. IsValid/
	// IsPlaying are soundID-keyed audio-manager calls safe off the main thread (v4's CutPlayerLine
	// precedent); only the GFx invoke must be on the main thread. atomics: the hook arms, the poll
	// thread reads+clears, the skip path + menu-close sink disarm. g_sawPlaying latches once the
	// handle is seen playing, so the arm-before-audio-starts sliver can't read as "already ended".
	static std::atomic<bool> g_replyArmed{ false };
	static std::atomic<bool> g_sawPlaying{ false };

	// Route spdlog (what SKSE::log::* uses) to <My Games>/SKSE/DBVODialogueTweaks.log so the
	// plugin leaves a visible trace that it loaded.
	void SetupLog()
	{
		auto logDir = SKSE::log::log_directory();
		if (!logDir) {
			return;
		}
		auto path = *logDir / "DBVODialogueTweaks.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
		auto logger = std::make_shared<spdlog::logger>("global", std::move(sink));
		logger->set_level(spdlog::level::info);
		logger->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(logger));
	}

	// case-INSENSITIVE: true iff a_path begins with "DBVO/" or "DBVO\". DBVO voice lines are
	// dispatched as Player.SpeakSound "DBVO/<file>.fuz"; this is the gate that limits the volume
	// scale to that voice pack and leaves all other speak-sound calls untouched. Tiny manual
	// compare — no heavy string deps.
	bool is_dbvo_path(const char* a_path)
	{
		if (!a_path) {
			return false;
		}
		static constexpr char kPrefix[4] = { 'd', 'b', 'v', 'o' };
		for (int i = 0; i < 4; ++i) {
			const char c = a_path[i];
			if (c == '\0') {
				return false;  // shorter than "DBVO" — bail before reading the separator
			}
			const char lc = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
			if (lc != kPrefix[i]) {
				return false;
			}
		}
		const char sep = a_path[4];
		return sep == '/' || sep == '\\';
	}

	// Papyrus native: DBVOTweaks.SetPlayerVoiceVolume(Float factor). Just stores the factor; the
	// speak-sound hook reads it. The class string "DBVOTweaks" MUST match the Papyrus script
	// (Scriptname DBVOTweaks Hidden / Function SetPlayerVoiceVolume(Float factor) Global Native).
	void SetPlayerVoiceVolume(RE::StaticFunctionTag*, float factor)
	{
		g_dbvoVolume = factor;
	}

	bool RegisterFuncs(RE::BSScript::IVirtualMachine* vm)
	{
		vm->RegisterFunction("SetPlayerVoiceVolume", "DBVOTweaks", SetPlayerVoiceVolume);
		SKSE::log::info("DBVOTweaks.SetPlayerVoiceVolume native registered");
		return true;
	}

	// MinHook entry detour on the NON-virtual Actor::SpeakSoundFunction (the one the console's
	// Player.SpeakSound dispatches into — it fills a_handle and STARTS it synchronously inside the
	// call). We call the original FIRST so the engine builds + plays the handle (lip-sync intact),
	// then — only for the player's own DBVO line — scale the live handle's volume. a4–a14 are
	// emotion/2D/lip/queue flags carried through untouched (widths per the TiltedEvolution id 37542
	// reference). We never retain a_handle past the call; the engine owns it.
	struct SpeakSoundHook
	{
		static bool thunk(RE::Actor* a_this, const char* a_path, RE::BSSoundHandle* a_handle,
			std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
			std::uint64_t a7, std::uint64_t a8, std::uint64_t a9,
			bool a10, std::uint64_t a11, bool a12, bool a13, bool a14)
		{
			const bool r = original(a_this, a_path, a_handle, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14);
			if (a_this && a_this->IsPlayerRef() && a_path && a_handle && a_handle->IsValid() && is_dbvo_path(a_path)) {
				a_handle->SetVolume(g_dbvoVolume.load());
				{
					// Keep this line's handle so a later skip can cut it (see g_playerLine).
					std::scoped_lock l{ g_playerLineMtx };
					g_playerLine = *a_handle;
				}
				// Arm the v5 reply watcher for this line: reset the start-race latch, then mark armed.
				// The detached poll thread is already running (started at kDataLoaded) and picks this
				// up on its next tick — nothing is scheduled on the hook thread.
				g_sawPlaying = false;
				g_replyArmed = true;
			}
			return r;
		}

		// MinHook fills this with the trampoline to the real SpeakSoundFunction (relocated
		// prologue + jump back past the stolen bytes). Calling original() runs the engine's
		// original function; the thunk wraps it.
		static inline decltype(&thunk) original = nullptr;
	};

	// Install the speak-sound entry hook at load (matching the sibling mods' at-load InstallHooks
	// idiom). Addressed ONLY via Address Library id (SE 36541 / AE 37542) — no hardcoded offset, so
	// one NG-built DLL resolves the right target per runtime. MinHook disassembles whatever prologue
	// is at the entry, relocates it into a trampoline (stored in SpeakSoundHook::original), and
	// writes a 5-byte jmp to our thunk — so calling original() runs the real function intact.
	void InstallHooks()
	{
		REL::Relocation<std::uintptr_t> target{ REL::RelocationID(36541, 37542) };
		if (auto s = MH_Initialize(); s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
			SKSE::log::error("DBVODialogueTweaks: MH_Initialize failed ({})", static_cast<int>(s));
			return;
		}
		if (auto s = MH_CreateHook(reinterpret_cast<LPVOID>(target.address()),
								   reinterpret_cast<LPVOID>(&SpeakSoundHook::thunk),
								   reinterpret_cast<LPVOID*>(&SpeakSoundHook::original));
			s != MH_OK) {
			SKSE::log::error("DBVODialogueTweaks: MH_CreateHook failed ({})", static_cast<int>(s));
			return;
		}
		if (auto s = MH_EnableHook(reinterpret_cast<LPVOID>(target.address())); s != MH_OK) {
			SKSE::log::error("DBVODialogueTweaks: MH_EnableHook failed ({})", static_cast<int>(s));
			return;
		}
		SKSE::log::info("DBVODialogueTweaks: speak-sound entry hook installed (MinHook)");
	}

	// Fire the NPC reply now (v5): invoke the swf method that clears the backstop timer and schedules
	// topicClicked after the gap slider. Main thread only (Scaleform isn't thread-safe) — it only ever
	// runs as an AddTask callback. GetMenu returns null when the dialogue menu isn't open, so a late or
	// spurious call is a safe no-op.
	void FireReplyNow()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return;
		}
		auto menu = ui->GetMenu(RE::DialogueMenu::MENU_NAME);
		if (!menu || !menu->uiMovie) {
			return;
		}
		menu->uiMovie->InvokeNoReturn("_root.DialogueMenu_mc.dbvoOnPlayerLineEnded", nullptr, 0);
	}

	// The v5 reply watcher — a single detached thread started once at kDataLoaded, looping for the
	// game's lifetime (like SkytestProbe's poll thread). It sleeps every tick so it never saturates a
	// core and is a near-no-op while idle. While armed it polls the retained handle off the main
	// thread; on the observed playing→stopped transition it claims the fire exactly once (atomic
	// exchange) and marshals FireReplyNow to the main thread via ONE AddTask (Scaleform must be touched
	// there). ~30 ms cadence adds at most a frame of latency to the gap — negligible vs the gap slider.
	void ReplyWatchThread()
	{
		using namespace std::chrono_literals;
		for (;;) {
			std::this_thread::sleep_for(30ms);
			if (!g_replyArmed.load()) {
				continue;
			}
			bool playing = false;
			{
				std::scoped_lock l{ g_playerLineMtx };
				playing = g_playerLine.IsValid() && g_playerLine.IsPlaying();
			}
			if (playing) {
				g_sawPlaying = true;
				continue;
			}
			if (g_sawPlaying.load() && g_replyArmed.exchange(false)) {
				// playing→stopped after the line was seen playing: fire the reply once.
				if (auto* task = SKSE::GetTaskInterface()) {
					task->AddTask([]() { FireReplyNow(); });
				}
			}
		}
	}

	// Cut the player's own in-flight DBVO line (fired when v1's skip advances the menu). Fade the
	// retained handle rather than hard-Stop (a mid-waveform Stop clicks; a short fade doesn't) and
	// reset the slot so we never re-cut a recycled soundID. FadeOutAndRelease/IsPlaying are
	// soundID-keyed audio-manager calls that enqueue onto the audio worker thread, so this is safe
	// to call directly from the mod-event sink thread (no task-interface marshalling needed).
	void CutPlayerLine()
	{
		// Skip already advanced the menu (trySkipPlayerLine clears the timer and calls topicClicked
		// itself), so stand the reply watcher down — otherwise the faded handle's playing→stopped
		// transition would fire a SECOND reply.
		g_replyArmed = false;
		std::scoped_lock l{ g_playerLineMtx };
		if (g_playerLine.IsValid() && g_playerLine.IsPlaying()) {
			g_playerLine.FadeOutAndRelease(30);
		}
		g_playerLine = RE::BSSoundHandle{};
	}

	// Cut the NPC's in-flight reply (player picked a NEW topic mid-reply). The per-line topic voice
	// handle lives on the speaking actor's ExtraSayToTopicInfo extra-data (.sound) — NOT on
	// HighProcessData::soundHandles (those stay empty for topic voice) and NOT reachable by
	// PauseCurrentDialogue alone (it only PAUSES — see ExtraSayToTopicInfo::voicePaused — so a
	// multi-segment reply keeps sounding). Both dead-ends were tried in-game. So: fade that .sound
	// to silence the segment that's PLAYING, AND PauseCurrentDialogue to stop the reply advancing to
	// further segments — the fade kills what's playing, Pause kills what's next. Marshalled onto the
	// main game thread (raw engine state). speaker falls back to lastSpeaker (menu mid-close, NPC
	// still talking); the IsPlaying guard makes this a no-op when no reply is in flight.
	void CutNpcReply()
	{
		auto* task = SKSE::GetTaskInterface();
		if (!task) {
			return;
		}
		task->AddTask([]() {
			auto* mtm = RE::MenuTopicManager::GetSingleton();
			if (!mtm) {
				return;
			}
			auto ref = mtm->speaker.get();
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (!actor) {
				auto lref = mtm->lastSpeaker.get();
				actor = lref ? lref->As<RE::Actor>() : nullptr;
			}
			if (!actor) {
				return;
			}
			if (auto* say = actor->extraList.GetByType<RE::ExtraSayToTopicInfo>()) {
				if (say->sound.IsValid() && say->sound.IsPlaying()) {
					say->sound.FadeOutAndRelease(30);
				}
			}
			actor->PauseCurrentDialogue();

			// Cutting the audio leaves the NPC's mouth frozen open: the phoneme keyframe holds its
			// last (mid-word) values and nothing zeroes them until the speaking state times out
			// (~1-2s). Two things make the reset actually stick (a plain reset ran on valid data but
			// did nothing):
			//   - SetSpeakingDone(true) stops the engine's face pump re-driving the phonemes;
			//   - the reset runs UNDER faceGen->lock (the lip-sync writer holds that spinlock while it
			//     drives the keyframes — without it our write races it and loses), with a_timer 0.0 to
			//     SNAP (any ease gives the next frame a window to re-clobber the open mouth).
			//     resetExpression = emotion, resetModifierAndPhoneme = mouth shape.
			// Verbatim PhotoMode/OStimNG full-face-revert pattern.
			actor->SetSpeakingDone(true);
			if (auto* faceGen = actor->GetFaceGenAnimationData()) {
				RE::BSSpinLockGuard locker(faceGen->lock);
				faceGen->ClearExpressionOverride();
				faceGen->Reset(0.0f, true, true, true, false);
			}
		});
	}

	// One sink for the swf's skip mod events — no Papyrus relay. DialogueMenu.swf fires
	// "CutPlayerDBVOLine" (player skip) and "CutNpcDBVOReply" (new-topic select) via
	// skse.SendModEvent; we dispatch each to its cut. Registered at kDataLoaded (the mod-callback
	// event source isn't up before game data loads).
	class DBVOEventSink : public RE::BSTEventSink<SKSE::ModCallbackEvent>
	{
	public:
		static DBVOEventSink* GetSingleton()
		{
			static DBVOEventSink singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event,
			RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
		{
			if (a_event) {
				if (a_event->eventName == "CutPlayerDBVOLine") {
					CutPlayerLine();
				} else if (a_event->eventName == "CutNpcDBVOReply") {
					CutNpcReply();
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}

	private:
		DBVOEventSink() = default;
	};

	// Disarm the v5 reply watcher when the dialogue menu closes — covers exiting dialogue mid-line, so a
	// stale arm can't leak into the next conversation or keep a tick chain alive on a line that outlives
	// the menu. (Firing into a closed menu is already a no-op via FireReplyNow's null-guard; this just
	// stops the watcher promptly.) Registered at kDataLoaded alongside the mod-event sink.
	class DBVOMenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static DBVOMenuSink* GetSingleton()
		{
			static DBVOMenuSink singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
			RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event && !a_event->opening && a_event->menuName == RE::DialogueMenu::MENU_NAME) {
				g_replyArmed = false;
			}
			return RE::BSEventNotifyControl::kContinue;
		}

	private:
		DBVOMenuSink() = default;
	};
}

// Declarative SKSE plugin metadata (CommonLibSSE-NG). Exported as SKSEPlugin_Version +
// SKSEPlugin_Query so SKSE recognises and loads the DLL.
SKSEPluginInfo(
	.Version = kVersion,
	.Name = "DBVODialogueTweaks",
	.Author = "mase",
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

// Entry point SKSE calls after loading the plugin.
SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();
	SKSE::Init(a_skse);
	SKSE::log::info("DBVODialogueTweaks {} loaded", kVersion.string());

	InstallHooks();

	if (auto* papyrus = SKSE::GetPapyrusInterface()) {
		if (!papyrus->Register(RegisterFuncs)) {
			SKSE::log::error("DBVODialogueTweaks: Papyrus Register returned false");
		}
	} else {
		SKSE::log::error("DBVODialogueTweaks: Papyrus interface is null");
	}

	// Hook the swf's skip mod events into the cut functions. The mod-callback event source isn't
	// live until game data has loaded, so defer AddEventSink to the kDataLoaded message.
	if (auto* messaging = SKSE::GetMessagingInterface()) {
		messaging->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
			if (a_msg && a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
				if (auto* source = SKSE::GetModCallbackEventSource()) {
					source->AddEventSink(DBVOEventSink::GetSingleton());
					SKSE::log::info("DBVODialogueTweaks: skip-cut mod-event sink registered");
				}
				if (auto* ui = RE::UI::GetSingleton()) {
					ui->AddEventSink<RE::MenuOpenCloseEvent>(DBVOMenuSink::GetSingleton());
					SKSE::log::info("DBVODialogueTweaks: dialogue-menu close sink registered");
				}
				// Start the single detached reply-watch poll thread (kDataLoaded fires once per
				// process, so this runs exactly once). Off-main-thread polling — see ReplyWatchThread.
				std::thread(ReplyWatchThread).detach();
				SKSE::log::info("DBVODialogueTweaks: reply-watch poll thread started");
			}
		});
	} else {
		SKSE::log::error("DBVODialogueTweaks: Messaging interface is null");
	}

	return true;
}
