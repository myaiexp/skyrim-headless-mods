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

## Addendum — 2026-09-02: DBVO 2 is the page's main download now

Re-checked against the Nexus API (mod 84329, owner key):

- The page is titled **"Dragonborn Voice Over 2"**. `2.0.1.6` (file `797830`, 2026-08-30, 2.44 MB) is
  the **MAIN** file; `2.0.1.5` (`772544`) and **DBVO 1.1.1 (`416153`)** are both `OLD_VERSION`.
- **Nothing newer than 2.0.1.6 has shipped**, so §1–§4 above still describe the current release —
  the "release" event is the page flip, not a new build.

Consequence for this mod: a new user who follows the plain
`nexusmods.com/skyrimspecialedition/mods/84329` link now lands on DBVO 2 by default and walks
straight into the §3 softlock. The README and the Nexus page copy (`docs/dbvo-page.md`) were
repointed at the OLD FILES entry for 1.1.1 the same day; **the live Nexus page still needs the same
edit by hand** (website-only, no write API).

### In-engine result — 2026-09-02: the softlock is confirmed, and DBVO 2 flags it itself

Both blockers cleared the same day (the repo moved to the 1.7.104 stack; the archive was
downloaded), so the §3 prediction was tested as a clean A/B. Setup: game **1.7.104.0**, SKSE 2.3.1,
**DBVO 2.0.1.6**, SKSE Menu Framework 3.14.1, the Karat legacy pack with
`use_legacy_voice_over:true` / `legacy_pack:"Danagis_KaratVoice"` /
`always_use_default_options:true`. Isolated `skytest` profiles, headless, base save in `QASmoke`,
Hulda (base `0x00013BA3`) spawned 110u ahead with `placeatme`, same topic clicked both times
("Heard any rumors lately?"), same input (`drive seq down enter`).

| Profile | Result |
| --- | --- |
| **A — DBVO 2 alone** | Works. Within ~5 s the topic list collapses to the clicked topic **and the NPC delivers her reply** (subtitle "Have you seen that Shrine of Azura? …", mouth animating). |
| **B — DBVO 2 + DBVO Dialogue Tweaks 1.0.0** | **Softlocked.** The topic list collapses to the clicked topic and then *nothing*: no reply, no subtitle, mouth closed, still stuck **26 s** later. Clicking again is a no-op. |

Exactly the §3 mechanism: the menu reaches `TOPIC_CLICKED` and never calls
`GameDelegate.call("TopicClicked")`, because `this.timer` is only armed by
`startTopicClickedTimer`, which DBVO 1.0's Papyrus called and DBVO 2 has no Papyrus to call.

**Two refinements to what the page/README should say:**

- It is a **dialogue** softlock, not a game hang. **Tab exits** the menu cleanly and the player is
  fully functional afterwards — you lose the conversation, not the session. Worth saying, because
  "softlock" alone reads like "reload your save".
- **DBVO 2 detects the conflict and says so itself**, in `DBVO.log` at load:
  `[W] [Conflict] A DBVO 1.0 patched dialoguemenu.swf is installed. Replace it with an unpatched one
  for DBVO 2.0 to work properly.` First-party confirmation, and a much better thing to quote at a
  user than our own reverse-engineering.

#### Measured: DBVO 2 does not shorten the player's line when you press skip

The claim needed an instrument, so one was built: SkytestProbe's **`speak-watch`** — a read-only
MinHook detour on `Actor::SpeakSoundFunction` (Address Library 36541/37542) that copies the
`BSSoundHandle` by value and samples it, never touching it. It confirms §1 empirically first: DBVO 2
really does play the player's line through that native, with the path
`DBVO/Danagis_KaratVoice/<Sanitized_Prompt>.fuz` — so the legacy-pack resolution and the `sanitized`
key strategy are now observed, not inferred.

Then the experiment, Profile A (DBVO 2 alone), same NPC, `speak-watch` armed, `elapsedMs` from the
`stopped` line (`why:"handle-released"` every time — the engine releases the handle at the natural
end of the line):

| Prompt | Activate pressed ~600 ms in? | Line ran for |
| --- | --- | --- |
| `Heard_any_rumors_lately_` | no | **1649 ms** |
| `Heard_any_rumors_lately_` | no | **1850 ms** |
| `Where_can_I_learn_more_about_magic_` | **yes** | **1564 ms** |
| `Where_can_I_learn_more_about_magic_` | **yes** | **1617 ms** |

Pressing the skip input a third of the way into the line **did not truncate it**: every run played
to its natural length (spread is ±one 250 ms sampling tick). If DBVO 2 cut the audio we would see
~600–900 ms. This is the positive evidence §4 predicted from the *absence* of an audio path in
`FireResponse` — the mod's one surviving differentiator is real.

**The gap that remains is narrow and named:** these runs do not independently prove
`SkipInputHandler` *fired* on the synthetic Activate, so "skip fires but cannot cut the audio" and
"skip never fired" both fit the numbers. Closing it needs DBVO 2's own `[FireResponse]` /
`[delay]` lines, which sit behind an **"Enable DBVO logging"** toggle that is **off by default and
exists only in its ImGui menu** (`SKSEMenuFramework`, F1) — the option is not a key in
`default_options.json`, so it cannot be pre-set from a file. Drive that toggle on (finding #25:
coordinate `drive click` works), re-run the table above, and the answer is complete.

## Open items

- ~~**Not tested in-engine.**~~ **Done 2026-09-02** — the §3 softlock is confirmed by A/B on game
  1.7.104 with DBVO 2.0.1.6, and DBVO 2 logs its own `[Conflict]` warning about the patched swf.
  §1's player-voice path is confirmed too (`speak-watch` sees
  `DBVO/Danagis_KaratVoice/<Sanitized_Prompt>.fuz`). §2 and §4 remain binary evidence.
- **"Player voice keeps playing on skip"** — **measured, one link short.** Pressing Activate ~600 ms
  into the line never truncated it: it ran its full ~1.6 s every time (table above). What is still
  unproven is that `SkipInputHandler` *fired* on the synthetic input, which needs DBVO 2's debug log
  (ImGui toggle, off by default). Do not cite it to MathiewMay as observed until that is closed.
- **`word-count-estimate` fallback frequency** is unknown — if `.fuz` duration reads fail often for
  some pack layout, DBVO 2's pacing would regress toward DBVO 1.0's for those lines.
- The two 5-byte globals at `0x1802037c0` / `0x1802037c8` (BSS, runtime-initialized) feeding the
  entry gate and the skip token were not identified; they do not affect any conclusion here.
