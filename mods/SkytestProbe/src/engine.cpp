#include "engine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <excpt.h>
#include <thread>

// EXCEPTION_POINTERS for the exec-fault SEH capture. NOGDI keeps wingdi.h out so its
// `GetObject` A/W macro can't clobber RE::BSScript::Variable::GetObject() below.
#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#	define NOGDI
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include <SKSE/SKSE.h>

#include "trace.h"

namespace
{
	// "teammates" is a set keyword; resolve_one returns null for it on purpose.
	bool IsSetKeyword(const std::string& a_ref)
	{
		return a_ref == "teammates";
	}

	// SEH-isolated wrapper around the fragile engine compile. The SEH filter CAPTURES the
	// fault (code / faulting instruction / accessed address) into a POD and LogExecFault
	// records it. PINNED 2026-06-16: this AVs at SkyrimSE.exe+0xce9843 reading -1, on game
	// 1.6.1170. It is NOT a headless or "missing console subsystem" issue — ConsoleLog is
	// non-null and the AV reproduces with the console menu open too. Root cause is a
	// dependency version skew: this CommonLibSSE-NG (v3.7.0-129, Sep 2024) PREDATES the
	// 1.6.1170 runtime, and CompileAndRun's bound Address Library id (AE 21890) is ABSENT
	// from the 1.6.1170 versionlib. CommonLib's non-VR id lookup (REL/ID.h) does not verify
	// the matched id, so a missing id silently resolves to the NEXT id and calls the wrong
	// function -> AV. exec is therefore mis-bound on this version; stage via direct-call
	// probe commands (give-spell/set-av/...), which is the harness design, not a fallback.
	// LogExecFault runs OUTSIDE the SEH frame; this function constructs no C++ object needing
	// unwinding (SEH rule). Catching the AV honors the "never crash the game on bad input"
	// contract — in a CommonLib build that matches the runtime, exec would run for real.
	struct ExecFault
	{
		std::uint32_t  code       = 0;
		std::uintptr_t ip         = 0;           // faulting instruction address
		std::uintptr_t access     = 0;           // address the faulting insn touched (AV)
		std::uint32_t  accessKind = 0xFFFFFFFF;  // 0 read, 1 write, 8 execute
		bool           hit        = false;
	};

	int CaptureExecFault(::EXCEPTION_POINTERS* a_ep, ExecFault& a_out)
	{
		a_out.hit = true;
		if (a_ep && a_ep->ExceptionRecord) {
			const auto* rec = a_ep->ExceptionRecord;
			a_out.code = static_cast<std::uint32_t>(rec->ExceptionCode);
			a_out.ip   = reinterpret_cast<std::uintptr_t>(rec->ExceptionAddress);
			if (rec->NumberParameters >= 2) {
				a_out.accessKind = static_cast<std::uint32_t>(rec->ExceptionInformation[0]);
				a_out.access     = static_cast<std::uintptr_t>(rec->ExceptionInformation[1]);
			}
		}
		return EXCEPTION_EXECUTE_HANDLER;
	}

	void LogExecFault(const ExecFault& a_f)
	{
		const std::uintptr_t base = REL::Module::get().base();
		SKSE::log::warn(
			"exec FAULT: code={:#010x} ip={:#x} module_base={:#x} ip_rva={:#x} access_addr={:#x} access_kind={}",
			a_f.code, a_f.ip, base, (a_f.ip >= base ? a_f.ip - base : std::uintptr_t{ 0 }),
			a_f.access, a_f.accessKind);
	}

	bool SafeCompileAndRun(RE::Script* a_script, RE::TESObjectREFR* a_target) noexcept
	{
		ExecFault fault;  // POD; filled by the SEH filter if CompileAndRun AVs
		__try {
			a_script->CompileAndRun(a_target);
		} __except (CaptureExecFault(GetExceptionInformation(), fault)) {
			// details logged below, outside the SEH frame
		}
		if (!fault.hit) {
			return true;
		}
		LogExecFault(fault);
		return false;
	}

	// ---- MCM (SkyUI) bound-script reveal ----------------------------------------
	// Find the first quest whose attached Papyrus object is (or derives from) a_className.
	// FindBoundObject is hierarchy-aware, but THIS helper is an exact single-class match
	// over all quests: it returns the first hit, so feed it a concrete script name
	// (e.g. "SKI_ConfigManager"). For a base-class sweep across every quest the scan
	// path iterates inline instead. Null-safe at every step (no VM / no data handler /
	// no policy -> empty smart-ptr).
	RE::BSTSmartPointer<RE::BSScript::Object> FindBoundScript(const char* a_className)
	{
		RE::BSTSmartPointer<RE::BSScript::Object> empty;
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm) {
			return empty;
		}
		auto* policy = vm->GetObjectHandlePolicy();
		if (!policy) {
			return empty;
		}
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh) {
			return empty;
		}
		for (auto* quest : dh->GetFormArray<RE::TESQuest>()) {
			if (!quest) {
				continue;
			}
			const RE::VMHandle handle = policy->GetHandleForObject(RE::FormType::Quest, quest);
			RE::BSTSmartPointer<RE::BSScript::Object> obj;
			if (vm->FindBoundObject(handle, a_className, obj) && obj) {
				return obj;
			}
		}
		return empty;
	}

	// The script class name as registered with the VM (the config's actual subclass,
	// e.g. "SkyUI_SE" or a mod's own SKI_ConfigBase subclass). Empty string if unknown.
	std::string ConfigClassName(const RE::BSScript::Object* a_obj)
	{
		if (!a_obj) {
			return {};
		}
		if (auto* ti = a_obj->GetTypeInfo()) {
			if (const char* nm = ti->GetName(); nm && *nm) {
				return nm;
			}
		}
		return {};
	}

	// Read a config object's ModName (property, string) + Pages (property, string array)
	// + class name, appending one {"name","script","pages":[...]} entry to a_mods.
	// Skips a null object. ModName is "" when absent; Pages is [] when absent/not an array.
	void AppendConfigEntry(const RE::BSScript::Object* a_obj, trace::json& a_mods)
	{
		if (!a_obj) {
			return;
		}
		trace::json entry = trace::json::object();

		std::string modName;
		if (const auto* p = a_obj->GetProperty("ModName"); p && p->IsString()) {
			modName = std::string(p->GetString());
		}
		entry["name"]   = modName;
		entry["script"] = ConfigClassName(a_obj);  // load-bearing: the key mcm-get consumes

		trace::json pages = trace::json::array();
		if (const auto* p = a_obj->GetProperty("Pages"); p && p->IsArray()) {
			if (auto arr = p->GetArray()) {
				for (std::uint32_t i = 0; i < arr->size(); ++i) {
					const auto& v = (*arr)[i];
					if (v.IsString()) {
						pages.push_back(std::string(v.GetString()));
					}
				}
			}
		}
		entry["pages"] = std::move(pages);

		a_mods.push_back(std::move(entry));
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
	if (a_ref == "speaker") {
		// The current dialogue speaker (the talking NPC), resolved exactly as the v4
		// CutNpcReply does: MenuTopicManager::speaker, falling back to lastSpeaker when
		// the menu has closed but the NPC is still finishing a line.
		if (auto* mtm = RE::MenuTopicManager::GetSingleton()) {
			if (auto sp = mtm->speaker.get()) {
				return sp.get();
			}
			if (auto sp = mtm->lastSpeaker.get()) {
				return sp.get();
			}
		}
		return nullptr;
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

bool engine::IsMenuOpen(const std::string& a_menu)
{
	auto* ui = RE::UI::GetSingleton();
	if (!ui) {
		return false;  // pre-load: UI singleton not up yet
	}
	return ui->IsMenuOpen(a_menu);
}

engine::ExecResult engine::RunConsoleCommand(const std::string& a_line)
{
	if (a_line.empty()) {
		return ExecResult::kEmpty;
	}
	// CompileAndRun needs a FULLY-LOADED, interactive world — at the main menu / loading
	// screen the console-compiler globals are uninitialized and it crashes. IsInWorld() is
	// that exact gate (shared with `status` so exec and status can never disagree). NB: even
	// past this gate, exec faults on game 1.6.1170 — but NOT because of headless or "in-world"
	// state: CommonLib mis-binds CompileAndRun on this runtime (stale AE id 21890, see
	// SafeCompileAndRun above). The gate is still correct; the fault is the dependency skew.
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
	// Pre-call state, kept as permanent diagnostics. (This DISPROVED the "missing console
	// subsystem" theory: consolelog is non-null here and exec faults with the menu open too;
	// the real cause is the stale CommonLib binding — see SafeCompileAndRun.)
	SKSE::log::info("exec pre: line='{}' script={:#x} consolelog={:#x} consolemenu_open={}",
		a_line, reinterpret_cast<std::uintptr_t>(script),
		reinterpret_cast<std::uintptr_t>(RE::ConsoleLog::GetSingleton()),
		IsMenuOpen(std::string(RE::Console::MENU_NAME)));
	const bool ran = SafeCompileAndRun(script, nullptr);  // null target = no selected ref
	if (!ran) {
		// Don't delete after a caught AV: the form may be in an indeterminate state and
		// the virtual dtor dispatch could itself fault. Leaking one tiny Script is fine.
		SKSE::log::warn("exec: CompileAndRun faulted for '{}' (see 'exec FAULT' line for the pinned address)", a_line);
		return ExecResult::kFaulted;
	}
	delete script;
	return ExecResult::kOk;
}

bool engine::GiveSpell(RE::Actor* a_actor, RE::FormID a_spellID, Hand a_hand, std::string& a_err)
{
	if (!a_actor) {
		a_err = "actor unresolved";
		return false;
	}
	auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(a_spellID);
	if (!spell) {
		a_err = "spell formID not found";
		return false;
	}
	if (!a_actor->HasSpell(spell)) {
		a_actor->AddSpell(spell);
	}
	auto* eqm = RE::ActorEquipManager::GetSingleton();
	if (!eqm) {
		a_err = "ActorEquipManager unavailable";
		return false;
	}
	// Vanilla equip-slot forms (Skyrim.esm): right hand 0x13F42, left hand 0x13F43.
	// Using the stable FormIDs sidesteps the DefaultObjectManager enum-name churn.
	auto* rightSlot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x00013F42);
	auto* leftSlot  = RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x00013F43);
	if (a_hand == Hand::kRight || a_hand == Hand::kBoth) {
		eqm->EquipSpell(a_actor, spell, rightSlot);
	}
	if (a_hand == Hand::kLeft || a_hand == Hand::kBoth) {
		eqm->EquipSpell(a_actor, spell, leftSlot);
	}
	return true;
}

bool engine::SetAV(RE::Actor* a_actor, RE::ActorValue a_av, float a_value)
{
	if (!a_actor) {
		return false;
	}
	auto* avo = a_actor->AsActorValueOwner();
	if (!avo) {
		return false;
	}
	avo->SetActorValue(a_av, a_value);  // base/permanent
	// Refill current to the new base: clear the damage modifier (positive restore caps
	// at base). For value 0 this is a no-op, so the current value drains to 0 too.
	avo->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, a_av, a_value);
	return true;
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

void engine::DumpFaceGen(RE::Actor* a_actor, const char* a_src)
{
	trace::json line{ { "src", a_src } };
	if (!a_actor) {
		line["error"] = "null actor";
		trace::Write(std::move(line));
		return;
	}
	line["ref"] = HexID(a_actor->GetFormID());
	if (const char* nm = a_actor->GetDisplayFullName(); nm && *nm) {
		line["name"] = nm;
	}
	// QSpeakingDone() == true means the engine considers the line finished; invert it
	// to the more natural "is mid-line" reading the snap test wants.
	line["speaking"] = !a_actor->QSpeakingDone();

	auto* fg = a_actor->GetFaceGenAnimationData();
	if (!fg) {
		line["facegen"] = "none (3D unloaded / no head)";
		trace::Write(std::move(line));
		return;
	}

	// Summarize a morph keyframe: count, the dominant morph (max + index), and the
	// full value array (capped). The lip-sync writer mutates these under fg->lock, so
	// the read below runs under that same lock to avoid a torn sample.
	auto summarize = [](RE::BSFaceGenKeyframeMultiple& a_kf) {
		trace::json         o     = trace::json::object();
		const std::uint32_t count = a_kf.count;
		o["count"] = count;
		trace::json   vals = trace::json::array();
		float         maxv = 0.0F;
		std::uint32_t maxi = 0;
		if (a_kf.values && count > 0 && count <= 256) {
			for (std::uint32_t i = 0; i < count; ++i) {
				const float v = a_kf.values[i];
				vals.push_back(v);
				if (v > maxv) {
					maxv = v;
					maxi = i;
				}
			}
		}
		o["max"]    = maxv;
		o["maxIdx"] = maxi;
		o["values"] = std::move(vals);
		return o;
	};

	// Compact {count,max,maxIdx} for a keyframe — used to sweep EVERY morph keyframe and
	// find which one carries the live mouth animation (phenomeKeyFrame read flat 0.0 during
	// speech, so the value is elsewhere — likely transitionTargetKeyFrame).
	auto compact = [](RE::BSFaceGenKeyframeMultiple& a_kf) {
		float         maxv = 0.0F;
		std::uint32_t maxi = 0;
		if (a_kf.values && a_kf.count > 0 && a_kf.count <= 256) {
			for (std::uint32_t i = 0; i < a_kf.count; ++i) {
				if (a_kf.values[i] > maxv) {
					maxv = a_kf.values[i];
					maxi = i;
				}
			}
		}
		return trace::json{ { "count", a_kf.count }, { "max", maxv }, { "maxIdx", maxi } };
	};

	{
		RE::BSSpinLockGuard locker(fg->lock);
		line["exprOverride"] = fg->exprOverride;
		// Sweep every keyframe (names tagged by struct offset) to locate the mouth driver.
		trace::json kf = trace::json::object();
		if (fg->transitionTargetKeyFrame) {
			kf["transitionTarget@18"] = compact(*fg->transitionTargetKeyFrame);
		}
		kf["expression@20"] = compact(fg->expressionKeyFrame);
		kf["unk040@40"]     = compact(fg->unk040);
		kf["modifier@60"]   = compact(fg->modifierKeyFrame);
		kf["phoneme@80"]    = compact(fg->phenomeKeyFrame);
		kf["custom@A0"]     = compact(fg->customKeyFrame);
		kf["unk0C0@C0"]     = compact(fg->unk0C0);
		kf["unk0E0@E0"]     = compact(fg->unk0E0);
		kf["unk100@100"]    = compact(fg->unk100);
		kf["unk120@120"]    = compact(fg->unk120);
		kf["unk140@140"]    = compact(fg->unk140);
		kf["unk160@160"]    = compact(fg->unk160);
		kf["unk180@180"]    = compact(fg->unk180);
		line["kf"]      = std::move(kf);
		line["phoneme"] = summarize(fg->phenomeKeyFrame);  // full values for the v4 field
	}
	trace::Write(std::move(line));
}

bool engine::CloseFaceGen(RE::Actor* a_actor, float a_timer, bool a_lock, bool a_speakingDone, std::string& a_err)
{
	if (!a_actor) {
		a_err = "actor unresolved";
		return false;
	}
	// SetSpeakingDone(true) stops the engine's face pump re-driving the phonemes; the v4
	// cut needs it or the reset is re-clobbered (DBVODialogueTweaks dead-end 6).
	if (a_speakingDone) {
		a_actor->SetSpeakingDone(true);
	}
	auto* fg = a_actor->GetFaceGenAnimationData();
	if (!fg) {
		a_err = "no facegen data (actor 3D unloaded?)";
		return false;
	}
	// a_lock mirrors the mod's BSSpinLockGuard. Dead-end 6's failed ease was UNLOCKED, so
	// the locked+eased combo is exactly the variable this probe isolates. Everything else
	// (ClearExpressionOverride, the Reset bool flags) matches CutNpcReply verbatim.
	if (a_lock) {
		RE::BSSpinLockGuard locker(fg->lock);
		fg->ClearExpressionOverride();
		fg->Reset(a_timer, true, true, true, false);
	} else {
		fg->ClearExpressionOverride();
		fg->Reset(a_timer, true, true, true, false);
	}
	return true;
}

// ---- owned per-frame transition-target ramp (the candidate mouth-close fix) --------
//
// CADENCE — a dedicated ticker thread paces this, NOT a self-re-queuing task. A task that
// re-enqueues itself via SKSE::GetTaskInterface()->AddTask HARD-FREEZES the game: the SKSE
// runtime drains tasks added during its own drain pass within the SAME frame, so self-
// re-queue is an infinite loop on one frame (cost two sessions to learn — see
// skytest/docs/headless-findings.md). The codebase's safe pattern (the command poll thread
// pacing MainTick) is the model: an EXTERNAL pacer that sleeps between ONE-SHOT AddTask
// enqueues. The ticker sleeps ~16 ms (during the ramp) so each step lands ~once per frame —
// fast enough to out-write the lip pump frame-by-frame — and idles when no ramp is active.
namespace
{
	// RICH ramp state — touched ONLY on the main thread (RampStep + Start/Cancel, which the
	// command handler marshals via the task queue). The ticker thread never reads it; it only
	// flips the atomics below. So no lock guards the struct — only fg->lock for keyframe I/O.
	struct RampState
	{
		bool                        triggered = false;
		bool                        done = false;  // finished/aborted — guards a double-emit from a trailing tick
		long long                   armMs = 0;     // for the self-trigger timeout
		long long                   startMs = 0;   // ramp t=0 (set at trigger)
		RE::BSFaceGenAnimationData* targetFg = nullptr;  // the speaker's facegen; the hook scales ONLY this one
		float                       lastHookTime = -1.0F;  // NiUpdateData.time of the last hook scale (per-frame dedup)
		engine::FaceGenRampParams   p;
	};
	RampState g_ramp;

	// Cross-thread flags (ticker <-> main). gen bumps per Start/Cancel: a RampStep carrying a
	// stale gen drops itself (a new ramp supersedes the old; Cancel stops it). active gates the
	// ticker's enqueue+pace; phase paces it (1 = ramp/hold => ~16 ms, 0 = waiting => ~66 ms).
	std::atomic<int>  g_rampGen{ 0 };
	std::atomic<bool> g_rampActive{ false };
	std::atomic<int>  g_rampPhase{ 0 };
	std::atomic<bool> g_tickerStarted{ false };

	// Read transitionTarget's max value (and its index) under fg->lock. count==0 -> max 0.
	float TtMax(RE::BSFaceGenKeyframeMultiple* a_kf, std::uint32_t& a_idx)
	{
		float         m = 0.0F;
		std::uint32_t mi = 0;
		if (a_kf->values && a_kf->count > 0 && a_kf->count <= 256) {
			for (std::uint32_t i = 0; i < a_kf->count; ++i) {
				if (a_kf->values[i] > m) {
					m = a_kf->values[i];
					mi = i;
				}
			}
		}
		a_idx = mi;
		return m;
	}

	// Finish the ramp: emit a terminal line once and let the ticker idle. Idempotent via done.
	void FinishRamp(const char* a_src, trace::json a_extra)
	{
		if (g_ramp.done) {
			return;
		}
		g_ramp.done = true;
		g_rampActive.store(false, std::memory_order_relaxed);
		a_extra["src"] = a_src;
		trace::Write(std::move(a_extra));
	}

	// ONE ramp step, on the main thread (enqueued by the ticker; NEVER re-enqueues itself).
	void RampStep(int a_gen)
	{
		if (a_gen != g_rampGen.load(std::memory_order_relaxed) || g_ramp.done) {
			return;  // superseded/cancelled/finished — a trailing enqueue, drop it
		}
		const auto&     p   = g_ramp.p;
		const long long now = trace::NowMs();

		auto* r     = engine::ResolveOne(p.ref);
		auto* actor = r ? r->As<RE::Actor>() : nullptr;
		auto* fg    = actor ? actor->GetFaceGenAnimationData() : nullptr;
		auto* kf    = fg ? fg->transitionTargetKeyFrame : nullptr;
		if (!kf) {
			// No speaker / 3D not loaded / no transition keyframe yet — keep waiting (the
			// ticker re-enqueues) up to the timeout.
			if (!g_ramp.triggered && now - g_ramp.armMs > static_cast<long long>(p.waitMs)) {
				FinishRamp("ramp-abort", trace::json{ { "reason", "no facegen/transitionTarget before waitMs" } });
			}
			return;
		}

		if (!g_ramp.triggered) {
			// WAIT phase: arm the ramp the moment she's actually mid-word (nonzero target).
			std::uint32_t mi = 0;
			float         m  = 0.0F;
			{
				RE::BSSpinLockGuard locker(fg->lock);
				m = TtMax(kf, mi);
			}
			if (m >= p.threshold) {
				// cut: replicate CutNpcReply's audio-stop FIRST so the lip pump goes quiet.
				// Without it the playing voice line keeps re-driving transitionTarget every
				// frame and any owned ramp loses the race (confirmed: dead-end 6).
				if (p.cut) {
					if (auto* say = actor->extraList.GetByType<RE::ExtraSayToTopicInfo>()) {
						if (say->sound.IsValid() && say->sound.IsPlaying()) {
							say->sound.FadeOutAndRelease(30);
						}
					}
					actor->PauseCurrentDialogue();
				}
				// Stop the speaking state if asked, then hand the actor's facegen to the hook —
				// it does the per-frame scaling at the apply point (winning the pump race).
				if (p.speakingDone) {
					actor->SetSpeakingDone(true);
				}
				g_ramp.targetFg     = fg;
				g_ramp.lastHookTime = -1.0F;
				g_ramp.triggered    = true;
				g_ramp.startMs      = now;
				g_rampPhase.store(1, std::memory_order_relaxed);  // speed the ticker up for the done-poll
				trace::Write(trace::json{ { "src", "ramp-trigger" },
					{ "ref", engine::HexID(actor->GetFormID()) }, { "max", m }, { "maxIdx", mi },
					{ "count", kf->count }, { "speakingDone", p.speakingDone }, { "cut", p.cut },
					{ "ms", p.ms }, { "holdMs", p.holdMs }, { "reassert", p.reassert } });
				return;
			}
			if (now - g_ramp.armMs > static_cast<long long>(p.waitMs)) {
				FinishRamp("ramp-abort", trace::json{ { "reason", "no speech within waitMs" } });
			}
			return;
		}

		// RAMP / HOLD lifecycle ONLY. The per-frame transitionTarget scaling is done by the
		// facegen update hook (ApplyRampScale) — it runs at the apply point and wins the race the
		// AddTask write lost. This step just retires the ramp once ms+holdMs has elapsed.
		if (now - g_ramp.startMs >= static_cast<long long>(p.ms + p.holdMs)) {
			FinishRamp("ramp-done", trace::json{ { "elapsed", now - g_ramp.startMs } });
		}
	}

	// Pre-apply scale, called from the BSFaceGenNiNode::UpdateDownwardPass hook each frame. While a
	// ramp is live for THIS node's facegen, multiply transitionTarget by the decay factor so the
	// engine's own morph-apply (the original call, right after) renders our eased value. Runs AFTER
	// the lip pump wrote transitionTarget and BEFORE the apply — the seam the AddTask write missed.
	// Idempotent per frame via NiUpdateData.time (a second downward pass the same frame is skipped,
	// so the in-place scale can't compound).
	void ApplyRampScale(RE::BSFaceGenNiNode* a_node, float a_time)
	{
		if (!a_node || !g_ramp.triggered || g_ramp.done || !g_ramp.targetFg) {
			return;
		}
		auto* fg = a_node->GetRuntimeData().animationData.get();
		if (fg != g_ramp.targetFg) {
			return;  // a different actor's head — leave it untouched
		}
		auto* kf = fg->transitionTargetKeyFrame;
		if (!kf || !kf->values) {
			return;
		}
		const auto&     p       = g_ramp.p;
		const long long elapsed = trace::NowMs() - g_ramp.startMs;
		const float     t       = (p.ms <= 0.0F) ? 1.0F : std::min(1.0F, static_cast<float>(elapsed) / p.ms);
		const bool      ramping = t < 1.0F;
		// hold phase: keep forcing 0 only when reassert (the fix). reassert:false stops scaling at
		// t>=1 so we can observe whether the close holds on its own once the pump quiets.
		if (!ramping && !p.reassert) {
			return;
		}
		if (a_time == g_ramp.lastHookTime) {
			return;  // already scaled this frame — don't compound the in-place multiply
		}
		g_ramp.lastHookTime = a_time;
		const float   factor    = 1.0F - t;
		float         maxBefore = 0.0F, maxAfter = 0.0F;
		std::uint32_t mbIdx = 0, maIdx = 0;
		{
			RE::BSSpinLockGuard locker(fg->lock);
			maxBefore = TtMax(kf, mbIdx);  // the pump's full value this frame
			const std::uint32_t n = std::min<std::uint32_t>(kf->count, 256);
			for (std::uint32_t i = 0; i < n; ++i) {
				kf->SetValue(i, kf->values[i] * factor);  // SetValue clears isUpdated -> apply re-reads
			}
			maxAfter = TtMax(kf, maIdx);  // == what the original's apply will render
		}
		trace::Write(trace::json{ { "src", "ramp" }, { "via", "hook" }, { "frac", t },
			{ "elapsed", elapsed }, { "phase", ramping ? "ramp" : "hold" },
			{ "maxBefore", maxBefore }, { "maxBeforeIdx", mbIdx },
			{ "maxAfter", maxAfter }, { "maxAfterIdx", maIdx } });
	}

	// The hook itself — a vtable detour on BSFaceGenNiNode::UpdateDownwardPass (idx 0x2C). Scale
	// pre-apply, then run the original so the engine applies our eased transitionTarget.
	struct FaceGenUpdateHook
	{
		static void thunk(RE::BSFaceGenNiNode* a_this, RE::NiUpdateData& a_data, std::uint32_t a_arg2)
		{
			ApplyRampScale(a_this, a_data.time);
			func(a_this, a_data, a_arg2);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// The pacer. One persistent thread (lazily started): idles cheaply when no ramp is active,
	// else enqueues exactly ONE RampStep per wake — the external-pacer pattern that keeps the
	// per-frame work off the forbidden self-re-queue path.
	void RampTickerLoop()
	{
		for (;;) {
			if (!g_rampActive.load(std::memory_order_relaxed)) {
				std::this_thread::sleep_for(std::chrono::milliseconds(150));
				continue;
			}
			const int gen = g_rampGen.load(std::memory_order_relaxed);
			if (auto* task = SKSE::GetTaskInterface()) {
				task->AddTask([gen]() { RampStep(gen); });
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(
				g_rampPhase.load(std::memory_order_relaxed) == 1 ? 16 : 66));
		}
	}
}

void engine::StartFaceGenRamp(const FaceGenRampParams& a_params)
{
	// Reset rich state (main thread) BEFORE flipping the atomics the ticker reads.
	g_ramp.triggered    = false;
	g_ramp.done         = false;
	g_ramp.targetFg     = nullptr;
	g_ramp.lastHookTime = -1.0F;
	g_ramp.armMs        = trace::NowMs();
	g_ramp.p            = a_params;
	g_rampPhase.store(0, std::memory_order_relaxed);
	g_rampGen.fetch_add(1, std::memory_order_relaxed);  // supersede any in-flight step
	g_rampActive.store(true, std::memory_order_relaxed);
	if (!g_tickerStarted.exchange(true)) {
		std::thread(RampTickerLoop).detach();
	}
	trace::Write(trace::json{ { "src", "ramp-arm" }, { "ref", a_params.ref },
		{ "threshold", a_params.threshold }, { "ms", a_params.ms },
		{ "holdMs", a_params.holdMs }, { "speakingDone", a_params.speakingDone },
		{ "reassert", a_params.reassert }, { "waitMs", a_params.waitMs } });
}

void engine::CancelFaceGenRamp()
{
	g_rampGen.fetch_add(1, std::memory_order_relaxed);  // any in-flight step sees a stale gen
	g_rampActive.store(false, std::memory_order_relaxed);
	g_ramp.triggered = false;
	g_ramp.done      = true;
	trace::Write(trace::json{ { "src", "ramp-cancel" } });
}

void engine::InstallFaceGenHook()
{
	// vtable detour on BSFaceGenNiNode::UpdateDownwardPass (idx 0x2C, SE/AE). Affects every head
	// node; ApplyRampScale gates to the one ramping facegen and is inert otherwise. Installed once
	// at kDataLoaded. The vtable is process-wide, so this is a one-time write.
	REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_BSFaceGenNiNode[0] };
	FaceGenUpdateHook::func = vtbl.write_vfunc(0x2C, &FaceGenUpdateHook::thunk);
	SKSE::log::info("SkytestProbe: BSFaceGenNiNode::UpdateDownwardPass hook installed (facegen ramp)");
}

int engine::WriteMcmList()
{
	// The VM is the floor: no VM means we're pre-load and can't introspect anything.
	// That's the ONLY -1 return (command acks false); every other outcome is a
	// successful scan that writes a record and returns count >= 0.
	auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
	if (!vm) {
		return -1;
	}

	trace::json mods = trace::json::array();
	const char* via  = "none";

	// ---- primary: SkyUI's central registry (SKI_ConfigManager) ------------------
	// _modConfigs (array of config Objects) and _modNames (array of strings) are the
	// authoritative, ordered registry. These are private script vars, so they may be
	// stripped from the .pex (GetVariable -> null) — fall through to the scan if so.
	if (auto mgr = FindBoundScript("SKI_ConfigManager"); mgr) {
		const auto* vConfigs = mgr->GetVariable("_modConfigs");
		const auto* vNames   = mgr->GetVariable("_modNames");
		if (vConfigs && vConfigs->IsArray() && vNames && vNames->IsArray()) {
			auto configs = vConfigs->GetArray();
			if (configs) {
				// Drive off the config array; _modNames is parallel but ModName is read
				// per-config below, so the names array only needs to confirm the registry
				// shape (both present & arrays) — the config objects carry everything.
				for (std::uint32_t i = 0; i < configs->size(); ++i) {
					const auto& slot = (*configs)[i];
					if (auto cfg = slot.GetObject(); cfg) {
						AppendConfigEntry(cfg.get(), mods);
					}
				}
				via = "manager";
			}
		}
	}

	// ---- fallback: scan every quest for an attached SKI_ConfigBase subclass ------
	// When the registry vars are stripped/empty (above left via=="none"), sweep all
	// quests with a BASE-class FindBoundObject match (hierarchy-aware), collecting
	// each config instance directly. Inline, not FindBoundScript, because that helper
	// returns the FIRST match only — here we want every match.
	if (mods.empty()) {
		auto* policy = vm->GetObjectHandlePolicy();
		auto* dh     = RE::TESDataHandler::GetSingleton();
		if (policy && dh) {
			for (auto* quest : dh->GetFormArray<RE::TESQuest>()) {
				if (!quest) {
					continue;
				}
				const RE::VMHandle handle = policy->GetHandleForObject(RE::FormType::Quest, quest);
				RE::BSTSmartPointer<RE::BSScript::Object> obj;
				if (vm->FindBoundObject(handle, "SKI_ConfigBase", obj) && obj) {
					AppendConfigEntry(obj.get(), mods);
				}
			}
			if (!mods.empty()) {
				via = "scan";
			}
		}
	}

	const int count = static_cast<int>(mods.size());
	trace::Write(trace::json{
		{ "src", "mcm-list" },
		{ "via", via },
		{ "count", count },
		{ "mods", std::move(mods) },
	});
	return count;
}

bool engine::WriteMcmGet(const std::string& a_script, const std::vector<std::string>& a_props)
{
	// Locate the quest bound to this exact config-script class (the class names
	// mcm-list emits). No match -> false, no record; the command acks the error.
	auto obj = FindBoundScript(a_script.c_str());
	if (!obj) {
		return false;
	}

	trace::json values  = trace::json::object();
	trace::json missing = trace::json::array();

	for (const auto& name : a_props) {
		const auto* p = obj->GetProperty(name);
		if (!p) {
			missing.push_back(name);  // no such property on this script
			continue;
		}
		// Scalar coercion by the Variable's runtime type. Array/object/none aren't
		// scalars we report in v1, so they land in "missing" (partial read is still ok).
		if (p->IsBool()) {
			values[name] = p->GetBool();
		} else if (p->IsInt()) {
			values[name] = p->GetSInt();
		} else if (p->IsFloat()) {
			values[name] = p->GetFloat();
		} else if (p->IsString()) {
			values[name] = std::string(p->GetString());
		} else {
			missing.push_back(name);
		}
	}

	trace::Write(trace::json{
		{ "src", "mcm-get" },
		{ "script", a_script },
		{ "values", std::move(values) },
		{ "missing", std::move(missing) },
	});
	return true;
}
