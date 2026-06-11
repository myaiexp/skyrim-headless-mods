# OneClickMap — handoff

**Status (2026-06-09):** Instant fast-travel to discovered locations **works** in-game. The
plugin is a **stopgap that crashes** on the first non-fast-travel message box, so it is NOT
Nexus-ready. This doc hands a fresh session everything needed to finish it. Read the design
(`oneclick-map-design.md`) and plan (`oneclick-map-plan.md`) first; this is the delta on top.

## TL;DR of where we are

- The feature is **proven achievable**: hooking `MessageBoxData::QueueMessage` intercepts the
  "Fast travel to X?" confirm box, and driving `FastTravelConfirmCallback::Run(kUnk1)` performs
  the trip with no box. Mase confirmed "works almost perfect" in-game.
- The committed/installed build (`mods/OneClickMap/src/main.cpp`) hooks `QueueMessage` at its
  **function entry** with `SKSE::GetTrampoline().write_branch<5>`. The travel branch works because
  it returns without using the trampoline. **But the pass-through path (`func(a_this)`) is a wild
  pointer and crashes on the first box that isn't a travelable fast-travel confirm** — marker
  management, "can't fast travel, enemies near", exit confirms, etc. Not just custom markers.
- Mase accepted the stopgap for personal fast-travel use (he doesn't use custom markers) and asked
  to preserve the working-travel build + this handoff for a fresh session.

## The root cause (this is the crux — don't re-derive it)

CommonLibSSE-NG's `Trampoline::write_branch<N>` / `write_call<N>` **do not relocate a function
prologue.** Confirmed by reading `SKSE/Trampoline.h` (the private overload):

```cpp
const auto disp   = reinterpret_cast<std::int32_t*>(a_src + N - 4);
const auto nextOp = a_src + N;
const auto func   = nextOp + *disp;   // assumes a_src is an existing `call/jmp rel32`
// ...writes the new branch...
return func;
```

So these primitives are **call-site / branch-site rewriters**: they only work when `a_src` already
points at a `call rel32` (`E8`) or `jmp rel32` (`E9`) instruction, and they return that instruction's
*existing* target. At a function **entry** (a normal prologue) the returned `func` is computed from
garbage prologue bytes → a wild pointer → EXECUTE access violation when called.

**Implication:** NG provides no prologue-relocating entry-detour. You hook either (a) a real
`call`/`jmp` instruction (`write_call`/`write_branch` at that site), or (b) a vtable slot
(`write_vfunc`). An arbitrary function entry is not safely hookable with the stock toolkit.

## Why the "clean" call-site fix stalled

The intended NG-safe fix is to `write_call` the specific `call QueueMessage` instruction that
queues the fast-travel confirm box (a real call site → valid `func` → safe pass-through → zero
blast radius). We could not locate that instruction:

- The MapMenu marker-click handler `RELOCATION_ID(52208, 53095)` contains exactly **one** direct
  `E8 call QueueMessage`, at **+0x2BD**. In-game it does **not** fire on a discovered-location click
  (no log line) — so it queues some *other* box, not the confirm. A runtime byte-scan of the whole
  function (stopping at ≥4×`CC` padding) finds no other `E8→QueueMessage`.
- The `.text` is **Steam-DRM-encrypted at rest** (only decrypted in-process at runtime), so it
  cannot be statically disassembled to find the call site.
- A crash backtrace frame suggested helper `52292` (`PlaceMarker(53113) → 442726 → 52292 →
  QueueMessage`), but that was a `[S]` stack-**scan** frame (unreliable); a runtime scan of `52292`
  (AE id) found no `E8→QueueMessage`. Dead lead.

**Conclusion:** the confirm box's `QueueMessage` call is either an **indirect call** (`FF /2`
through a function pointer/vtable — invisible to an `E8`-only scan) or lives in a helper we have
not identified. This is the open question.

## Three paths forward (pick one)

**Framing:** every crash came from suppressing the box *pre-render* (the global `QueueMessage`
entry-detour). Paths A/B keep pre-render suppression (no flash, harder/riskier). Path C abandons
suppression — let the box render and auto-confirm it — which uses only crash-proof primitives at
the cost of a likely ~1-frame flash. **Path C is the recommended starting point for a Nexus
release** (cannot crash, no DRM-hidden address hunt); Paths A/B are the no-flash purist options.

### Path C — auto-confirm the rendered box (Mase's idea — simplest, cannot crash)
Stop trying to suppress the box; let it appear and programmatically click "Yes." This never touches
`QueueMessage`, so the entry-detour crash class is gone entirely. Building blocks (all verified to
exist, all crash-proof — event sinks and vtable hooks, no prologue relocation):
- `RE::MenuOpenCloseEvent` sink (pure `BSTEventSink` registration, zero code hooking) → fires when
  `RE::MessageBoxMenu` (`MENU_NAME = "MessageBoxMenu"`) opens. Gate to fast-travel: correlate with
  a just-happened travelable map click — e.g. set a flag from the proven-safe format-call
  `write_call<5>` at `52208/53095 + OFFSET_3(0x342,0x3A6,0x3D9)` (fires only on a fast-travel prompt
  build), or check `MapMenu` state.
- Drive the trip with the already-proven primitive: `FastTravelConfirmCallback::Run(kUnk1)`.
- **Open piece to RE (the only real work):** how to press "Yes" — `MessageBoxMenu`'s header does NOT
  expose its callback (`RE/M/MessageBoxMenu.h`: only `Accept`@vtbl1, `ProcessMessage`@vtbl4, and an
  8-byte runtime blob). So either (a) find how to reach the live box's `FastTravelConfirmCallback`
  to call `Run(kUnk1)` directly, or (b) inject the "Yes" button selection into
  `MessageBoxMenu::ProcessMessage` (safe vtable hook on `MessageBoxMenu::VTABLE`, gated). Look at how
  other mods auto-answer message boxes; check `RE::IMessageBoxCallback`, the message-box-data path,
  and whether the active `MessageBoxData` is reachable from `RE::UI`/the menu.
- **Tradeoff:** the open-event fires at render time, so expect a ~1-frame box flicker before
  auto-travel (the "janky-but-harmless" look many Nexus mods have). Acceptable for release; if the
  flash is objectionable, fall back to Path A for true pre-render suppression.

### Path A — return-address probe → `write_call` (no flash, deterministic)
A one-off diagnostic build that hooks `QueueMessage`'s entry (`write_branch<5>`) and, in the thunk,
when `a_this->callback`'s vtable is `FastTravelConfirmCallback`, logs the **caller's return
address** (`_ReturnAddress()` / `__builtin_return_address(0)`) minus `REL::Module::get().base()` =
the exact call-site RVA — regardless of how deep or indirect the call is. During that run the thunk
must **suppress** every box (return without calling the wild `func`) to avoid the known crash, so
it is a controlled ~20-second run (open map, click a discovered location, read log, quit), not
normal play. Then ship a final build that hooks that exact site: if it's a direct `E8` call,
`write_call<5>`; if the RVA's bytes are `FF /2` (indirect), that needs `write_call<6>` or hooking
the pointer/vtable it calls through. Guard with a byte-pattern check so a wrong address aborts
instead of crashing. This is guaranteed to find the site.

### Path B — vendor a real entry-detour (robust fallback)
Add a MinHook-style trampoline (prologue relocation with a length-disassembler) so the **global
`QueueMessage` entry hook** — already proven to intercept the confirm box — gets a *valid*
pass-through. Heavier (new dependency / hand-rolled relocation) and keeps the full blast radius
(every box runs through our cheap vtable gate), but it directly fixes the one build we know works.
Use this if Path A reveals the call is indirect and awkward to `write_call`.

A vtable hook on `FastTravelConfirmCallback::Run` (slot 0x1) is **not** a solution: `Run` fires
*after* the box is answered, so it can't suppress the box or make travel one-click. It's only the
travel-drive primitive.

## Verified facts / address book (all confirmed in-game or against NG headers)

| Thing | Value / locator | Notes |
|-------|-----------------|-------|
| `MessageBoxData::QueueMessage` | `RELOCATION_ID(51422, 52271)` | confirmed: the header's own `QueueMessage()` uses this id. Non-virtual member; `this`=`MessageBoxData*` in RCX. |
| `MessageBoxData::callback` | `BSTSmartPointer<IMessageBoxCallback>` @ offset `0x40` | `.get()` → raw callback ptr; compare `*(uintptr_t*)cb` to the class vtable. |
| `FastTravelConfirmCallback` | members: `MapMenu* mapMenu`@0x10, `cursorPosX`@0x18, `cursorPosY`@0x1C; `Run` is vtable slot **0x1** | the confirm box's callback. |
| Travel-drive primitive | `callback->Run(RE::IMessageBoxCallback::Message::kUnk1)` | kUnk1 == "Yes/travel"; **drives the trip AND closes the map.** Do NOT also send `kHide` (it cancels the trip → "map closes, no travel"). |
| Cursor marker (travelable?) | `cb->mapMenu->GetRuntimeData().mapMarker.get().get()` → `extraList.GetByType<ExtraMapMarker>()->mapData->flags.any(MapMarkerData::Flag::kCanTravelTo)` | the discovered-vs-not test. Verified true on discovered, customMarker flips correctly. |
| `playerMapMarker` (has custom marker?) | `PlayerCharacter::GetSingleton()->GetInfoRuntimeData().playerMapMarker` | NB: `GetInfoRuntimeData()`, not `GetPlayerRuntimeData()`. |
| MapMenu click handler | `RELOCATION_ID(52208, 53095)` | only direct `E8→QueueMessage` is at **+0x2BD** (a non-confirm box); prompt-format call (PapyrusExtender `GetFastTravelTarget`) at `+OFFSET_3(0x342, 0x3A6, 0x3D9)`. |
| Place-marker callback class | `PlacePlayerMarkerCallbackFunctor` | seen in the empty-ground crash (RDI). Empty-ground placement is already one-click vanilla. |
| `MapMenu::PlaceMarker` | `RELOCATION_ID(52226, 53113)` | where non-travelable clicks route; we deliberately do NOT hook it. |

### Scope clarification from in-game testing (simplifies the feature)
Only **two** map-click outcomes actually exist in Mase's game:
1. Click a **discovered/travelable** marker → fast-travel confirm box (the only thing OneClickMap
   needs to change → instant travel).
2. Click **anything else** (empty terrain, undiscovered, marker management) → routes through
   `PlaceMarker`, which is **already one-click-correct in vanilla** (instant place when no marker;
   Move/Leave/Remove when one exists). So the design's other branches need **zero code** — the only
   feature is "discovered click → instant travel." Popup #4 has no dedicated callback class
   (it's a generic box inside `PlaceMarker`); the design's documented fallback (leave it vanilla)
   stands.

## NG / toolchain gotchas (save the next session time)
- Editor/LSP shows false `SKSE/SKSE.h not found` + `undeclared RE/SKSE/REL` diagnostics (no
  FetchContent include paths). **Ignore them; trust `./build.sh`.**
- `CMakeLists.txt` keeps the `rapidcsv` FetchContent block even though OneClickMap has no CSV use —
  CommonLibSSE-NG itself references `RAPIDCSV_INCLUDE_DIRS`; the build fails without it.
- `TESFullName::GetFullName` is a non-inlined import this NG static lib does **not** export — using
  it fails the link. Log marker **FormID** instead of name.
- `write_call<5>` / `write_branch<5>` need `SKSE::AllocTrampoline(...)` first; they only work at a
  real `E8`/`E9` instruction (see root cause).
- DRM-encrypted `.text` ⇒ no static disassembly; observe at runtime instead (byte-scan,
  `_ReturnAddress`, in-game probes). The `.text` IS decrypted by the time SKSE loads plugins
  (the probe's `write_call` at `52208/53095+0x3A6` worked at load).

## Build / install / test
- Build: `./mods/OneClickMap/build.sh` → `OneClickMap.dll` (PE32+).
- Install: `./mods/OneClickMap/build.sh --install` → copies into the live game's `SKSE/Plugins`.
- Log: `<prefix>/Documents/My Games/Skyrim Special Edition/SKSE/OneClickMap.log`.
- Crash logs (CrashLogger): `<prefix>/.../SKSE/crash-*.log`. Trust `[P]` frames; `[S]` are stack
  scans and can be false (the 52292 lead was a false `[S]`).
- In-game verification only Mase can run (Proton prefix, this desktop). SKSE loads plugins at
  launch → fully restart the game to pick up a new DLL.

## Precedents (already fetched / referenced)
- `powerof3/PapyrusExtenderSSE` `src/Game/HookedEventHandler.cpp`, namespace `FastTravel`: the
  `write_vfunc<FastTravelConfirmCallback, 0x1>` hook, the `func(callback, kUnk0/kUnk1)` drive/cancel
  idiom (kHide only on cancel), and the `52208/53095 + OFFSET_3(0x342,0x3A6,0x3D9)` format-call hook.
- Exit-9B / "Disable Fast Travel SKSE" (MIT): pattern-guarded `write_call<6>` at `53095+0x31F` (AE)
  — a decision-site hook (toggles travel on/off, can't drive one-click); validated on 1.6.318, not
  1.6.1170. Useful as a guarded-call-site pattern reference.

## Plan-task status
- Task 1 (scaffold), Task 2 (read-only probe) — done & committed.
- Task 3 (instant-travel dispatch) — travel works; **blocked** on the safe-suppression mechanism
  (this doc). Task 4 (docs) — pending the real fix. Finish/wrapup — pending.
