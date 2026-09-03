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

## Why DBVO 1.x is dead on 1.7.104

Established in-engine 2026-09-03 (see `mods/DBVODialogueTweaks/README.md` → Compatibility). SKSE
2.3.1 refuses both SKSE plugins DBVO 1.x's `DBVO_Script_MCM` calls into, before loading them:
`ConsoleUtilSSE.dll` 1.5.1.0 (*"must be recompiled for new address library"*) and `JContainers64.dll`
(*"disabled, incompatible with current version of the game"*). ConsoleUtil speaks the line,
JContainers reads the voice-pack settings — so DBVO 1.x produces no player voice at all.

Newest upstream at the time of writing: ConsoleUtilSSE NG **1.6.1** (2026-08-22, its file page says
*"Confirmed working on 1.7.99"*), and for JContainers a **GitHub-only pre-release v4.3.2**
(2026-08-29, *"SKSE 2.3.1 / SAE 1.7.104"*) — Nexus still serves 4.2.13.1 (2026-07-04), which
predates the patch. Neither is installed here and the Nexus API is read-only, so a session cannot
fetch them; whether the pair revives DBVO 1.x on 1.7.104 is untested and not worth a session.

## Why not take DBVO 1.x over

DBReV **is** that takeover, shipped. From its own page (quoted, not verified in-engine by us):

- *"full backwards compatibility support for DBVO 1.0 voice packs (but not DBVO 2)"* — it eats the
  entire legacy pack ecosystem, which was the whole argument for reviving 1.x.
- *"built around an SKSE plugin to calculate dialogue timings in real time"* — **that is our
  headline feature, native.**
- Volume gain to 300%, pitch, reverb, four audio post-processing effects — covers our volume slider
  and then some.
- SkyUI **MCM**, explicitly *"as some people reported having issues with the SKSE Menu Framework
  used by DBVO 2"*.
- Fixes the long-dialogue-path CTD (legacy packs name files after full dialogue lines, which blows
  the Windows 260-char path limit).
- Author-tested on 1.5.97, 1.6.1170 and **1.7.104** (the page text says "1.7.140" — a typo; the
  1.5 file description and the author's sticky both say 1.7.104). On 1.7.x it needs a
  **GitHub pre-release JContainers** (v4.3.2) — JContainers is still a hard requirement.
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

## Pointers

- `mods/DBVODialogueTweaks/README.md` — the mod, its 1.7.104 verification, and the Compatibility section.
- `docs/plans/dbvo-v2-compatibility-analysis.md` — the static analysis of DBVO 2's shipped DLLs.
- `docs/plans/dbrev-comparison-analysis.md` — DBReV vs DBVO 2 vs our mod: architecture from its
  logs, the six questions (skip/audio cut, timing, dependencies, lip-sync, packs, builds), and
  why nothing is worth building. Evidence-tagged; no binary was available.
- `docs/ideas.md` — the remaining open items.
