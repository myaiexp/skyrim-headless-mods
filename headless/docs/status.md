# Status & next steps

_As of the session that built this dir (2026-06-09)._

## What works

| Capability | State | Evidence |
|---|---|---|
| Invisible headless render | ✅ | Skyrim main menu rendered at 1280×720 in `gamescope --backend headless`; `0` game windows on the real desktop. |
| GPU acceleration | ✅ | DXVK/Vulkan via DRM render node; menu renders normally. |
| Screenshot | ✅ | `SIGUSR2` → `/tmp/gamescope_*.avif` → PNG; menu read pixel-for-pixel. |
| Isolation (no seat leak) | ✅ | XTEST into the headless inner `:1` left the real `:0` pointer unmoved. |
| **Keyboard input (libei)** | ✅ | `Esc` closed the Load submenu; `Down`×2 (via XTEST earlier) moved highlight CONTINUE→NEW→LOAD; `Enter` opened the Load screen. Deterministic. |
| **Mouse position / click** | ❌ | Neither absolute nor relative+click lands in Skyrim's menu — see below. |

## The open problem: pointer doesn't land

libei **keyboard** routes to the game, but **pointer** doesn't (findings #9). Channel is proven
(same socket, keyboard works), so it's specifically pointer routing/mapping. Leads to chase, roughly
in order of promise:

1. **Absolute region scaling.** gamescope reports an unbounded abs region (`~2^31`). Figure out how
   gamescope maps libei absolute coords to the 1280×720 output — likely a fixed scale (try
   `abs = px/W * 2^31`, `py/H * 2^31`), or query the region more carefully. If abs maps correctly,
   clicking known pixels becomes trivial (no relative/sensitivity mess).
2. **Pointer device caps.** Confirm the bound device actually advertises pointer/button caps after
   `RESUMED` (`ei_device_has_capability`); maybe only keyboard cap was granted. If so, bind/await the
   pointer device explicitly before emulating.
3. **start_emulating timing / frames.** Verify pointer motion+button are inside a started-emulating
   window and each `ei_device_frame`'d; try a button press *with* a small motion in the same frame.
4. **Does Skyrim need the pointer "entered"/shown?** The menu may ignore motion until the cursor is
   considered active. Try a continuous motion stream, or check `controlmap`/mouse settings.
5. **Fallback: SKSE.** If pointer stays unreliable, drive clicks in-process (engine call to the menu
   widget) instead of faking a cursor — see below.

## Next steps (beyond the pointer)

- **Drive the real goal with keyboard:** `seq down down enter` → Load → pick a save → in-game →
  `M` for map → dismiss the confirmation box (Enter/Esc). That validates the whole loop end-to-end
  and likely solves the `OneClickMap` confirmation-box chore without needing the mouse at all.
- **Polish the CLI:** a `load <save-substring>` helper; a `wait-for-menu`/`wait-in-game` that polls
  for the window on `:N` (the session used throwaway `waitwin*.py` pollers — fold the good one in).
- **SKSE ground-truth tie-in (the endgame):** an in-process plugin (this repo's tier 2) that reports
  real state (`UI::IsMenuOpen(...)`, player pos, menu stack) over a side-channel and can activate
  menu items via engine calls. gamescope stays the eyes; SKSE becomes deterministic hands + truth.
  This removes pixel-reading and the entire OS-input problem for anything the plugin can reach.

## Environment notes (this machine)

- Skyrim SE prefix is now **Proton Experimental (11.0-100)** — migrated from GE-Proton10-34 this
  session (findings #6); the user is fine with this.
- Launcher is swapped to SKSE (`SkyrimSELauncher.exe` == `skse64_loader.exe`).
- `sudo` is password-locked; stay user-space (findings #7).
- libei socket: `$XDG_RUNTIME_DIR/gamescope-0-ei`. Inner Xwayland: `:1` (or next free).
- Skyrim takes ~1-2 min from launch to main menu — don't mistake the slow boot for a failure.
