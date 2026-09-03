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
| **Dragonborn ReVoiced (DBReV)** | 184221 | 1.5, 2026-09-02, Raynor1511 | SKSE DLL + SkyUI MCM, own lip-sync driver | **No.** Its page: *"Not compatible with any patches targeting the legacy DBVO mod … remove them if you have them!"* |

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

Newest upstream at the time of writing: ConsoleUtilSSE NG **1.6.1** (2026-08-22), JContainers SE
**4.2.13.1** (2026-07-04). Neither is installed here and the Nexus API is read-only, so a session
cannot fetch them; JContainers' newest predates the 1.7.104 patch, so it may not be enough.

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
- Claims compatibility with 1.5.97, 1.6.1170 and **1.7.140** — ahead of the build we test on.
- Built on alandtse's CommonLibSSE-NG v4.39.3, forked to support 1.7.x.

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

## The one thing still possibly ours

**Clean audio cut on skip.** DBVO 2 structurally cannot do it — it never holds a sound handle (no
sound RTTI in either build, neither references Address Library 36541/37542, `FireResponse` makes no
audio call; see `docs/plans/dbvo-v2-compatibility-analysis.md`). Our stack holds that handle because
it hooks `Actor::SpeakSoundFunction`.

**Whether DBReV closes that gap is UNKNOWN and is the open question.** It advertises audio effects,
volume gain and its own lip-sync driver, and claims compatibility with SmartTalk's dialogue-skip
features — but says nothing about cutting the player's line audio on skip. If the gap is real, the
move is to offer it upstream (to Raynor1511 or MathiewMay), not to ship a competing framework.

## Pointers

- `mods/DBVODialogueTweaks/README.md` — the mod, its 1.7.104 verification, and the Compatibility section.
- `docs/plans/dbvo-v2-compatibility-analysis.md` — the static analysis of DBVO 2's shipped DLLs.
- `docs/ideas.md` — the remaining open items.
