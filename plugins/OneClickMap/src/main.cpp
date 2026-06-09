#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace
{
	constexpr REL::Version kVersion{ 1, 0, 0 };

	// Task-2 read-only reverse-engineering probe.
	//
	// Goal: prove that every signal the eventual three-way dispatch needs is reachable at the
	// MapMenu click-handler site that PapyrusExtender hooks (RELOCATION_ID(52208, 53095)), by
	// HOOKING and LOGGING only — nothing the player sees changes. Two hooks, both call the
	// original unchanged:
	//
	//  (A) FastTravelConfirmCallback::Run (vtable slot 0x1) — the canonical PapyrusExtender
	//      ChangeFastTravelTarget hook. This is where the click->confirm code path lands, and
	//      it is the one site that hands us ALL FOUR reads at once:
	//        4. the FastTravelConfirmCallback instance itself is `a_this` (reachable, trivially);
	//        1. the cursor-target REFR via a_this->mapMenu->GetRuntimeData().mapMarker, classified
	//           travelable via ExtraMapMarker -> MapMarkerData::Flag::kCanTravelTo;
	//        2. whether a custom marker exists via PlayerCharacter::playerMapMarker.get();
	//        + which button (a_msg) the player pressed (kUnk1 == Yes/travel).
	//
	//  (B) a write_call patch at the EXACT 52208/53095 + OFFSET_3(0x342,0x3A6,0x3D9) site that
	//      PapyrusExtender's GetFastTravelTarget patches (the prompt-format call). This proves the
	//      literal call site is reachable/patchable in our build; it logs the prompt-build target
	//      name and calls the original format function unchanged.
	//
	// Read 3 (popup #4: "Move It / Leave It / Remove It") cannot be tied to a dedicated callback
	// CLASS from the headers — the CommonLibSSE-NG headers expose exactly one map-related
	// IMessageBoxCallback subclass (FastTravelConfirmCallback); the marker move/leave/remove box
	// is a GENERIC engine message box raised inline inside MapMenu::PlaceMarker()
	// (RELOCATION_ID(52226, 53113)), via the generic OldMessageBoxCallback (a bare function-pointer
	// callback), not a named RTTI class. That fact is logged at install time (see InstallHooks).

	void SetupLog()
	{
		auto logDir = SKSE::log::log_directory();
		if (!logDir) {
			return;
		}
		auto path = *logDir / "OneClickMap.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
		auto logger = std::make_shared<spdlog::logger>("global", std::move(sink));
		// Probe lines are emitted at debug level; raise the sink so they are captured. The load
		// banner stays at info and is therefore still recorded.
		logger->set_level(spdlog::level::debug);
		logger->flush_on(spdlog::level::debug);
		spdlog::set_default_logger(std::move(logger));
	}

	// --- read 1: classify the REFR under the cursor -------------------------------
	//
	// Mirrors PapyrusExtender's GetMapMarkerFromObject: a map-marker REFR carries an
	// ExtraMapMarker whose mapData->flags tells us whether the marker is travelable
	// (kCanTravelTo set == discovered/fast-travelable).
	struct MarkerRead
	{
		bool       present{ false };
		bool       travelable{ false };
		RE::FormID formID{ 0 };
	};

	// Note: the marker's display name (mapData->locationName.GetFullName()) is intentionally NOT
	// read — TESFullName::GetFullName is a non-inlined engine import this NG static lib does not
	// export, so referencing it fails the link. The marker FormID below identifies the marker for
	// the probe just as well, and none of the four required reads needs the name string.
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

	// --- hook (A): FastTravelConfirmCallback::Run (vtable slot 0x1) ----------------
	//
	// READ-ONLY: logs the four reads, then calls the original Run unchanged. Suppresses nothing,
	// triggers no travel, places no marker.
	struct ProbeRun
	{
		static void thunk(RE::FastTravelConfirmCallback* a_this, RE::IMessageBoxCallback::Message a_message)
		{
			// read 4: the FastTravelConfirmCallback instance is reachable here — it IS a_this.
			// read 1: cursor-target REFR via the MapMenu runtime mapMarker handle.
			RE::TESObjectREFR* cursorRefr = nullptr;
			RE::MapMenu*       mapMenu = a_this ? a_this->mapMenu : nullptr;
			if (mapMenu) {
				cursorRefr = mapMenu->GetRuntimeData().mapMarker.get().get();
			}
			const MarkerRead cursor = ReadCursorMarker(cursorRefr);

			// read 2: does a custom (player-set) marker exist?
			bool       customMarker = false;
			RE::FormID customMarkerID = 0;
			if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
				if (auto refr = pc->GetInfoRuntimeData().playerMapMarker.get()) {
					customMarker = true;
					customMarkerID = refr->GetFormID();
				}
			}

			SKSE::log::debug(
				"[probe:Run] button={} | callback={} mapMenu={} | cursorMarker: present={} travelable={} formID={:08X} | customMarker: exists={} formID={:08X}",
				static_cast<int>(a_message),
				static_cast<const void*>(a_this),
				static_cast<const void*>(mapMenu),
				cursor.present, cursor.travelable, cursor.formID,
				customMarker, customMarkerID);

			func(a_this, a_message);  // original Run — unchanged behaviour
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// --- hook (B): the literal 52208/53095 prompt-format call site -----------------
	//
	// PapyrusExtender patches a (BSString*, const char* template, const char* target) format call
	// at RELOCATION_ID(52208, 53095) + OFFSET_3(0x342, 0x3A6, 0x3D9). a_target is the candidate
	// location name when the click leads to building the fast-travel confirm prompt. READ-ONLY:
	// logs the target, calls the original format function unchanged.
	struct ProbePromptFormat
	{
		static int thunk(RE::BSString* a_buffer, const char* a_template, const char* a_target)
		{
			SKSE::log::debug("[probe:prompt] format call hit at 52208/53095 site | target=\"{}\"",
				a_target ? a_target : "<null>");
			return func(a_buffer, a_template, a_target);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	void InstallHooks()
	{
		// (A) vfunc hook on FastTravelConfirmCallback::Run, slot 0x1 — the in-repo idiom
		// (GhostAllies) for a vtable write. Run's slot 0x1 is stable across SE/AE.
		REL::Relocation<std::uintptr_t> vtbl{ RE::FastTravelConfirmCallback::VTABLE[0] };
		ProbeRun::func = vtbl.write_vfunc(0x1, ProbeRun::thunk);
		SKSE::log::info("OneClickMap probe: hooked FastTravelConfirmCallback::Run (vtable slot 0x1)");

		// (B) write_call at the exact 52208/53095 prompt-format site. Requires a trampoline; the
		// 14-byte reserve covers one rel32 call rewrite. VariantOffset(SE, AE, VR) is NG's
		// equivalent of PapyrusExtender's OFFSET_3 (VR field inert — VR is disabled in this build).
		SKSE::AllocTrampoline(64);
		REL::Relocation<std::uintptr_t> mapClick{ RELOCATION_ID(52208, 53095),
			REL::VariantOffset(0x342, 0x3A6, 0x3D9) };
		ProbePromptFormat::func =
			SKSE::GetTrampoline().write_call<5>(mapClick.address(), ProbePromptFormat::thunk);
		SKSE::log::info("OneClickMap probe: hooked prompt-format call at RELOCATION_ID(52208,53095)+OFFSET_3(0x342,0x3A6,0x3D9)");

		// Read 3 finding, recorded at install time: the "Move It / Leave It / Remove It" custom-
		// marker popup has NO dedicated IMessageBoxCallback subclass in the headers. The only
		// map-related callback class is FastTravelConfirmCallback; the marker box is a generic
		// engine message box raised inside MapMenu::PlaceMarker() (RELOCATION_ID(52226, 53113))
		// via the generic OldMessageBoxCallback. So popup #4 is NOT locatable as a distinct
		// class/address from headers — it lives inside PlaceMarker's code path.
		SKSE::log::info("OneClickMap probe: popup#4 (Move/Leave/Remove) has no dedicated callback class; "
						"it is a generic box inside MapMenu::PlaceMarker (RELOCATION_ID(52226,53113)).");
	}
}

// Declarative SKSE plugin metadata (CommonLibSSE-NG). Exported as
// SKSEPlugin_Version + SKSEPlugin_Query so SKSE recognises and loads the DLL.
SKSEPluginInfo(
	.Version = kVersion,
	.Name = "OneClickMap",
	.Author = "mase",
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

// Entry point SKSE calls after loading the plugin.
SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();
	SKSE::Init(a_skse);
	SKSE::log::info("OneClickMap loaded (v{})", kVersion.string());

	InstallHooks();

	return true;
}
