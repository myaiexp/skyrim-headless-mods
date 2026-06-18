# SkytestProbe MCM-Reveal Implementation Plan

**Goal:** Add two read-only probe commands — `mcm-list` (enumerate registered SkyUI MCMs + pages)
and `mcm-get <ConfigScript> <prop…>` (a known mod's live property values) — so MCM verification is a
deterministic, headless probe command instead of pixel-driving the menu.

**Architecture:** New main-thread engine helpers reach SkyUI's MCM state through the Papyrus VM
(`VirtualMachine` → handle policy → `FindBoundObject` → read `Object` properties/variables), each
writing one `trace.jsonl` record (mirroring the existing `engine::DumpActor`). Two new `commands.cpp`
dispatch arms wire them in, following the exact `{id,cmd,…}` → `EnqueueMain` → `engine::` → `trace`
pattern. Pure read-only; no new threads, files, or deps.

**Tech Stack:** CommonLibSSE-NG (clang-cl + xwin cross-build), `RE::BSScript` VM introspection,
nlohmann/json (vendored), the probe's existing file protocol.

**Design:** `docs/plans/skytest-probe-mcm-reveal-design.md`

> **Verification note:** SKSE plugin — no unit-test framework. Tasks verify by (a) the DLL compiling
> and (b) in-game assertions against the full profile (SkyUI present) + the already-installed
> AutoFireBow MCM. "Test Cases" are build assertions and scripted probe commands + expected
> `trace.jsonl` records.

> **Concurrency:** another session may hold uncommitted `mods/DBVODialogueTweaks/` changes. Every task
> here touches only `mods/SkytestProbe/**`. **Stage by filename**; never `git add -A`/`.`.

---

## File Structure

| File | Change | Responsibility |
| --- | --- | --- |
| `mods/SkytestProbe/src/engine.h` | Modify | Declare `WriteMcmList()` + `WriteMcmGet(script, props)` (main-thread, null-safe, each writes its own trace record — the `DumpActor` contract). |
| `mods/SkytestProbe/src/engine.cpp` | Modify | Implement both + an internal `FindBoundScript(className)` VM locate helper. New includes: `RE/B/BSScript*`, `RE/V/VirtualMachine` etc. (via `RE/Skyrim.h`, already included). |
| `mods/SkytestProbe/src/commands.cpp` | Modify | Two dispatch arms (`mcm-list`, `mcm-get`) beside `dump`/`status`. |
| `mods/SkytestProbe/CMakeLists.txt` | — | No change — VM headers ship with the already-linked CommonLibSSE-NG. |

The probe DLL must be **installed into the full profile** (SkyUI present) to exercise these; that's a
manual `build.sh --install`-style copy for v1. No code change gates on profile — `WriteMcmList` just
returns 0 where SkyUI is absent.

---

### Task 1: VM foundation + `mcm-list` [Mode: Delegated]

**Files:**
- Modify: `mods/SkytestProbe/src/engine.h`, `mods/SkytestProbe/src/engine.cpp`
- Modify: `mods/SkytestProbe/src/commands.cpp`

**Contracts:**

`engine.h` (additions):
```cpp
// MCM reveal (read-only). Main-thread only, null-safe — degrade to an empty result +
// honest trace, never a crash. Each WRITES its own trace record (mirrors DumpActor).

// Enumerate registered SkyUI MCM configs -> writes one record:
//   {"src":"mcm-list","via":"manager"|"scan"|"none","count":N,
//    "mods":[{"name":<ModName>,"script":<class>,"pages":[...]}]}
// Returns the count (>=0); 0 with count:0 written when SkyUI is absent (a successful scan).
// Returns -1 ONLY when the Papyrus VM itself is unavailable (pre-load) — command acks false.
int WriteMcmList();
```

`commands.cpp` dispatch arm:
```cpp
if (c == "mcm-list") {
    EnqueueMain([id]() {
        const int n = engine::WriteMcmList();
        trace::Ack(id, n >= 0, n < 0 ? "mcm-list: Papyrus VM unavailable" : "");
    });
    return;
}
```

**Mechanism (engine.cpp):**

Internal locate helper, reused by Task 2:
```cpp
// Find the quest whose bound Papyrus script class == a_className; return its script object
// (nullptr if none / no VM / no data handler). Main thread only.
RE::BSTSmartPointer<RE::BSScript::Object> FindBoundScript(const char* a_className);
//   vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
//   policy = vm->GetObjectHandlePolicy();
//   for q in RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESQuest>():
//       handle = policy->GetHandleForObject(RE::FormType::Quest, q);   // FormType overload
//       if vm->FindBoundObject(handle, a_className, out): return out;
//   return nullptr;
```

`WriteMcmList` — enumerate registered configs by **two paths, primary then fallback**, emitting which
fired via the `"via"` field (the design's "validate at runtime" — one build handles both):

1. **`via:"manager"` (primary):** `FindBoundScript("SKI_ConfigManager")`; read its registry arrays
   `obj->GetVariable("_modConfigs")` (array of config `Object`s) and `obj->GetVariable("_modNames")`
   (array of strings), plus `_configCount`. Iterate by `arr->size()`. For each config `Object`, read
   `GetProperty("ModName")` and `GetProperty("Pages")` (array of strings).
2. **`via:"scan"` (fallback, when `_modConfigs`/`_modNames` don't resolve — `GetVariable` null or not
   an array):** an **inline** loop over `GetFormArray<RE::TESQuest>()`, calling `FindBoundObject(handle,
   "SKI_ConfigBase", out)` (base-class match) per quest. This is NOT a `FindBoundScript` call —
   that helper is exact single-class match (used by the `manager` primary path and by `mcm-get`);
   the scan needs the base-class `SKI_ConfigBase` match across all quests, so it iterates here
   directly. For every quest that binds a `SKI_ConfigBase`-derived script, read its `ModName`/`Pages`.

Both paths yield the same per-mod `{name, script, pages}` and write one combined record. **`script`
is load-bearing — always emit it** (the config's class name from `obj->GetTypeInfo()`): it is the key
a tester feeds to `mcm-get`, so it is not optional. Variable reads use the verified API:
`Variable::IsArray()`/`GetArray()` → `Array::size()`/`operator[]`; element `GetString()` for names,
`GetObject()` for configs; `GetProperty` returns `Variable*` (null when absent).

**Constraints:**
- Main-thread only (lives in `engine.cpp`, called solely via `EnqueueMain`). No locks.
- Every pointer null-checked: no VM, no data handler, no `SKI_ConfigManager`, a null/non-array
  variable, a config with no `ModName` → skip/empty, never deref-crash.
- SkyUI absent ⇒ `via:"none"`, `count:0`, `mods:[]` — a normal result, `ack ok:true`.
- No hardcoded FormIDs — locate by bound-script class only.

**Test Cases:**
- Build: `cd mods/SkytestProbe && ./build.sh` compiles to `build/SkytestProbe.dll` (PE32+).
- `grep -c "mcm-list" src/commands.cpp` ≥ 1; `grep -c "WriteMcmList" src/engine.cpp` ≥ 1.
- (in-game, Task 3) `{cmd:"mcm-list"}` → a `{"src":"mcm-list",…}` record with `count≥1` and an entry
  `{"name":"AutoFireBow","pages":["Settings"]}`.

**Verification:**
Run: `cd mods/SkytestProbe && ./build.sh && file build/SkytestProbe.dll`
Expected: builds; `PE32+ executable (DLL) … x86-64`.

**Commit after passing** (`git add mods/SkytestProbe/src/engine.h mods/SkytestProbe/src/engine.cpp
mods/SkytestProbe/src/commands.cpp`).

---

### Task 2: `mcm-get <ConfigScript> <prop…>` [Mode: Direct]

**Files:**
- Modify: `mods/SkytestProbe/src/engine.h`, `mods/SkytestProbe/src/engine.cpp`
- Modify: `mods/SkytestProbe/src/commands.cpp`

**Contracts:**

`engine.h`:
```cpp
// Read named properties off a config script class -> writes:
//   {"src":"mcm-get","script":<class>,"values":{<prop>:<bool|int|double|string>},"missing":[...]}
// Returns false (writes nothing) when no quest binds that script — the command acks the error.
bool WriteMcmGet(const std::string& a_script, const std::vector<std::string>& a_props);
```

`commands.cpp` dispatch arm:
```cpp
if (c == "mcm-get") {
    const std::string script = JStr(cmd, "script");
    auto props = JStrArr(cmd, "props");
    if (script.empty()) { trace::Ack(id, false, "mcm-get: missing script"); return; }
    if (props.empty())  { trace::Ack(id, false, "mcm-get: missing props"); return; }
    EnqueueMain([id, script, props]() {
        if (engine::WriteMcmGet(script, props)) trace::Ack(id, true);
        else trace::Ack(id, false, "mcm-get: no quest with script " + script);
    });
    return;
}
```

**Mechanism (engine.cpp):** `FindBoundScript(a_script.c_str())` (Task 1's helper). If null → return
false (no record). Else, for each requested prop: `obj->GetProperty(prop)`:
- null → append to `missing[]`.
- scalar → coerce by the `Variable`'s type and store into `values`: `GetBool()`→bool,
  `GetSInt()`→int, `GetFloat()`→double, `GetString()`→string. (Determine the type via the
  `Variable`'s type tag; `IsArray()` and object/array types are **not** scalar.)
- array/object/unsupported → append to `missing[]` (v1 reads scalars only).
Write the record, return true.

**Constraints:**
- Main-thread, null-safe (same rules as Task 1).
- Partial success is success: unknown/non-scalar props go in `missing[]`, the rest still report,
  `ack ok:true`. `ack ok:false` only when the *script* isn't found.
- Reuse `FindBoundScript` — do not duplicate the VM-locate logic.

**Test Cases:**
- Build compiles (as Task 1).
- (in-game, Task 3) `{cmd:"mcm-get",script:"AutoFireBowMCM",props:["bEnabled","fDamageBonus","fMinShotDelay","iToggleKey"]}`
  → `{"src":"mcm-get","script":"AutoFireBowMCM","values":{"bEnabled":true,"fDamageBonus":10.0,"fMinShotDelay":0.0,"iToggleKey":-1},"missing":[]}`.
- (in-game) `{cmd:"mcm-get",script:"NoSuchMCM",props:["x"]}` → `ack ok:false`, no record, no crash.
- (in-game) a bogus prop on a real script lands in `missing[]` with `ack ok:true`.

**Verification:**
Run: `cd mods/SkytestProbe && ./build.sh && file build/SkytestProbe.dll`
Expected: builds clean.

**Commit after passing** (same three files).

---

### Task 3: Full-profile verification [Mode: Direct]

**Files:** none (verification). Update the probe's command reference in
`docs/plans/skytest-probe-design.md` (add `mcm-list`/`mcm-get` to the verb list) if it enumerates
verbs.

**Steps (Opus drives — a subagent can't run the game):**
1. Build SkytestProbe; **install** `build/SkytestProbe.dll` + `SkytestProbe.ini` into the full
   profile's `…/Data/SKSE/Plugins/` (AutoFireBow already installed there).
2. `skytest play`; load the save; confirm in-world (probe `status`, or the AutoFireBow
   loop-registered log line).
3. Write `{cmd:"mcm-list"}` to `…/SKSE/skytest/commands.jsonl`; read the `mcm-list` record from
   `trace.jsonl` → assert it contains `{name:"AutoFireBow", pages:["Settings"]}` (and note `via`).
4. Write `mcm-get` for `AutoFireBowMCM` with the four props → assert the values match the MCM
   defaults (`bEnabled:true, fDamageBonus:10, fMinShotDelay:0, iToggleKey:-1`).
5. Negative: `mcm-get` bogus script → `ack ok:false`, no crash.
6. Quit the game. Confirm AutoFireBow.esp still active.

**Constraints:**
- The probe must be in the **full** profile for SkyUI to be present (step 1). Don't expect MCM data in
  the vanilla+1 `test` profile (there `mcm-list` correctly returns `count:0`).
- Acceptance gate for the feature — both `mcm-list` and `mcm-get` must return the expected records.

**Verification:** the scripted commands above, asserted against `trace.jsonl`.

**Commit** any doc touch-ups (`git add mods/SkytestProbe/... docs/...` by name).

---

## Execution
**Skill:** superpowers:subagent-driven-development
- **Task 1 — Delegated:** the VM-introspection foundation has genuine API-behavior uncertainty
  (which enumeration path resolves at runtime) and benefits from focused implementation against the
  verified CommonLibSSE-NG signatures; dual-path + null-safety is real logic.
- **Tasks 2, 3 — Direct:** `mcm-get` is mechanical once `FindBoundScript` exists; Task 3 is an
  Opus-driven in-game verification.
