# DBVODialogueTweaks v2 — configurable response gap (design)

> **Shipped note:** the per-word knob ships as **ms-per-word** (default `200`), not the `wpm` this doc
> discusses — same lever, reciprocal unit (`200 ms/word` ≡ `300 wpm`), picked during bring-up as more
> intuitive and division-free. The two-knob rationale below is unchanged; mentally read "wpm slider" as
> "ms/word slider". Shipped swf delay: `round(words × msPerWord) + pad`.
>
> **Path note (post-2026-06-12 reorg):** the v3 row's `plugins/` is now `mods/<Mod>/plugin/` +
> `tools/skse/`. Kept as written.

**Mod:** `mods/DBVODialogueTweaks/` — extends the shipped v1 (manual player-line skip, swf-only).
v2 makes DBVO's **NPC-reply delay** configurable at runtime via two MCM sliders, so fast voice
packs (e.g. Karat) stop being mis-paced by DBVO's fixed `300 wpm + 1400 ms` assumption.

| Phase  | Feature                                                       | Tier                | Status                         |
| ------ | ------------------------------------------------------------- | ------------------- | ------------------------------ |
| v1     | Manual player-line skip (E / left-click)                      | swf only            | shipped                        |
| **v2** | **Configurable response gap — `wpm` + `padMs` via MCM**       | swf + Papyrus + MCM | **this design — building now** |
| v3     | SKSE C++: `.fuz`-duration scheduling / player-voice audio cut | SKSE (`plugins/`)   | deferred (correct-but-hard)    |

Scope for now is **build-for-yourself-first**: prove the mechanism in the live load order
(DBVO + Karat). Public Nexus release is a tracked follow-up (`docs/ideas.md`), not part of v2 —
DBVO's page already grants modify-and-release with credit, and DBVO is a frozen target (see the mod
`README.md` "Permissions"), so release stays a clean, low-maintenance separate pass.

## Goal

Stock DBVO delays the NPC reply by a fixed estimate, computed in the **swf** (not DBVO's Papyrus):

```actionscript
// DialogueMenu.startTopicClickedTimer — stock formula, src/__Packages/DialogueMenu.as:269
_loc4_ = Math.round(_loc3_.split(" (")[0].split(" ").length * 60 / 300 * 1000) + 1400;
//                  └ word count (strips "(Persuade)" etc.) ┘   └ 300 wpm ┘        └ flat pad ┘
this.timer = setTimeout(this, "topicClicked", _loc4_);   // topicClicked → GameDelegate "TopicClicked" → NPC replies
```

The two magic numbers each fail differently:

- **`300` (wpm)** assumes a talk-speed. Real packs vary; **Karat is much faster**, so the per-word
  budget is too long on every line. This is the lever for "my pack talks faster/slower than DBVO assumes."
- **`1400` (pad)** is flat dead air on top of the estimate — the lever for a constant tail offset.

v2 replaces both literals with values the user controls live. A single pair still can't track real
per-line audio length (that's the v3 `.fuz` fix) — v2 is **calibration of an accepted approximation**,
better than stock on a uniformly fast/slow pack.

## Architecture — three pieces

| Piece | Role | Notes |
| --- | --- | --- |
| **swf** (`src/__Packages/DialogueMenu.as`) | `startTopicClickedTimer` reads `this.dbvoWpm` / `this.dbvoPadMs` instead of the `300` / `1400` literals. Baked-in defaults = stock. | Same `build.sh` / ffdec path as v1. v1's skip is untouched (additive). |
| **ESL plugin + quest script** | Holds `fWpm` + `fPadMs` in its own Auto properties; renders the two MCM sliders itself; on each dialogue-menu open pushes both into the live swf. | New: first Papyrus/plugin tier for this mod. ESL generated headlessly via Mutagen. |
| **Native SkyUI menu (in the config script)** | The script `extends SKI_ConfigBase` and builds the two sliders in `OnPageReset` — **no MCM Helper, no `config.json`**. | SkyUI only (already a universal dep). |

### swf change

```actionscript
function startTopicClickedTimer(voicePackID)
{
   ...
   else
   {
      var wpm = this.dbvoWpm > 0 ? this.dbvoWpm : 300;       // guard: 0/undefined -> stock default
      var pad = this.dbvoPadMs >= 0 ? this.dbvoPadMs : 1400; // (> 0 also blocks a 0-push divide-by-zero)
      _loc3_ = this.TopicListHolder.List_mc.selectedEntry.text;
      _loc4_ = Math.round(_loc3_.split(" (")[0].split(" ").length * 60 / wpm * 1000) + pad;
      this.timer = setTimeout(this, "topicClicked", _loc4_);
      this.skipArmedAt = getTimer();
   }
}
```

Defaults baked in mean: if Papyrus never pushes (mod's plugin disabled, or push lands late), the swf
behaves exactly like stock+v1. No crash path, no dependency on the push having happened.

### Transport — push on menu-open (the load-bearing decision)

DBVO's **own** Papyrus calls `startTopicClickedTimer(voicePackID)` with a single argument, and we
don't control that call. So we don't touch DBVO's script at all. Instead **our** quest script writes
the values onto the live menu as members the swf reads:

```papyrus
Event OnMenuOpen(String menuName)        ; RegisterForMenu("Dialogue Menu") in OnInit
   UI.SetFloat("Dialogue Menu", "_root.DialogueMenu_mc.dbvoWpm",   wpm)
   UI.SetFloat("Dialogue Menu", "_root.DialogueMenu_mc.dbvoPadMs", padMs)
EndEvent
```

**Why push on every open, not once / not on slider-change:** the dialogue-menu swf instance is
recreated each conversation, so any value set on a previous instance is gone. The values must be
re-pushed each open. A slider change updates the property; the *next* dialogue open pushes the new
value (MCM and dialogue are never open simultaneously, so there's no mid-conversation re-push case).

**Timing margin:** the swf *reads* the members only at `startTopicClickedTimer` time — i.e. after the
player clicks a topic, many frames after the menu opened. So even if the `UI.SetFloat` lands a frame
or two after `OnMenuOpen`, it's still far ahead of the read. The baked defaults cover any gap.

**Rejected alternative — edit DBVO's Papyrus to pass the values through its own invoke.** This was
dismissed on *work-required* grounds, not the stale-mod update risk: the timer lives in the swf, so we
edit the swf either way; routing values through DBVO's script would *additionally* require teaching it
where to read our settings — strictly more coupling and more code for zero benefit. Push-on-open keeps
our plugin fully decoupled from DBVO's scripts.

### Plugin form (native SkyUI MCM — no MCM Helper)

MCM Helper is **not** a SkyUI alternative — it's an optional JSON-authoring layer *on top of* SkyUI
(`MCM_ConfigBase extends SKI_ConfigBase`). Since this is a tiny 2-slider menu and we'd like a lean
dependency footprint for an eventual public release, we drop MCM Helper and write the menu the classic
SkyUI way. This removes a runtime dependency and a vendored source tree; the cost is ~50 lines of
standard SkyUI menu Papyrus.

- **Config script `extends SKI_ConfigBase`.** It holds `fWpm` / `fPadMs` as its own Auto properties
  (initializers `= 300.0` / `= 1400.0` = stock) and builds the sliders itself: `OnConfigInit` sets
  `ModName` + `Pages`; `OnPageReset` calls `AddSliderOption` ×2 (storing the option-IDs);
  `OnOptionSliderOpen` sets each slider's range/default/value; `OnOptionSliderAccept` stores the new
  value. **No `config.json`, no MCM Helper.** `ModName`/`Pages`/defaults are all set in Papyrus, so the
  plugin needs **no ESP-set property values**.
- **Player ref-alias is still required** (this is a SkyUI base-class fact, not an MCM Helper one):
  `SKI_ConfigBase.OnInit → OnGameReload` registers the menu only on *first* run; on every later save
  reload `OnInit` does not re-fire, so a player alias carrying `SKI_PlayerLoadGameAlias` must call
  `OwningQuest.OnGameReload()` each load or the MCM entry goes stale after a reload. So the quest needs
  **one player ReferenceAlias** (ForcedReference → PlayerRef `0x14`) with that alias script.
- **Vendor SkyUI Papyrus sources only** into `tools/papyrus-sources/skyui/` (`SKI_ConfigBase`,
  `SKI_QuestBase`, `SKI_ConfigManager`, `SKI_PlayerLoadGameAlias`, + the rest of the SkyUI SDK Source
  tree to satisfy the type graph). Compile-time only — not shipped (SkyUI provides the `.pex` at
  runtime), same private-repo status as the vendored SKSE sources. **No MCM Helper sources.**
- **Extend `EspGen`** to emit the quest's player ref-alias (PlayerRef `0x14` + `SKI_PlayerLoadGameAlias`)
  alongside the hosted config script. Unchanged by the MCM-Helper drop — the alias requirement is
  SkyUI's.
- **Reload + push:** override `OnGameReload()` (call `Parent.OnGameReload()` first, then
  `RegisterForMenu("Dialogue Menu")` so registration re-arms each load); `OnMenuOpen` does the
  `UI.SetFloat` push. The push body uses only vanilla + SKSE + `UI.psc`.

One-time toolchain investment (SkyUI sources + EspGen alias support) that also unlocks SkyUI MCM for any
future mod here. The transport, two-knob surface, and stock-default behavior are unchanged.

### MCM surface

| Slider | Script property | Default (initializer) | Range (tune in-game) |
| --- | --- | --- | --- |
| Voice-pack speed (wpm) | `fWpm` | 300.0 | 150–600 |
| NPC response pad (ms) | `fPadMs` | 1400.0 | 0–2500 |

Built in Papyrus via `AddSliderOption` / `SetSliderDialog*` / `SetSliderOptionValue` (no `config.json`).

Both default to stock, so installing v2 changes nothing until the user deliberately tunes — no
surprise re-pacing on first launch.

## Build & test

1. **swf** — edit `src/__Packages/DialogueMenu.as`, run `build.sh` (ffdec `-importScript`). md5-pinned
   stock baseline guards against vendoring the wrong swf.
2. **Papyrus** — compile the `SKI_ConfigBase`-derived quest `.psc` with the repo's `tools/` compiler
   (against the vendored SkyUI sources).
3. **Plugin** — generate the ESL via Mutagen (quest + player ref-alias). No `config.json` — the menu is
   built in the script.
4. **Test** in a skytest profile carrying the MCM stack (DBVO + Karat + SkyUI + this plugin — a
   Papyrus/MCM feature that only manifests on top of the live order, so full-stack, not a bare isolated
   pair):
   - Open MCM → move a slider → open a merchant → confirm the NPC-reply gap visibly tracks the slider.
   - Papyrus log shows the pushed value arriving (add a one-line `Debug.Trace` on push during bring-up).
   - v1 skip (E / left-click) still works — additive change didn't regress it.

**Done when:** reproduced in-game — moving the wpm and pad sliders measurably changes the player→NPC
gap on a known line, defaults reproduce stock pacing, and v1 skip is intact.

## Why this shape

- **swf reads pushed members; baked defaults = stock.** Keeps the swf self-contained and crash-free
  with or without the plugin, and makes the plugin a pure additive overlay.
- **Push-on-open, not DBVO-script edit.** Least-work path that leaves DBVO untouched (see Transport).
- **Two knobs, not one.** The formula genuinely has two independent constants; one slider can't cover
  both "fast pack" (wpm) and "flat tail" (pad). Confirmed with the user.
- **Calibration, not correctness.** A fixed wpm+pad can't track real per-line audio — that's the v3
  `.fuz`-duration fix, deferred because the SKSE tier is high-friction and the user wants the
  ship-now control first.
