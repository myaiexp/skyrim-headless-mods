# skytest v2 — "fast to testing position" (handoff)

> **STATUS (v2 shipped):** the autoload path is built. `skytest setup-save` creates the
> base save; `skytest <mod>` injects **powerofthree's Start On Save** (Nexus, DLL-only,
> Address-Library-only, AE/1.6.1170 via CommonLibSSE-NG, v2.7.0) into the **test profile only**
> and pins its `Save File` to `SkytestBase`, so launch boots into the prepared scene (hold SHIFT
> to skip to menu). Plugin + ini template in `skytest/base-skse/`, archive in the managing repo's `01-core/`.
> Decisions taken: **qasmoke** single base save; **generic vanilla ally baked into the save**
> for fixtures. User docs in `skytest/README.md` → "Boot straight into a test save".
> **Verified:** `setup-save` run in-game; `SkytestBase.ess` created via console `save SkytestBase`
> as described. **Still open:** confirm a live autoload by running `skytest <mod>` on a real mod
> (boots into SkytestBase with the mod active). Deferred below:
> per-mod test-esp / console-batch-at-load fixtures (item 3), and the v3 headless automation (item 4).

Brief for the next session. v1 is shipped and working; v2 is about cutting the time
from `skytest <mod>` to **standing in a useful test spot, in control of the character**.

## Where v1 left things (done, verified in-game)

- `skytest/skytest` swaps the live `Data/` symlink between profiles:
  `full` (real 40-mod setup) / `vanilla` (whitelist baseline) / `test` (vanilla + 1 mod).
- Launches via the direct `launch-skse.sh` path (sets `SteamAppId=489830`), waits for the
  real game PID (survives a 10s grace, so the launcher flash can't trigger an early
  restore), holds `test` for the whole session, restores `Data -> full` on quit.
- `--play` + a `.desktop` entry make the fast direct path the everyday default for the
  full game.
- Base SKSE set in a test profile = Address Library (`versionlib*.bin`) + CrashLogger.
- Startup: ~2 DLLs vs the full ~51. Verified with GhostAllies (shot through an ally).

See `skytest/README.md` and the script header for full detail.

## v2 goal

`skytest <mod>` → boots straight into a **prepared test save** (no main-menu clicks),
character controllable, in a spot suited to testing — ideally with any fixtures the mod
needs already present (e.g. an ally for GhostAllies).

## Pieces to build

1. **A base test save.** Made in a *vanilla + base-SKSE only* profile so it loads under
   ANY test (a test = vanilla + base + the one mod; the save must not depend on the mod
   or any non-base content, or it crashes — same reason modded saves crash today).
   - Candidate spots: `coc qasmoke` (Bethesda's dev hall — tiny, instant load, has all
     items/NPCs) and/or a clean exterior like Whiterun for realism. Decide one or both.
   - Needs a no-mod launch path to create it — add `skytest setup-save` (launch
     vanilla+base, build the scene, save), since `skytest <mod>` currently requires a mod.

2. **Skip the main menu → auto-load the save.** Preferred ordering (Mase):
   - **Start with auto-load-latest** — simplest; an existing SKSE plugin that loads the
     most recent save. RESEARCH which is current for **1.6.1170 AE** (Nexus + context7/brave).
   - **Load a *specific* save** (a small custom mod) is a nicer middle option if
     auto-load-latest is too blunt.
   - **Full main-menu skip** is the fun end-state but **deferred until after the headless-
     Skyrim work** — expect a lot of probing/testing to get the engine past the menu
     cleanly. Don't lead with this.

3. **Per-mod fixtures.** GhostAllies needs a player *teammate* to act on. Options:
   - bake a conjured teammate into the base save (simplest, but shared across all mods);
   - run a console batch at load (`bat <file>` — needs an auto-run hook);
   - a tiny per-mod test `.esp` that places fixtures.
   Pick the approach; baking a generic ally into the base save likely covers most cases.

4. **(v3 / stretch) headless-ish automation.** boot → run a console batch (coc + spawn +
   assert) → quit, for CI-style mod checks. skytest already owns launch + lifecycle, so
   this is a thin layer on top. Ties into the separate "run Skyrim headless" experiments
   (needs a virtual display + the `SteamAppId` env, which we already set).

## Constraints / gotchas (learned in v1)

- **Modded saves crash** in a vanilla+1 profile — by design. Test saves must depend only
  on vanilla + base SKSE.
- The launcher/loader **flashes a short-lived process** that matches a `skyrimse` name
  search; v1 handles it with a grace-window PID lock — keep that if reworking lifecycle.
- A `crash-*.log` on **quit-to-desktop** is the known vanilla engine shutdown crash, not
  the mod under test. Check the call stack, not the module list, before blaming a mod.
- Quick ally for manual tests today: console → click NPC → `setplayerteammate 1`.

## Pointers

- Tool + flags: `skytest/skytest` (header) ; usage in `skytest/README.md`.
- Launch path: `<game>/launch-skse.sh` (direct Proton; `SteamAppId=489830` required).
- Saves: `<prefix>/Documents/My Games/Skyrim Special Edition/Saves/`.
- Profiles live in `<game>/.profiles/` (not in this repo).
