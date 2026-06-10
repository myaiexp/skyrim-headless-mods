# DBVODialogueTweaks

A small set of tweaks to **[Dragonborn Voice Over (DBVO)](https://www.nexusmods.com/skyrimspecialedition/mods/84329)**
dialogue pacing, built in phases:

| Phase  | Feature                                                                       | Tier                | Status                                                                 |
| ------ | ----------------------------------------------------------------------------- | ------------------- | ---------------------------------------------------------------------- |
| **v1** | **Manual player-line skip** (E / left-click), vanilla-style                   | swf only            | **building now** — design: `docs/plans/dbvo-dialogue-tweaks-design.md` |
| v2     | Configurable response gap + speed (pad ms + wpm) via MCM                      | swf + Papyrus + MCM | scoped below, not started                                              |
| v3     | SKSE C++: cut player voice on skip; optional exact `.fuz`-duration scheduling | SKSE (`plugins/`)   | scoped below, not started                                              |

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

Expose the timing instead of hardcoding it. The clean hook already exists: DBVO's Papyrus script
calls into the swf via `UI.Invoke…("_root.DialogueMenu_mc.startTopicClickedTimer", voicePackId)`.

1. **swf**: extend `startTopicClickedTimer` to accept `padMs` and `wpm` (or a precomputed delay)
   and use them in place of the literal `1400` / `300`. Recompile with **ffdec**.
2. **Papyrus**: small script holding the settings; pass them on that same `UI.Invoke…` call
   (array invoke, or set props on `_root.DialogueMenu_mc` just before).
3. **MCM** (MCM Helper): two sliders — **NPC response pad (ms)** and **words-per-minute** — so the
   user calibrates to their voice pack live. Keep vanilla skip behavior (don't bundle Instant Skip's
   `bAllowProgress` removal; make it a separate optional toggle if wanted).

Open question: cleanest swf↔Papyrus value transport (extra `UI.InvokeNumberA` args vs. writing a
shared property the swf reads at `startTopicClickedTimer` time). Resolve during design.

## Tier 3 — the actually-correct fix (separate, `plugins/`)

A fixed pad and a wpm guess are both approximations. An **SKSE C++ plugin** (like the others in
`plugins/`) could read the real **`.fuz`/`.xwm` duration** of the player line and schedule the NPC
reply to land exactly when it ends — zero dead air, zero overlap, on every line, no calibration.
This eliminates the word-count heuristic entirely and would supersede tier 2. Track separately if/when
tier 2 isn't good enough.

## Tooling / deps

- **ffdec** (Flash decompiler) — `-export script <out> <swf>` / `-importScript <in> <out> <scriptsdir>`
  (AS2 recompiles). Run via `java -jar ffdec.jar …`.
- Papyrus compiler — already in this repo (`tools/`).
- **MCM Helper** + SkyUI (runtime deps, already in the user's load order).
- Builds on the standard DBVO requirements (SKSE, PapyrusUtil, ConsoleUtilSSE NG).

## Permissions

The swf is a **derivative of DBVO's** asset. Personal use is fine; a public release needs the DBVO
author's permission (or ship a script-only patch). Same reason this repo stays private re: bundled
Bethesda Papyrus sources — don't redistribute the swf.

## First steps when starting (v2)

1. Extend the v1 design into a v2 section / new design doc (transport mechanism, MCM layout, what the
   swf exposes). v1 already establishes the swf rebuild path it shares.
2. Decompile stock swf, prototype the parameterized `startTopicClickedTimer`.
3. Stub the Papyrus MCM, wire one slider end-to-end, verify the value reaches the swf (Papyrus log).
