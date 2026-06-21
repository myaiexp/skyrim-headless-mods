// Spell equip and actor-value staging helpers (main-thread only).
#include "actor_setup.h"

#include <cmath>

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

bool engine::PlaceActorForward(RE::FormID a_baseID, float a_distance, bool a_freeze,
	RE::FormID& a_outID, std::string& a_err)
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		a_err = "no player";
		return false;
	}
	auto* base  = RE::TESForm::LookupByID(a_baseID);
	auto* bound = base ? base->As<RE::TESBoundObject>() : nullptr;
	if (!bound) {
		a_err = "base formID is not a placeable bound object";
		return false;
	}
	auto  spawned = player->PlaceObjectAtMe(bound, false);  // NiPointer<TESObjectREFR>
	auto* ref     = spawned ? spawned.get() : nullptr;
	if (!ref) {
		a_err = "PlaceObjectAtMe returned null";
		return false;
	}
	// Place a_distance units down the player's forward vector at the player's elevation.
	// Skyrim yaw (data.angle.z) is measured from +Y clockwise, so forward = (sin z, cos z, 0).
	const float        yaw  = player->data.angle.z;
	const RE::NiPoint3 ppos = player->GetPosition();
	ref->SetPosition(RE::NiPoint3{
		ppos.x + std::sin(yaw) * a_distance,
		ppos.y + std::cos(yaw) * a_distance,
		ppos.z });
	// Face back toward the player (tidy; AI is frozen so it won't turn anyway). No SetAngle
	// member exists in this CommonLib — write the yaw field directly.
	ref->data.angle.z = yaw + 3.14159265f;
	if (auto* actor = ref->As<RE::Actor>(); actor && a_freeze) {
		actor->EnableAI(false);  // halt locomotion; collision body stays (no SetDontMove here)
	}
	a_outID = ref->GetFormID();
	return true;
}

bool engine::SetTeammate(RE::Actor* a_actor, bool a_on)
{
	if (!a_actor) {
		return false;
	}
	auto& bits = a_actor->GetActorRuntimeData().boolBits;
	if (a_on) {
		bits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);
	} else {
		bits.reset(RE::Actor::BOOL_BITS::kPlayerTeammate);
	}
	return true;
}

bool engine::CastAt(RE::Actor* a_caster, RE::FormID a_spellID, RE::TESObjectREFR* a_target,
	Hand a_hand, std::string& a_err)
{
	if (!a_caster) {
		a_err = "caster unresolved";
		return false;
	}
	auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(a_spellID);
	if (!spell) {
		a_err = "spell formID is not a SpellItem";
		return false;
	}
	// kBoth is meaningless for a single immediate cast — fold it to the right hand.
	auto source = (a_hand == Hand::kLeft) ? RE::MagicSystem::CastingSource::kLeftHand
	                                      : RE::MagicSystem::CastingSource::kRightHand;
	auto* caster = a_caster->GetMagicCaster(source);
	if (!caster) {
		a_err = "no magic caster for that hand";
		return false;
	}
	// CastSpellImmediate has no default args: spell, noHitEffectArt, target, effectiveness,
	// hostileEffectivenessOnly, magnitudeOverride, blameActor.
	caster->CastSpellImmediate(spell, false, a_target, 1.0f, false, 0.0f, a_caster);
	return true;
}