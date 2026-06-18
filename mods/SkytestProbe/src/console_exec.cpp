// Console command execution via CompileAndRun (SEH-guarded, in-world gated).
#include "console_exec.h"

#include <cstdint>
#include <excpt.h>

// EXCEPTION_POINTERS for the exec-fault SEH capture. NOGDI keeps wingdi.h out so its
// `GetObject` A/W macro can't clobber RE::BSScript::Variable::GetObject() below.
#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#	define NOGDI
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include <SKSE/SKSE.h>

#include "trace.h"
#include "worldstate.h"

namespace
{
	// SEH-isolated wrapper around the fragile engine compile. The SEH filter CAPTURES the
	// fault (code / faulting instruction / accessed address) into a POD and LogExecFault
	// records it. PINNED 2026-06-16: this AVs at SkyrimSE.exe+0xce9843 reading -1, on game
	// 1.6.1170. It is NOT a headless or "missing console subsystem" issue — ConsoleLog is
	// non-null and the AV reproduces with the console menu open too. Root cause is a
	// dependency version skew: this CommonLibSSE-NG (v3.7.0-129, Sep 2024) PREDATES the
	// 1.6.1170 runtime, and CompileAndRun's bound Address Library id (AE 21890) is ABSENT
	// from the 1.6.1170 versionlib. CommonLib's non-VR id lookup (REL/ID.h) does not verify
	// the matched id, so a missing id silently resolves to the NEXT id and calls the wrong
	// function -> AV. exec is therefore mis-bound on this version; stage via direct-call
	// probe commands (give-spell/set-av/...), which is the harness design, not a fallback.
	// LogExecFault runs OUTSIDE the SEH frame; this function constructs no C++ object needing
	// unwinding (SEH rule). Catching the AV honors the "never crash the game on bad input"
	// contract — in a CommonLib build that matches the runtime, exec would run for real.
	struct ExecFault
	{
		std::uint32_t  code       = 0;
		std::uintptr_t ip         = 0;           // faulting instruction address
		std::uintptr_t access     = 0;           // address the faulting insn touched (AV)
		std::uint32_t  accessKind = 0xFFFFFFFF;  // 0 read, 1 write, 8 execute
		bool           hit        = false;
	};

	int CaptureExecFault(::EXCEPTION_POINTERS* a_ep, ExecFault& a_out)
	{
		a_out.hit = true;
		if (a_ep && a_ep->ExceptionRecord) {
			const auto* rec = a_ep->ExceptionRecord;
			a_out.code = static_cast<std::uint32_t>(rec->ExceptionCode);
			a_out.ip   = reinterpret_cast<std::uintptr_t>(rec->ExceptionAddress);
			if (rec->NumberParameters >= 2) {
				a_out.accessKind = static_cast<std::uint32_t>(rec->ExceptionInformation[0]);
				a_out.access     = static_cast<std::uintptr_t>(rec->ExceptionInformation[1]);
			}
		}
		return EXCEPTION_EXECUTE_HANDLER;
	}

	void LogExecFault(const ExecFault& a_f)
	{
		const std::uintptr_t base = REL::Module::get().base();
		SKSE::log::warn(
			"exec FAULT: code={:#010x} ip={:#x} module_base={:#x} ip_rva={:#x} access_addr={:#x} access_kind={}",
			a_f.code, a_f.ip, base, (a_f.ip >= base ? a_f.ip - base : std::uintptr_t{ 0 }),
			a_f.access, a_f.accessKind);
	}

	bool SafeCompileAndRun(RE::Script* a_script, RE::TESObjectREFR* a_target) noexcept
	{
		ExecFault fault;  // POD; filled by the SEH filter if CompileAndRun AVs
		__try {
			a_script->CompileAndRun(a_target);
		} __except (CaptureExecFault(GetExceptionInformation(), fault)) {
			// details logged below, outside the SEH frame
		}
		if (!fault.hit) {
			return true;
		}
		LogExecFault(fault);
		return false;
	}
}

engine::ExecResult engine::RunConsoleCommand(const std::string& a_line)
{
	if (a_line.empty()) {
		return ExecResult::kEmpty;
	}
	// CompileAndRun needs a FULLY-LOADED, interactive world — at the main menu / loading
	// screen the console-compiler globals are uninitialized and it crashes. IsInWorld() is
	// that exact gate (shared with `status` so exec and status can never disagree). NB: even
	// past this gate, exec faults on game 1.6.1170 — but NOT because of headless or "in-world"
	// state: CommonLib mis-binds CompileAndRun on this runtime (stale AE id 21890, see
	// SafeCompileAndRun above). The gate is still correct; the fault is the dependency skew.
	if (!IsInWorld()) {
		return ExecResult::kNotInWorld;
	}
	// The ENGINE must construct the Script: `new RE::Script()` would force this TU
	// to emit the Script/TESForm vtable, whose virtuals are address-library engine
	// code (unresolved at link). The form factory hands back an engine-constructed
	// Script with a valid vtable (ConsoleUtilSSE-NG technique). delete is fine — the
	// virtual dtor dispatches through that engine vtable, needing no link symbol.
	auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
	auto* form = factory ? factory->Create() : nullptr;
	auto* script = form ? form->As<RE::Script>() : nullptr;
	if (!script) {
		return ExecResult::kFaulted;  // couldn't create the Script form
	}
	script->SetCommand(a_line);
	// Pre-call state, kept as permanent diagnostics. (This DISPROVED the "missing console
	// subsystem" theory: consolelog is non-null here and exec faults with the menu open too;
	// the real cause is the stale CommonLib binding — see SafeCompileAndRun.)
	SKSE::log::info("exec pre: line='{}' script={:#x} consolelog={:#x} consolemenu_open={}",
		a_line, reinterpret_cast<std::uintptr_t>(script),
		reinterpret_cast<std::uintptr_t>(RE::ConsoleLog::GetSingleton()),
		IsMenuOpen(std::string(RE::Console::MENU_NAME)));
	const bool ran = SafeCompileAndRun(script, nullptr);  // null target = no selected ref
	if (!ran) {
		// Don't delete after a caught AV: the form may be in an indeterminate state and
		// the virtual dtor dispatch could itself fault. Leaking one tiny Script is fine.
		SKSE::log::warn("exec: CompileAndRun faulted for '{}' (see 'exec FAULT' line for the pinned address)", a_line);
		return ExecResult::kFaulted;
	}
	delete script;
	return ExecResult::kOk;
}