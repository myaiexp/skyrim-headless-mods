# OneClickTravel

> **Status: working, verified in-game (v1.0.0, AE 1.6.1170).** Click a discovered location on
> the world map and you travel, instantly, no confirmation box.

Open the world map, click a place you've already discovered, and you're there. No "Fast travel to
X? Yes / No / Place Marker" box to click through first. The click _is_ the answer. Every other map
interaction, and every other game message box, is left exactly as vanilla.

## What it does

- **Discovered location → instant fast travel.** Clicking a fast-travelable marker starts the trip
  immediately. The vanilla confirmation box is suppressed _before it ever renders_. No flash, no
  extra click.
- **Everything else is bit-for-bit vanilla.** Placing, moving, or removing your custom marker, and
  every unrelated message box in the game (enemies-nearby travel warnings, quit prompts, anything),
  behaves exactly as it always did. The mod only ever touches the fast-travel confirmation.

## Requirements

- Skyrim Special Edition or Anniversary Edition + **SKSE**
- **Address Library for SKSE Plugins**

No `.esp`, no scripts, no Papyrus, no SkyUI: a single SKSE DLL. It takes no load-order slot.

## Compatibility

- **SE + AE: one DLL for both.** Built on CommonLibSSE-NG; every engine address is resolved at
  runtime through the Address Library, so the same file runs on every SE and AE build (Steam or
  GOG) as long as Address Library is installed. **Verified in-game on AE** (v1.6.1170).
- **Map-replacer mods: fully compatible.** It hooks the engine's native C++ message box, **not**
  `map.swf`, so it stacks cleanly with map overhauls that replace the map interface: A Quality
  World Map, FlatMapMarkers, Atlas Map Markers, Baka World Map, and the like. It changes _when the
  confirm box fires_, not how the map is drawn.
- **VR: untested.** No VR-specific build is provided.

## The one accepted trade-off

- **"Place Marker" on a discovered location is gone.** Because the whole confirmation box is
  suppressed, you can no longer pick **Place Marker** _on_ a fast-travelable location. Clicking a
  discovered place always travels. That's the entire point of the mod, and it's the only vanilla
  behavior it removes. Marker placement everywhere else (undiscovered locations, empty terrain) is
  untouched.

## Installation

1. Install with a mod manager and enable it (or drop `OneClickTravel.dll` into `Data/SKSE/Plugins/`).
2. **Restart Skyrim.**

That's all. It's active immediately, always on, no configuration.

## How it works

Skyrim's "Fast travel to X?" prompt is a **native C++ message box**, not part of `map.swf` and not
Papyrus, which is why an SWF edit would be the wrong layer and would conflict with map-replacer
mods. Every message box in the game is handed to the UI through a single chokepoint,
`MessageBoxData::QueueMessage`. OneClickTravel detours that entry with **MinHook** and inspects each
box on its way in:

- If the box's callback is a **`FastTravelConfirmCallback`** (vtable match) **and** the marker under
  the cursor is flagged travelable (`kCanTravelTo`), the plugin drives the trip directly via
  `callback->Run(kUnk1)` (the same "Yes / travel" primitive vanilla uses) and **returns without
  queuing the box**. It's never rendered, so there's no flash to dismiss.
- **Every other box**, including a fast-travel box for a somehow-non-travelable target, is passed
  straight through to the original `QueueMessage` unchanged. Nothing else in the game is affected.

The entry detour uses MinHook (rather than a `call`-site rewrite) so the original function's prologue
is relocated into a valid trampoline. The pass-through path is safe for every non-travel box. An
`OneClickTravel.log` in the SKSE log dir records each interception for verification.

## Building from source

Linux, headless, no Creation Kit or SSEEdit.

```bash
./build.sh            # configure + build -> build/OneClickTravel.dll
./build.sh --install  # also copy the DLL into the live game's SKSE/Plugins
```

Cross-compiled Linux → Windows with the in-repo `tools/skse` toolchain (clang-cl + lld-link + xwin;
CommonLibSSE-NG and MinHook fetched and pinned by CMake). See `../../docs/skse-toolchain.md`.

## Design notes

The full design (the scope cut to a single branch, why `QueueMessage` is the right chokepoint, why
MinHook over a `call`-site rewrite, and the verified engine address book) lives in
`../../docs/plans/oneclick-travel-design.md`. Deferred work (a Shift-click escape hatch to restore the
vanilla Place-Marker option, MCM/INI config) is in `../../docs/ideas.md`.
