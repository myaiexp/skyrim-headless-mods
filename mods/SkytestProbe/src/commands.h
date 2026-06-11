#pragma once
// Command reader. A background thread polls commands.jsonl (~pollIntervalMs),
// reopening the file each tick (defeats wine read-caching). At startup it executes
// the whole file from the top, then processes only newly-appended complete lines;
// a truncated/rewritten file is detected (size < offset) and re-read from the top.
// Parsing is off-thread and engine-free; every command's actual work is marshalled
// onto the main thread via the SKSE task queue. Each command gets a trace ack line.
namespace commands
{
	// Start the poll thread. Call once at kDataLoaded (after trace::Init). Idempotent.
	void Start();
}
