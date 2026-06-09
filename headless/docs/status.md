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
| **Mouse — relative move + click** | ✅ | `rel` deltas moved the cursor onto `LOAD`, hover highlighted it, `click` opened the Load screen (save list). Full chain works. |
| **Mouse — absolute click `(x,y)`** | ✅ | `drive.sh click 1195 556` opened the Load screen blind; `drive.sh abs x y` positions. Built on relative (corner-home + delta), mapped exactly 1:1. |
| **Raw libei absolute (`abs`/`pointer_motion_absolute`)** | ❌ by design | Inert — Skyrim ignores it (raw-mouse mode). Don't use; use the synthesized `moveto`/`clickat` instead. |

## Pointer: resolved (findings #9)

The pointer channel works — via **relative** motion, not absolute.

- **Absolute is a dead end** for Skyrim and there is **no scaling factor to find** (the old theory was
  wrong): gamescope passes abs coords through verbatim, but only as an *absolute* Wayland event, which
  raw-mode Skyrim discards. Proven: warping onto a menu item changes nothing.
- **Relative works end-to-end**: move → hover-highlight → click-activate, all reproduced in the menu.
- The cursor is the **arrow sprite, always on screen** (no idle-fade); it just parks off the
  bottom-right edge when positive `rel` deltas pile up. Cursor state **persists between `eidriver`
  invocations**.

### Absolute `click <x> <y>` — DONE (open-loop, exact)

Shipped. Sensitivity measured **exactly 1:1** (linear, isotropic, no acceleration), so open-loop is
exact — no visual servoing needed. Implemented in `eidriver.c` as `home`/`moveto`/`clickat`, exposed
via `drive.sh abs <x> <y>` (position) and `drive.sh click <x> <y>` (position + click). Mechanism:
clamp-to-corner for a known origin, then a relative delta = the target pixel. **Gotcha baked in:**
Skyrim caps a single oversized relative delta and silently desyncs the click target (findings #9b), so
every move is chunked into ≤1000-px steps. Verified: blind `click 1195 556` → Load screen.

Remaining pointer niceties (optional): a `click <save-substring>` that pairs OCR/template with the
click; drag support for sliders (button-down → `moveto` → button-up — the in-process position tracking
already supports it). Note: bottom-bar `Select`/`Back` prompts are keyboard/controller hints, **not**
mouse-clickable — use Tab/Esc for those.

For **menus**, keyboard navigates deterministically (arrows/Enter/Esc/Tab) — but the **world map is not
a menu**: testing `OneClickMap` means **clicking a discovered map marker** to fast-travel, which only
the mouse can do. That's the real reason the pointer path mattered. (The fast-travel confirmation box
OneClickMap removes is itself keyboard-dismissable, but that was never the blocker.)

### SKSE ground-truth (endgame, unchanged)

For anything an in-process plugin can reach, skip the cursor entirely and activate the menu widget via
engine call — gamescope stays the eyes, SKSE becomes deterministic hands + truth.

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
