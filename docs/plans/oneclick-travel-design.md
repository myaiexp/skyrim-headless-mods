# OneClickTravel — design

**Status:** ~~approved 2026-06-09, not yet built~~ → **finishing 2026-06-14.** Instant fast-travel
already works in-game; the build is being made shippable via a MinHook entry detour. **Read the
"Mechanism update (2026-06-14)" section below first — it supersedes the scope/mechanism sections
beneath it**, which are kept as the original design record.
**Type:** SKSE C++ plugin (tier 2), CommonLibSSE-NG, headless clang-cl toolchain.
**Target:** Skyrim SE/AE **v1.6.1170**, SKSE.
**Name:** `OneClickTravel` — **locked** (drives `mods/OneClickTravel/`, `CMakeLists.txt` target,
`build.sh`, and `OneClickTravel.log`; renaming later means touching all four, so pin it at step 0).

## Mechanism update (2026-06-14) — scope cut + MinHook entry detour (supersedes the sections below)

Two things changed after the in-game probing (its notes are inlined in this section):

**Scope collapsed to a single branch.** In the live game only two map-click outcomes actually
occur: a **discovered/travelable** click (wants instant travel) and **everything else**, which
already routes through `MapMenu::PlaceMarker` and is **already one-click-correct in vanilla**
(instant place when no marker; Move/Leave/Remove when one exists). So the three-way dispatch, the
`52208/53095` click-handler hook, and the popup-#4-invocation "open question" below are **not
needed** — the only code is "discovered click → instant travel." Non-travel clicks never reach the
plugin.

**Hook point + mechanism.** Intercept `MessageBoxData::QueueMessage` (`RELOCATION_ID(51422, 52271)`),
the single chokepoint that hands a box to the UI, and gate on the callback's vtable:

- `callback` is `FastTravelConfirmCallback` **and** the cursor marker is `kCanTravelTo` →
  `Run(kUnk1)` (drives the trip + closes the map), return **without** calling the original →
  the box never renders (true pre-render suppression, no flash).
- anything else → call the original `QueueMessage` → 100% vanilla.

The detour uses **MinHook** (FetchContent, mirroring DBVO v4's verified idiom: `MH_Initialize` →
`MH_CreateHook` → `MH_EnableHook`, original stored as the relocated-prologue trampoline). This is
the fix for the stopgap build's one bug: NG's `write_branch<5>` cannot relocate a function prologue,
so its pass-through `func` was a wild pointer that crashed on the first non-fast-travel box. MinHook
relocates the prologue → valid `original` → the pass-through is safe.

### Alternatives considered

- **Path C — let the box render, auto-click "Yes"** (the handoff's then-recommended path, written
  before MinHook was in the repo). Rejected once MinHook landed: `MessageBoxMenu` exposes **no**
  reference to its `MessageBoxData`/callback (header-confirmed), so C would *still* need a
  QueueMessage/format-call hook to capture the callback — and on top of that adds a ~1-frame box
  flash plus a fragile click→menu-open correlation. MinHook keeps the proven detect/drive logic,
  suppresses pre-render (no flash), and reuses an in-repo, in-game-verified pattern.
- **Path A (return-address probe → `write_call`) / Path B (hand-rolled prologue relocation)** —
  both obsolete now that MinHook (a real entry-detour) is a vendored, in-game-proven dependency
  here (DBVO v4).

### Files

- `mods/OneClickTravel/src/main.cpp` — swap the install body to MinHook; rename `func` → `original`;
  delete the now-false "KNOWN LIMITATION — CRASHES" comment; refresh the header comment.
- `mods/OneClickTravel/CMakeLists.txt` — add the MinHook `FetchContent` block + `minhook` to
  `target_link_libraries` (copy DBVO's).

### Test (in-game, the part only the user can run)

1. `./mods/OneClickTravel/build.sh --install`, fully restart the game.
2. Open map, click a **discovered** marker → instant travel, no box *(regression — already worked)*.
3. Trigger a **non-travel** box → **no crash** *(the whole fix)* — e.g. try to fast-travel with
   enemies nearby, or hit a quit/exit confirm; the box appears and behaves vanilla.
4. Click an **undiscovered** location → vanilla marker-place still works.

## Reference — verified address book + gotchas (folded from the retired handoff, 2026-06-14)

The OneClickTravel handoff doc was retired once v1 shipped; its load-bearing reference is preserved
here. All values confirmed in-game on v1.6.1170 or against NG headers.

### Address book

| Thing | Value / locator | Notes |
|-------|-----------------|-------|
| `MessageBoxData::QueueMessage` (the hook target) | `RELOCATION_ID(51422, 52271)` | non-virtual member; `this` = `MessageBoxData*` in RCX. Detoured at entry via MinHook. |
| `MessageBoxData::callback` (the vtable gate) | `BSTSmartPointer<IMessageBoxCallback>` @ offset `0x40` | `.get()` → raw callback ptr; compare `*(uintptr_t*)cb` to `FastTravelConfirmCallback::VTABLE[0]`. |
| `FastTravelConfirmCallback` | members `MapMenu* mapMenu`@0x10, `cursorPosX`@0x18, `cursorPosY`@0x1C; `Run` is vtable slot **0x1** | the confirm box's callback. |
| Travel-drive primitive | `callback->Run(IMessageBoxCallback::Message::kUnk1)` | kUnk1 = "Yes/travel"; **drives the trip AND closes the map.** Do NOT also send `kHide` — it cancels the trip ("map closes, no travel"). |
| Cursor marker travelable? | `cb->mapMenu->GetRuntimeData().mapMarker.get().get()` → `extraList.GetByType<ExtraMapMarker>()->mapData->flags.any(MapMarkerData::Flag::kCanTravelTo)` | discovered-vs-not test; customMarker flips it correctly. |
| `playerMapMarker` (custom marker exists?) | `PlayerCharacter::GetSingleton()->GetInfoRuntimeData().playerMapMarker` | NB: `GetInfoRuntimeData()`, **not** `GetPlayerRuntimeData()`. Unused by v1 — kept for the deferred modifier-key / marker work. |
| MapMenu click handler | `RELOCATION_ID(52208, 53095)` | only direct `E8→QueueMessage` is at **+0x2BD** (a non-confirm box); prompt-format call at `+OFFSET_3(0x342, 0x3A6, 0x3D9)`. Not hooked by v1. |
| `MapMenu::PlaceMarker` | `RELOCATION_ID(52226, 53113)` | where non-travelable clicks route — already one-click-correct vanilla; deliberately NOT hooked. |
| Place-marker callback class | `PlacePlayerMarkerCallbackFunctor` | the empty-ground placement path; one-click vanilla already. |

### Toolchain gotchas

- Editor/LSP shows false `SKSE/SKSE.h not found` / `undeclared RE/SKSE/REL` diagnostics (no
  FetchContent include paths). **Ignore them; trust `./build.sh`.**
- `CMakeLists.txt` must keep the `rapidcsv` FetchContent block even though OneClickTravel has no CSV
  use — CommonLibSSE-NG itself references `RAPIDCSV_INCLUDE_DIRS`; the build fails without it.
- `TESFullName::GetFullName` is a non-inlined import this NG static lib does **not** export — using
  it fails the link. Log a marker's **FormID**, not its name.
- Entry detours use **MinHook** (FetchContent), not NG's `write_branch<5>`/`write_call<5>`: those
  only rewrite an existing `E8`/`E9` call/branch site (and need `SKSE::AllocTrampoline` first), so
  they cannot safely detour a function entry — the bug this build fixed.
- DRM-encrypted `.text` ⇒ no static disassembly; observe at runtime (byte-scan, `_ReturnAddress`,
  in-game probes). The `.text` IS decrypted by the time SKSE loads plugins.

### Build / install / test

- Build: `./mods/OneClickTravel/build.sh` → `OneClickTravel.dll` (PE32+); `--install` copies it into the
  live game's `SKSE/Plugins`.
- Log: `<prefix>/Documents/My Games/Skyrim Special Edition/SKSE/OneClickTravel.log`.
- Crash logs (CrashLogger): `<prefix>/.../SKSE/crash-*.log` — trust `[P]` frames; `[S]` are stack
  scans and can be false.
- In-game verification is Mase-only (Proton prefix, this desktop); SKSE loads plugins at launch, so
  fully restart the game to pick up a new DLL.

## Goal

Remove the confirmation popups on the world map so a click does the obvious thing in one
step: clicking a **discovered** location fast-travels immediately (no "Fast travel to X?
Yes/No/Place Marker" box), and clicking a non-travelable spot **places** a custom marker
immediately when you don't have one. The only popup that survives is the marker
**management** menu (Move/Leave/Remove), which is exactly what you want when a marker
already exists.

No equivalent mod exists for SE/AE (verified). The adjacent *Disable Fast Travel SKSE*
(Nexus 54217) hooks the **same** fast-travel callback to do the opposite (cancel travel).

## The four vanilla popups (ground truth, from in-game screenshots)

| # | You click | Custom marker set? | Vanilla popup | Buttons |
|---|-----------|--------------------|---------------|---------|
| 1 | **Discovered** location     | —   | "Fast travel to X?"                        | Yes / No / **Place Marker** |
| 2 | **Undiscovered** location   | No  | "You have not discovered this location yet. Place marker?" | Yes / No |
| 3 | **Undiscovered** location   | Yes | *same as #2*                               | Yes / No |
| 4 | **Empty terrain**           | Yes | "Do you want to move your marker or remove it?" | Move It / Leave It / Remove It |

Vanilla keys the choice off **location-vs-empty-terrain**. This mod re-keys it off
**travelable + does-a-marker-exist**, which is simpler and matches intent.

## Behavior spec (approved)

The single decision at every map click:

| You click | Marker exists? | New behavior | vs vanilla |
|-----------|----------------|--------------|------------|
| Discovered (travelable) location | —   | **Fast travel instantly**          | skips box #1 |
| Undiscovered / non-travelable    | No  | **Place custom marker instantly**  | skips box #2 |
| Empty terrain                    | No  | **Place custom marker instantly**  | skips the place prompt |
| Undiscovered / non-travelable    | Yes | **Move/Leave/Remove menu**         | **changes** box #3 (was Place-marker Yes/No) |
| Empty terrain                    | Yes | **Move/Leave/Remove menu**         | unchanged (box #4) |

Reduced to logic — no location-type branching, just two predicates:

```
if (markerUnderCursorIsTravelable)        -> fast travel now
else if (no custom marker currently set)  -> place custom marker at cursor now
else                                       -> show Move/Leave/Remove menu
```

### Accepted consequences (confirmed with user 2026-06-09)

1. **"Place Marker" on a discovered location is gone.** Clicking a discovered place always
   travels — you can no longer drop a custom marker *on* a fast-travelable location. Intended
   (that's the whole "click discovered → just go" ask).
2. **Box #3 changes.** Clicking an undiscovered location while a marker already exists now
   gives Move/Leave/Remove instead of "Place marker? Yes/No". To put the marker on that new
   spot, pick **Move It**. Pure result of the "marker exists → management menu" rule.

## Mechanism

All four popups are **native C++** message boxes created by the MapMenu click handler — not
the SWF (`map.swf` only renders the generic box) and not Papyrus. So this is an SKSE hook;
an SWF edit would be the wrong layer and would hard-conflict with the user's map stack
(FWMF / FlatMapMarkers / Baka World Map / Atlas Map Markers).

### Known engine pieces (CommonLibSSE-NG / address library)

| Piece | Locator | Use |
|-------|---------|-----|
| MapMenu click handler (builds the prompt) | `RELOCATION_ID(52208, 53095)` | the single branch point; the site PapyrusExtender hooks |
| `RE::FastTravelConfirmCallback::Run` | vtable index `0x1`; `Message::kUnk1` = Yes/travel | drive the travel without showing box #1 |
| `RE::MapMenu::PlaceMarker()` | `RELOCATION_ID(52226, 53113)` | create-or-move the single custom marker at the cursor, from code |
| `RE::PlayerCharacter::playerMapMarker` | `ObjectRefHandle`, offset `0x054` | "does a custom marker exist" — `.get()` valid ⇒ yes |
| Travelable test | `refr->extraList.GetByType<RE::ExtraMapMarker>()->mapData->flags.any(RE::MapMarkerData::Flag::kCanTravelTo)` | classify the marker under the cursor |
| Marker under cursor | `MapMenu` runtime `mapMarker` (`ObjectRefHandle`) | the click target |

All IDs are present in the address library for both SE (1.5.97) and AE (1.6.x); a single
CommonLibSSE-NG runtime-detection DLL covers v1.6.1170. `Run`'s vtable index `0x1` is stable
across SE/AE. No manual sigscanning.

### Approach: suppress at the click handler, branch three ways

Hook the click handler (`52208/53095`). On a click, read the two predicates (travelable
marker under cursor; `playerMapMarker` set) and act directly **instead of** letting the
default box appear:

- **travelable** → drive the fast-travel confirm path with `Message::kUnk1` (the
  PapyrusExtender idiom: `func(callback, Message::kUnk1)`), then hide the menu. No box flash.
- **not travelable + no marker** → call `MapMenu::PlaceMarker()`. No box.
- **not travelable + marker exists** → show the **Move/Leave/Remove** box (popup #4).

This removes the boxes entirely rather than flashing-then-auto-dismissing them (the inferior
alternative of hooking each callback's `Run`, which only fires *after* the user answers and
so can't suppress the box).

### Open implementation question — the proof-point that gates the build

The one piece research did **not** pin down: how popup #4 (Move/Leave/Remove) is triggered in
code, so we can invoke it for the "not travelable + marker exists" branch — including the
undiscovered-**location** case where vanilla would instead show popup #3.

First milestone, before committing to the full build, a probe that proves **all three reads
fire correctly at the hook site**, since a fast-travel mis-fire is as damaging as a missing #4:

1. The **cursor-target REFR** is reliably obtainable at the hook (the `MapMenu` runtime
   `mapMarker` handle resolves), and its **travelable** flag reads correctly
   (`ExtraMapMarker` → `kCanTravelTo`) — distinguishing discovered from undiscovered.
2. **`playerMapMarker`** correctly reports marker-exists / not.
3. **Popup #4's trigger** can be identified and fired on demand (find the callback class / the
   MapMenu path that raises the move/leave/remove box, mirroring how `FastTravelConfirmCallback`
   was found).

If all three hold, the rest is well-trodden (travel + `PlaceMarker` are both proven).

**Fallback if #4 can't be invoked cleanly:** for the *undiscovered-location + marker-exists*
case only, fall through to vanilla (popup #3, "Place marker? Yes/No"). Empty-terrain +
marker-exists already shows #4 natively, so the common case still works; only the changed
box #3 would revert. Documented deviation, not a blocker.

Note the control-flow shape this implies: the main approach **suppresses** the default box on
every branch, but this fallback (and any "let vanilla handle it" case) requires the hook to
also be able to **pass through to the original** cleanly for one specific branch. The plan
should treat "suppress" and "call original" as two distinct, both-supported hook outcomes
rather than assuming every path suppresses.

## Architecture

- **New standalone plugin:** `mods/OneClickTravel/`, built with the existing headless
  clang-cl + lld-link + xwin + CommonLibSSE-NG (FetchContent) toolchain, mirroring
  `mods/AutoFireBow/` and `mods/GhostAllies/` (own `CMakeLists.txt`, `build.sh`,
  `src/main.cpp`). Loads/ships/disables independently — one DLL, one responsibility.
- **Logging:** own `OneClickTravel.log` in the SKSE log dir (same pattern as the sibling
  plugins), used for the proof-point and to verify each branch fires correctly in-game.
- All logic in one file (`src/main.cpp`): the click-handler hook + the two predicate reads +
  the three-way dispatch. No `.esp`, no Papyrus, no config (v1).

## Verification (definition of done)

In-game on v1.6.1170, with the user's live map stack installed:

1. Click a **discovered** location → travels immediately, no popup.
2. Click an **undiscovered location / empty spot** with **no** custom marker → marker placed
   immediately at the cursor, no popup.
3. Click anywhere non-travelable **with** a custom marker already set → **Move/Leave/Remove**
   menu appears (including when clicking an undiscovered location — the changed box #3).
4. Move It / Leave It / Remove It each behave correctly (relocate to the clicked spot /
   cancel / delete).
5. No regression to the map mods (FWMF satellite render, FlatMapMarkers, Baka pan/FOV).

## Precedent (reference implementations)

- `powerof3/PapyrusExtenderSSE` — `src/Game/HookedEventHandler.cpp`, namespace `FastTravel`:
  the hook template for the `52208/53095` click site and `FastTravelConfirmCallback` vfunc
  `0x1`, plus the `ExtraMapMarker`/`kCanTravelTo` classification (`GetMapMarkerFromObject`),
  and the `func(callback, Message::kUnk0/kUnk1)` + `kHide` menu-dismiss idiom.
- *Disable Fast Travel SKSE — No Janky Map UI* (Nexus 54217) — same callback, opposite action
  (cancels travel); proves the hook point is the right one.
- `kassent/CustomMapMarker` (Custom Map Marker Redone) — `CreateMapMarker` / `RemoveMapMarker`
  / `IsCustomMapMarker`; reference for marker REFR + `ExtraMapMarker` manipulation if the
  built-in `PlaceMarker()` proves insufficient.
- CommonLibSSE-NG headers: `RE/M/MapMenu.h` (`PlaceMarker`, runtime `mapMarker`),
  `RE/F/FastTravelConfirmCallback.h` (members + vtable).
