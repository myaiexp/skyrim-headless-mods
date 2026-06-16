## DBVO Dialogue Tweaks

The NPC's reply lands when your voiced line actually ends. A small SKSE plugin watches your line's audio and fires the reply the moment it truly stops. You can also skip your own line, set the volume of your own voice, and tune the gap before the reply.

*Pacing and control tweaks for [Dragonborn Voice Over (DBVO)](https://www.nexusmods.com/skyrimspecialedition/mods/84329).*

### Features

- **Reply on line-end** — the NPC answers right after your line truly finishes, plus a small configurable gap, on every line, whatever the voice pack's speed.
- **Manual skip** — press E / left-click to skip your own voiced line and move on, vanilla-dialogue style.
- **Clean cut on skip and interrupt** — skipping fades your in-flight line out without a click; picking a new topic while an NPC is mid-reply cuts that reply too.
- **Player-voice volume** — attenuate just your own DBVO line, 0–100%, without touching other audio.
- **Configurable gap** — the pause after your line ends before the NPC answers, 0–1000 ms (0 = instant).
- **Native SkyUI MCM** — one screen, no MCM Helper dependency.

### Requirements

- Skyrim Special Edition or Anniversary Edition + SKSE
- [Dragonborn Voice Over](https://www.nexusmods.com/skyrimspecialedition/mods/84329) and everything it requires (PapyrusUtil, ConsoleUtilSSE NG)
- SkyUI (for the MCM)
- Address Library for SKSE Plugins

### Compatibility

- **SE and AE — one DLL for both.** Built on CommonLibSSE-NG and reaches the engine through the Address Library (addresses resolved at runtime), so the same file runs on every SE and AE build, Steam or GOG, as long as Address Library is installed. Tested on AE/Steam; SE (1.5.97) and GOG run the same build and should work, but are untested — please report if you hit anything.
- **VR — no.** Skyrim VR uses a different dialogue UI and would need a separate build; it isn't provided.
- It overwrites only DBVO's `Interface/dialoguemenu.swf` — let it win that conflict (install it after DBVO) — and never touches DBVO's own scripts. The `.esp` is ESL-flagged and takes no load-order slot.

### Configuration

- **Gap after your line ends (0–1000 ms)** — pause between your line ending and the NPC's reply. 0 = instant.
- **Player voice volume (0–100%)** — volume of your own DBVO voice line only. 100 = unchanged.

### How it works

<details>
<summary>Show</summary>

**The starting point.** DBVO ships no SKSE plugin of its own — it's built entirely from Papyrus, ConsoleUtil, and an edited dialogue menu. Your voiced line is played through ConsoleUtil's `Player.SpeakSound "DBVO/<file>.fuz"`, which starts the sound and returns nothing — there's no "this line finished" callback to wait on. So the NPC's reply is gated by a plain timer inside `dialoguemenu.swf`, started when you pick a topic:

```js
// DBVO's reply timing, in dialoguemenu.swf
words = topicText.split(" (")[0].split(" ").length;  // word count, minus "(Persuade)" etc.
delay = round(words * 200) + 1400;                  // ~300 wpm guess + a flat 1400 ms pad
setTimeout("topicClicked", delay);                  // → NPC replies after `delay`
```

The reply lands at a time guessed from word count. It can't know your voice pack's actual speed, so it's wrong in both directions: a flat 1400 ms pad on top, and 200 ms/word that fast AI packs beat easily. The fix needs to know when the audio actually ends — which means reaching the sound itself, which means a DLL.

**The hook.** This mod is a small SKSE plugin (CommonLibSSE-NG). At load it uses MinHook to detour the engine's speak-sound function — the same one ConsoleUtil's `Player.SpeakSound` dispatches into. It's addressed only by Address Library ID (SE 36541 / AE 37542), which is why one DLL covers both SE and AE: the right address is resolved at runtime. The detour calls the original function first, so the engine builds and starts the sound handle normally (lip-sync intact), then inspects the call. Only if the speaker is the player and the path begins with `"DBVO/"` does it act — every other sound in the game passes straight through, untouched. For a matching line it does three things: scales the line's volume to the MCM slider, keeps a copy of the sound handle so it can be cut later, and arms the reply watcher.

*(The handle copy is safe to keep: a BSSoundHandle is a tiny value keyed by a sound ID, and every operation re-resolves the live sound through the audio manager by that ID — so a copy made when the line started still controls it moments later. Each new line overwrites the slot, so only the current line is ever cuttable, which is exactly the right scope.)*

**Reply on line-end.** One background thread starts when the game loads and runs for the whole session. It sleeps ~30 ms per tick — idle it's a near-no-op, never pinning a core. While a line is armed it polls the retained handle: still playing, or stopped? The check runs off the main thread (the audio-manager calls are safe there), and a latch makes sure the thread has actually seen the line playing before it's allowed to detect an end — so the brief window between arming and the audio starting can't be misread as "already finished." The moment it sees playing → stopped, it claims the event exactly once and hands a single task to the main thread. That last hop matters: Scaleform (the Flash UI layer) isn't thread-safe, so the swf can only be touched on the main thread. The task calls one new swf function, `dbvoOnPlayerLineEnded`, which cancels the word-count timer and reschedules the reply after your configured gap. Net result: the reply is driven by the real end of the audio, plus a gap you choose, on every line.

*(Why a dedicated thread and not the engine's task queue? An earlier version re-scheduled itself on the main thread's task queue each tick. That froze the game solid: SKSE drains that queue to empty every frame, so a task that re-queues itself spins the frame forever. Polling on its own thread with a real sleep is what fixed it.)*

**The backstop.** The swf still keeps a word-count timer — but deliberately a long one, and only as a fallback:

```js
// swf backstop — only fires if the DLL never reports the end
delay = round(words * 300) + 2000;  // generous on purpose: must outlast any real line
```

When the DLL is present and working, `dbvoOnPlayerLineEnded` clears this long before it expires, so you never see it. It exists so the mod degrades gracefully: without the DLL — or on a line whose end somehow isn't detected — dialogue still advances, just on the old (now very forgiving) guess instead of hanging forever.

**The gap.** `dbvoOnPlayerLineEnded` waits the MCM's "gap" value (0–1000 ms, default 250) before firing the reply, so you can dial the pacing from instant to a clear beat between lines.

**Player-voice volume.** Same hook, no extra machinery: it scales the line's handle to the slider. The value is pushed down from the MCM through a tiny Papyrus native and read in the hook; at 100% the hook is a pure pass-through, so leaving it default changes nothing. It touches only your own DBVO line — every other sound in the game keeps its own volume.

**Skip and interrupt — clean cuts.** The swf raises a mod event in two cases and the DLL turns each into a clean stop:

- **You skip your own line (E / left-click).** The menu advances immediately and the DLL fades the retained player handle out over a few milliseconds rather than hard-stopping it — a hard stop mid-waveform clicks audibly; a short fade doesn't. It also stands the reply watcher down, so the fade's own playing → stopped transition can't fire a second reply. (A short debounce stops the same click that picked the topic from instantly skipping it.)
- **You pick a new topic while the NPC is mid-reply.** This one's fiddlier: the NPC's topic voice doesn't live where you'd expect, and the engine's "pause dialogue" only pauses — a multi-segment reply keeps going. So the DLL does both: it fades the currently-playing segment to silence and tells the engine to stop advancing to the next segment. Cutting the audio also leaves the NPC's mouth frozen open mid-word, so it snaps the face back to neutral — clearing the lip-sync keyframes under the same lock the engine's lip-sync writer uses, so the reset actually sticks instead of being overwritten next frame. (This is the same full-face-revert other mods like PhotoMode use.)

**Tidying up.** When the dialogue menu closes, the watcher is disarmed, so a half-finished line can't leak its reply into your next conversation. And firing a reply into a closed menu is a no-op anyway — the call simply finds no menu and does nothing.

</details>

### Source

Full source is on GitHub, MIT-licensed: [myaiexp/skyrim-headless-mods](https://github.com/myaiexp/skyrim-headless-mods) (this mod lives under `mods/DBVODialogueTweaks`).

It's all built headlessly on Linux — no Creation Kit, no SSEEdit, no GUI tooling. The DLL is cross-compiled against CommonLibSSE-NG, the swf is recompiled with ffdec, the Papyrus scripts compile from the command line, and the `.esp` is generated with Mutagen; a single build script produces every file the mod ships.

### Permissions and credits

Built on **[Dragonborn Voice Over](https://www.nexusmods.com/skyrimspecialedition/mods/84329)** by MathiewMay — permission received from the author. The bundled `dialoguemenu.swf` is DBVO's asset recompiled with these tweaks. All credit for DBVO goes to MathiewMay.
