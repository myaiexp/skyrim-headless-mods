#pragma once
// The probe registry. Engine event sinks are registered ONCE at kDataLoaded and
// left inert (atomic gate + optional FormID filter) — arming flips the gate and
// swaps the filter, never re-registers. Anim-graph sinks are attached per-actor on
// demand and kept alive for the process lifetime (the engine holds a raw pointer;
// freeing a still-attached sink is a UAF), re-attached opportunistically each tick
// so they survive save-loads / 3D reloads. Watches are sampled at poll cadence.
//
// Threading: RegisterEventSinks/ArmTrace/ArmAnim/ArmWatch/MainTick/WriteStatus all
// run on the MAIN thread (command handlers marshal via the SKSE task queue). The
// engine invokes sink ProcessEvent on its own event thread; the EventGate mutex
// guards the filter across that boundary. The poll thread only reads the atomic
// HasMainTickWork() flag.
#include <string>
#include <vector>

#include <RE/Skyrim.h>

namespace probes
{
	// One-time registration of the 7 engine event sinks. Main thread, kDataLoaded.
	void RegisterEventSinks();

	// `trace` command. Arm (on=true) or disarm (on=false) the named event sinks
	// ("hit","equip","magic-apply","combat","activate","container","death"). refs is
	// the raw ref-keyword list (player/crosshair/teammates/hex), resolved here on the
	// main thread; an empty refs list => match-all, a non-empty list => log only
	// events touching those refs (re-resolved each tick so teammates populate after a
	// load). Re-arming replaces the filter. Returns the unknown event names; when any
	// are unknown NOTHING is armed (all-or-nothing).
	std::vector<std::string> ArmTrace(const std::vector<std::string>& a_events, bool a_on,
	                                  const std::vector<std::string>& a_refs);

	// `anim-trace` command. Attach (on) / detach (off) an anim-graph sink to one
	// actor. The sink object is never freed (kept alive for process lifetime); off
	// just detaches + clears the armed flag. Returns false if attach found no loaded
	// 3D yet (the tick will retry).
	bool ArmAnim(RE::Actor* a_actor, bool a_on);
	// Detach + disarm every anim sink (`anim-trace off ref:all`).
	void DisarmAllAnim();

	// `watch` command. Sample a_av on a_ref at poll cadence, log on change. on=false
	// removes the watch. id keys the watch (ref+":"+av).
	void ArmWatch(const std::string& a_id, const std::string& a_ref,
	              const std::string& a_av, bool a_on);

	// `facegen-watch` command. Sample a_ref's facegen morphs at poll cadence (~4 Hz),
	// logging EVERY tick while armed (a time series — the snap/decay dynamics need
	// every sample, not change-gating). on=false removes it. Keyed by the ref string.
	void ArmFaceWatch(const std::string& a_ref, bool a_on);

	// Main-thread tick enqueued by the poll thread when HasMainTickWork(): samples
	// active watches, re-attaches armed anim sinks (survives loads/3D reloads), and
	// re-resolves armed event filters (teammates membership tracking).
	void MainTick();
	// Cheap atomic check so the poll thread only enqueues a tick when there's work.
	bool HasMainTickWork();

	// `status` command. Write a trace line listing every currently-armed probe.
	void WriteStatus();
}
