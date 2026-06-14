# DBVODialogueTweaks v3 — player-voice volume slider (implementation plan)

**Goal:** A 0–200% MCM slider that scales the volume of only the player's DBVO voice line, via an SKSE
plugin that hooks `Actor::SpeakSoundFunction` and calls `SetVolume` on the handle the engine returns.

**Architecture:** An MCM `Float` property (persists in save) is pushed — on slider-accept and on every
game-load — into the SKSE DLL through a Papyrus global-native (`DBVOTweaks.SetPlayerVoiceVolume`). The
DLL holds it in `g_dbvoVolume` and, from a trampoline hook on `Actor::SpeakSoundFunction`
(`RELOCATION_ID(36541, 37542)`), applies `handle->SetVolume(g_dbvoVolume)` to the player's DBVO line
only (gated on `IsPlayerRef()` + a `"DBVO/"` path prefix). The engine still runs `SpeakSound` in full,
so lip-sync is untouched and NPC voices are never affected. A prerequisite repo-layout unification moves
the SKSE tier out of `plugins/` so the DLL can live in-place under the mod.

**Tech Stack:** C++23, CommonLibSSE-NG (FetchContent, AE+SE), spdlog; headless clang-cl + lld-link +
xwin cross-build (Linux→Windows DLL); Address Library for version-independent offsets; Papyrus compiler
(wine, in-repo) + SkyUI sources (already vendored in v2); SKSE Papyrus interface for the native.

**Design:** `docs/plans/dbvo-v3-player-voice-volume-design.md` (spec-reviewed, approved).

**Domain note on verification:** the DLL only does anything inside a running Skyrim — there is no
host-side unit harness. So each DLL task's "test cases" are: (a) the build succeeds and emits a PE32+
DLL, (b) the specified lines appear in `DBVODialogueTweaks.log`, and (c) the described in-game
observation holds. Papyrus tasks verify by compiling clean. The end-to-end behavior is Task 5
(human-in-loop, skytest full profile). Treat the log assertions and the in-game checklist as the
executable spec.

---

## Reference material

- Design (read first): `docs/plans/dbvo-v3-player-voice-volume-design.md` — mechanism, hook target,
  gates, the boost caveat, "Why this shape".
- In-repo pattern source (post-Task-1 paths): `mods/AutoFireBow/{CMakeLists.txt,src/main.cpp}` — copy
  its CMake, spdlog/CommonLibSSE-NG pins, `SetupLog()`, `SKSEPluginInfo`/`SKSEPluginLoad` boilerplate,
  and its at-load `InstallHooks()` idiom. AutoFireBow/GhostAllies use **vtable** hooks; this plugin uses
  a **trampoline branch hook** instead (`SKSE::GetTrampoline().write_branch<5>`), since
  `SpeakSoundFunction` is non-virtual.
- The v2 mod (the side we extend): `mods/DBVODialogueTweaks/{build.sh, src/papyrus/DBVODialogueTweaksMCM.psc}`.
- Shared toolchain after Task 1: `tools/skse/{cross-env.sh, cmake/clang-cl-msvc.cmake, setup-sdk-symlinks.sh}`,
  `tools/env.sh` (`$GAME_DATA`/`$PLUGINS_TXT`), `tools/compile-papyrus.sh` (already imports `skyui/`).
- External precedent for the hook (mine, don't copy blind): `tiltedphoques/TiltedEvolution`
  `Code/client/Games/Skyrim/Actor.cpp` (`SpeakSoundFunction` signature + id 37542); `VersuchDrei/OStimNG`
  `skse/src/GameLogic/GameHooks.h` (hooks inside the same `RELOCATION_ID(36541, 37542)`).

## File structure

| File | Responsibility | New/Mod |
| --- | --- | --- |
| `mods/{AutoFireBow,GhostAllies,OneClickTravel}/` | the 3 DLL-only mods, moved from `plugins/` | Move |
| each moved `…/build.sh` | re-root toolchain from `$PLUGINS_DIR` → `tools/skse/` | Modify |
| `tools/skse/{cross-env.sh,cmake/,setup-sdk-symlinks.sh}` | SKSE cross-compile toolchain, moved from `plugins/` | Move |
| `.gitignore`, ~16 `.md` docs | path references `plugins/` → `mods/` or `tools/skse/` | Modify |
| `mods/DBVODialogueTweaks/plugin/CMakeLists.txt` | build `DBVODialogueTweaks.dll` (AutoFireBow's, renamed) | New |
| `mods/DBVODialogueTweaks/plugin/src/main.cpp` | whole plugin: log, the hook, `g_dbvoVolume`, native registration | New |
| `mods/DBVODialogueTweaks/src/papyrus/DBVOTweaks.psc` | global-native declaration the MCM calls | New |
| `mods/DBVODialogueTweaks/src/papyrus/DBVODialogueTweaksMCM.psc` | add "Voice" page + slider + push to native | Modify |
| `mods/DBVODialogueTweaks/build.sh` | renumber to 4 steps; compile both psc; build+install the DLL | Modify |

---

### Task 1: Repo layout unification (prerequisite) [Mode: Direct]

**Files:**
- Move: `plugins/AutoFireBow`, `plugins/GhostAllies`, `plugins/OneClickTravel` → `mods/` (via `git mv`).
- Move: `plugins/cross-env.sh`, `plugins/cmake/`, `plugins/setup-sdk-symlinks.sh` → `tools/skse/`.
- Delete: `plugins/` (must be empty after the moves).
- Modify: the three moved `build.sh`; `tools/skse/cross-env.sh`; `tools/skse/cmake/clang-cl-msvc.cmake`;
  `tools/skse/setup-sdk-symlinks.sh`; `.gitignore`; the doc set referencing `plugins/`.

**Contract — build.sh re-root** (each moved mod's `build.sh`; they are currently identical in this part):
```bash
# was: PLUGINS_DIR="$(dirname "$HERE")"            # = plugins/
#      source "$PLUGINS_DIR/cross-env.sh"
#      -DCMAKE_TOOLCHAIN_FILE="$PLUGINS_DIR/cmake/clang-cl-msvc.cmake"
#      source "$PLUGINS_DIR/../tools/env.sh"
# now (HERE = mods/<Name>/):
REPO_ROOT="$(cd "$HERE/../.." && pwd)"
SKSE_DIR="$REPO_ROOT/tools/skse"
source "$SKSE_DIR/cross-env.sh"
# -DCMAKE_TOOLCHAIN_FILE="$SKSE_DIR/cmake/clang-cl-msvc.cmake"
source "$REPO_ROOT/tools/env.sh"
```

**Constraints:**
- `git mv` (preserve history); do not copy+delete.
- `RapidBowHold` already lives in `mods/` (esp+pex, no DLL) — **do not touch it**.
- Inspect `cross-env.sh`, `cmake/clang-cl-msvc.cmake`, `setup-sdk-symlinks.sh` for any internal
  `plugins/`-relative assumptions (xwin SDK cache path, build dirs, symlink targets) and fix them to the
  `tools/skse/` location. In `.gitignore`, the single `plugins/*/build/` line becomes **two** globs:
  `mods/*/build/` (the moved DLL-only mods) **and** `mods/*/plugin/build/` (DBVODialogueTweaks's nested
  DLL build dir) — don't drop either.
- Doc sweep: `rg -l 'plugins/' --glob '*.md'` (16 files) — update path references so they resolve
  (`plugins/AutoFireBow` → `mods/AutoFireBow`, `plugins/` toolchain → `tools/skse/`). Update paths only;
  don't rewrite historical narrative beyond the path. Also `README.md`'s mod table.

**Test Cases (verification):**
- `test ! -d plugins` (directory gone); `ls mods/AutoFireBow mods/GhostAllies mods/OneClickTravel tools/skse/cross-env.sh`.
- **Toolchain still builds from the new home:** `./mods/GhostAllies/build.sh` exits 0 and
  `file mods/GhostAllies/build/GhostAllies.dll` → `PE32+ executable (DLL) ... x86-64`. (Proves the
  re-root works; GhostAllies is the most complex of the three.)
- `rg -l 'plugins/' --glob '*.md' | wc -l` → `0` (or only intentional archival mentions, listed
  explicitly in the commit message if any remain).

**Commit after passing** (its own commit, ahead of the feature — message notes the unify rationale).

---

### Task 2: Papyrus — native bridge + MCM volume slider [Mode: Delegated]

**Files:**
- Create: `mods/DBVODialogueTweaks/src/papyrus/DBVOTweaks.psc`
- Modify: `mods/DBVODialogueTweaks/src/papyrus/DBVODialogueTweaksMCM.psc`

**Contract — `DBVOTweaks.psc`** (global-native holder; ships as `DBVOTweaks.pex`):
```papyrus
Scriptname DBVOTweaks Hidden
; factor 0.0–2.0; 1.0 = unchanged. Registered by DBVODialogueTweaks.dll.
Function SetPlayerVoiceVolume(Float factor) Global Native
```

**Contract — `DBVODialogueTweaksMCM.psc` additions** (extend the existing `SKI_ConfigBase` script; keep
the two v2 timing sliders and the `OnMenuOpen` swf push exactly as-is):
- Add `Float Property fPlayerVoiceVol = 100.0 Auto` (percent; persists like the v2 properties).
- `Pages` grows to `["Timing", "Voice"]`.
- In `OnPageReset(page)`, when `page == "Voice"`: `SetCursorFillMode(TOP_TO_BOTTOM)` then
  `_volOID = AddSliderOption("Player voice volume", fPlayerVoiceVol, "{0}%")` (new `Int _volOID`).
- `OnOptionSliderOpen(oid)`: for `_volOID` → `SetSliderDialogRange(0, 200)`,
  `SetSliderDialogDefaultValue(100)`, `SetSliderDialogInterval(5)`, `SetSliderDialogStartValue(fPlayerVoiceVol)`.
- `OnOptionSliderAccept(oid, value)`: for `_volOID` → `fPlayerVoiceVol = value`;
  `SetSliderOptionValue(oid, value, "{0}%")`; `DBVOTweaks.SetPlayerVoiceVolume(fPlayerVoiceVol / 100.0)`.
- In the existing `OnGameReload()` (after `Parent.OnGameReload()` / the v2 `RegisterForMenu`):
  `DBVOTweaks.SetPlayerVoiceVolume(fPlayerVoiceVol / 100.0)` — re-push the persisted value every load
  (the DLL keeps no saved state).

**Constraints:**
- Existing "Timing" page (`_mspwOID`/`_padOID`) and the `OnMenuOpen` `UI.SetFloat` push are unchanged —
  this is additive. Dispatch on `oid` must keep all three options separate.
- The MCM compiles against vanilla + skse + the vendored `skyui/` sources + `DBVOTweaks.psc` (same dir).
- `fPlayerVoiceVol` is the single source of truth (no SKSE co-save); `/100.0` converts percent → factor.

**Test Cases (verification):**
- `tools/compile-papyrus.sh DBVOTweaks mods/DBVODialogueTweaks/src/papyrus /tmp/pex` → `DBVOTweaks.pex`, no errors.
- `tools/compile-papyrus.sh DBVODialogueTweaksMCM mods/DBVODialogueTweaks/src/papyrus /tmp/pex` →
  `DBVODialogueTweaksMCM.pex`, no errors (resolves `DBVOTweaks` + the SkyUI base).

**Commit after passing.**

---

### Task 3: DLL scaffold that loads + build.sh 4-step wiring [Mode: Direct]

**Files:**
- Create: `mods/DBVODialogueTweaks/plugin/CMakeLists.txt`
- Create: `mods/DBVODialogueTweaks/plugin/src/main.cpp`
- Modify: `mods/DBVODialogueTweaks/build.sh`

**Contracts:**
- `CMakeLists.txt`: structure identical to `mods/AutoFireBow/CMakeLists.txt` with `project(DBVODialogueTweaks …)`
  and target renamed; same spdlog pin, same CommonLibSSE-NG `GIT_TAG`, `cxx_std_23`, `PREFIX ""` / `SUFFIX ".dll"`.
- `main.cpp` (scaffold only, no behavior yet): `SetupLog()` → `<SKSE log dir>/DBVODialogueTweaks.log`
  (AutoFireBow idiom); `SKSEPluginInfo(.Name="DBVODialogueTweaks", .RuntimeCompatibility=AddressLibrary,
  .StructCompatibility=Independent)`; `SKSEPluginLoad` calls `SetupLog()`, `SKSE::Init(a_skse)`, logs
  `"DBVODialogueTweaks <ver> loaded"`.
- `build.sh` renumber to 4 steps (esp stays last; DLL appended):
  - `[1/4]` swf (ffdec) — unchanged.
  - `[2/4]` Papyrus — compile **both** `DBVODialogueTweaksMCM.psc` **and** `DBVOTweaks.psc` →
    `build/Scripts/`.
  - `[3/4]` esp (EspGen) — unchanged (no esp change; the native script attaches to nothing).
  - `[4/4]` DLL — `cmake -S plugin -B plugin/build` with `tools/skse/cmake/clang-cl-msvc.cmake`
    (source `tools/skse/cross-env.sh` first), `cmake --build`. On `--install`: `cp` the built DLL →
    `$GAME_DATA/SKSE/Plugins/DBVODialogueTweaks.dll`, and copy `DBVOTweaks.pex` alongside the existing
    pex into `$GAME_DATA/Scripts/`. Mirror the existing pre-install md5 logging for each copied file.

**Test Cases (verification):**
- `./mods/DBVODialogueTweaks/build.sh` exits 0; all artifacts present:
  `build/{DBVODialogueTweaks.esp, Interface/dialoguemenu.swf, Scripts/DBVODialogueTweaksMCM.pex,
  Scripts/DBVOTweaks.pex}` and `file plugin/build/DBVODialogueTweaks.dll` → `PE32+ … DLL … x86-64`.
- `./mods/DBVODialogueTweaks/build.sh --install` copies the dll to `$GAME_DATA/SKSE/Plugins/` and
  `DBVOTweaks.pex` to `$GAME_DATA/Scripts/`.
- After launching the game: `DBVODialogueTweaks.log` exists and contains `… loaded`.

**Constraints:** no behavior yet — this proves the DLL builds on the moved toolchain, the 4-step build
assembles every artifact, and the plugin loads.

**Commit after passing.**

---

### Task 4: The hook + per-handle SetVolume + native registration [Mode: Delegated]

**Files:**
- Modify: `mods/DBVODialogueTweaks/plugin/src/main.cpp`

**Contracts:**
- **Papyrus native** — register via `SKSE::GetPapyrusInterface()->Register(RegisterFuncs)` in
  `SKSEPluginLoad`:
  ```cpp
  static std::atomic<float> g_dbvoVolume{ 1.0f };
  void SetPlayerVoiceVolume(RE::StaticFunctionTag*, float factor) { g_dbvoVolume = factor; }
  bool RegisterFuncs(RE::BSScript::IVirtualMachine* vm) {
      vm->RegisterFunction("SetPlayerVoiceVolume", "DBVOTweaks", SetPlayerVoiceVolume);
      return true;
  }
  ```
  Log the registration. (Class string `"DBVOTweaks"` must match `DBVOTweaks.psc`.)
- **Trampoline hook** on `Actor::SpeakSoundFunction`, installed in `SKSEPluginLoad` right after
  `SKSE::Init` (matches AutoFireBow's at-load `InstallHooks()`):
  ```cpp
  REL::Relocation<std::uintptr_t> target{ REL::RelocationID(36541, 37542) };
  SKSE::AllocTrampoline(14);
  func = SKSE::GetTrampoline().write_branch<5>(target.address(), thunk);
  ```
  Thunk signature (from TiltedEvolution; arg2 is the out `BSSoundHandle*` the engine fills + starts):
  ```cpp
  static bool thunk(RE::Actor* a_this, const char* a_path, RE::BSSoundHandle* a_handle,
                    std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                    std::uint64_t a7, std::uint64_t a8, std::uint64_t a9,
                    bool a10, std::uint64_t a11, bool a12, bool a13, bool a14);
  ```
  Behavior: call `func(...)` first (passes a4–a14 through untouched), then:
  ```cpp
  if (a_this && a_this->IsPlayerRef() && a_path && a_handle && a_handle->IsValid()
      && is_dbvo_path(a_path)) {
      a_handle->SetVolume(g_dbvoVolume.load());
  }
  return r;
  ```
- `is_dbvo_path(const char*)`: case-insensitive — true iff the path begins with `"DBVO/"` **or**
  `"DBVO\\"`.
- Do **not** retain `a_handle` past the call (engine owns it). If a frame-1 race appears, defer the
  `SetVolume` via `SKSE::GetTaskInterface()->AddTask` with a **by-value** copy of the 12-byte handle.

**Constraints:**
- Resolve the address **only** via `REL::RelocationID(36541, 37542)` (Address Library) — no hardcoded
  1.6.1170 offset.
- The hook must be behavior-neutral when `g_dbvoVolume == 1.0` and for every non-(player+DBVO) call
  (plain pass-through). `SetVolume`/`IsValid`/`BSSoundHandle` come from CommonLibSSE-NG
  `RE/B/BSSoundHandle.h`.
- Add a throttled debug log on the first few player+DBVO hits (path + applied volume) for bring-up;
  gate it so it doesn't flood. Remove or quiet before the final commit.

**Test Cases (verification):**
- `./mods/DBVODialogueTweaks/build.sh` → DLL rebuilds, PE32+.
- After launch: `DBVODialogueTweaks.log` shows the native registered and the hook installed (no
  trampoline/Address-Library error). Full behavioral proof is Task 5.

**Commit after passing.**

---

### Task 5: In-game end-to-end verification (skytest, human-in-loop) [Mode: Direct]

Not automatable — requires launching Skyrim. Install the full stack into a skytest **full profile**
(per the "which mode?" rule — only manifests on the live load order): DBVO + Karat + SkyUI + this mod
(swf + 2×pex + esp + dll). Build+install via `./mods/DBVODialogueTweaks/build.sh --install`.

**Checklist (the "Done when"):**
- [ ] `DBVODialogueTweaks.log`: plugin loaded, native registered, hook installed.
- [ ] MCM "DBVO Dialogue Tweaks" shows a **"Voice"** page with a **Player voice volume** slider at 100%;
      the v2 "Timing" sliders still work.
- [ ] Slider **50%** → player line audibly quieter; **NPC reply volume unchanged** (proves per-source
      isolation, not a category-wide change).
- [ ] Slider **0%** → player line silent; NPC reply normal; dialogue still advances (v1/v2 intact).
- [ ] Slider **100%** → indistinguishable from stock DBVO.
- [ ] Slider **150%** → record whether the player line gets louder or clamps at 100% (the boost-clamp
      question → `docs/ideas.md` follow-up; if clamped, note for the cap-at-100 decision).
- [ ] Value **survives save→reload** (set 50%, reload, confirm still attenuated via `OnGameReload` push).
- [ ] v1 skip (E / left-click mid-line) and v2 timing sliders unregressed.

Quiet/remove the Task-4 bring-up trace, rebuild, commit final.

---
## Execution
**Skill:** superpowers:subagent-driven-development
- Mode A (Direct — Opus): Tasks 1, 3, 5
- Mode B (Delegated — subagent): Tasks 2 (Papyrus native + MCM), 4 (the SKSE hook)

**Order / deps:** 1 → 2 → 3 → 4 → 5 (strict). 1 unblocks everything (DLL home + toolchain). 2 creates
both `.psc` so 3's `[2/4]` build step compiles them. 3 scaffolds the DLL + full build wiring. 4 adds the
hook/native into the built DLL. 5 verifies the assembled stack in-game.
