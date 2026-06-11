#pragma once
// Trace writer + file-protocol paths. One mutex-guarded appender to trace.jsonl,
// one compact JSON object per line, flushed per line, every line carrying a "t"
// (epoch-ms) timestamp and a "src" tag. Thread-safe: callable from the poll
// thread (acks, markers, errors) AND the main thread (probe events, dumps).
#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

namespace trace
{
	using json = nlohmann::json;

	// Epoch milliseconds (system clock). Stamped onto every line lacking a "t".
	long long NowMs();

	// Rotate trace.jsonl -> trace.prev.jsonl (one-deep history), open a fresh
	// trace.jsonl, write the session-header line. Creates the skytest/ dir if
	// missing. Safe to call once at kDataLoaded. Degrades silently (no writer)
	// if the SKSE log dir can't be resolved.
	void Init();

	// Append one JSON object as a line. Adds "t" if absent. No-op before Init().
	void Write(json a_obj);

	// Ack line per the protocol: {"t":..,"ack":"<id>","ok":true} or
	// {"t":..,"ack":"<id>","ok":false,"err":"…"}.
	void Ack(const std::string& a_id, bool a_ok, const std::string& a_err = {});

	// Structured error line: {"t":..,"src":"error","msg":"…"}.
	void Error(const std::string& a_msg);

	// The skytest/ dir (…/SKSE/skytest). Empty path if Init() failed.
	const std::filesystem::path& Dir();

	// …/SKSE/skytest/commands.jsonl — the command file the poll thread reads.
	const std::filesystem::path& CommandsPath();
}
