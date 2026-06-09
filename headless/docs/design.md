# Design — headless Skyrim driver

## Goal

Run Skyrim SE so that an automated agent can operate it the way a human tester would —
load in, look at the screen, press a few things, confirm the outcome — **without** the game
appearing on a monitor and **without** stealing the real keyboard/mouse. The motivating chore:
testing `OneClickMap`, which makes a click on a world-map marker fast-travel **instantly** instead
of popping a confirmation box. Verifying it means **clicking a discovered map marker** and checking
it travels with no popup — which inherently needs a working in-game **mouse** (keyboard can't select
an arbitrary marker on the map). That requirement is exactly why the pointer path had to be solved;
the confirmation box itself is keyboard-dismissable and was never the blocker.

Two halves: **eyes** (see the game state) and **hands** (send input). They're solved
separately and that separation is the key design decision.

## Eyes — `gamescope --backend headless` + SIGUSR2

`gamescope` is Valve's micro-compositor (what the Steam Deck runs games inside). Its `headless`
backend renders the game on the GPU into an **offscreen** buffer — no window, no output on any
monitor. The game still gets full GPU acceleration via the DRM render node.

Screenshots: gamescope writes its composited frame to `/tmp/gamescope_<timestamp>.avif` when sent
`SIGUSR2`. We convert AVIF→PNG to view/diff. This is the *only* reliable capture path — the game
renders through Vulkan/DXVK, so X11 grabbers (`import`, `xwd`) capture a black buffer; gamescope's
own dump is the composited result.

What a headless gamescope exposes to its child:
- `DISPLAY=:N` — an inner **Xwayland** for X11 clients. Skyrim under Proton/Wine is an X11 client,
  so this is what it uses. (It does *not* hand the child a Wayland socket — a Wayland-native app
  like `foot` can't connect; irrelevant for Skyrim.)
- `gamescope-0` — gamescope's own Wayland compositor socket.
- `gamescope-0-ei` — a **libei** (EIS) socket for input emulation. This is the hands channel.

## Hands — libei via `gamescope-0-ei`

libei is the freedesktop protocol for *emulated input* (it's how Steam Input / remote play inject).
gamescope advertises a libei server (EIS) on `gamescope-0-ei`. A libei **client** connects there
and sends pointer/button/keyboard events, which gamescope routes into the game.

Why libei and not XTEST/uinput:
- It's **relative-native** (`ei_device_pointer_motion(dx, dy)`) — which is what a raw-input game
  like Skyrim actually consumes. XTEST can only send *absolute* X pointer events, which the game
  mistranslates (see findings).
- It's **socket-scoped**: a libei client connects to `gamescope-0-ei` specifically. It physically
  cannot deliver to your real seat. (`uinput` would — it's a global kernel device.)
- It's **isolated by the headless-ness**: the session has no host seat, so nothing bridges to your
  desktop. Verified: injecting into the session left the real cursor coordinates unchanged.

The client is `src/eidriver.c` (~200 lines, compiled against `libei-1.0`). It does the
handshake — connect → `EI_EVENT_SEAT_ADDED` (bind pointer/abs-pointer/button/keyboard caps) →
`EI_EVENT_DEVICE_ADDED` → `EI_EVENT_DEVICE_RESUMED` — then `ei_device_start_emulating` and emits
events, each followed by `ei_device_frame`. Keyboard uses **evdev** keycodes (e.g. `KEY_ESC=1`).

## Launching Skyrim inside it

We run `skse64_loader.exe` directly through `proton run` inside the headless gamescope (no Steam
launch). Two requirements that aren't obvious:
- **`SteamAppId=489830`** (and `SteamGameId`) must be set, or `SkyrimSE.exe` can't initialize
  `steam_api` outside Steam's launch wrapper and exits within seconds.
- **`STEAM_COMPAT_DATA_PATH`** / **`STEAM_COMPAT_CLIENT_INSTALL_PATH`** point Proton at the live
  prefix so saves/mods/`Plugins.txt` are the real ones.

This machine's launcher is already swapped to SKSE (`SkyrimSELauncher.exe` == `skse64_loader.exe`),
so the normal Steam launch path also runs SKSE; we bypass it to control the environment.

## Eyes + hands together (the loop)

```
launch.sh            # gamescope --backend headless -- proton run skse64_loader.exe
  └─ wait ~1-2 min for SkyrimSE.exe + the main-menu window on :N
shot.sh out.png      # SIGUSR2 -> AVIF -> PNG; agent reads the actual menu
drive.sh seq ...     # keyboard events via libei -> gamescope -> game
shot.sh out2.png     # confirm the state changed
```

## Why not the alternatives

- **Nested gamescope (window on your desktop)** — works for render+screenshot, but shares your
  seat, so injected input moves your real cursor. Killed the non-interference goal. Headless fixes
  it by removing the seat entirely.
- **A second headless Hyprland/sway compositor** — Hyprland now uses the *aquamarine* backend,
  which won't init headless without the DRM master your live session holds (it crashes). gamescope
  owns the headless-render problem more cleanly anyway.
- **uinput (`ydotool`/`dotool`)** — relative input, but a *global* kernel device → leaks to your
  seat, and a headless gamescope doesn't read libinput devices regardless.

## Future: SKSE ground-truth

Screenshots are pixels — the agent reads state visually. The robust complement is an in-process
**SKSE C++ plugin** (this repo's tier 2) that reports real state (`UI::IsMenuOpen("MapMenu")`,
player position, the menu stack) over a side-channel and can drive menus via engine calls — no
input devices, no pixel-reading, fully deterministic. gamescope stays the eyes; SKSE becomes the
authoritative hands. That's the endgame this dir is built to grow into.
