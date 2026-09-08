# DBVO Dialogue Tweaks v6 — Voice Boost Implementation Plan

**Goal:** Let the player-voice slider go above 100% (to 300%) by multiplying the XAudio2 voice's gain after the engine's own push, shipped as mod 1.1.0 with an in-engine replay that proves the gain landed.

**Architecture:** A vtable hook on `BSXAudio2GameSound::SetVolumeImpl` runs the engine's push, then for the player's current DBVO line (identified by sound ID through the audio manager's map, on the audio thread only) re-applies `readback × boost` to the `IXAudio2SourceVoice`. The existing speak-sound hook publishes the line's sound ID and the boost (atomics) *before* it sends the queued `SetVolume`. skytest gains a host-side `until:log:` gate over SKSE plugin logs so the DLL's readback line is the assertion.

**Tech Stack:** CommonLibSSE-NG v7.1.0 (cross-compiled, `tools/skse`), Papyrus (SkyUI MCM, `tools/compile-papyrus.sh`), bash (skytest replay), ffdec/EspGen unchanged.

**Spec:** `docs/plans/dbvo-v6-voice-boost-design.md` (read it first — it carries the engine evidence and the three review fixes every task below encodes).

---

## File structure

| File | Responsibility |
| --- | --- |
| `skytest/lib/replay.sh` | `until:log:` gate: parse, check against the mark, poll loop branch, lint |
| `skytest/lib/gamescope.sh` | `gs_mark_plugin_logs`: record each `SKSE/*.log` size at ready-time into the probe IO dir |
| `skytest/skytest` | call `gs_mark_plugin_logs` where a launched session becomes ready (replay/test verbs) |
| `skytest/lib/replay.test.sh` | unit tests for the gate (pure, no game) |
| `skytest/README.md` | gate table entry |
| `mods/DBVODialogueTweaks/plugin/src/main.cpp` | boost state, publish order, `SetVolumeImplHook`, version 1.1.0 |
| `mods/DBVODialogueTweaks/plugin/CMakeLists.txt` | project version 1.1.0 |
| `mods/DBVODialogueTweaks/src/papyrus/DBVODialogueTweaksMCM.psc` | slider 0–300, info text |
| `mods/DBVODialogueTweaks/src/papyrus/DBVOTweaks.psc` | native contract comment |
| `mods/DBVODialogueTweaks/package.sh`, `stage-test-profile.sh` | 1.1.0, newest-zip staging |
| `mods/DBVODialogueTweaks/voiceboost.steps`, `voiceboost-control.steps` | the in-engine test and its control |
| `mods/DBVODialogueTweaks/README.md`, `docs/dbvo-page.bbcode`, `docs/ideas.md`, `docs/dbvo-landscape.md`, `CLAUDE.md` | release sweep |

---

### Task 1: skytest `until:log:<plugin>|<substring>` gate `[Mode: Delegated]`

**Files:**
- Modify: `skytest/lib/replay.sh` (gate branch beside the probe path — NOT a `resolve_gate` row)
- Modify: `skytest/lib/gamescope.sh` (`gs_mark_plugin_logs`)
- Modify: `skytest/skytest` — call the mark in `_boot_test_session`, right after the readiness block (`gs_wait_probe` under `SKYTEST_NO_AUTOLOAD`, else `gs_wait_ready`) and before its `return "$rc"`, for rc 0 *and* 1 (a timed-out-but-live session still gets marked). That one function is the boot path of both `test` and `replay`.
- Test: `skytest/lib/replay.test.sh`
- Modify: `skytest/README.md` (gate list, after the `uivar` bullet)

**Contracts:**
- `_log_gate_parse <cond> <name_var> <sub_var>` — `cond` is `log:<plugin>|<substring>` (no `until:` prefix, no leading `!`). Returns 0 and fills the two namerefs; returns 2 with a `replay: gate 'log:<plugin>|<substring>' needs both fields` message on stderr when either side is empty or the `|` is missing. `<plugin>` is the log's basename without `.log`; `<substring>` is the rest of the line verbatim (it may contain `|`, spaces, `:`, `>`), matched with `grep -F`.
- `_log_gate_dir` — `${SKYTEST_SKSE_LOG_DIR:-$MYGAMES/SKSE}` (the env override exists for the unit tests).
- `_log_gate_mark <plugin>` — prints the byte offset recorded for `<plugin>.log` in `$(_skytest_io_dir)/logmarks` (format: one `<plugin>\t<bytes>` line per file), or `0` when the file has no mark.
- `_log_gate_check <plugin> <substring>` — 0 when some line of `<dir>/<plugin>.log` **past the mark** contains `<substring>`; 1 otherwise. A file smaller than its mark was re-truncated → scan from byte 0. A missing log file → 1 (not an error: the plugin may not have written yet).
- `gs_mark_plugin_logs` (gamescope.sh) — for every `$MYGAMES/SKSE/*.log`, write `<plugin>\t<size>` to `$(_skytest_io_dir)/logmarks` (overwrite the file). Called once per launch after the probe answers. `gs_reset_io` today truncates only `commands.jsonl` and `trace.jsonl` — add `rm -f "$dir/logmarks"` to it so a previous launch's marks cannot outlive the launch (the stale-state class of finding #13).
- `replay_wait_gate` — new leading `case` arms ahead of `resolve_gate`: `log:*` polls `_log_gate_check` every 1 s until satisfied (0) or the deadline (1), with the same session-death fast-fail (2) and the same "waiting for gate … / satisfied / timed out" messages as the probe path. `!log:*` is satisfied when `_log_gate_check` returns 1 and **fails fast (1) on the first hit** — a log line is never un-written, so polling a present line to the deadline would only make the control run look hung; the message says `gate !log:… failed: line present`. Neither arm calls `_probe_send`.
- `_lint_gate` — accepts `until:log:…` / `until:!log:…` via `_log_gate_parse`; `resolve_gate` itself keeps rejecting `log:` (it is not a probe gate), so `_lint_gate` must branch before calling it.
- The header comment block over the gate table ("ONE resolve_gate row + ONE probe handler") gets a sentence that host-side gates (`log:`) live in `replay_wait_gate` instead.

**Test Cases** (append to `replay.test.sh`, same `check`/`check_rc` helpers):

```bash
# parse
n='' s=''; _log_gate_parse 'log:DBVODialogueTweaks|voice boost x2.50' n s
check "log parse name" 'DBVODialogueTweaks' "$n"
check "log parse sub"  'voice boost x2.50' "$s"
n='' s=''; _log_gate_parse 'log:Foo|a|b: c' n s
check "log parse sub keeps later pipes" 'a|b: c' "$s"
_log_gate_parse 'log:Foo' n s 2>/dev/null; check_rc "log parse no sub" 2 "$?"
_log_gate_parse 'log:|x' n s 2>/dev/null;  check_rc "log parse no name" 2 "$?"

# check against a mark (SKYTEST_SKSE_LOG_DIR + a stubbed _skytest_io_dir pointing at a temp dir)
#   file "Foo.log" = "loaded\nvoice boost x2.50: …\n"; mark Foo = 0        -> check returns 0
#   same file; mark Foo = size of "loaded\n"                               -> 0 (line is past the mark)
#   same file; mark Foo = full size                                        -> 1 (nothing past the mark)
#   file truncated to "voice boost x2.50\n" (smaller than its mark)        -> 0 (rescanned from 0)
#   no Foo.log at all                                                      -> 1
#   substring not present                                                  -> 1

# lint
check "lint log gate ok"   0 "$(_lint_gate 'until:log:Foo|bar' >/dev/null 2>&1; echo $?)"
check "lint !log gate ok"  0 "$(_lint_gate 'until:!log:Foo|bar' >/dev/null 2>&1; echo $?)"
e="$(replay_parse - <<<'wait until:log:Foo' | replay_lint 2>&1)"; rc=$?
contains "lint bad log gate msg" "bad gate 'until:log:Foo'" "$e"

# parser keeps the whole rest of the line as the gate (substring with spaces + '>')
check "parse log gate whole line" \
  'STEP wait gate=until:log:DBVODialogueTweaks|voice boost x2.50: 1.000 -> 2.500' \
  "$(replay_parse - <<<'wait until:log:DBVODialogueTweaks|voice boost x2.50: 1.000 -> 2.500')"
```

**Constraints:**
- Pure bash + coreutils + `grep -F`, like the rest of `replay.sh`; no jq needed for this gate.
- `replay_wait_gate`'s existing probe path is untouched — the log arms are added ahead of `resolve_gate`.
- The unit test stubs `_skytest_io_dir` (a temp dir) and sets `SKYTEST_SKSE_LOG_DIR`; it never touches `$MYGAMES`.
- README bullet wording must say the window is "written since the session became ready", that a line is matched once and never re-emitted (so pin the substring per stimulus), and that `!log:` is an immediate assertion of absence.

**Verification:**
Run: `bash skytest/lib/replay.test.sh` — exit 0, the new checks listed in its output.
Run: `bash -n skytest/skytest skytest/lib/gamescope.sh skytest/lib/replay.sh`.

**Commit after passing:** `feat(skytest): until:log:<plugin>|<substring> — assert on an SKSE plugin's own log`

---

### Task 2: the DLL — boost state, publish order, `SetVolumeImpl` hook `[Mode: Delegated]`

**Files:**
- Modify: `mods/DBVODialogueTweaks/plugin/src/main.cpp`
- Modify: `mods/DBVODialogueTweaks/plugin/CMakeLists.txt` (`project(DBVODialogueTweaks VERSION 1.1.0 …)`)

**Contracts (all in the anonymous namespace of `main.cpp`):**
- `std::atomic<float> g_dbvoVolume{1.0f}` — unchanged meaning: the factor sent to the handle, **≤ 1.0**.
- `std::atomic<float> g_dbvoBoost{1.0f}` — the multiplier applied on the voice, **≥ 1.0**.
- `std::atomic<std::uint32_t> g_boostSoundID{kNoSound}` (`kNoSound = 0xFFFFFFFF`, the engine's "no sound" id) — the player line the boost applies to.
- `std::atomic<std::uint32_t> g_boostLoggedID{kNoSound}` — log latch; `std::atomic<bool> g_boostThreadWarned{false}` — one-shot.
- `void SetPlayerVoiceVolume(RE::StaticFunctionTag*, float factor)` — `g_dbvoVolume = std::min(factor, 1.0f)`; `g_dbvoBoost = std::max(factor, 1.0f)`. Negative or NaN input → treated as 0 for volume, 1 for boost.
- Speak-sound hook body, in this order after `original(...)` and the existing gate: (1) retain `g_playerLine` under `g_playerLineMtx` (existing); (2) `g_boostSoundID.store(a_handle->soundID)`; (3) `a_handle->SetVolume(g_dbvoVolume.load())`; (4) `g_sawPlaying = false; g_replyArmed = true` (existing). Step 3 moves from first to after the publish — the comment must say why (queued message vs. the id it will be matched against).
- `struct SetVolumeImplHook { static void thunk(RE::BSXAudio2GameSound* a_this); static inline REL::Relocation<decltype(&thunk)> func; }` installed in `InstallHooks()` via `REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_BSXAudio2GameSound[0] }; func = vtbl.write_vfunc(0x18, thunk);` with an info log line on success. Installation order: after the MinHook detour; failure of either is logged and the other still installs.
- `thunk` semantics:
  1. `func(a_this)` always, first.
  2. Return unless `g_dbvoBoost > 1.0f` and `g_boostSoundID != kNoSound`.
  3. `auto* mgr = RE::BSAudioManager::GetSingleton()`; return if null.
  4. If `REX::W32::GetCurrentThreadId() != mgr->ownerThreadID`: log **once** (`g_boostThreadWarned`) `"voice boost skipped: SetVolumeImpl on thread {} but audio owner is {}"` and return.
  5. `auto it = mgr->activeSounds.find(id)`; return unless found and `it->second == a_this`.
  6. If `a_this->sourceVoice == nullptr`: log once per id (`g_boostLoggedID` latch) `"voice boost skipped: sound {} matched but has no XAudio2 voice yet"` and return — the id match comes before the voice check so the one remaining silent failure mode has a name in the log.
  7. `float v = 0.f; a_this->sourceVoice->GetVolume(&v); a_this->sourceVoice->SetVolume(v * boost, 0);`
  8. If `g_boostLoggedID.exchange(id) != id`: `SKSE::log::info("voice boost x{:.2f}: voice {:.3f} -> {:.3f} (sound {})", boost, v, v * boost, id)`.
  - The hook **never** takes `g_playerLineMtx`, never calls into `BSSoundHandle`, never touches `g_playerLine`. Comment block must carry the lock-order reason from the spec.
- `kVersion = REL::Version{ 1, 1, 0 }`.
- Includes: `RE/Skyrim.h` already covers `BSXAudio2GameSound`, `BSAudioManager`, `IXAudio2Voice`; `REX::W32::GetCurrentThreadId` is in `REX/W32/KERNEL32.h` (pulled by `SKSE/SKSE.h`).

**Test Cases:** none host-side (cross-compiled engine code). The in-engine assertion is Task 4; the build is the compile-time check:

```
./build.sh                           # all five artifacts; the DLL step must print the .dll as PE32+
rg -n 'voice boost x|write_vfunc\(0x18' plugin/src/main.cpp     # both present
```

**Constraints:**
- Don't refactor v3–v5 code beyond the reorder in the speak hook; the mod is timing-sensitive (CLAUDE.md). Keep every existing log line.
- `BSTHashMap::find` returns an iterator whose `->second` is the `BSGameSound*` (CommonLib `BSTScatterTable`); compare pointers, not ids.
- `IXAudio2Voice::SetVolume(float, std::uint32_t a_operationSet = XAUDIO2_COMMIT_NOW)` — pass `0` explicitly.

**Verification:**
Run: `cd mods/DBVODialogueTweaks && ./build.sh 2>&1 | tail -5` — ends with the artifact listing; `file plugin/build/DBVODialogueTweaks.dll` says `PE32+ executable (DLL)`.

**Commit after passing:** `feat(dbvo): v6 — boost the player line above 100% on the XAudio2 voice (SetVolumeImpl hook)`

---

### Task 3: MCM slider 0–300 + info text, native contract comment `[Mode: Direct]`

**Files:**
- Modify: `mods/DBVODialogueTweaks/src/papyrus/DBVODialogueTweaksMCM.psc`
- Modify: `mods/DBVODialogueTweaks/src/papyrus/DBVOTweaks.psc`

**Contracts:**
- `OnOptionSliderOpen`: `_volOID` → `SetSliderDialogRange(0, 300)`, default 100, interval 5 (unchanged), start value unchanged.
- New `Event OnOptionHighlight(Int oid)`: `_volOID` → `SetInfoText("Your own DBVO line only. 100 = as the pack was mastered. Above 100 amplifies: a loud pack will clip, a quiet one (some vampire packs) wants 200-300.")`; `_padOID` → `SetInfoText("Pause between the end of your voiced line and the NPC's reply.")`. **ASCII only** in these strings (plain hyphens, no em/en dashes): Skyrim's UI fonts do not reliably carry those glyphs.
- `GetVersion` stays 4 (a persisted value ≤ 100 is still valid; no migration).
- `DBVOTweaks.psc` comment: factor `0.0–3.0`; `≤ 1.0` attenuates the sound handle, `> 1.0` is applied as gain on the XAudio2 voice by the DLL's `SetVolumeImpl` hook; 1.0 = pass-through.

**Test Cases:** compile is the test (`./build.sh` steps 2/5 and 3/5 must succeed against the vendored SkyUI sources); `rg -n 'SetSliderDialogRange\(0, 300\)|OnOptionHighlight|SetInfoText' src/papyrus/DBVODialogueTweaksMCM.psc` shows all three.

**Verification:**
Run: `cd mods/DBVODialogueTweaks && ./build.sh 2>&1 | rg '\[2/5\]|\[3/5\]|error' ` — the two compile lines, no `error`.

**Commit after passing:** `feat(dbvo): MCM volume slider 0–300% with info text`

---

### Task 4: packaging at 1.1.0, the replay test and its control, run both `[Mode: Direct]`

**Files:**
- Modify: `mods/DBVODialogueTweaks/package.sh` (`VERSION="1.1.0"`)
- Modify: `mods/DBVODialogueTweaks/stage-test-profile.sh` (`DIST` = the newest `dist/DBVO Dialogue Tweaks *.zip` by version sort, error if none)
- Create: `mods/DBVODialogueTweaks/voiceboost.steps`
- Create: `mods/DBVODialogueTweaks/voiceboost-control.steps`

**Contracts (`voiceboost.steps`):**
- Header comment: what it proves, the exact run lines, why only the console is driven (spec, Testing), NO_AUTOLOAD note copied from `replyonlineend.steps`.
- Steps: the five boot steps + `wait until:inworld` + `wait 3s` from `replyonlineend.steps`, then:
  `cmd {"cmd":"papyrus-call","class":"DBVOTweaks","function":"SetPlayerVoiceVolume","args":[2.5]}` / `wait 1s` /
  `tap tilde` / `wait until:menu:Console` / `type player.speaksound "dbvo/t1.fuz"` / `tap enter` / `cmd {"cmd":"status"}` (timing witness) / `tap tilde` / `wait until:!menu:Console` /
  **assertion** `wait until:log:DBVODialogueTweaks|voice boost x2.50`.
  (Changed 2026-09-08 from a console `cgf` line: on 1.7.104 neither `cgf` nor `callglobalfunction`
  exists — verified live — so a `papyrus-call` command was added to SkytestProbe, a Task 1b.)
- `voiceboost-control.steps`: identical with `[1.0]` in the papyrus-call args, then `wait 3s` and the assertion `wait until:!log:DBVODialogueTweaks|voice boost` (must PASS — no boost line at 100%).
- Both are `replay_lint`-clean (`skytest replay --dry-run` if available, else the linter via the unit-test harness).

**Test Cases (in-engine, the verification itself):**

```
cd mods/DBVODialogueTweaks && ./build.sh && ./package.sh && ./stage-test-profile.sh
SKYTEST_NO_AUTOLOAD=1 skytest replay ~/.cache/skytest-dbvotweaks mods/DBVODialogueTweaks/voiceboost.steps --headless --no-shots
    # expected: PASS; the log line "voice boost x2.50: voice V -> V×2.5 (sound N)" exists in $MYGAMES/SKSE/DBVODialogueTweaks.log
SKYTEST_NO_AUTOLOAD=1 skytest replay ~/.cache/skytest-dbvotweaks mods/DBVODialogueTweaks/voiceboost-control.steps --headless --no-shots
    # expected: PASS (absence gate); the log has no "voice boost" line
SKYTEST_NO_AUTOLOAD=1 skytest replay ~/.cache/skytest-dbvotweaks-nodll mods/DBVODialogueTweaks/voiceboost.steps --headless --no-shots
    # expected: FAIL at the log gate (no DLL, no line) — the A/B for the hook itself
SKYTEST_NO_AUTOLOAD=1 skytest replay ~/.cache/skytest-dbvotweaks mods/DBVODialogueTweaks/replyonlineend.steps --headless --no-shots
    # expected: still PASS — v5 unaffected by the reorder in the speak hook
```

Also read `$MYGAMES/SKSE/DBVODialogueTweaks.log` after the first run: it must contain the vtable-hook-installed line and must NOT contain "voice boost skipped" (the thread check). If it does contain it, stop: the `ownerThreadID` assumption failed and the spec's fallback (identify without the map) has to be designed, not patched in.

**Constraints:**
- `stage-test-profile.sh` must keep working when several zips exist in `dist/` (pick the highest version, `sort -V`).
- Sessions are detached: fire the replay, keep working, read the result. Don't shorten gates below the 180 s default to "speed up".

**Verification:** the four replay outcomes above, plus the log inspection.

**Commit after passing:** `test(dbvo): voiceboost.steps — the boost is verified in-engine on 1.7.104, by A/B`

---

### Task 5: release sweep — docs, page text, per-file build compatibility `[Mode: Direct]`

**Files:**
- Modify: `mods/DBVODialogueTweaks/README.md` — feature bullet (boost, clipping caveat, remove "attenuation only" + the workaround sentence added 2026-09-08), Configuration row `0–300%`, Requirements version note (`1.0.1 or newer` stays), Compatibility "reply timing verified" bullet gains the boost verification + `voiceboost.steps`, Testing section lists both new scripts, How-it-works "Volume" bullet describes the two-path split.
- Modify: `docs/dbvo-page.bbcode` — the same four spots in BBCode, a `[b]1.1.0[/b]` changelog entry, and a new `[heading]Files and Skyrim builds[/heading]` section with one line per uploaded file to paste into each file's Nexus description: `1.0.0 — Skyrim SE 1.5.97 through AE 1.6.1170 (Address Library formats 1 and 2)`; `1.0.1 — those plus 1.7.99 / 1.7.104 (format 5); no feature change`; `1.1.0 — same builds as 1.0.1; adds the boost`. Also fix the DBVO 2 paragraph's claim that `dialogue_volume` "is the volume" if it does not amplify (unknown → phrase as "its volume setting").
- Modify: `docs/ideas.md` — delete the two boost bullets under "2026-06-11 — DBVODialogueTweaks v3 volume-slider follow-ups" (the ruling now lives in the README + design doc); keep the section if anything else remains, else delete the heading.
- Modify: `docs/dbvo-landscape.md` — grep `attenuat|0–100|volume slider`; fix any attenuate-only statement.
- Modify: `CLAUDE.md` — the "Loading is not working" bullet: add the volume boost to the verified-functional list with the date and `voiceboost.steps`; the "skip / interrupt-cut / volume features are also still untested" clause drops `volume`.

**Test Cases:** `rg -n -i 'attenuation only|cannot amplify|0–100' mods/DBVODialogueTweaks/README.md docs/dbvo-page.bbcode docs/dbvo-landscape.md` → no hits except historical changelog lines.

**Constraints:** BBCode, not Markdown, in the page file (Nexus renders Markdown literally — `docs/ideas.md` records this). No duration/effort framing anywhere.

**Verification:** the `rg` above; `git diff --stat` touches exactly the listed files.

**Commit after passing:** `docs(dbvo): 1.1.0 — boost documented, per-file Skyrim-build text for the Nexus page`

**Manual (Mase):** paste `docs/dbvo-page.bbcode` onto the Nexus page, set the main version to 1.1.0, upload `dist/DBVO Dialogue Tweaks 1.1.0.zip` with its file description, and add the per-file build lines to the 1.0.0 and 1.0.1 file descriptions. Then listen: a quiet pack at 250–300% in a real conversation.

---
## Execution
**Skill:** superpowers:subagent-driven-development
- Mode A tasks (3, 4, 5): Opus implements directly
- Mode B tasks (1, 2): Dispatched to subagents
- Order: 1 → 2 → 3 → 4 → 5 (Task 4 needs 1–3; Task 5 needs 4's outcome to state "verified").
