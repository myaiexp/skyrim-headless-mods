# DBVO Dialogue Tweaks v6 — player-voice boost (above 100%)

**Status: approved 2026-09-08 (range 0–300%), building.** Ships as mod version **1.1.0**.

## Problem

The v3 volume slider attenuates only. First Nexus feedback (2026-09-08): a vampire voice pack is
mastered far quieter than NPC VO and the user "hoped 50 = unchanged". v3 found in-game that
`BSSoundHandle::SetVolume(1.5)` sounds like 1.0 and capped the slider at 100. The user wants the
other direction.

## What the engine does (decompiled from the live 1.7.104 exe, `tools/ghidra`)

| Step | Where | What |
| --- | --- | --- |
| slider → handle | `BSSoundHandle::SetVolume` (AL 66365/67626) | `if (soundID != -1) ComposeMessage(mgr, 0xD, soundID, int(volume × K))` — a **message to the audio thread**, volume quantised to an int |
| audio thread | `BSGameSound::SetVolume` (CommonLib `src/RE/B/BSGameSound.cpp`) | `volume = clamp(v, 1e-5, 1.0)` — **the clamp** — then `SetVolumeImpl()` |
| the push | `BSXAudio2GameSound::SetVolumeImpl` (vtable slot 0x18, AL 68020) | `sourceVoice->SetVolume(EffectiveVolume(this), 0)` — 17 instructions, nothing else |
| the formula | `EffectiveVolume` (AL 67873) | `master × volume × category->GetVolume()` → clamped → to millibels, **capped at 0 dB**, minus static/system attenuations → back to linear. Cannot exceed 1.0 by construction |

`Update`, `UpdateEmitterPosition` (every 65 ms) and `OutputModelChangedImpl` call the voice's
`SetOutputMatrix` / filter slots, never `SetVolume`. So **`SetVolumeImpl` is the only place the
XAudio2 voice's gain is written**, and it runs on the audio thread. XAudio2 (`IXAudio2Voice::SetVolume`,
slot 0x0C) and FAudio under Proton accept gain above 1.0 (up to 2^24).

The June Ghidra image was 1.6.1170; the same functions decompile identically on 1.7.104
(vtable at the Address-Library `VTABLE_BSXAudio2GameSound` address, `addrlib.py id 236556`).

## Design — hook the push, multiply after the engine

```
speak-sound hook (main thread, existing v3)              audio thread
  factor = slider/100                                      message → BSGameSound::SetVolume(min(factor,1))
  a_handle->SetVolume(min(factor, 1.0))   ───────────►         clamp → SetVolumeImpl(this)
  g_playerLine = *a_handle (existing)                            │
  g_boost = max(factor, 1.0)                                     ▼ (our vfunc hook)
                                                           original(this)            // voice = effective (≤1)
                                                           if boost > 1 && IsPlayerLine(this):
                                                               v = voice->GetVolume()
                                                               voice->SetVolume(v × boost, 0)
                                                               log once per line: "voice v → v×boost"
```

- **Hook**: `REL::Relocation<uintptr_t>{ RE::VTABLE_BSXAudio2GameSound[0] }.write_vfunc(0x18, thunk)`,
  installed at load next to the MinHook detour (precedent: `AutoFireBow`, `SkytestProbe/facegen_ramp.cpp`).
  Address-Library-resolved for SE and AE, so the one-DLL story is unchanged.
- **IsPlayerLine(this)**: `mgr = BSAudioManager::GetSingleton()`; require
  `GetCurrentThreadId() == mgr->ownerThreadID` (field +0xF4), then
  `mgr->activeSounds.find(g_playerLine.soundID)` (id → `BSGameSound*`, CommonLib `BSTHashMap`) and
  compare the pointer to `this`. The map is owned by the audio thread; the thread check is what
  makes reading it from a hook safe, and any other thread is a pass-through. `g_playerLine` is the
  handle v4 already retains (mutex-guarded copy); its `soundID` is read under that mutex.
- **Readback, not formula**: `GetVolume` returns the value the engine just set with
  `XAUDIO2_COMMIT_NOW`, so the multiplier composes with whatever master/category/attenuation the
  engine computed, and every later engine re-apply (a fade, a category change) passes through the
  hook again and stays consistent. No dependence on the millibel formula.
- **Split at 100**: the slider maps to `factor = value/100`; the handle gets `min(factor, 1.0)`
  (exactly today's path — at ≤100 the hook's boost branch is never taken), the hook gets
  `max(factor, 1.0)`. 100 stays "unchanged"; a saved slider value from 1.0.x needs no migration.
- **MCM**: range 0–300, default 100, interval 5, label unchanged; the slider's info text says
  above 100 amplifies and can clip a loud pack.

## Error handling

- No `sourceVoice`, wrong thread, lookup miss, `g_playerLine` invalid, boost ≤ 1 → the hook is
  `original(this)` and nothing else. The failure direction is always "no boost", never "louder
  than asked" or a crash.
- Clipping: gain above unity on an already-hot pack distorts. Documented in the MCM info, README and
  Nexus page; not clamped by us (the user asked for it).
- v4's `FadeOutAndRelease` on skip still works: the fade's `SetVolume` steps run through the hook,
  each step re-read and re-multiplied, so the fade starts from the boosted level.
- SkytestProbe's `speak-watch` hooks the same speak-sound function (MinHook, one hook per target) —
  already documented; unchanged by this design.

## Testing

`mods/DBVODialogueTweaks/voiceboost.steps`, same boot + staging as `replyonlineend.steps`:

1. Set the factor through the console (no SkyUI in the test stage): `cgf "DBVOTweaks.SetPlayerVoiceVolume" 2.5`
   — `cgf` calls a Papyrus global; the native is registered by the DLL and `DBVOTweaks.pex` ships.
2. Speak the staged line: `player.speaksound "dbvo/t1.fuz"`; close the console.
3. **Assertion**: a new skytest gate `until:log:DBVODialogueTweaks|voice boost` polls
   `<My Games>/SKSE/DBVODialogueTweaks.log` for a line written after the gate started. The DLL logs
   the readback (`voice boost: 1.000 -> 2.500 (slider 250%)`), so the gate proves the vtable hook
   fired on the player's sound, the thread check passed, and XAudio2 accepted a gain above 1.0.
4. **Control**: the same script with `cgf … 1.0` must fail the gate (no boost line). Audible
   confirmation is Mase's in the real game — the only honest check for "louder".

The `until:log:` gate is a general addition to `skytest/lib/replay.sh` (any SKSE plugin's log
becomes assertable); `until:!log:` negates like every other gate.

## Why this shape

- **Hook `SetVolumeImpl`, not `IXAudio2SourceVoice::SetVolume`.** The voice vtable lives in
  XAudio2/FAudio, shared by every voice in the process, and patching it means filtering every
  call by `this`; the engine's virtual is one object type, Address-Library-addressed, and is the
  only writer anyway.
- **Not a sentinel volume.** Sending a magic value through the handle and matching
  `this->volume` in the hook would avoid the map, but the volume is quantised to an int through
  the message, sound objects are pooled (`audioCache`) so a stale member could mark the wrong
  sound, and the thread-checked map lookup is exact.
- **Not `this->volume = boost` before `original`.** `EffectiveVolume` caps at 0 dB in millibels;
  a member above 1.0 is flattened. Only a write after the engine's own push can exceed unity.
- **Not re-encoding packs / a custom output model** (v3's deferred options): a slider that
  boosts at runtime is what the user asked for and what DBVO 2 / DBReV advertise.

## References

- `tools/ghidra/out/BSXAudio2GameSound_slot0x18.c`, `fn_140cd5410.c`, `fn_140cc9850.c`,
  `fn_140ccbe00.c` (regenerable: `ghidra.sh query find_via_rtti.py 'BSXAudio2GameSound:0x18'`,
  `GHIDRA_PROJECT=scratch ghidra.sh query decompile_at.py 0x140cd5410 0x140cc9850 0x140ccbe00`).
- CommonLibSSE-NG v7.1.0: `RE/B/BSXAudio2GameSound.h` (`sourceVoice` +0x128),
  `RE/B/BSAudioManager.h` (`activeSounds` +0x28, `ownerThreadID` +0xF4), `RE/I/IXAudio2Voice.h`
  (`SetVolume` 0x0C, `GetVolume` 0x0D), `src/RE/B/BSGameSound.cpp` (the clamp).
- `docs/plans/dbvo-v3-player-voice-volume-design.md` — the slider this extends.
