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

## 9. RESOLVED — libei *relative* pointer drives Skyrim; *absolute* is inert by design

Settled empirically (2026-06-09, reproduced in the headless menu). Both halves are now proven:

**Absolute (`ei_device_pointer_motion_absolute`) does nothing — and the old "scaling" theory was wrong.**
gamescope's abs region is `INT32_MAX × INT32_MAX` (literally named `"Mr. Worldwide"`, offset 0) but
it is **decorative** — gamescope does *not* normalise against it. On `POINTER_MOTION_ABSOLUTE` it
passes the value **verbatim** to `wlserver_mousewarp(x,y)` (assign → clamp to focused surface), so
the correct abs value is just the raw output pixel. There was never a scaling factor to derive. The
real blocker: gamescope's absolute warp emits only an **absolute** Wayland pointer event, *never*
`relative-pointer-v1`. Skyrim runs **raw/relative mouse even in menus**, so it ignores absolute
motion entirely. Proof: warping onto `LOAD` (`abs 1200 554`) left the highlight on `CONTINUE` —
zero effect. (Source: gamescope 3.16.23 `InputEmulation.cpp` / `wlserver.cpp`.)

**Relative (`ei_device_pointer_motion`) works end-to-end.** `wlserver_mousemotion` emits
`relative-pointer-v1` unconditionally, which raw-mode Skyrim consumes. Proven chain in the main menu:
`rel` deltas moved the cursor up from its park spot → hover **highlighted** CREATIONS then LOAD →
`click` **opened the Load screen** (save list rendered). The earlier "relative did nothing" was a
misread: the cursor had moved but parked **off the bottom-right edge** (positive deltas accumulate and
clamp there), so it looked like no cursor at all.

**Two corrections to older notes:**
- The menu cursor is the **arrow sprite** and is *always present* — it does **not** idle-fade
  (supersedes finding #8's "hidden cursor"). It only *looks* absent when relative deltas have parked
  it off the bottom-right edge; nudge `rel` up-left and it reappears.
- **Cursor position persists across separate `eidriver` invocations** — gamescope keeps cursor state
  between libei sessions (`stop_emulating` doesn't reset it). So `rel`-position in one call then
  `click` in the next lands correctly; you don't have to do move+click in one process.

→ **Consequence for the API:** "click at pixel (x,y)" cannot ride raw libei *absolute*, but it rides
**relative** trivially. Sensitivity was **measured exactly 1:1** — `rel D` moves the cursor `D`
pixels, linear, isotropic, no acceleration (swept 200/400/600 on both axes, landed dead-on; my earlier
"~0.6–0.9, non-uniform" guess was eyeball noise off scaled screenshots). So **open-loop is exact** and
closed-loop servoing is unnecessary overhead here. Absolute positioning = clamp-to-corner (known
origin (0,0)) + one relative delta. **Implemented** as `moveto`/`clickat` in `eidriver.c`, exposed as
`drive.sh abs <x> <y>` and `drive.sh click <x> <y>`. Verified end-to-end: blind `click 1195 556`
opened the Load screen. Keyboard remains the deterministic path for anything menus expose to it.

### 9b. Skyrim caps a single oversized relative delta — move in modest steps

Hit while implementing the above. `moveto` first homed with one giant `rel(-8192,-8192)` to slam the
cursor to (0,0). The *visible/hover* cursor went to the right place (the target item highlighted), but
**clicks silently missed** — opened nothing. A *modest* home (`rel(-1400,-800)`, or repeated
`rel(-1500,-1000)`) clicked correctly every time. So Skyrim's raw-input integration **caps/rejects a
single relative delta above ~1400-ish**: gamescope's compositor cursor (which drives the highlight)
still moves 1:1 and clamps fine, but Skyrim's *internal* cursor — the click hit-test point — under-moves
and desyncs from it. → Fix: never send one big delta. `eidriver` now chunks every move into
≤1000-px steps with a short pump between (`rel_step`), so home-overshoot and long `moveto`s stay in the
safe regime. (This also means a future >~1400-wide resolution won't silently re-break clicks.)

## 10. The world map pans at screen edges — don't home/corner there

Hit while testing the OneClickMap loop (load → `M` → click a discovered marker → fast-travel). On the
**map**, a cursor at/near a screen edge **pans the map view** (it's a click-drag-able pannable surface,
not a fixed menu). So the absolute primitive's clamp-to-corner home (`abs`/`clickat`, which slam to
(0,0)) is **destructive on the map**: it pans the view, so the target pixel no longer points at the
marker, and the click lands on bare terrain → Skyrim places a *"Custom Destination"* flag instead of
selecting the location. (Menus are unaffected — they don't pan.)

→ **Map interaction recipe (verified, traveled to Dustman's Cairn):**
1. Position with **bare relative nudges** (`drive.sh rel dx dy`) — *no* home, and keep clear of the
   screen edges so the view doesn't pan. The cursor's on-screen position persists between invocations,
   so nudge → screenshot → nudge again works.
2. **Verify with the marker's name tooltip.** Hovering a discovered marker pops its name + status
   (e.g. "Dustman's Cairn / Cleared"); discovered = white/grey glyph, undiscovered = black. The tooltip
   showing means the cursor is within the marker's snap tolerance.
3. **Click with a bare `drive.sh click`** (no x,y → no home → no pan). With the snap active, the click
   selects that marker → fast-travel confirm box → `tap enter` (Yes).

For the automated OneClickMap test, the durable version is template-matching the white marker glyphs to
get their pixels (markers are ~12px; eyeballing won't scale), then this same no-home nudge+click. A
`moveto`-on-map variant would need a non-edge origin (e.g. re-open the map to recenter the cursor on
the player) instead of the corner home.

## 11. Process detection by cmdline grep is a trap — use a pidfile + the probe

Three separate ways the "is it up / is it ready?" question bit us, and the fixes now baked in:

1. **`pgrep -f "gamescope --backend headless"` self-matches.** `-f` matches the *whole cmdline* of
   *every* process — including an editor on `launch.sh`, or a shell command you typed that contained
   that phrase. The launch guard fired on a phantom "already running". The bracket trick doesn't save
   you (a plain phrase still matches). → **Fix:** `launch.sh` writes the real gamescope pid to
   `/tmp/headless-skyrim.pid` and the guard checks `kill -0 <pid>` + `/proc/<pid>/cmdline` is
   gamescope. `shot.sh` reads that pid for SIGUSR2 (no pgrep); `stop.sh` removes it.

2. **`$!` after `setsid … &` is a corpse, and `pgrep -x gamescope` finds nothing.** `setsid` can't
   `setsid()` as a process-group leader, so it **forks** the real process and exits — `$!` is the
   wrapper, already dead, useless for a pidfile. And gamescope's process name isn't exactly
   `gamescope`, so `-x` whiffs (we used to match `-W 1280 -H 720` instead). → **Fix:** launch via an
   inner `setsid bash -c 'echo $$ > pidfile; exec gamescope …'`. The pid is recorded *before* the
   `exec`, and `exec` means gamescope **inherits** it — so the pidfile holds the true compositor pid.

3. **"In-world" has no process signal — and the main menu looks ready.** `SkyrimSE.exe` spawns
   minutes into the Proton boot, so an early `pgrep SkyrimSE.exe` false-negatives mid-load. Worse,
   the main menu *looks* ready (main thread free → `status` acks there) and `parentCell`/`gameActive`
   both flip true mid-load, too early. The only reliable "fully interactive" signal is the player's
   **`Is3DLoaded()`**. → **Fix:** `SkytestProbe`'s `status` now reports a `world` block
   (`inWorld`/`is3DLoaded`/`mainMenu`/`loadingMenu`) — `inWorld` is the exact gate the probe's `exec`
   uses, so the two can't disagree. `./ready.sh` polls it and blocks until `inWorld:true`. Don't gate
   readiness on any pgrep.

## 12. Steam blocks a 2nd game instance — so stop.sh tears down by session, not pattern

Tested directly: with a normal Steam-launched Skyrim already running, `./launch.sh` brings up its
gamescope + Proton scaffolding (`python3` + `steam.exe`) but **the game never spawns** — no
`SkyrimSE.exe` under the headless session, no swapchain in the log. Proton's `steam.exe` wrapper
won't start a second copy of an appid Steam already considers running; the first game is unaffected.

Consequence for teardown: the usual thing you clean up is a *blocked* headless launch (gamescope +
proton idling) sitting next to your real game — where the **only** `SkyrimSE.exe` is your game. So
the old `stop.sh` (`pkill -9 -f SkyrimSE.exe` + `pkill -9 wineserver`) would have killed **your
game** and its wineserver, not the headless scaffolding. → **Fix:** `stop.sh` kills everything in
the gamescope **session** (via the pidfile's session-leader pid), taking down the headless tree but
leaving a game you're playing in the same prefix alone; broad pattern-kill is only the no-pidfile
fallback. Verified live: blocked launch alongside a running game → `./stop.sh` removed the headless
session and left the game running, exit 0. (Its liveness re-check tests the exact pid, not a cmdline
grep — else it self-matches per #11, which it did on the first cut.)
