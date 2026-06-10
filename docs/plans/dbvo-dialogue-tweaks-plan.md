# DBVODialogueTweaks v1 — manual player-line skip — Implementation Plan

**Goal:** Make DBVO's player voiced line skippable like vanilla dialogue — E / left-click during the
line immediately advances to the NPC reply / barter menu.

**Architecture:** Single edit to DBVO's decompiled `DialogueMenu.as` (Flash AS2): during the
`TOPIC_CLICKED` wait window, route E + left-click to `clearTimeout(this.timer); this.topicClicked()`,
gated by a short debounce so the selecting click doesn't self-skip. Rebuild the swf with ffdec from a
**vendored stock baseline**. No Papyrus, no MCM, no DLL. Audio overlap accepted (clean cut is v3).

**Tech Stack:** ActionScript 2 (decompiled), JPEXS ffdec (`-export script` / `-importScript`), Skyrim
SE `Interface/dialoguemenu.swf`, skytest full profile (DBVO + Karat) for in-game verification.

**Design:** `docs/plans/dbvo-dialogue-tweaks-design.md`. Read it before starting.

---

## Prerequisites (resolve before Task 1)

- **ffdec** currently only at `/tmp/ffdec/usr/share/java/ffdec/ffdec.jar` (ephemeral). Confirm it
  still exists; if not, reinstall (AUR `jpexs-decompiler` / re-extract) and record the stable path in
  `build.sh`. The build must not depend on `/tmp`.
- **Stock baseline swf** — md5 `b1f70c5806ad94359bb0d780a9069d34`. Copy authoritative source:
  `~/Downloads/skyrim-mods/00-docs/overrides/2026-06-09-DBVO-instant-skip/Interface/dialoguemenu.swf`
  (matches the live game). Do **not** start from the `+900` experiment.

## File Structure

| Path                                                       | Responsibility                                                                                                                                                                                                                           |
| ---------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `mods/DBVODialogueTweaks/stock/dialoguemenu.swf`           | Vendored stock baseline (md5 `b1f70c58…`), so builds are reproducible without the live game or `/tmp`.                                                                                                                                   |
| `mods/DBVODialogueTweaks/src/DialogueMenu.as`              | The **edited** AS2 class — the only authored source. Decompiled from stock, then the skip logic added.                                                                                                                                   |
| `mods/DBVODialogueTweaks/build.sh`                         | Reproducible build: ffdec `-importScript src/DialogueMenu.as` into a copy of `stock/dialoguemenu.swf` → `build/Interface/dialoguemenu.swf`. Follows the repo's `./build.sh [--install]` convention (see `plugins/GhostAllies/build.sh`). |
| `mods/DBVODialogueTweaks/build/Interface/dialoguemenu.swf` | Rebuilt artifact (the installable file).                                                                                                                                                                                                 |
| `mods/DBVODialogueTweaks/README.md`                        | Exists (scope doc, v2/v3). Leave as-is.                                                                                                                                                                                                  |

---

### Task 1: Build scaffold + decompile stock to source [Mode: Direct]

**Files:**

- Create: `mods/DBVODialogueTweaks/stock/dialoguemenu.swf` (copied from the override baseline)
- Create: `mods/DBVODialogueTweaks/src/DialogueMenu.as` (ffdec export of the stock class)
- Create: `mods/DBVODialogueTweaks/build.sh`

**Steps / contracts:**

1. Copy the stock baseline swf into `stock/`. Assert md5 == `b1f70c5806ad94359bb0d780a9069d34`.
2. Decompile to get the canonical stock `DialogueMenu.as`:
   `java -jar <ffdec.jar> -export script <tmpdir> stock/dialoguemenu.swf`, then locate the exported
   `DialogueMenu.as` (top-level class; ffdec writes it under `<tmpdir>/scripts/DialogueMenu.as` or
   `.../<frame>/DefineSprite_*` — confirm path) and copy it to `src/DialogueMenu.as`.
3. `build.sh` must:
   - hold `FFDEC=<stable path>` (fail loudly if missing),
   - copy `stock/dialoguemenu.swf` → `build/Interface/dialoguemenu.swf`,
   - `java -jar $FFDEC -importScript build/Interface/dialoguemenu.swf build/Interface/dialoguemenu.swf <scriptsdir-containing src/DialogueMenu.as at the right relative path>`
     — **derive the exact arg order and scriptsdir layout by replaying the working `+900` build** at
     `~/Downloads/skyrim-mods/00-docs/custom-tweaks/dbvo-npc-gap/` (it round-trips successfully), not by
     re-guessing from `--help`,
   - print the output swf md5.

**Verification:**

- Record the actual pad constant in the stock decompile's `startTopicClickedTimer`. Confirm it is **not
  `900`** (which would mean the wrong, `+900`-experiment baseline got vendored). The design assumes stock
  is `+ 1400`; if it differs, note the real value — don't hard-fail on the exact number.
- `./build.sh` with the **unmodified** `src/DialogueMenu.as` produces a swf that the game loads and that
  behaves exactly like stock DBVO (round-trip sanity — proves the toolchain before we change behavior).

**Commit after passing** (`build(dbvo): vendor stock swf + reproducible ffdec build`).

---

### Task 2: Implement the skip in DialogueMenu.as [Mode: Direct]

**Files:**

- Modify: `mods/DBVODialogueTweaks/src/DialogueMenu.as`

**Contract — behavior to add (see design §"The change"):**

- A new debounce timestamp field, set when the timer is armed:
  - In `startTopicClickedTimer(voicePackID)`, on the branch that sets `this.timer = setTimeout(...)`,
    also record `this.skipArmedAt = getTimer();` (AS2 `getTimer()` = ms since start).
  - **Anchor rationale (decision — reviewer suggested anchoring in `onSelectionClick` instead; we
    keep it here on purpose).** Skip is only _possible_ once `this.timer` is armed, which happens in
    `startTopicClickedTimer` — _after_ the `PlayDBVOTopic` Papyrus round-trip. Anchoring the debounce
    at the arm point gives a fixed 250 ms quiet window measured from when the line actually starts, so
    the selecting click's echoes (and the variable round-trip latency) can't consume the window. The
    alternative (anchor at the selecting click in `onSelectionClick`) is _less_ robust: a slow
    round-trip would expire the window before the line even starts; a fast one leaves no guard. So
    arm-time anchoring is correct here, not a bug.
- A skip path reachable from **left-click** and (if routable) **E**:
  - Add a method, e.g. `function trySkipPlayerLine()`:
    ```
    if (this.eMenuState == DialogueMenu.TOPIC_CLICKED
        && this.timerBool && this.timer != undefined
        && getTimer() - this.skipArmedAt >= SKIP_DEBOUNCE_MS) {
        clearTimeout(this.timer);
        this.timer = undefined;
        this.topicClicked();   // sets timerBool=false; GameDelegate "TopicClicked"
    }
    ```
  - `static var SKIP_DEBOUNCE_MS = 250;`
  - The `eMenuState == TOPIC_CLICKED` term is **required** (matches design): stock `topicClicked()`
    clears `timerBool` but leaves `this.timer` holding an already-fired (stale) setTimeout id, so
    `this.timer != undefined` alone can be true with no live timer. The state check closes that.
- Wire input:
  - **Left-click:** `onMouseDown` calls `onItemSelect({mouseClick:true})`. Add the skip check as the
    **very first statements of `onItemSelect`, _above_ the existing `if (this.bAllowProgress && …)`
    block** (that block stays dead during the player-line wait because nothing sets `bAllowProgress`
    true then — so a branch placed _inside_ it would never run). If a player line is pending
    (`eMenuState == TOPIC_CLICKED && this.timer != undefined`), call `trySkipPlayerLine()` and
    `return`. Leave the `bAllowProgress`/`SkipText` NPC-skip path below it untouched.
  - **Caveat — `onMouseDown` parity hack:** `onMouseDown` only forwards to `onItemSelect` on _odd_
    invocations (`iMouseDownExecutionCount % 2 != 0`), so it acts on roughly every other physical
    mousedown. The skip will inherit this — left-click skip may need two clicks sometimes. Observe in
    Task 3; if it feels bad, consider routing left-click skip independent of the parity counter.
  - **E / activate keyboard:** in `handleInput`, when `eMenuState == TOPIC_CLICKED` and a player line is
    pending, on the activate nav code call `trySkipPlayerLine()`. **This routing is unverified** —
    `handleInput` (gated by `IsKeyPressed`) has no existing activate/Enter case and forwards nav codes
    to `pathToFocus`; the activate code in this menu is likely `ENTER`/`itemPress`. In Task 3, first
    confirm `handleInput` is even _called_ with the activate code during `TOPIC_CLICKED` before wiring.
    If it doesn't arrive, leave left-click working and record the gap (→ v3 SKSE input hook per
    `docs/ideas.md`).

**Constraints:**

- Do not remove or alter `bAllowProgress`, `SkipText`, `ALLOW_PROGRESS_DELAY`, or the
  `startTopicClickedTimer` `"off"` branch (voice-pack-off path must still fire `TopicClicked` directly).
- `topicClicked()` already guards state via `timerBool`; calling it twice must be impossible — clearing
  `this.timer`/`timerBool` in `trySkipPlayerLine` before calling ensures the scheduled callback can't
  also fire.
- Keep the diff minimal and match surrounding AS2 style (the file is decompiler output).

**Test cases (in-game behavioral spec — executed in Task 3):**

- **T1 skip-advances:** at a merchant, click the barter topic; mid voiced line, left-click → barter menu
  opens immediately (well before the stock ~1400 ms + word estimate).
- **T2 no self-skip:** the click that _selects_ the topic does not instantly advance — the line plays at
  least `SKIP_DEBOUNCE_MS` before a skip registers.
- **T3 no double-fire:** after skipping, exactly one `TopicClicked` fires — no skipped-past topic, no
  desync, dialogue continues normally.
- **T4 NPC-skip intact:** the pre-existing "click to skip the NPC's spoken line" still works unchanged.
- **T5 E-key:** if wired, E mid-line behaves like T1. If not routable, documented as left-click-only.
- **T6 left-click consistency:** confirm whether the `onMouseDown` parity hack makes left-click skip
  swallow every other click. If it does and feels bad, decide whether to bypass the parity counter.

**Verification:** `./build.sh` succeeds and prints a new md5 (≠ stock). Behavior verified in Task 3.

**Commit after Task 3 confirms** (don't commit an unverified swf as "working").

---

### Task 3: Install to full profile + in-game verification [Mode: Direct]

**Files:**

- Create: `~/Downloads/skyrim-mods/00-docs/overrides/<date>-DBVODialogueTweaks-v1/Interface/dialoguemenu.swf`
  (backup of the live swf before overwrite, per staging-repo convention)
- Modify (live, reversible): `<full profile>/Interface/dialoguemenu.swf`

**Steps:**

1. **Why full profile, not `skytest test`:** the skip only manifests with DBVO active, and skytest's
   isolated mode is _vanilla + one mod_. DBVO + Karat already live in the full profile, so test there.
2. Back up the current live `dialoguemenu.swf` (md5 `b1f70c58…`) to the dated `overrides/` dir above;
   note it in `~/Downloads/skyrim-mods/README.md`.
3. Copy `build/Interface/dialoguemenu.swf` over the live
   `~/.steam/steam/steamapps/common/Skyrim Special Edition/Data/Interface/dialoguemenu.swf`
   (Data is a symlink to `.profiles/full`; writing through it is correct).
4. Launch (`skytest play` or normal Steam launch), load a save near a merchant.
5. **Run T1–T6** from Task 2. This step needs Mase at the keyboard (mouse-in-headless is WIP, and this is
   timing/feel-sensitive). Capture: did left-click skip? did E skip? debounce feel right at 250 ms?
   does left-click need a double-tap (parity hack)? Before wiring E, confirm `handleInput` fires with the
   activate code during `TOPIC_CLICKED` (a Papyrus/`trace` log line or `cprint` is enough to verify).
6. **Tune & iterate:** if 250 ms feels off, adjust `SKIP_DEBOUNCE_MS`, rebuild, reinstall, retest. If E
   doesn't route, finalize left-click-only and update the design doc's known-risk note + `docs/ideas.md`.
7. **Restore option:** to revert, copy the `overrides/` backup back (or `skytest normal`/`uninstall`).

**Verification:**

- T1–T4 pass; T5 resolved (works or documented-as-deferred).
- Final installed swf md5 == `build/Interface/dialoguemenu.swf` md5.

**On pass:**

- Commit the verified source + artifact (`feat(dbvo): v1 player-line skip (E/left-click), swf-only`).
- Update `mods/DBVODialogueTweaks/README.md` v1 row: building → **done (verified in-game)**, with the
  final debounce value and the E-key outcome.
- Update the design doc "Done when" if the E-routing fallback was taken.

---

## Notes

- **Permissions:** the rebuilt swf is a derivative of DBVO's asset — personal use only, do not
  redistribute (repo is private). Already noted in the mod README.
- **No mase.fi logging, no `git deployboth`** for Skyrim work (per project CLAUDE.md). Plain commits;
  `git push origin` is allowed for this repo but not required by the plan.

---

## Execution

**Skill:** superpowers:subagent-driven-development

- All tasks are **Mode A — Direct** (Opus implements). The work is a single small AS2 file plus a
  manual, timing-sensitive in-game verification loop that needs the live session and Mase at the
  keyboard — subagent dispatch would add overhead without value here.
