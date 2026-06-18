#include "commands.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include <SKSE/SKSE.h>
#include <nlohmann/json.hpp>

#include "config.h"
#include "engine.h"
#include "probes.h"
#include "trace.h"

namespace
{
	using json = nlohmann::json;

	// ---- defensive field extraction (never throws) ------------------------------
	std::string JStr(const json& j, const char* k, std::string def = {})
	{
		auto it = j.find(k);
		return (it != j.end() && it->is_string()) ? it->get<std::string>() : def;
	}
	bool JBool(const json& j, const char* k, bool def)
	{
		auto it = j.find(k);
		return (it != j.end() && it->is_boolean()) ? it->get<bool>() : def;
	}
	bool JHasNum(const json& j, const char* k)
	{
		auto it = j.find(k);
		return it != j.end() && it->is_number();
	}
	double JNum(const json& j, const char* k, double def = 0.0)
	{
		auto it = j.find(k);
		return (it != j.end() && it->is_number()) ? it->get<double>() : def;
	}
	std::vector<std::string> JStrArr(const json& j, const char* k)
	{
		std::vector<std::string> v;
		auto                     it = j.find(k);
		if (it != j.end() && it->is_array()) {
			for (const auto& e : *it) {
				if (e.is_string()) {
					v.push_back(e.get<std::string>());
				}
			}
		}
		return v;
	}

	// Marshal engine work onto the main thread. The closure runs a later frame; it
	// captures only owned data (strings/ids), never raw engine pointers.
	void EnqueueMain(std::function<void()> a_fn)
	{
		auto* task = SKSE::GetTaskInterface();
		if (!task) {
			return;
		}
		task->AddTask(std::move(a_fn));
	}

	std::string JoinUnknown(const std::vector<std::string>& a_v)
	{
		std::string s;
		for (size_t i = 0; i < a_v.size(); ++i) {
			s += (i ? "," : "") + a_v[i];
		}
		return s;
	}

	// ---- dispatch one parsed command --------------------------------------------
	void Dispatch(const json& cmd)
	{
		const std::string id = JStr(cmd, "id", "?");
		const std::string c  = JStr(cmd, "cmd");

		if (c == "marker") {
			trace::Write(json{ { "src", "marker" }, { "note", JStr(cmd, "note") } });
			trace::Ack(id, true);
			return;
		}

		if (c == "trace") {
			auto events = JStrArr(cmd, "events");
			const bool on = JBool(cmd, "on", true);
			auto refs = JStrArr(cmd, "refs");
			if (events.empty()) {
				trace::Ack(id, false, "trace: empty events list");
				return;
			}
			EnqueueMain([id, events, on, refs]() {
				auto unknown = probes::ArmTrace(events, on, refs);  // resolves refs on main thread
				if (unknown.empty()) {
					trace::Ack(id, true);
				} else {
					trace::Ack(id, false, "unknown events (nothing armed): " + JoinUnknown(unknown));
				}
			});
			return;
		}

		if (c == "anim-trace") {
			const bool on = JBool(cmd, "on", true);
			const std::string ref = JStr(cmd, "ref");
			if (ref.empty()) {
				trace::Ack(id, false, "anim-trace: missing ref");
				return;
			}
			EnqueueMain([id, on, ref]() {
				if (!on && ref == "all") {  // disarm-all escape hatch
					probes::DisarmAllAnim();
					trace::Ack(id, true);
					return;
				}
				if (ref == "teammates") {
					auto set = engine::ResolveSet(ref);
					if (set.empty()) {
						trace::Ack(id, false, "anim-trace: no teammates resolved");
						return;
					}
					for (auto* a : set) {
						probes::ArmAnim(a, on);
					}
					trace::Ack(id, true);
					return;
				}
				auto* r = engine::ResolveOne(ref);
				auto* actor = r ? r->As<RE::Actor>() : nullptr;
				if (!actor) {
					trace::Ack(id, false, "anim-trace: unresolvable actor " + ref);
					return;
				}
				const bool attached = probes::ArmAnim(actor, on);
				// attach can defer (3D not loaded yet); the tick will re-attach.
				trace::Ack(id, true);
				if (on && !attached) {
					trace::Write(json{ { "src", "anim" }, { "ref", ref },
						{ "note", "sink queued; actor 3D not loaded yet (tick will re-attach)" } });
				}
			});
			return;
		}

		if (c == "dump") {
			const std::string ref = JStr(cmd, "ref");
			auto avs = JStrArr(cmd, "avs");
			if (ref.empty()) {
				trace::Ack(id, false, "dump: missing ref");
				return;
			}
			EnqueueMain([id, ref, avs]() {
				if (ref == "teammates") {
					auto set = engine::ResolveSet(ref);
					if (set.empty()) {
						trace::Ack(id, false, "dump: no teammates");
						return;
					}
					for (auto* a : set) {
						engine::DumpActor(a, avs, "dump");
					}
					trace::Ack(id, true);
					return;
				}
				auto* r = engine::ResolveOne(ref);
				auto* actor = r ? r->As<RE::Actor>() : nullptr;
				if (!actor) {
					trace::Ack(id, false, "dump: unresolvable actor " + ref);
					return;
				}
				engine::DumpActor(actor, avs, "dump");
				trace::Ack(id, true);
			});
			return;
		}

		if (c == "watch") {
			const bool on = JBool(cmd, "on", true);
			const std::string ref = JStr(cmd, "ref");
			const std::string av  = JStr(cmd, "av");
			if (ref.empty() || av.empty()) {
				trace::Ack(id, false, "watch: missing ref or av");
				return;
			}
			if (ref == "teammates") {  // watch samples ONE actor value; the set keyword has no per-member tracking
				trace::Ack(id, false, "watch: 'teammates' set not supported; watch a single ref");
				return;
			}
			EnqueueMain([id, on, ref, av]() {
				probes::ArmWatch(ref + ":" + av, ref, av, on);  // key on ref+av so on:false disarms
				trace::Ack(id, true);
			});
			return;
		}

		if (c == "facegen-watch") {
			const bool        on  = JBool(cmd, "on", true);
			const std::string ref = JStr(cmd, "ref", "speaker");
			if (ref.empty()) {
				trace::Ack(id, false, "facegen-watch: missing ref");
				return;
			}
			EnqueueMain([id, on, ref]() {
				probes::ArmFaceWatch(ref, on);
				trace::Ack(id, true);
			});
			return;
		}

		if (c == "facegen-close") {
			const std::string ref          = JStr(cmd, "ref", "speaker");
			const float       timer        = static_cast<float>(JNum(cmd, "timer", 0.0));
			const bool        lock         = JBool(cmd, "lock", true);
			const bool        speakingDone = JBool(cmd, "speakingDone", true);
			EnqueueMain([id, ref, timer, lock, speakingDone]() {
				auto* r     = engine::ResolveOne(ref);
				auto* actor = r ? r->As<RE::Actor>() : nullptr;
				if (!actor) {
					trace::Ack(id, false, "facegen-close: unresolvable actor " + ref);
					return;
				}
				std::string err;
				if (engine::CloseFaceGen(actor, timer, lock, speakingDone, err)) {
					trace::Write(json{ { "src", "facegen-close" },
						{ "ref", engine::HexID(actor->GetFormID()) },
						{ "timer", timer }, { "lock", lock }, { "speakingDone", speakingDone } });
					trace::Ack(id, true);
				} else {
					trace::Ack(id, false, "facegen-close: " + err);
				}
			});
			return;
		}

		if (c == "facegen-ramp") {
			const bool on = JBool(cmd, "on", true);
			if (!on) {  // facegen-ramp on:false cancels the active ramp
				EnqueueMain([id]() {
					engine::CancelFaceGenRamp();
					trace::Ack(id, true);
				});
				return;
			}
			engine::FaceGenRampParams p;
			p.ref          = JStr(cmd, "ref", "speaker");
			p.ms           = static_cast<float>(JNum(cmd, "ms", p.ms));
			p.holdMs       = static_cast<float>(JNum(cmd, "holdMs", p.holdMs));
			p.threshold    = static_cast<float>(JNum(cmd, "threshold", p.threshold));
			p.waitMs       = static_cast<float>(JNum(cmd, "waitMs", p.waitMs));
			p.speakingDone = JBool(cmd, "speakingDone", p.speakingDone);
			p.cut          = JBool(cmd, "cut", p.cut);
			p.live         = JBool(cmd, "live", p.live);
			p.reassert     = JBool(cmd, "reassert", p.reassert);
			EnqueueMain([id, p]() {
				engine::StartFaceGenRamp(p);
				trace::Ack(id, true);  // armed; the ramp self-triggers + logs its own series
			});
			return;
		}

		if (c == "exec") {
			const std::string line = JStr(cmd, "line");
			if (line.empty()) {
				trace::Ack(id, false, "exec: missing line");
				return;
			}
			EnqueueMain([id, line]() {
				switch (engine::RunConsoleCommand(line)) {
				case engine::ExecResult::kOk:
					trace::Ack(id, true);
					break;
				case engine::ExecResult::kNotInWorld:
					trace::Ack(id, false, "exec: not in a loaded world (load a save first)");
					break;
				case engine::ExecResult::kFaulted:
					trace::Ack(id, false, "exec: CompileAndRun mis-bound on this game version (stale CommonLib id, see SkytestProbe.log) — stage via direct-call probe commands instead");
					break;
				case engine::ExecResult::kEmpty:
					trace::Ack(id, false, "exec: empty command line");
					break;
				}
			});
			return;
		}

		if (c == "give-spell") {
			const std::string ref  = JStr(cmd, "ref", "player");
			const std::string sp   = JStr(cmd, "spell");
			const std::string hand = JStr(cmd, "hand", "right");
			if (sp.empty()) {
				trace::Ack(id, false, "give-spell: missing spell formID (e.g. \"0x0001C789\")");
				return;
			}
			RE::FormID fid = 0;
			try {
				fid = static_cast<RE::FormID>(std::stoul(sp, nullptr, 16));
			} catch (const std::exception&) {
				trace::Ack(id, false, "give-spell: bad spell formID: " + sp);
				return;
			}
			engine::Hand h = engine::Hand::kRight;
			if (hand == "left") {
				h = engine::Hand::kLeft;
			} else if (hand == "both") {
				h = engine::Hand::kBoth;
			} else if (hand != "right") {
				trace::Ack(id, false, "give-spell: hand must be right|left|both");
				return;
			}
			EnqueueMain([id, ref, fid, h]() {
				auto* r     = engine::ResolveOne(ref);
				auto* actor = r ? r->As<RE::Actor>() : nullptr;
				if (!actor) {
					trace::Ack(id, false, "give-spell: unresolvable actor " + ref);
					return;
				}
				std::string err;
				if (engine::GiveSpell(actor, fid, h, err)) {
					trace::Ack(id, true);
				} else {
					trace::Ack(id, false, "give-spell: " + err);
				}
			});
			return;
		}

		if (c == "set-av") {
			const std::string ref = JStr(cmd, "ref", "player");
			const std::string av  = JStr(cmd, "av");
			if (av.empty()) {
				trace::Ack(id, false, "set-av: missing av (e.g. \"magicka\")");
				return;
			}
			if (!JHasNum(cmd, "value")) {
				trace::Ack(id, false, "set-av: missing numeric value");
				return;
			}
			const float val = static_cast<float>(JNum(cmd, "value"));
			EnqueueMain([id, ref, av, val]() {
				auto* r     = engine::ResolveOne(ref);
				auto* actor = r ? r->As<RE::Actor>() : nullptr;
				if (!actor) {
					trace::Ack(id, false, "set-av: unresolvable actor " + ref);
					return;
				}
				RE::ActorValue avEnum = engine::ResolveActorValue(av);
				if (avEnum == RE::ActorValue::kNone) {
					trace::Ack(id, false, "set-av: unknown actor value " + av);
					return;
				}
				if (engine::SetAV(actor, avEnum, val)) {
					trace::Ack(id, true);
				} else {
					trace::Ack(id, false, "set-av: failed (no AV owner)");
				}
			});
			return;
		}

		if (c == "status") {
			EnqueueMain([id]() {
				probes::WriteStatus();
				trace::Ack(id, true);
			});
			return;
		}

		if (c == "is-menu-open") {
			const std::string menu = JStr(cmd, "menu");
			if (menu.empty()) {
				trace::Ack(id, false, "is-menu-open: missing menu");
				return;
			}
			EnqueueMain([id, menu]() {
				const bool open = engine::IsMenuOpen(menu);
				trace::Write(json{ { "src", "menu" }, { "menu", menu }, { "open", open } });
				trace::Ack(id, true);
			});
			return;
		}

		if (c == "mcm-list") {
			EnqueueMain([id]() {
				const int n = engine::WriteMcmList();
				trace::Ack(id, n >= 0, n < 0 ? "mcm-list: Papyrus VM unavailable" : "");
			});
			return;
		}

		if (c == "mcm-get") {
			const std::string script = JStr(cmd, "script");
			auto              props  = JStrArr(cmd, "props");
			if (script.empty()) {
				trace::Ack(id, false, "mcm-get: missing script");
				return;
			}
			if (props.empty()) {
				trace::Ack(id, false, "mcm-get: missing props");
				return;
			}
			EnqueueMain([id, script, props]() {
				if (engine::WriteMcmGet(script, props)) {
					trace::Ack(id, true);
				} else {
					trace::Ack(id, false, "mcm-get: no quest with script " + script);
				}
			});
			return;
		}

		trace::Ack(id, false, c.empty() ? "missing cmd" : ("unknown cmd: " + c));
	}

	void ExecuteLine(std::string_view a_line)
	{
		// strip a trailing CR (CC may write \r\n) and skip blank lines.
		if (!a_line.empty() && a_line.back() == '\r') {
			a_line.remove_suffix(1);
		}
		if (a_line.empty()) {
			return;
		}
		json obj = json::parse(std::string(a_line), nullptr, /*allow_exceptions=*/false);
		if (obj.is_discarded() || !obj.is_object()) {
			trace::Error("bad command line: " + std::string(a_line));
			return;
		}
		Dispatch(obj);
	}

	void PollLoop()
	{
		// Path globals are immutable after trace::Init() (which runs before this thread
		// starts), so snapshot once instead of an unguarded cross-thread read per tick.
		const std::filesystem::path path = trace::CommandsPath();
		std::streamoff               lastOffset = 0;
		for (;;) {
			std::this_thread::sleep_for(std::chrono::milliseconds(config::pollIntervalMs));

			if (!path.empty()) {
				std::ifstream f(path, std::ios::binary);
				if (f.is_open()) {
					f.seekg(0, std::ios::end);
					const std::streamoff size = f.tellg();
					if (size < 0) {
						// tellg() failed (transient I/O) — skip this tick rather than
						// reset the offset, which would re-execute the whole file.
					} else {
						if (size < lastOffset) {
							lastOffset = 0;  // truncated/rewritten -> re-read from top
						}
						f.seekg(lastOffset);
						std::string chunk((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

						// Process only COMPLETE lines (ending in '\n'); leave a partial
						// trailing line for the next poll.
						std::size_t start = 0;
						while (true) {
							std::size_t nl = chunk.find('\n', start);
							if (nl == std::string::npos) {
								break;
							}
							ExecuteLine(std::string_view(chunk).substr(start, nl - start));
							start = nl + 1;
						}
						lastOffset += static_cast<std::streamoff>(start);
					}
				}
			}

			// Main-thread tick (~4 Hz): watch sampling + anim re-attach + filter
			// re-resolve, only when there's armed work.
			if (probes::HasMainTickWork()) {
				EnqueueMain([]() { probes::MainTick(); });
			}
		}
	}

	std::atomic<bool> g_started{ false };
}

void commands::Start()
{
	if (g_started.exchange(true)) {
		return;  // already started
	}
	std::thread(PollLoop).detach();
	SKSE::log::info("SkytestProbe: command poll thread started ({} ms)", config::pollIntervalMs);
}
