# AutoFireBow

> **Status: working, AE-tested (v1.6.1170, plugin v2.1.0); SE/VR built but untested.** Hold attack
> with a bow and it auto-fires continuously — each shot a genuine full-draw release, configurable
> through an in-game SkyUI menu.

Nock, draw, hold, and the bow keeps firing on its own — every arrow loosed at a real, fully-charged
draw, no matter how briefly you meant to tap. The loop rides the engine's own draw events and
synthetic input, so it's agnostic to bow speed, perks, and enchantments: there's no draw-time
guessing and no animation framework underneath. An MCM master toggle, hotkey, and two sliders let
you tune or disable it without touching the load order.

## What it does

- **Hold-to-auto-fire.** While the attack control is held, the bow re-nocks and looses repeatedly.
  Release the button and it stops. One control, always the same gesture.
- **Every auto shot is genuinely full-draw.** The loop fires off the engine's own `BowDrawn` event
  (real full charge) and looses by replaying a synthetic attack-release, so the engine launches an
  honestly-charged arrow — full power, speed, and damage. No partial "wet noodle" auto shots.
- **A small auto-only damage bump** (default +10%) compensates for the auto loop's cadence lagging
  skilled manual play. It scales DPS, not single-shot power, and applies **only** to auto-fired
  arrows — manual single shots pass through completely vanilla.
- **Player-only, per-arrow.** NPC archers are untouched; nothing about multishot or other archers
  is altered.
- **Crossbows behave normally.** The auto-fire loop is bow-only, so crossbows are unaffected — no
  auto-crossbow.

## Requirements

- Skyrim Special Edition or Anniversary Edition + **SKSE**
- **Address Library for SKSE Plugins**
- **SkyUI** — required for the in-game MCM (the configuration menu _is_ a SkyUI MCM)

Ships an `AutoFireBow.esp` (a Start-Game-Enabled quest hosting the MCM) plus two `.pex` scripts and
one SKSE DLL.

## Compatibility

- **SE + AE, one DLL for both.** Built on CommonLibSSE-NG; the `GetPowerSpeedMult` vtable slot and
  every engine address are resolved at runtime through the Address Library, so the same file runs on
  every SE and AE build (Steam or GOG) as long as Address Library is installed. **Verified in-game
  on AE** (v1.6.1170).
- **VR, untested.** The DLL resolves the correct per-runtime vtable slot, but no VR install exists
  here to verify on — treat VR as unverified, not unsupported.
- Script-free in the hot path and player-only, so it won't stack oddly with multishot mods or touch
  NPC archers. No animations are replaced.

## Installation

1. Install with a mod manager and enable it (it ships an `.esp` + scripts + a DLL), or copy the
   files in by hand: `AutoFireBow.dll` → `Data/SKSE/Plugins/`, the two `.pex` → `Data/Scripts/`,
   `AutoFireBow.esp` → `Data/` (and enable it in your load order).
2. **Fully restart Skyrim** (the Papyrus VM caches `.pex` per session).

The auto-fire is on by default; open the **AutoFireBow** MCM to toggle it, bind a hotkey, or adjust
the sliders.

## How it works

Two engine hooks plus a one-way config bridge, all in a single SKSE DLL (`src/main.cpp`):

**1. `BowLoopSink` → auto-loose off `BowDrawn` (the rapid-fire loop).** An animation-graph sink on
the player watches for the engine's own bow events. On `BowDraw` (nock) it re-arms a per-cycle fire
guard; on `BowDrawn` (genuine full draw) it calls `LooseNow`, which replays a synthetic
`"Right Attack/Block"` release through `BSInputDeviceManager` so the engine looses the
already-fully-charged arrow on its real path. The re-nock is an explicit synthetic _press_ injected
once the auto arrow has actually launched (a held button never self-redraws — holding _is_ the draw,
releasing _is_ the loose, on the same axis). An `AttackInputSink` tracks whether the button is
really held so the loop gates correctly and ignores its own injected events.

**2. `AutoArrowHook` → the auto-only damage bump.** A vtable hook on
`ArrowProjectile::GetPowerSpeedMult` multiplies `weaponDamage` by the configured bonus, gated on the
shooter being the player and on the arrow having been tagged by the auto-loose (`g_boostNextArrow`,
cleared on first apply so the bump can't compound). There is **no power clamp** — auto arrows are
already at honest full draw; this hook only scales damage on the tagged auto arrow. Manual arrows
return the engine's value untouched.

**Config bridge (Papyrus → DLL, one-way).** The MCM (`AutoFireBowMCM`, `extends SKI_ConfigBase`)
owns the settings as per-save `Auto` properties and pushes them to the DLL through three `Global
Native` setters on the hidden `AutoFireBow` script (`SetEnabled`, `SetDamageBonus`,
`SetMinShotDelay`). The DLL stores each in an atomic and reflects it; it never reads back, so
persistence stays entirely in the co-save. On every load, `OnGameReload` re-pushes all values and
re-establishes the hotkey registration. DLL defaults match the script defaults (enabled, +10%,
0 ms) so behavior is correct before the first push or with no MCM interaction.

An `AutoFireBow.log` in the SKSE log dir records the loop's loose/re-nock/damage decisions for
in-game verification.

## Limitations

- **SE/VR are built but untested.** AE (v1.6.1170) is verified in-game; the SE/VR paths use
  CommonLibSSE-NG's own authoritative vtable index but have not been run on those runtimes.
- **The damage bump is a flat buff, premise pending verification.** It was conceived to offset lost
  DPS, but a probe found auto arrows already read full power in the test setup (draw-scaling may not
  have been active there) — so the +10% currently reads as a plain buff. Whether `weaponDamage` maps
  to real enemy damage on a clean vanilla save is still open (see design notes).
- **Crossbows get no auto-fire.** Intentional — the loop is bow-only.

## Building from source

Linux, headless: no Creation Kit or SSEEdit.

```bash
./build.sh            # compile .pex + generate .esp + cross-build the DLL -> build/
./build.sh --install  # also copy all four artifacts into the live game + activate the esp
```

`build.sh` runs three steps: compile `AutoFireBowMCM.psc` + `AutoFireBow.psc` against the vendored
SkyUI sources (wine PapyrusCompiler; only our own `.pex` ship), generate `AutoFireBow.esp` via
EspGen/Mutagen (a Start-Game-Enabled quest + a `SKI_PlayerLoadGameAlias` player alias), then
cross-compile the DLL with the in-repo `tools/skse` toolchain (clang-cl + lld-link + xwin;
CommonLibSSE-NG fetched and pinned by CMake). See `../../docs/skse-toolchain.md` and
`../../docs/papyrus-toolchain.md`.

## Design notes

The MCM architecture — the `SKI_ConfigBase` page, the one-way Papyrus→DLL native bridge, the EspGen
quest+alias, and the min-shot-delay timer thread — is in `../../docs/plans/autofirebow-mcm-design.md`
(it follows the DBVODialogueTweaks MCM precedent in this repo). The release messaging draft for the
Nexus page is in `../../docs/autofirebow-nexus-page.md`.

The current build is the result of the **real-charge spike**
(`../../docs/plans/autofirebow-real-charge-design.md`): it replaced the old full-power _clamp_ on
`GetPowerSpeedMult` with synthetic input-release driving the engine's genuine charge path, so auto
arrows are honestly charged and the only remaining use of the hook is the auto-only damage bump.
That same doc records the open question about whether the +10% bonus is still warranted. Further
deferred work (and the history of the original INI-config plan the MCM superseded) is in
`../../docs/ideas.md`.
