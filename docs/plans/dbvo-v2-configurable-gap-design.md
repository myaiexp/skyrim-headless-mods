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

### Plugin form (MCM scaffolding — heavier than first assumed)

Researched against the MCM Helper wiki/repo: a `config.json` **alone does not register a menu**. The
quest's config script **must `extends MCM_ConfigBase`** (→ SkyUI's `SKI_ConfigBase`), and the standard
template carries a **player reference alias** with `SKI_PlayerLoadGameAlias` to re-register on load. So:

- **Binding = `PropertyValueFloat`** (resolved, not ModSetting). The slider values land directly in the
  config script's Auto properties `fWpm` / `fPadMs`; defaults come from the property **initializers**
  (`Float Property fWpm = 300.0 Auto`), so there is **no 0/uninitialized window** and **no `MCM.psc`
  call** is needed to read them. `config.json`'s `defaultValue` only feeds "reset to default" (set it to
  match). `sourceForm`/`scriptName` omitted → resolve to the config quest/script.
- **Vendor SkyUI + MCM Helper Papyrus sources** into `tools/papyrus-sources/` (`MCM_ConfigBase.psc`,
  `SKI_ConfigBase.psc` + parent chain, `SKI_PlayerLoadGameAlias.psc`) so the script compiles. Compile-
  time only — not shipped, no release-permission concern (same status as the vendored SKSE sources).
- **Extend `EspGen`** — today it emits a bare property-less, alias-less quest. The MCM quest needs its
  script to extend `MCM_ConfigBase` **and** a player ref-alias (PlayerRef `0x14`) carrying
  `SKI_PlayerLoadGameAlias`. No ESP-side property *values* are needed (the `.pex` initializers supply
  defaults); MCM Helper reads/writes the live property and persists it in the co-save.
- The `OnMenuOpen → UI.SetFloat` push lives on this same `MCM_ConfigBase`-derived script (it inherits
  the base; the push body itself uses only vanilla+SKSE+`UI.psc`).

This is a one-time toolchain investment (SkyUI sources + EspGen alias support) that also unlocks MCM for
any future mod in this repo. The transport, two-knob surface, and stock-default behavior are unchanged.

### MCM surface

| Slider | Script property | Default (initializer) | Range (tune in-game) |
| --- | --- | --- | --- |
| Voice-pack speed (wpm) | `fWpm` | 300.0 | ~150–600 |
| NPC response pad (ms) | `fPadMs` | 1400.0 | 0–2500 |

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
