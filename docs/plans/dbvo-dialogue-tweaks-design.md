# DBVODialogueTweaks — design

**Mod:** `mods/DBVODialogueTweaks/` (renamed from `DBVOResponseGap`).
A small set of swf/Papyrus/SKSE tweaks to DBVO's dialogue pacing. Built in phases:

| Phase  | Feature                                                       | Tier                  | Status                         |
| ------ | ------------------------------------------------------------- | --------------------- | ------------------------------ |
| **v1** | **Manual player-line skip** (E / left-click)                  | swf only              | **this design — building now** |
| v2     | Configurable response gap + speed (pad ms + wpm), MCM sliders | swf + Papyrus + MCM   | deferred (scoped, see README)  |
| v3     | Cut in-flight voice audio: player line on skip + NPC line on new-topic | SKSE C++ (`plugins/`) | deferred (swf can't reach it)  |

This doc fully specifies **v1**; v2/v3 are roadmap only (tracked in `docs/ideas.md`).

## v1 — manual player-line skip

### Goal

Make the player's DBVO voiced line behave like vanilla dialogue: pressing **E or left-click**
while the line is playing **immediately advances** the dialogue (NPC reply fires / barter menu
opens) instead of forcing the user to wait out DBVO's fixed timer. Repetitive service lines
("let's trade some items") stop being a forced pause.

**v1 does not silence the audio.** Console `Player.SpeakSound` (how DBVO plays the line — confirmed
in `DBVO_Script_MCM.pex`: `RegisterForModEvent "PlayDBVOTopic"` → `ExecuteCommand`) returns no sound
handle, so Papyrus/swf cannot stop the instance. Skipping early therefore lets the line's tail
**overlap** the NPC reply briefly. Clean cut is v3 (SKSE). This overlap is the accepted v1 tradeoff.

### Baseline

Live game runs **stock DBVO** `dialoguemenu.swf` — md5 `b1f70c5806ad94359bb0d780a9069d34`,
1400 ms pad (reference copy: `~/Downloads/skyrim-mods/00-docs/overrides/2026-06-09-DBVO-instant-skip/`).
v1 rebuilds from this stock swf. (The `+900` custom build, md5 `7227f30e…`, is a separate experiment;
not the v1 base.)

### The mechanism today

In `DialogueMenu.as` (decompiled AS2), clicking a topic runs:

```
onSelectionClick → eMenuState = TOPIC_CLICKED   // only if prior state was TOPIC_LIST_SHOWN
                 → timerBool = true
                 → initDBVO()                   // SendModEvent "PlayDBVOTopic" → DBVO Papyrus plays the line
... DBVO Papyrus resolves the voice pack, then calls back into the swf ...
startTopicClickedTimer(voicePackID)             // this.timer = setTimeout(this,"topicClicked", delay)
... delay elapses ...
topicClicked()   → GameDelegate.call("TopicClicked", [topicIndex])  // NPC replies / menu advances
```

Note the two entry points: `onSelectionClick` only calls `initDBVO()` (fires the `PlayDBVOTopic` mod
event). `startTopicClickedTimer(voicePackID)` is a **separate** call invoked by DBVO's Papyrus
round-trip — that's where `this.timer` is actually armed, so the debounce timestamp belongs there, not
in `onSelectionClick`.

During the `delay` window the menu is in `TOPIC_CLICKED`. Input is **ignored**: clicks reach
`onMouseDown → onItemSelect`, but `onItemSelect` only acts when `bAllowProgress` is true, and nothing
sets it true during this window. So the user is forced to wait. (`bAllowProgress` + `SkipText` is the
_separate_ vanilla mechanic for skipping the **NPC's** spoken line; v1 leaves it untouched.)

### The change

While a player line is pending (`eMenuState == TOPIC_CLICKED` **and** `this.timer` is set), route
**E and left-click** to skip it:

```
clearTimeout(this.timer);
this.topicClicked();          // fire the reply now
```

Details:

- **Debounce (~250 ms).** The same click/keypress that _selected_ the topic must not instantly
  self-skip it. Record a timestamp when `startTopicClickedTimer` arms the timer; ignore skip input
  until ~250 ms later. (Mirrors the existing `ALLOW_PROGRESS_DELAY = 750` pattern; 250 ms is enough
  to clear the selecting input without feeling laggy — tune in-game.)
- **Left-click** already arrives via `onMouseDown → onItemSelect`; add the skip branch there, gated on
  `TOPIC_CLICKED` + pending timer + debounce.
- **E / activate** keyboard routing during `TOPIC_CLICKED` is **unverified** — the list may stop
  forwarding `itemPress` once a topic is clicked. Prototype step confirms whether E reaches the menu;
  if not, add an activate-key branch in `handleInput`. **Known risk:** if E genuinely can't be routed
  from the swf in this state, v1 ships left-click-only and we document it (E-skip then becomes a v3
  SKSE input hook).
- **Always on, no MCM.** Like vanilla skip, it's just available. Keeps v1 swf-only (no Papyrus).

### Build & test

1. Decompile stock swf with **ffdec** (`-export script`), edit `DialogueMenu.as`, recompile
   (`-importScript`). Same toolchain v2 needs.
2. Install the rebuilt swf into an **isolated skytest profile** (DBVO + Karat), backing up the live
   stock swf to `00-docs/overrides/` first.
3. In-game: open a merchant, click the barter topic, press E (and separately, left-click) mid-line →
   barter menu opens immediately. Verify: (a) no double-fire / no skipping past the intended topic,
   (b) the selecting click does **not** self-skip (debounce holds), (c) overlap is only the audio tail,
   not a logic break.

### Done when

Reproduced in-game: barter line skippable by left-click (and E if routable), debounce prevents
self-skip, dialogue logic intact. Overlap audio is expected and noted, not a failure.

## v2 / v3 — deferred (roadmap)

- **v2 — configurable gap + speed + MCM.** Parameterize `startTopicClickedTimer`'s `wordCount × 200 ms
(300 wpm) + 1400 ms pad`: expose `padMs` and `wpm` via MCM sliders, transported swf↔Papyrus. Full
  scope in `mods/DBVODialogueTweaks/README.md`. Shares the v1 swf rebuild.
- **v3 — audio cut (SKSE).** SKSE C++ plugin (sibling to `plugins/`). Two related in-flight-audio
  problems the swf provably cannot reach, both needing the audio/voice layer:
  - **(a) player-line tail on skip** — hook/track the player's `SpeakSound` instance and stop it when
    v1's skip fires, removing the overlap.
  - **(b) NPC line not interrupted when selecting a new topic** (investigated 2026-06-10, in-game).
    Symptom: while an NPC reply is playing, selecting a new topic makes the player's new line play
    *over* the still-running NPC voice (vanilla would cut the NPC line and start the new exchange).
    **Root cause:** DBVO defers the engine's `TopicClicked` behind its timer, so the engine is never
    told to advance/cut the current NPC line at selection time; the player line (`SpeakSound`) fires
    immediately → overlap. **Why it's not swf-fixable:** the swf has only two line-control levers, and
    both fail — `GameDelegate.call("SkipText")` does **not** stop the NPC voice audio in this AE setup
    (confirmed: the normal click-skip advances the menu/subtitle but the NPC voice keeps playing), and
    `TopicClicked` would cut the NPC line only by *immediately* starting the next NPC reply, overlapping
    the player line and breaking DBVO's timing. A `SkipText`-on-select swf attempt was built and tested
    → no effect (reverted). So interrupting the NPC voice needs the SKSE audio layer, same tier as (a).
  - Possibly also the README's exact-`.fuz`-duration NPC-reply scheduling (eliminates the wpm guess).

## Why this shape

- **swf-only v1, audio cut last.** The user explicitly wants vanilla-style skip now and accepts the
  audio cut as a later, more complex addition. swf-only keeps v1 a single self-contained edit with no
  Papyrus/MCM surface, shippable independently of v2.
- **Reuse the existing timer + input plumbing** rather than a new SKSE input layer. `this.timer` and
  `onMouseDown`/`handleInput` already exist; v1 just unblocks input that's currently gated off. An
  SKSE approach was considered and pushed to v3 because it's where the audio-cut has to live anyway —
  no reason to pull input hooking forward when the swf already receives the clicks.
- **No MCM in v1** (YAGNI): vanilla skip isn't configurable either; a toggle adds a Papyrus/MCM
  dependency v1 otherwise doesn't need.
