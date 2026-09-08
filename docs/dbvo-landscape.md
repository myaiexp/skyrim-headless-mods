# The DBVO landscape — three frameworks, and where our mod stands

**Date:** 2026-09-03 · **Ruling: do NOT take over / fork DBVO 1.x.** Someone already did, it is
actively maintained, and our mod is on its explicit incompatibility list.

Read this before proposing any work on `mods/DBVODialogueTweaks`. It exists because the obvious
idea — "DBVO 1.x is dead on 1.7.104, we own a patched `dialoguemenu.swf` and an SKSE DLL already,
let's absorb DBVO 1.x and drop its dead dependencies" — is technically cheap, sounds right, and is
the wrong call for reasons that are not visible from inside this repo.

## The three frameworks

All three voice the *player's* dialogue choices. They are mutually exclusive installs.

| | Nexus | Current | Shape | Our mod works with it? |
| --- | --- | --- | --- | --- |
| **DBVO 1.x** | 84329 (OLD FILES) | 1.1.1, Aug 2023, unsupported | Papyrus + SkyUI MCM + JContainers + ConsoleUtil + a patched `dialoguemenu.swf` | **Yes — this is the only one.** And it does not run on 1.7.104 (below). |
| **DBVO 2** | 84329 (main) | 2.0.1.6, 2026-08-30, MathiewMay | One SKSE DLL + SKSE Menu Framework. No esp, no Papyrus, **no swf** | **No.** Ships no swf and our patched one softlocks it. |
| **Dragonborn ReVoiced (DBReV)** | 184221 | 1.5, 2026-09-02, Raynor1511 | SKSE DLL + ESL esp/Papyrus + SkyUI MCM + **JContainers** (no ConsoleUtil since 1.5), own audio + lip-sync driver, **no swf** | **No.** Its page: *"Not compatible with any patches targeting the legacy DBVO mod … remove them if you have them!"* |

**Verified here via the Nexus API (2026-09-03):** DBVO 2 = 84329, v2.0.1.6, updated 2026-08-30,
345,332 unique page downloads. DBReV = 184221, v1.5, published, updated 2026-09-02, 7,474 downloads
/ 3,404 unique. DBVO NG = 181906, **wastebinned / unavailable**, v1.1.9, last updated 2026-06-12,
same author as DBReV.

## Why DBVO 1.x was dead on 1.7.104 — and is not any more (2026-09-09)

> **Settled in-engine: DBVO 1.1.1 runs on game 1.7.104 with ConsoleUtilSSE NG 1.6.1 +
> JContainers SE 4.3.2.** Both load; a topic click drives the real chain (mod event → JContainers
> → ConsoleUtil `SpeakSound` → `startTopicClickedTimer` → our reply-on-line-end), witnessed
> independently by SkytestProbe's `speak-watch`
> (`DBVO/Danagis_KaratVoice/Where_can_I_learn_more_about_magic_.fuz`, 1784 ms, natural end).
> Replayable: `mods/DBVODialogueTweaks/{stage-dbvo1x-profile.sh,dbvo1x-endtoend.steps,
> dbvo1x-control.steps}`. Two traps that cost a boot each: JContainers needs its whole
> `SKSE/Plugins/JCData/` tree or it throws during registration and the game dies with
> `skse64.log` still saying "loaded correctly", and any SkyUI-carrying profile pops
> **SKYUI ERROR CODE 4** which swallows the activation key (findings #39/#40).
>
> **This does not reopen the takeover question below.** What changed is that a DBVO 1.x user can
> stay on 1.7.104 — the audience is no longer pinned to downgraded 1.6.1170 — not that DBVO 1.x
> gained a maintainer. The section below is kept as the record of what was broken.

## What was broken (2026-09-03)

Established in-engine 2026-09-03 (see `mods/DBVODialogueTweaks/README.md` → Compatibility). SKSE
2.3.1 refuses both SKSE plugins DBVO 1.x's `DBVO_Script_MCM` calls into, before loading them:
`ConsoleUtilSSE.dll` 1.5.1.0 (*"must be recompiled for new address library"*) and `JContainers64.dll`
(*"disabled, incompatible with current version of the game"*). ConsoleUtil speaks the line,
JContainers reads the voice-pack settings — so DBVO 1.x produces no player voice at all.

The fix, both from their own authors: ConsoleUtilSSE NG **1.6.1** (2026-08-22) and JContainers SE
**4.3.2** (*"SKSE 2.3.1 / SAE 1.7.104"*; a GitHub-only pre-release from 2026-08-29 until Nexus
caught up on 2026-09-07). Mase downloaded both on 2026-09-09 and the pair was verified here the
same day — see the box above. The older builds are still refused, so "update those two" remains the
first thing to ask of a 1.7.104 user reporting silence.

## Why not take DBVO 1.x over

DBReV **is** that takeover, shipped. From its own page (quoted, not verified in-engine by us):

- *"full backwards compatibility support for DBVO 1.0 voice packs (but not DBVO 2)"* — it eats the
  entire legacy pack ecosystem, which was the whole argument for reviving 1.x.
- *"built around an SKSE plugin to calculate dialogue timings in real time"* — **that is our
  headline feature, native.**
- Volume gain to 300%, pitch, reverb, four audio post-processing effects — covers our volume slider
  (which also reaches 300% since 1.1.0) and then some.
- SkyUI **MCM**, explicitly *"as some people reported having issues with the SKSE Menu Framework
  used by DBVO 2"*.
- Fixes the long-dialogue-path CTD (legacy packs name files after full dialogue lines, which blows
  the Windows 260-char path limit).
- Author-tested on 1.5.97, 1.6.1170 and **1.7.104** (the page text says "1.7.140" — a typo; the
  1.5 file description and the author's sticky both say 1.7.104). On 1.7.x it needs
  JContainers **4.3.2** (on Nexus since 2026-09-07) — JContainers is still a hard requirement.
- Built on alandtse's CommonLibSSE-NG v4.39.3, forked to support 1.7.x.
- **No skip input of its own** — the author: *"The skipping should work with SmartTalk's feature
  (it's not a native game function)."* Skip is SmartTalk's; DBReV fast-forwards when it fires.

So a takeover would mean rebuilding, from behind, a timing engine that already exists, against an
author who also ships a voice-pack maker app and locale mappings for 500+ mods.

**There is also a political edge.** DBReV's FAQ states its predecessor DBVO NG *"was previously
removed from the Nexus due to a complaint by the author of DBVO"* — and the API confirms 181906 is
wastebinned. Our mod exists under permission received directly from MathiewMay. Forking his 1.x
stack wholesale is the exact move that got the last one taken down.

## What this means for our mod's audience

This is the part that inverts, and it is easy to get backwards.

For a **voice pack**, staying in 2023 format is the right call — one pack serves DBVO 1.1.1, DBReV,
*and* DBVO 2 in Legacy Mode. Third-party research (Grok, 2026-09-03, **not verified by us**) puts
the split at roughly 305k unique downloads on DBVO 1.1.1's core file against ~20k on DBVO 2.0.1.5,
with the big collections instructing users to install DBVO 2 and turn Legacy Mode on.

Our mod is **not** a voice pack. It is a `dialoguemenu.swf` patch, and both successors require
removing exactly that file. So it serves only people still running the **DBVO 1.1.1 framework
itself** — which is the one configuration that no longer works on 1.7.104, and which both
successors are actively migrating people off. That audience is not the 305k; it is a shrinking
subset with two maintained exits.

## The one thing still possibly ours — resolved 2026-09-03: it is not structurally ours any more

**Clean audio cut on skip.** DBVO 2 structurally cannot do it — it never holds a sound handle (no
sound RTTI in either build, neither references Address Library 36541/37542, `FireResponse` makes no
audio call; see `docs/plans/dbvo-v2-compatibility-analysis.md`). Our stack holds that handle because
it hooks `Actor::SpeakSoundFunction`.

**DBReV: behaviour unknown, capability present.** Full treatment in
`docs/plans/dbrev-comparison-analysis.md` (page, API article, 314 comments and two user-posted
debug logs — the binary itself is not on this machine, so nothing there is disassembly). What it
establishes:

- Through 1.4.6 DBReV spoke the line via ConsoleUtil `Player.SpeakSound`, exactly like DBVO 1.x
  and DBVO 2, with no sign of holding the handle — DBVO 2's gap, inferred.
- **1.5 (2026-09-02) took the audio in-house**: it decodes the xWMA itself and plays through an
  XAudio2 source voice it owns (captured from the engine, or its own as fallback — the log lines
  `Armed XAudio2 for …` / `disarmed the XAudio2 hook and playing it ourselves`). It holds that
  voice for the whole line, so a cut on skip is one call away. Its skip path exists
  (`topicClicked() … fast-forwarding (skip)` → `kPlayerLineEnd … reason skipped`) but **no
  statement anywhere says the audio is stopped**, and the only logged skip landed after the line
  had already ended.
- DBReV has **no skip input of its own** — skipping the player's line is SmartTalk's feature.

So the differentiator that was *structural* against DBVO 2 is at most *behavioural* against
DBReV 1.5, unverifiable from outside its binary, and a few lines for its author. **It is not a
reason to keep the mod alive and not a reason to build anything.** If it is ever offered upstream,
the natural recipient is now Raynor1511 (skip handler + held voice) rather than MathiewMay (skip
handler, no voice). The only thing our mod does that DBReV lacks is a *native* skip key — a DBVO
1.x-swf feature with no audience.

## UI overhauls that carry a DBVO 1.x-patched `dialoguemenu.swf` (researched 2026-09-08)

> **Four are BUILT and verified in-engine (2026-09-08): Untarnished UI, Dear Diary Dark Mode
> white + warm, NORDIC UI.** They ship as a FOMOD "Dialogue menu style" choice. How a variant is
> made, re-ported and added to — including the one merge conflict every CDUI-lineage base
> produces — is `mods/DBVODialogueTweaks/variants/README.md`; the priority order for the
> *remaining* overhauls is at the end of this section.

Context: the first Nexus feedback on our mod said "my dialogue options moved from the left to
the right". Our swf is stock-DBVO layout (vanilla: bottom-centre), so the user had been running a
UI overhaul's DBVO-patched swf, and ours overwrote it. Which overhauls those are, where their
DBVO patch lives, and how their menu is placed — all from the Nexus API (`tools/nexus`, total
downloads) and the mod pages. A compatibility patch = our script deltas re-applied on top of
*their* DBVO-patched swf.

| UI mod (Nexus ID) | DL / endorse | Topic list | DBVO 1.x-patched swf lives in |
| --- | --- | --- | --- |
| Dear Diary Dark Mode (60837) | 2.27M / 13.8k | left (CDUI-based, configurable) | DBVO page Old files: "Dear Diary Dark Mode Patch" white + warm, v1.1.0 2023-07-19 |
| Untarnished UI (75188) | 1.49M / 7.3k | left (dialogue remade on DDDM) | DBVO Old files "Untarnished UI Patch" v1.1.0; **121096** "DBVO – Untarnished UI Patch" v1.1.1 2024-06-04, 11.1k DL (the strongest single patch-demand signal) |
| Dialogue Interface ReShaped (46546) | 524k / 7.8k | **left** (its page: "Aligns menu to the left side") | DBVO Old files "Dialogue Interface ReShaped Patch" v1.1.0; **86808** "Dragonborn voice over-DIR patch" 3.4k DL |
| Convenient Dialogue UI (57943) | 457k / 4.7k | left by default (`bRightSidedList`, `interface/dialoguemenu.txt`) | DBVO Old files: four "Convenient Dialogue UI Patch – {Vanilla, Minimalist, NordicUI, DIR} look" v1.1.0 |
| Dragonborn Reskin – Dialogue Menu (157643) | 162k / 406 | left or right, configurable | its **own** main file "Version for DBVO" v1.2 2025-10-12 — the only patch still maintained |
| Dragonbreaker UI (73208) | 71k / 663 | left edge | bundled since 1.4.3 (2023-03-29); also DBVO Old files |
| NORDIC UI (49881) | 2.61M / 24.4k | **centre** (vanilla-like) | DBVO Old files "NordicUI Patch" v1.1.0; 87378 (21:9) |
| Edge UI (130983) | 709k / 5.0k | **right** | bundled in its FOMOD ("DBVO compatibility" option) |
| Oathvein UI (160916) | 368k / 1.8k | **right** | FOMOD with/without DBVO; also via 182554 |
| Vel'dun UI (176230) | 201k / 1.3k | unknown | **removed** in 1.0.6 ("no longer needed with DBVO 2 or DBReV") |

Not relevant: SkyHUD (hudmenu only), Dear Diary paper (no dialogue menu), Better Dialogue
Controls (vanilla placement). **Every DBVO-page patch sits under Old files, all v1.1.0 dated
2023-07-19, all DBVO 1.x** — DBVO 2 and DBReV need an *unpatched* swf.

Two facts that make patches tractable:

- **The patches are all applied by function name onto Bethesda's `DialogueMenu` class**
  (`onSelectionClick` → `initDBVO`, plus appended `initDBVO`/`startTopicClickedTimer`/
  `topicClicked`), spelled out in the "DBVO Automatic SWF Patcher" article (mod 182554, a
  Python + JPEXS regex patcher, "tested on Oathvein, Vel'dun, NordicUI"). Our
  `src/__Packages/DialogueMenu.as` has exactly that shape, so a compat build is our named-function
  deltas ported onto each target's decompiled script, then `build.sh`'s ffdec import against
  *their* swf as the base instead of `stock/`.
- **No third-party patched swf has public source** (GitHub code search for the DBVO hook names
  hits only this repo); Nordic and Untarnished credit JPEXS, i.e. decompiled edits. So each
  target's swf has to be fetched from Nexus by hand (the API download needs Premium) and
  decompiled here.

Decompiled (ffdec) the four DBVO-page patches Mase downloaded on 2026-09-08 — Untarnished, DDDM
white, DDDM warm, NordicUI. All four keep every Bethesda `DialogueMenu` function name plus DBVO's
`initDBVO` / `startTopicClickedTimer` / `topicClicked`, and they fall into **two script
families**: NordicUI's script is stock DBVO's to within 3 lines (the swf differs in layout
assets only), while Untarnished and both DDDM variants share one CDUI-lineage script (~190
lines from stock; DDDM warm and white are script-identical, Untarnished differs from them by 13
lines, adds `topicsFadeIn`/`topicsFadeOut`). So our deltas port twice, not four times.

Most likely match for "left → stock": the DDDM lineage (DDDM, Untarnished, Dragonbreaker), then
DIR, then CDUI / Dragonborn Reskin. **Built 2026-09-08 (the first four of that priority list):
Untarnished, DDDM white, DDDM warm, NordicUI** — each verified in-engine on 1.7.104 by replaying
`replyonlineend.steps` against its own swf. **Still to build, in this order: DIR → CDUI (four
looks) → Dragonborn Reskin**; each is a `variants/<id>/` directory plus one `port.sh` run.
Caveat from the sections above: this audience is DBVO 1.x, which does not run on 1.7.104 today; it
is the downgraded-1.6.1170 population.

Three things the build pass established that the research above could not:

- **Every third-party patch dropped stock DBVO's `this.timerBool = false;`** from
  `startTopicClickedTimer`'s `voicePackID == "off"` branch — all four bases, identically. Left
  as-is that hangs the topic list when the voice is off, so `port.sh` restores it as a fixup and
  `check_offbranch` proves it survived the build. Expect the same in DIR/CDUI/Reskin.
- **One conflict per CDUI-lineage base, always the same one**: they place the copied topic text at
  `TextCopy_mc.textField._y = 3.75 - _loc3_` where stock DBVO uses `6.25`, right where our
  `CutNpcDBVOReply` line is inserted. Keep their offset, take our line.
- **The DLL and MCM half is layout-agnostic.** Every base keeps Bethesda's `_root.DialogueMenu_mc`,
  which is the only path the DLL (`dbvoOnPlayerLineEnded`) and the MCM (`dbvoPadMs`) address — so a
  variant is one swf and nothing else, and the FOMOD ships one `core/` for all of them.

## Pointers

- `mods/DBVODialogueTweaks/README.md` — the mod, its 1.7.104 verification, and the Compatibility section.
- `docs/plans/dbvo-v2-compatibility-analysis.md` — the static analysis of DBVO 2's shipped DLLs.
- `docs/plans/dbrev-comparison-analysis.md` — DBReV vs DBVO 2 vs our mod: architecture from its
  logs, the six questions (skip/audio cut, timing, dependencies, lip-sync, packs, builds), and
  why nothing is worth building. Evidence-tagged; no binary was available.
- `docs/ideas.md` — the remaining open items.
