# SkytestProbe — design

**Status:** designed 2026-06-11, not yet built. No separate implementation plan — exploration
happens during implementation; this doc is the contract.

## Goal

Kill the probe-recompile-restart debugging loop. Today, debugging a mod goes: bug occurs → CC
adds a trace/log to the mod source → rebuild DLL → Mase restarts the game and reproduces → CC
reads the log → repeat. Every probe iteration costs a rebuild and a game restart.

SkytestProbe is an SKSE plugin that ships a **pre-compiled, generic probe toolkit** whose probes
are **armed/disarmed/filtered at runtime through files**. The per-bug iteration becomes: CC
writes a probe spec to a command file → the running game picks it up → Mase reproduces the bug →
the plugin writes a structured trace → CC reads it. **Restarts only happen when an actual fix
changes the mod-under-test's DLL.** When a bug needs a probe the toolkit lacks, we fall back to
one recompile — and add that probe to SkytestProbe permanently, so the toolkit compounds.

Primary debugging target: our own SKSE C++ mods (GhostAllies-class bugs: events, collision,
actor state). The plugin must also be safe in the **full ~40-mod profile** (`skytest play`),
because some mods (e.g. DBVODialogueTweaks) only manifest there.

## Constraints

- **DLL-only, Address-Library-only, no `.esp`** — same constraint po3 StartOnSave satisfies, so
  skytest can inject it into any profile without touching the load order.
- **Passive until commanded** — zero gameplay effect and near-zero cost when no probe is armed.
- **Never crash the game on bad input** — malformed commands, unknown FormIDs, missing files all
  degrade to error lines in the trace.
- Game: Skyrim SE **1.6.1170 (AE)**. Toolchain: CommonLibSSE-NG cross-compiled on Linux with
  clang-cl (see `tools/skse/`, `docs/skse-toolchain.md`; `mods/GhostAllies/` is the reference
  implementation for CMake setup, logging bootstrap, vtable hooks, and char-controller access).

## Architecture

New mod at `mods/SkytestProbe/` (CMakeLists + `build.sh` cloned from GhostAllies; FetchContent
adds a JSON library — nlohmann/json suggested, implementer's choice).

Four pieces inside the DLL:

1. **Command reader** — a background thread polls `commands.jsonl` (~250 ms). New lines are
   parsed off-thread; execution closures are handed to the game's main thread via
   `SKSE::GetTaskInterface()->AddTask`. **No engine access from the poll thread.**
2. **Probe registry** — all `ScriptEventSourceHolder` sinks (hit, equip, magic-effect-apply,
   combat, activate, container-changed, death) are registered once at `kDataLoaded` but inert:
   each guarded by an atomic enabled flag + optional FormID filter set, no-op fast path when
   disabled. Animation-graph sinks are the genuinely dynamic part: attached per-actor on demand
   (`AddAnimationGraphEventSink`), detached on disarm.
3. **Trace writer** — single mutex-guarded appender to `trace.jsonl`, one JSON object per line,
   flushed per line, every line carrying a timestamp and a `src` tag (which probe produced it).
4. **Marker hotkey** — input-device sink; on the configured key (default **F11**, configurable
   via `SkytestProbe.ini` next to the DLL) writes a marker line plus an auto-dump of player +
   crosshair target. `RE::DebugNotification` gives Mase on-screen feedback when probes arm/
   disarm and markers drop (ini-toggleable).

**Ref resolution is lazy.** Keywords (`player`, `crosshair`, `teammates`) and hex FormIDs
resolve at use time (per event / per dump), not at command time — so a probe armed at the main
menu simply starts producing once a save is loaded; an unresolvable ref produces an error line,
not a crash.

## File protocol

Files live under the SKSE log directory, which is **stable across Data profile swaps**:

- Windows view: `Documents/My Games/Skyrim Special Edition/SKSE/skytest/`
  (resolve via `SKSE::log::log_directory()` + `skytest/`; create if missing)
- Linux view: `~/.steam/steam/steamapps/compatdata/489830/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/SKSE/skytest/`

| File | Direction | Semantics |
|------|-----------|-----------|
| `commands.jsonl` | CC → game | One JSON command per line, each with an `id`. At startup the plugin executes the **whole file from the top** (it's the declarative probe script for the session — CC can pre-stage it before launch), then polls for appended lines (live commands mid-game). CC rewrites/truncates the file between sessions. |
| `trace.jsonl` | game → CC | Append-only structured output: a session-header line at startup (plugin + game version), then command acks, probe events, dumps, markers. |
| `trace.prev.jsonl` | — | At startup the plugin rotates the previous `trace.jsonl` here (one-deep history). |

Every command gets an ack line: `{"ack":"<id>","ok":true}` or `{"ack":"<id>","ok":false,"err":"…"}`.

## v1 command catalog

| Command (JSON `cmd`) | Args | Does |
|---|---|---|
| `trace` | `on`, `events:[hit,equip,magic-apply,combat,activate,container,death]`, optional `refs:[…]` | Arm/disarm engine event sinks. With `refs`, log only events whose source **or** target matches; without, log all of that type. |
| `anim-trace` | `on`, `ref` | Animation graph events for one actor (the AutoFireBow `BowDrawn` class of bug). |
| `dump` | `ref` | Snapshot one actor → trace: FormID, name, base, position/cell, actor values (health/magicka/stamina + any requested), active effects, equipped gear, teammate flag, 3D-loaded, and **char-controller collision group** (reuse GhostAllies's access code — the GhostAllies class of bug). |
| `watch` | `on`, `ref`, `av` | Sample an actor value at poll cadence (~4 Hz), log only on change. Per-frame sampling via a `Main::Update` hook is a deliberate non-goal for v1 — explore only if 4 Hz proves too coarse. |
| `exec` | `line` | Run a console command via the engine's compile-and-run (precedent: ConsoleUtilSSE-NG). Fire-and-forget + ack; **console output capture is out of scope for v1**. Also mechanically covers fixture setup (spawn teammate, `setstage`, …). |
| `marker` | optional `note` | Drop a marker line (CC-side counterpart of the F11 hotkey). |
| `status` | — | Write a trace line listing currently-armed probes. |

`ref` accepts `player`, `crosshair`, `teammates`, or a hex runtime FormID string (`"0x14"`).

## skytest integration (separate repo, small)

The built DLL is staged at `~/Downloads/skyrim-mods/1-skytest/base-skse/SkytestProbe.dll`
(+ ini template). `1-skytest/skytest` injects it into the **test profile unconditionally**,
mirroring the existing StartOnSave symlink injection (but with no save-existence condition).
Full-profile use = normal manual install into the full profile, optional, for DBVO-style cases.

## Failure model

- Malformed JSON / unknown command → error line in trace, plugin keeps running.
- Unknown/unresolvable FormID → `ok:false` ack or per-use error line.
- All engine access on the main thread (task queue); poll thread touches only files.
- Missing `skytest/` dir → created at startup; absent `commands.jsonl` → idle, near-zero cost.
- Defensive null checks throughout sinks (CommonLib style); a failing probe disarms itself and
  logs why rather than crashing.

## Risks & unknowns (explore during implementation, earliest first)

1. **Linux↔wine file visibility** — appends written by Linux must be seen by the plugin's reads
   (and vice versa) promptly. Reopen-per-poll should defeat caching. **Verify with a minimal
   prototype before building anything else** (poll thread + ack roundtrip only).
2. **`Script::CompileAndRun` on 1.6.1170** via CommonLibSSE-NG — known technique
   (ConsoleUtilSSE-NG), confirm the NG header path and a working selected-ref-null call.
3. **Animation-graph sink lifecycle** — attach/detach across save loads and actor 3D unloads.
4. **Input sink + menus** — F11 should work (or be safely ignored) when menus are open.
5. **Crosshair resolution** — `RE::CrosshairPickData` vs SKSE crosshair-ref event; pick one.
6. **Trace volume** — unfiltered `container` events can spam; acceptable (CC's problem), but
   the writer must not stall the main thread (line-buffered, flush per line, no fsync).

## Verification (definition of done)

1. DLL builds via the toolchain and loads in the test profile (SKSE log line, game reaches menu).
2. **Roundtrip:** while the game runs, `echo '{"id":"t1","cmd":"marker"}' >> commands.jsonl`
   from a Linux shell → ack + marker appear in `trace.jsonl` within ~1 s.
3. `exec` `player.additem f 100` → gold visibly appears in-game.
4. **Acceptance scenario (the GhostAllies re-run):** in a save with a follower, arm `trace on`
   for hit events + `dump` the follower; shoot the follower; the trace shows the hit event
   (attacker/target FormIDs) and the dump shows actor values + collision group — the data the
   original GhostAllies debugging needed recompile loops to see.
5. F11 in-game → marker + player/crosshair auto-dumps in trace, on-screen notification shown.
6. Full-profile smoke test: plugin runs in the 40-mod profile without crash or visible cost.

## Out of scope for v1 (deferred, see docs/ideas.md)

Arbitrary-address hook probes (true dynamic injection — crash-prone); socket/RPC command
bridge; console output capture for `exec`; Papyrus script-variable peeking; hot-reload of the
mod-under-test; main-menu skip; editor-ID ref addressing; a per-mod fixture-autoexec convention
(`exec` covers the mechanics; the convention can come later).

## Precedent (reference implementations)

- `mods/GhostAllies/` — CMake/toolchain template, logging bootstrap, vtable hooks,
  char-controller collision-group access.
- **po3 StartOnSave** (`1-skytest/base-skse/`) — the DLL-only + Address-Library-only pattern.
- **ConsoleUtilSSE-NG** — console `CompileAndRun` from an SKSE plugin.
- **CrashLogger** — already in every test profile; its logs complement the trace.
