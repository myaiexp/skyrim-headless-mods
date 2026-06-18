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
| `mods/`    | The mods built here, one dir each (`<mod>/build.sh`, `src/`; design in `docs/plans/`). AutoFireBow, AutoCastSpell, GhostAllies, OneClickTravel, SkytestProbe (SKSE C++); DBVODialogueTweaks (swf+SKSE); RapidBowHold (Papyrus).                                                                                                                                                                                                                                       |
| `tools/`   | Reusable toolchain — `EspGen`/`BsaExtract` (Mutagen), `papyrus-compiler` + `compile-papyrus.sh`, `papyrus-sources/`, `skse/`, `env.sh` (machine paths); `nexus` (read-only Nexus API status/stats CLI).                                                                                                                                                                                                                                                                                                        |
| `skytest/` | **Fast isolated, drivable mod-test launcher.** Swaps the live `Data/` between symlink profiles (vanilla / vanilla+1-mod / full) for interference-free testing; injects SkytestProbe + Start On Save; runs the mod under **gamescope** as a detached, drivable session — screenshot (SIGUSR2→AVIF), isolated **libei** input, in-world poll — **visible** (default) or **`--headless`**. Absorbed the old `headless/` driver (2026-06-12). `skytest/README.md`. |
| `docs/`    | Toolchain references (`papyrus-*.md`, `skse-*.md`), `ideas.md`, `plans/` (design+plan+handoff per topic).                                                                                                                                                                                                                                                                                                                                                      |

## Two tiers + the test harness

1. **Papyrus** (`tools/`) — data edits + logic scripts. Mutagen for `.esp`, wine+PapyrusCompiler for `.pex`. Great until you need engine internals (see `docs/papyrus-limits.md`).
2. **SKSE C++** (`tools/skse/`) — native DLL, full engine access, for what Papyrus fundamentally can't reach. Cross-compiled Linux→Windows (clang-cl + lld-link + xwin; CommonLibSSE-NG via FetchContent).
3. **Test what you build** — `skytest/`: vanilla+1 isolation **plus** a drivable gamescope test session (visible or `--headless`) to screenshot, inject input, and probe the mod in-engine. One tool.

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
  leaves it live to probe. **Caveat:** console `exec` staging does NOT work on game 1.6.1170 —
  CommonLib mis-binds `CompileAndRun` (stale dependency, pinned in `skytest/docs/headless-findings.md`
  #18; would fault windowed too). Use direct-call probe commands — the harness design, not a fallback.

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
