// Snapshot one actor into a trace dump-shaped JSON line.
#include "dump_actor.h"

#include <cstdint>

#include "resolve.h"
#include "trace.h"

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