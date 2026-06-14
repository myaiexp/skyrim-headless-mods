# OneClickTravel — Nexus page copy

**Status: release-ready (v1.0.0, verified in-game on AE 1.6.1170).** This is the copy to paste into
the Nexus mod page. Honesty over hype throughout — single-purpose and always-on by design, that's the
feature, not a gap. Page title can be **OneClickTravel** or, if a spaced title reads better in search,
**One-Click Map — Instant Fast Travel**; the DLL/plugin name stays `OneClickTravel` either way.

## The one-liner

> Click a discovered location on the world map and you just go. No "Fast travel to X? Yes / No /
> Place Marker" box — the click is the answer.

## What it does

Open the map, click a place you've already discovered, travel. The vanilla confirmation box is
suppressed _before it renders_ — no flash, no second click. Everything else on the map, and every
other message box in the game, is left exactly as vanilla.

## What's genuinely new (state it plainly, don't trash other mods)

No published SE/AE mod does this (verified across Nexus and GitHub). The closest neighbor, **Disable
Fast Travel SKSE** (Nexus 54217), hooks the _same_ engine callback to do the **opposite** — cancel
travel rather than confirm it instantly — which is a nice proof that this is the right hook point,
aimed the other way. Frame it as "here's a small thing that hadn't been done," not as a competitor
takedown.

## How it works (short, for the curious)

- Pure **SKSE C++** engine hook — **script-free**. No SkyUI, no Papyrus, no `.esp`, no animation
  frameworks. One DLL, no load-order slot.
- The "Fast travel to X?" prompt is a native message box, not part of `map.swf`. The mod detours the
  single engine chokepoint every box passes through, and when it sees the fast-travel confirm for a
  travelable marker it drives the trip directly and skips the box. Every other box passes through
  untouched — so it stacks cleanly with map-replacer mods (it changes _when the box fires_, not how
  the map is drawn).

## Requirements

- **SKSE64** and **Address Library for SKSE Plugins**. (Do **not** bundle Address Library — require it.)
- **Runtime:** AE / 1.6.x **tested in-game**. SE (1.5.97) is **built but untested** — the DLL resolves
  the correct address per runtime via Address Library, but I have no SE install to verify on. VR is
  unsupported. Label SE clearly as unverified, not unsupported.

## Known limitations (say this up front)

- **"Place Marker" on a discovered location is gone.** Because the whole confirm box is suppressed,
  you can no longer drop a custom marker _on_ a fast-travelable location — clicking it always travels.
  That's the entire point, and it's the **only** vanilla behavior the mod removes. Marker placement
  everywhere else (undiscovered locations, empty terrain) is untouched.
- **Always-on, no toggle.** Every discovered-location click travels instantly. If someone downloads
  it, they want it to work — config isn't a goal. (A Shift-click escape hatch to restore the vanilla
  Place-Marker option on demand is a possible future addition; see the GitHub `docs/ideas.md`.)

## Compatibility

Script-free and dependency-light, so broadly compatible. It hooks the native message box rather than
`map.swf`, so it works alongside map overhauls (A Quality World Map, FlatMapMarkers, Atlas Map
Markers, Baka World Map, …). It only ever touches the fast-travel confirmation; no other UI, no NPC
behavior, no animations.

## The behind-the-scenes hook (flavor, not the headline)

Built **entirely on Linux, headless** — cross-compiled to a Windows DLL with clang-cl + lld-link +
xwin and CommonLibSSE-NG, **no Creation Kit, no SSEEdit, no Windows PC ever involved.** Worth a short
trivia line; don't make it the headline.

## Open decisions before publishing

- **Header / page image** — needs an asset (a world-map screenshot reads best). The one thing not
  generated in-repo.
- Permissions / donation settings — your call. (All original code, no third-party IP — unlike DBVO
  Dialogue Tweaks, there's no upstream author to credit and no permission to honor.)
- Version label — v1.0.0 as built, or a v0.x/beta label to set first-release expectations.
