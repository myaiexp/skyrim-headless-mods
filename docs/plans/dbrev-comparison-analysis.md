# Dragonborn ReVoiced (DBReV) vs. DBVO 2 vs. DBVO Dialogue Tweaks — comparison analysis

**Date:** 2026-09-03 · **Verdict:** the clean audio cut on skip is **not confirmed closed by DBReV,
but it is no longer structurally ours.** DBReV 1.5 holds the player line's XAudio2 source voice for
the whole line, so cutting it on skip is one call for its author; DBVO 2 cannot do it at all. DBReV
has **no skip input of its own** (skip is SmartTalk's job), and its reply timing is the same
precomputed-duration model as DBVO 2's, not live end-detection. Nothing here is worth building.

Companion to `docs/plans/dbvo-v2-compatibility-analysis.md` (DBVO 2) and `docs/dbvo-landscape.md`
(the three-framework picture and the "don't fork DBVO 1.x" ruling). Analysis only.

## Evidence classes — read this before trusting any line below

The DBVO 2 analysis had the DLLs on disk (file manifest, `default_options.json`, string tables,
RTTI walk, targeted disassembly). **That method could not be mirrored for DBReV.** The DBReV
archive is not on this machine (searched `~/Downloads`, `~/Projects`, `~/.cache`, the Steam
install and every skytest profile), the Nexus API has no download endpoint, and every
`content_preview_link` the files endpoint hands out for mod 184221 returns
`{"code":"not_found"}` — so not even the archive's file tree could be read. **No RTTI, no string
table, no disassembly claims are made here.** Every statement is tagged:

- **[verified]** — read by this session from a machine-readable source we control the query for:
  the Nexus API (mod object, files + file descriptions, changelogs, for 184221 and the related
  mods), the requirements metadata embedded in the mod page's HTML, GitHub (JContainers
  releases; crajjjj/AudioUtil's lip-format research doc).
- **[quoted]** — text written by the author or a user: the mod description, the 1.5 file
  description, the "DBReV API Integration" article (Nexus article 12355), and **314 of the 327
  comments** (fetched through the page's own comment widget endpoint, 8 pages).
- **[log]** — two user-posted `DBReV.log` files, both with the Debug toggle on, both still on
  pastebin: **v1.5.0** (2026-09-02, pastebin `vfm1YAu1`, game 1.5.97, a legacy DBVO pack,
  contains one skip) and **v1.4.6** (2026-09-02, `Mct74GY6`, game 1.5.97, a DBReV-format pack).
  These are first-party log lines from the shipped binary — the closest thing to a string table
  available — but they were produced on someone else's machine, not here.
- **[inferred]** — my reading of the above. Marked every time.

Nothing was installed into the live game and skytest was not launched: with no DBReV binary
there is nothing to run.

## 1. What DBReV actually is

### Shape [quoted + log]

- `DBReV.dll` — an SKSE plugin built on *"alandtse's CommonLibSSE-NG v4.39.3 (MIT License),
  forked and extended to support Skyrim 1.7.x (no code past v4.39.3 is used)"* [quoted]. The log
  banner is `DBReV v1.5.0 loaded` [log].
- **An ESL-flagged plugin** — every main-file description since 1.2 reads *"The main mod file
  (ESLified)"* [verified]; the API article says *"Do not test for DBReV.esp"* [quoted].
- **Papyrus** — `Registered Papyrus functions on DBReV_Core and DBReV_Main` [log]; the API
  article: *"DBReV dispatches from inside a Papyrus native call"* [quoted]. The MCM is SkyUI
  (`setstage SKI_ConfigManagerInstance 1` is the page's fix for a missing MCM entry) [quoted].
- **No `dialoguemenu.swf`.** *"Fully compatible with any UI overhaul mod (patching not
  required)"* [quoted]; DBVO-NG *did* need UI patches, DBReV tells users to remove them
  [quoted]. The log shows how: on menu open it runs
  `[TopicClickedPause] Patched 'TopicClicked' callback on Dialogue Menu open (movie 0x…)`,
  `Injected native topicClicked() skip handler`, and
  `Installed stale-list gate on GameDelegate 'ShowDialogueList' -> DoShowDialogueList()` [log].
  That is the same seam DBVO 2 uses (an FxDelegate-side intercept of `TopicClicked`) plus a
  runtime hook on the swf's own `topicClicked()` and a gate on the topic-list refresh [inferred].
- `Data/DBReV/` — `translations/<lang>.json` for the MCM, per-plugin locale databases
  (`Skyrim.esm.db` … 8,855 entries for the base game + DLC) [log], `global_settings.json`
  [verified, 1.3.2 changelog].

### Per-click flow, from the 1.5 log [log; step reading is inferred]

```
[TopicClickedPause] Patched 'TopicClicked' callback on Dialogue Menu open
[DBReV] Received Playback Request: 你有什么卖的吗？ (Index: 1)
[ResolveDialoguePath] Map: Skyrim.esm [raw 0007F6BB -> local 0007F6BB] (src DIAL,
    chain [SkyrimRevoiced Skypatcher.esp <- Unofficial Skyrim Special Edition Patch.esp
    <- Dragonborn.esm <- Dawnguard.esm <- Skyrim.esm] via Dragonborn.esm [3/5])
    -> What_have_you_got_for_sale_
[DBReV] Resolved Path: DBVO/FearTcbTriss/What_have_you_got_for_sale_.fuz
[ParseXwma][diag] container=XWMA fmtTag=0x0161 ch=1 sr=44100 … dpds=true(decoded=118784) … dur=1.3468
[GetFuzDuration] Success: Data/Sound/DBVO/FearTcbTriss/What_have_you_got_for_sale_.fuz (1.3467574s)
[VoiceApply] Armed XAudio2 for 'DBVO/…/What_have_you_got_for_sale_.fuz'
    (vol 300%, pitch 1.00x, fx -1/-1/-1/-1, reverb 0, 118784-byte decoded)
[API] kPlayerLineStart: index 1, audio 1.347s, total 1.347s, lip 1766 bytes, 'DBVO/…'
[Fallback] SpeakSound produced no voice for 'DBVO/…' after 250ms
    -- disarmed the XAudio2 hook and playing it ourselves
[LipSync] track 1.50s vs audio 1.35s (+11.4%)
[Preview] Engine XAudio2 not seen yet -- cannot play directly
[Playback] played 'DBVO/…' at 300% -> false (routing: UNROUTED -- no engine voice sampled yet…)
[LipSync] face ready
[TopicClickedPause] topicClicked() 1554ms after selection -- fast-forwarding (skip)
[API] kPlayerLineEnd: index 1, reason skipped
```

1. The click reaches the DLL as a **playback request from Papyrus** carrying the prompt text
   (here a Chinese client) and the topic index.
2. The prompt is resolved to a **FormID and its override chain** (which plugin last edited the
   line), then through the locale database to the English sanitized key. This is how a Chinese
   client plays an English legacy pack with no user-made JSON.
3. The `.fuz` is located, its **xWMA header is parsed for the duration** (`dpds` decoded size ÷
   sample rate) — the same precompute DBVO 2 does.
4. **Playback (1.5):** DBReV decodes the xWMA itself (`118784-byte decoded`) and **arms a hook on
   the engine's XAudio2** so that the source voice the engine creates for the line is captured
   and given DBReV's processed audio — gain to 300 %, pitch, four effects, reverb. If no engine
   voice shows up within 250 ms it **plays the line through its own XAudio2 source voice**,
   routed like a sampled engine voice. In this log both failed because the user had an
   `XAudio2_7.dll` replacement in the game folder; the author's diagnosis names exactly that
   [quoted]. A **"Legacy Playback"** switch on the MCM's Debug page keeps the 1.4.6 route
   [quoted].
5. **Lip-sync (1.5):** DBReV's own driver takes the LIP chunk out of the fuz and animates the face
   (§2.4).
6. The dialogue is **held for `totalSeconds`** (audio + configured post-line delay), then advanced
   — `kPlayerLineEnd … reason completed` — or advanced early on a skip (§2.1).

### How 1.4.6 differed [log + quoted]

The 1.4.6 log has the same resolve/duration lines but no `[VoiceApply]`, `[LipSync]`,
`[Fallback]` or `[Playback]` tags; instead
`[DBReV][GUARDRAIL] ConsoleUtilSSE detected -- voice playback dependency OK`. A user's crash log
from 1.4.6 shows the literal command in a register:
`Player.SpeakSound "DBReV/Fengfuren260829/Unofficial Skyrim Special Edition Patch.esp/dbrev_line_000bc7c2.fuz"`,
and the author: *"The crash happens when DBReV asks ConsoleUtilSSE to run the command
`Player.SpeakSound` … DBReV 1.4.6 has to use this command but the next version will not. It will
play the voice line and the lip sync by itself, and ConsoleUtilSSE will no longer be needed at
all."* [quoted, 2026-09-01]. So **through 1.4.6 DBReV spoke the line exactly the way DBVO 1.x
did** — ConsoleUtil → `Player.SpeakSound` → engine — and 1.5 is the release that took the audio
in-house.

## 2. The six questions

### 2.1 Does DBReV cut the PLAYER's line audio on skip? — answered first

**Behaviour: unknown. Capability: yes in 1.5, probably not before.**

What is established:

- **DBReV has no skip input of its own.** The author, 2026-07-08: *"The skipping should work with
  SmartTalk's feature (it's not a native game function)."* A user whose *"quick-click dialogue
  skipping no longer appears to work"* was asked *"Are you using SmartTalk?"* and reported
  *"Installing Smart Talk has successfully resolved my issue"* (2026-08-20/22). The page lists
  *"Fully compatible with SmartTalk, including its dialogue skip features (patching not
  required)"*; SmartTalk's own 1.0.4 changelog: *"If DBVO is installed, SmartTalk will also allow
  player dialogues to be skipped smoothly."* [all quoted]. Without SmartTalk, a DBReV user
  **cannot skip their own line at all** [inferred from the above]. That is the opposite of DBVO 2
  (native `SkipInputHandler` since 2.0.1.6) and of our mod (E / left-click in the swf).
- **DBReV does have a skip *path*.** `Injected native topicClicked() skip handler` at menu open;
  on the skip, `topicClicked() 1554ms after selection -- fast-forwarding (skip)` followed by
  `[API] kPlayerLineEnd: index 1, reason skipped` [log]. The API article defines three end
  reasons: *"a line that played out (kEndReason_Completed) … one the player skipped
  (kEndReason_Skipped) or one cut short by a new topic click (kEndReason_Superseded)"* [quoted].
  So a skip is a first-class event inside DBReV, and anything that calls the swf's
  `topicClicked()` during the hold (SmartTalk's input handler being the shipped example)
  fast-forwards the dialogue [inferred].
- **Whether the audio is stopped on that event is stated nowhere** — not on the page, not in any
  changelog, not in the article, not in 314 comments. No user complains that their voice keeps
  talking over the NPC after a skip, and none praises a clean cut. The one logged skip fired at
  1,554 ms on a 1,347 ms line — after the audio had ended — so the log cannot show a cut either
  way [log + inferred].

What can be concluded structurally:

- **1.4.6 and earlier:** ConsoleUtil `Player.SpeakSound`, same as DBVO 1.x and DBVO 2. There is no
  sign DBReV held the resulting `BSSoundHandle` (that would need the `Actor::SpeakSoundFunction`
  hook our DLL installs), so a skip most likely advanced the dialogue and left the line playing —
  DBVO 2's exact gap [inferred].
- **1.5:** DBReV *owns the playback voice*. Either it has captured the engine's XAudio2 source
  voice for the line (`Armed XAudio2 for …`) or it created its own (`playing it ourselves`), and
  in both cases it holds an `IXAudio2SourceVoice` for the whole line — the one object DBVO 2
  never has. Stopping it on `reason skipped` is a `Stop()`/`FlushSourceBuffers()` pair. The 1.5
  lip driver also has to stop writing to the face on a skip (otherwise the mouth keeps moving
  after the dialogue has advanced), so the skip path already halts *something* DBReV owns
  [inferred, weak].

**Plain statement:** against DBVO 2 our cut-on-skip was a *structural* differentiator — it lacked
the handle. Against DBReV 1.5 it is at most a *behavioural* one that cannot be confirmed from
outside the binary, sits behind a third-party skip input, and costs its author a few lines. It is
not a reason to keep the mod alive and not a reason to build anything. Our mod does keep one
thing DBReV lacks — a **native skip input** — but that is a DBVO 1.x-swf feature with no
audience left (see `docs/dbvo-landscape.md`).

### 2.2 Reply timing

| | Base | Offset | Fallback | Nature |
| --- | --- | --- | --- | --- |
| **DBReV** | `audioSeconds` = *"Length of the voice file, measured from the FUZ/xWMA header before playback"* [quoted]; `[GetFuzDuration] Success … (1.3467574s)` [log] | *"the user's configured post-line delay"*; `totalSeconds` = audio + delay = *"how long DBReV holds the dialogue open, i.e. when the conversation actually advances"* [quoted] | *"0.0 means DBReV could not measure the file and fell back to an estimate"* [quoted]; DBVO-NG 1.0.1 called it the *"generic timing formula"* [verified] | **Precomputed, timer-driven.** In the 1.5 log DBReV's own playback *failed* (`played … -> false`), yet `kPlayerLineEnd … reason completed` still arrived 2.300 s after `kPlayerLineStart` for a 2.276 s file. The hold is a timer on the header duration, not an end-of-playback callback [inferred from log timestamps]. |
| **DBVO 2** | `[delay] {}ms = fuz-duration {}ms + offset {}ms` | `npc_response_delay` (default 0) | `word-count-estimate` | Precomputed, same model (`dbvo-v2-compatibility-analysis.md` §1). |
| **Ours** | Live: the `Actor::SpeakSoundFunction` hook retains the `BSSoundHandle`, a detached thread polls `IsPlaying()` every 30 ms, the playing→stopped edge fires the reply | *"Gap after your line ends"*, 0–1000 ms | swf backstop `words × 300 + 2000 ms` | **Observed end**, verified in-engine on 1.7.104 by A/B (README → Compatibility). |

DBReV's lineage confirms the model: its predecessor DLL logged to `FuzDurationPlugin.log` and the
1.0.1 fix was for *"exceptionally long dialogue lines [that] would fail to calculate timing,
triggering fallback to generic timing formula"* [verified, DBVO-NG changelog]. DBReV also ships the
timing to other mods — `kPlayerLineStart` carries `audioSeconds` and `totalSeconds`, and the
article warns that driving lips off `totalSeconds` *"leaves the character silently mouthing for a
second and a half after the voice ends"* [quoted].

Difference that matters: a precomputed hold is deterministic and survives a playback failure (the
conversation still advances on time — arguably a feature); ours reflects what actually played but
only exists for lines that went through `SpeakSoundFunction`. Neither DBReV nor DBVO 2 observes
the end of the audio. On 1.5 DBReV *could* — it owns the voice and XAudio2 reports buffer end — but
the log says it does not [inferred].

### 2.3 Dependencies — "SKSE plugin + SkyUI MCM only" is wrong on one count

The page's own Requirements metadata (the JSON Nexus embeds in the description tab, read from the
HTML) lists exactly three mods **[verified]**:

| Requirement | Version listed | Note |
| --- | --- | --- |
| Address Library All in One (1.7.104.0) | v13 | Repointed 2026-08-29 after the AL author archived the old file [quoted] |
| **JContainers SE** | 4.2.13.1 | **Hard requirement, still.** |
| SkyUI | 6.11 | For the MCM |

Not listed: ConsoleUtilSSE NG (dropped in 1.5 — *"Removed 'ConsoleUtilSSE NG' as a requirement,
it is no longer needed"* [verified changelog]; load-bearing before, per the 1.4.6 log's
`ConsoleUtilSSE detected -- voice playback dependency OK`), PapyrusUtil, SKSE Menu Framework.

**JContainers is the catch on 1.7.x.** The author's sticky (2026-08-31): *"To run it on 1.7.x you
will need the pre-release version of JContainers SE, currently only available on the author's
github page. Other than that you just need the latest version of SkyUI and Address Library … Note:
SkyUI has not officially released a 1.7.x compatible version, but it works fine from my testing."*
[quoted]. GitHub confirms **[verified]**: `ryobg/JContainers` has pre-releases **v4.3.0**
(1.6.1170), **v4.3.1** (SKSE 2.3.0 / 1.7.99) and **v4.3.2** (2026-08-29, *"SKSE 2.3.1 / SAE
1.7.104"*), all flagged `prerelease`; Nexus still serves 4.2.13.1. So on 1.7.104 DBReV needs a
GitHub-only pre-release of a version-locked DLL, plus an unofficially-compatible SkyUI.

Versus the others: DBVO 1.x (and therefore our mod) needs ConsoleUtil **and** JContainers; DBReV
1.5 dropped ConsoleUtil but kept JContainers; DBVO 2 needs neither (SKSE Menu Framework instead).
Side effect for our own docs: the JContainers half of "DBVO 1.x is dead on 1.7.104" now has an
upstream candidate fix (4.3.2 pre-release), and ConsoleUtilSSE NG 1.6.1's file page says
*"Confirmed working on 1.7.99"* [verified]. Neither is installed or tested here; the ruling in
`docs/dbvo-landscape.md` does not depend on it.

### 2.4 The lip-sync driver — what it does that DBVO 2 doesn't

- **Through 1.4.6: nothing DBVO 2 doesn't.** Both let the engine animate the face from the fuz's
  LIP chunk as a side effect of `Player.SpeakSound`. The author, 2026-08-12: *"it simply calls the
  game's native function for playing lip sync animations"*; *"DBReV doesn't create the lip-sync,
  it just plays it from the voice pack"* [quoted]. That engine path has a known limit the author
  names: *"the engine only animates the player's face when it thinks the head is on screen"*
  [quoted] — which is why Cinematic Conversation Camera (CCC) broke lip-sync and wrote its own
  driver.
- **1.5: DBReV drives the face itself.** Changelog: *"DBReV now drives the audio and lip-sync
  directly, enabling full compatibility with Cinematic Conversation Camera"*; to users: *"make
  sure to disable CCC's lip-sync driver so that DBReV can take over correctly"* [quoted]. Log:
  `[LipSync] track 1.50s vs audio 1.35s (+11.4%)` then `[LipSync] face ready` [log] — it decodes
  the LIP track, compares its length to the audio's, and animates the face on its own schedule.
- **What the driver is built on [verified, GitHub].** The page credits crajj's AudioUtil research
  *"whose research into Skyrim's lip-sync system enabled me to build DBReV's own lip-sync driver
  (no actual code from his project was used)"* [quoted]. AudioUtil's `RLE Hypothesis.md` records
  the engine decompile (2026-08-31): the `.lip` payload is zero-RLE compression over a dense
  `float32[frames × 33]` grid — 16 phonemes + 17 modifiers, 30 fps — and a correction dated
  **2026-09-02, credited to Raynor1511**: the u16 before the payload is a start token
  (`>> 2` = leading zero cells) and slot 32 is the 17th modifier (HeadYaw), not padding.
  *"Raynor found it by capturing the engine's own facegen output during `Player.SpeakSound` and
  diffing against the decode."* So DBReV's driver decodes the authored FaceFX track exactly and
  writes phoneme/modifier weights to the player's face per frame, bypassing the engine's
  on-screen-head gate [inferred from the above].
- **It also exports the data.** Since 1.4.4 the SKSE messaging API hands third parties `lipData /
  lipSize` — *"the LIP chunk lifted verbatim out of the FUZ container"* — alongside the timing
  [quoted]. CCC's page recommends *"Dragonborn ReVoiced 1.4.4 or newer"* for *"the most precise
  player-voice timing and lip sync"* [verified, CCC description].

DBVO 2 has none of this: no lip code, no API. Neither do we.

### 2.5 Voice-pack handling

**Legacy DBVO 1.0 packs** — resolved to `DBVO/<pack>/<Sanitized_Prompt>.fuz` [log], the same
layout DBVO 2 resolves in legacy mode (`speak-watch` saw `DBVO/Danagis_KaratVoice/<…>.fuz` on
2026-09-02). On top, DBReV adds [verified changelogs unless noted]:

- **Locale databases** — the prompt is mapped by FormID through a per-plugin `.db` to the English
  key, so non-English clients play English packs with no user JSON (base game + DLC built in;
  an optional "Extended Locales Database" covers *"500+ mods"* incl. Nolvus and Gate to
  Sovngarde; a "Locale Database Maker" app builds more).
- **Override-chain matching** (1.3.3, 1.4.6) — *"When one mod rewrites another mod's dialogue,
  DBReV now plays the line belonging to the mod that actually changed it, and falls back to the
  original mod's audio if the newer version isn't voiced."* Visible in the log's
  `chain [SkyrimRevoiced Skypatcher.esp <- Unofficial … Patch.esp <- … <- Skyrim.esm]`.
- Multi-stage key matching (1.3.3), parenthetical lines, Various Dialogue Tags compatibility
  (1.2, 1.3.3), BSA packs, and a **compressed-BSA guardrail** (`Voice-archive scan clean: no
  compressed DBReV/DBVO BSAs found` [log]).
- **The long-path CTD is *not* fixed for legacy packs.** Page: *"This only happens when using a
  DBVO 1.0 voice pack containing very long dialogue lines … due to the Windows file system path
  limit of 260 characters … The best solution is to migrate your voice pack"* [quoted]. DBVO-NG
  1.0.1 only *"improved long path support"* [verified]; the log prints `Path Length: 92` per
  line [log].

**DBReV's own format** — not hashed: **keyed by plugin + FormID**. *"All file names inside a DBReV
voice pack follow the same structure of `<pluginID>/dbrev_line_<formID>.fuz`"* [quoted, Voice
Pack Maker page 184169]; in the wild:
`DBReV/Fengfuren260829/Unofficial Skyrim Special Edition Patch.esp/dbrev_line_0007f6bb(1).fuz`
with `Resolved variation: … (Pool Size: 3)` [log]. Consequences the page claims [quoted]:
language-agnostic (no locale mapping at all), up to 20 random variations per line, custom line
text, the long-path crash gone by construction, uncompressed BSA with an auto-generated light
ESP, and a migration tool that converts a legacy pack *"without having to regenerate a single
FUZ file"*. It is **mutually incompatible with DBVO 2 packs**: *"Same way how you can't use DBReV
voice packs with DBVO2. These are two competing mods. The only thing in common is DBVO 1.0 voice
packs which both mods support."* [quoted, 2026-07-21].

DBVO 2's own format keys on the prompt (sanitized or hashed) with alias parsing; ours has no pack
format — it rides whatever DBVO 1.x plays.

### 2.6 Game-build support

- The page says *"tested specifically against 1.5.97, 1.6.1170 and 1.7.140"* [quoted] — **1.7.140
  is a typo for 1.7.104.** The 1.5 file description says *"Compatible with Skyrim SE/AE versions
  1.5.97 - 1.7.104"* [verified] and the sticky says *"I tested it myself on game version 1.7.104"*
  [quoted]. No 1.7.140 build exists as far as this repo knows (Steam is on 1.7.104). The
  landscape doc repeated the typo; fixed alongside this analysis.
- Format-5 Address Library: DBReV 1.5 runs on 1.7.104 per its author, so its CommonLib fork reads
  it [inferred]. The 1.4.6 file says *"1.5.97 - 1.6.x"* [verified] — 1.4.6 and earlier die on
  1.7.x like every pre-August DLL.
- **1.5 has a 1.5.97 regression**: with an `XAudio2_7.dll` replacement in the game folder the new
  audio driver never sees the engine's XAudio2 and the line is silent (lips move, no sound);
  workaround is Legacy Playback, which needs ConsoleUtil again [quoted + log]. The author runs
  1.6.1170 himself [quoted]. VR and GOG are not mentioned anywhere.
- For scale: DBVO 2.0.1.6 loaded and replied correctly on 1.7.104 in our own A/B (2026-09-02);
  our mod's reply-on-line-end is verified on 1.7.104 (2026-09-03); DBReV's 1.7.104 claim is the
  author's word only.

## 3. Feature-by-feature

| | **DBVO Dialogue Tweaks** (ours, on DBVO 1.x) | **DBVO 2** 2.0.1.6 | **DBReV** 1.5 |
| --- | --- | --- | --- |
| Reply timing | live end-detection off the retained `BSSoundHandle`, 30 ms poll | precomputed `fuz-duration` + `npc_response_delay` | precomputed xWMA-header duration + post-line delay, timer-driven |
| Timing fallback | swf backstop `words×300+2000` | `word-count-estimate` | *"an estimate"* (formula) |
| Skip input | **native** (E / click, in the swf) | **native** (`SkipInputHandler`: Activate / LMB / gamepad A) | **none** — SmartTalk's input handler calls `topicClicked()`; DBReV fast-forwards |
| Player audio cut on skip | **yes** (30 ms fade) | **no** — no handle | **unknown**; holds the XAudio2 voice, so trivially possible |
| NPC reply cut on new topic | yes | no evidence | no evidence (`Superseded` is about the player's line) |
| Player-voice volume | 0–100 % attenuation | `dialogue_volume` 0–100 + `dialogue_reverb` | gain to **300 % absolute**, pitch, reverb, 4 stackable effects, "Room Acoustics" reverb-send fix, in-MCM preview, per-character profiles |
| Lip-sync | engine (via SpeakSound) | engine | **own driver** (1.5); engine before |
| swf patch | **is one** (DBVO 1.x's swf recompiled) | none (native `TopicClicked`/`SkipText` intercept) | none (runtime `TopicClicked` patch + injected `topicClicked()` handler + `ShowDialogueList` gate) |
| Config UI | SkyUI MCM | SKSE Menu Framework (ImGui) | SkyUI MCM |
| Hard dependencies | DBVO 1.x + ConsoleUtilSSE NG + JContainers + SkyUI + Address Library | SKSE + SKSE Menu Framework + Address Library | SkyUI + **JContainers** + Address Library (+ own ESL esp + Papyrus) |
| Legacy (1.0) packs | whatever DBVO 1.x plays | yes, Legacy Mode | yes, plus locale DBs and override-chain matching |
| Own pack format | — | `Sound/DBVO` 2.0 packs, prompt-keyed | plugin+FormID-keyed, variations, text overrides |
| Third-party API | — | none found | SKSE messaging: line start/end (+reason) + timing + raw LIP bytes |
| 1.7.104 | verified (reply-on-line-end only; DBVO 1.x itself does not run) | loaded and worked in our A/B | author-tested; needs a GitHub pre-release JContainers |
| Nexus (2026-09-03) [verified] | 182628 | 84329 — 345k unique (page total incl. 1.x) | 184221 — 7,480 DL / 3,409 unique / 213 endorsements |

## 4. What this means for our mod

1. **No differentiator survives that we can demonstrate.** Reply timing: both successors have the
   precomputed model natively, and the *observed-end* distinction is a nuance no user asked for.
   Volume: both exceed us. Skip: DBVO 2 has it natively; DBReV delegates it to SmartTalk. Audio
   cut on skip: structurally missing in DBVO 2, unverified-but-trivial in DBReV 1.5.
2. **The right home for "cut the audio on skip" is upstream, and the more natural upstream is now
   DBReV**, not DBVO 2: DBReV already has a skip handler that emits `reason skipped` and a held
   source voice. DBVO 2 has the handler but not the voice. Either way it is an offer of a few
   lines to an author, not a mod. (The political note in `docs/dbvo-landscape.md` still applies
   to anything that looks like forking.)
3. **Do not build.** Not a DBReV patch (its page names patches to the legacy stack as incompatible,
   and it has no swf to patch), not a DLL-only successor (both successors would race their own
   skip path), not a DBVO 1.x fork (DBReV is that fork, shipped and maintained).

## 5. Open items

- **The DBReV binary was never analysed.** Getting `DBReV.dll` 1.5 onto this machine (a manual
  Nexus download — the API cannot) would allow the DBVO 2 treatment: the string table alone
  would settle §2.1 (look for a stop/flush call or log line reached from the `reason skipped`
  path near `[TopicClickedPause]`, and for `IXAudio2SourceVoice`-shaped RTTI or imports).
- **Cut-on-skip in-engine** would need DBReV 1.5 **and SmartTalk** in a skytest profile (there is
  no skip without it) plus a JContainers 4.3.2 pre-release, and a probe other than `speak-watch`:
  in the direct-playback path the audio may not be the engine's `BSSoundHandle` at all.
- **1.5's healthy playback path is read from one *failing* log.** The success-path lines (what
  `[VoiceApply]` logs when the engine voice *is* captured) have not been seen; the "capture the
  engine's XAudio2 voice, fall back to our own" reading in §1 is inferred from the fallback text.
- 13 of 327 comments were not captured by the widget walk; nothing suggests they matter.
- Whether DBVO 1.x itself revives on 1.7.104 with JContainers 4.3.2 + ConsoleUtilSSE NG 1.6.1 is
  untested and, per the landscape ruling, not worth a session.
