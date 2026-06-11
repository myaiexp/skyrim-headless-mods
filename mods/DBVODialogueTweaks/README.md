# DBVODialogueTweaks

A small set of tweaks to **[Dragonborn Voice Over (DBVO)](https://www.nexusmods.com/skyrimspecialedition/mods/84329)**
dialogue pacing, built in phases:

| Phase  | Feature                                                                       | Tier                | Status                                                            |
| ------ | ----------------------------------------------------------------------------- | ------------------- | ----------------------------------------------------------------- |
| **v1** | **Manual player-line skip** (E / left-click), vanilla-style                   | swf only            | **shipped** — design: `docs/plans/dbvo-dialogue-tweaks-design.md` |
| **v2** | Configurable response gap (pad ms + ms/word) via MCM                          | swf + Papyrus + MCM | **shipped** — verified in-game                                    |
| v3     | SKSE C++: cut player voice on skip; optional exact `.fuz`-duration scheduling | SKSE (`plugin/`)    | scoped below, not started                                         |

v1 is fully specified in the design doc above. The rest of this README is the **v2** scope doc
(the configurable gap); v3 is the **Tier 3** section near the bottom.

(Renamed from `DBVOResponseGap` once the skip feature broadened it past just the gap.)

---

## v2 — configurable response gap

Make the **gap between the player's voiced line and the NPC's reply** configurable, so
fast voice packs don't leave dead air (or, over-corrected, overlap). Born out of installing DBVO +
the **Karat** AI voice pack: Karat speaks far faster than DBVO assumes, so DBVO's fixed timing
mis-paces every line.

## The problem, precisely

DBVO has **no DLL of its own**. The player line is spoken via ConsoleUtil `Player.SpeakSound`
(no "finished" callback), and the **NPC reply is gated by a timer in `Interface/dialoguemenu.swf`**
(AS2). The relevant function, `DialogueMenu.startTopicClickedTimer`:

```actionscript
// stock DBVO
_loc3_ = this.TopicListHolder.List_mc.selectedEntry.text;          // the player line text
_loc4_ = Math.round(_loc3_.split(" (")[0].split(" ").length        // word count (strips "(Persuade)" etc.)
                    * 60 / 300 * 1000) + 1400;                     // words × 200ms (300 wpm) + 1400ms pad
this.timer = setTimeout(this, "topicClicked", _loc4_);             // topicClicked → GameDelegate "TopicClicked" → NPC replies
```

So the NPC reply is delayed by **`wordCount × 200 ms` (a 300-wpm length estimate) + a flat
`1400 ms` pad**. Two failure modes:

- The `1400` pad is pure dead air on top of the estimate.
- The `wordCount × 200 ms` estimate assumes 300 wpm. Real voice packs vary wildly. **Karat is much
  faster** — e.g. _"where can I get a drink"_ = 6 words ⇒ 1200 ms budgeted, but the clip finishes in
  under a second. A single fixed pad **cannot** track this per-line: short of reading the real audio
  length, any constant is wrong for some lines.

Reference points already built (in the _staging_ repo, `~/Downloads/skyrim-mods/`):

- **stock** swf — md5 `b1f70c58…` — `00-docs/overrides/2026-06-09-DBVO-instant-skip/`
- **"Instant Skip"** (Nexus 140682) — md5 `9c93e72d…` — sets the timeout to `1` ⇒ NPC fires
  immediately ⇒ player+NPC **overlap**. Also drops the `bAllowProgress` skip-cooldown (unrelated).
- **custom `+900`** build — `00-docs/custom-tweaks/dbvo-npc-gap/` (swf + edited `DialogueMenu.as`) —
  pad cut `1400 → 900`. Better, but still a fixed pad, so still mis-paces. Proves the recompile path.

## Tier 2 — configurable (this mod's target)

**Designed:** `docs/plans/dbvo-v2-configurable-gap-design.md` (read that for the authoritative spec).
Summary of the chosen shape:

1. **swf**: `startTopicClickedTimer` reads `this.dbvoMsPerWord` / `this.dbvoPadMs` (baked defaults `200`
   / `1400` = stock) in place of the literals — the delay is `round(words × msPerWord) + pad`. Recompile
   with **ffdec**.
2. **Papyrus**: an **independent** ESL quest script — we do _not_ touch DBVO's script. It pushes the
   two values onto the live menu (`UI.SetFloat("Dialogue Menu", "_root.DialogueMenu_mc.dbvoMsPerWord", …)`)
   **on each dialogue-menu open** (the swf instance is recreated per conversation, so the push must
   repeat). The rejected alternative — routing values through DBVO's own `UI.Invoke` call — would mean
   editing DBVO's script for no benefit (timer's in the swf either way); see the design doc.
3. **MCM** (**native SkyUI**, _not_ MCM Helper): the quest script `extends SKI_ConfigBase` and builds
   two sliders in Papyrus — **Per-word delay (ms)** and **NPC response pad (ms)** — calibrated live. MCM
   Helper is only an optional JSON layer on top of SkyUI; for a 2-slider menu we drop it to keep the
   dependency footprint to SkyUI alone. Vanilla skip behavior kept (Instant Skip's `bAllowProgress`
   removal is _not_ bundled). Needs a player ref-alias (`SKI_PlayerLoadGameAlias`) for reload re-reg.

> **Shipped as ms/word, not wpm.** The per-word knob is exposed directly as **ms per word** (default
> `200` = stock's 300 wpm, since `60/300×1000 = 200`), not words-per-minute — more intuitive ("6 words ×
> 200 ms") and it drops the division. _Lower = faster reply._ The design/plan docs were written around a
> `wpm` slider; the only change is the unit/reciprocal. Stock-formula mentions of "300 wpm" below still
> correctly describe DBVO's _original_ code.

## Tier 3 — the actually-correct fix (in-place, `plugin/`)

A fixed pad and a wpm guess are both approximations. An **SKSE C++ plugin** (like the others in
`mods/`) could read the real **`.fuz`/`.xwm` duration** of the player line and schedule the NPC
reply to land exactly when it ends — zero dead air, zero overlap, on every line, no calibration.
This eliminates the word-count heuristic entirely and would supersede tier 2. Track separately if/when
tier 2 isn't good enough.

## Tooling / deps

- **ffdec** (Flash decompiler) — `-export script <out> <swf>` / `-importScript <in> <out> <scriptsdir>`
  (AS2 recompiles). Run via `java -jar ffdec.jar …`.
- Papyrus compiler — already in this repo (`tools/`). SkyUI SDK `.psc` sources vendored under
  `tools/papyrus-sources/skyui/` (compile-time only) to compile against `SKI_ConfigBase`.
- **SkyUI** (runtime dep, already in the user's load order). **No MCM Helper** — native SkyUI MCM.
- Builds on the standard DBVO requirements (SKSE, PapyrusUtil, ConsoleUtilSSE NG).

## Permissions

DBVO's Nexus page **grants modify-and-release**: _"You are allowed to modify my files and release bug
fixes or improve on the features so long as you credit me as the original creator."_ So shipping the
**modified swf** (a derivative of DBVO's asset) publicly is fine **with credit to the DBVO author** —
no separate ask needed, and a script-only patch is not required. (This repo still stays private re:
bundled **Bethesda** Papyrus sources — a separate Bethesda-asset concern, unrelated to DBVO.)

**DBVO is a frozen target** — last updated ~3 years ago. So the md5-pinned stock swf in `build.sh`
won't drift from an updated upstream (there is none), the modified swf we ship won't bitrot, and a
public release stays low-maintenance. Don't re-derive these two facts each session — they're settled here.

## Building v2

v2 is fully designed and planned — execute from the plan, don't re-derive:

- **Design (rationale):** `docs/plans/dbvo-v2-configurable-gap-design.md`
- **Plan (6 tasks, step-by-step):** `docs/plans/dbvo-v2-configurable-gap-plan.md`

Shape in one breath: swf reads `dbvoMsPerWord`/`dbvoPadMs` (defaults = stock) → an independent
`SKI_ConfigBase` quest (native SkyUI MCM, player ref-alias for reload) pushes the slider values onto
the live menu via `UI.SetFloat` on each dialogue-menu open. Vendor SkyUI `.psc` sources + extend
`EspGen` for the player alias are the one-time toolchain steps (Tasks 1–2).
