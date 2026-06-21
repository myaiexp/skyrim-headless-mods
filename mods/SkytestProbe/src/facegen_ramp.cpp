// Owned facegen transition-target ramp with per-frame apply hook.
#include "facegen_ramp.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <SKSE/SKSE.h>

#include "resolve.h"
#include "trace.h"
#include "worldstate.h"

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
	// A captured value array for one keyframe channel (one channel of a mouth pose).
	struct ChanVals
	{
		std::string        kf;
		std::vector<float> vals;
	};
	// One channel's ease: from the speech-open pose toward the rest pose, over the ramp duration.
	struct ChanEase
	{
		std::string        kf;
		std::vector<float> from;  // speech-open pose (captured just before the skip)
		std::vector<float> to;    // rest pose (captured during a silence) — empty => ease to 0
	};
	// The mouth channels the morph-apply (FUN_140432550) actually reads — ch0/ch1/ch3. Easing only
	// unk140 (ch0, lips) left unk0C0/unk180 to SNAP at the skip, so the inner-mouth/tongue flashed to
	// its rest pose while the lips still glided. The skip-ease eases ALL of these in lockstep.
	constexpr std::array<const char*, 3> kMouthChans{ "unk0C0", "unk140", "unk180" };

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
		std::vector<float>          snapshot;        // start values captured at trigger (single-kf snapshot mode)
		std::vector<ChanEase>       mouthChans;      // multi-channel skip-ease: per-channel open->rest ease (empty
		                                             // => single-kf ramp via p.kf). The hook BLENDS each channel
		                                             // from the speech-open pose toward the captured rest pose.
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

	// ---- per-frame observe state (read-only characterization; main-thread, like g_ramp) -------
	// The hook logs the target fg's mouth keyframe(s) every frame while active, never modifying
	// them. g_observeFg is PINNED at arm: the hook matches only this fg, so a stale pointer after a
	// 3D reload simply stops matching (never dereferenced) — safe, just needs a re-arm. g_observeKf
	// empty => all kMouthChans, else one keyframe. g_observeLastTime dedups a second pass per frame.
	std::atomic<bool>           g_observeActive{ false };
	RE::BSFaceGenAnimationData* g_observeFg = nullptr;
	std::string                 g_observeKf;
	float                       g_observeLastTime = -1.0F;

	// Map a keyframe tag (as printed by DumpFaceGen, e.g. "unk140", "transitionTarget") to the
	// matching keyframe on this facegen. transitionTarget is a POINTER member (0x18, may be null);
	// every other keyframe is INLINE in the struct, so we return its address. The set mirrors the
	// facegen dump exactly, so a tag copied straight from a sweep line targets the same keyframe.
	RE::BSFaceGenKeyframeMultiple* SelectKeyframe(RE::BSFaceGenAnimationData* a_fg, const std::string& a_kf)
	{
		if (!a_fg) {
			return nullptr;
		}
		if (a_kf == "transitionTarget") return a_fg->transitionTargetKeyFrame;
		if (a_kf == "expression")       return &a_fg->expressionKeyFrame;
		if (a_kf == "unk040")           return &a_fg->unk040;
		if (a_kf == "modifier")         return &a_fg->modifierKeyFrame;
		if (a_kf == "phoneme")          return &a_fg->phenomeKeyFrame;
		if (a_kf == "custom")           return &a_fg->customKeyFrame;
		if (a_kf == "unk0C0")           return &a_fg->unk0C0;
		if (a_kf == "unk0E0")           return &a_fg->unk0E0;
		if (a_kf == "unk100")           return &a_fg->unk100;
		if (a_kf == "unk120")           return &a_fg->unk120;
		if (a_kf == "unk140")           return &a_fg->unk140;
		if (a_kf == "unk160")           return &a_fg->unk160;
		if (a_kf == "unk180")           return &a_fg->unk180;
		return nullptr;  // unknown tag
	}

	// Read a keyframe's max value (and its index) under fg->lock. count==0 -> max 0.
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
		auto* kf    = SelectKeyframe(fg, p.kf);
		if (!kf) {
			// No speaker / 3D not loaded / target keyframe not present yet — keep waiting (the
			// ticker re-enqueues) up to the timeout.
			if (!g_ramp.triggered && now - g_ramp.armMs > static_cast<long long>(p.waitMs)) {
				FinishRamp("ramp-abort", trace::json{ { "reason", "no facegen/" + p.kf + " before waitMs" } });
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
				if (p.snapshot) {
					RE::BSSpinLockGuard locker(fg->lock);
					const std::uint32_t n = (kf->values && kf->count <= 256) ? kf->count : 0;
					g_ramp.snapshot.assign(kf->values, kf->values + n);
				}
				g_rampPhase.store(1, std::memory_order_relaxed);  // speed the ticker up for the done-poll
				trace::Write(trace::json{ { "src", "ramp-trigger" },
					{ "ref", engine::HexID(actor->GetFormID()) }, { "kf", p.kf }, { "max", m }, { "maxIdx", mi },
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
		auto* kf = SelectKeyframe(fg, g_ramp.p.kf);
		if (!kf || !kf->values) {
			return;
		}
		const auto&     p       = g_ramp.p;
		const long long elapsed = trace::NowMs() - g_ramp.startMs;
		// SELF-RETIRE on the render loop. RampStep's retirement runs on the SKSE task queue, which
		// STALLS in dialogue/menus while THIS hook keeps firing on the render loop — that desync
		// left transitionTarget pinned at 0 (mouth shut) for minutes (regression 2026-06-18: a
		// 205 s freeze). Retiring here bounds the hold to ms+holdMs regardless of the task queue.
		// Same thread as the rest of this hook, so touching g_ramp via FinishRamp is race-free.
		if (elapsed >= static_cast<long long>(p.ms + p.holdMs)) {
			FinishRamp("ramp-done", trace::json{ { "elapsed", elapsed }, { "via", "hook" } });
			return;
		}
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

		// Multi-channel skip-ease: blend EACH apply-read mouth channel from its captured speech-open pose
		// toward its captured REST pose — values[i] = from[i]*(1-t) + to[i]*t. At t=0 the speech pose, at
		// t=1 the rest pose, so lips + tongue + inner mouth all settle to neutral in lockstep (no channel
		// snaps ahead). NOTE: the t=1 target is a CAPTURED rest pose, NOT the live value — during the PC's
		// dialogue the menu freezes the face at the open pose, so blending toward live would hold it open.
		if (!g_ramp.mouthChans.empty()) {
			float         unk140After = 0.0F;
			std::uint32_t u140Idx = 0;
			RE::BSSpinLockGuard locker(fg->lock);
			for (const auto& ch : g_ramp.mouthChans) {
				auto* ckf = SelectKeyframe(fg, ch.kf);
				if (!ckf || !ckf->values) {
					continue;
				}
				const std::uint32_t n = std::min<std::uint32_t>(ckf->count,
					static_cast<std::uint32_t>(std::min<std::size_t>(ch.from.size(), 256)));
				for (std::uint32_t i = 0; i < n; ++i) {
					const float to = (i < ch.to.size()) ? ch.to[i] : 0.0F;  // no rest captured -> ease to 0
					ckf->SetValue(i, ch.from[i] * factor + to * t);
				}
				if (ch.kf == "unk140") {
					unk140After = TtMax(ckf, u140Idx);
				}
			}
			trace::Write(trace::json{ { "src", "ramp" }, { "via", "hook" }, { "mode", "blend" },
				{ "frac", t }, { "elapsed", elapsed }, { "phase", ramping ? "ramp" : "hold" },
				{ "chans", g_ramp.mouthChans.size() }, { "unk140After", unk140After } });
			return;
		}

		float         maxBefore = 0.0F, maxAfter = 0.0F;
		std::uint32_t mbIdx = 0, maIdx = 0;
		{
			RE::BSSpinLockGuard locker(fg->lock);
			maxBefore = TtMax(kf, mbIdx);  // the pump's full value this frame
			const std::uint32_t n = std::min<std::uint32_t>(kf->count, 256);
			for (std::uint32_t i = 0; i < n; ++i) {
				// scale mode: ease the LIVE value (works while the pump keeps it alive). snapshot mode:
				// ease the value captured at trigger (robust if the engine zeroed the live value — the
				// snap). SetValue clears isUpdated so the deferred apply re-reads our write.
				const float base = (p.snapshot && i < g_ramp.snapshot.size()) ? g_ramp.snapshot[i] : kf->values[i];
				kf->SetValue(i, base * factor);
			}
			maxAfter = TtMax(kf, maIdx);  // == what the original's apply will render
		}
		trace::Write(trace::json{ { "src", "ramp" }, { "via", "hook" }, { "kf", p.kf }, { "frac", t },
			{ "elapsed", elapsed }, { "phase", ramping ? "ramp" : "hold" }, { "snapshot", p.snapshot },
			{ "maxBefore", maxBefore }, { "maxBeforeIdx", mbIdx },
			{ "maxAfter", maxAfter }, { "maxAfterIdx", maIdx } });
	}

	// Per-frame OBSERVE (read-only): while armed for THIS node's facegen, log the target mouth
	// keyframe(s)' max every frame WITHOUT touching them — the per-frame seam the 4 Hz facegen-watch
	// can't reach. Runs in the same hook (same main thread), so it sees the pump's value at the same
	// apply point the ramp scales at. Never SetValue — purely characterization.
	void ObserveFrame(RE::BSFaceGenNiNode* a_node, float a_time)
	{
		if (!g_observeActive.load(std::memory_order_relaxed) || !a_node) {
			return;
		}
		auto* fg = a_node->GetRuntimeData().animationData.get();
		if (!fg || fg != g_observeFg) {
			return;  // a different actor's head (or our pinned fg went stale on a 3D reload)
		}
		if (a_time == g_observeLastTime) {
			return;  // already logged this frame (a second downward pass)
		}
		g_observeLastTime = a_time;
		const engine::SimClock clk = engine::GetSimClock();
		RE::BSSpinLockGuard    locker(fg->lock);
		auto logKf = [&](const char* a_tag) {
			auto* kf = SelectKeyframe(fg, a_tag);
			if (!kf) {
				return;
			}
			std::uint32_t idx = 0;
			const float   mx  = TtMax(kf, idx);
			trace::Write(trace::json{ { "src", "face-frame" }, { "kf", a_tag }, { "max", mx },
				{ "maxIdx", idx }, { "time", a_time }, { "gt", clk.gt }, { "paused", clk.paused } });
		};
		if (g_observeKf.empty()) {
			for (const char* tag : kMouthChans) {
				logKf(tag);
			}
		} else {
			logKf(g_observeKf.c_str());
		}
	}

	// The hook itself — a vtable detour on BSFaceGenNiNode::UpdateDownwardPass (idx 0x2C). Scale
	// pre-apply, then run the original so the engine applies our eased transitionTarget.
	struct FaceGenUpdateHook
	{
		static void thunk(RE::BSFaceGenNiNode* a_this, RE::NiUpdateData& a_data, std::uint32_t a_arg2)
		{
			ApplyRampScale(a_this, a_data.time);
			ObserveFrame(a_this, a_data.time);  // read-only per-frame characterization (idle unless armed)
			func(a_this, a_data, a_arg2);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// ---- skip-ease: react to the real CutNpcDBVOReply skip ------------------------------------
	std::atomic<bool>      g_skipEaseArmed{ false };
	engine::SkipEaseParams g_skipEaseParams;  // main-thread only (ArmSkipEase + TriggerEaseNow both marshal there)

	// Rolling capture: the real skip zeroes the mouth keyframes within a frame — before our deferred
	// trigger can read them (every skip-ease-trigger logged max:0) — and during the PC's reply the menu
	// FREEZES the face at the open pose. So while armed, the ticker samples the speaker each tick and
	// keeps TWO poses by the unk140 gate: the last OPEN pose (mid-word, the ease START) and the last
	// REST pose (a silence between words, the ease TARGET). Both per-channel. Main-thread only.
	std::vector<ChanVals>       g_speakerOpen;          // last speech-open pose (all kMouthChans)
	std::vector<ChanVals>       g_speakerRest;          // last rest pose (silence) — the neutral mouth
	RE::BSFaceGenAnimationData* g_openFg = nullptr;     // whose facegen the OPEN pose came from
	long long                   g_openMs = 0;           // when (NowMs) — recency gate at skip time
	float                       g_openMax = 0.0F;       // unk140 max at open capture
	RE::BSFaceGenAnimationData* g_restFg = nullptr;     // whose facegen the REST pose came from

	// Capture every apply-read channel's current values into a_dst.
	void CaptureChans(RE::BSFaceGenAnimationData* a_fg, std::vector<ChanVals>& a_dst)
	{
		a_dst.clear();
		for (const char* tag : kMouthChans) {
			auto* kf = SelectKeyframe(a_fg, tag);
			if (!kf) {
				continue;
			}
			const std::uint32_t n = (kf->values && kf->count <= 256) ? kf->count : 0;
			a_dst.push_back(ChanVals{ tag, std::vector<float>(kf->values, kf->values + n) });
		}
	}

	// Sample the live speaker; route the pose to OPEN (mid-word) or REST (silence) by the unk140 gate.
	// Main thread (ticker-enqueued). Cheap: one resolve + a few locked max scans + a copy.
	void SampleSpeakerSnap()
	{
		auto* r     = engine::ResolveOne("speaker");
		auto* actor = r ? r->As<RE::Actor>() : nullptr;
		auto* fg    = actor ? actor->GetFaceGenAnimationData() : nullptr;
		auto* gate  = SelectKeyframe(fg, "unk140");  // unk140 = the speech-open indicator
		if (!gate) {
			return;
		}
		RE::BSSpinLockGuard locker(fg->lock);
		std::uint32_t mi = 0;
		const float   m  = TtMax(gate, mi);
		if (m > 0.05F) {  // mid-word: this is a speech-open pose
			CaptureChans(fg, g_speakerOpen);
			g_openFg  = fg;
			g_openMs  = trace::NowMs();
			g_openMax = m;
		} else {  // silence: the neutral/rest mouth (the ease target)
			CaptureChans(fg, g_speakerRest);
			g_restFg = fg;
		}
	}

	// The pacer. One persistent thread (lazily started): idles cheaply when neither a ramp nor a
	// skip-ease arm is live, else enqueues exactly ONE main-thread task per wake — RampStep while a
	// ramp runs, otherwise the rolling pre-snap capture. External-pacer pattern: never self-re-queues.
	void RampTickerLoop()
	{
		for (;;) {
			const bool rampActive = g_rampActive.load(std::memory_order_relaxed);
			const bool snapArmed  = g_skipEaseArmed.load(std::memory_order_relaxed);
			if (!rampActive && !snapArmed) {
				std::this_thread::sleep_for(std::chrono::milliseconds(150));
				continue;
			}
			if (auto* task = SKSE::GetTaskInterface()) {
				if (rampActive) {
					const int gen = g_rampGen.load(std::memory_order_relaxed);
					task->AddTask([gen]() { RampStep(gen); });
				} else {
					task->AddTask([]() { SampleSpeakerSnap(); });  // keep the pre-snap value fresh
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(
				(rampActive && g_rampPhase.load(std::memory_order_relaxed) == 1) ? 16 : 66));
		}
	}

	// Start an eased close IMMEDIATELY on the live speaker — no WAIT/threshold phase. Runs on the main
	// thread (the sink marshals here via AddTask). Sets the ramp already-triggered and hands the target
	// to the per-frame hook; the ticker is reused only to retire it. cut:false because DBVO's CutNpcReply
	// already faded the audio — we only own the mouth keyframe.
	void TriggerEaseNow()
	{
		auto* r     = engine::ResolveOne("speaker");
		auto* actor = r ? r->As<RE::Actor>() : nullptr;
		auto* fg    = actor ? actor->GetFaceGenAnimationData() : nullptr;
		if (!fg) {
			trace::Write(trace::json{ { "src", "skip-ease" }, { "skip", "no speaker facegen at skip" } });
			return;
		}
		const long long now = trace::NowMs();
		// Ease from the speech-OPEN pose toward the REST pose. REQUIRE a fresh open capture from THIS
		// speaker (the skip already zeroed the live keyframes) — no recent open pose => nothing to ease.
		const bool haveOpen = (g_openFg == fg) && !g_speakerOpen.empty() &&
		                      (now - g_openMs <= 250) && (g_openMax > 0.05F);
		if (!haveOpen) {
			trace::Write(trace::json{ { "src", "skip-ease" }, { "skip", "no fresh open pose" },
				{ "openAgeMs", now - g_openMs }, { "openMax", g_openMax } });
			return;
		}
		// Pair each open channel with its rest target (same fg's last silence pose); missing -> ease to 0.
		const bool haveRest = (g_restFg == fg) && !g_speakerRest.empty();
		g_ramp.mouthChans.clear();
		for (const auto& oc : g_speakerOpen) {
			ChanEase ease{ oc.kf, oc.vals, {} };
			if (haveRest) {
				for (const auto& rc : g_speakerRest) {
					if (rc.kf == oc.kf) {
						ease.to = rc.vals;
						break;
					}
				}
			}
			g_ramp.mouthChans.push_back(std::move(ease));
		}

		engine::FaceGenRampParams p;
		p.ref          = "speaker";
		p.kf           = "unk140";  // the lip channel — used for the per-frame trace readout
		p.ms           = g_skipEaseParams.ms;
		p.holdMs       = g_skipEaseParams.holdMs;
		p.snapshot     = true;
		p.reassert     = true;
		p.cut          = false;
		p.speakingDone = false;
		p.threshold    = 0.0F;
		p.waitMs       = 0.0F;

		g_ramp.p            = p;
		g_ramp.done         = false;
		g_ramp.targetFg     = fg;
		g_ramp.lastHookTime = -1.0F;
		g_ramp.triggered    = true;
		g_ramp.startMs      = now;
		g_ramp.armMs        = now;
		g_rampPhase.store(1, std::memory_order_relaxed);
		g_rampGen.fetch_add(1, std::memory_order_relaxed);
		g_rampActive.store(true, std::memory_order_relaxed);
		if (!g_tickerStarted.exchange(true)) {
			std::thread(RampTickerLoop).detach();
		}
		trace::Write(trace::json{ { "src", "skip-ease-trigger" }, { "ref", engine::HexID(actor->GetFormID()) },
			{ "chans", g_ramp.mouthChans.size() }, { "openMax", g_openMax }, { "openAgeMs", now - g_openMs },
			{ "haveRest", haveRest }, { "ms", p.ms }, { "holdMs", p.holdMs } });
	}

	// Sink DBVO's swf skip mod-events. On "CutNpcDBVOReply" (the new-topic skip), while armed, marshal
	// an immediate eased close. kContinue so DBVODialogueTweaks' own CutNpcReply sink still runs.
	class SkipEaseSink : public RE::BSTEventSink<SKSE::ModCallbackEvent>
	{
	public:
		static SkipEaseSink* GetSingleton()
		{
			static SkipEaseSink singleton;
			return &singleton;
		}
		RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event,
			RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
		{
			if (a_event && g_skipEaseArmed.load(std::memory_order_relaxed) &&
				a_event->eventName == "CutNpcDBVOReply") {
				if (auto* task = SKSE::GetTaskInterface()) {
					task->AddTask([]() { TriggerEaseNow(); });
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}

	private:
		SkipEaseSink() = default;
	};
}

void engine::StartFaceGenRamp(const FaceGenRampParams& a_params)
{
	// Reset rich state (main thread) BEFORE flipping the atomics the ticker reads.
	g_ramp.triggered    = false;
	g_ramp.done         = false;
	g_ramp.targetFg     = nullptr;
	g_ramp.lastHookTime = -1.0F;
	g_ramp.mouthChans.clear();  // single-kf test ramp — not the multi-channel skip-ease blend
	g_ramp.armMs        = trace::NowMs();
	g_ramp.p            = a_params;
	g_rampPhase.store(0, std::memory_order_relaxed);
	g_rampGen.fetch_add(1, std::memory_order_relaxed);  // supersede any in-flight step
	g_rampActive.store(true, std::memory_order_relaxed);
	if (!g_tickerStarted.exchange(true)) {
		std::thread(RampTickerLoop).detach();
	}
	trace::Write(trace::json{ { "src", "ramp-arm" }, { "ref", a_params.ref }, { "kf", a_params.kf },
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

bool engine::ArmFaceGenObserve(const std::string& a_ref, const std::string& a_kf, bool a_on, std::string& a_err)
{
	if (!a_on) {
		g_observeActive.store(false, std::memory_order_relaxed);
		g_observeFg       = nullptr;
		g_observeKf.clear();
		g_observeLastTime = -1.0F;
		trace::Write(trace::json{ { "src", "face-observe" }, { "on", false } });
		return true;
	}
	auto* r     = engine::ResolveOne(a_ref);
	auto* actor = r ? r->As<RE::Actor>() : nullptr;
	if (!actor) {
		a_err = "unresolvable actor " + a_ref;
		return false;
	}
	auto* fg = actor->GetFaceGenAnimationData();
	if (!fg) {
		a_err = "no facegen data (actor 3D unloaded?)";
		return false;
	}
	if (!a_kf.empty() && !SelectKeyframe(fg, a_kf)) {
		a_err = "unknown keyframe tag '" + a_kf + "'";
		return false;
	}
	g_observeFg       = fg;
	g_observeKf       = a_kf;
	g_observeLastTime = -1.0F;
	g_observeActive.store(true, std::memory_order_relaxed);
	trace::Write(trace::json{ { "src", "face-observe" }, { "on", true },
		{ "ref", engine::HexID(actor->GetFormID()) }, { "kf", a_kf.empty() ? "<mouth>" : a_kf } });
	return true;
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

void engine::ArmSkipEase(bool a_on, const SkipEaseParams& a_params)
{
	g_skipEaseParams = a_params;  // main thread (commands marshals via EnqueueMain)
	g_skipEaseArmed.store(a_on, std::memory_order_relaxed);
	if (a_on) {
		// start the pacer so it begins the rolling pre-snap capture immediately
		if (!g_tickerStarted.exchange(true)) {
			std::thread(RampTickerLoop).detach();
		}
	} else {
		CancelFaceGenRamp();  // stand down any in-flight ease
	}
	trace::Write(trace::json{ { "src", "skip-ease-arm" }, { "on", a_on }, { "kf", a_params.kf },
		{ "ms", a_params.ms }, { "holdMs", a_params.holdMs }, { "snapshot", a_params.snapshot } });
}

void engine::InstallSkipEaseSink()
{
	// The mod-callback event source isn't up before game data loads — install at kDataLoaded.
	if (auto* source = SKSE::GetModCallbackEventSource()) {
		source->AddEventSink(SkipEaseSink::GetSingleton());
		SKSE::log::info("SkytestProbe: CutNpcDBVOReply skip-ease sink installed");
	}
}