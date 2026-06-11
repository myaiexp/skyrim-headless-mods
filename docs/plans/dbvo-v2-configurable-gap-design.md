# DBVODialogueTweaks v2 — configurable response gap (design)

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
| **ESL plugin + quest script** | Holds `wpm` + `padMs` (MCM-backed). On each dialogue-menu open, pushes both into the live swf. | New: first Papyrus/plugin tier for this mod. ESL generated headlessly via Mutagen. |
| **MCM Helper `config.json`** | Two sliders (wpm, pad ms) bound to the quest script's Float properties. | `Interface/MCM/Config/DBVODialogueTweaks/config.json`. |

### swf change

```actionscript
function startTopicClickedTimer(voicePackID)
{
   ...
   else
   {
      var wpm = this.dbvoWpm != undefined ? this.dbvoWpm : 300;     // baked default = stock
      var pad = this.dbvoPadMs != undefined ? this.dbvoPadMs : 1400;
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

### Plugin form

Minimal **ESL** (Mutagen, like the repo's other plugins): one Quest, **Start Game Enabled**, carrying
the control script. The script: two Float properties (`wpm`, `padMs`), `RegisterForMenu("Dialogue
Menu")` in `OnInit`, the `OnMenuOpen` push above. MCM Helper renders the menu from `config.json` and
persists the two values; the sliders bind to the script's properties. No new ESP records beyond the
quest.

> **Open API detail for the plan (not a design fork):** confirm MCM Helper's binding mode —
> `PropertyValueFloat` (sliders read/write the quest script's properties directly, MCM Helper
> persists) vs an INI/`ModSetting`-backed value the script mirrors into its properties. Either reaches
> the same `OnMenuOpen` push; pick the simpler one when wiring the `config.json`.

### MCM surface

| Slider | Property | Default | Range (tune in-game) |
| --- | --- | --- | --- |
| Voice-pack speed (wpm) | `wpm` | 300 | ~150–600 |
| NPC response pad (ms) | `padMs` | 1400 | 0–2500 |

Both default to stock, so installing v2 changes nothing until the user deliberately tunes — no
surprise re-pacing on first launch.

## Build & test

1. **swf** — edit `src/__Packages/DialogueMenu.as`, run `build.sh` (ffdec `-importScript`). md5-pinned
   stock baseline guards against vendoring the wrong swf.
2. **Papyrus** — compile the quest `.psc` with the repo's `tools/` compiler.
3. **Plugin** — generate the ESL via Mutagen; place `config.json` under `Interface/MCM/Config/…`.
4. **Test** in a skytest profile carrying the MCM stack (DBVO + Karat + SkyUI + MCM Helper + this
   plugin — a Papyrus/MCM feature that only manifests on top of the live order, so full-stack, not a
   bare isolated pair):
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
