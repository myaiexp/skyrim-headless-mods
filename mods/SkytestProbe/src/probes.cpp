#include "probes.h"

#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include <SKSE/SKSE.h>

#include "engine.h"
#include "trace.h"

namespace
{
	// ---- trace line for one engine event ----------------------------------------
	void WriteEvent(const char* a_tag, RE::FormID a_from, RE::FormID a_to, const std::string& a_desc)
	{
		trace::Write(trace::json{
			{ "src", a_tag },
			{ "from", engine::HexID(a_from) },
			{ "to", engine::HexID(a_to) },
			{ "desc", a_desc } });
	}

	std::string RefLabel(const RE::NiPointer<RE::TESObjectREFR>& a_ref)
	{
		auto* p = a_ref.get();
		if (!p) {
			return "none";
		}
		std::string hex = engine::HexID(p->GetFormID());
		const char*  nm = p->GetName();
		return (nm && nm[0]) ? hex + " (" + nm + ")" : hex;
	}

	RE::FormID RefID(const RE::NiPointer<RE::TESObjectREFR>& a_ref)
	{
		auto* p = a_ref.get();
		return p ? p->GetFormID() : 0;
	}

	RE::FormID HandleID(const RE::ObjectRefHandle& a_handle)
	{
		auto ref = a_handle.get();
		return ref ? ref->GetFormID() : 0;
	}

	// ---- shared gate (one per event type) ---------------------------------------
	struct EventGate
	{
		std::atomic<bool>              enabled{ false };
		std::mutex                     mtx;          // guards everything below (event vs main thread)
		bool                           hasFilter = false;  // refs were supplied (vs match-all)
		std::vector<std::string>       refStrings;   // raw refs, re-resolved each tick
		std::unordered_set<RE::FormID> filter;       // resolved FormIDs

		// match-all when no refs given; match-nothing when refs given but unresolved
		// (e.g. teammates before a save loads) — never silently match-all on empty.
		bool passes(RE::FormID a_src, RE::FormID a_tgt)
		{
			if (!enabled.load(std::memory_order_relaxed)) {
				return false;  // disarmed fast path
			}
			std::scoped_lock lk(mtx);
			if (!hasFilter) {
				return true;
			}
			if (filter.empty()) {
				return false;
			}
			return (a_src && filter.count(a_src)) || (a_tgt && filter.count(a_tgt));
		}
	};

	// ---- CRTP sink base ---------------------------------------------------------
	template <class Event, class Derived>
	class ProbeSink : public RE::BSTEventSink<Event>
	{
	public:
		EventGate gate;

		RE::BSEventNotifyControl ProcessEvent(const Event* a_event, RE::BSTEventSource<Event>*) override
		{
			if (!a_event || !gate.enabled.load(std::memory_order_relaxed)) {
				return RE::BSEventNotifyControl::kContinue;  // inert / disarmed
			}
			RE::FormID  src = 0, tgt = 0;
			std::string desc;
			static_cast<Derived*>(this)->Extract(*a_event, src, tgt, desc);
			if (gate.passes(src, tgt)) {
				WriteEvent(Derived::kTag, src, tgt, desc);
			}
			return RE::BSEventNotifyControl::kContinue;  // never kStop: probe is passive
		}
	};

	// ---- per-event sinks (src = aggressor/actor; tgt = victim/object) -----------
	class HitSink : public ProbeSink<RE::TESHitEvent, HitSink>
	{
	public:
		static constexpr const char* kTag = "hit";
		void Extract(const RE::TESHitEvent& e, RE::FormID& src, RE::FormID& tgt, std::string& d)
		{
			src = RefID(e.cause);
			tgt = RefID(e.target);
			d = "cause=" + RefLabel(e.cause) + " target=" + RefLabel(e.target) +
			    " weapon=" + engine::FormLabel(e.source) + " proj=" + engine::FormLabel(e.projectile) +
			    " flags=" + std::to_string(static_cast<std::uint8_t>(e.flags.get()));
		}
	};

	class EquipSink : public ProbeSink<RE::TESEquipEvent, EquipSink>
	{
	public:
		static constexpr const char* kTag = "equip";
		void Extract(const RE::TESEquipEvent& e, RE::FormID& src, RE::FormID& tgt, std::string& d)
		{
			src = RefID(e.actor);
			tgt = e.baseObject;
			d = "actor=" + RefLabel(e.actor) + (e.equipped ? " equipped " : " unequipped ") +
			    engine::FormLabel(e.baseObject);
		}
	};

	class MagicApplySink : public ProbeSink<RE::TESMagicEffectApplyEvent, MagicApplySink>
	{
	public:
		static constexpr const char* kTag = "magic-apply";
		void Extract(const RE::TESMagicEffectApplyEvent& e, RE::FormID& src, RE::FormID& tgt, std::string& d)
		{
			src = RefID(e.caster);
			tgt = RefID(e.target);
			d = "caster=" + RefLabel(e.caster) + " target=" + RefLabel(e.target) +
			    " effect=" + engine::FormLabel(e.magicEffect);
		}
	};

	class CombatSink : public ProbeSink<RE::TESCombatEvent, CombatSink>
	{
	public:
		static constexpr const char* kTag = "combat";
		void Extract(const RE::TESCombatEvent& e, RE::FormID& src, RE::FormID& tgt, std::string& d)
		{
			src = RefID(e.actor);
			tgt = RefID(e.targetActor);
			const char* st = "none";
			switch (e.newState.get()) {
			case RE::ACTOR_COMBAT_STATE::kCombat:    st = "combat";    break;
			case RE::ACTOR_COMBAT_STATE::kSearching: st = "searching"; break;
			default: break;
			}
			d = "actor=" + RefLabel(e.actor) + " target=" + RefLabel(e.targetActor) + " state=" + st;
		}
	};

	class ActivateSink : public ProbeSink<RE::TESActivateEvent, ActivateSink>
	{
	public:
		static constexpr const char* kTag = "activate";
		void Extract(const RE::TESActivateEvent& e, RE::FormID& src, RE::FormID& tgt, std::string& d)
		{
			src = RefID(e.actionRef);
			tgt = RefID(e.objectActivated);
			d = "actionRef=" + RefLabel(e.actionRef) + " activated=" + RefLabel(e.objectActivated);
		}
	};

	class ContainerSink : public ProbeSink<RE::TESContainerChangedEvent, ContainerSink>
	{
	public:
		static constexpr const char* kTag = "container";
		void Extract(const RE::TESContainerChangedEvent& e, RE::FormID& src, RE::FormID& tgt, std::string& d)
		{
			src = e.oldContainer;
			tgt = e.newContainer;
			d = "item=" + engine::FormLabel(e.baseObj) + " x" + std::to_string(e.itemCount) +
			    " from=" + engine::FormLabel(e.oldContainer) + " to=" + engine::FormLabel(e.newContainer) +
			    " ref=" + engine::FormLabel(HandleID(e.reference));
		}
	};

	class DeathSink : public ProbeSink<RE::TESDeathEvent, DeathSink>
	{
	public:
		static constexpr const char* kTag = "death";
		void Extract(const RE::TESDeathEvent& e, RE::FormID& src, RE::FormID& tgt, std::string& d)
		{
			src = RefID(e.actorKiller);
			tgt = RefID(e.actorDying);
			d = std::string(e.dead ? "died" : "dying") +
			    " actor=" + RefLabel(e.actorDying) + " killer=" + RefLabel(e.actorKiller);
		}
	};

	HitSink        g_hit;
	EquipSink      g_equip;
	MagicApplySink g_magic;
	CombatSink     g_combat;
	ActivateSink   g_activate;
	ContainerSink  g_container;
	DeathSink      g_death;

	// event-name -> gate. Keys are the design's `events` list values.
	const std::unordered_map<std::string, EventGate*> g_gates = {
		{ "hit", &g_hit.gate },
		{ "equip", &g_equip.gate },
		{ "magic-apply", &g_magic.gate },
		{ "combat", &g_combat.gate },
		{ "activate", &g_activate.gate },
		{ "container", &g_container.gate },
		{ "death", &g_death.gate },
	};
	std::atomic<int> g_armedFilterCount{ 0 };  // gates armed WITH a filter (need re-resolve)

	void RecountArmedFilters()
	{
		int n = 0;
		for (const auto& [name, gate] : g_gates) {
			if (gate->enabled.load(std::memory_order_relaxed)) {
				std::scoped_lock lk(gate->mtx);
				if (gate->hasFilter) {
					++n;
				}
			}
		}
		g_armedFilterCount.store(n, std::memory_order_relaxed);
	}

	// ---- anim-graph sinks (per-actor, kept alive for process lifetime) ----------
	class AnimGraphProbeSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
	{
	public:
		explicit AnimGraphProbeSink(RE::FormID a_owner) :
			_owner(a_owner) {}

		RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event,
			RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
		{
			if (a_event) {
				trace::Write(trace::json{
					{ "src", "anim" },
					{ "ref", engine::HexID(_owner) },
					{ "tag", a_event->tag.empty() ? "" : a_event->tag.c_str() },
					{ "payload", a_event->payload.empty() ? "" : a_event->payload.c_str() } });
			}
			return RE::BSEventNotifyControl::kContinue;
		}

	private:
		RE::FormID _owner;
	};

	struct AnimEntry
	{
		std::unique_ptr<AnimGraphProbeSink> sink;
		bool                                armed = false;  // should-be-attached
	};
	// FormID -> entry. The sink is NEVER freed at runtime (the engine holds a raw
	// pointer; freeing a still-registered sink is a UAF). Entries persist; `armed`
	// tracks intent and the tick re-attaches. Main-thread only.
	std::unordered_map<RE::FormID, AnimEntry> g_animSinks;
	std::atomic<int>                          g_armedAnimCount{ 0 };

	void RecountArmedAnim()
	{
		int n = 0;
		for (const auto& [id, e] : g_animSinks) {
			if (e.armed) {
				++n;
			}
		}
		g_armedAnimCount.store(n, std::memory_order_relaxed);
	}

	// ---- watches ----------------------------------------------------------------
	struct Watch
	{
		std::string ref;
		std::string av;
		RE::FormID  lastActor = 0;       // re-baseline when the resolved actor changes
		float       last = 0.0F;
		bool        hasLast = false;
	};
	std::unordered_map<std::string, Watch> g_watches;  // id -> watch. Main-thread only.
	std::atomic<int>                       g_activeWatchCount{ 0 };

	// ---- facegen watches --------------------------------------------------------
	// Refs whose facegen morphs are dumped every MainTick while armed. Main-thread only.
	std::unordered_set<std::string> g_faceWatches;
	std::atomic<int>                g_activeFaceWatchCount{ 0 };
}

void probes::RegisterEventSinks()
{
	auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
	if (!holder) {
		SKSE::log::error("ScriptEventSourceHolder null; event sinks not registered");
		return;
	}
	holder->AddEventSink<RE::TESHitEvent>(&g_hit);
	holder->AddEventSink<RE::TESEquipEvent>(&g_equip);
	holder->AddEventSink<RE::TESMagicEffectApplyEvent>(&g_magic);
	holder->AddEventSink<RE::TESCombatEvent>(&g_combat);
	holder->AddEventSink<RE::TESActivateEvent>(&g_activate);
	holder->AddEventSink<RE::TESContainerChangedEvent>(&g_container);
	holder->AddEventSink<RE::TESDeathEvent>(&g_death);
	SKSE::log::info("SkytestProbe: 7 engine event sinks registered (inert)");
}

std::vector<std::string> probes::ArmTrace(const std::vector<std::string>& a_events, bool a_on,
	const std::vector<std::string>& a_refs)
{
	// All-or-nothing: validate every event name first; arm nothing if any is unknown.
	std::vector<std::string> unknown;
	for (const auto& name : a_events) {
		if (!g_gates.count(name)) {
			unknown.push_back(name);
		}
	}
	if (!unknown.empty()) {
		return unknown;
	}

	const auto resolved = a_on ? engine::ResolveToFormIDs(a_refs) : std::unordered_set<RE::FormID>{};
	for (const auto& name : a_events) {
		EventGate* gate = g_gates.at(name);
		{
			std::scoped_lock lk(gate->mtx);
			gate->hasFilter  = a_on && !a_refs.empty();
			gate->refStrings = a_on ? a_refs : std::vector<std::string>{};
			gate->filter     = resolved;
		}
		gate->enabled.store(a_on, std::memory_order_relaxed);
	}
	RecountArmedFilters();
	return {};  // success
}

bool probes::ArmAnim(RE::Actor* a_actor, bool a_on)
{
	if (!a_actor) {
		return false;
	}
	const RE::FormID id = a_actor->GetFormID();
	auto& e = g_animSinks[id];
	if (!e.sink) {
		e.sink = std::make_unique<AnimGraphProbeSink>(id);
	}
	bool result = true;
	if (a_on) {
		e.armed = true;
		result = a_actor->AddAnimationGraphEventSink(e.sink.get());  // false = no 3D yet (tick retries)
	} else {
		e.armed = false;
		a_actor->RemoveAnimationGraphEventSink(e.sink.get());  // void; safe no-op if detached
	}
	RecountArmedAnim();
	return result;
}

void probes::DisarmAllAnim()
{
	for (auto& [id, e] : g_animSinks) {
		if (e.armed) {
			if (auto* form = RE::TESForm::LookupByID(id)) {
				if (auto* actor = form->As<RE::Actor>()) {
					actor->RemoveAnimationGraphEventSink(e.sink.get());
				}
			}
			e.armed = false;
		}
	}
	RecountArmedAnim();
}

void probes::ArmWatch(const std::string& a_id, const std::string& a_ref, const std::string& a_av, bool a_on)
{
	if (a_on) {
		g_watches[a_id] = Watch{ a_ref, a_av, 0, 0.0F, false };
	} else {
		g_watches.erase(a_id);
	}
	g_activeWatchCount.store(static_cast<int>(g_watches.size()), std::memory_order_relaxed);
}

void probes::ArmFaceWatch(const std::string& a_ref, bool a_on)
{
	if (a_on) {
		g_faceWatches.insert(a_ref);
	} else {
		g_faceWatches.erase(a_ref);
	}
	g_activeFaceWatchCount.store(static_cast<int>(g_faceWatches.size()), std::memory_order_relaxed);
}

namespace
{
	void SampleWatches()
	{
		for (auto& [id, w] : g_watches) {
			auto* ref = engine::ResolveOne(w.ref);
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (!actor) {
				continue;  // not resolvable yet (armed at menu) — produce once valid
			}
			const RE::ActorValue av = engine::ResolveActorValue(w.av);
			if (av == RE::ActorValue::kNone) {
				continue;
			}
			auto* avo = actor->AsActorValueOwner();
			if (!avo) {
				continue;
			}
			const RE::FormID actorID = actor->GetFormID();
			const float      value = avo->GetActorValue(av);
			// Suppress only a real no-change on the SAME actor; re-baseline (and emit)
			// when a dynamic ref (crosshair) now points at a different actor, and treat
			// NaN==NaN as no-change so a NaN AV can't spam at poll cadence.
			if (w.hasLast && actorID == w.lastActor &&
				(value == w.last || (std::isnan(value) && std::isnan(w.last)))) {
				continue;
			}
			w.last = value;
			w.lastActor = actorID;
			w.hasLast = true;
			trace::Write(trace::json{
				{ "src", "watch" },
				{ "id", id },
				{ "ref", engine::HexID(actorID) },
				{ "av", w.av },
				{ "value", value } });
		}
	}

	// Dump each armed face-watch's morphs every tick (no change-suppression: the snap
	// and the eased decay ARE the per-sample dynamics we're measuring). The ref is
	// re-resolved each tick so "speaker"/"crosshair" track the live target.
	void SampleFaceWatches()
	{
		for (const auto& ref : g_faceWatches) {
			auto* r     = engine::ResolveOne(ref);
			auto* actor = r ? r->As<RE::Actor>() : nullptr;
			if (!actor) {
				continue;  // no speaker / unloaded yet — emit once it resolves
			}
			engine::DumpFaceGen(actor, "face");
		}
	}

	// Re-attach every armed anim sink to its (possibly reloaded) actor. Idempotent:
	// AddAnimationGraphEventSink is a safe no-op when already attached, so this
	// transparently re-arms across save-loads and 3D reloads.
	void ReconcileAnim()
	{
		for (auto& [id, e] : g_animSinks) {
			if (!e.armed) {
				continue;
			}
			auto* form = RE::TESForm::LookupByID(id);
			auto* actor = form ? form->As<RE::Actor>() : nullptr;
			if (actor && actor->Is3DLoaded()) {
				actor->AddAnimationGraphEventSink(e.sink.get());
			}
		}
	}

	// Re-resolve armed event filters so a teammates/hex filter armed before a save
	// loaded (or as followers come and go) tracks the live set.
	void ReResolveFilters()
	{
		for (const auto& [name, gate] : g_gates) {
			if (!gate->enabled.load(std::memory_order_relaxed)) {
				continue;
			}
			std::vector<std::string> refs;
			{
				std::scoped_lock lk(gate->mtx);
				if (!gate->hasFilter) {
					continue;
				}
				refs = gate->refStrings;
			}
			auto resolved = engine::ResolveToFormIDs(refs);  // engine call: outside the lock
			std::scoped_lock lk(gate->mtx);
			gate->filter = std::move(resolved);
		}
	}
}

void probes::MainTick()
{
	if (g_activeWatchCount.load(std::memory_order_relaxed) > 0) {
		SampleWatches();
	}
	if (g_activeFaceWatchCount.load(std::memory_order_relaxed) > 0) {
		SampleFaceWatches();
	}
	if (g_armedAnimCount.load(std::memory_order_relaxed) > 0) {
		ReconcileAnim();
	}
	if (g_armedFilterCount.load(std::memory_order_relaxed) > 0) {
		ReResolveFilters();
	}
}

bool probes::HasMainTickWork()
{
	return g_activeWatchCount.load(std::memory_order_relaxed) > 0 ||
	       g_activeFaceWatchCount.load(std::memory_order_relaxed) > 0 ||
	       g_armedAnimCount.load(std::memory_order_relaxed) > 0 ||
	       g_armedFilterCount.load(std::memory_order_relaxed) > 0;
}

void probes::WriteStatus()
{
	// World-readiness first — this is what a host poller waits on (`inWorld:true`
	// means fully interactive). `status` acks at the main menu too, so the ack
	// alone says nothing; read these fields. Same predicate the exec gate uses.
	const engine::WorldState w = engine::GetWorldState();

	trace::json armed = trace::json::object();

	trace::json events = trace::json::object();
	for (const auto& [name, gate] : g_gates) {
		if (gate->enabled.load(std::memory_order_relaxed)) {
			std::scoped_lock lk(gate->mtx);
			events[name] = { { "hasFilter", gate->hasFilter }, { "filterSize", gate->filter.size() } };
		}
	}
	armed["events"] = std::move(events);

	trace::json anims = trace::json::array();
	for (const auto& [id, e] : g_animSinks) {
		if (e.armed) {
			anims.push_back(engine::HexID(id));
		}
	}
	armed["anim"] = std::move(anims);

	trace::json watches = trace::json::array();
	for (const auto& [id, w] : g_watches) {
		watches.push_back({ { "id", id }, { "ref", w.ref }, { "av", w.av } });
	}
	armed["watch"] = std::move(watches);

	trace::json faceWatches = trace::json::array();
	for (const auto& ref : g_faceWatches) {
		faceWatches.push_back(ref);
	}
	armed["faceWatch"] = std::move(faceWatches);

	trace::Write(trace::json{
		{ "src", "status" },
		{ "inWorld", w.inWorld },  // hoisted for a cheap grep; the world block has the breakdown
		{ "world", { { "inWorld", w.inWorld }, { "is3DLoaded", w.is3DLoaded },
		             { "mainMenu", w.mainMenu }, { "loadingMenu", w.loadingMenu } } },
		{ "armed", std::move(armed) } });
}
