#pragma once
// Console command execution via CompileAndRun (SEH-guarded, in-world gated).
#include <string>

#include <RE/Skyrim.h>

namespace engine
{
	// Run one console command line, fire-and-forget (ConsoleUtilSSE-NG technique).
	enum class ExecResult
	{
		kOk,          // compiled + ran
		kEmpty,       // empty command line
		kNotInWorld,  // main menu / mid-load: gated out (CompileAndRun would crash)
		kFaulted,     // reached CompileAndRun but it AV'd (caught by SEH; CommonLib mis-binds it on 1.6.1170 — stale AE id 21890)
	};
	ExecResult RunConsoleCommand(const std::string& a_line);
}