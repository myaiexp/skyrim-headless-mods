#include "trace.h"

#include <chrono>
#include <fstream>
#include <mutex>

#include <SKSE/SKSE.h>

namespace
{
	std::mutex            g_mtx;        // guards the appender + paths below
	std::ofstream         g_out;        // append handle to trace.jsonl
	bool                  g_ready = false;
	std::filesystem::path g_dir;        // …/SKSE/skytest
	std::filesystem::path g_commands;   // …/SKSE/skytest/commands.jsonl
	std::filesystem::path g_trace;      // …/SKSE/skytest/trace.jsonl
	std::filesystem::path g_tracePrev;  // …/SKSE/skytest/trace.prev.jsonl

	// Compact, non-ascii-escaping dump with the `replace` error handler so a
	// non-UTF-8 byte from an engine string (actor names can be CP-1252) becomes
	// U+FFFD instead of throwing and stalling the writer.
	std::string DumpLine(const trace::json& a_obj)
	{
		return a_obj.dump(-1, ' ', false, trace::json::error_handler_t::replace);
	}
}

long long trace::NowMs()
{
	using namespace std::chrono;
	return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

void trace::Init()
{
	std::scoped_lock lk(g_mtx);

	auto logDir = SKSE::log::log_directory();
	if (!logDir) {
		SKSE::log::error("trace: log_directory() unresolved; trace writer disabled");
		return;
	}

	g_dir       = *logDir / "skytest";
	g_commands  = g_dir / "commands.jsonl";
	g_trace     = g_dir / "trace.jsonl";
	g_tracePrev = g_dir / "trace.prev.jsonl";

	std::error_code ec;
	std::filesystem::create_directories(g_dir, ec);
	if (ec) {
		SKSE::log::error("trace: could not create {}: {}", g_dir.string(), ec.message());
		return;
	}

	// One-deep history: previous trace.jsonl -> trace.prev.jsonl (overwrite).
	if (std::filesystem::exists(g_trace, ec)) {
		std::filesystem::remove(g_tracePrev, ec);
		std::filesystem::rename(g_trace, g_tracePrev, ec);  // best-effort; ignore ec
	}

	g_out.open(g_trace, std::ios::out | std::ios::trunc);
	if (!g_out.is_open()) {
		SKSE::log::error("trace: could not open {} for writing", g_trace.string());
		return;
	}
	g_ready = true;

	json header{
		{ "t", NowMs() },
		{ "src", "session" },
		{ "plugin", "0.1.0" },
		{ "game", "1.6.1170" }
	};
	g_out << DumpLine(header) << '\n';
	g_out.flush();

	SKSE::log::info("trace: writing {}", g_trace.string());
}

void trace::Write(json a_obj)
{
	std::scoped_lock lk(g_mtx);
	if (!g_ready) {
		return;
	}
	if (!a_obj.contains("t")) {
		a_obj["t"] = NowMs();
	}
	g_out << DumpLine(a_obj) << '\n';
	g_out.flush();
}

void trace::Ack(const std::string& a_id, bool a_ok, const std::string& a_err)
{
	json ack{ { "t", NowMs() }, { "ack", a_id }, { "ok", a_ok } };
	if (!a_ok && !a_err.empty()) {
		ack["err"] = a_err;
	}
	Write(std::move(ack));
}

void trace::Error(const std::string& a_msg)
{
	Write(json{ { "src", "error" }, { "msg", a_msg } });
}

const std::filesystem::path& trace::Dir()
{
	return g_dir;
}

const std::filesystem::path& trace::CommandsPath()
{
	return g_commands;
}
