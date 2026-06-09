# headless — drive Skyrim with no monitor, no hands

A way to run Skyrim SE **invisibly** (no window on any monitor), **screenshot** it on demand,
and **inject input** into it — all without touching your real keyboard/mouse/desktop. The point
is a tight test loop: instead of a human launching the game, loading a save, clicking a few
things, and reporting back, this harness lets an agent do it directly — load in, screenshot,
read the actual game state, press keys, confirm the result.

It's a **testing tool for this repo's mods** (e.g. `OneClickMap`'s world-map confirmation box —
the exact thing that motivated this). Long-term it pairs with the SKSE C++ tier: gamescope for
*eyes*, and eventually an in-process SKSE plugin for *hands + ground-truth state*.

> Status in one line: **invisible render + screenshots + isolated keyboard input all work.**
> Mouse positioning/click does not yet land in-game. See `docs/status.md`.

## How it works (the short version)

| Concern | Mechanism |
|---|---|
| **Invisible + GPU render** | `gamescope --backend headless` — renders Skyrim to an offscreen buffer, no output on any monitor. |
| **Screenshot** | Send gamescope `SIGUSR2` → it writes `/tmp/gamescope_<ts>.avif`; convert to PNG. (X11 grabbers see only black — the game is Vulkan/DXVK.) |
| **Input** | **libei** via gamescope's `gamescope-0-ei` socket. Relative-native, evdev keycodes, scoped to that session — **cannot leak to your seat**. |
| **Isolation** | A headless gamescope has no host seat, so injected input never reaches your real cursor/keyboard. Measured: XTEST into the session left the real pointer unmoved. |

Full rationale in [`docs/design.md`](docs/design.md). The expensive dead-ends (and why each
failed) are in [`docs/findings.md`](docs/findings.md) — **read that before changing the approach.**

## Layout

| Path | What |
|------|------|
| `launch.sh` | Start Skyrim+SKSE in a headless gamescope (the `SteamAppId` fix is baked in). |
| `shot.sh` | `SIGUSR2` → newest gamescope AVIF → PNG (optional crop/scale). |
| `drive.sh` | Friendly wrapper over `src/eidriver` (`tap enter`, `seq down down enter`, `click`, `rel`, `abs`). |
| `stop.sh` | Kill the session cleanly. |
| `src/eidriver.c` | The libei input client. Keyboard ✅, pointer ⚠️ (see status). |
| `src/build.sh` | `gcc` + `pkg-config libei-1.0`. No sudo. |
| `docs/design.md` | Architecture + why each piece. |
| `docs/findings.md` | Every wall we hit and the reason. |
| `docs/status.md` | What works, what's left, next steps. |

## Quick start

```bash
cd headless
src/build.sh                       # compile the libei input client (once)
./launch.sh                        # start Skyrim headless; wait ~1-2 min for the menu
./shot.sh /tmp/sky.png             # capture the current frame
./drive.sh seq down down enter     # keyboard-navigate the menu (CONTINUE->NEW->LOAD, then activate)
./shot.sh /tmp/sky2.png            # see the result
./stop.sh                          # tear it down
```

## Requirements (all already present on this machine, no root)

`gamescope`, `libei` (+ header at `/usr/include/libei-1.0`, resolved by `pkg-config`), `gcc`,
ImageMagick (`magick`). A Skyrim SE + SKSE install (launcher swapped to SKSE; prefix is currently
Proton Experimental). `sudo` is **not** available here — everything is user-space/compiled.
