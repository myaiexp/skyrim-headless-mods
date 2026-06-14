# AutoFireBow MCM — design (2026-06-14)

Add an in-game **SkyUI MCM** to AutoFireBow so its behavior is configurable instead of
hardcoded-always-on. Built **without MCM Helper** — a classic `SKI_ConfigBase` Papyrus menu plus a
C++↔Papyrus native bridge, exactly the shape DBVODialogueTweaks v3 already ships.

**Supersedes** the INI-config plan in `docs/ideas.md` (2026-06-08 "AutoFireBow config"). That entry
chose an INI read by the DLL specifically to stay zero-dependency and avoid the Papyrus tier; this
design reverses that on the user's explicit call to have a real in-game menu. The INI approach and
the other menu routes are recorded under "Alternatives considered" below.

## Starting point

AutoFireBow is currently a **single-file, zero-dependency** SKSE DLL (`src/main.cpp`) — no `.esp`, no
Papyrus, no config. Everything is hardcoded:

- Auto-fire is **always on** while the attack control is held.
- `constexpr float kAutoDamageMult = 1.10f` — the DPS-compensation bump applied to auto arrows.
- The old power **clamp** is already gone (`AutoArrowHook` only multiplies `weaponDamage`; auto
  arrows loose at genuine full draw), so the "auto-fire vs clamp" split from the old ideas entry
  collapses to a single auto-fire on/off.

## Decision

A classic **SkyUI MCM** (`SKI_ConfigBase`), no MCM Helper. This makes **SkyUI a hard requirement**
for AutoFireBow — inherent to "an MCM without MCM Helper," since SkyUI *is* the MCM framework. SKSE
is already required; the new user-facing dependency is SkyUI only.

## Architecture — three new pieces + a one-way bridge

AutoFireBow grows into the DBVO shape (minus the swf — there is no flash UI to edit):

| Piece | File | Role |
| --- | --- | --- |
| MCM menu | `src/papyrus/AutoFireBowMCM.psc` | `extends SKI_ConfigBase`; one page; holds settings as `Auto` properties — **the source of truth**, persisted per-save. |
| Native bridge | `src/papyrus/AutoFireBow.psc` | `Hidden`; `Global Native` setters the DLL registers (mirrors `DBVOTweaks.psc`). |
| Plugin | `AutoFireBow.esp` (ESL-flagged) | A Start-Game-Enabled quest hosting the MCM script + a `SKI_PlayerLoadGameAlias` player alias. Generated headlessly by EspGen (Mutagen). |

**The bridge is one-way: Papyrus → DLL.** An MCM option change calls a native setter; the DLL stores
the value in an atomic and acts on it. On `OnGameReload` the script re-pushes *all* current values
**and re-establishes `RegisterForKey` for the hotkey** (key registrations don't survive a reload, so
this must re-run every load — analogous to DBVO re-running `RegisterForMenu`), so a save's settings
re-apply every load. The DLL never reads back — **Papyrus owns the values, the DLL owns the
behavior**. This keeps persistence entirely in the co-save (no INI, no DLL-side file I/O) and matches
DBVO.

## The MCM page + storage

Single page ("AutoFireBow"), four controls. Settings persist **per-save** via the script's `Auto`
properties — the SkyUI convention (a reversal of the old INI plan's "global," but the natural fit for
a menu).

| Control | Type | Default | Bridge call |
| --- | --- | --- | --- |
| Enabled | toggle | on | `SetEnabled(bool)` |
| Toggle hotkey | keymap | unbound | *(none — see below)* |
| Auto-arrow damage bonus | slider, % (0–100) | 10% | `SetDamageBonus(float)` |
| Min shot delay | slider, ms (0–1000) | 0 | `SetMinShotDelay(float)` |

- **Toggle hotkey needs no C++ input hook.** SkyUI's `AddKeyMapOption` + `RegisterForKey`/`OnKeyDown`
  let the Papyrus script flip its own `bEnabled` property and call the same `SetEnabled` setter the
  toggle uses — key and on-screen toggle stay in sync, and the DLL's existing `AttackInputSink` is
  untouched.
- **Damage bonus** exposes `kAutoDamageMult` as a 0–100% slider (10% → ×1.10). Capped at 0–100% — a
  bonus, not a nerf surface; revisit the range only if asked. **Unit convention:** the bridge passes
  the *finished multiplier* — Papyrus sends `SetDamageBonus(1.0 + pct/100.0)` (10% → 1.10) and the
  DLL stores it verbatim into `g_damageMult`, mirroring DBVO passing a ready-to-use factor (no
  percent↔multiplier math DLL-side, no off-by-100 at the boundary).
- **DLL defaults match script defaults** (enabled, +10%, 0 ms) so behavior is correct *before* the
  first push — e.g. on a fresh load in the window before `OnGameReload` fires, or a mid-session
  install. This is the *only* thing protecting a freshly-seeded `Auto` property from a `0`/`false`
  initial value before the first MCM interaction, so the plan must verify the DLL defaults genuinely
  cover that window.
- **No version-migration block at v1.** AutoFireBow has never shipped an MCM, so there is no prior
  save whose persisted page list / properties need migrating — DBVO's `GetVersion`/`OnVersionUpdate`
  dance is *not* needed here. Add one only if a *later* version adds properties to an
  already-released MCM (a new `Auto` property is not guaranteed its declared default on an existing
  save).

## DLL changes (`src/main.cpp`)

- Replace `constexpr float kAutoDamageMult` with `std::atomic<float> g_damageMult{1.10f}`; add
  `std::atomic<bool> g_enabled{true}` and `std::atomic<float> g_minShotDelayMs{0.0f}`.
- **Gate:** `BowLoopSink`'s `BowDrawn` branch checks `g_enabled` before `LooseNow()`. Disabled = the
  sink no-ops; manual play stays 100% vanilla. Sinks remain registered unconditionally (no
  install/uninstall churn on toggle).
- **Damage:** `AutoArrowHook::thunk` reads `g_damageMult.load()` instead of the constant.
- **Register natives:** in `SKSEPluginLoad`, register `SetEnabled` / `SetDamageBonus` /
  `SetMinShotDelay` on script `AutoFireBow` via `GetPapyrusInterface()->Register(...)` — the same
  call DBVO uses for `SetPlayerVoiceVolume`.

## Cadence cap — the one piece needing new machinery

The other three controls just *gate or scale* existing behavior. Min-shot-delay is the only one that
needs **timing the mod doesn't currently have**. Insertion point: delay the **re-nock press** rather
than the loose — after an auto arrow launches (`AutoArrowHook`), wait the delay, then inject the
synthetic re-draw. Effective floor between shots ≈ `delay` + draw time.

- **`delay == 0` (default):** the existing immediate re-nock path runs unchanged — zero overhead, no
  thread.
- **`delay > 0`:** a short-lived detached timer thread sleeps the delay, then enqueues the synthetic
  press through `SKSE::GetTaskInterface()`. The thread only sleeps and enqueues — it **never touches
  game state directly** (the press runs on the game thread via the task interface), so it is safe.

(A `Main::Update` poll hook is the thread-free alternative but adds an Address-Library hook + a polled
flag for one optional feature; the opt-in timer thread is simpler and lower-risk. Revisit only if the
thread approach misbehaves.)

## Build pipeline

`build.sh` grows from 1 step to 3, mirroring DBVO's `build.sh` (no swf step):

1. Compile `AutoFireBowMCM.psc` + `AutoFireBow.psc` → `build/Scripts/` via
   `tools/compile-papyrus.sh` (against the vendored SkyUI sources; ship only our own `.pex`).
2. Generate `AutoFireBow.esp` via EspGen:
   `EspGen … AutoFireBow.esp AutoFireBowMCMQuest AutoFireBowMCM "AutoFireBow" --player-alias SKI_PlayerLoadGameAlias`.
3. Build the DLL (the existing clang-cl + xwin cross-build).

`--install` copies the esp + 2 `.pex` + dll into `Data/` and `Data/SKSE/Plugins/`, and activates the
esp in `Plugins.txt` (leading `*`). Mirror DBVO's per-file pre-install md5 echo for revertability.

## Testing

- **Auto-fire mechanic** (gate on/off, damage scaling, cadence): standalone — `skytest test
  AutoFireBow` (vanilla+1).
- **MCM page itself:** the menu needs **SkyUI**, which the vanilla+1 profile lacks → full-profile
  install + `skytest play` (the same route DBVO's MCM is verified through). Confirm: the page
  appears, controls bind, the hotkey toggles Enabled, the damage/delay sliders change behavior, and
  all four settings survive save → reload (per-save persistence).

## Alternatives considered

- **INI read by the DLL** (the prior v1 decision) — zero dependencies, stays pure-C++, settings
  global. Rejected here only because the user explicitly wants an in-game menu; it remains the
  lightest option and is the reason AutoFireBow was zero-dep. Not resurrected.
- **MCM Helper** — JSON-driven menu, no hand-authored `SKI_ConfigBase` script, and can back settings
  with an INI the DLL reads. Rejected: adds **MCM Helper** as a second required mod on top of SkyUI.
  The user asked specifically for "an MCM without MCM Helper."
- **SKSE Menu Framework (ImGui)** — pure-C++ in-game settings panel, **zero** required mods, no
  esp/Papyrus. Rejected: it is a dev-style overlay, not the familiar pause-menu MCM the user wants;
  the point of this work is the standard MCM UX.

## Why this shape

The DBVO precedent in this repo means none of the moving parts are novel: `SKI_ConfigBase` page,
`Hidden` native bridge, EspGen quest+alias, `compile-papyrus.sh`, the `--install` flow. Following it
keeps AutoFireBow consistent with the only other MCM-bearing mod here and avoids inventing a second
pattern for the same job. The one-way Papyrus→DLL bridge keeps persistence in the co-save and the DLL
stateless across saves — the DLL just reflects whatever the active save's MCM last pushed.
