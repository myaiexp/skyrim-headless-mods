#pragma once
// Dispatch a Papyrus GLOBAL (static) function from a probe command.
//
// Why this exists: a mod's settings API is often a `Global Native` on its own script —
// DBVOTweaks.SetPlayerVoiceVolume(Float), registered by its SKSE DLL — and a replay stage with
// no SkyUI/MCM has no player-facing way to reach it. The console can't either: on game 1.7.104
// `cgf` / `callglobalfunction` both answer "Script command … not found" (verified in-engine
// 2026-09-08). This is the direct-call substitute, the same idea as ui-invoke for
// UI.InvokeString: hand the VM the class, the function and typed arguments exactly as the mod's
// own Papyrus would, so every mod's Papyrus API becomes drivable from a .steps script.
//
// A dispatch is a QUEUE, not a call. The VM runs the function on a script stack of its own and
// hands the return value to a callback afterwards, so the command acks "queued" and the callback
// writes the src:"papyrus-call" trace line — that line, not the ack, is the completion signal.
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace engine
{
	// One typed argument. The VM binds arguments against the function's declared signature
	// without int<->float coercion, so the type has to be chosen by the caller: the command
	// layer maps a JSON integral to int and any other JSON number to float (`2.0` for a Float
	// parameter, `2` for an Int one).
	using PapyrusArg = std::variant<std::int32_t, float, bool, std::string>;

	enum class PapyrusCallResult
	{
		kOk,       // queued; the completion trace line follows when the stack finishes
		kNoVM,     // the Papyrus VM singleton is unavailable (too early in boot)
		kRefused   // DispatchStaticCall returned false (class or function not found)
	};

	// Queue a_class.a_function(a_args...) on the VM. Main-thread only. The completion callback
	// runs on whichever thread the VM finishes the stack on and writes
	// {"src":"papyrus-call","class":…,"function":…,"ok":true,"result":<scalar, or null for
	// None / object / array>} through the thread-safe trace writer.
	PapyrusCallResult DispatchPapyrusStatic(const std::string& a_class, const std::string& a_function,
		const std::vector<PapyrusArg>& a_args);
}
