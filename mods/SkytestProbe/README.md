# SkytestProbe

> **Status: working, v0.2.0.** A runtime-armed SKSE C++ **debug toolkit** — not a gameplay
> mod. The `skytest` test harness auto-injects it; this README is its usage / command reference.

SkytestProbe kills the probe-recompile-restart loop. Without it, debugging an SKSE mod means
adding a trace to the source, rebuilding the DLL, restarting the game, reproducing the bug,
reading the log — and repeating for every probe. SkytestProbe ships a **pre-compiled, generic
probe toolkit** that's armed, filtered, and queried entirely at runtime over a file protocol:
CC writes JSON commands to `commands.jsonl`, the running game writes structured traces to
`trace.jsonl`. The game restarts only when an actual fix changes the mod-under-test's DLL. When
a bug needs a probe the toolkit lacks, that probe is added here permanently — the toolkit
compounds.

## What it does

- **File protocol, no recompile.** Commands in: `commands.jsonl` (one JSON object per line,
  each with an `id`). Traces out: `trace.jsonl` (append-only, one JSON object per line, each
  carrying an epoch-ms `t` and a `src` tag). Both live under
  `<SKSE log dir>/skytest/` — a path that's **stable across `Data/` profile swaps**. At startup
  the plugin rotates the old trace to `trace.prev.jsonl`, executes `commands.jsonl` from the top
  (the declarative session script CC can pre-stage before launch), then polls for appended
  lines (~4 Hz). Every command gets an ack line: `{"ack":"<id>","ok":true}` or
  `{…,"ok":false,"err":"…"}`.
- **Passive until armed.** All engine event sinks register once at `kDataLoaded` but stay inert
  behind an atomic gate — near-zero idle cost when nothing is armed.
- **Never crashes on bad input.** Malformed JSON, unknown commands, unresolvable FormIDs, and
  missing files all degrade to error / `ok:false` lines, never a crash. All engine access is
  marshalled to the main thread; the poll thread only touches files. Ref resolution is lazy
  (resolved per-use, so a probe armed at the main menu starts producing once a save loads).
- **F11 marker hotkey.** Pressing F11 (configurable) writes a marker line plus an auto-dump of
  the player and the crosshair target, with an on-screen notification.
- **`skytest` auto-injects it.** `skytest` reads the built DLL straight from this mod's `build/`
  and injects it (plus its ini, verbatim) into every test profile unconditionally — no load-order
  slot, no separate staging step.

## Command reference

Every command is a JSON object on its own line in `commands.jsonl`, e.g.
`{"id":"t1","cmd":"dump","ref":"crosshair","avs":["onehanded"]}`. `id` is echoed in the ack;
`cmd` selects the command. Output goes to `trace.jsonl` tagged with the listed `src`.

A `ref` accepts `player`, `crosshair`, `teammates`, or a hex runtime FormID string (`"0x14"`).

| `cmd`          | Args                                                                                                       | What it does (trace `src`)                                                                                                                                                                                                                                                                                                                                                        |
| -------------- | ---------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `trace`        | `events:[…]`, `on` (default `true`), optional `refs:[…]`                                                   | Arm/disarm engine event sinks. `events` ⊆ `hit`, `equip`, `magic-apply`, `combat`, `activate`, `container`, `death`. With `refs`, log only events whose source **or** target matches (re-resolved each tick, so `teammates` tracks the live party); without, log all of that type. Re-arming an event type replaces its filter. Unknown event names arm nothing (all-or-nothing). |
| `anim-trace`   | `ref`, `on` (default `true`)                                                                               | Animation-graph events for one actor (the `BowDrawn`-class bug). `ref:"teammates"` attaches to each member resolved at command time. `on:false ref:"all"` detaches everything. Survives save-loads / 3D reloads (re-attached each tick). (`src:"anim"`)                                                                                                                           |
| `dump`         | `ref`, optional `avs:[…]`                                                                                  | Snapshot one actor → FormID, name, base, position/cell, actor values (health/magicka/stamina + any in `avs`), active effects, equipped gear, teammate flag, 3D-loaded, and **char-controller collision group** (the GhostAllies-class data). `ref:"teammates"` dumps each member. (`src:"dump"`)                                                                                  |
| `watch`        | `ref`, `av`, `on` (default `true`)                                                                         | Sample one actor value at poll cadence (~4 Hz), log only on change. Single-actor only (`teammates` rejected). `on:false` removes the watch. (`src:"watch"`)                                                                                                                                                                                                                       |
| `status`       | —                                                                                                          | Write world-readiness (top-level `inWorld` for a cheap grep, plus a `world` block: `inWorld`/`is3DLoaded`/`mainMenu`/`loadingMenu`) and the currently-armed probes. `inWorld` is the **exact gate `exec` uses**. Host pollers (`skytest ready`) wait on `inWorld:true` — the ack alone means nothing (it acks at the main menu too).                                              |
| `marker`       | optional `note`                                                                                            | Drop a marker line (the CC-side counterpart of the F11 hotkey). (`src:"marker"`)                                                                                                                                                                                                                                                                                                  |
| `is-menu-open` | `menu`                                                                                                     | Report whether the named UI menu is currently open. (`src:"menu"`, `{menu, open}`)                                                                                                                                                                                                                                                                                                |
| `give-spell`   | `spell` (hex FormID), optional `ref` (default `player`), `hand` (`right`\|`left`\|`both`, default `right`) | Add a spell to the actor's spell list (if missing) and equip it to the hand(s). Direct engine call — see the caveat below.                                                                                                                                                                                                                                                        |
| `set-av`       | `av`, `value` (number), optional `ref` (default `player`)                                                  | Set an actor value's base **and** refill its current to that value (so `magicka 1000` is immediately castable; `0` drains it). Direct engine call — see the caveat below.                                                                                                                                                                                                         |
| `exec`         | `line`                                                                                                     | Run a console command via `CompileAndRun`. **Currently faults on this game version** (see the caveat below) — acks `ok:false`, never crashes. Fire-and-forget; console-output capture is out of scope.                                                                                                                                                                            |
| `mcm-list`     | —                                                                                                          | Enumerate registered SkyUI MCMs (read-only, headless): `{via:"manager"\|"scan"\|"none", count, mods:[{name, script, pages}]}`. Needs the probe loaded where **SkyUI** is (full profile); `count:0` when absent (a successful scan, not an error). Acks `ok:false` only when the Papyrus VM is unavailable (pre-load). (`src:"mcm-list"`)                                          |
| `mcm-get`      | `script`, `props:[…]`                                                                                      | Read named scalar properties (bool/int/float/string) off a config script class, e.g. `AutoFireBowMCM` → `{values:{prop:val}, missing:[…]}`. Absent / non-scalar props go in `missing`. Acks `ok:false` only when no quest binds `script`. The deterministic, value-level MCM check (no menu driving). (`src:"mcm-get"`)                                                           |

### facegen probes (DBVO mouth-snap work)

These were added to debug the DBVO mouth-close / lip-sync seam; they instrument facegen morph
state at per-frame resolution. Documented here because they ship in the DLL — full rationale is
in the DBVO design docs and the mouth-snap handoff.

| `cmd`               | Args                                                                                                                                     | What it does (trace `src`)                                                                                                                                                                                                                                                                                                                                                |
| ------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `facegen-watch`     | optional `ref` (default `speaker`), `on` (default `true`)                                                                                | Sample the actor's facegen morphs every tick while armed (a time series — every sample, not change-gated). (`src` per the morph dump)                                                                                                                                                                                                                                     |
| `facegen-close`     | optional `ref` (default `speaker`), `timer` (default `0` = hard snap), `lock` (default `true`), `speakingDone` (default `true`)          | Parameterized facegen reset — the DBVO NPC-cut close with the snap variables exposed. (`src:"facegen-close"`)                                                                                                                                                                                                                                                             |
| `facegen-ramp`      | optional `ref` (default `speaker`), `ms`, `holdMs`, `threshold`, `waitMs`, `speakingDone`, `cut`, `live`, `reassert`; `on:false` cancels | Owned ramp of the transition-target keyframe toward 0, applied per-frame at the morph-apply seam (the only point that beats the engine's lip pump). Self-triggers on speech, logs a `{src:"ramp", maxBefore, maxAfter, t, elapsed}` series so the hold-vs-bounce verdict is visible per frame.                                                                            |
| `facegen-skip-ease` | optional `kf`, `ms`, `holdMs`, `snapshot`; `on:false` disarms                                                                            | Arm an eased mouth-close reaction to the player's **real** NPC-reply skip (the `CutNpcDBVOReply` mod event), driven by the same per-frame apply hook as `facegen-ramp` but triggered immediately on the skip (no WAIT/threshold phase). (`src:"ramp"` series)                                                                                                             |
| `facegen-observe`   | optional `ref` (default `speaker`), `kf` (default = all mouth channels `unk0C0`/`unk140`/`unk180`); `on:false` disarms                   | **Read-only** per-frame characterization: logs the target keyframe(s)' max **every render frame without modifying them** — sub-100 ms transitions the 4 Hz `facegen-watch` is too coarse for (the 1-frame snap, the residual tongue flick). Pins to the actor resolved at arm (re-arm if the speaker changes). (`src:"face-frame"` with per-frame `time`, `gt`, `paused`) |

**Paused-vs-running guard.** Every facegen line — `facegen-watch`'s `src:"face"`, `dump`, and
`facegen-observe`'s `src:"face-frame"` — carries `paused` (the sim is frozen: a menu/console is up
or `Main::freezeTime`) and `gt` (the per-frame game-time delta — measured `~0.0167` running, `0.0`
frozen). A paused snapshot is byte-identical to live data; reading one as live is exactly what sent
the mouth-snap chase down the multi-session `transitionTarget` dead end. These two tags say which it
is, so a frozen sample is never mistaken for live mouth dynamics.

## The exec / CompileAndRun caveat

`give-spell` and `set-av` exist as **direct-engine-call** commands rather than going through
`exec` because programmatic `exec` (`Script::CompileAndRun`) is **mis-bound on game 1.6.1170**.
This CommonLibSSE-NG build (Sep 2024) predates the 1.6.1170 runtime, so `CompileAndRun`'s bound
AE id (21890) is absent from that versionlib and CommonLib's non-VR lookup silently calls the
**next** id — wrong function — which access-violates. It is **not** a headless limitation; it
would fault in a windowed game on this version too. The call is gated to in-world and wrapped in
SEH (`__try`/`__except`), so the AV becomes an `ok:false` "faulted" ack instead of a crash. `exec`
is therefore retired, not fixed: **stage state with the direct-call commands** (`give-spell`,
`set-av`) instead. Full pin: [`../../skytest/docs/headless-findings.md`](../../skytest/docs/headless-findings.md) #18.

## Configuration

`SkytestProbe.ini` sits next to the DLL in `Data/SKSE/Plugins/` (missing/garbled keys keep their
defaults):

| Key              | Default                          | Meaning                                                                                                                                       |
| ---------------- | -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| `MarkerHotkey`   | `87` (DX scancode, `0x57` = F11) | The marker-hotkey scancode. `0` disables the hotkey entirely.                                                                                 |
| `Notifications`  | `true`                           | Show on-screen `DebugNotification`s when probes arm/disarm and markers drop. Turn off if noisy in the full profile.                           |
| `PollIntervalMs` | `250` (~4 Hz)                    | Command-file poll cadence (also the `watch` / `facegen-watch` sampling cadence). Lower = snappier live commands at slightly higher idle cost. |

## Building from source

Linux, headless: no Creation Kit or SSEEdit.

```bash
./build.sh            # configure + build -> build/SkytestProbe.dll
./build.sh --install  # also copy the DLL + ini into the live game's SKSE/Plugins
                      # (full-profile manual install, for DBVO-style cases)
```

`skytest` reads `SkytestProbe.dll` straight from `build/` and injects it into each test profile,
so there's no separate staging step — `--install` is only for the manual full-profile install
(e.g. the MCM-reveal commands, which need SkyUI present). Cross-compiled Linux → Windows with the
in-repo `tools/skse` toolchain (clang-cl + lld-link + xwin; CommonLibSSE-NG and spdlog fetched
and pinned by CMake; JSON is the vendored single-header nlohmann/json). See
[`../../docs/skse-toolchain.md`](../../docs/skse-toolchain.md).

## Design notes

The authoritative contract — file protocol, the v1 command catalog, the failure model, and the
as-built deltas (the `exec` pin, lazy filter resolution, anim-sink lifetime) — is
[`../../docs/plans/skytest-probe-design.md`](../../docs/plans/skytest-probe-design.md). The
`mcm-list` / `mcm-get` commands have their own design doc:
[`../../docs/plans/skytest-probe-mcm-reveal-design.md`](../../docs/plans/skytest-probe-mcm-reveal-design.md).
The `facegen-*` commands come from the DBVO mouth-snap work — see the `dbvo-*` design docs and
[`../../docs/plans/dbvo-mouth-snap-handoff.md`](../../docs/plans/dbvo-mouth-snap-handoff.md).
Deferred work (a socket/RPC command bridge, console-output capture for `exec`, per-frame `watch`
via a `Main::Update` hook, MCM _driving_) lives in
[`../../docs/ideas.md`](../../docs/ideas.md).
