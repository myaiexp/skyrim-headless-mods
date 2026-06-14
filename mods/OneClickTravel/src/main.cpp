#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <MinHook.h>

namespace
{
	constexpr REL::Version kVersion{ 1, 0, 0 };

	// OneClickTravel — one-click fast travel on the world map.
	//
	// Behaviour (scope reduced from the original design after the Task-2 in-game probe):
	//   When the player clicks a TRAVELABLE (discovered) location on the world map, fast-travel
	//   INSTANTLY — the vanilla "Fast travel to X? Yes / No / Place Marker" confirmation box
	//   (box #1) is suppressed before it ever renders, and the trip is driven directly.
	//
	// Everything else is left bit-for-bit vanilla. Non-travelable clicks (empty terrain,
	// undiscovered locations, marker placement / Move-Leave-Remove management) route entirely
	// through MapMenu::PlaceMarker (RELOCATION_ID(52226,53113)) which already does instant-place
	// and the manage box correctly — those clicks do not even reach this plugin. We do NOT hook
	// PlaceMarker and do NOT synthesise any marker popup.
	//
	// --- How box #1 is suppressed (clean, not flash-then-hide) --------------------------------
	//
	// The map click-handler at RELOCATION_ID(52208,53095), after formatting the prompt string,
	// constructs a MessageBoxData, stores a freshly-built FastTravelConfirmCallback in its
	// `callback` member, and calls MessageBoxData::QueueMessage() (RELOCATION_ID(51422,52271)) to
	// enqueue the box for rendering. QueueMessage is the single chokepoint that hands the box to
	// the UI — intercepting it lets us decide BEFORE anything is shown.
	//
	// We detour QueueMessage's entry (MinHook). On each call we inspect data->callback:
	//   * If it is a FastTravelConfirmCallback (vtable match) AND the marker under the cursor is
	//     travelable, we DRIVE the trip — call the callback's own Run(kUnk1) (kUnk1 == the
	//     "Yes / travel" answer; this is the proven travel-drive primitive, same as
	//     PapyrusExtender's ChangeFastTravelTarget), hide the MapMenu, and RETURN WITHOUT calling
	//     the original QueueMessage. The box is therefore never queued and never rendered — true
	//     suppression, strictly better than letting it flash then auto-dismissing it.
	//   * In every other case (non-FastTravel box, or a FastTravel box whose target somehow isn't
	//     travelable) we call the original QueueMessage unchanged, so the generic marker
	//     Move/Leave/Remove box and all unrelated game message boxes stay 100% vanilla.
	//
	// This is the only behaviour implemented. The design's documented fallback stands: any box for
	// an undiscovered/non-travelable target is left vanilla (it won't reach the travelable branch).

	void SetupLog()
	{
		auto logDir = SKSE::log::log_directory();
		if (!logDir) {
			return;
		}
		auto path = *logDir / "OneClickTravel.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
		auto logger = std::make_shared<spdlog::logger>("global", std::move(sink));
		logger->set_level(spdlog::level::info);
		logger->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(logger));
	}

	// Classify the REFR under the cursor: a map-marker REFR carries an ExtraMapMarker whose
	// mapData->flags tells us whether the marker is travelable (kCanTravelTo == discovered /
	// fast-travelable). Mirrors PapyrusExtender's GetMapMarkerFromObject. (Retained from the
	// Task-2 probe — header-verified.)
	//
	// The marker's display name (mapData->locationName.GetFullName()) is intentionally NOT read:
	// TESFullName::GetFullName is a non-inlined engine import this NG static lib does not export,
	// so referencing it fails the link. FormID identifies the marker for the log just as well.
	struct MarkerRead
	{
		bool       present{ false };
		bool       travelable{ false };
		RE::FormID formID{ 0 };
	};

	MarkerRead ReadCursorMarker(RE::TESObjectREFR* a_refr)
	{
		MarkerRead out;
		if (!a_refr) {
			return out;
		}
		out.formID = a_refr->GetFormID();
		if (const auto xMarker = a_refr->extraList.GetByType<RE::ExtraMapMarker>()) {
			if (const auto mapData = xMarker->mapData) {
				out.present = true;
				out.travelable = mapData->flags.any(RE::MapMarkerData::Flag::kCanTravelTo);
			}
		}
		return out;
	}

	// Drive the actual fast travel for a confirmed target. Run(kUnk1) is the "Yes / travel" answer
	// (per PapyrusExtender's ChangeFastTravelTarget, which keys confirmed travel off kUnk1); it
	// initiates the trip AND closes the map itself, exactly as pressing Yes does in vanilla.
	//
	// NB: we do NOT send our own kHide here. An extra kHide tears MapMenu down through the close
	// path before the queued fast-travel executes, dropping the trip (observed in-game: map closed,
	// no travel). The proven precedent only hides the menu on its CANCEL path, never on travel.
	void DriveTravel(RE::FastTravelConfirmCallback* a_callback)
	{
		a_callback->Run(RE::IMessageBoxCallback::Message::kUnk1);
	}

	// --- The suppression hook: MessageBoxData::QueueMessage entry detour -----------------------
	struct QueueMessageHook
	{
		static void thunk(RE::MessageBoxData* a_this)
		{
			if (a_this) {
				// Is this the fast-travel confirm box? Gate strictly on the callback's vtable so we
				// never touch any other game message box.
				auto* rawCallback = a_this->callback.get();
				REL::Relocation<std::uintptr_t> ftcbVtbl{ RE::FastTravelConfirmCallback::VTABLE[0] };
				const bool isFastTravelBox =
					rawCallback && *reinterpret_cast<std::uintptr_t*>(rawCallback) == ftcbVtbl.address();

				if (isFastTravelBox) {
					auto* ftCallback = static_cast<RE::FastTravelConfirmCallback*>(rawCallback);

					// Resolve the marker under the cursor via the callback's MapMenu, exactly as the
					// probe did, and classify it.
					RE::TESObjectREFR* cursorRefr = nullptr;
					if (auto* mapMenu = ftCallback->mapMenu) {
						cursorRefr = mapMenu->GetRuntimeData().mapMarker.get().get();
					}
					const MarkerRead cursor = ReadCursorMarker(cursorRefr);

					if (cursor.travelable) {
						// SUPPRESS-AND-ACT: drive the trip and never queue the box (Run closes the map).
						SKSE::log::info(
							"travelable click intercepted; suppressing confirm box and driving travel (target formID={:08X})",
							cursor.formID);
						DriveTravel(ftCallback);
						return;  // box #1 is never enqueued -> never rendered
					}

					// A fast-travel box whose target is not travelable: leave it vanilla. (Not
					// expected in practice — non-travelable clicks don't build a confirm box — but
					// handled explicitly so we only ever suppress a genuine travelable target.)
					SKSE::log::info(
						"fast-travel confirm box for non-travelable target (formID={:08X}); leaving vanilla",
						cursor.formID);
				}
			}

			// PASS-THROUGH: every non-travelable / non-fast-travel box renders unchanged.
			original(a_this);
		}

		// MinHook fills this with the trampoline to the real QueueMessage (relocated prologue + a
		// jump back past the stolen bytes). Calling original() runs the engine's original intact.
		static inline decltype(&thunk) original = nullptr;
	};

	void InstallHooks()
	{
		// Detour MessageBoxData::QueueMessage at its ENTRY with MinHook. QueueMessage is the single
		// chokepoint that hands a box to the UI, so intercepting it lets us suppress the fast-travel
		// confirm box BEFORE it renders (no flash) and pass every other box through untouched.
		//
		// MinHook — not NG's write_branch<5> — because write_branch<5> does NOT relocate a function
		// prologue: it decodes the bytes at the entry AS an existing `call/jmp rel32`, so its returned
		// pass-through pointer is a wild pointer that crashes on the first non-fast-travel box. MinHook
		// disassembles the real prologue, relocates it into a trampoline (stored in
		// QueueMessageHook::original), and writes a 5-byte jmp to our thunk — so original() is valid and
		// the pass-through is safe. Same idiom DBVODialogueTweaks uses, verified in-game.
		REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(51422, 52271) };
		if (auto s = MH_Initialize(); s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
			SKSE::log::error("OneClickTravel: MH_Initialize failed ({})", static_cast<int>(s));
			return;
		}
		if (auto s = MH_CreateHook(reinterpret_cast<LPVOID>(target.address()),
								   reinterpret_cast<LPVOID>(&QueueMessageHook::thunk),
								   reinterpret_cast<LPVOID*>(&QueueMessageHook::original));
			s != MH_OK) {
			SKSE::log::error("OneClickTravel: MH_CreateHook failed ({})", static_cast<int>(s));
			return;
		}
		if (auto s = MH_EnableHook(reinterpret_cast<LPVOID>(target.address())); s != MH_OK) {
			SKSE::log::error("OneClickTravel: MH_EnableHook failed ({})", static_cast<int>(s));
			return;
		}

		SKSE::log::info(
			"OneClickTravel: hooked MessageBoxData::QueueMessage (RELOCATION_ID(51422,52271)) via MinHook; "
			"travelable map clicks fast-travel instantly, all other boxes pass through vanilla");
	}
}

// Declarative SKSE plugin metadata (CommonLibSSE-NG). Exported as
// SKSEPlugin_Version + SKSEPlugin_Query so SKSE recognises and loads the DLL.
SKSEPluginInfo(
	.Version = kVersion,
	.Name = "OneClickTravel",
	.Author = "mase",
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

// Entry point SKSE calls after loading the plugin.
SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();
	SKSE::Init(a_skse);
	SKSE::log::info("OneClickTravel loaded (v{})", kVersion.string());

	InstallHooks();

	return true;
}
