# AutoFireBow — Nexus page concept

**Status: draft outline, not final copy.** The mod is still being iterated on; this captures
_what to communicate_ on the release page and _why_, so the messaging we worked out isn't lost.
Turn into prose at release time. Honesty over hype throughout — overclaiming is what gets a first
mod torn apart in the comments.

## The one-liner

> Hold attack with a bow and it fires continuously — every arrow at full power, full damage,
> instantly, no matter how briefly you tapped. **Nock, draw, release, loop.**

Single-purpose and always-on by design. "You don't have to think about it" is the feature, not a
gap. (Config — master toggle, feature split, hotkey — is planned; see `docs/ideas.md`.)

## What's genuinely new (state it plainly, don't trash other mods)

Nothing published does this exact combination: **hold-to-continuously-auto-fire + every shot
forced to full power/damage via an engine hook, script-free, with zero animation-framework
dependencies.** Verified by searching Nexus, GitHub, Reddit, and old forums — every adjacent mod
compromises on the axis this one doesn't:

- Archery Gameplay Overhaul — spammable, but hard-caps power at **80%**.
- Bow Rapid Combo — fast, but rapid shots do **~1/3 damage** and need Nemesis/MCO/DAR.
- Bow Charge Plus — rapid/multishot, but tied to **dodge mechanics + MCM**, charge-level gated.
- Semi-automatic Bows and Arrows — strips the draw animation, but you **still must hold** for power.

Frame as "here's what it does and it hasn't been done," **not** as a competitor takedown.

## How it works (short, for the curious)

- Pure **SKSE C++** engine hook — **script-free**. No Nemesis, MCO, DAR, SkyUI, or `.esp`.
- Auto-loose keys off the engine's own **`BowDrawn`** event, so it's agnostic to bow speed, perks,
  and enchantments — no draw-time guessing.
- Full power via hooking `ArrowProjectile::GetPowerSpeedMult` and clamping the arrow's draw power
  to 1.0 (then letting the engine recompute full speed + damage). Player-only; NPCs unaffected.

## Requirements

- **SKSE64** and **Address Library for SKSE Plugins**. (Do **not** bundle Address Library — require it.)
- **Runtime:** AE / 1.6.x **tested in-game**. SE (1.5.97) and VR are **built but untested** — the
  DLL resolves the correct vtable slot per runtime, but I have no SE/VR install to verify on.
  Label this clearly so SE/VR users know it's unverified, not unsupported.

## Known limitations (say these up front)

- **Always-on, no toggle yet.** Every bow tap becomes a full-power auto shot. Config is planned.
- **Damage clamp is lightly tested** against other damage-altering mods — flag as early/beta.
- **Crossbows:** the power clamp is a no-op (they already fire at full draw); the auto-fire loop is
  bow-only. So crossbows behave normally — worth a one-liner so nobody expects auto-crossbow.

## Compatibility

Script-free and dependency-light, so broadly compatible. Player-only and per-arrow, so it won't
touch NPC archers or stack oddly with multishot. No animations replaced.

## The fun fact / behind-the-scenes hook

Built **entirely on Linux, headless** — cross-compiled to a Windows DLL with clang-cl + lld-link +
xwin and CommonLibSSE-NG, **no Creation Kit, no SSEEdit, no Windows PC ever involved.** Worth a
short "trivia" line: this mod has literally never touched Windows. (Mase's note: this was <5% of
the effort vs the mod itself — mention as flavor, don't make it the headline.)

## Open decisions before publishing

- Permissions / donation settings — your call.
- Version label (v0.x / beta) to set expectations for a first release.
- Whether to ship the INI + hotkey config first (see `docs/ideas.md`) or release always-on and add
  config based on comments.
