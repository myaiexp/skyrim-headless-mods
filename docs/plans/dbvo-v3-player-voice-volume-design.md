# DBVODialogueTweaks v3 — player-voice volume slider — design

**Mod:** `mods/DBVODialogueTweaks/`. The first **v3 (SKSE C++) tier** increment: an MCM slider
that scales the volume of **only the player character's DBVO voice line**, independent of NPC
dialogue and all other game audio.

| Phase  | Feature                                                                 | Tier                | Status                          |
| ------ | ----------------------------------------------------------------------- | ------------------- | ------------------------------- |
| v1     | Manual player-line skip (E / left-click)                                | swf only            | shipped                         |
| v2     | Configurable response gap (pad ms + ms/word), MCM sliders               | swf + Papyrus + MCM | shipped                         |
| **v3** | **Player-voice volume slider** (per-handle attenuation)                 | **SKSE C++ + MCM**  | **this design — building now**  |
| v3+    | Cut player-line tail on skip; exact `.fuz`-duration NPC scheduling       | SKSE C++            | deferred (same tier; see ideas) |

The v3 SKSE tier was originally roadmapped as "cut in-flight voice audio." The **volume slider is
the first thing built on that tier** because it shares the exact same enabling primitive — a handle
on the player's voice sound instance — and is self-contained and testable in isolation. Cut-on-skip
and exact pacing snap onto the same hook later (`docs/ideas.md`).

## Goal

The player's DBVO line (AI voice pack, e.g. Karat) is mastered/played louder than Bethesda NPC VO,
so it sits "desynced" against NPC replies. Give the user a **0–200% slider** (default 100% = no
change) to dial the player line up or down. Attenuation (0–100%) is the load-bearing case; boost
(100–200%) is best-effort (see *Value mapping & the boost caveat*).

**Why nothing cheaper works** (both ruled out empirically/by analysis — see *Why this shape*):
DBVO emits the line as the console command `Player.SpeakSound "DBVO/<file>.fuz"` (via ConsoleUtilSSE),
which returns no handle and **accepts no volume or category argument** (confirmed in-game: appending
` Voice` breaks the command → silence). So neither the swf/MCM layer nor a Papyrus category re-route
can attenuate just that one source. Per-source volume requires the SKSE audio layer.

## Prerequisite (Step 0) — repo layout unification

The SKSE DLL belongs to DBVODialogueTweaks, but the repo currently splits mods by **implementation
tier**: `plugins/` holds DLL-only mods *and* the SKSE cross-compile toolchain, `mods/` holds
everything else. A multi-tier mod (swf + Papyrus + esp + DLL) fits neither bucket. The fix is to
categorize by **role**, not tier:

```
mods/<Name>/            every mod, any tier mix (esp / Papyrus / swf / DLL)
  AutoFireBow/          (DLL-only)        ← moved from plugins/
  GhostAllies/          (DLL-only)        ← moved from plugins/
  OneClickTravel/          (DLL-only)        ← moved from plugins/
  RapidBowHold/         (esp+pex)         ← already here; unchanged
  DBVODialogueTweaks/
    src/  …             (existing swf/Papyrus sources)
    plugin/             NEW: main.cpp, CMakeLists.txt   (DLL lives in-place)
tools/
  skse/                 ← cross-env.sh, cmake/, setup-sdk-symlinks.sh (moved from plugins/)
  …                     compile-papyrus.sh, EspGen, env.sh, BsaExtract (unchanged)
```

`plugins/` is deleted. Each moved DLL mod's `build.sh` re-roots the toolchain from `$PLUGINS_DIR`
to `tools/skse/` (only the three DLL mods move; `RapidBowHold` is already an esp+pex resident of
`mods/` and is untouched). This is mechanical (3 `build.sh` path-fixes, `cmake/`, `.gitignore`) plus a
doc sweep — **16 `.md` files** reference `plugins/` (README + skse-toolchain/-tier-bringup + the
per-mod plan/design docs). Done **first**, as its own commit, so the new DLL lands in the clean home
rather than being moved later. It has independent value (removes the false mod/plugin
dichotomy) and no design content beyond this target tree — it goes straight to the plan.

## Approach — Design A: hook the playback, attenuate the resulting handle

Confirmed feasible by investigation (two live AE-1.6.x projects use this exact function). We let DBVO
call `SpeakSound` untouched — the engine does its full job, **including driving the LIP track**, so
**lip-sync is preserved** — and the plugin only scales the sound handle the engine produces.

### Hook target

`Actor::SpeakSoundFunction` — `RELOCATION_ID(36541, 37542)` (AE database id **37542**). The console
command `SpeakSound` (opcode 87) dispatches into this single engine function. Address resolves through
the installed **Address Library** (`REL::RelocationID`), so it is version-portable exactly like the
existing plugins' hooks — no hardcoded 1.6.1170 offset.

It is a **non-virtual** engine function, so the install is a **trampoline branch hook**
(`SKSE::GetTrampoline().write_branch<5>(...)`), not the vtable write AutoFireBow/GhostAllies use. The
trampoline API (`SKSE::GetTrampoline`, `SKSE::AllocTrampoline`, `write_branch<5>`) is present in the
pinned SKSE headers.

Signature (from TiltedEvolution `Code/client/Games/Skyrim/Actor.cpp`, which both calls and hooks it;
OStimNG hooks inside the same id for the same player-SpeakSound use):

```cpp
// thiscall on Actor*; arg2 is a caller-supplied BSSoundHandle the engine fills & starts playing.
bool Actor::SpeakSoundFunction(
    const char* apName,          // the "DBVO/<file>.fuz" path
    RE::BSSoundHandle* a_handle, // <-- out-param (uint32[3] == BSSoundHandle, 0xC bytes)
    std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
    std::uint64_t a7, std::uint64_t a8, std::uint64_t a9,
    bool a10, std::uint64_t a11, bool a12, bool a13, bool a14);
```

### The thunk

```cpp
static bool thunk(RE::Actor* a_this, const char* a_path, RE::BSSoundHandle* a_handle, /*…a4–a14…*/) {
    bool r = func(a_this, a_path, a_handle, /*…a4–a14 passed through untouched…*/);
    if (a_this && a_this->IsPlayerRef() && a_path && a_handle && a_handle->IsValid()
        && is_dbvo_path(a_path)) {
        a_handle->SetVolume(g_dbvoVolume);   // 0.0–2.0; default 1.0 (no-op)
    }
    return r;
}
```

- **`BSSoundHandle::SetVolume(float)`** is confirmed present in the pinned CommonLibSSE-NG
  (`RE/B/BSSoundHandle.h`, `sizeof == 0xC`). `FadeVolume(v,0,0,0)` is an equivalent instant set if
  needed.
- **Call the original first**: the engine fills and *starts* the handle synchronously inside
  `SpeakSoundFunction` (Tilted passes an uninitialised `handle[3]` in and it returns playing), so on
  return `a_handle` is valid and live, and `SetVolume` takes effect immediately. If a frame-1 race
  ever appears, defer via `SKSE::GetTaskInterface()->AddTask` with a by-value copy of the 12-byte POD
  handle (do **not** retain the pointer past the call — the engine owns it).

### Disambiguation — touch only the player's DBVO line

Two cheap conjunctive gates; everything failing either gate hits the untouched original:

1. **`a_this->IsPlayerRef()`** — NPC dialogue never routes through `SpeakSound`; it goes through the
   topic/response system (`ProcessResponse`). So this alone excludes all NPC voice.
2. **`is_dbvo_path(a_path)`** — case-insensitive prefix check for `"DBVO/"` (and `"DBVO\\"` for
   safety). Confirmed: DBVO's pex builds the path from string literals `DBVO/` + … + `.fuz`, and
   `SpeakSoundFunction` receives that exact arg1. Excludes any other `SpeakSound` caller (e.g. OStim).

## Components

| Unit | Where | Responsibility | Depends on |
| ---- | ----- | -------------- | ---------- |
| **SKSE plugin** `DBVODialogueTweaks.dll` | `mods/DBVODialogueTweaks/plugin/` | install the hook; hold `g_dbvoVolume`; register the Papyrus native | CommonLibSSE-NG, Address Library, SKSE |
| **Papyrus native bridge** `DBVOTweaks.psc` | `mods/DBVODialogueTweaks/src/papyrus/` | declare `SetPlayerVoiceVolume(Float) Global Native` for the MCM to call | the DLL (registers it) |
| **MCM** `DBVODialogueTweaksMCM.psc` | (existing) | new "Voice" page + slider; persist value; push to DLL on accept & on reload | SkyUI (`SKI_ConfigBase`), `DBVOTweaks` |
| **Build glue** | `mods/DBVODialogueTweaks/build.sh` | 4th step: build DLL (toolchain at `tools/skse/`), install to `SKSE/Plugins/` | `tools/skse/` |

### SKSE plugin

CommonLibSSE-NG entry (`SKSEPluginLoad` / `SKSE::Init`), mirror an existing plugin's scaffold.
Install the trampoline hook **in `SKSEPluginLoad`, right after `SKSE::Init`** (`AllocTrampoline(14)`
then `write_branch<5>`) — matching the existing plugins' idiom (AutoFireBow calls `InstallHooks()` at
load, not on a message). Register the Papyrus native via the `SKSEPapyrusInterface`. State is a single
`static float g_dbvoVolume = 1.0f`.
No SKSE co-save serialization — the MCM owns persistence (below), and re-pushes on every load.

### Papyrus native bridge

```papyrus
Scriptname DBVOTweaks Hidden
Function SetPlayerVoiceVolume(Float factor) Global Native   ; factor 0.0–2.0
```

A global-native holder script; ships as `DBVOTweaks.pex`. The plugin binds
`"SetPlayerVoiceVolume"` on class `"DBVOTweaks"` in its `RegisterFuncs` callback.

### MCM extension

Extend the existing `SKI_ConfigBase` quest script:

- `Float Property fPlayerVoiceVol = 100.0 Auto` — persists in the save like the v2 timing properties.
- `Pages` grows to `["Timing", "Voice"]`; the "Voice" page hosts one `AddSliderOption("Player voice
  volume", fPlayerVoiceVol, "{0}%")`.
- `OnOptionSliderOpen`: range `0–200`, default `100`, interval `5`, start `fPlayerVoiceVol`.
- `OnOptionSliderAccept`: store value, `SetSliderOptionValue(…, "{0}%")`, then
  `DBVOTweaks.SetPlayerVoiceVolume(fPlayerVoiceVol / 100.0)` — **live apply**.
- `OnGameReload` (already overridden for v2's menu registration): also call
  `DBVOTweaks.SetPlayerVoiceVolume(fPlayerVoiceVol / 100.0)` — **re-push the persisted value every
  load**, since the DLL keeps no saved state.

### Build integration

`mods/DBVODialogueTweaks/build.sh` is renumbered to **4 steps** (current is `[1/3]` swf → `[2/3]`
Papyrus(MCM) → `[3/3]` esp; the esp is last, not Papyrus). Final ordering:

- `[1/4]` swf (ffdec) — unchanged.
- `[2/4]` Papyrus — now compiles **both** `DBVODialogueTweaksMCM.psc` and the new `DBVOTweaks.psc`.
- `[3/4]` esp (EspGen) — unchanged.
- `[4/4]` **DLL** — configure+build `plugin/` with the `tools/skse/` toolchain (cmake +
  `clang-cl-msvc.cmake`); on `--install` copy `DBVODialogueTweaks.dll` → `<Data>/SKSE/Plugins/`.

One `build.sh [--install]` still builds and installs the whole mod (swf + 2×pex + esp + dll).

## Data flow

```
MCM slider (Papyrus property, persists in save)
  ├─ OnOptionSliderAccept ─┐
  └─ OnGameReload ─────────┴─→ DBVOTweaks.SetPlayerVoiceVolume(v/100)   [Papyrus native]
                                   └─→ plugin: g_dbvoVolume = v/100
                                          └─→ hook on Actor::SpeakSoundFunction:
                                                original runs (plays line + lip-sync),
                                                then if player && DBVO-path:
                                                  handle.SetVolume(g_dbvoVolume)
```

## Value mapping & the boost caveat

MCM `0–200` (%) → factor `0.0–2.0`. **Attenuation (0–100% → 0.0–1.0) is guaranteed.** **Boost
(100–200% → 1.0–2.0) may clamp at 1.0** — engine sound-handle volumes are normally 0.0–1.0
multipliers and `SetVolume(>1.0)` is not confirmed to amplify. We keep the 0–200 range (user choice),
verify boost in-game, and if it clamps: either cap the slider at 100 or revisit via a different gain
path. Not a blocker — the "tame it" goal lives entirely in 0–100%.

## Error handling / edge cases

- Guard `a_this`/`a_path`/`a_handle` null and `!IsValid()` at the hook; any failure → plain
  pass-through (original behavior).
- `g_dbvoVolume` defaults to `1.0` → before the MCM ever pushes, behavior is identical to today.
- Value applies to the **next** line, not the currently-playing one — acceptable (no live re-scale
  of an in-flight line needed).
- Single global player-voice volume (not per-voice-pack) — YAGNI; the user runs one pack.

## Testing / verification

In-game (full profile, real conversation — this only manifests on the live load order):

1. SKSE log shows the plugin loaded and the hook installed.
2. Slider 50% → player line audibly quieter; **NPC reply unchanged** (proves per-source isolation).
3. Slider 0% → player line silent; NPC reply normal.
4. Slider 100% → indistinguishable from stock DBVO.
5. Slider 150% → tests the boost-clamp question (record the result).
6. Player lip-sync path intact (mechanism unbroken even though the player face is off-screen in
   normal dialogue).
7. Value survives save/reload (persisted property re-pushed on `OnGameReload`).

## Why this shape

- **Hook-and-attenuate over replace-playback (Design B).** B has the plugin play the `.fuz` itself
  via `BSAudioManager::GetSoundHandleByFile` (`RELOCATION_ID(66402, 67664)`) +
  `BSResource::ID::GenerateFromPath` with an explicit volume. It works (full reference exists) and
  resolves BSA paths + demuxes `.fuz` through the engine's archive layer — but it must **suppress**
  the original call to avoid double audio and it **loses lip-sync** (the engine's `SpeakSoundFunction`
  is what drives the LIP track). A is less code and keeps lip-sync. B stays as the fallback if A's
  branch hook proves unreliable on this runtime.
- **Category re-route (rejected, failed empirically).** Appending an audio-category arg to DBVO's
  `SpeakSound` (`… Voice`) to make the player line obey the in-game Voice slider **broke the command
  → silence**. `SpeakSound` takes only a path. (A web claim that it accepts a category argument was
  wrong; the in-game test is authoritative.) Even had it worked, it couples player level to NPC level
  rather than giving an independent dial.
- **Gain-bake the voice pack (rejected for this goal).** Offline re-encoding the Karat `.fuz` clips
  to a lower level is independent of NPC volume and needs no SKSE, but it is a fixed, set-and-forget
  level, not an adjustable slider, and a chunky BSA/xWMA batch pipeline. Kept as a mention only.
- **Layout: in-place `plugin/` over alternatives.** Moving the whole mod *into* `plugins/` just
  inverts the category error (then `plugins/` holds an esp+swf mod). Co-locating the DLL under the
  mod while pointing CMake at `../../plugins/cmake` by relative path leaves the toolchain mis-homed in
  a mod-projects dir. Unifying under `mods/` + `tools/skse/` fixes the root cause once.

## Risks / unknowns (from investigation)

1. **Boost clamp** (above) — verify 150% in-game.
2. **Path form** — confirm in-game DBVO emits `DBVO/…` (forward slash) as arg1; match case-insensitive
   on both separators. (Pex evidence says forward slash.)
3. **`SpeakSoundFunction` later args (a4–a14)** — emotion/2D/lip/queue flags. Design A reads only arg2
   and the return and passes a4–a14 through untouched, so low risk; Tilted's `…,1,1` tail is
   informational only.
4. **NG API-name skew** — only affects Design B: the pinned header may expose `BuildSoundDataFromFile`
   where upstream renamed it `GetSoundHandleByFile` (same function). Irrelevant to A.

## References

- **TiltedEvolution** `Code/client/Games/Skyrim/Actor.cpp` — `SpeakSoundFunction` signature + id 37542
  (`POINTER_SKYRIMSE(TSpeakSoundFunction, …, 37542)`), calls and hooks it.
- **OStimNG** `skse/src/GameLogic/GameHooks.h`, `skse/src/GameAPI/GameDialogue.cpp` — identical
  `SpeakSound`-via-ConsoleUtil player-voice pattern; hooks inside `RELOCATION_ID(36541, 37542)`.
- **CommonLibSSE-NG** `RE/B/BSSoundHandle.h` (`SetVolume`, `FadeVolume`, `IsValid`/`IsPlaying`),
  `RE/B/BSAudioManager.h` (`GetSoundHandleByFile` / `BuildSoundDataFromFile`,
  `RELOCATION_ID(66402, 67664)`), `RE/A/Actor.h` (`IsPlayerRef`, `PlayASound`).
- **Heisenberg / Pipboy** `src/PipboyInteraction.cpp` — full Design-B play-from-path-with-volume
  reference.
- Repo hook idiom to mirror (switch vtable → trampoline): `mods/AutoFireBow/src/main.cpp`,
  `mods/GhostAllies/src/main.cpp` (post-refactor paths).
