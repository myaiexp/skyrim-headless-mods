# DBVODialogueTweaks v4 — cut the voice on skip (design)

**Status:** shipped, verified in-game 2026-06-14. **Tier:** SKSE C++ + swf (no new Papyrus).
**Builds on:** v1 (manual skip), v3 (the `Actor::SpeakSoundFunction` hook + the mod's DLL).

## Goal

Make skipping actually cut the **voice audio**, not just advance the menu. Two sub-features:

- **(a) Player line on skip.** v1's skip (E / left-click during `TOPIC_CLICKED`) fired `topicClicked`
  early but left the player's DBVO line playing — its tail overlapped the NPC reply. Now the line is
  cut when the skip fires.
- **(b) NPC reply on new-topic interrupt.** Picking a new topic while the NPC is mid-reply used to let
  the player's new line play **over** the still-talking NPC. Now the NPC's reply is cut first —
  including **multi-segment** replies (the hard case; see dead-ends).

## Architecture (3 levers, one sink)

```
swf (DialogueMenu.as)                SKSE C++ (plugin/src/main.cpp)
  trySkipPlayerLine()  --SendModEvent("CutPlayerDBVOLine")-->  ┐
  onSelectionClick()   --SendModEvent("CutNpcDBVOReply")---->  ┤ one ModCallbackEvent sink
                          (before initDBVO, cut-before-play)    ┘ (registered at kDataLoaded)
                                                                  ├── CutPlayerLine()
                                                                  └── CutNpcReply()
```

- **swf → SKSE bridge:** the swf already fires `skse.SendModEvent("PlayDBVOTopic", …)`, so AS2 can fire
  mod events directly. We add two: `CutPlayerDBVOLine` (in `trySkipPlayerLine`) and `CutNpcDBVOReply`
  (in `onSelectionClick`, **before** `initDBVO()` so the cut is queued ahead of the new line's
  `PlayDBVOTopic`). A single C++ `BSTEventSink<ModCallbackEvent>` dispatches both — **no Papyrus relay**
  (no new `.psc`, no per-reload re-registration).

- **(a) Player cut — retained SpeakSound handle.** DBVO plays the player line via console
  `Player.SpeakSound`, which routes through the v3 `Actor::SpeakSoundFunction` hook. The hook now
  **retains a by-value copy** of that line's `BSSoundHandle` (`g_playerLine`, mutex-guarded). `CutPlayerLine()`
  does `FadeOutAndRelease(30)` on it (guarded by `IsValid()/IsPlaying()`, slot reset after). Called
  directly off the sink thread — `BSSoundHandle` methods are soundID-keyed audio-manager calls, safe
  off the main thread (matches DialogueHistory / SpellHotbar2 prior art).

- **(b) NPC cut — `ExtraSayToTopicInfo.sound` + `PauseCurrentDialogue`.** The NPC reply is **not** a
  DBVO console SpeakSound (DBVO only voices the _player_), so it never reaches our hook. The per-line
  topic voice handle lives on the **speaking actor's `ExtraSayToTopicInfo` extra-data** (`.sound`).
  `CutNpcReply()` resolves the speaker from `MenuTopicManager` (falling back to `lastSpeaker`),
  `FadeOutAndRelease(30)`s that `.sound`, **and** calls `Actor::PauseCurrentDialogue()`. Both are
  required: the fade kills the segment that's **playing**, Pause stops the reply **advancing** to its
  remaining segments. Marshalled onto the main thread via the SKSE task interface (raw engine state).

## Dead-ends (do not re-try these — all tested in-game)

1. **NPC reply is engine topic voice, not SpeakSound.** Confirmed against DBVO's `.pex` and the Karat
   BSA (no NPC voice content). So sub-feature (b) cannot reuse the player's SpeakSound hook — there is
   no handle to retain.
2. **`PauseCurrentDialogue` alone does NOT cut a multi-segment reply.** It only _pauses_ (the actor's
   `ExtraSayToTopicInfo::voicePaused` flag is the tell). It cut single-segment replies (so it _looked_
   like it worked) but left multi-line replies sounding — the player talked over subtitle 3+.
3. **`HighProcessData::soundHandles[2]` are NOT the topic voice.** Fading them on cut had **zero**
   effect (verified by instrumented logging). They stay empty for topic dialogue.
4. The real handle is **`ExtraSayToTopicInfo.sound`** on the speaker — found by enumerating every
   `BSSoundHandle` member in the engine structs, then confirmed by runtime logging.
5. (Carried from v3, still true:) CommonLibSSE-NG `write_branch<5>` can't hook a function **entry** —
   use MinHook. That hook is what makes (a)'s handle retention possible.

## Open follow-ups (deferred)

- **NPC neutral expression on cut.** When the NPC reply is cut, the actor's facial expression freezes
  in its last speaking frame for ~1–2 s until the engine resets it — looks off. Reset the speaker's
  expression to neutral in `CutNpcReply()`. Tracked in `docs/ideas.md`.
- **Exact `.fuz`-duration NPC-reply scheduling** (the other half of the old "v3+" item) — unrelated to
  cutting; still deferred. See `docs/ideas.md`.
