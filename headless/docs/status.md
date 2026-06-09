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
| **Mouse — absolute positioning** | ❌ by design | `abs` is inert: Skyrim ignores absolute pointer motion (raw-mouse mode). Not fixable at the libei level — see below. |

## Pointer: resolved (findings #9)

The pointer channel works — via **relative** motion, not absolute.

- **Absolute is a dead end** for Skyrim and there is **no scaling factor to find** (the old theory was
  wrong): gamescope passes abs coords through verbatim, but only as an *absolute* Wayland event, which
  raw-mode Skyrim discards. Proven: warping onto a menu item changes nothing.
- **Relative works end-to-end**: move → hover-highlight → click-activate, all reproduced in the menu.
- The cursor is the **arrow sprite, always on screen** (no idle-fade); it just parks off the
  bottom-right edge when positive `rel` deltas pile up. Cursor state **persists between `eidriver`
  invocations**.

### Next step — absolute-coordinate ergonomics on top of relative

To get a `click <px> <py>` API (what we actually want), build it on relative motion:

1. **Closed-loop visual servoing (recommended).** Screenshot → locate the arrow sprite (bright,
   consistent shape on dark bg — template match) → compute pixel delta to target → send `rel` scaled
   by measured sensitivity (~0.6–0.9 px/unit) → re-shot and converge (1–3 iterations) → `click`.
   Self-correcting, immune to the non-uniform sensitivity that sank open-loop before.
2. **Open-loop corner-reset (simpler, driftier).** Slam a large `rel` to a corner (deterministic
   clamp = known origin), then move by `(target − corner) / sensitivity`. One-time calibration; drifts.
3. **SKSE ground-truth (endgame).** For anything an in-process plugin can reach, skip the cursor
   entirely and activate the menu widget via engine call — see below.

Note: for **menus**, keyboard already navigates deterministically (arrows/Enter/Esc/Tab), so pointer
precision is only needed where keyboard can't reach. The motivating `OneClickMap` confirmation box is
very likely keyboard-dismissable.

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
