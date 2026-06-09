# Findings — dead-ends and the reasons (read before changing the approach)

Every wall hit while building this, and *why* it's a wall. The point is to not re-pay for these.

## 1. XTEST into a nested gamescope leaks to your real cursor

First attempt: run gamescope as a normal **nested window** on the live Hyprland, inject input
with XTEST into its inner Xwayland (`:1`). Input reached the game **but moved the real desktop
cursor** — confirmed by the user watching it drift to the bottom-right while they were elsewhere.
Reason: a nested gamescope shares the host **seat**; its pointer is bridged to your real one.
→ Fix: run gamescope **headless** (no host seat). Then measured before/after: XTEST into the
headless inner `:1` left the real `:0` pointer coordinates **identical** = isolated.

## 2. XTEST absolute positioning can't drive Skyrim's cursor

Even isolated, XTEST only sends **absolute** X pointer events. Skyrim uses **relative/raw** mouse
mode, so Wine converts each absolute warp into a *delta from the previous position*. The cursor's
true position is decoupled and unknown; large warps accumulate and clamp in a corner. Origin-reset
tricks + sensitivity guessing didn't make it reliable. → This is *why* we abandoned XTEST for
input and moved to **libei** (relative-native). XTEST is fine for nothing here.

## 3. A second headless Hyprland/sway compositor — Hyprland crashes headless

Tried standing up a second compositor (own seat, own socket) to host the game. `grim` + `wtype`
worked against it — **but it wasn't actually headless**: launched with `WAYLAND_DISPLAY` in the
env, Hyprland created a *nested window* (monitor `WAYLAND-1`) on the real desktop, and the user's
keyboard reached it by focus. Scrubbing the env and forcing headless **crashed**: `CBackend::create()
failed`. Reason: modern Hyprland uses the **aquamarine** backend (not wlroots), so `WLR_BACKENDS=headless`
is ignored, and aquamarine's headless backend won't initialize without the DRM master that the live
Hyprland already holds. `AQ_FORCE_BACKEND=headless` didn't help. → gamescope `--backend headless`
owns this cleanly; don't fight Hyprland for it.

## 4. Headless gamescope gives no Wayland socket to clients (only Xwayland)

A test with `foot` (Wayland-native) failed: `failed to connect to wayland; no compositor running?`.
A headless gamescope hands its child `DISPLAY=:N` (Xwayland) but **no `WAYLAND_DISPLAY`**. Not a
problem — Skyrim/Proton is an X11 client — but it means you can't smoke-test the session with a
Wayland app. Use an X client or just Skyrim.

## 5. `launch-skse.sh` (bare `proton run`) exits instantly — needs `SteamAppId`

The game's existing `launch-skse.sh` does a bare `proton run skse64_loader.exe`. Inside gamescope
it bootstrapped Proton (`explorer.exe`, `xalia.exe`, DXVK) then **exited with no crash log** —
`SkyrimSE.exe` came up and died within seconds. Reason: launched outside Steam's app wrapper and
with no `steam_appid.txt`, `steam_api` can't identify the app and the game bails. → Fix: export
**`SteamAppId=489830`** (+ `SteamGameId`) in the launch env. After that it boots to the menu.

## 6. `launch-skse.sh` forces Proton Experimental → migrated the live prefix

That script hardcodes **Proton - Experimental**, but the prefix had last run under **GE-Proton10-34**
(what Steam uses). Running it migrated the shared prefix `GE-Proton10-34 → 11.0-100` and logged
"Prefix has an invalid version?!". The user doesn't care which Proton (GE was only a lag experiment),
so we left it on Experimental/11 and the launcher here uses Experimental to match. If Steam later
complains, that's the cause. **The launcher path Skyrim actually uses is the swapped launcher**
(`SkyrimSELauncher.exe` is byte-identical to `skse64_loader.exe`), launched by Steam — `launch-skse.sh`
was a separate, divergent path.

## 7. `sudo` is password-locked in this environment

No passwordless sudo, no askpass — `xdotool`/`ydotool` wouldn't install. → Everything is user-space:
`python-xlib`/`pywayland` via `pip --user`, and the libei client **compiled** against the already-present
`libei` libs (no `-dev` package needed beyond the bundled header). Don't plan around installing packages.

## 8. Skyrim hides the menu cursor — no visual cursor feedback

The main-menu mouse cursor is hidden in most states, so you can't *see* where the pointer is to
calibrate. Frame-diffing to locate it fails too: the main menu has an **animated fog/smoke**
background, so two frames differ across a huge area, not just the cursor. → Use **semantic** signals
(does a menu item highlight / a submenu open / a screen change), not cursor-spotting.

## 9. libei keyboard routes to the game; libei pointer does not (yet)

Via `gamescope-0-ei`: the handshake succeeds, `Esc` (evdev `1`) **closed the Load submenu** — so
libei keyboard genuinely reaches Skyrim and evdev keycodes are correct. But pointer didn't land:
- Absolute (`ei_device_pointer_motion_absolute`) — gamescope advertises an **unbounded** region
  (`~2^31 × 2^31`), so pixel coords map to a near-zero fraction of a 2-billion-unit plane; abs
  coords don't correspond to screen pixels without a scaling factor we haven't derived.
- Relative (`ei_device_pointer_motion`) + click — reset-to-corner then move, then click on
  CONTINUE: **no state change**. Keyboard through the same channel works, so the channel is fine;
  pointer events just aren't effectively reaching Skyrim's menu.
→ Open problem (see status.md). Keyboard is the working control method for menus; the original
world-map confirmation box is almost certainly keyboard-dismissable (Enter/Tab/Esc).
