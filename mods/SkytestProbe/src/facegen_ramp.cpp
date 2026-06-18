// Owned facegen transition-target ramp with per-frame apply hook.
#include "facegen_ramp.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

#include <SKSE/SKSE.h>

#include "resolve.h"
#include "trace.h"

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