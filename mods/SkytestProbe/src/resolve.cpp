// FormID/ref resolution and crosshair/actor-value lookup helpers.
#include "resolve.h"

#include <cstdio>
#include <unordered_map>

namespace
{
	// "teammates" is a set keyword; resolve_one returns null for it on purpose.
	bool IsSetKeyword(const std::string& a_ref)
	{
		return a_ref == "teammates";
	}

	// name -> spawned ref FormID (placeatme "as <name>"). Main-thread-only access.
	std::unordered_map<std::string, RE::FormID> g_aliases;
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

	// A registered placeatme alias ("ally"/"enemy"/…) — checked AFTER the reserved keywords
	// (so an alias can never shadow player/crosshair/speaker) but BEFORE hex parsing (alias
	// names like "ally"/"enemy" aren't valid hex, but a name beginning with hex digits could
	// be, and the explicit alias should always win).
	if (auto it = g_aliases.find(a_ref); it != g_aliases.end()) {
		auto* form = RE::TESForm::LookupByID(it->second);
		return form ? form->As<RE::TESObjectREFR>() : nullptr;  // null if the ref was freed
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

void engine::SetAlias(const std::string& a_name, RE::FormID a_id)
{
	g_aliases[a_name] = a_id;  // overwrites a prior spawn under the same name
}

void engine::ClearAliases()
{
	g_aliases.clear();
}