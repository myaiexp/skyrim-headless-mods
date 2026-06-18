// SkyUI MCM config enumeration and property read helpers.
#include "mcm.h"

#include <cstdint>

#include <RE/Skyrim.h>

#include "trace.h"

namespace
{
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