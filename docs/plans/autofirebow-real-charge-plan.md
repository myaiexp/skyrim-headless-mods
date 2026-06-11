# AutoFireBow Real Charge — Implementation Plan

**Goal:** Remove the `power`/`weaponDamage` clamps and make auto-fired arrows genuinely
engine-charged by driving the real input-release path, so the mod is honest and public-worthy.

**Architecture:** Replace the loop's `NotifyAnimationGraph("attackRelease")` cosmetic loose (which
produces uncharged 0.350 arrows, rescued only by the clamp) with a **synthetic input-release**
fed through the engine's own attack pipeline. The physical button stays held, so the engine keeps
doing a real charged draw; we only fake the _release_ event. A probe build resolves the single
in-game unknown (does a held button auto-redraw after a real loose?) before committing the final
wiring. Spec: `docs/plans/autofirebow-real-charge-design.md`.

**Tech Stack:** SKSE C++ / CommonLibSSE-NG, cross-compiled Linux→Windows (clang-cl + lld-link +
xwin), CMake/Ninja. Single TU: `mods/AutoFireBow/src/main.cpp`.

**Iterate loop (SKSE DLL — no Papyrus bytecode caching):**

```
edit src/main.cpp
./mods/AutoFireBow/build.sh --install     # builds + copies DLL into Data/SKSE/Plugins
quit Skyrim to desktop  →  relaunch  →  load save
read <prefix>/.../My Games/Skyrim Special Edition/SKSE/AutoFireBow.log
```

The DLL reloads fresh every launch — no `stopquest`/`startquest` dance, just restart. Mase runs
the in-game steps and reports the logged values; Opus builds and edits between rounds.

---

### Task 0: Resolve the synthetic-`ButtonEvent` API (research, write into code)

**Files:**

- Reference (read): CommonLibSSE-NG headers under
  `mods/AutoFireBow/build/_deps/commonlibsse-src/include/RE/` — `ButtonEvent`, `InputEvent`,
  `BSInputDeviceManager`, `InputDevices`, and how the existing `AttackInputSink` reads
  `QUserEvent()` / `IsPressed()` (`src/main.cpp:120-152`).
- No code committed yet — output is a confirmed construction + injection recipe captured as a
  short comment block or scratch note used by Task 1.

**What to determine (the spike is gated on this):**

1. **Construct** a `ButtonEvent` that the engine maps to the `"Right Attack/Block"` user event:
   which device (`INPUT_DEVICE`), which id/scancode, and how `value`/`heldDownSecs` express a
   _release_ (value `0.0`) vs a _press_ (value `1.0`, `heldDownSecs ≈ 0`). CommonLibSSE-NG exposes
   a `ButtonEvent::Create(...)` factory — confirm its exact signature and that `QUserEvent()`
   resolves to the bound control (it reads the user-event mapping, so the raw key must map to the
   attack control, or the event must carry the user-event string directly).
2. **Inject** it so the engine's attack handler consumes it as real input. Candidate routes,
   confirm which the engine actually honors:
   - Build a linked `InputEvent*` list and dispatch through `BSInputDeviceManager`'s event sink
     chain, **or**
   - Hand it to the player controls / `PlayerControls` input handlers directly.
3. **Risk to confirm:** whether the attack handler reads **queued events** (injection works) or
   **live device state** (injection alone won't charge/loose — escalate to a fallback in the
   design doc).

**Contracts (shape the code will take):**

```cpp
// Send one synthetic attack button event through the engine input path.
// pressed=false => release (value 0); pressed=true => press (value 1, heldDownSecs 0).
void SendSyntheticAttack(bool pressed);
```

**Verification:**
This task is "confident we know how to build it." Done when the construction + injection calls
compile against CommonLibSSE-NG (`./mods/AutoFireBow/build.sh` succeeds) and the recipe is
written down. Behavioral proof is Task 1 (in-game).

**Commit** the `SendSyntheticAttack` helper (compiling, unused) once it builds.

[Mode: Direct]

---

### Task 1: Probe build — synthetic release, no re-nock, clamp removed

**Files:**

- Modify: `mods/AutoFireBow/src/main.cpp`

**Changes:**

- In `BowLoopSink::ProcessEvent`, on `BowDrawn` (when `AttackHeld() && !g_firedThisCycle`):
  replace `ScheduleRelease()` (the `NotifyAnimationGraph("attackRelease")` task) with a deferred
  `SendSyntheticAttack(false)` (real release).
- **Inject nothing for re-nock** — leave `arrowRelease` handling to _only_ log; do **not** call
  `ScheduleRedraw()`. We are measuring whether the held button redraws on its own.
- **Remove `PowerSpeedHook`** (the struct, its `InstallHooks` vtable write, and the call) so the
  logged power is the engine's honest value, not the clamped `1.0`.
- **Logging — lift wholesale from `src/main.cpp.probe-logging.bak`** (it already has the exact
  instrumentation; don't reconstruct). Bring across onto the _current_ `main.cpp` base:
  - `AnimProbeSink` — logs every bow/arrow/attack/draw/release graph tag with the live attack-state
    (`AttackStateName`), so a new draw cycle starting while held is directly visible.
  - `MsSinceDrawStart` + `g_drawStart` anchor (reset on `bowDraw`) — ms-since-nock timing, which is
    how we'll read whether/when a re-draw and the next `BowDrawn` occur.
  - The per-cycle `>>> RELEASE natural_power={:.3f} weaponDamage X->Y +Nms attackState=...` line —
    this is the honest-power read-out (logs the engine's `runtime.power` _before_ any rewrite).
  - `AttackStateName` helper + the `<cctype>/<chrono>/<string>` includes it needs.
  - **Do NOT carry over the `.bak`'s regressions:** keep the current base's runtime-aware vtable
    slot (`REL::Relocate<std::size_t>(0xAF, 0xB0)`, not the `.bak`'s hardcoded `0xB0`) and the
    `AutoFireBow` name/log path (the `.bak` still says `RapidBow` → `RapidBow.log`). We're lifting
    the _probe logging_, not reverting the file.

**Contracts:**

- The loop still arms on `bowDraw`, fires once per cycle on `BowDrawn`. Only the _loose mechanism_
  and _re-nock_ change. `AttackInputSink` / `g_attackHeld` unchanged.

**In-game verification (Mase runs; this IS the spec):**
Build+install, restart, equip a bow, **hold** attack. Read `AutoFireBow.log`. Record:

| Observation                                                                     | Meaning                                                                       |
| ------------------------------------------------------------------------------- | ----------------------------------------------------------------------------- |
| First auto arrow logs `natural_power ≈ 1.0`                                     | Synthetic release reaches the real charge path ✅                             |
| First auto arrow logs `natural_power ≈ 0.35`                                    | Injection does NOT charge → escalate to design-doc fallback                   |
| A new `bowDraw`/`BowDrawn` cycle appears while still held (no re-nock injected) | Engine auto-redraws → re-nock injection unneeded ✅✅                         |
| No further draw after the first loose while held                                | Need synthetic press for re-nock (Task 2)                                     |
| Black screen / no input / ambient audio only                                    | Main-thread hang — injection re-entered the pipeline; defer harder or rethink |

**Verification command:** `./mods/AutoFireBow/build.sh --install`; expected `file` output
`PE32+ executable (DLL)`. Then in-game log read above.

**Commit after the probe build is captured** (with a one-line note of the observed outcome in the
commit body once Mase reports).

[Mode: Direct]

---

### Task 2: Wire the resolved re-nock path; delete graph loose + clamp for good

**Depends on Task 1's in-game outcome — branch:**

- **Auto-redraws while held** → delete `ScheduleRedraw()` and all `NotifyAnimationGraph` re-nock
  calls entirely; the loop is just: hold → engine draws → `BowDrawn` → `SendSyntheticAttack(false)`
  → engine redraws → repeat.
- **Does not auto-redraw** → on `arrowRelease` (when `g_firedThisCycle && AttackHeld()`), inject a
  deferred `SendSyntheticAttack(true)` (real press) to start the next draw, replacing the old
  graph re-nock.

**Files:**

- Modify: `mods/AutoFireBow/src/main.cpp`

**Changes:**

- Implement the branch chosen above.
- Ensure `PowerSpeedHook` and **both** clamp writes (`runtime.power = 1.0`,
  `runtime.weaponDamage *= 1/natural`) are **gone** from the codebase — `grep` returns nothing.
- Remove the now-dead `ScheduleRelease`/`ScheduleRedraw` graph helpers and any
  `NotifyAnimationGraph` calls that the chosen path no longer uses.
- Keep one concise info log confirming honest charge; drop the verbose probe spam.
- Update `SKSEPluginInfo` version bump and the load-line message (no longer "full power+damage").

**Contracts:**

- Loose is driven only by synthetic input; no projectile field is rewritten anywhere.
- Manual single shots (button not held in a loop) are 100% vanilla — verify the input sink doesn't
  mis-fire on a normal tap.

**In-game verification (Mase runs):**

- Auto-fire looses repeatedly while held, stops cleanly on release.
- `AutoFireBow.log` shows auto arrows at `natural_power ≈ 1.0` with **no clamp in the binary**.
- Damage/speed match a real full-draw shot (compare against a manual full-draw arrow).
- No main-thread hang over sustained holding.
- Manual quick-tap = vanilla partial-draw arrow (no free full damage anymore).

**Verification command:** `./mods/AutoFireBow/build.sh --install` then the in-game checks above.

**Commit after in-game pass.**

[Mode: Direct]

---

### Task 3: Cleanup + honest Nexus messaging

**Files:**

- Modify: `mods/AutoFireBow/src/main.cpp` (final comment/header pass — the file header still
  describes "force full charge … clamp"; rewrite to describe the real-input-release design).
- Delete: `mods/AutoFireBow/src/main.cpp.probe-logging.bak` — it was the probe-logging source
  lifted in Task 1 and has served its purpose once the spike ships. Delete **only if Mase confirms**
  he doesn't want to keep it as a reusable instrumentation reference for future archery RE.
- Modify: `docs/autofirebow-nexus-page.md` — change the one-liner and "what's genuinely new" from
  "every tap forced to full power via an engine hook / clamp" to the honest "hold to
  auto-fire **genuinely-charged** shots, script-free, no animation framework." Drop the
  "full power forced via `GetPowerSpeedMult` clamp" mechanic bullet; replace with the
  synthetic-input-release description. Keep the honesty-over-hype framing.
- Modify: `docs/skse-tier-bringup.md` — mark option 1 (clamp) superseded by option 3 (synthetic
  input); note the real-charge result and link the design doc.

**Verification:**

- `grep -rn "power = 1.0\|weaponDamage \*=\|GetPowerSpeedMult" mods/AutoFireBow/src` → no clamp
  remnants (a vtable hook may remain only if Task 2's path still needs it; it should not).
- Docs read truthfully against the shipped behavior.

**Commit; then Mase pushes (`git push origin`).**

[Mode: Direct]

---

## Execution

**Skill:** superpowers:subagent-driven-development

- All tasks **Mode A (Direct)** — each round ends with an in-game test only Mase can run, so Opus
  must hold the conversation and react to logged results between tasks. Subagents can't test the
  live runtime, so delegation buys nothing here.
- Tasks are **strictly sequential and gated**: Task 2's shape is decided by Task 1's in-game
  outcome. Do not pre-build Task 2 branches.
