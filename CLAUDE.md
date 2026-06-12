# CLAUDE.md — skyrim-headless-mods (mod *making*)

Orientation for new sessions. **Map, not a manual** — it points to where detail lives.
Read `README.md` for the full toolchain tour and the per-mod table before changing anything.

## Which repo am I in? (read this first)

This is the **mod-MAKING** repo. There are two Skyrim areas on this machine and they are
**not the same job**:

| Repo | Job | Git remote? |
|------|-----|-------------|
| **`~/Projects/skyrim-headless-mods/` (here)** | **Making** mods — authoring `.esp`/`.pex`/SKSE DLLs from code, headlessly on Linux, and testing them. | **Yes** (`origin` → GitHub) — commit *and* push. |
| `~/Downloads/skyrim-mods/` | **Managing** mods — downloading, installing, troubleshooting the *live* game. | No (local-only — commit, don't push). |

If the task is "install / fix / troubleshoot a mod in my actual playthrough," you're in the
wrong place — that's `~/Downloads/skyrim-mods/`. Here, you **build** mods and test them in
isolation. Don't conflate the two.

## What this is

Making Skyrim SE mods **headlessly on Linux** — no SSEEdit, no Creation Kit, no GUI tooling.
Plugins are authored in code (Mutagen/C#), Papyrus compiles from the CLI under wine, and
engine-level behavior lives in a cross-compiled SKSE C++ tier. Full *why* + pipeline in `README.md`.

## Layout

| Path | Holds |
|------|-------|
| `mods/` | The mods built here, one dir each (`<mod>/build.sh`, `src/`; design in `docs/plans/`). AutoFireBow, GhostAllies, SkytestProbe (SKSE C++); DBVODialogueTweaks (swf+SKSE); RapidBowHold (Papyrus); OneClickMap (designed). |
| `tools/` | Reusable toolchain — `EspGen`/`BsaExtract` (Mutagen), `papyrus-compiler` + `compile-papyrus.sh`, `papyrus-sources/`, `skse/`, `env.sh` (machine paths). |
| `headless/` | **Drive the game with no monitor:** headless `gamescope`, SIGUSR2→AVIF screenshots, libei input — a test loop for the mods here. `headless/README.md`. |
| `skytest/` | **Fast isolated mod-test launcher** (moved here 2026-06-12). Swaps the live `Data/` between symlink profiles (vanilla / vanilla+1-mod / full) for interference-free testing; injects SkytestProbe + Start On Save into test profiles. `skytest/README.md`. |
| `docs/` | Toolchain references (`papyrus-*.md`, `skse-*.md`), `ideas.md`, `plans/` (design+plan+handoff per topic). |

## Two tiers + the test harness

1. **Papyrus** (`tools/`) — data edits + logic scripts. Mutagen for `.esp`, wine+PapyrusCompiler for `.pex`. Great until you need engine internals (see `docs/papyrus-limits.md`).
2. **SKSE C++** (`tools/skse/`) — native DLL, full engine access, for what Papyrus fundamentally can't reach. Cross-compiled Linux→Windows (clang-cl + lld-link + xwin; CommonLibSSE-NG via FetchContent).
3. **Test what you build** — `skytest/` for fast vanilla+1 isolation; `headless/` to drive the game with no display.

## Testing a mod you built — which mode?

- **`skytest test <mod>`** (isolated vanilla+1) for a mod that works **standalone** — a new spell,
  a self-contained DLL/esp. Fast and interference-free.
- **Full-profile install + `skytest play`** for a mod that only manifests **on top of the live
  load order** — patches, or asset overrides of another mod (e.g. a DBVO swf edit that needs DBVO
  + a voice pack present). The vanilla+1 profile can't reproduce it: install into the real game
  (over in `~/Downloads/skyrim-mods/`) and test there. Full rule in `skytest/README.md`.

> **skytest manages the live game's `Data/` symlink.** It lives here now but operates on the real
> install (`…/Skyrim Special Edition/Data` → `.profiles/full`). The mod-*managing* repo
> (`~/Downloads/skyrim-mods/`) still relies on that symlink for its manual installs — don't change
> skytest's symlink behavior without checking that side too. Revert the whole scheme with
> `skytest uninstall`.

## Working norms

- **Git: this repo HAS a remote** (`origin` → GitHub) — commit **and** `git push origin`, unlike the
  local-only `~/Downloads/skyrim-mods/`. The repo is **private** (`tools/papyrus-sources/` vendors
  Bethesda's Papyrus API stubs) — don't make it public without regating those.
- **No mase.fi logging for Skyrim.** Do **not** run `git deployboth` or `mase-fi-update` — this
  overrides the global "log features / run deployboth" rules. (Same as the managing repo.)
- **Two CC sessions may touch a Skyrim folder at once — stage precisely** by filename
  (`git add <path> …`); never `git add -A`/`-u`/`.`. Check `git status` first, add only what's yours.
- Follow the global per-project doc convention: design → `docs/plans/<topic>-design.md`, plan →
  `-plan.md`, deferred work/tech-debt → `docs/ideas.md`.

## Pointers

- **`README.md`** — toolchain tour, per-mod table, two-tier rationale, prerequisites, quick start.
- **`skytest/README.md`** — the skytest manual (verbs, profiles, boot-into-save, SkytestProbe).
- **`headless/README.md`** — the no-monitor driver (gamescope + screenshots + libei).
- **`docs/papyrus-{toolchain,workflow,limits}.md`** — the Papyrus tier end to end + its hard limits.
- **`docs/skse-{toolchain,tier-bringup}.md`** — the SKSE C++ tier bring-up.
- **`docs/ideas.md`** — deferred features + tech debt. **`docs/plans/`** — design/plan/handoff per topic.
