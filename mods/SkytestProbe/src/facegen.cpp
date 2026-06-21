// Facegen morph snapshot and parameterized NPC-cut close helpers.
#include "facegen.h"

#include <cstdint>

#include "resolve.h"
#include "trace.h"
#include "worldstate.h"

void engine::DumpFaceGen(RE::Actor* a_actor, const char* a_src)
{
	trace::json line{ { "src", a_src } };
	// Paused-vs-running guard on EVERY facegen line: a frozen sim emits identical samples
	// (see GetSimClock). `paused:true` => don't read this as live mouth dynamics; `gt:0` =>
	// the sim advanced no game time this frame (frozen), `gt>0` => it's actively stepping.
	const SimClock clk = GetSimClock();
	line["paused"] = clk.paused;
	line["gt"]     = clk.gt;
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