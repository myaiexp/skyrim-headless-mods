// Spell equip and actor-value staging helpers (main-thread only).
#include "actor_setup.h"

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