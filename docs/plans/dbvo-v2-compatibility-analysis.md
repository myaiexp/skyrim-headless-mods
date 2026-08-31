# DBVO 2 vs. DBVO Dialogue Tweaks — compatibility + relevance analysis

**Date:** 2026-08-31 · **Verdict:** the mod is superseded by DBVO 2 **and actively breaks it**.
Retire it for DBVO 2 users; one feature (clean audio cut on skip) survives as a possible narrow
successor or upstream contribution.

Method: static analysis of the two shipped DBVO 2 builds (2.0.1.5, 2.0.1.6) — file manifest,
`default_options.json`, string tables, RTTI walk, and targeted x86-64 disassembly — cross-read
against this mod's own `src/__Packages/DialogueMenu.as` and `plugin/src/main.cpp`.
**Not verified in-engine** (see [Open items](#open-items)).

## 1. What DBVO 2 actually is

A complete native rewrite. The 2.0.1.6 archive ships exactly two things:

```
SKSE/Plugins/DBVO.dll              (2.19 MB, CommonLibSSE-NG 6.7.1 static)
SKSE/Plugins/DBVO2/                default_options.json + locale_packs/{de,es,fr,it,ja,pl,ru,zh}
```

No `.esp`. No Papyrus scripts. **No `Interface/dialoguemenu.swf`.** Requirements dropped from
DBVO 1.0's PapyrusUtil + ConsoleUtilSSE NG to just SKSE + SKSE Menu Framework + Address Library.

The whole SKSE side is three translation units (embedded source paths):
`SKSE/src/DialogueListener.cpp`, `SKSE/src/Menu.cpp`, `SKSE/src/main.cpp`.

### Why it needs no swf patch

`dbvo::InstallDialogueListener()` — logged as *"Dialogue listener installed (Accept/TopicClicked
hooked)"* — installs a `TopicClickedProcessor : RE::FxDelegateHandler::CallbackProcessor`. It
intercepts the **`TopicClicked` and `SkipText` FxDelegate callbacks natively**, on the receiving
side of the GFx→game boundary. DBVO 1.0 achieved the same interception by *editing the swf*; DBVO 2
does it from the DLL, which is why its page advertises UI-mod compatibility.

### Per-click flow

1. `DeferredTopicClicked(const RE::FxDelegateArgs&)` — resolve the prompt text to a voice-file key.
   Logs `[resolve] mode={} pack="{}"`, `[resolve] keySource={} "{}" (hasAlias={}, aliasParsing={})`,
   with `sanitized` / `hashed` key strategies and a locale/STRINGS translation layer.
2. Build the `.fuz` path — `Sound/DBVO/{}/{}`, `DBVO/{}/{}/{}`, `DBVO/{}/{}.fuz`, with
   `[fuz] vfs-variant (random)` / `[fuz] folder-variant (random)` for multi-take lines. On a miss:
   `[fuz] not found "{}"; passing click through (no player voice)`.
3. Speak it — `[speak] Player.SpeakSound "{}"`.
4. **Compute the reply delay** — `[delay] {}ms = {} {}ms + offset {}ms`, where the `{}` source tag is
   one of two adjacent literals in the binary:

   ```
   fuz-duration
   word-count-estimate
   ```

5. Schedule `FireResponse(token, /*skipped=*/false)` → advances the dialogue, driving
   `_root.DialogueMenu_mc.ShowDialogueText` / `HideDialogueText` / `SubtitleText`.

**Step 4 is the headline.** The base delay is the *real audio length read from the `.fuz`*, with the
word-count guess demoted to a fallback. That is this mod's reason for existing, now upstream.

## 2. Answering the three questions this analysis started from

### "Are DBVO 1.0 voice packs unsupported in v2?"

They are supported. `use_legacy_voice_over` is a **resolution-mode** switch inside one code path, not
a fork in the plumbing:

- legacy packs → `Data/DragonbornVoiceOver/voice_packs` (string:
  *"No legacy voice packs found in Data/DragonbornVoiceOver/voice_packs"*)
- 2.0 packs → `Sound/DBVO/…`

Both converge on the same `[delay]` / `FireResponse` code. The two saved slots — `legacy_pack` and
`current_pack` — are why the toggle carries its own dropdown: it remembers a selection per format,
not because the pipelines differ. (The pack-format docs also advise turning off alias parsing for
legacy packs, matching `resolve_alias_dialogue` / `disable_alias_parsing`.)

### "Why does the page talk about padding, not pacing to the line?"

Because the pacing isn't a setting — it's the default, so there's no knob to describe.
`npc_response_delay` **defaults to `0`** and is only the `+ offset {}ms` term added on top of the
`fuz-duration` base. It occupies exactly the role of this mod's *"Gap after your line ends"* slider.
The page documents the knob; the mechanism sits underneath it, undescribed.

### "Do we need two patches, legacy and 2.0?"

No — one code path serves both modes. Moot anyway, per §3.

## 3. The blocking finding: this mod softlocks dialogue under DBVO 2

Installing DBVO Dialogue Tweaks over DBVO 2 **hangs the conversation**. This follows directly from
`src/__Packages/DialogueMenu.as`:

- `onSelectionClick()` sets `timerBool = true`, moves to `TOPIC_CLICKED`, plays the click animation,
  fires the `CutNpcDBVOReply` and `PlayDBVOTopic` mod events — and **never calls
  `GameDelegate.call("TopicClicked", …)`**.
- Every path that *does* call it (`topicClicked()` via the timer, `dbvoOnPlayerLineEnded()`,
  `trySkipPlayerLine()`) is gated on `this.timer != undefined`.
- `this.timer` is set **only** in `startTopicClickedTimer(voicePackID)` — which nothing in this repo
  calls. It was invoked by **DBVO 1.0's Papyrus**, which DBVO 2 does not have.

So under DBVO 2: click a topic → the UI animates into the clicked state → no voice plays → the
delegate is never called → the conversation never advances. `DoShowDialogueList` is additionally
gated on `timerBool == false`, so the topic list won't refresh either.

Secondary harm: the bundled swf is DBVO 1.0's lineage and would overwrite any UI mod's
`dialoguemenu.swf`, undoing one of DBVO 2's stated improvements.

The DLL alone is comparatively benign — its `Actor::SpeakSoundFunction` hook still fires, since it
detours the *engine* function rather than a DBVO call site, and DBVO 2's paths still begin `DBVO/`
so `is_dbvo_path()` still matches. But its volume scale would then compound with DBVO 2's own
`dialogue_volume`, and `dbvoOnPlayerLineEnded` no longer exists on the unpatched swf.

## 4. Feature-by-feature

| This mod | DBVO 2 status |
| --- | --- |
| **Reply on line-end** | **Absorbed.** `fuz-duration` base delay; `word-count-estimate` is now only the fallback. Theirs is precomputed from the file, mine was live end-detection off the sound handle. |
| **Configurable gap** | **Absorbed.** `npc_response_delay`, default `0`, added as the offset term. |
| **Player-voice volume** | **Absorbed and exceeded.** `dialogue_volume` (0–100) plus `dialogue_reverb` (default 80), which this mod never had. |
| **Manual skip (E / click)** | **Absorbed in 2.0.1.6** — see below. |
| **Clean audio cut on skip / interrupt** | **Still missing.** The only surviving differentiator. |

### Skip landed in 2.0.1.6 (2026-08-30)

2.0.1.5 → 2.0.1.6 adds three classes — `SkipInputHandler : BSTEventSink<InputEvent*>`,
`DialogueCloseHandler : BSTEventSink<MenuOpenCloseEvent>`, and Papyrus VM callback machinery
(`IVirtualMachine::Awaitable::CallbackFunctor`, `IStackCallbackFunctor`). The `[…]` log-tag set is
otherwise byte-identical between the builds, so the core dialogue logic is unchanged; the rest of the
size growth is CommonLibSSE-NG 6.7.1 being statically linked.

Disassembling `SkipInputHandler::ProcessEvent` (vtable slot 1 at `0x18019f0a8` → `0x1801212d0`):

1. Early-out unless a gate passes, then a `std::chrono` elapsed check against a stored timestamp:
   `cmp rax, 0x11e1a300` → **300 ms debounce** (the same role as this mod's `SKIP_DEBOUNCE_MS`).
2. Walk the input event list, take `AsButtonEvent()`, require `value > 0`, and match against the
   **Activate** user event, **mouse device 1 / idCode 0** (left click), or **gamepad device 2 /
   idCode 0x1000** (A).
3. On a match: `CreateThread` → proc at `0x180121a20` → `sleep_for(100ms)` →
   `SKSE::TaskInterface::AddTask` → the functor at `0x180121b00` tail-calls
   **`FireResponse(token, /*skipped=*/true)`** (`0x1801209d0`, confirmed by its
   `[FireResponse] …` string references).

So DBVO 2.0.1.6 implements skip on the same inputs this mod used, one day before this analysis.

### Why the audio cut is the survivor

**DBVO 2 never holds a sound handle.** Three independent signals:

- No sound-related RTTI in either build — no `BSSoundHandle`, `BSAudioManager`, `BSISoundHandle`.
- Neither build references the `Actor::SpeakSoundFunction` Address Library ID
  (**36541** SE / **37542** AE) — a byte scan for both little-endian IDs returns zero hits in each.
- `FireResponse` makes no audio call; it only advances dialogue and drives the GFx text.

It fires the line and forgets it. That is also the likely reason `SkipText` was *swallowed*
(`[SkipText] swallowed during player voice window`) until 2.0.1.6 could route skip through
`FireResponse` instead. The consequence: on a DBVO 2 skip the player's line has nothing stopping it,
so it should keep playing over the NPC's reply.

This mod hooks that exact engine native and retains the `BSSoundHandle` (`g_playerLine`), which is
what makes a clean fade-out possible at all.

## 5. Recommendation

1. **Retire the mod for DBVO 2 and publish the incompatibility.** Non-optional: the mod is live on
   Nexus (182628) and anyone who upgrades DBVO hits the §3 softlock with no obvious cause. A page
   notice plus the README warning (landed with this analysis) is the minimum.
2. **Offer the fade-on-skip upstream.** Now that MathiewMay has the input handler and the
   `FireResponse(…, skipped=true)` path, cutting the player's line is a small addition — it needs
   only the handle that hooking `Actor::SpeakSoundFunction` (36541/37542) yields. This mod already
   has that code, and there is an existing permission relationship with the author.
3. **Only if upstreaming stalls: a DLL-only successor.** Drop the swf, `.esp`, MCM and Papyrus
   entirely; keep the SpeakSound hook purely to hold the handle and fade it when DBVO 2's skip
   fires. Risk to weigh first: both plugins would sink the same input, so the two skips race, and
   the fade must not double-fire the response. That is real work for one polish detail the author
   can now add in a few lines — hence third.

## Open items

- **Not tested in-engine.** Everything above is static. The §3 softlock follows from AS2 control
  flow and is solid; the rest is binary evidence. A `skytest` pass over DBVO 2 would confirm.
- **"Player voice keeps playing on skip"** is inferred from the *absence* of any audio-stop path in
  `FireResponse`, not observed. Worth one in-engine check before citing it to the author.
- **`word-count-estimate` fallback frequency** is unknown — if `.fuz` duration reads fail often for
  some pack layout, DBVO 2's pacing would regress toward DBVO 1.0's for those lines.
- The two 5-byte globals at `0x1802037c0` / `0x1802037c8` (BSS, runtime-initialized) feeding the
  entry gate and the skip token were not identified; they do not affect any conclusion here.
