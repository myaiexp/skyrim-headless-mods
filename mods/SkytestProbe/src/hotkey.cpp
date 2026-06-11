#include "hotkey.h"

#include <SKSE/SKSE.h>

#include "config.h"
#include "engine.h"
#include "trace.h"

namespace
{
	void OnMarkerHotkey()
	{
		// ProcessEvent runs on the engine's input/main thread, so engine reads here
		// are safe without the task queue.
		trace::Write(trace::json{ { "src", "marker" }, { "note", "f11" } });

		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			engine::DumpActor(player, {}, "f11");
		}
		if (auto* ref = engine::GetCrosshairRef()) {
			if (auto* actor = ref->As<RE::Actor>()) {
				engine::DumpActor(actor, {}, "f11-crosshair");
			} else {
				trace::Write(trace::json{
					{ "src", "f11-crosshair" },
					{ "ref", engine::HexID(ref->GetFormID()) },
					{ "note", "crosshair target is not an actor" } });
			}
		}

		if (config::notifications) {
			RE::DebugNotification("SkytestProbe: marker dropped");
		}
		SKSE::log::info("SkytestProbe: F11 marker fired");
	}

	// Event type is InputEvent* (a pointer), so the sink's ProcessEvent takes
	// `InputEvent* const*` — a pointer to the linked-list head, matching the engine's
	// own MenuControls signature. Getting this type wrong silently fails to override.
	class HotkeySink : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static HotkeySink* GetSingleton()
		{
			static HotkeySink instance;
			return &instance;
		}

		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event,
			RE::BSTEventSource<RE::InputEvent*>*) override
		{
			if (!a_event) {
				return RE::BSEventNotifyControl::kContinue;
			}
			for (auto* e = *a_event; e; e = e->next) {
				if (e->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
					continue;
				}
				auto* button = e->AsButtonEvent();
				if (!button) {
					continue;
				}
				if (button->GetIDCode() != config::markerHotkey) {
					continue;
				}
				if (button->IsDown()) {  // press edge only (held-duration == 0)
					OnMarkerHotkey();
				}
			}
			return RE::BSEventNotifyControl::kContinue;  // never swallow input
		}

	private:
		HotkeySink() = default;
	};
}

void hotkey::Register()
{
	if (config::markerHotkey == 0) {
		SKSE::log::info("SkytestProbe: marker hotkey disabled (MarkerHotkey=0)");
		return;
	}
	auto* idm = RE::BSInputDeviceManager::GetSingleton();
	if (!idm) {
		SKSE::log::error("BSInputDeviceManager null; marker hotkey not registered");
		return;
	}
	idm->AddEventSink(HotkeySink::GetSingleton());
	SKSE::log::info("SkytestProbe: marker hotkey registered (DX 0x{:02X})", config::markerHotkey);
}
