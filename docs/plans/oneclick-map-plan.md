# OneClickMap Implementation Plan

**Goal:** An SKSE C++ plugin that removes the world-map confirmation popups so a click does the obvious action in one step — discovered location → instant fast travel; non-travelable spot with no custom marker → instant place; otherwise → the Move/Leave/Remove menu.

**Architecture:** A single standalone CommonLibSSE-NG DLL hooking the MapMenu click handler (`RELOCATION_ID(52208, 53095)`). At each click it reads two predicates — is the marker under the cursor travelable, and does a custom marker already exist — and dispatches one of three actions instead of letting the default message box appear. Built with the repo's existing headless clang-cl → Windows-DLL toolchain, mirroring `mods/GhostAllies/` and `mods/AutoFireBow/`.

**Tech Stack:** C++23, CommonLibSSE-NG (FetchContent, runtime SE+AE), spdlog, clang-cl + lld-link + xwin cross-build, CMake/Ninja.

**Spec:** `docs/plans/oneclick-map-design.md` (read it — behavior table, accepted consequences, and the proof-point all live there).

**Testing reality:** SKSE plugins have no Linux unit-test harness here. "Tests" are (a) the cross-build succeeding (automatable on Linux via `build.sh`), and (b) in-game verification scenarios with expected `OneClickMap.log` lines (manual — Mase runs the game on this desktop's Proton prefix). Each task states both.

---

### Task 1: Scaffold the plugin (builds + loads as a no-op) [Mode: Direct]

Stand up the DLL so it compiles with the cross toolchain and loads in-game, doing nothing yet. Pure boilerplate mirrored from `mods/GhostAllies/`.

**Files:**
- Create: `mods/OneClickMap/CMakeLists.txt`
- Create: `mods/OneClickMap/build.sh`
- Create: `mods/OneClickMap/src/main.cpp`

**Contracts:**
- `CMakeLists.txt`: copy `mods/GhostAllies/CMakeLists.txt` verbatim, replacing the project name and target with `OneClickMap` (same spdlog v1.13.0 / CommonLibSSE-NG pinned tag / `cxx_std_23` / `PREFIX "" SUFFIX ".dll"`). Drop the `rapidcsv` block — OneClickMap has no CSV dependency.
- `build.sh`: copy `mods/GhostAllies/build.sh` verbatim, replacing every `GhostAllies` with `OneClickMap`. Same `--install` path (`$GAME_DATA/SKSE/Plugins`).
- `main.cpp` must expose the SKSE plugin entry and version data CommonLibSSE-NG requires (mirror GhostAllies):
  - Use the modern declarative form GhostAllies uses: `SKSEPluginInfo(...)` macro + `SKSEPluginLoad(const SKSE::LoadInterface*)` (`mods/GhostAllies/src/main.cpp` ~L412–429). There is no `SKSEPlugin_Version`/`SKSEPlugin_Load` pair in this repo — copy the sibling's form exactly.
  - `SKSE::Init(skse)` then open a log sink at `<SKSE log dir>/OneClickMap.log` via `spdlog::basic_logger_mt`, pattern matching GhostAllies, default level `info`.
  - On load, log one banner line: `OneClickMap loaded (v<version>)`.
- No hooks yet.

**Constraints:**
- Must use the runtime-detection CommonLibSSE-NG build (single DLL covers SE 1.5.97 + AE 1.6.x including 1.6.1170). Do not pin to a runtime.
- Name is locked to `OneClickMap` everywhere (dir, CMake target, DLL, log) per the design.

**Verification:**
- Run: `./mods/OneClickMap/build.sh`
- Expected: `built: .../OneClickMap.dll`, and `file` reports a PE32+ DLL for MS Windows.
- In-game (manual): `./mods/OneClickMap/build.sh --install`, restart Skyrim, confirm `<prefix>/Documents/My Games/Skyrim Special Edition/SKSE/OneClickMap.log` contains the `OneClickMap loaded` banner and the game reaches the main menu without a crash.

**Commit after the build succeeds** (don't gate the commit on the manual in-game check).

---

### Task 2: Proof-point probe — prove the three hook-site reads [Mode: Delegated]

The gating research task from the design. Hook the click handler read-only and **log**, without changing any behavior, to prove every signal the dispatch needs is reachable at that site. This is throwaway/observational code that Task 3 builds on; keep it behind a verbose log level.

**Files:**
- Modify: `mods/OneClickMap/src/main.cpp`

**Contracts:**
- Install a hook at `RELOCATION_ID(52208, 53095)` (the MapMenu click handler PapyrusExtender hooks; cross-check the exact call offset against `powerof3/PapyrusExtenderSSE` `src/Game/HookedEventHandler.cpp` namespace `FastTravel`). Use SKSE trampoline / `write_call` or a vfunc write as appropriate — match the precedent.
- On each map click, log a single structured line capturing all three reads:
  1. **Cursor-target REFR + travelable:** resolve the `MapMenu` runtime `mapMarker` (`ObjectRefHandle`); if valid, read `refr->extraList.GetByType<RE::ExtraMapMarker>()->mapData->flags.any(RE::MapMarkerData::Flag::kCanTravelTo)`. Log: marker present? travelable?
  2. **Custom marker exists:** `RE::PlayerCharacter::GetSingleton()->playerMapMarker.get()` non-null? Log it.
  3. **Popup #4 trigger:** identify how the engine raises the "Move your marker or remove it?" (Move It / Leave It / Remove It) box — the callback class and/or the MapMenu code path. Find it the way `FastTravelConfirmCallback` was found (RTTI/vtable scan, CommonLibSSE-NG header search, decompile reference). Log the finding (address/class) — or log that it could not be located.
  4. **Travel-confirm callback handle:** also determine, at this same site, how to obtain the `RE::FastTravelConfirmCallback` instance (or the equivalent travel-trigger entry point) needed to drive travel in Task 3 — this is the same `52208/53095` site PapyrusExtender hooks, so it belongs to this probe, not a surprise in Task 3. Log whether it is reachable.
- The original click handler must still run normally (call original; this task changes nothing the player sees).

**Constraints:**
- Read-only: no travel, no placement, no suppression in this task.
- Resolve all `RELOCATION_ID`s through the address library; do not hardcode raw addresses.

**Verification (manual, in-game — this is the gate):**
1. Open the world map, click a **discovered** location → log shows `marker=yes travelable=yes customMarker=<state>`.
2. Click an **undiscovered** location → log shows `travelable=no`.
3. With **no** custom marker set, then with **one** set → log shows `customMarker=no` then `customMarker=yes` correctly.
4. The popup #4 trigger is identified and recorded in the log / a code comment, **or** documented as not-locatable.

**Gate decision (record in the plan/commit message):**
- If reads 1–3 are correct **and** the popup #4 trigger is located → proceed to Task 3 full behavior.
- If the popup #4 trigger **cannot** be located → proceed to Task 3 with the design's documented fallback (undiscovered-location + marker-exists falls through to vanilla box #3; empty-terrain + marker-exists still gets native box #4).

**Commit after the build succeeds**, noting the gate outcome.

---

### Task 3: Three-way dispatch — the actual behavior [Mode: Delegated]

Replace the read-only probe with the real action: suppress the default box and perform the dispatched action, per the design's reduced logic.

**Files:**
- Modify: `mods/OneClickMap/src/main.cpp`

**Contracts:**
Implement the design's logic at the hook:

```
if (markerUnderCursor && travelable)        -> drive fast travel, suppress box   // skips box #1
else if (no custom marker exists)           -> MapMenu::PlaceMarker(), suppress box // skips box #2
else                                        -> raise Move/Leave/Remove (box #4)
```

- **Travel branch:** drive the fast-travel confirm path with `RE::IMessageBoxCallback::Message::kUnk1` and dismiss the menu, using the PapyrusExtender idiom (`func(callback, Message::kUnk1)` + `RE::UIMessageQueue::AddMessage(RE::MapMenu::MENU_NAME, kHide, nullptr)` if needed). Do **not** let box #1 render.
- **Place branch:** call `RE::MapMenu::PlaceMarker()` (`RELOCATION_ID(52226, 53113)`, member fn on the live `MapMenu`). It sources the cursor world position itself. Do **not** let box #2 render.
- **Management branch:** raise box #4 via the trigger found in Task 2. If Task 2 hit the fallback, this branch instead **calls the original handler** for that case (vanilla shows #3 for undiscovered-location, #4 for empty-terrain).
- The hook must cleanly support **both** outcomes — *suppress and act* (travel/place branches) and *pass through to original* (fallback management case). Treat these as two distinct, explicit control-flow paths, not an afterthought.
- Each branch logs which action it took (keep at `info` so verification reads the log).

**Constraints:**
- Only the map-menu click path is touched. No other UI, no `.esp`, no Papyrus, no config (v1 is unconditional).
- Discovered-location "Place Marker" option is intentionally gone (accepted consequence #1) — do not try to preserve it.

**Verification (manual, in-game — definition of done, from the design):**
1. Click a discovered location → travels immediately, **no** popup.
2. Click undiscovered / empty spot with **no** marker → marker placed at cursor immediately, **no** popup.
3. Click anywhere non-travelable **with** a marker set → Move/Leave/Remove menu appears (including on an undiscovered location — the changed box #3; or, on the fallback, box #3 on locations and #4 on empty terrain).
4. Move It / Leave It / Remove It each behave correctly (relocate to clicked spot / cancel / delete).
5. No regression to the map stack (FWMF satellite render, FlatMapMarkers, Baka pan/FOV) — map opens, renders, pans, no crash.

**Commit after the build succeeds.**

---

### Task 4: Document the shipped mod [Mode: Direct]

**Files:**
- Modify: `~/Projects/skyrim-headless-mods/README.md` (add a `mods/OneClickMap/` row to the table, one-line description matching the AutoFireBow/GhostAllies rows; mark AE-tested or built-untested per what Task 3's in-game run actually achieved).
- Modify: `docs/plans/oneclick-map-design.md` (flip the `Status:` line to shipped/verified or built-untested, recording the Task 2 gate outcome and any fallback taken).

**Constraints:**
- State only what was actually verified in-game. If Mase did not run a given scenario, say "built, untested" — do not claim verification that didn't happen (repo convention; matches GhostAllies' honest status lines).
- No mase.fi logging, no `git deployboth` (Skyrim workspace rule).

**Verification:** `git status` clean after commit; README table renders (run `prettier --write README.md` if the table was edited).

**Commit.**

---
## Execution
**Skill:** superpowers:subagent-driven-development
- Task 1 (scaffold): Mode A — Opus implements directly (boilerplate mirror).
- Task 2 (probe / RE proof-point): Mode B — dispatched to subagent (reverse-engineering, exploration).
- Task 3 (three-way dispatch): Mode B — dispatched to subagent (engine integration, multiple valid hook shapes).
- Task 4 (docs): Mode A — Opus implements directly.

**Note:** Tasks 2 and 3 each end in a manual in-game verification that only Mase can run (Proton prefix on this desktop). Build-success is the automatable commit gate; in-game results are recorded after Mase tests.
