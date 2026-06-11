# DBVODialogueTweaks v2 — configurable response gap (implementation plan)

**Goal:** Make DBVO's NPC-reply delay tunable live via two MCM sliders (words-per-minute + pad ms),
replacing the swf's hardcoded `300 wpm + 1400 ms` estimate.

**Architecture:** The swf reads two members (`this.dbvoWpm` / `this.dbvoPadMs`, baked defaults =
stock) when arming its reply timer. An independent plugin — one Start-Game-Enabled quest whose script
`extends SKI_ConfigBase` (native SkyUI MCM, **no MCM Helper**) — holds the two values in its own Auto
properties, builds the two sliders in Papyrus, and **pushes the values onto the live dialogue menu via
`UI.SetFloat` each time the menu opens**. DBVO's own scripts are never touched.

**Tech stack:** ffdec (AS2 swf recompile), Papyrus compiler (wine, in-repo), Mutagen (headless ESP
gen via `tools/EspGen`), SkyUI (runtime dep + compile-time Papyrus sources to vendor; no MCM Helper).

**Design:** `docs/plans/dbvo-v2-configurable-gap-design.md` (spec-reviewed, approved).

---

## File structure

| File | Responsibility | New/Mod |
| --- | --- | --- |
| `tools/papyrus-sources/skyui/**` | Vendored SkyUI SDK `Source/*.psc` (compile-time only; no MCM Helper) | New |
| `tools/compile-papyrus.sh` | Add `skyui/` to the compiler import path | Modify |
| `tools/EspGen/Program.cs` | Optional player ref-alias (PlayerRef + alias script) for the config quest | Modify |
| `mods/DBVODialogueTweaks/src/__Packages/DialogueMenu.as` | swf reads `dbvoWpm`/`dbvoPadMs` instead of literals | Modify |
| `mods/DBVODialogueTweaks/src/scripts/DBVODialogueTweaksMCM.psc` | `SKI_ConfigBase` config script: properties + sliders + menu-open push | New |
| `mods/DBVODialogueTweaks/build.sh` | Build swf + compile psc + gen esp + assemble `build/` tree | Modify |

Final installable `build/` layout (what `--install` copies into `Data/`):
```
build/
  DBVODialogueTweaks.esp                       (EspGen — quest + player alias)
  Interface/dialoguemenu.swf                   (ffdec)
  Scripts/DBVODialogueTweaksMCM.pex            (compile-papyrus)
```
We compile **against** SkyUI `.psc` but ship none of their `.pex` — `SKI_ConfigBase.pex`,
`SKI_QuestBase.pex`, `SKI_PlayerLoadGameAlias.pex`, `SKI_ConfigManager.pex` come from the user's
installed SkyUI. **No `config.json`** — the menu is built in our script.

---

### Task 1: Vendor SkyUI Papyrus sources, wire the import path  [Mode: Direct]

**Files:**
- Create: `tools/papyrus-sources/skyui/` (bulk-copied SkyUI SDK `.psc` Source tree)
- Modify: `tools/compile-papyrus.sh`

**What:**
- Locate the SkyUI **SDK** / SkyUI source (modders-resource) download. Check the staging repo first
  (`~/Downloads/skyrim-mods/` — `02-tools/` or the installed-active dirs); else note it must be fetched
  (authoritative copies: `github.com/schlangster/skyui` under `dist/Data/Scripts/Source/`). Extract its
  `Scripts/Source/*.psc` into `tools/papyrus-sources/skyui/`. Bulk-copy the whole Source tree, don't
  cherry-pick — the type graph pulls in `SKI_QuestBase`, `SKI_ConfigManager`, etc. **No MCM Helper.**
- Add the dir to the compiler import path in `compile-papyrus.sh` (after the mod src, before `skse`):
  `…-i="$(winpath "$SRC_DIR");$(winpath "$SRCROOT/skyui");$(winpath "$SRCROOT/skse");…"`.

**Constraints:**
- These are **compile-time only**, like the vendored SKSE sources — never shipped. Same private-repo
  status (don't redistribute), unrelated to the DBVO release-permission.

**Verification:**
- `ls tools/papyrus-sources/skyui/SKI_ConfigBase.psc tools/papyrus-sources/skyui/SKI_QuestBase.psc tools/papyrus-sources/skyui/SKI_PlayerLoadGameAlias.psc` — all exist.
- Smoke-compile a throwaway `Scriptname _T extends SKI_ConfigBase` via `compile-papyrus.sh` → produces
  `_T.pex` with no missing-type errors. Delete the throwaway after.

**Commit after passing.**

---

### Task 2: Extend EspGen — player ref-alias for MCM quests  [Mode: Delegated]

**Files:**
- Modify: `tools/EspGen/Program.cs`

**Contract:**
- New optional CLI form (back-compatible — existing 3/4-arg calls unchanged):
  ```
  EspGen <out.esp> <QuestEditorID> <ScriptName> [FullName] [--player-alias <AliasScriptName>]
  ```
- When `--player-alias <AliasScriptName>` is present, add to the quest **one ReferenceAlias** (ID 0,
  Name e.g. `"PlayerAlias"`) whose **Forced Reference = PlayerRef** (`Skyrim.esm:0x00000014`) and whose
  alias VMAD hosts a single local script named `<AliasScriptName>`. This adds `Skyrim.esm` as a master
  (expected). The quest's own hosted script (`<ScriptName>`) is added exactly as today.
- No script **property** entries are written (the `.pex` Auto-initializers supply defaults; the live
  values are stored in the `SKI_ConfigBase` script's properties and persisted in the co-save).

**Constraints:**
- ESL/light-master flag is **optional** — `.esp` output is acceptable; match repo convention. If
  trivial in Mutagen, flag it light master.
- Existing behavior (no `--player-alias`) must be byte-for-byte unchanged for old invocations.

**Verification:**
- `cd tools/EspGen && dotnet run -- /tmp/_t.esp TestQuest TestScript "Test" --player-alias SKI_PlayerLoadGameAlias`
- Re-read `/tmp/_t.esp` with Mutagen (a few lines in `Program.cs` under a `--verify` path, or a scratch
  LINQPad-style snippet) and assert: 1 Quest, `StartGameEnabled`, hosts `TestScript`; 1 alias with
  ForcedReference → `0x14` and script `SKI_PlayerLoadGameAlias`; masters = `{Skyrim.esm}`.
- Confirm old form still works: `dotnet run -- /tmp/_t2.esp Q S` → 1 quest, no alias, no masters.

**Commit after passing.**

---

### Task 3: swf — parameterize `startTopicClickedTimer`  [Mode: Direct]

**Files:**
- Modify: `mods/DBVODialogueTweaks/src/__Packages/DialogueMenu.as` (the `else` branch of
  `startTopicClickedTimer`, currently lines 266–272)

**Contract (exact edit):**
```actionscript
   else
   {
      var wpm = this.dbvoWpm > 0 ? this.dbvoWpm : 300;          // guard: 0/undefined -> stock
      var pad = this.dbvoPadMs >= 0 ? this.dbvoPadMs : 1400;
      _loc3_ = this.TopicListHolder.List_mc.selectedEntry.text;
      _loc4_ = Math.round(_loc3_.split(" (")[0].split(" ").length * 60 / wpm * 1000) + pad;
      this.timer = setTimeout(this,"topicClicked",_loc4_);
      this.skipArmedAt = getTimer();
   }
```

**Constraints:**
- The `wpm > 0` guard is load-bearing: a 0 push would make `60/wpm` divide by zero. Defaults reproduce
  stock exactly (`300`/`1400`).
- For a self-documenting swf-side contract, declare the two members alongside the existing
  `var skipArmedAt;` (line 13): `var dbvoWpm;` / `var dbvoPadMs;`. Not required for correctness (AS2
  reads undeclared members as `undefined`, which the guards handle), but it makes the pushed interface
  visible in the class.
- v1's skip path (`trySkipPlayerLine`, `skipArmedAt`) and every other class in the swf stay untouched —
  this is purely the timing literals (plus the two member decls above).

**Verification:**
- `./build.sh` → succeeds; reported md5 ≠ stock `b1f70c58…`.
- `ffdec -export script /tmp/chk build/Interface/dialoguemenu.swf` then grep the exported
  `DialogueMenu.as` for `dbvoWpm` and confirm the `* 60 / wpm * 1000) + pad` line is present.

**Commit after passing.**

---

### Task 4: Papyrus config script  [Mode: Delegated]

**Files:**
- Create: `mods/DBVODialogueTweaks/src/scripts/DBVODialogueTweaksMCM.psc`

**Contract** — native SkyUI MCM (`SKI_ConfigBase`), menu built in Papyrus, no `config.json`:
```papyrus
Scriptname DBVODialogueTweaksMCM extends SKI_ConfigBase

Float Property fWpm   = 300.0  Auto    ; stock default
Float Property fPadMs = 1400.0 Auto    ; stock default

Int _wpmOID    ; option-IDs captured in OnPageReset for dispatch
Int _padOID
```
Behavior (standard SkyUI menu lifecycle — see SkyUI `ExampleConfigMenu.psc`):
- `Event OnConfigInit()` — set `ModName = "DBVO Dialogue Tweaks"` and `Pages = new String[1]` /
  `Pages[0] = "Timing"`. (Display name + page tabs in Papyrus → no ESP property values needed.)
- `Event OnPageReset(String page)` — `SetCursorFillMode(TOP_TO_BOTTOM)`; then
  `_wpmOID = AddSliderOption("Voice-pack speed", fWpm, "{0} wpm")` and
  `_padOID = AddSliderOption("NPC response pad", fPadMs, "{0} ms")`.
- `Event OnOptionSliderOpen(Int oid)` — dispatch on `oid`:
  `_wpmOID` → range `150..600`, default `300`, interval `10`, start `fWpm`;
  `_padOID` → range `0..2500`, default `1400`, interval `25`, start `fPadMs`
  (via `SetSliderDialogRange/DefaultValue/Interval/StartValue`).
- `Event OnOptionSliderAccept(Int oid, Float value)` — store into `fWpm`/`fPadMs` by `oid`, then
  `SetSliderOptionValue(oid, value, "{0} wpm"|"{0} ms")` to refresh the displayed text.
- `Event OnGameReload()` — call `Parent.OnGameReload()` **first** (preserves SkyUI's per-load
  re-registration), then `RegisterForMenu("Dialogue Menu")` so the push re-arms each load. Do **not**
  override raw `OnInit` (SkyUI bootstraps there). The player alias from Task 2 is what re-invokes
  `OnGameReload` on every load.
- `Event OnMenuOpen(String menuName)`:
  ```
  if menuName == "Dialogue Menu"
     If fWpm > 0
        UI.SetFloat("Dialogue Menu", "_root.DialogueMenu_mc.dbvoWpm",   fWpm)
        UI.SetFloat("Dialogue Menu", "_root.DialogueMenu_mc.dbvoPadMs", fPadMs)
     EndIf
  endif
  ```

**Constraints:**
- The slider values are stored in this script's own `fWpm`/`fPadMs` properties — **no `MCM.psc`/MCM
  Helper**. The push reads them directly.
- Compiles against vanilla + skse + `UI.psc` + the Task-1 `skyui/` sources only.
- `UI.SetFloat` appears only in the push; member names (`dbvoWpm`/`dbvoPadMs`) must match Task 3 exactly.

**Verification:**
- `tools/compile-papyrus.sh DBVODialogueTweaksMCM mods/DBVODialogueTweaks/src/scripts /tmp/pex` →
  produces `/tmp/pex/DBVODialogueTweaksMCM.pex`, no errors.

**Commit after passing.**

---

### Task 5: build.sh assembly  [Mode: Direct]

**Files:**
- Modify: `mods/DBVODialogueTweaks/build.sh`

No `config.json` — the menu is authored in the Task-4 script. `build.sh` just compiles + assembles the
three artifacts.

**`build.sh` additions** (keep the existing swf path + md5 guard; layer on):
- Compile `src/scripts/DBVODialogueTweaksMCM.psc` → `build/Scripts/DBVODialogueTweaksMCM.pex` (via
  `tools/compile-papyrus.sh`, which now includes the `skyui/` import path from Task 1).
- Generate the plugin: `EspGen build/DBVODialogueTweaks.esp DBVODialogueTweaksMCMQuest
  DBVODialogueTweaksMCM "DBVO Dialogue Tweaks" --player-alias SKI_PlayerLoadGameAlias`.
- `--install`: copy **all** of `build/` into `$GAME_DATA` (esp, swf, pex) — back up any live file first
  per workspace rules, and enable the esp in `Plugins.txt` (leading `*`).

**Constraints:**
- `--install` must list each file it copies and its pre-install md5 (mirror the existing swf line) so a
  later revert is possible.
- Don't ship SkyUI `.pex` — only `DBVODialogueTweaksMCM.pex`.

**Verification:**
- `./build.sh` → the three `build/` artifacts (esp, swf, pex) all exist; print the tree.
- Re-read `build/DBVODialogueTweaks.esp` with Mutagen (or the Task-2 `--verify` path) → quest +
  player alias present.

**Commit after passing.**

---

### Task 6: In-game integration test (skytest, manual)  [Mode: Direct — human-in-loop]

Not automatable — requires launching Skyrim. Install into a skytest profile carrying the full MCM
stack: **DBVO + Karat + SkyUI + this plugin** (full-profile, per the "which mode?" rule —
a Papyrus/MCM feature that only manifests on the live order). Add a temporary `Debug.Trace` on push
during bring-up.

**Checklist (the "Done when"):**
- [ ] MCM lists **DBVO Dialogue Tweaks** with the two sliders; values start at 300 / 1400.
- [ ] Papyrus log shows the `UI.SetFloat` push firing on dialogue-menu open (the temporary trace).
- [ ] Move **wpm** up / **pad** down → open a merchant → the player→NPC gap **visibly shortens**; revert
      sliders → gap returns to stock feel.
- [ ] Defaults (300 / 1400) reproduce stock DBVO pacing (no regression vs pre-v2).
- [ ] **v1 skip** (E / left-click mid-line) still works — additive change didn't regress it.
- [ ] No `60/0` blowup at any slider position (min is 150, but confirm the guard).

Remove the temporary trace, rebuild, commit final.

---

## Execution
**Skill:** superpowers:subagent-driven-development
- Mode A (Direct — Opus): Tasks 1, 3, 5, 6
- Mode B (Delegated — subagent): Tasks 2 (Mutagen/EspGen), 4 (MCM Papyrus script)

**Order / deps:** 1 → 4 (sources before psc compile); 2 → 5 (EspGen before assembly); 3 independent;
5 depends on 2+3+4; 6 last. 1, 2, 3 can run in parallel.
