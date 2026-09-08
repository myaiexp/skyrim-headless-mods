# DBVO Dialogue Tweaks

> **For DBVO 1.x only — superseded by DBVO 2.** This mod patches the `dialoguemenu.swf` + Papyrus
> stack that Dragonborn Voice Over shipped through 1.1.1. DBVO 2 (2026) replaced that stack with a
> single DLL, absorbed nearly every feature below, and **softlocks dialogue if you install this on
> top of it** — see [Compatibility](#compatibility). The one thing DBVO 2 still does not do is cut
> your line's audio when you skip it.

Pacing and control tweaks for **[Dragonborn Voice Over (DBVO)](https://www.nexusmods.com/skyrimspecialedition/mods/84329) 1.x**:
makes the NPC reply land **when your voiced line actually ends** instead of after DBVO's fixed
time-guess, lets you **skip** your own line, and adds a **player-voice volume** slider. Everything is
configurable from a native SkyUI MCM.

DBVO 1.x times the NPC's reply by _estimating_ your line's length from its word count. Fast voice
packs (Karat and other AI packs) finish well before that estimate, so every line ends in dead air.
Or, over-corrected, the NPC talks over you. This mod replaces the guess with real end-detection: a
lightweight SKSE plugin watches your line and cues the reply the moment it stops.

## Features

- **Reply on line-end**: the NPC answers right after your line truly finishes (plus a small,
  configurable gap), on every line, whatever the voice pack's speed. No dead air, no overlap.
- **Manual skip**: press **E** / left-click to skip your own voiced line and move on immediately,
  vanilla-dialogue style.
- **Clean cut on skip & interrupt**: skipping fades your in-flight line out cleanly (no click);
  picking a new topic while an NPC is mid-reply cuts that reply too.
- **Player-voice volume**: _just_ your own DBVO line, **0–300%** (100% = as the pack was mastered),
  without touching any other audio. Below 100 attenuates; above 100 **amplifies** (since 1.1.0) — for
  a pack mastered far quieter than the NPCs, such as some vampire packs. Gain above unity can clip a
  pack that is already loud, so raise it only as far as it needs.
- **Configurable gap**: the pause after your line ends before the NPC answers, 0–1000 ms (0 = instant).
- **Native SkyUI MCM**: a single screen, no MCM Helper dependency.

## Requirements

- Skyrim Special Edition or Anniversary Edition + **SKSE**. On the **1.7.99 / 1.7.104** game builds
  you need **v1.0.1 of this mod or newer** — 1.0.0's DLL cannot read the format-5 Address Library
  those builds use and will not load. Match SKSE to your game (2.3.1 for 1.7.104).
- **DBVO 1.x** — [Dragonborn Voice Over 1.1.1](https://www.nexusmods.com/skyrimspecialedition/mods/84329?tab=files&file_id=416153),
  which now lives under the mod page's **OLD FILES** tab: the page's main download is DBVO 2, which
  this mod does not support. Plus what DBVO 1.x itself calls into — **ConsoleUtilSSE NG** (speaks
  your line) and **JContainers SE** (reads DBVO's voice-pack settings) — both current for your game
  build. On 1.7.104 the older builds of those two are refused outright; see
  [Compatibility](#compatibility).
- **SkyUI** (for the MCM)
- **Address Library for SKSE Plugins** — v13 ("All in One (1.7.104.0)") on the new builds.

## Compatibility

> **⚠ DBVO 2 (Dragonborn Voice Over 2) — do NOT install this mod.** DBVO 2 is now the main download
> on mod page 84329 (2.0.1.6, 2026-08-30; the page is titled "Dragonborn Voice Over 2" and 1.1.1 is
> demoted to OLD FILES). It is a native rewrite that ships no `dialoguemenu.swf` and no Papyrus, and
> the swf here depends on DBVO 1.0's Papyrus calling `startTopicClickedTimer` to arm the reply. Under
> DBVO 2 that call never comes, so clicking a topic **softlocks the conversation**: the menu enters
> the clicked state and the NPC never replies. **Confirmed in-engine** (2026-09-02, game 1.7.104,
> DBVO 2.0.1.6) — the same NPC and topic that reply in ~5 s with DBVO 2 alone were still stuck 26 s
> after the click with this mod installed. Pressing **Tab** does escape the menu and the game keeps
> running, so you lose the conversation rather than the save. DBVO 2 also detects this itself and
> writes to its own log: *"[Conflict] A DBVO 1.0 patched dialoguemenu.swf is installed. Replace it
> with an unpatched one for DBVO 2.0 to work properly."*
>
> DBVO 2 also absorbs nearly everything this mod added: the reply is timed off the real `.fuz`
> duration, `npc_response_delay` replaces the gap slider, `dialogue_volume` (+ `dialogue_reverb`)
> replaces the volume slider, and 2.0.1.6 added manual skip. Only the clean audio cut on skip is
> still missing there. Full analysis:
> [`docs/plans/dbvo-v2-compatibility-analysis.md`](../../docs/plans/dbvo-v2-compatibility-analysis.md).

- **SE + AE, yes, one DLL for both.** The plugin is built on CommonLibSSE-NG and reaches the engine
  purely through the Address Library (the SE/AE addresses are resolved at runtime), so the same file
  runs on every SE and AE build (Steam or GOG) as long as Address Library is installed. The Papyrus
  scripts, the `.esp`, and the recompiled swf are all shared across SE and AE.
- **Skyrim 1.7.104, yes — from v1.0.1, and the reply timing is verified there.** Tested in-engine
  on game 1.7.104 with SKSE 2.3.1 and Address Library v13 (2026-09-03). The plugin loads, installs
  its speak-sound hook, registers its Papyrus native and both event sinks — and the feature this
  mod exists for demonstrably still works on the new build: with a player line playing, the plugin
  detected the line's end, cleared the menu's word-count backstop, and re-fired the NPC's reply
  after the configured gap. A/B against the *same* profile with only `DBVODialogueTweaks.dll`
  removed, where the reply instead landed on the word-count guess:

  | run | reply arrived |
  | --- | --- |
  | with the DLL | at the line's real end **+ the configured gap** (15 s gap → 20.3 s after the line started; predicted 20.0 s) |
  | without it | at the swf backstop, 4.1 s after the timer was armed — the gap ignored |

  Replayable as `replyonlineend.steps` (see [Testing](#testing) below). The 1.1.0 **volume boost** is
  verified the same way (2026-09-08, `voiceboost.steps`): at 250% the plugin's log shows the XAudio2
  voice's gain read back at 2.5× the engine's value for the player's line; at 100% no such line is
  written; with the DLL removed the assertion fails. v1.0.0 does **not** load on
  1.7.99/1.7.104: those builds changed the Address Library database to format 5 and 1.0.0's
  CommonLibSSE-NG predates that, so it aborts with *"Unsupported address library format: 5"*. The
  1.7.104 build still targets SE + AE the same way, but only 1.7.104 has been re-tested since the
  rebuild.

- **DBVO 1.x runs on 1.7.104 again — update its two dependencies, and the whole chain works.**
  **Verified end-to-end in-engine on 2026-09-09** (game 1.7.104, SKSE 2.3.1) with
  **ConsoleUtilSSE NG 1.6.1** and **JContainers SE 4.3.2**: both load (`plugin ConsoleUtilSSE.dll
  (… 01060010) loaded correctly`, `plugin JContainers64.dll … loaded correctly`), and clicking a
  topic drove the *entire* real chain — DBVO's Papyrus took the mod event, resolved the voice pack
  through JContainers, spoke the line through ConsoleUtil
  (`DBVO/Danagis_KaratVoice/Where_can_I_learn_more_about_magic_.fuz`, 1784 ms, natural end,
  observed by SkytestProbe's read-only `speak-watch`), armed this mod's menu via
  `UI.InvokeString(… startTopicClickedTimer …)`, and the reply landed at the line's real end plus
  the configured gap. A/B'd against the same stage minus only `DBVODialogueTweaks.dll`, where the
  reply instead fired on the word-count backstop. Replayable: `dbvo1x-endtoend.steps` +
  `dbvo1x-control.steps` (see [Testing](#testing)).

  Two gotchas that are worth knowing before you blame anything else: JContainers needs its whole
  `SKSE/Plugins/JCData/` folder (without `JCData/Domains/` it throws during registration and the
  game dies at boot, while `skse64.log` still says it "loaded correctly"), and SkyUI may greet you
  with *"SKYUI ERROR CODE 4 — Your Papyrus INI settings are invalid"* if your `Skyrim.ini` carries
  a `[Papyrus]` memory tweak — that modal swallows the activation key until you dismiss it.

  <details><summary>What was broken before those builds existed (checked 2026-09-03)</summary>

  SKSE 2.3.1 refused **both** of the SKSE plugins DBVO 1.x's `DBVO_Script_MCM` calls into, before
  they ever loaded, and put up its own modal saying so:

  ```
  ConsoleUtilSSE.dll: must be recompiled for new address library      (1.5.1.0)
  JContainers64.dll: disabled, incompatible with current version of the game
  ```

  ConsoleUtil is what speaks your line (`Player.SpeakSound "DBVO/…"`) and JContainers is what reads
  DBVO's voice-pack settings, so with those two refused DBVO 1.x produced no player voice at all —
  and this mod, which exists to time the reply to that voice, had nothing to time. The older builds
  are still refused: it is ConsoleUtilSSE NG **1.6.1**+ (2026-08-22) and JContainers SE **4.3.2**+
  (on Nexus since 2026-09-07; 4.2.13.1 is refused) that fix it. (Note it is JContainers, not
  PapyrusUtil, that DBVO 1.x's shipped script actually uses, whatever the mod page's requirement
  list says.)

  </details>
- **VR, no.** Skyrim VR uses a different dialogue UI (a different `dialoguemenu.swf`) and needs a
  separate VR build; neither is provided.
- **UI overhauls that replace the dialogue menu — pick your menu in the installer.** This mod ships
  the *whole* `dialoguemenu.swf`, so on a UI overhaul that carries its own DBVO-patched menu one swf
  has to lose, and installing this one used to revert the topic list to DBVO's stock placement —
  that is the "my dialogue options moved from the left to the right" report (Nexus, 2026-09-08).
  The FOMOD now asks which menu to install:

  | Menu style | For |
  | --- | --- |
  | **Stock DBVO menu** (default) | no dialogue-menu overhaul, or one without a DBVO patch |
  | **Untarnished UI** | [Untarnished UI](https://www.nexusmods.com/skyrimspecialedition/mods/75188) |
  | **Dear Diary Dark Mode (white)** / **(warm)** | [Dear Diary Dark Mode](https://www.nexusmods.com/skyrimspecialedition/mods/60837), matching colour |
  | **NORDIC UI** | [NORDIC UI](https://www.nexusmods.com/skyrimspecialedition/mods/49881) |

  Each of those four **is that overhaul's own DBVO-patched swf** with this mod's script changes
  ported onto it, so you keep your layout *and* get skip, reply-on-line-end and the cuts. Install
  the matching one and let it overwrite both DBVO's and the overhaul's `dialoguemenu.swf`.

  Running an overhaul that isn't listed (Dialogue Interface ReShaped, Convenient Dialogue UI,
  Dragonborn Reskin, Edge UI, Oathvein, …)? Let the overhaul's swf win: the **volume slider still
  works** — it is the DLL's speak-sound hook and needs nothing from the swf (the DLL's
  `dbvoOnPlayerLineEnded` invoke is a silent no-op on a swf that lacks it, and the cut events are
  never sent) — you lose only skip and the reply timing. Adding a variant is mechanical, and the
  recipe is in
  [`variants/README.md`](variants/README.md).
- **⚠ Dragonborn ReVoiced (DBReV) — also incompatible, and it is where DBVO 1.x users are going.**
  [DBReV](https://www.nexusmods.com/skyrimspecialedition/mods/184221) (mod 184221, v1.5, 2026-09-02)
  is an independent successor that *does* eat DBVO 1.0 voice packs, computes reply timing natively
  in its own SKSE plugin (from the `.fuz` header, like DBVO 2), plays the line and drives lip-sync
  itself since 1.5, and adds volume/pitch/reverb. It still needs JContainers (4.3.2 or newer on
  1.7.x) and has no skip key of its own (skip is SmartTalk's). Its page is explicit: *"Not
  compatible with any patches targeting the legacy DBVO mod … remove them if you have them!"* —
  that includes this mod's `dialoguemenu.swf`. So on 1.7.104 a DBVO 1.x user has two working
  exits (DBVO 2 in Legacy Mode, or DBReV) and **both require uninstalling this mod**. Full
  picture, numbers and the reasoning behind not forking DBVO 1.x ourselves:
  [`docs/dbvo-landscape.md`](../../docs/dbvo-landscape.md); the DBReV comparison itself:
  [`docs/plans/dbrev-comparison-analysis.md`](../../docs/plans/dbrev-comparison-analysis.md).
- **Pinned to DBVO 1.x, permanently.** The swf this mod is built from was a fixed target for years —
  until DBVO 2 (2026) replaced the whole swf + Papyrus stack with a single DLL. Within DBVO 1.x this
  mod is still stable; it does not and cannot follow DBVO forward, and there is nothing left to
  follow it to (DBVO 2 does the same work natively).

## Installation

1. Install **DBVO 1.1.1** (the OLD FILES tab, _not_ the page's main DBVO 2 download) and get it working.
2. Install this mod with a mod manager and let it **overwrite DBVO's `Interface/dialoguemenu.swf`**:
   the bundled swf _is_ DBVO's, recompiled with these tweaks, so it must win over DBVO's copy.
   The installer's **Dialogue menu style** page is where a UI overhaul is handled — pick your
   overhaul there instead of "Stock DBVO menu", and let that swf win over the overhaul's too
   (see [Compatibility](#compatibility)).
3. Enable `DBVODialogueTweaks.esp`.
4. **Fully restart** Skyrim (the Papyrus VM caches scripts per session).

Tune everything under **MCM → DBVO Dialogue Tweaks**.

## Configuration

| Option                       | Range     | Meaning                                                            |
| ---------------------------- | --------- | ------------------------------------------------------------------ |
| **Gap after your line ends** | 0–1000 ms | Pause between your line ending and the NPC's reply. `0` = instant. |
| **Player voice volume**      | 0–300%    | Volume of your own DBVO voice line only. `100` = unchanged; above amplifies (can clip a loud pack). |

## How it works

DBVO ships no DLL of its own. Your line is spoken through ConsoleUtil's `Player.SpeakSound` (which
gives no "finished" callback), and the NPC's reply is gated by a timer inside
`Interface/dialoguemenu.swf`. By default that timer just guesses how long your line will take from its
word count:

```actionscript
// DBVO's default reply timing (in dialoguemenu.swf)
words = lineText.split(" (")[0].split(" ").length;   // word count (strips "(Persuade)", etc.)
delay = round(words * 200) + 1400;                   // ~200 ms/word (a 300-wpm guess) + 1400 ms pad
setTimeout("topicClicked", delay);                   // → NPC replies
```

Two things go wrong:

- The flat **1400 ms pad** is dead air on top of the estimate.
- **200 ms/word assumes 300 wpm.** Real packs vary wildly: a fast AI voice can finish _"where can I
  get a drink"_ (6 words, ~1200 ms budgeted) in under a second. No single constant can track this per
  line.

This mod fixes it with a small **SKSE plugin** that hooks `Player.SpeakSound`. When _your_ DBVO line
starts, the plugin keeps the line's sound handle and watches it on a background thread; the instant the
line stops playing, it tells the menu to fire the reply after your configured gap, so the timing
matches the _actual_ audio, every time. The swf keeps only a generous word-count **backstop**, used
just in case the plugin isn't running.

The same hook powers the rest:

- **Volume**: below 100% it scales your line's sound handle to the slider. The engine clamps that
  path at 1.0 and caps its XAudio2 push at 0 dB, so above 100% a second, tiny hook on the engine's
  own volume-apply virtual (`BSXAudio2GameSound::SetVolumeImpl`, the one place the XAudio2 voice's
  gain is written) lets the engine set its value and then multiplies the voice's gain for your line
  only — matched by sound id on the audio thread, so no other sound is ever touched.
- **Skip / interrupt**: the swf sends mod events when you skip or pick a new topic; the plugin turns
  those into clean audio cuts (a short fade on the player line, plus a fade and dialogue-pause on an
  interrupted NPC reply).

Bundled artifacts, all built headlessly on Linux:

- `Interface/dialoguemenu.swf`: DBVO's swf recompiled with the skip + end-detection hooks (via ffdec).
- `SKSE/Plugins/DBVODialogueTweaks.dll`: the SKSE plugin (CommonLibSSE-NG, cross-compiled).
- `Scripts/DBVODialogueTweaksMCM.pex` + `Scripts/DBVOTweaks.pex`: the SkyUI MCM and a tiny
  Papyrus-native bridge to the DLL.
- `DBVODialogueTweaks.esp`: an independent, **ESL-flagged** plugin (a quest hosting the MCM, plus a
  player alias) that takes no load-order slot and never touches DBVO's own scripts.

## Building from source

Linux, headless, no Creation Kit or SSEEdit. `./build.sh` produces all five artifacts (the swf, the
DLL, the two `.pex`, and the `.esp`) plus one swf per UI-overhaul compatibility variant;
`./build.sh --install` also copies them into the live game (`--install <variant>` installs that
variant's swf instead of the stock one). `./package.sh` then builds the FOMOD, whose "Dialogue menu
style" page is generated from whatever variants are present.

A variant is the overhaul's own DBVO-patched swf with our deltas three-way-merged onto its
decompiled script (`./variants/port.sh`), and the base swfs it merges onto are third-party UI
assets, so they are **git-ignored** — a fresh clone builds the stock swf and skips the variants
until you drop each `base.swf` in. Both, plus how to add a fifth overhaul, are in
[`variants/README.md`](variants/README.md).

Toolchain:

- **ffdec** (JPEXS Free Flash Decompiler) for the AS2 swf recompile. Set `FFDEC=/path/to/ffdec.jar` if
  it isn't at the default location.
- The in-repo Papyrus compiler (`tools/`) for the `.pex` scripts, compiled against vendored SkyUI
  sources.
- **EspGen** (Mutagen) for the `.esp`.
- The `tools/skse` cross-compile toolchain (clang-cl + lld-link + xwin) for the DLL. CommonLibSSE-NG
  and MinHook are fetched and pinned by CMake.

## Permissions & credits

Built on **Dragonborn Voice Over** by **MathiewMay**, with permission received from the author. The
bundled `dialoguemenu.swf` is MathiewMay's asset recompiled with these tweaks. **All credit for DBVO
goes to MathiewMay.**

The four UI-overhaul menu styles are built on the DBVO-patched `dialoguemenu.swf` published for each
of those UI mods on the DBVO page's OLD FILES tab, and each carries that UI mod's own dialogue-menu
design. Only the `DialogueMenu` class's script is changed; every layout, font and asset in them is
its author's work, used under that mod's own Nexus permissions (checked 2026-09-09 — all three
permit it), and **credit for each menu's look goes to its author**:

| Menu style | Mod | Author |
| --- | --- | --- |
| Untarnished UI | [Untarnished UI](https://www.nexusmods.com/skyrimspecialedition/mods/75188) | **Vor** |
| Dear Diary Dark Mode (white / warm) | [Dear Diary Dark Mode](https://www.nexusmods.com/skyrimspecialedition/mods/60837) | **uranreactor** |
| NORDIC UI | [NORDIC UI](https://www.nexusmods.com/skyrimspecialedition/mods/49881) | **outobugi** |

## Testing

Two in-engine verifications, replayable and hands-free: `replyonlineend.steps` for the
reply-on-line-end feature and `voiceboost.steps` (+ `voiceboost-control.steps`) for the volume
boost. Build the two profiles once (after `./package.sh`), then run each half:

```bash
./stage-test-profile.sh          # ~/.cache/skytest-dbvotweaks{,-nodll}: identical but the DLL
SKYTEST_NO_AUTOLOAD=1 skytest replay ~/.cache/skytest-dbvotweaks \
    mods/DBVODialogueTweaks/replyonlineend.steps --headless --no-shots     # must PASS
SKYTEST_NO_AUTOLOAD=1 skytest replay ~/.cache/skytest-dbvotweaks-nodll \
    mods/DBVODialogueTweaks/replyonlineend.steps --headless --no-shots     # must FAIL
SKYTEST_NO_AUTOLOAD=1 skytest replay ~/.cache/skytest-dbvotweaks \
    mods/DBVODialogueTweaks/voiceboost.steps --headless --no-shots         # must PASS
SKYTEST_NO_AUTOLOAD=1 skytest replay ~/.cache/skytest-dbvotweaks \
    mods/DBVODialogueTweaks/voiceboost-control.steps --headless --no-shots # must PASS (100% = no boost line)
SKYTEST_NO_AUTOLOAD=1 skytest replay ~/.cache/skytest-dbvotweaks-nodll \
    mods/DBVODialogueTweaks/voiceboost.steps --headless --no-shots         # must FAIL
```

The boost script sets the slider through the mod's own Papyrus native (SkytestProbe's
`papyrus-call`), speaks a staged line, and asserts on the plugin's log (`until:log:`): the DLL
writes `voice boost x2.50: voice V -> V×2.5 (sound N)` only when its hook fired on the player's
sound, on the audio thread, and XAudio2 accepted the gain.

The last two steps are the assertion: the reply must **not** have fired once the swf's word-count
backstop would have expired, and must then fire on its own at line-end + the configured gap. The
control has exactly one file fewer and fails the first of those.

**Each menu style is verified the same way** — `replyonlineend.steps` is layout-independent (it
picks the topic with the keyboard and gates on which topic it landed on, because every overhaul
puts the list somewhere else), so a variant needs only its own stage:

```bash
./stage-test-profile.sh --variant nordicui
SKYTEST_NO_AUTOLOAD=1 skytest replay ~/.cache/skytest-dbvotweaks-nordicui \
    mods/DBVODialogueTweaks/replyonlineend.steps --headless --no-shots     # must PASS
```

All five passed on game 1.7.104 (2026-09-08): **stock**, **untarnished**, **dddm-white**,
**dddm-warm**, **nordicui** — each against its own swf (distinct md5 per run), each reaching both
assertion gates. `variants/menu.steps` is the companion pass that just photographs a variant's open
dialogue menu, which is how a new overhaul's layout is eyeballed before trusting it.

**The end-to-end pair — DBVO 1.x actually running** (2026-09-09, the verification behind the
Compatibility bullet above). Needs the two updated dependency archives in `~/Downloads`
(ConsoleUtilSSE NG, JContainers SE); everything else is lifted from the live full profile, and the
staged DBVO settings are a copy with the voice pack switched on, so your own install is untouched:

```bash
./stage-dbvo1x-profile.sh        # ~/.cache/skytest-dbvo1x{,-nodll}
SKYTEST_NO_AUTOLOAD=1 skytest replay ~/.cache/skytest-dbvo1x \
    mods/DBVODialogueTweaks/dbvo1x-endtoend.steps --headless --no-shots   # must PASS
SKYTEST_NO_AUTOLOAD=1 skytest replay ~/.cache/skytest-dbvo1x-nodll \
    mods/DBVODialogueTweaks/dbvo1x-control.steps --headless --no-shots    # must PASS (inverted)
skytest trace --src speak        # the control's witness: DBVO's own line, path and duration
```

Nothing is synthesised there: the topic click alone drives DBVO's Papyrus, JContainers, ConsoleUtil
and this mod's DLL in sequence. The control is the same stage minus one file and asserts the
opposite state at the same moment — six seconds after the click the reply has already fired,
because nothing cancelled the word-count backstop. `--variant <id>` stages a compatibility menu
instead of the stock one.

The scripts below need none of that. Because DBVO 1.x's Papyrus could not run on 1.7.104 when they
were written (see [Compatibility](#compatibility)), each supplies the two stimuli DBVO would have:
the console runs the same
`Player.SpeakSound "DBVO/…"` ConsoleUtil would, and a SkytestProbe `ui-invoke` makes the same
`UI.InvokeString(… startTopicClickedTimer …)` call. Everything downstream of those two is the
mod's own code. Neither script covers the skip or interrupt-cut features — those ride the same
hook, but each needs its own in-engine test.

## Design notes

Per-feature design write-ups (rationale and the dead-ends that shaped each one) live under
`docs/plans/`: `dbvo-dialogue-tweaks-design.md` (skip), `dbvo-v2-configurable-gap-design.md` (gap +
MCM), `dbvo-v3-player-voice-volume-design.md`, `dbvo-v4-voice-cut-on-skip-design.md`, and
`dbvo-v5-reply-on-line-end-design.md` (end-detection).
