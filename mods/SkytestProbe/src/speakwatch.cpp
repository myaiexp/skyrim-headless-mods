#include "speakwatch.h"

#include <algorithm>
#include <atomic>
#include <mutex>

#include <MinHook.h>
#include <SKSE/SKSE.h>

#include "trace.h"

namespace
{
	std::mutex        g_mtx;
	RE::BSSoundHandle g_line;         // by-value copy of the player's line; guarded by g_mtx
	long long         g_startMs = 0;  // when the hook saw it start
	bool              g_sawPlaying = false;
	bool              g_reported = false;  // "stopped" is emitted once per line
	std::string       g_prefix = "DBVO/";

	std::atomic<bool> g_armed{ false };
	std::atomic<bool> g_installed{ false };

	bool PathMatches(const char* a_path)
	{
		if (!a_path) {
			return false;
		}
		std::scoped_lock l{ g_mtx };
		if (g_prefix.empty()) {
			return true;
		}
		// Case-insensitive prefix compare: the engine sees whatever case the caller passed, and
		// DBVO 1.x (ConsoleUtil, "DBVO/…") and 2.x (native, "Sound/DBVO/…" resolved down to
		// "DBVO/…") are not consistent about it.
		const std::size_t n = g_prefix.size();
		for (std::size_t i = 0; i < n; ++i) {
			const char c = a_path[i];
			if (c == '\0') {
				return false;
			}
			if (std::tolower(static_cast<unsigned char>(c)) !=
				std::tolower(static_cast<unsigned char>(g_prefix[i]))) {
				return false;
			}
		}
		return true;
	}

	// Entry detour on the NON-virtual Actor::SpeakSoundFunction. Signature (a4–a14 are
	// emotion/2D/lip/queue flags) mirrors DBVODialogueTweaks' hook, which took its widths from
	// the TiltedEvolution id-37542 reference. The original is called FIRST so the engine has
	// built and started the handle before we look at it; we then only READ.
	struct SpeakSoundHook
	{
		static bool thunk(RE::Actor* a_this, const char* a_path, RE::BSSoundHandle* a_handle,
			std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
			std::uint64_t a7, std::uint64_t a8, std::uint64_t a9,
			bool a10, std::uint64_t a11, bool a12, bool a13, bool a14)
		{
			const bool r = original(a_this, a_path, a_handle, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14);
			if (g_armed.load(std::memory_order_relaxed) && a_this && a_this->IsPlayerRef() &&
				a_handle && a_handle->IsValid() && PathMatches(a_path)) {
				const long long now = trace::NowMs();
				bool            playing = false;
				{
					std::scoped_lock l{ g_mtx };
					g_line = *a_handle;  // by value: the engine owns the real one
					g_startMs = now;
					g_sawPlaying = false;
					g_reported = false;
					playing = g_line.IsPlaying();
				}
				trace::Write({ { "src", "speak" }, { "event", "start" }, { "t", now },
					{ "path", a_path }, { "playing", playing } });
			}
			return r;
		}

		static inline decltype(&thunk) original = nullptr;
	};
}

bool engine::InstallSpeakWatchHook(std::string& a_err)
{
	if (g_installed.load()) {
		return true;
	}
	REL::Relocation<std::uintptr_t> target{ REL::RelocationID(36541, 37542) };
	if (auto s = MH_Initialize(); s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
		a_err = "MH_Initialize failed (" + std::to_string(static_cast<int>(s)) + ")";
		return false;
	}
	if (auto s = MH_CreateHook(reinterpret_cast<LPVOID>(target.address()),
			reinterpret_cast<LPVOID>(&SpeakSoundHook::thunk),
			reinterpret_cast<LPVOID*>(&SpeakSoundHook::original));
		s != MH_OK) {
		// MH_ERROR_ALREADY_CREATED is the expected outcome in a profile that also has
		// DBVODialogueTweaks: report it plainly so `speak-watch` can refuse with a reason
		// instead of silently sampling a handle it never receives.
		a_err = (s == MH_ERROR_ALREADY_CREATED)
			? "SpeakSoundFunction is already hooked by another plugin (DBVODialogueTweaks hooks it too; MinHook allows one hook per target)"
			: "MH_CreateHook failed (" + std::to_string(static_cast<int>(s)) + ")";
		return false;
	}
	if (auto s = MH_EnableHook(reinterpret_cast<LPVOID>(target.address())); s != MH_OK) {
		a_err = "MH_EnableHook failed (" + std::to_string(static_cast<int>(s)) + ")";
		return false;
	}
	g_installed.store(true);
	return true;
}

bool engine::ArmSpeakWatch(const std::string& a_prefix, bool a_on, std::string& a_err)
{
	if (a_on && !g_installed.load()) {
		a_err = "speak-sound hook not installed (see the SkytestProbe log for why)";
		return false;
	}
	{
		std::scoped_lock l{ g_mtx };
		if (!a_prefix.empty()) {
			g_prefix = a_prefix;
		}
		g_line = RE::BSSoundHandle{};
		g_sawPlaying = false;
		g_reported = true;  // nothing retained yet -> nothing to report
	}
	g_armed.store(a_on);
	return true;
}

void engine::SampleSpeakWatch()
{
	long long   startMs = 0;
	bool        emitPlaying = false;
	bool        emitStopped = false;
	const char* stopReason = "";
	{
		std::scoped_lock l{ g_mtx };
		if (g_reported) {
			return;
		}
		startMs = g_startMs;
		// The engine can RELEASE the handle when the line ends, which flips IsValid() false —
		// so "no longer valid" is a stop, not a reason to skip the sample. Returning early on
		// !IsValid() (the obvious reading) silently loses every natural end-of-line: the first
		// run of this probe recorded `playing` and then nothing at all.
		const bool valid = g_line.IsValid();
		if (!valid && !g_sawPlaying) {
			return;  // nothing retained yet
		}
		const bool playing = valid && g_line.IsPlaying();
		if (playing) {
			if (!g_sawPlaying) {
				g_sawPlaying = true;
				emitPlaying = true;
			}
		} else if (g_sawPlaying) {
			// Only call it stopped after we have actually seen it playing — a sample taken in the
			// gap between the handle being built and the mixer starting it would otherwise report
			// a zero-length line.
			g_reported = true;
			emitStopped = true;
			stopReason = valid ? "not-playing" : "handle-released";
		}
	}
	const long long now = trace::NowMs();
	if (emitPlaying) {
		trace::Write({ { "src", "speak" }, { "event", "playing" }, { "t", now },
			{ "elapsedMs", now - startMs } });
	}
	if (emitStopped) {
		trace::Write({ { "src", "speak" }, { "event", "stopped" }, { "t", now },
			{ "elapsedMs", now - startMs }, { "why", stopReason } });
	}
}

bool engine::SpeakWatchArmed()
{
	return g_armed.load(std::memory_order_relaxed);
}
