#include "engine.h"

#include <cstdio>
#include <excpt.h>

#include <SKSE/SKSE.h>

#include "trace.h"

namespace
{
	// "teammates" is a set keyword; resolve_one returns null for it on purpose.
	bool IsSetKeyword(const std::string& a_ref)
	{
		return a_ref == "teammates";
	}

	// SEH-isolated wrapper around the fragile engine compile. Script::CompileAndRun
	// reaches into the console-compiler subsystem, which is uninitialized in a
	// console-less context (e.g. headless): it then calls through a garbage function
	// pointer -> EXCEPTION_ACCESS_VIOLATION. Catching the SEH access violation here
	// honors the design's "never crash the game on bad input" contract: in a normal
	// game this never fires (zero cost); in a console-less one exec degrades to an
	// error line. This function holds NO C++ objects needing unwinding (SEH rule).
	bool SafeCompileAndRun(RE::Script* a_script, RE::TESObjectREFR* a_target) noexcept
	{
		__try {
			a_script->CompileAndRun(a_target);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}
}

std::string engine::HexID(RE::FormID a_id)
{
	char buf[16];
	std::snprintf(buf, sizeof(buf), "0x%08X", a_id);
	return buf;
}

std::string engine::FormLabel(RE::FormID a_id)
{
	std::string hex = HexID(a_id);
	if (a_id == 0) {
		return hex;
	}
	if (auto* f = RE::TESForm::LookupByID(a_id)) {
		if (const char* nm = f->GetName(); nm && nm[0]) {
			return hex + " (" + nm + ")";
		}
	}
	return hex;
}

RE::TESObjectREFR* engine::ResolveOne(const std::string& a_ref)
{
	if (a_ref.empty() || IsSetKeyword(a_ref)) {
		return nullptr;
	}
	if (a_ref == "player") {
		return RE::PlayerCharacter::GetSingleton();  // PlayerCharacter is-a TESObjectREFR
	}
	if (a_ref == "crosshair") {
		return GetCrosshairRef();
	}

	// Otherwise parse as a hex runtime FormID ("0x14", "0X14", or bare "14").
	RE::FormID id = 0;
	try {
		id = static_cast<RE::FormID>(std::stoul(a_ref, nullptr, 16));
	} catch (const std::exception&) {
		return nullptr;  // not a keyword, not valid hex
	}
	if (id == 0) {
		return nullptr;
	}
	auto* form = RE::TESForm::LookupByID(id);
	return form ? form->As<RE::TESObjectREFR>() : nullptr;  // null for non-ref forms
}

std::vector<RE::Actor*> engine::ResolveSet(const std::string& a_ref)
{
	std::vector<RE::Actor*> out;
	if (a_ref != "teammates") {
		return out;
	}
	auto* lists = RE::ProcessLists::GetSingleton();
	if (!lists) {
		return out;  // no process lists yet (no save loaded) -> empty
	}
	auto* player = RE::PlayerCharacter::GetSingleton();
	for (auto& handle : lists->highActorHandles) {
		auto actor = handle.get();  // NiPointer<Actor>, null if stale
		RE::Actor* raw = actor.get();
		if (!raw || raw == player) {
			continue;
		}
		if (raw->IsPlayerTeammate()) {
			out.push_back(raw);
		}
	}
	return out;
}

std::unordered_set<RE::FormID> engine::ResolveToFormIDs(const std::vector<std::string>& a_refs)
{
	std::unordered_set<RE::FormID> ids;
	for (const auto& ref : a_refs) {
		if (IsSetKeyword(ref)) {
			for (auto* a : ResolveSet(ref)) {
				if (a) {
					ids.insert(a->GetFormID());
				}
			}
			continue;
		}
		if (auto* r = ResolveOne(ref)) {
			ids.insert(r->GetFormID());
		} else {
			// A hex FormID that isn't a currently-loaded ref is still a valid
			// filter target (events carry raw FormIDs): parse and add it directly.
			try {
				RE::FormID id = static_cast<RE::FormID>(std::stoul(ref, nullptr, 16));
				if (id != 0) {
					ids.insert(id);
				}
			} catch (const std::exception&) {
				// keyword that resolved to nothing (e.g. crosshair over nothing) — skip
			}
		}
	}
	return ids;
}

RE::TESObjectREFR* engine::GetCrosshairRef()
{
	auto* pick = RE::CrosshairPickData::GetSingleton();
	if (!pick) {
		return nullptr;  // not available at main menu / pre-load
	}
	if (!pick->target) {
		return nullptr;  // nothing highlighted (handle == 0)
	}
	RE::NiPointer<RE::TESObjectREFR> ref = pick->target.get();  // resolves handle
	return ref.get();  // null if the handle went stale
}

RE::ActorValue engine::ResolveActorValue(std::string_view a_name)
{
	auto* avl = RE::ActorValueList::GetSingleton();
	if (!avl) {
		return RE::ActorValue::kNone;
	}
	return avl->LookupActorValueByName(a_name);  // kNone if unknown (case-insensitive)
}

engine::WorldState engine::GetWorldState()
{
	WorldState w;
	auto* ui = RE::UI::GetSingleton();
	if (ui) {
		w.mainMenu    = ui->IsMenuOpen(RE::MainMenu::MENU_NAME);
		w.loadingMenu = ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
	}
	auto* player = RE::PlayerCharacter::GetSingleton();
	w.is3DLoaded  = player && player->Is3DLoaded();
	w.inWorld     = ui && !w.mainMenu && !w.loadingMenu && w.is3DLoaded;
	return w;
}

bool engine::IsInWorld()
{
	return GetWorldState().inWorld;
}

engine::ExecResult engine::RunConsoleCommand(const std::string& a_line)
{
	if (a_line.empty()) {
		return ExecResult::kEmpty;
	}
	// CompileAndRun needs a FULLY-LOADED, interactive world — at the main menu AND
	// during the loading screen the console-compiler globals are uninitialized and it
	// crashes (verified via headless smoke: EXCEPTION_ACCESS_VIOLATION in the compile
	// path, with SkytestProbe frames in the stack). IsInWorld() is that exact gate
	// (shared with `status` so exec and status can never disagree).
	if (!IsInWorld()) {
		return ExecResult::kNotInWorld;
	}
	// The ENGINE must construct the Script: `new RE::Script()` would force this TU
	// to emit the Script/TESForm vtable, whose virtuals are address-library engine
	// code (unresolved at link). The form factory hands back an engine-constructed
	// Script with a valid vtable (ConsoleUtilSSE-NG technique). delete is fine — the
	// virtual dtor dispatches through that engine vtable, needing no link symbol.
	auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
	auto* form = factory ? factory->Create() : nullptr;
	auto* script = form ? form->As<RE::Script>() : nullptr;
	if (!script) {
		return ExecResult::kFaulted;  // couldn't create the Script form
	}
	script->SetCommand(a_line);
	const bool ran = SafeCompileAndRun(script, nullptr);  // null target = no selected ref
	if (!ran) {
		// Don't delete after a caught AV: the form may be in an indeterminate state and
		// the virtual dtor dispatch could itself fault. Leaking one tiny Script is fine.
		SKSE::log::warn("exec: CompileAndRun faulted (no console subsystem?) for '{}'", a_line);
		return ExecResult::kFaulted;
	}
	delete script;
	return ExecResult::kOk;
}

void engine::DumpActor(RE::Actor* a_actor, const std::vector<std::string>& a_avs, const char* a_src)
{
	trace::json line{ { "src", a_src } };
	if (!a_actor) {
		line["error"] = "null actor";
		trace::Write(std::move(line));
		return;
	}

	line["ref"] = HexID(a_actor->GetFormID());
	trace::json f = trace::json::object();

	// (1) identity
	if (const char* disp = a_actor->GetDisplayFullName(); disp && *disp) {
		f["name"] = disp;
	} else if (const char* nm = a_actor->GetName(); nm && *nm) {
		f["name"] = nm;
	}
	if (auto* base = a_actor->GetActorBase()) {
		f["baseFormID"] = HexID(base->GetFormID());
		if (const char* bn = base->GetName(); bn && *bn) {
			f["baseName"] = bn;
		}
	}

	// (2) position + cell
	const RE::NiPoint3 pos = a_actor->GetPosition();
	f["pos"] = { pos.x, pos.y, pos.z };
	if (auto* cell = a_actor->GetParentCell()) {
		f["cellFormID"] = HexID(cell->GetFormID());
		if (const char* eid = cell->GetFormEditorID(); eid && *eid) {
			f["cellEditorID"] = eid;
		}
		f["cellInterior"] = cell->IsInteriorCell();
	}

	// (3) actor values: current (with temp modifiers) + permanent (base+perm).
	// In the AE layout Actor inherits only TESObjectREFR; the ActorValueOwner base
	// is reached via AsActorValueOwner() (relocated member, always non-null here).
	if (auto* avo = a_actor->AsActorValueOwner()) {
		f["health"]  = { { "cur", avo->GetActorValue(RE::ActorValue::kHealth) },
		                 { "perm", avo->GetPermanentActorValue(RE::ActorValue::kHealth) } };
		f["magicka"] = { { "cur", avo->GetActorValue(RE::ActorValue::kMagicka) },
		                 { "perm", avo->GetPermanentActorValue(RE::ActorValue::kMagicka) } };
		f["stamina"] = { { "cur", avo->GetActorValue(RE::ActorValue::kStamina) },
		                 { "perm", avo->GetPermanentActorValue(RE::ActorValue::kStamina) } };
		for (const auto& name : a_avs) {
			const RE::ActorValue av = ResolveActorValue(name);
			if (av == RE::ActorValue::kNone) {
				f["av"][name] = "unknown-av";
				continue;
			}
			f["av"][name] = { { "cur", avo->GetActorValue(av) },
			                  { "perm", avo->GetPermanentActorValue(av) } };
		}
	}

	// (4) active magic effects
	if (auto* mt = a_actor->GetMagicTarget()) {
		if (auto* effects = mt->GetActiveEffectList()) {
			trace::json arr = trace::json::array();
			for (RE::ActiveEffect* eff : *effects) {
				if (!eff) {
					continue;
				}
				trace::json e = trace::json::object();
				if (auto* mgef = eff->GetBaseObject()) {
					if (const char* en = mgef->GetName(); en && *en) {
						e["name"] = en;
					}
					e["hostile"] = mgef->IsHostile();
				}
				e["magnitude"] = eff->magnitude;
				e["duration"]  = eff->duration;
				e["elapsed"]   = eff->elapsedSeconds;
				arr.push_back(std::move(e));
			}
			if (!arr.empty()) {
				f["effects"] = std::move(arr);
			}
		}
	}

	// (5) equipped gear (hands + head/body/shield)
	if (auto* rh = a_actor->GetEquippedObject(false)) {
		if (const char* n = rh->GetName(); n && *n) {
			f["equip"]["rightHand"] = n;
		} else {
			f["equip"]["rightHand"] = HexID(rh->GetFormID());
		}
	}
	if (auto* lh = a_actor->GetEquippedObject(true)) {
		if (const char* n = lh->GetName(); n && *n) {
			f["equip"]["leftHand"] = n;
		} else {
			f["equip"]["leftHand"] = HexID(lh->GetFormID());
		}
	}
	using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
	const struct { const char* key; Slot slot; } armorSlots[] = {
		{ "head", Slot::kHead }, { "body", Slot::kBody }, { "shield", Slot::kShield }
	};
	for (const auto& s : armorSlots) {
		if (auto* armo = a_actor->GetWornArmor(s.slot)) {
			if (const char* n = armo->GetName(); n && *n) {
				f["armor"][s.key] = n;
			} else {
				f["armor"][s.key] = HexID(armo->GetFormID());
			}
		}
	}

	// (6) flags / 3D
	f["isPlayerTeammate"] = a_actor->IsPlayerTeammate();
	f["is3DLoaded"]       = a_actor->Is3DLoaded();

	// (7) char-controller collision group (GhostAllies pattern): top 16 bits of
	// collisionFilterInfo = systemGroup. Controller null when 3D not loaded.
	if (auto* ctrl = a_actor->GetCharController()) {
		std::uint32_t info = 0;
		ctrl->GetCollisionFilterInfo(info);
		f["collisionFilterInfo"] = HexID(info);
		f["systemGroup"]         = info >> 16;
	} else {
		f["charController"] = "none (3D unloaded)";
	}

	line["fields"] = std::move(f);
	trace::Write(std::move(line));
}
