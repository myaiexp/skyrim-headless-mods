# DBVO Dialogue Tweaks v6 — player-voice boost (above 100%)

**Status: approved 2026-09-08 (range 0–300%), building.** Ships as mod version **1.2.0** — not
1.1.0: the Nexus page's main version was already bumped to 1.1.1 while the uploaded file stayed
1.0.1, so 1.2.0 is the first number that is unambiguous on both. The release also adds, on the
Nexus page and each file's description, **which Skyrim builds each uploaded file runs on**
(1.0.0: SE 1.5.97 through AE 1.6.1170; 1.0.1 and later: those plus 1.7.99 / 1.7.104, i.e.
Address Library formats 1, 2 and 5 — one DLL).

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
  1. g_playerLine = *a_handle (existing, mutex)                  clamp → SetVolumeImpl(this)
  2. g_boostSoundID = a_handle->soundID  (atomic)   PUBLISH        │
  3. g_boost = max(factor, 1.0)          (atomic)   FIRST          ▼ (our vfunc hook)
  4. a_handle->SetVolume(min(factor, 1.0))  ──────────►  original(this)            // voice = effective (≤1)
                                                           if boost > 1 && IsPlayerLine(this):
                                                               v = voice->GetVolume()
                                                               voice->SetVolume(v × boost, 0)
                                                               log once per soundID: "voice v → v×boost"
```

- **Hook**: `REL::Relocation<uintptr_t>{ RE::VTABLE_BSXAudio2GameSound[0] }.write_vfunc(0x18, thunk)`,
  installed at load next to the MinHook detour (precedent: `AutoFireBow`, `SkytestProbe/facegen_ramp.cpp`).
  Address-Library-resolved for SE and AE, so the one-DLL story is unchanged.
- **Publish before send.** `SetVolume` is a *queued* message. The speak hook stores the new
  line's `soundID` and the boost (both atomics) **before** it calls `a_handle->SetVolume`;
  otherwise the audio thread can service the message against the previous line's id, pass
  through, and nothing re-pushes that sound's volume — an intermittent silent no-boost. (v3's
  code sends first and retains after; the order is reversed here on purpose.)
- **IsPlayerLine(this) — atomics only, no mutex on the audio thread.** `mgr =
  BSAudioManager::GetSingleton()`; require `GetCurrentThreadId() == mgr->ownerThreadID` (+0xF4),
  then `mgr->activeSounds.find(g_boostSoundID.load())` (id → `BSGameSound*`, CommonLib
  `BSTHashMap`) and compare the pointer to `this`. The map is owned by the audio thread; the
  thread check is what makes reading it from a hook safe, and any other thread is a pass-through.
  The hook must **never take `g_playerLineMtx`**: the poll thread holds that mutex while calling
  into the audio manager (`IsPlaying` every 30 ms) and so does `CutPlayerLine`
  (`FadeOutAndRelease`); if the audio thread held a manager-internal lock while servicing the
  volume message and then blocked on our mutex, that is a lock-order inversion. Hence the
  separate `g_boostSoundID` atomic — the hook reads nothing else of ours.
- **Thread check is logged, not assumed.** That `ownerThreadID` is the thread servicing message
  0xD is inferred from CommonLib's field name, not from a decompile. On the first entry for a
  boosted line the hook logs `GetCurrentThreadId()` against `mgr->ownerThreadID`, so a mismatch
  reads as a named line in the log ("boost skipped: hook on thread X, audio owner Y") instead of a
  silent no-boost.
- **Log latch**: one "voice boost" line per `soundID` (a fade re-applies the volume several times;
  those re-applies are boosted but not logged).
- **Readback, not formula**: `GetVolume` returns the value the engine just set with
  `XAUDIO2_COMMIT_NOW`, so the multiplier composes with whatever master/category/attenuation the
  engine computed, and every later engine re-apply (a fade, a category change) passes through the
  hook again and stays consistent. No dependence on the millibel formula.
- **Split at 100**: the slider maps to `factor = value/100`; the handle gets `min(factor, 1.0)`
  (exactly today's path — at ≤100 the hook's boost branch is never taken), the hook gets
  `max(factor, 1.0)`. 100 stays "unchanged"; a saved slider value from 1.0.x needs no migration.
- **MCM**: range 0–300, default 100, interval 5, label unchanged; a new `OnOptionHighlight` →
  `SetInfoText` handler (the MCM has none today) says above 100 amplifies and can clip a loud pack.

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

`mods/DBVODialogueTweaks/voiceboost.steps`, the boot of `replyonlineend.steps` and then **only the
console** — no NPC, no topic click, no `ui-set`: the boost needs a player line, not a
conversation, and every extra step is flake surface. `--no-shots` is not load-bearing here.

1. Set the factor through the console (no SkyUI in the test stage): `cgf "DBVOTweaks.SetPlayerVoiceVolume" 2.5`
   — `cgf` calls a Papyrus global; the native is registered by the DLL and `DBVOTweaks.pex` ships.
2. Speak the staged line: `player.speaksound "dbvo/t1.fuz"`; close the console.
3. **Assertion**: a new skytest gate `until:log:DBVODialogueTweaks|voice boost: 1.000 -> 2.500`
   — the DLL logs the readback with the slider value, so the substring pins *this* stimulus, and
   the gate proves the vtable hook fired on the player's sound, the thread check passed, and
   XAudio2 accepted a gain above 1.0.
4. **Control**: the same script with `cgf … 1.0`, ending on `wait until:!log:DBVODialogueTweaks|voice boost`
   — no boost line may exist. Audible confirmation is Mase's in the real game — the only honest
   check for "louder".

**The `until:log:<plugin>|<substring>` gate** is a *host-side* addition to `skytest/lib/replay.sh`:
it reads `$MYGAMES/SKSE/<plugin>.log`, not the probe, so it is a new branch in `replay_wait_gate`
(and `_lint_gate`) beside the probe-query path, not a `resolve_gate` row. Its **window** is "bytes
written since the session became ready": the launch path records each `SKSE/*.log`'s size once
the probe answers (the plugin has truncated and written its load lines by then) into the probe IO
dir, which `gs_reset_io` already clears per launch; the gate scans only past that mark (a file
smaller than its mark was re-truncated → scan from 0). This is why the gate can be evaluated
*after* the stimulus: a log line is written once and never re-emitted, so a gate-start window
(what the probe gates use, because each poll re-asks the probe) would never match. Within one
session the window covers every step, so a script that stimulates twice must pin the substring
(the slider value, above). `until:!log:` = no matching line in the window, evaluated per poll —
an assertion that resolves immediately when the line is absent.

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

## Release sweep (1.2.0)

Everything that says "attenuation only" or carries a version: `README.md` (feature bullet,
Configuration table row, Requirements), `src/papyrus/DBVOTweaks.psc` comment ("factor 0.0–2.0"),
`package.sh` `VERSION`, `plugin/CMakeLists.txt` `project(… VERSION)` (stale at 1.0.0), `kVersion`,
`stage-test-profile.sh` (hardcodes `dist/… 1.0.1.zip` — make it pick the newest zip), the two
boost entries in `docs/ideas.md` (ship → ruling moves to the README, entries deleted),
`docs/dbvo-landscape.md` if it calls the slider attenuate-only, and `docs/dbvo-page.bbcode`
(feature, configuration, changelog **and the per-file Skyrim-build text**). Pasting the page and
the per-file descriptions onto Nexus is **Mase's manual step** (`tools/nexus` is read-only).

## References

- `tools/ghidra/out/BSXAudio2GameSound_slot0x18.c`, `fn_140cd5410.c`, `fn_140cc9850.c`,
  `fn_140ccbe00.c` (regenerable: `ghidra.sh query find_via_rtti.py 'BSXAudio2GameSound:0x18'`,
  `GHIDRA_PROJECT=scratch ghidra.sh query decompile_at.py 0x140cd5410 0x140cc9850 0x140ccbe00`).
- CommonLibSSE-NG v7.1.0: `RE/B/BSXAudio2GameSound.h` (`sourceVoice` +0x128),
  `RE/B/BSAudioManager.h` (`activeSounds` +0x28, `ownerThreadID` +0xF4), `RE/I/IXAudio2Voice.h`
  (`SetVolume` 0x0C, `GetVolume` 0x0D), `src/RE/B/BSGameSound.cpp` (the clamp).
- `docs/plans/dbvo-v3-player-voice-volume-design.md` — the slider this extends.
