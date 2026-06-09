# OneClickMap — design

**Status:** approved 2026-06-09, not yet built.
**Type:** SKSE C++ plugin (tier 2), CommonLibSSE-NG, headless clang-cl toolchain.
**Target:** Skyrim SE/AE **v1.6.1170**, SKSE.
**Working name:** `OneClickMap` (provisional, rename freely).

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

First milestone, before committing to the full build: a probe that **identifies and fires
popup #4's trigger** from the hook (find the callback class / the MapMenu path that raises the
move/leave/remove box, mirroring how `FastTravelConfirmCallback` was found). If #4 can be
raised cleanly on demand, the rest is well-trodden (travel + `PlaceMarker` are both proven).

**Fallback if #4 can't be invoked cleanly:** for the *undiscovered-location + marker-exists*
case only, fall through to vanilla (popup #3, "Place marker? Yes/No"). Empty-terrain +
marker-exists already shows #4 natively, so the common case still works; only the changed
box #3 would revert. Documented deviation, not a blocker.

## Architecture

- **New standalone plugin:** `plugins/OneClickMap/`, built with the existing headless
  clang-cl + lld-link + xwin + CommonLibSSE-NG (FetchContent) toolchain, mirroring
  `plugins/AutoFireBow/` and `plugins/GhostAllies/` (own `CMakeLists.txt`, `build.sh`,
  `src/main.cpp`). Loads/ships/disables independently — one DLL, one responsibility.
- **Logging:** own `OneClickMap.log` in the SKSE log dir (same pattern as the sibling
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
