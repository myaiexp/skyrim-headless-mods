# DBVODialogueTweaks v5 — NPC replies when your line actually ends (design)

**Status:** designed 2026-06-14, not yet built. **Tier:** SKSE C++ + swf + MCM (no new Papyrus
natives). **Builds on:** v2 (the gap slider + per-open `UI.SetFloat` push), v3 (the
`Actor::SpeakSoundFunction` hook + the mod's DLL), v4 (the retained `g_playerLine` handle + the
swf↔SKSE mod-event bridge).

## Goal

Stop _guessing_ when the player's voiced line ends and start _knowing_. Today the NPC reply is gated
by the swf's word-count estimate (`round(words × msPerWord) + pad`); v2 made that estimate tunable,
but no constant tracks per-line reality — fast packs (Karat) finish well before the estimate, slow
ones after. v5 detects the **real end** of the player line and fires the reply a small, configurable
beat later. This is the "exact `.fuz`-duration scheduling" item deferred under v4 in `docs/ideas.md`,
realized as **end-detection** rather than duration-prediction (see "Why this shape").

The capstone: with this in, the gap is correct on every line, every voice pack, with no calibration.

## The problem, precisely

The reply trigger lives entirely in `Interface/dialoguemenu.swf`. On topic click,
`startTopicClickedTimer(voicePackID)` does (post-v2):

```actionscript
var mspw = this.dbvoMsPerWord >= 0 ? this.dbvoMsPerWord : 200;
var pad  = this.dbvoPadMs    >= 0 ? this.dbvoPadMs    : 1400;
_loc4_ = Math.round(text.split(" (")[0].split(" ").length * mspw) + pad;
this.timer = setTimeout(this, "topicClicked", _loc4_);   // topicClicked → GameDelegate "TopicClicked" → NPC replies
```

The swf cannot read audio length, so any `mspw`/`pad` is wrong for some line. The SKSE plugin,
however, already holds the player line's live `BSSoundHandle` (`g_playerLine`, captured in the v3/v4
SpeakSound hook) — the one thing that knows when the audio truly ends.

## Architecture

The plugin owns the **when**; the swf still owns the **how** (it already knows how to fire
`topicClicked`). One new swf method, one watcher in the DLL, reusing the existing handle.

```
swf (DialogueMenu.as)                         SKSE C++ (plugin/src/main.cpp)

 onSelectionClick → initDBVO                    SpeakSoundHook::thunk
   └ SendModEvent("PlayDBVOTopic")  ──DBVO──►     (player + DBVO path)
                                                   ├ retain g_playerLine   (v4, unchanged)
 startTopicClickedTimer(packID)                    └ ARM the end-watcher    (NEW)
   └ setTimeout(topicClicked, BACKSTOP)  ◄─┐
        (generous internal hang-guard)      │     per-frame watcher (main thread, while armed):
                                            │       observe handle playing … → stopped
 dbvoOnPlayerLineEnded()  (NEW)  ◄──GFx Invoke──    fire dbvoOnPlayerLineEnded(), then DISARM
   ├ clearTimeout(this.timer)                       (RE::UI → "Dialogue Menu" movie → Invoke)
   └ setTimeout(topicClicked, gap)
        (gap = the surviving pad slider)
```

- **Arm.** The SpeakSound hook already runs `original()` (which starts the sound synchronously) then
  retains `g_playerLine` for the player+DBVO case. In that same branch, set an `armed` flag. The
  watcher only looks while armed, so the per-frame cost exists only during the brief window a player
  line is in flight.

- **Watch (main thread, per frame).** While armed, poll the retained handle. Require observing
  `IsPlaying() == true` **at least once** before treating `IsPlaying() == false` as "ended" — guards
  the sliver between arming and the audio engine actually starting the sound. On the playing→stopped
  transition, invoke the swf and disarm.

- **Fire.** `dbvoOnPlayerLineEnded()` clears the backstop timer and `setTimeout(topicClicked, gap)`.
  The gap is the surviving v2 pad value, already pushed onto the live menu per dialogue-menu open via
  `UI.SetFloat` (no change to that plumbing — only its meaning changes: it's now silence after the
  _real_ end, not padding on a guess).

- **Backstop.** `startTopicClickedTimer`'s non-"off" path keeps a `setTimeout(topicClicked, …)` but
  with a **generous internal estimate** (e.g. `words × 300 + 2000`, hardcoded in the swf, not a knob)
  purely so dialogue never hangs if the end-signal never arrives. Normal flow: the plugin's fire
  replaces this timer with the short gap timer long before the backstop would elapse. It only fires on
  its own when no playing handle ever existed (missing `.fuz`).

## State machine / edge cases

- **Skip** (existing `CutPlayerDBVOLine`, fired by `trySkipPlayerLine`): the swf already cuts the line
  **and calls `topicClicked()` itself** in the same method. So the plugin's `CutPlayerLine()` must also
  **disarm** the watcher — otherwise the faded handle's playing→stopped transition would fire a second
  reply. Skip → swf fires; watcher stands down.

- **New topic mid-reply** (existing `CutNpcDBVOReply` / the next `onSelectionClick`): the next
  `PlayDBVOTopic` → SpeakSound re-arms naturally. No special handling.

- **Menu closes / conversation ends while armed**: disarm. The fire path is additionally guarded to
  invoke only when the "Dialogue Menu" is open, so a late transition can't poke a dead movie.

- **`voicePackID == "off"`** (DBVO has no voice for this line / voice-over disabled): unchanged — the
  swf fires `topicClicked` immediately and never arms.

- **Re-entrancy / single line scope.** Only the most-recent line is armed (each SpeakSound capture
  overwrites `g_playerLine` and re-arms) — exactly matching the existing v4 "only the latest line is
  cuttable" scope.

## swf + MCM changes

**swf (`DialogueMenu.as`):**

- **New method `dbvoOnPlayerLineEnded()`** — clear `this.timer`, `setTimeout(topicClicked, gap)` where
  `gap = this.dbvoPadMs >= 0 ? this.dbvoPadMs : 250`.
- **`startTopicClickedTimer`** — non-"off" path sets the generous internal backstop; stops reading
  `dbvoMsPerWord`. The `dbvoMsPerWord` var becomes dead and is removed from the class. The baked
  `dbvoPadMs` fallback default changes `1400 → 250` (it's now a trailing beat, not estimate padding).

**MCM (`DBVODialogueTweaksMCM.psc`):**

- **Drop the per-word knob entirely** — the `fMsPerWord` property, its `_mspwOID` /
  `AddSliderOption("Per-word delay", …)` / `OnOptionSliderOpen` / `OnOptionSliderAccept` branches, **and**
  the `UI.SetFloat(… "dbvoMsPerWord" …)` line in `OnMenuOpen`.
- **Relabel the surviving slider** "NPC response pad" → **"Gap after your line ends"**; range
  `0–2500 → 0–1000` (0 = instant), `SetSliderDialogDefaultValue` `1400 → 250`. Its per-open
  `UI.SetFloat(… "dbvoPadMs", fPadMs)` push is unchanged.
- **Reseed `fPadMs` + version bump (load-bearing).** `OnMenuOpen` re-pushes the *stored* `fPadMs`
  (default `1400.0`) on every dialogue open, so just changing the swf's baked default does nothing —
  the slider would read "250" but behave as 1400. So: declared default `fPadMs = 1400.0 → 250.0`, and
  `GetVersion() 2 → 3` with an `OnVersionUpdate` branch that reseeds `fPadMs = 250.0`. Same "a property
  whose meaning changed must be reseeded on migration" move v3 used for `fPlayerVoiceVol` (psc lines
  18–34). Reseeding is correct precisely because the meaning changed: an old persisted pad (1400, or a
  user-tuned 900) is meaningless as a post-real-end gap.
  - **Migration must not clobber the other settings.** The live upgrade path is **stored version 2 →
    3** (Mase's save is already at 2: v3 bumped 1→2). SkyUI calls `OnVersionUpdate(GetVersion())`
    **once with the target version** (not once per step), so the version-3 branch reseeds `fPadMs`
    **only** and leaves `fPlayerVoiceVol` and the 2-page array alone (both already correct at version 2;
    re-seeding voice would wipe a tuned value). Pre-voice (version-1) saves don't exist in the wild —
    nothing public shipped — so the 1→3 jump is a non-issue to engineer now; note it in the plan only
    if the deferred public release revisits it.

## How C++ signals the swf

Direct Scaleform invoke from the plugin, on the main thread (where the watcher runs):
`RE::UI::GetSingleton()->GetMovie("Dialogue Menu")` → `movie->Invoke("_root.DialogueMenu_mc.dbvoOnPlayerLineEnded", …)`.
This is a **new channel in this plugin** (v2 pushed swf values from _Papyrus_; the plugin only ever
_received_ swf mod events), but `GFxMovie::Invoke` is a standard CommonLibSSE-NG capability and the
watcher is already main-thread, so no marshalling beyond what the watcher mechanism gives us.

**Rejected alternative — Papyrus relay.** C++ `SendModEvent("DBVOLineEnded")` → the v2 quest script
`RegisterForModEvent`s it → does `UI.Invoke(...)` to the swf (reusing v2's exact Papyrus→swf push).
Avoids GFx in C++, but adds a C++→VM→swf hop whose scheduling jitter is precisely the imprecision v5
exists to remove. Direct invoke keeps the gap deterministic.

## Why this shape (detect, not predict)

Two ways to "know when the line ends": **predict** it (read the `.fuz`/`.xwm` duration up front, hand
the swf an exact timeout) or **detect** it (watch the live handle, fire on real stop). Chose detect:

- Observes ground truth — correct on every line/pack, and naturally robust to anything that
  stretches/pauses playback; a prediction is not.
- Reuses the `IsValid()`/`IsPlaying()` path already proven in v4's `CutPlayerLine()`. No audio-file or
  BSA parsing, no dependence on a duration field whose availability on `BSSoundHandle` couldn't be
  confirmed without a build.
- Cost is the swf↔DLL "fire now" round-trip + a per-frame poll while armed — both cheap and bounded to
  the in-flight window.

Predict stays a viable fallback only if detection proves flaky in-game; not expected.

## Testing

Manifests only on top of DBVO + a voice pack, so it's a **full-profile `skytest play`** test (like
v2/v4), not vanilla+1. Install the built swf + DLL + MCM over the live DBVO+Karat order and verify:

1. **Fast line (Karat short topic)** — reply lands right after the audio ends, no dead air.
2. **Long line** — no premature reply; reply still tracks the real end.
3. **Skip** (E / click during `TOPIC_CLICKED`) — line cut, **exactly one** reply (no double-fire from
   the watcher).
4. **New-topic interrupt** — NPC reply still cut (v4 path intact), new line re-arms.
5. **Gap slider** — 0 = effectively instant, larger = visibly longer beat; survives save/reload.
6. **Missing-audio line** (force a topic whose `.fuz` is absent) — backstop still advances dialogue.

## Open items to confirm during planning

- **~~Per-frame watcher mechanism~~ — RESOLVED (and corrected, see Dead-ends).** Shipped as a single
  **detached poll thread** (off the main thread, ~30 ms sleep), NOT a main-thread tick.
- **~~`GFxMovie::Invoke` signature / movie lookup~~ — RESOLVED.** `RE::UI::GetSingleton()->GetMenu(
  RE::DialogueMenu::MENU_NAME)->uiMovie->InvokeNoReturn("_root.DialogueMenu_mc.dbvoOnPlayerLineEnded",
  nullptr, 0)`, on the main thread. Verified against the fetched CommonLib headers and at runtime.

## Dead-ends (do not re-try)

- **Watcher as a self-re-arming `SKSE::GetTaskInterface()->AddTask` loop on the main thread — FROZE the
  game** for the entire player-line duration (verified in-game 2026-06-14). SKSE drains its task queue
  to empty each pass, so a task that re-queues itself spins the frame and never yields — the freeze
  lasted exactly the armed window, then released when the line ended and the watcher disarmed. **Fix:**
  poll on a **detached thread** with an explicit `sleep_for` (so it can never saturate the main thread),
  and marshal only the one-shot `FireReplyNow` (the Scaleform call) back to the main thread via a single
  `AddTask` — the proven SkytestProbe pattern. `IsValid()`/`IsPlaying()` on the handle are soundID-keyed
  audio-manager calls safe off the main thread (v4's `CutPlayerLine` precedent), so the poll itself
  needs no marshalling. Lesson: never use a self-re-arming SKSE task as a per-frame tick.
