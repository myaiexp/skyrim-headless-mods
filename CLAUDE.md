# CLAUDE.md — skyrim-headless-mods (mod _making_)

Orientation for new sessions. **Map, not a manual** — it points to where detail lives.
Read `README.md` for the full toolchain tour and the per-mod table before changing anything.

## Which repo am I in? (read this first)

This is the **mod-MAKING** repo. There are two Skyrim areas on this machine and they are
**not the same job**:

| Repo                                          | Job                                                                                                   | Git remote?                                      |
| --------------------------------------------- | ----------------------------------------------------------------------------------------------------- | ------------------------------------------------ |
| **`~/Projects/skyrim-headless-mods/` (here)** | **Making** mods — authoring `.esp`/`.pex`/SKSE DLLs from code, headlessly on Linux, and testing them. | **Yes** (`origin` → GitHub) — commit _and_ push. |
| `~/Downloads/skyrim-mods/`                    | **Managing** mods — downloading, installing, troubleshooting the _live_ game.                         | No (local-only — commit, don't push).            |

If the task is "install / fix / troubleshoot a mod in my actual playthrough," you're in the
wrong place — that's `~/Downloads/skyrim-mods/`. Here, you **build** mods and test them in
isolation. Don't conflate the two.

## What this is

Making Skyrim SE mods **headlessly on Linux** — no SSEEdit, no Creation Kit, no GUI tooling.
Plugins are authored in code (Mutagen/C#), Papyrus compiles from the CLI under wine, and
engine-level behavior lives in a cross-compiled SKSE C++ tier. Full _why_ + pipeline in `README.md`.

## Layout

| Path       | Holds                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| ---------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `mods/`    | The mods built here, one dir each (`<mod>/build.sh`, `src/`; design in `docs/plans/`). AutoFireBow, AutoCastSpell, GhostAllies, OneClickTravel, SkytestProbe (SKSE C++); DBVODialogueTweaks (swf+SKSE); RapidBowHold (Papyrus).                                                                                                                                                                                                                                |
| `tools/`   | Reusable toolchain — `EspGen`/`BsaExtract` (Mutagen), `papyrus-compiler` + `compile-papyrus.sh`, `papyrus-sources/`, `skse/`, `env.sh` (machine paths); `ghidra` (headless Ghidra RE tier); `nexus` (read-only Nexus API status/stats CLI).                                                                                                                                                                                                                    |
| `skytest/` | **Fast isolated, drivable mod-test launcher.** Swaps the live `Data/` between symlink profiles (vanilla / vanilla+1-mod / full) for interference-free testing; injects SkytestProbe + Start On Save; runs the mod under **gamescope** as a detached, drivable session — screenshot (SIGUSR2→AVIF), isolated **libei** input, in-world poll — **visible** (default) or **`--headless`**. Absorbed the old `headless/` driver (2026-06-12). `skytest/README.md`. |
| `docs/`    | Toolchain references (`papyrus-*.md`, `skse-*.md`), `ideas.md`, `plans/` (design+plan+handoff per topic).                                                                                                                                                                                                                                                                                                                                                      |

## Two tiers + the test harness

1. **Papyrus** (`tools/`) — data edits + logic scripts. Mutagen for `.esp`, wine+PapyrusCompiler for `.pex`. Great until you need engine internals (see `docs/papyrus-limits.md`).
2. **SKSE C++** (`tools/skse/`) — native DLL, full engine access, for what Papyrus fundamentally can't reach. Cross-compiled Linux→Windows (clang-cl + lld-link + xwin; CommonLibSSE-NG via FetchContent).
3. **Test what you build** — `skytest/`: vanilla+1 isolation **plus** a drivable gamescope test session (visible or `--headless`) to screenshot, inject input, and probe the mod in-engine. One tool.

## The game is on 1.7.104 — and so is this repo (2026-09-02)

Steam updated `SkyrimSE.exe` to **1.7.104.0** on 2026-09-01, Bethesda's first Skyrim patch in two
and a half years. This repo has moved with it and is **verified in-engine on 1.7.104**: SKSE
**2.3.1**, Address Library **v13**, and all six SKSE plugins rebuilt against
**`alandtse/CommonLibSSE-NG v7.1.0`** (CharmedBaryon's upstream is abandoned at `b93280e` and
misfiles 1.7.x as pre-AE — never repin to it; `docs/skse-toolchain.md` has the detail).

What this means for anything you touch here:

- **The new Address Library is FORMAT 5** (`versionlib-1-7-104-0.bin`; 1.6.1170's was format 2).
  Any SKSE DLL built before ~2026-08 dies on it with a modal — `REL/ID.h: Unsupported address
  library format: 5` — which **parks the boot forever**, it does not fail soft. `skytest` detects
  that modal and aborts the wait naming the plugin; believe it and go update that DLL.
- **Two bundled/third-party DLLs are still stale**, so a test profile is not automatically clean:
  `CrashLogger.dll` is renamed `.disabled-stale-for-1.7.104` in the vanilla profile, and
  `skytest/base-skse/po3_StartOnSave.dll` (v2.7.0.1) cannot load — so **boot-into-save is
  unavailable**. Use `SKYTEST_NO_AUTOLOAD=1 skytest test …`: it boots to the menu with only the
  base save loadable, returns as soon as the probe answers (~40 s — it no longer waits out the
  in-world timeout), and prints the drive-in recipe (`drive click 1177 496` → `drive tap enter` →
  `ready`). Restoring autoload needs Start On Save **2.8.0** (Nexus 50054, file 795157) dropped
  into `skytest/base-skse/`.
- **`.profiles/full` — the real ~40-mod load order — is NOT usable.** Every third-party plugin in
  it predates the patch. `skytest play` will hit the format-5 modal. Only `skytest test` (vanilla
  + our own rebuilt DLLs) is known-good.
- **A full six-mod rebuild is slow on purpose-less duplication**: each mod's `FetchContent`
  compiles its own private CommonLibSSE-NG (~500 TUs, ~10 min), six times over, and `ccache` is
  not installed. See `docs/ideas.md` before you sit through it again.
- `skytest status` prints a `runtime` line and every launch verb runs `assert_runtime_match`.
  **Trust that line** rather than reasoning about whether the game "should" work.
- **Loading is not working.** The tier-move verified that the six DLLs *load* on 1.7.104.
  Re-verified as *functional* there so far, each by A/B and each replayable:
  **OneClickTravel** (2026-09-03, against `test --vanilla`, `mods/OneClickTravel/oneclick.steps`)
  and **DBVODialogueTweaks**' reply-on-line-end (2026-09-03, against the same profile minus its
  DLL, `mods/DBVODialogueTweaks/replyonlineend.steps`). AutoFireBow, AutoCastSpell and GhostAllies
  are still load-only — re-run each mod's own in-engine test before claiming otherwise, and note
  DBVODialogueTweaks' skip / interrupt-cut / volume features are also still untested here.
- **A third-party mod can be dead on 1.7.104 in two different ways, and only one is loud.**
  A CommonLibSSE plugin too old for the format-5 Address Library throws its own modal and parks
  the boot. A *version-locked* plugin is refused by SKSE itself, before loading, behind a win32
  message box that a coordinate click cannot dismiss — `skytest drive tap n` continues without it,
  and the message appears only in `skse64.log` (skytest scans it and names the DLL). This is what
  currently kills **DBVO 1.x**: SKSE refuses ConsoleUtilSSE and JContainers64, so DBVO speaks no
  player line at all. Assume any un-updated framework DLL is in one of those two states.

## Testing a mod you built — which mode?

- **`skytest test <mod>`** (isolated vanilla+1, a **drivable gamescope session** — visible by
  default, `--headless` for no window; then `ready`/`shot`/`drive`/`stop`) for a mod that works
  **standalone** — a new spell, a self-contained DLL/esp. Fast and interference-free. Running a mod
  in-engine this way is the normal close-out for a change, not a detour — and the session is detached,
  so you fire it off and keep working, then `drive`/`shot`/probe it.
- **Full-profile install + `skytest play`** for a mod that only manifests **on top of the live load
  order** — patches, or asset overrides of another mod (e.g. a DBVO swf edit that needs DBVO and a
  voice pack present). The vanilla+1 profile can't reproduce it: install into the real game (over
  in `~/Downloads/skyrim-mods/`) and test there. Full rule in `skytest/README.md`.
- **First test = drive live; every test after = `skytest replay`.** Once you've driven a setup by
  hand, persist it as `mods/<mod>/<name>.steps` and re-run it with `skytest replay <mod> <name>.steps`
  — it boots the same isolated session and snaps to the target state via probe-gated steps, then
  leaves it live to probe. Make the LAST gate the assertion (`until:cell:<EditorID>` after a
  travel/`coc`, `until:menu:<NAME>` after an open) so the script fails when the mod does, and see
  `mods/OneClickTravel/oneclick.steps` for the shape.
- **World staging: probe command if one exists, otherwise TYPE THE CONSOLE.** Direct-call probe
  commands (`placeatme`, `give-spell`, `set-av`, `ui-invoke`/`ui-set`/`ui-get`, …) are first
  choice — acked, main-thread. For anything they don't cover, drive the real console:
  `drive tap tilde` + `drive type '<line>'` + `drive tap enter` (a `type` step exists in
  `.steps`), verified on 1.7.104 with `tmm 1`, `coc riverwood` and `player.speaksound`. The
  console's **command table** is fine; only the probe's *programmatic* `exec` is broken (CommonLib
  mis-binds `CompileAndRun` — findings #18/#27). **Quote any path argument** — the console splits
  an unquoted one at the first `/` (`type` holds SHIFT for `"` and `_` since finding #31).
  **Close the console** before driving anything else *and before the thing you're measuring
  happens*: open, it eats keys, turns clicks into console ref-picks, and swallows a mod's
  callback into the menu underneath (#33).
- **A menu-side mod is testable and assertable.** SkytestProbe's `ui-invoke`/`ui-set`/`ui-get`
  call into an open menu's ActionScript — the direct-call form of `UI.InvokeString`/`SetFloat` —
  so a swf can be stimulated when its own Papyrus can't run, and the `until:uivar:<menu>|<path>|
  <value>` replay gate turns the swf's internal state into a pass/fail assertion.
- **A/B every behavioral claim with `skytest test --vanilla`** — the same rig, same save, same
  staging, no mod. It is one flag, so the control differs from the test run in exactly one thing.

> **skytest manages the live game's `Data/` symlink.** It lives here now but operates on the real
> install (`…/Skyrim Special Edition/Data` → `.profiles/full`). The mod-_managing_ repo
> (`~/Downloads/skyrim-mods/`) still relies on that symlink for its manual installs — don't change
> skytest's symlink behavior without checking that side too. Revert the whole scheme with
> `skytest uninstall`.

## Working norms

- **Git: this repo HAS a remote** (`origin` → GitHub) — commit **and** `git push origin`, unlike the
  local-only `~/Downloads/skyrim-mods/`. The tree is **clean for open-source**: all third-party IP
  (Bethesda vanilla stubs + `.flg`, SKSE sources, CK compiler binary) is git-ignored and populated
  locally (see each dir's `README.md`); license is **MIT** (`LICENSE`). Old commits still contain
  those files — **accepted, no history rewrite** (they ship with the game / are widely mirrored).
  A few third-party files are intentionally **kept committed in the tree** because their licenses
  permit redistribution with credit: DBVO's `dialoguemenu.swf`/`.as` (DBVO permits modding +
  release with credit) and `skytest/base-skse/po3_StartOnSave.dll` (powerofthree's permissive
  terms) — attribution lives in the relevant mod's README. Repo is **cleared to go public**; flip
  with `gh repo edit --visibility public`.
- **No deploy/changelog automation for Skyrim.** Don't run `git deployboth` or any site-update
  logging here — plain commits + `git push origin` only. (Same as the managing repo.)
- **Two CC sessions may touch a Skyrim folder at once — stage precisely** by filename
  (`git add <path> …`); never `git add -A`/`-u`/`.`. Check `git status` first, add only what's yours.
- Follow the global per-project doc convention: design → `docs/plans/<topic>-design.md`, plan →
  `-plan.md`, deferred work/tech-debt → `docs/ideas.md`.
- **Don't "clean up" working code unprompted.** Once a mod is verified in-engine, don't refactor,
  tidy, or strip it (logging, comments) unless asked — these mods are timing-sensitive and can
  regress invisibly (AutoCastSpell's recharge leaned on its per-cycle log flush for pacing; removing
  it cut the loop from 7 casts to 2). If you do touch it, re-run the **same** in-engine test that
  proved it; on any regression, **restore the known-good build first** — don't pivot to a weaker
  design unless Mase chose that.
- **SkytestProbe is the permanent home for all test instrumentation.** Any probe, watch, state
  query, event sink, or console hook you add to test a mod goes into SkytestProbe and _stays_ — it's
  an accreting toolkit maintained alongside the mods, not scratch instrumentation bolted on and
  stripped out. Need a new signal for a test (a sync gate, a state query)? Add it to SkytestProbe so
  the next test reuses it. Never park probe code in the mod-under-test or delete it once the test
  passes.
- **Using skytest to debug a mod improves skytest too.** When a debugging session hits harness
  friction (a verb that misbehaves, a missing boot path, a stale-injection gotcha) or learns a
  reusable trick, fold the fix/instrument/gotcha back into skytest or its docs _in the same session_
  — don't just work around it. The harness accretes alongside the mods it tests. (Companion to the
  SkytestProbe-instrumentation norm.)
- **Describe a skytest session as _detached_, never by duration or "heaviness."** Don't say "takes a
  while" / "heavy" — say what it does and that you fire it off and keep working. (Mirrors the global
  "no duration/work-amount framing" rule.)

## Pointers

- **`README.md`** — toolchain tour, per-mod table, two-tier rationale, prerequisites, quick start.
- **`skytest/README.md`** — the skytest manual (verbs incl. `test`/`ready`/`shot`/`drive`/`stop`, profiles, the drivable display layer, boot-into-save, SkytestProbe).
- **`skytest/docs/headless-findings.md`** — gamescope/libei dead-ends + the headless-screenshot open item; read before changing the display/input approach.
- **`docs/papyrus-{toolchain,workflow,limits}.md`** — the Papyrus tier end to end + its hard limits.
- **`docs/skse-{toolchain,tier-bringup}.md`** — the SKSE C++ tier bring-up.
- **`docs/ghidra.md`** — the headless Ghidra RE tier (`tools/ghidra/ghidra.sh`): disassemble `SkyrimSE.exe` to find non-virtual hook seams the Address-Library tier can't reach (analyse once, query many; PyGhidra in a venv).
- **`docs/nexus-api.md`** — the Nexus Mods read-only API + `tools/nexus` (mod release-status / stats checker).
- **`docs/ideas.md`** — deferred features + tech debt. **`docs/plans/`** — design/plan/handoff per topic.
