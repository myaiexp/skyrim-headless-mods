# DBVODialogueTweaks v5 — reply-on-line-end Implementation Plan

**Goal:** Replace the swf's word-count reply timer with detection of the player line's real end —
the SKSE plugin watches the retained `g_playerLine` handle and, the moment it stops, tells the swf to
fire the NPC reply after a small configurable gap.

**Architecture:** The DLL owns the *when* (a detached poll thread watches the handle while armed — see
the Task 3 correction; the original main-thread `AddTask` loop froze the game and was replaced;
on the playing→stopped transition it invokes a new swf method via Scaleform). The swf owns the *how*
(it already knows how to fire `topicClicked`) and keeps a generous internal backstop so dialogue never
hangs if no end-signal arrives. The MCM drops the now-meaningless per-word knob and repurposes the pad
knob as the trailing gap.

**Tech Stack:** CommonLibSSE-NG (SKSE C++, clang-cl/xwin cross-build), ActionScript 2 (ffdec recompile
of `dialoguemenu.swf`), Papyrus (SkyUI `SKI_ConfigBase` MCM, wine PapyrusCompiler).

**Design:** `docs/plans/dbvo-v5-reply-on-line-end-design.md` (authoritative — read for rationale,
dead-ends, the detect-not-predict decision).

---

## File structure

| File | Change | Responsibility |
| ---- | ------ | -------------- |
| `mods/DBVODialogueTweaks/plugin/src/main.cpp` | Modify | Arm on SpeakSound capture; self-re-arming watcher task; GFx fire; disarm on skip / menu-close. The whole state machine. |
| `mods/DBVODialogueTweaks/src/__Packages/DialogueMenu.as` | Modify | New `dbvoOnPlayerLineEnded()`; `startTopicClickedTimer` → generous backstop; drop `dbvoMsPerWord`. |
| `mods/DBVODialogueTweaks/src/papyrus/DBVODialogueTweaksMCM.psc` | Modify | Drop per-word slider/property/push; relabel + reseed pad as the gap; `GetVersion` 2→3 migration. |

No new files. `DBVOTweaks.psc` (the native bridge) and `build.sh` are unchanged.

All three change together at runtime but are independent edits; build order is swf → MCM → DLL (matches
`build.sh`). Verification is in-game on the full profile (no unit-test harness for these tiers — see
"Verification reality" below).

---

### Task 1: swf — fire-on-signal + backstop  [Mode: Direct]

**Files:**
- Modify: `mods/DBVODialogueTweaks/src/__Packages/DialogueMenu.as`

**Contracts:**

- Remove the class field `var dbvoMsPerWord;` (keep `var dbvoPadMs;`).
- `startTopicClickedTimer(voicePackID)` — the `"off"` branch is **unchanged** (immediate
  `GameDelegate.call("TopicClicked", …)`). The non-`"off"` branch stops reading `dbvoMsPerWord` and sets
  a **generous internal backstop** instead of the tuned estimate:
  ```actionscript
  var words = this.TopicListHolder.List_mc.selectedEntry.text.split(" (")[0].split(" ").length;
  var backstop = Math.round(words * 300) + 2000;   // hang-guard only; plugin normally fires first
  this.timer = setTimeout(this, "topicClicked", backstop);
  this.skipArmedAt = getTimer();
  ```
- New method `dbvoOnPlayerLineEnded()` — invoked by the DLL when the real line ends. Guarded so a
  spurious/late call is a no-op; replaces the backstop timer with the short gap timer:
  ```actionscript
  function dbvoOnPlayerLineEnded() {
     if (this.eMenuState == DialogueMenu.TOPIC_CLICKED && this.timerBool && this.timer != undefined) {
        clearTimeout(this.timer);
        var gap = this.dbvoPadMs >= 0 ? this.dbvoPadMs : 250;
        this.timer = setTimeout(this, "topicClicked", gap);
     }
  }
  ```

**Constraints:**
- Do not touch `topicClicked`, `trySkipPlayerLine`, `onSelectionClick`, or the mod-event sends — the v4
  skip/cut paths must stay intact. The `timerBool`/`timer != undefined` guards in `dbvoOnPlayerLineEnded`
  are what prevent a double-fire if a skip and a natural end race.
- Only `src/__Packages/DialogueMenu.as` is authored; ffdec leaves all other swf classes untouched.

**Test Cases (in-game, Task 4):** backstop alone (no DLL signal) still advances dialogue; the gap timer
fires the reply after a natural end; skip during the gap advances immediately with no double reply.

**Verification:** `./build.sh` step [1/4] recompiles the swf; confirm its md5 differs from the stock
`b1f70c58…`. Full behavior verified in Task 4.

**Commit after the swf + its two siblings build clean (or commit per-file if preferred).**

---

### Task 2: MCM — drop per-word, repurpose pad as the gap, migrate  [Mode: Direct]

**Files:**
- Modify: `mods/DBVODialogueTweaks/src/papyrus/DBVODialogueTweaksMCM.psc`

**Contracts (exact edits):**
- Delete the `Float Property fMsPerWord = 200.0 Auto` declaration and the `Int _mspwOID` field.
- `Float Property fPadMs = 1400.0 Auto` → `Float Property fPadMs = 250.0 Auto`.
- `GetVersion()` returns `3` (was `2`).
- `OnVersionUpdate(Int aVersion)` — **add** a version-3 branch that reseeds the gap **only**; leave the
  existing `aVersion == 2` block as-is:
  ```papyrus
  Event OnVersionUpdate(Int aVersion)
      If aVersion == 2
          fPlayerVoiceVol = 100.0
          Pages = new String[2]
          Pages[0] = "Timing"
          Pages[1] = "Voice"
      EndIf
      If aVersion == 3
          fPadMs = 250.0      ; pad's meaning changed (now a post-real-end gap) — old value is stale
      EndIf
  EndEvent
  ```
- `OnPageReset` "Timing": drop the per-word `AddSliderOption`; the surviving slider relabels:
  ```papyrus
  _padOID = AddSliderOption("Gap after your line ends", fPadMs, "{0} ms")
  ```
- `OnOptionSliderOpen`: delete the `_mspwOID` branch; the `_padOID` branch becomes range `0–1000`,
  default `250`, interval `25` (`SetSliderDialogStartValue(fPadMs)` unchanged).
- `OnOptionSliderAccept`: delete the `_mspwOID` branch (keep `_padOID` and `_volOID`).
- `OnMenuOpen`: delete the `UI.SetFloat("Dialogue Menu", "_root.DialogueMenu_mc.dbvoMsPerWord", fMsPerWord)`
  line; keep the `dbvoPadMs` push.

**Constraints:**
- The live upgrade path is **stored version 2 → 3** (Mase's save). SkyUI's `CheckVersion` calls
  `OnVersionUpdate(GetVersion())` once with the **target** (3) — confirmed by v3 having migrated 1→2 via
  an `aVersion == 2` block. So the `aVersion == 3` branch reseeds `fPadMs` only and must NOT re-touch
  `fPlayerVoiceVol` or `Pages` (already correct at v2; re-seeding voice would wipe a tuned value).
- Do not touch the "Voice" page or `DBVOTweaks.SetPlayerVoiceVolume` wiring.

**Test Cases (in-game, Task 4):** after load, MCM "Timing" shows one slider labelled "Gap after your
line ends" defaulting to 250; changing it changes the silence before the reply; value survives
save/reload; the voice-volume slider is unchanged.

**Verification:** `./build.sh` step [2/4] compiles `DBVODialogueTweaksMCM.pex` with no errors.

**Commit after build clean.**

---

### Task 3: SKSE plugin — arm / watch / fire / disarm  [Mode: Direct]

> **CORRECTION (2026-06-14, post-ship):** the watcher below was first built as a **main-thread
> self-re-arming `AddTask` loop** (the `g_watchScheduled` / `WatchReplyTick` code in this task) and it
> **froze the game for the whole player line** — SKSE drains its task queue to empty, so a self-re-queuing
> task spins the frame. **Shipped mechanism = a single detached poll thread** (off the main thread,
> ~30 ms `sleep_for`) that marshals only the one-shot `FireReplyNow` to the main thread via one `AddTask`.
> `g_watchScheduled` is gone. See the design doc's "Dead-ends" section; `main.cpp` is the source of truth.

**Files:**
- Modify: `mods/DBVODialogueTweaks/plugin/src/main.cpp`

**State (file-scope, alongside the existing `g_playerLine`/`g_playerLineMtx`):**
```cpp
static std::atomic<bool> g_replyArmed{ false };    // a player DBVO line is in flight, awaiting end
static std::atomic<bool> g_sawPlaying{ false };    // start-race guard: handle observed playing once
static std::atomic<bool> g_watchScheduled{ false };// exactly one re-arming watcher task in flight
```

**Contracts:**

- **Arm** — in `SpeakSoundHook::thunk`, inside the existing `IsPlayerRef() && is_dbvo_path(...)` block
  (right after `g_playerLine = *a_handle;`): `g_sawPlaying = false; g_replyArmed = true;` then schedule
  the watcher exactly once:
  ```cpp
  bool expected = false;
  if (g_watchScheduled.compare_exchange_strong(expected, true)) {
      SKSE::GetTaskInterface()->AddTask([]() { WatchReplyTick(); });
  }
  ```
  (The hook may run off the main thread; `AddTask` is safe to call from any thread. A re-arm while a
  tick is already scheduled is absorbed by the CAS guard — the live loop picks up the new
  `g_playerLine`/reset `g_sawPlaying` under the mutex.)

- **Watch** — `void WatchReplyTick()` runs on the main thread (it's an `AddTask` callback):
  ```cpp
  void WatchReplyTick() {
      if (!g_replyArmed.load()) { g_watchScheduled = false; return; }   // disarmed → stop
      bool playing = false;
      { std::scoped_lock l{ g_playerLineMtx };
        playing = g_playerLine.IsValid() && g_playerLine.IsPlaying(); }
      if (playing) { g_sawPlaying = true; }
      if (g_sawPlaying.load() && !playing) {                            // real end
          g_replyArmed = false; g_watchScheduled = false;
          FireReplyNow();
          return;
      }
      SKSE::GetTaskInterface()->AddTask([]() { WatchReplyTick(); });    // re-arm next frame
  }
  ```

- **Fire** — `void FireReplyNow()` (main thread; null-guards double as the menu-open guard):
  ```cpp
  void FireReplyNow() {
      auto* ui = RE::UI::GetSingleton();
      if (!ui) { return; }
      auto menu = ui->GetMenu(RE::DialogueMenu::MENU_NAME);   // GPtr<IMenu>; null if not open
      if (!menu || !menu->uiMovie) { return; }
      menu->uiMovie->InvokeNoReturn("_root.DialogueMenu_mc.dbvoOnPlayerLineEnded", nullptr, 0);
  }
  ```

- **Disarm on skip** — in `CutPlayerLine()` (already runs on skip), add `g_replyArmed = false;` so the
  faded handle's playing→stopped transition does NOT fire a second reply (the swf's `trySkipPlayerLine`
  already called `topicClicked()` itself). Keep the existing fade/handle-reset.

- **Disarm on menu close** — add a `BSTEventSink<RE::MenuOpenCloseEvent>` that, on
  `menuName == RE::DialogueMenu::MENU_NAME && !opening`, sets `g_replyArmed = false;`. Register it at
  `kDataLoaded` via `RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(sink)` (same deferral
  point as the existing `ModCallbackEvent` sink). May share or sit beside the existing sink class.

**Constraints:**
- Reads of `g_playerLine` stay under `g_playerLineMtx` (the v4 invariant). `g_replyArmed`/`g_sawPlaying`/
  `g_watchScheduled` are atomics — no extra lock.
- `WatchReplyTick`/`FireReplyNow` must only ever run on the main thread (they touch Scaleform). They are
  only ever invoked as `AddTask` callbacks — keep it that way.
- Do not change the volume-scaling behavior, the `is_dbvo_path` gate, the MinHook install, or
  `CutNpcReply`. v3/v4 must be untouched.
- A spurious arm with no dialogue menu open is harmless: `FireReplyNow` no-ops on a null menu and the
  loop self-terminates when the line ends.

**Test Cases (in-game, Task 4):**
- Fast Karat line → reply lands right after audio ends (no dead air).
- Long line → no premature reply; reply tracks the real end.
- Skip (E / click in `TOPIC_CLICKED`) → line cut, **exactly one** reply.
- New-topic interrupt → NPC reply still cut (v4 intact), new line re-arms and schedules correctly.
- Missing-audio line → backstop advances dialogue (no hang).

**Verification:** `./build.sh` step [4/4] cross-builds `DBVODialogueTweaks.dll` with no errors;
`file` reports a PE32+ DLL.

**Commit after the DLL builds clean.**

---

### Task 4: Build, install, in-game verification  [Mode: Direct]

**Steps:**
1. `cd mods/DBVODialogueTweaks && ./build.sh` — all five artifacts build (swf md5 ≠ stock; both `.pex`
   compile; esp regenerates; DLL is PE32+).
2. Install onto the **full live profile** (this manifests only on top of DBVO + a voice pack, so it is
   NOT a vanilla+1 `skytest test`): `./build.sh --install`, then **fully restart** Skyrim (Papyrus VM
   caches `.pex` per session). Drive via `skytest play` / `skytest playtest` per `skytest/README.md`.
3. Walk the Task-3 test cases against live DBVO + Karat. Record evidence (screenshots / observed timing)
   before declaring done — per the repo's "evidence before claims" rule.

**Verification reality:** there is no unit-test harness for the swf/Papyrus/SKSE tiers in this repo;
"test cases" above are in-game scenarios and Task 4 is where they are actually exercised. A green build
is necessary but not sufficient — the feature is "done" only after the in-game walk passes.

**Commit** any fixes found during verification; update the mod `README.md` v-table (add the v5 row) and
flip `docs/ideas.md`'s v5 entry from "designed" to "shipped, verified" once it passes.

---

## Execution
**Skill:** superpowers:subagent-driven-development
All tasks are **Mode: Direct** — the contracts are fully specified and the three edits are tightly
coupled around shared runtime state, so Opus implements them directly in this session rather than
dispatching subagents. Build + in-game verification (Task 4) is the gate before "done".
