# SkytestProbe — reveal MCM (design, 2026-06-14)

Extend **SkytestProbe** (the runtime debug probe) so it can **read SkyUI MCM state** from the C++
side and emit it to the trace — turning "verify a mod's MCM" into a deterministic, headless probe
command instead of pixel-driving the menu (which the AutoFireBow MCM test proved unreliable — the
#9b cursor desync). This is the "SKSE ground-truth tie-in" from `docs/ideas.md`, aimed at MCMs.

**Scope: reveal-first.** v1 is **read-only**. *Driving* MCMs (open a page, set an option) is a
deferred follow-up (see "Deferred"). Target environment: the **full modded profile** (SkyUI present);
the probe no-ops cleanly where SkyUI is absent.

## The load-bearing constraint (why the shape is what it is)

SkyUI stores the MCM **registry** centrally but **not** the option **values**:

- `SKI_ConfigManager` (a quest script) holds the registered configs + mod names; each
  `SKI_ConfigBase` subclass exposes `ModName` (string) and `Pages` (string[]) as **persistent Auto
  properties**.
- Option labels/values are **built transiently in `OnPageReset`** and only exist while that mod's
  page is **open** — there is no generic "option → value" table anywhere.

Therefore, **headless** reading can yield *which MCMs exist + their pages* (registry) and *a known
mod's values* (by reading that config script's named properties) — but **generic on-screen option
values for an arbitrary mod require the menu open** (a Scaleform/GFx scrape), which is deferred to
the drive phase. (Full mechanism research: the C++↔SkyUI investigation summarized below.)

## Architecture — two commands + two engine helpers

Mirrors the probe's existing command shape exactly (`{id, cmd, …}` → `Dispatch` in `commands.cpp` →
`EnqueueMain` onto the main thread → an `engine::` helper → `trace::Write` record + `trace::Ack`).

### Command 1 — `mcm-list` (enumerate registered MCMs)

- **Input:** `{ "id": "...", "cmd": "mcm-list" }` (no args).
- **Engine helper `engine::ListMcm()`** (new, `engine.{h,cpp}`, main-thread, null-safe): enumerate
  the registered MCM config scripts and return, per config, `{ name (ModName), script (class name),
  pages (Pages[]) }`.
- **Mechanism:** iterate `TESDataHandler::GetFormArray<RE::TESQuest>()`; for each quest, obtain its
  bound script object via the VM and test whether its **type hierarchy includes `SKI_ConfigBase`**
  (walk `BSScript::Object::GetTypeInfo()` → parent chain). For matches, read the **`ModName`/`Pages`
  properties** (`Object::GetProperty`). This avoids depending on SkyUI's private registry-array
  *variable* names (`_modNames`/`_modConfigs`), which a stripped `.pex` could rename — properties are
  stable. (If the type-hierarchy scan proves unworkable, fall back to reading `SKI_ConfigManager`'s
  arrays; the implementation validates which is reliable at runtime — see "Risks".)
- **Output (one trace record):**
  `{ "src":"mcm-list", "count":N, "mods":[ {"name":"AutoFireBow","script":"AutoFireBowMCM","pages":["Settings"]}, … ] }`
- **Ack:** `ok:true` always when the scan completes (even `count:0`); `ok:false` only on a genuine
  fault (no VM / no data handler).

### Command 2 — `mcm-get <ConfigScript> <prop…>` (read a known mod's values)

- **Input:** `{ "id":"...", "cmd":"mcm-get", "script":"AutoFireBowMCM", "props":["bEnabled","fDamageBonus","fMinShotDelay","iToggleKey"] }`.
- **Engine helper `engine::GetMcmProps(script, props)`** (new, main-thread, null-safe): locate the
  quest whose bound script class == `script` (same VM locate as above, exact class match via
  `FindBoundObject(handle, script, …)`), read each requested property, coercing by the
  `BSScript::Variable` type (Bool/Int/Float/String). Returns `{ values: {prop:val,…}, missing:[…] }`.
- **Output (one trace record):**
  `{ "src":"mcm-get", "script":"AutoFireBowMCM", "values":{"bEnabled":true,"fDamageBonus":10.0,"fMinShotDelay":0.0,"iToggleKey":-1}, "missing":[] }`
- **Validation/Ack:** missing `script` → `ok:false "mcm-get: missing script"`; empty `props` →
  `ok:false "mcm-get: missing props"`; script not found → `ok:false "mcm-get: no quest with script
  <name>"`; unknown property names go in `missing[]` with `ok:true` (partial reads succeed).

### Shared VM-introspection helper

Both commands need "find the quest bound to script class X and get its `BSScript::Object`." Factor
this into one internal helper in `engine.cpp`:
`FindBoundScript(className) -> BSScript::ObjectPtr` using
`VirtualMachine::GetSingleton()` → `GetObjectHandlePolicy()->GetHandleForObject(Quest, quest)` →
`FindBoundObject(handle, className, out)`. Main-thread only (asserted by living in `engine.cpp`,
which the command path only ever calls via `EnqueueMain`).

## Data flow

```
commands.jsonl line  →  poll thread (ExecuteLine)  →  Dispatch(cmd)
  →  EnqueueMain([...]{ engine::ListMcm() / GetMcmProps(...) })   # main thread, next frame
      →  trace::Write({src:"mcm-list"|"mcm-get", …})
      →  trace::Ack(id, ok, err)
```
Identical to `dump`/`status`. No new threading, no new files — reuses the existing channel
(`…/SKSE/skytest/{commands,trace}.jsonl`), which is shared across the prefix, so it works regardless
of which profile loaded the probe.

## Placement (where the probe runs)

The probe must be loaded where SkyUI is — the **full profile**. The probe DLL is profile-agnostic
and passive until armed, so for v1 it's a **manual install** into the full profile's
`SKSE/Plugins/` (alongside how AutoFireBow was installed). When SkyUI is absent (vanilla+1 `test` profile), `mcm-list` returns
`count:0` and `mcm-get` acks `ok:false "mcm-get: no quest with script <name>"` (the same as any
unresolvable script) — both clean, never a crash.

## Error handling / no-op

Every path is null-safe (the probe's existing rule): no VM, no data handler, no SkyUI, an
unresolvable script, a stripped property — each degrades to an empty result + an honest `ack`/trace
field, never a crash. SkyUI absent is a normal, expected `count:0`, not an error.

## Testing

Full profile + SkyUI:

1. Build + install SkytestProbe into the full profile; AutoFireBow is already installed.
2. `skytest play`; load a save.
3. Write `{cmd:"mcm-list"}` to `commands.jsonl`; assert the `mcm-list` trace record contains
   `{name:"AutoFireBow", script:"AutoFireBowMCM", pages:["Settings"]}` (and the rest of the load
   order's MCMs).
4. Write `{cmd:"mcm-get", script:"AutoFireBowMCM", props:[…]}`; assert the values match what the MCM
   shows (Enabled=true, damage=10, …).
5. `mcm-get` against a **bogus** script → `ok:false`, no crash; run `mcm-list` in the **vanilla+1**
   `test` profile (no SkyUI) → `count:0`, no crash.

This finally gives the AutoFireBow MCM a *value-level* verification we couldn't get by driving the UI.

## Alternatives considered

- **Calling a Papyrus function on `SKI_ConfigManager` from C++** (DispatchMethodCall) — SkyUI exposes
  no getter returning the registry, and any data-returning Papyrus call is latent/async (result via
  callback). Buys nothing over reading the bound object's properties synchronously. Rejected.
- **GFx (Scaleform) scrape of the open MCM** — reads generic on-screen labels+values, but only for
  the **currently-open page**, and needs a one-time runtime `VisitMembers` dump to discover SkyUI's
  flash array path (the `.as` source isn't vendored). It's the natural foundation for **drive** (same
  `GFxMovieView` / `SKICP_*` mod events), so it's deferred there rather than split across phases.
- **Reading `SKI_ConfigManager`'s private `_modNames`/`_modConfigs` variables** — works, but depends
  on the `.pex` retaining private *variable* names; the property-based type-hierarchy scan is more
  robust. Kept as a documented fallback, not the primary path.

## Risks

- **Type-hierarchy scan vs. private-var read.** Primary plan (scan quests for `SKI_ConfigBase`-derived
  scripts, read `ModName`/`Pages` properties) avoids private-var fragility, but assumes
  `Object::GetTypeInfo()` parent-walk reliably identifies the base class and that iterating all
  quests is acceptably cheap (it's a one-shot command, not per-frame). Validate both at runtime
  during implementation; fall back to the `SKI_ConfigManager` array read if needed.
- **Property name/type coupling in `mcm-get`.** `mcm-get` is intentionally mod-specific (caller names
  the script + properties). That's correct for testing a *known* mod; it is **not** a generic
  value-reader (that's the deferred GFx scrape).

## Deferred (the drive phase + generic values)

- **`mcm-scrape`** — generic on-screen labels+values for the currently-open page (GFx). Menu-open-only;
  needs the flash-path discovery spike.
- **Drive** — open a config / select a page / set an option from C++, via `movie->Invoke` on SkyUI's
  flash methods or `SendModEvent` of the `SKICP_*` events (`SKICP_optionSelected`,
  `SKICP_pageSelected`, …). Built on the same open-menu GFx plumbing as `mcm-scrape`.
