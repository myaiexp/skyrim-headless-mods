# AutoFireBow MCM Implementation Plan

**Goal:** Give AutoFireBow a classic SkyUI MCM (no MCM Helper) so Enabled / toggle-hotkey /
damage-bonus / min-shot-delay are configurable in-game instead of hardcoded.

**Architecture:** A one-way Papyrus→DLL bridge. A `SKI_ConfigBase` MCM script owns the settings
(per-save `Auto` properties) and pushes them to the DLL through `Global Native` setters; the DLL
stores each in an atomic and reflects it in the existing bow-loop hooks. An ESL-flagged esp hosts the
quest + player alias. Mirrors the DBVODialogueTweaks v3 shape exactly.

**Tech Stack:** SKSE C++ (CommonLibSSE-NG, clang-cl + xwin cross-build), Papyrus (`SKI_ConfigBase`
against vendored SkyUI sources, wine PapyrusCompiler), Mutagen/EspGen for the esp.

**Design:** `docs/plans/autofirebow-mcm-design.md`

> **Verification note:** this repo has no unit-test framework — an SKSE+Papyrus+esp mod is verified by
> (a) the build producing the expected artifacts and (b) in-game behavior via `skytest`. "Test Cases"
> below are therefore build assertions and scripted in-game checks, not pytest-style tests. TDD in the
> classic sense doesn't apply; each task's verification is its acceptance gate before commit.

> **Concurrency:** another session holds uncommitted changes under `mods/DBVODialogueTweaks/`. Every
> task here touches only `mods/AutoFireBow/**`. **Stage by filename** (`git add mods/AutoFireBow/...`);
> never `git add -A`/`.`. Confirm `git status` before each commit.

---

## File Structure

| File | Create/Modify | Responsibility |
| --- | --- | --- |
| `mods/AutoFireBow/src/main.cpp` | Modify | Config atomics; register 3 Papyrus natives; gate the loop on `g_enabled`; read `g_damageMult`; apply `g_minShotDelayMs` to the re-nock. |
| `mods/AutoFireBow/src/papyrus/AutoFireBowMCM.psc` | Create | `SKI_ConfigBase` menu — one page, 4 controls, per-save `Auto` properties, push-on-change + push-on-reload. |
| `mods/AutoFireBow/src/papyrus/AutoFireBow.psc` | Create | `Hidden` native bridge — the 3 `Global Native` setter declarations the DLL registers. |
| `mods/AutoFireBow/build.sh` | Modify | Grow from 1 step (DLL) to 3 (compile 2×pex → EspGen esp → DLL); extend `--install` to copy pex+esp and activate the esp. |
| `mods/AutoFireBow/CMakeLists.txt` | — | No change — `GetPapyrusInterface()` is already in CommonLibSSE-NG; no new deps, no MinHook. |

Defaults are defined **once per concept** and must agree on both sides:
`Enabled=true`, `DamageBonus=10%` → multiplier `1.10`, `MinShotDelay=0 ms`.

---

### Task 1: DLL — config state, native bridge, loop gating, cadence [Mode: Direct]

**Files:**
- Modify: `mods/AutoFireBow/src/main.cpp`

**Contracts:**

Add three config atomics (replacing `constexpr float kAutoDamageMult`):
```cpp
std::atomic<bool>  g_enabled{ true };
std::atomic<float> g_damageMult{ 1.10f };     // finished multiplier; 1.10 = +10%
std::atomic<float> g_minShotDelayMs{ 0.0f };  // 0 = re-nock immediately (current behavior)
```

Three native setters + registration (class string `"AutoFireBow"` MUST match the `.psc`):
```cpp
void SetEnabled(RE::StaticFunctionTag*, bool v)        { g_enabled.store(v); }
void SetDamageBonus(RE::StaticFunctionTag*, float mult) { g_damageMult.store(mult); }   // receives 1.0+pct/100
void SetMinShotDelay(RE::StaticFunctionTag*, float ms)  { g_minShotDelayMs.store(ms < 0.f ? 0.f : ms); }

bool RegisterPapyrus(RE::BSScript::IVirtualMachine* vm) {
    vm->RegisterFunction("SetEnabled",      "AutoFireBow", SetEnabled);
    vm->RegisterFunction("SetDamageBonus",  "AutoFireBow", SetDamageBonus);
    vm->RegisterFunction("SetMinShotDelay", "AutoFireBow", SetMinShotDelay);
    return true;
}
// In SKSEPluginLoad, after SKSE::Init: SKSE::GetPapyrusInterface()->Register(RegisterPapyrus);
```

Wiring into the existing hooks:
- **Gate (`BowLoopSink::ProcessEvent`, `BowDrawn` branch):** loose only when enabled —
  `if (g_enabled.load() && AttackHeld() && !g_firedThisCycle) { LooseNow(); }`. Disabled = no-op;
  manual play untouched. Sinks stay registered unconditionally.
- **Damage (`AutoArrowHook::thunk`):** `runtime.weaponDamage *= g_damageMult.load();` (was the
  constant). Keep the existing `g_boostNextArrow` gate + log line.
- **Cadence (the re-nock inside `AutoArrowHook::thunk`):** factor the existing "re-nock press" task
  into a callable, then schedule it by delay:
```cpp
auto enqueueRenock = []() {
    if (auto* t = SKSE::GetTaskInterface()) {
        t->AddTask([]() {
            if (g_enabled.load() && AttackHeld()) {
                SendSyntheticAttack(true);
                SKSE::log::info("AutoFireBow: re-nock press injected (loop continues)");
            }
        });
    }
};
const float delayMs = g_minShotDelayMs.load();
if (delayMs > 0.0f) {
    std::thread([delayMs, enqueueRenock]() {
        std::this_thread::sleep_for(std::chrono::duration<float, std::milli>(delayMs));
        enqueueRenock();   // thread only sleeps + enqueues; the press runs on the game thread
    }).detach();
} else {
    enqueueRenock();       // default path: unchanged, no thread
}
```
  Add `#include <atomic>`, `#include <thread>`, and `#include <chrono>` (the current `main.cpp` has
  none of these; DBVO's precedent includes `<atomic>` explicitly). The `g_enabled` re-check in the
  task means a toggle-off (or hotkey-off) during the delay cancels the next shot.

**Constraints:**
- Atomics only (Papyrus VM thread writes; game thread reads) — no locks.
- The detached thread must touch **no** game state directly; it only sleeps and calls
  `GetTaskInterface()->AddTask`. The press executes on the game thread.
- Default behavior with no MCM present and before any push must equal today's mod (enabled, +10%,
  immediate re-nock) — guaranteed by the atomic initializers.

**Test Cases (build assertions):**
- `cd mods/AutoFireBow && ./build.sh` compiles cleanly (build.sh is still DLL-only at this task).
- `mods/AutoFireBow/build/AutoFireBow.dll` exists and `file` reports a PE32+ DLL.
- `grep -c RegisterFunction src/main.cpp` == 3.

**Verification:**
Run: `cd mods/AutoFireBow && ./build.sh && file build/AutoFireBow.dll`
Expected: builds; `PE32+ executable (DLL) (console) x86-64, for MS Windows`.

**Commit after passing** (`git add mods/AutoFireBow/src/main.cpp`).

---

### Task 2: Papyrus scripts — MCM menu + native bridge [Mode: Direct]

**Files:**
- Create: `mods/AutoFireBow/src/papyrus/AutoFireBow.psc`
- Create: `mods/AutoFireBow/src/papyrus/AutoFireBowMCM.psc`

**Contract — `AutoFireBow.psc` (native bridge):**
```papyrus
Scriptname AutoFireBow Hidden
; Registered by AutoFireBow.dll. One-way Papyrus -> DLL config push.
Function SetEnabled(Bool abEnabled) Global Native
Function SetDamageBonus(Float aMult) Global Native    ; finished multiplier: 1.0 + pct/100 (1.10 = +10%)
Function SetMinShotDelay(Float aMs) Global Native
```

**Contract — `AutoFireBowMCM.psc` (`extends SKI_ConfigBase`):**

Properties (per-save source of truth, with defaults matching the DLL):
- `Bool  Property bEnabled    = True  Auto`
- `Float Property fDamageBonus = 10.0 Auto`  (percent shown to the user)
- `Float Property fMinShotDelay = 0.0 Auto`  (ms)
- `Int   Property iToggleKey   = -1   Auto`  (DXScanCode; -1 = unbound)

Behavior:
- `OnConfigInit()` — `ModName = "AutoFireBow"`; single page `Pages = new String[1]` = `"Settings"`.
- `OnPageReset(page)` — `SetCursorFillMode(TOP_TO_BOTTOM)`; add, capturing OIDs:
  - `AddToggleOption("Enabled", bEnabled)`
  - `AddKeyMapOption("Toggle hotkey", iToggleKey)`
  - `AddSliderOption("Auto-arrow damage bonus", fDamageBonus, "{0}%")`
  - `AddSliderOption("Min shot delay", fMinShotDelay, "{0} ms")`
- `OnOptionSelect(oid)` — for the Enabled toggle: flip `bEnabled`, `SetToggleOptionValue(oid, bEnabled)`,
  `AutoFireBow.SetEnabled(bEnabled)`.
- `OnOptionKeyMapChange(oid, keyCode, conflictControl, conflictName)` — set `iToggleKey = keyCode`,
  `SetKeyMapOptionValue(oid, keyCode)`, then re-`RegisterForKey` (unregister old first).
- `OnOptionSliderOpen(oid)` — damage: range 0–100, default 10, interval 5, start `fDamageBonus`;
  delay: range 0–1000, default 0, interval 25, start `fMinShotDelay`.
- `OnOptionSliderAccept(oid, value)`:
  - damage → `fDamageBonus = value`; `SetSliderOptionValue(oid, value, "{0}%")`;
    `AutoFireBow.SetDamageBonus(1.0 + value / 100.0)`.
  - delay → `fMinShotDelay = value`; `SetSliderOptionValue(oid, value, "{0} ms")`;
    `AutoFireBow.SetMinShotDelay(value)`.
- `OnKeyDown(keyCode)` — if `keyCode == iToggleKey` and not in a menu/text field: flip `bEnabled`;
  `AutoFireBow.SetEnabled(bEnabled)`; `Debug.Notification("AutoFireBow " + ...)`.
- `OnGameReload()` — `Parent.OnGameReload()`; **re-push all three** values to the DLL
  (`SetEnabled(bEnabled)`, `SetDamageBonus(1.0 + fDamageBonus/100.0)`, `SetMinShotDelay(fMinShotDelay)`);
  and `If iToggleKey >= 0: RegisterForKey(iToggleKey)` (key registration does not survive a reload).
- **No `GetVersion`/`OnVersionUpdate`** — first MCM AutoFireBow ships; no prior save to migrate.

**Constraints:**
- The native class string in both files is exactly `AutoFireBow` (matches Task 1's `RegisterFunction`).
- Ship only our two `.pex`; compile *against* vendored SkyUI but never ship SkyUI's `.pex`.
- Keymap edits must unregister the previous key before registering the new one (avoid stale binds).

**Test Cases (compile assertions):**
```bash
cd ~/Projects/skyrim-headless-mods
tools/compile-papyrus.sh AutoFireBow    mods/AutoFireBow/src/papyrus /tmp/afb-pex
tools/compile-papyrus.sh AutoFireBowMCM mods/AutoFireBow/src/papyrus /tmp/afb-pex
# both must exit 0 and produce the .pex:
test -f /tmp/afb-pex/AutoFireBow.pex && test -f /tmp/afb-pex/AutoFireBowMCM.pex
```

**Verification:**
Run the two `compile-papyrus.sh` calls above.
Expected: both exit 0; `AutoFireBow.pex` and `AutoFireBowMCM.pex` produced (MCM resolves
`SKI_ConfigBase` from the vendored SkyUI sources).

**Commit after passing** (`git add mods/AutoFireBow/src/papyrus/AutoFireBow.psc
mods/AutoFireBow/src/papyrus/AutoFireBowMCM.psc`).

---

### Task 3: Build pipeline — pex + esp + install [Mode: Direct]

**Files:**
- Modify: `mods/AutoFireBow/build.sh`

**Contracts:** restructure to 3 numbered steps mirroring `mods/DBVODialogueTweaks/build.sh` (drop the
swf step). Identity vars at top:
```bash
ESP="AutoFireBow.esp"
QUEST_EDID="AutoFireBowMCMQuest"
MCM_SCRIPT="AutoFireBowMCM"
NATIVE_SCRIPT="AutoFireBow"
FULLNAME="AutoFireBow"
PLAYER_ALIAS="SKI_PlayerLoadGameAlias"
```
- **[1/3] Papyrus:** `tools/compile-papyrus.sh "$MCM_SCRIPT"    "$HERE/src/papyrus" "$BUILD/Scripts"`
  and the same for `"$NATIVE_SCRIPT"`.
- **[2/3] esp:** `source "$REPO_ROOT/tools/env.sh"`; then
  `"$DOTNET" run --project "$REPO_ROOT/tools/EspGen" -- "$BUILD/$ESP" "$QUEST_EDID" "$MCM_SCRIPT" "$FULLNAME" --player-alias "$PLAYER_ALIAS"`.
- **[3/3] DLL:** the existing clang-cl cross-build (unchanged), output `build/AutoFireBow.dll`.
- **`--install`:** copy `build/AutoFireBow.dll` → `$GAME_DATA/SKSE/Plugins/`, the two `.pex` →
  `$GAME_DATA/Scripts/`, and `$BUILD/$ESP` → `$GAME_DATA/`; echo each destination's pre-install md5
  for revertability; activate the esp in `$PLUGINS_TXT` (leading `*`, idempotent — reuse DBVO's
  grep/sed/append block). Print the "FULLY restart Skyrim (Papyrus VM caches .pex)" note.

**Constraints:**
- `BUILD="$HERE/build"`; DLL stays at `build/AutoFireBow.dll` (AutoFireBow keeps the DLL in `build/`,
  not a `plugin/build/` — it has no `plugin/` subdir, unlike DBVO). Put `Scripts/` and the esp under
  the same `build/`.
- Keep `set -euo pipefail`. Don't break the bare `./build.sh` (no-arg) path.

**Test Cases (artifact assertions):**
```bash
cd mods/AutoFireBow && ./build.sh
test -f build/AutoFireBow.dll
test -f build/Scripts/AutoFireBowMCM.pex
test -f build/Scripts/AutoFireBow.pex
test -f build/AutoFireBow.esp
```

**Verification:**
Run: `cd mods/AutoFireBow && ./build.sh && ls -la build/AutoFireBow.dll build/AutoFireBow.esp build/Scripts/*.pex`
Expected: all four artifacts present; exit 0.

**Commit after passing** (`git add mods/AutoFireBow/build.sh`).

---

### Task 4: In-game verification via skytest [Mode: Direct]

**Files:** none (verification only — may update `mods/AutoFireBow/README.md` if one exists / the
per-mod README table needs the new SkyUI dependency noted).

**Two surfaces, two profiles** (per `skytest/README.md` + project CLAUDE.md):

1. **Auto-fire mechanic (standalone, vanilla+1):** `skytest test AutoFireBow`. Drive in-world with a
   bow equipped; hold attack; confirm the rapid auto-fire loop still works post-refactor (gate
   defaults on, damage bump applies). Confirms the DLL changes didn't regress the core loop with no
   MCM present (atomics at defaults).
2. **MCM page (needs SkyUI → full profile):** the vanilla+1 profile has no SkyUI, so install into the
   live game (`./build.sh --install`) and `skytest play`. Verify:
   - **Page present:** Esc → Mod Configuration → "AutoFireBow" → "Settings" page lists all 4 controls.
   - **Enabled toggle:** off → holding attack no longer auto-fires (manual shots only); on → resumes.
   - **Hotkey:** bind a key; in-world it flips Enabled (Debug.Notification shows state); stays in sync
     with the toggle.
   - **Damage bonus:** set 0% → auto arrows do vanilla damage; set 50% → noticeably higher (probe via
     SkytestProbe `exec`/trace if convenient, else observe).
   - **Min shot delay:** set ~500 ms → visibly slower cadence between auto-shots; 0 → fastest.
   - **Persistence:** change all four, save, reload → values restored on the MCM page and behavior
     matches (per-save `Auto` props + `OnGameReload` re-push).

**Constraints:**
- Full restart between rebuilds (Papyrus VM caches `.pex` per session) — `--install` already warns.
- This task is the acceptance gate for the whole feature; do not mark the feature done until both
  surfaces pass.

**Verification:** the checklist above, executed via `skytest`.

**Commit** any README/doc touch-ups (`git add mods/AutoFireBow/README.md` only if changed).

---

## Execution
**Skill:** superpowers:subagent-driven-development
- Mode A tasks: Opus implements directly
- All four tasks are **Mode: Direct** — this is a precise port of the in-repo DBVO MCM pattern; the
  only novel code (the cadence timer thread) is fully specified above. No subagent dispatch needed.
