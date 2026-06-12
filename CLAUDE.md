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
| `mods/`    | The mods built here, one dir each (`<mod>/build.sh`, `src/`; design in `docs/plans/`). AutoFireBow, GhostAllies, SkytestProbe (SKSE C++); DBVODialogueTweaks (swf+SKSE); RapidBowHold (Papyrus); OneClickMap (designed).                                                                                                                                                                                                                                       |
| `tools/`   | Reusable toolchain — `EspGen`/`BsaExtract` (Mutagen), `papyrus-compiler` + `compile-papyrus.sh`, `papyrus-sources/`, `skse/`, `env.sh` (machine paths).                                                                                                                                                                                                                                                                                                        |
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
  in-engine this way is the normal close-out for a change, not a heavyweight detour — the ~1–2 min
  boot is expected and worth it; the session is detached so you're not blocked while it boots.
- **Full-profile install + `skytest play`** for a mod that only manifests **on top of the live
  load order** — patches, or asset overrides of another mod (e.g. a DBVO swf edit that needs DBVO
  and a voice pack present). The vanilla+1 profile can't reproduce it: install into the real game
  (over in `~/Downloads/skyrim-mods/`) and test there. Full rule in `skytest/README.md`.

> **skytest manages the live game's `Data/` symlink.** It lives here now but operates on the real
> install (`…/Skyrim Special Edition/Data` → `.profiles/full`). The mod-_managing_ repo
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
- **`skytest/README.md`** — the skytest manual (verbs incl. `test`/`ready`/`shot`/`drive`/`stop`, profiles, the drivable display layer, boot-into-save, SkytestProbe).
- **`skytest/docs/headless-findings.md`** — gamescope/libei dead-ends + the headless-screenshot open item; read before changing the display/input approach.
- **`docs/papyrus-{toolchain,workflow,limits}.md`** — the Papyrus tier end to end + its hard limits.
- **`docs/skse-{toolchain,tier-bringup}.md`** — the SKSE C++ tier bring-up.
- **`docs/ideas.md`** — deferred features + tech debt. **`docs/plans/`** — design/plan/handoff per topic.
