# Findings — dead-ends and the reasons (read before changing the approach)

Every wall hit while building this, and _why_ it's a wall. The point is to not re-pay for these.

## 1. XTEST into a nested gamescope leaks to your real cursor

First attempt: run gamescope as a normal **nested window** on the live Hyprland, inject input
with XTEST into its inner Xwayland (`:1`). Input reached the game **but moved the real desktop
cursor** — confirmed by the user watching it drift to the bottom-right while they were elsewhere.
Reason: a nested gamescope shares the host **seat**; its pointer is bridged to your real one.
→ Fix: run gamescope **headless** (no host seat). Then measured before/after: XTEST into the
headless inner `:1` left the real `:0` pointer coordinates **identical** = isolated.

## 2. XTEST absolute positioning can't drive Skyrim's cursor

Even isolated, XTEST only sends **absolute** X pointer events. Skyrim uses **relative/raw** mouse
mode, so Wine converts each absolute warp into a _delta from the previous position_. The cursor's
true position is decoupled and unknown; large warps accumulate and clamp in a corner. Origin-reset
tricks + sensitivity guessing didn't make it reliable. → This is _why_ we abandoned XTEST for
input and moved to **libei** (relative-native). XTEST is fine for nothing here.

## 3. A second headless Hyprland/sway compositor — Hyprland crashes headless

Tried standing up a second compositor (own seat, own socket) to host the game. `grim` + `wtype`
worked against it — **but it wasn't actually headless**: launched with `WAYLAND_DISPLAY` in the
env, Hyprland created a _nested window_ (monitor `WAYLAND-1`) on the real desktop, and the user's
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

The main-menu mouse cursor is hidden in most states, so you can't _see_ where the pointer is to
calibrate. Frame-diffing to locate it fails too: the main menu has an **animated fog/smoke**
background, so two frames differ across a huge area, not just the cursor. → Use **semantic** signals
(does a menu item highlight / a submenu open / a screen change), not cursor-spotting.

## 9. RESOLVED — libei _relative_ pointer drives Skyrim; _absolute_ is inert by design

Settled empirically (2026-06-09, reproduced in the headless menu). Both halves are now proven:

**Absolute (`ei_device_pointer_motion_absolute`) does nothing — and the old "scaling" theory was wrong.**
gamescope's abs region is `INT32_MAX × INT32_MAX` (literally named `"Mr. Worldwide"`, offset 0) but
it is **decorative** — gamescope does _not_ normalise against it. On `POINTER_MOTION_ABSOLUTE` it
passes the value **verbatim** to `wlserver_mousewarp(x,y)` (assign → clamp to focused surface), so
the correct abs value is just the raw output pixel. There was never a scaling factor to derive. The
real blocker: gamescope's absolute warp emits only an **absolute** Wayland pointer event, _never_
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

- The menu cursor is the **arrow sprite** and is _always present_ — it does **not** idle-fade
  (supersedes finding #8's "hidden cursor"). It only _looks_ absent when relative deltas have parked
  it off the bottom-right edge; nudge `rel` up-left and it reappears.
- **Cursor position persists across separate `eidriver` invocations** — gamescope keeps cursor state
  between libei sessions (`stop_emulating` doesn't reset it). So `rel`-position in one call then
  `click` in the next lands correctly; you don't have to do move+click in one process.

→ **Consequence for the API:** "click at pixel (x,y)" cannot ride raw libei _absolute_, but it rides
**relative** trivially. Sensitivity was **measured exactly 1:1** — `rel D` moves the cursor `D`
pixels, linear, isotropic, no acceleration (swept 200/400/600 on both axes, landed dead-on; my earlier
"~0.6–0.9, non-uniform" guess was eyeball noise off scaled screenshots). So **open-loop is exact** and
closed-loop servoing is unnecessary overhead here. Absolute positioning = clamp-to-corner (known
origin (0,0)) + one relative delta. **Implemented** as `moveto`/`clickat` in `eidriver.c`, exposed as
`drive.sh abs <x> <y>` and `drive.sh click <x> <y>`. Verified end-to-end: blind `click 1195 556`
opened the Load screen. Keyboard remains the deterministic path for anything menus expose to it.

### 9b. Skyrim caps a single oversized relative delta — move in modest steps

Hit while implementing the above. `moveto` first homed with one giant `rel(-8192,-8192)` to slam the
cursor to (0,0). The _visible/hover_ cursor went to the right place (the target item highlighted), but
**clicks silently missed** — opened nothing. A _modest_ home (`rel(-1400,-800)`, or repeated
`rel(-1500,-1000)`) clicked correctly every time. So Skyrim's raw-input integration **caps/rejects a
single relative delta above ~1400-ish**: gamescope's compositor cursor (which drives the highlight)
still moves 1:1 and clamps fine, but Skyrim's _internal_ cursor — the click hit-test point — under-moves
and desyncs from it. → Fix: never send one big delta. `eidriver` now chunks every move into
≤1000-px steps with a short pump between (`rel_step`), so home-overshoot and long `moveto`s stay in the
safe regime. (This also means a future >~1400-wide resolution won't silently re-break clicks.)

## 10. The world map pans at screen edges — don't home/corner there

Hit while testing the OneClickTravel loop (load → `M` → click a discovered marker → fast-travel). On the
**map**, a cursor at/near a screen edge **pans the map view** (it's a click-drag-able pannable surface,
not a fixed menu). So the absolute primitive's clamp-to-corner home (`abs`/`clickat`, which slam to
(0,0)) is **destructive on the map**: it pans the view, so the target pixel no longer points at the
marker, and the click lands on bare terrain → Skyrim places a _"Custom Destination"_ flag instead of
selecting the location. (Menus are unaffected — they don't pan.)

→ **Map interaction recipe (verified, traveled to Dustman's Cairn):**

1. Position with **bare relative nudges** (`drive.sh rel dx dy`) — _no_ home, and keep clear of the
   screen edges so the view doesn't pan. The cursor's on-screen position persists between invocations,
   so nudge → screenshot → nudge again works.
2. **Verify with the marker's name tooltip.** Hovering a discovered marker pops its name + status
   (e.g. "Dustman's Cairn / Cleared"); discovered = white/grey glyph, undiscovered = black. The tooltip
   showing means the cursor is within the marker's snap tolerance.
3. **Click with a bare `drive.sh click`** (no x,y → no home → no pan). With the snap active, the click
   selects that marker → fast-travel confirm box → `tap enter` (Yes).

For the automated OneClickTravel test, the durable version is template-matching the white marker glyphs to
get their pixels (markers are ~12px; eyeballing won't scale), then this same no-home nudge+click. A
`moveto`-on-map variant would need a non-edge origin (e.g. re-open the map to recenter the cursor on
the player) instead of the corner home.

## 11. Process detection by cmdline grep is a trap — use a pidfile + the probe

Three separate ways the "is it up / is it ready?" question bit us, and the fixes now baked in:

1. **`pgrep -f "gamescope --backend headless"` self-matches.** `-f` matches the _whole cmdline_ of
   _every_ process — including an editor on `launch.sh`, or a shell command you typed that contained
   that phrase. The launch guard fired on a phantom "already running". The bracket trick doesn't save
   you (a plain phrase still matches). → **Fix:** `launch.sh` writes the real gamescope pid to
   `/tmp/headless-skyrim.pid` and the guard checks `kill -0 <pid>` + `/proc/<pid>/cmdline` is
   gamescope. `shot.sh` reads that pid for SIGUSR2 (no pgrep); `stop.sh` removes it.

2. **`$!` after `setsid … &` is a corpse, and `pgrep -x gamescope` finds nothing.** `setsid` can't
   `setsid()` as a process-group leader, so it **forks** the real process and exits — `$!` is the
   wrapper, already dead, useless for a pidfile. And gamescope's process name isn't exactly
   `gamescope`, so `-x` whiffs (we used to match `-W 1280 -H 720` instead). → **Fix:** launch via an
   inner `setsid bash -c 'echo $$ > pidfile; exec gamescope …'`. The pid is recorded _before_ the
   `exec`, and `exec` means gamescope **inherits** it — so the pidfile holds the true compositor pid.

3. **"In-world" has no process signal — and the main menu looks ready.** `SkyrimSE.exe` spawns
   minutes into the Proton boot, so an early `pgrep SkyrimSE.exe` false-negatives mid-load. Worse,
   the main menu _looks_ ready (main thread free → `status` acks there) and `parentCell`/`gameActive`
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

Consequence for teardown: the usual thing you clean up is a _blocked_ headless launch (gamescope +
proton idling) sitting next to your real game — where the **only** `SkyrimSE.exe` is your game. So
the old `stop.sh` (`pkill -9 -f SkyrimSE.exe` + `pkill -9 wineserver`) would have killed **your
game** and its wineserver, not the headless scaffolding. → **Fix:** `stop.sh` kills everything in
the gamescope **session** (via the pidfile's session-leader pid), taking down the headless tree but
leaving a game you're playing in the same prefix alone; broad pattern-kill is only the no-pidfile
fallback. Verified live: blocked launch alongside a running game → `./stop.sh` removed the headless
session and left the game running, exit 0. (Its liveness re-check tests the exact pid, not a cmdline
grep — else it self-matches per #11, which it did on the first cut.)

## 13. A black `shot` means the game never spawned in the session — a Skyrim was ALREADY running

Surfaced **and misdiagnosed then corrected** 2026-06-12 while functionally verifying the skytest
merge. First read: "`--backend headless` doesn't composite the game surface" — `skytest test <mod>
--headless` → `ready` reported `inWorld:true`, `drive` connected to `gamescope-0-ei`, yet every
`shot` returned the _identical_ ~1215-byte black AVIF (mean ≈8/255), across in-world, the Esc menu,
_and_ the map. **That conclusion was wrong.** The real cause was finding #12 + a shared-channel trap:

- **A real Skyrim (the user's) was already running** the whole time. Steam blocks a 2nd instance of
  the appid (#12), so the test session's gamescope came up but **the game never spawned inside it** —
  the gamescope log shows Vulkan/Xwayland/libei init then _nothing_: no DXVK, no swapchain, one empty
  `xwm: got the same buffer committed twice`, then only screenshots + `gamescope_ei: Unhandled libei
event!`. An empty compositor → a black frame. Nothing wrong with the SIGUSR2 path.
- **`ready` was a false positive.** The probe channel (`commands.jsonl`/`trace.jsonl`) lives in the
  prefix's SKSE log dir, which is **shared across every game in the prefix** — not per session. So the
  _other_ running game (probe-bearing) answered the `status` pings, and `gs_wait_ready` (which reads
  `grep '"src":"status"' | tail -1`, not matched to its own command id) reported in-world about the
  **wrong game**. `drive`'s libei events went to the empty session (`Unhandled`), not that game.

**Root gap (now fixed):** `game_running()` used to comm-match (`pgrep -i skyrimse`), but a wine/proton
game's comm is often `Main`, so it **didn't see the already-running game** and `cmd_test` proceeded
into the doomed launch. It now cmdline-matches `pgrep -f 'SkyrimSE\.exe'` (self/parent excluded per
#11; the launcher uses `skse64_loader.exe`, so it's matched only once the game truly spawns), and
`test`/`play`/`setup-save`/`uninstall`/`init` refuse up front: _"Skyrim is already running (pid N) —
close it first."_ That removes the whole class of "why is my test black?" confusion.

**Clean re-test (2026-06-12, no other game running):**

- ✅ **`shot` works headless.** With nothing blocking, the game spawned and `shot` captured a clear,
  readable **main menu** — 675 KB AVIF, mean ~2633 (sRGB), vs the 1215-byte mean-8 black frame when
  blocked. So `--backend headless` composites and captures fine; the earlier black was 100% the
  appid-block. `ready` also behaved honestly (`booting → main-menu`, no false in-world).
- ⚠️ **`drive` (keyboard) did NOT move the menu.** `eidriver` connected and sent, but the main-menu
  "missing content" modal didn't respond to `tab`/`esc`/`enter` (probe stayed `mainMenu:true`), and
  `gamescope_ei: Unhandled libei event!` logged on each send — on the same gamescope 3.16.23+ where
  finding #9 proved keyboard nav worked. **Not a merge regression** (eidriver is byte-identical, only
  repointed). Unverified whether it's this specific modal, keyboard focus in the session, or a
  libei/gamescope drift since #9 — needs a clean **in-world** scene to test movement/console, which
  the item below currently blocks. Left OPEN.
- ⚠️ **Autoload stuck at the menu — the SHARED Saves folder, not a bad base save.** First read blamed
  a "contaminated SkytestBase"; **wrong.** SkytestBase's master list is vanilla + 5 Creation Club
  plugins only (`ccBGSSSE001-Fish` / `ccBGSSSE025-AdvDSGS` / `ccQDRSSE001-SurvivalMode` /
  `ccBGSSSE037-Curios` / `_ResourcePack`), **all present in the test profile** (they're in
  `Skyrim.ccc`, so `collect_vanilla_plugins` carries them) — it loads clean. The on-screen "missing
  content" prompt lists _completely different_ mods (`LegacyoftheDragonborn`/`RaceMenu`/`XPMSE`/
  unofficial patch/…) that are **not in SkytestBase at all**. They're from the user's **main modded
  save**: the Saves folder lives in the prefix (`…/Documents/My Games/.../Saves`) and is **shared
  across every profile**, so a vanilla+1 test game's main menu auto-checks the _newest_ save (the main
  one) for the "Continue" button and pops that modal — which appears to block po3 StartOnSave from
  cleanly autoloading SkytestBase. Pre-existing skytest-v2 behavior, not the merge; the README already
  flags the shared-Saves-folder hazard for the "latest save" case. **RESOLVED:** per-test Saves
  isolation now ships — `isolate_saves` (`skytest:402`, called unconditionally on the test-boot path
  at `:607`) redirects `SLocalSavePath` to a `Saves_skytest` dir holding only SkytestBase, so the
  test menu has nothing else to auto-check and the "missing content" modal never fires. Torn down by
  `deisolate_saves` on stop/normal/play/uninstall; shown in `skytest status`.

(The `--backend wayland` `shot`/`drive` confirmation, and the in-world `drive` retest, were
superseded by finding #14 — keyboard `drive` is confirmed in-world — and the Saves isolation above
removed the autoload blocker that was gating a clean in-world run.)

## 14. RESOLVED (keyboard) — in-world driving worked via former full-profile wrapper; tab-clicks still desync

2026-06-14, while verifying AutoFireBow's SkyUI MCM. The earlier in-world blockers were both
profile artifacts, not engine/libei faults — sidestepped by a full-profile gamescope wrapper and a
log-based check. That wrapper was later removed from the public CLI; keep this section as historical
evidence about gamescope/libei behavior, not as current command guidance. (The drivable full-profile
capability was since reintroduced as **`skytest play agent [--headless]`** — `cmd_play`, `skytest:838`
— now _with_ SkytestProbe injected + IO reset, so `ready`/`drive`/`shot` apply.)

**Former full-profile gamescope wrapper.** `gs_launch` wraps whatever `Data` points at, so a drivable
full-modded session was `cmd_test` minus the vanilla+1 swap and minus injection. At the time it was
exposed as `skytest playtest [--headless]`: `full` stayed pristine (no SkytestProbe, so `ready`/`exec`
didn't apply — drive by `shot` + the in-game console + SKSE/Papyrus logs). It reached MCMs because
SkyUI was present in the full profile. It also dodged #13's autoload modal entirely: the full profile's
load order matched the real saves, so `CONTINUE` loaded with no "missing content" prompt.

**Keyboard input is confirmed in-world (supersedes #13's OPEN keyboard item).** End-to-end, headless,
all via `drive tap ...`: main menu `CONTINUE` → `Continue from your last saved game?` confirm → save
load → in-world (signalled by the mod's own `AutoFireBow.log` "loop registered on player") → `escape`
opened the Journal. Every keyboard tap registered. #13's "keyboard didn't dismiss the modal" was the
#13 _modal_ specifically (a no-content prompt that may swallow keys), not keyboard input in general.

**Mouse/cursor in menus is still imprecise (the #9b desync, unresolved).** Could not switch the
Journal's QUESTS/GENERAL-STATS/SYSTEM tabs to reach Mod Configuration: `drive click x y` (clickat)
landed ~80px right of target and bare `click` after manual `drive rel` homing missed too. Raw
`drive rel` desyncs Skyrim's internal hit-test cursor from gamescope's compositor cursor; only the
eidriver's chunked `clickat`/`moveto` (`rel_step`, <=1000px) is supposed to stay synced, and even it
overshot here. So **menu navigation that requires a precise click is not yet reliable**; keyboard is.
(Journal category tabs may also be controller-bumper-only, with no keyboard binding — untested.)

**The escape hatch: verify via the Papyrus log, not the UI.** Grep `.../Logs/Script/Papyrus.0.log`
for `Registered <ModName> at MCM` (SkyUI logs every page it registers) and confirm **no**
`<Native> is not a valid function` lines (a missing C++↔Papyrus native logs there on the
`OnGameReload` push). For AutoFireBow this gave the full result without driving the menu: quest
`INITIALIZED`, `Registered AutoFireBow at MCM`, zero native errors = MCM page + bridge both work.
Driving a precise in-menu click is the remaining open item for true visual MCM screenshots.

## 15. libei "hold" ≠ a real held mouse/key — they diverge for input-state-machine mechanics

2026-06-14, building AutoCastSpell's auto-recast loop. **The single most important headless-testing
caveat learned this session.** `drive raw btn 272 1 sleep N btn 272 0` holds the control via one
libei button-down for N ms. For _presence/held gates_ (is the attack control down?) this matches a
real hold fine. For anything keyed off the engine's **input state machine while held** — charge-
while-held, auto-repeat, post-action re-trigger — it does **NOT** match real hardware.

Concretely: after a fire-and-forget spell fires, a **real** held cast button makes the engine start
the next charge **on its own** (the same way a held bow re-draws); a **libei** continuous hold does
**not** auto-recharge. So the loop needed a synthetic re-press to cycle under libei, but cycled
_without_ it (off the engine's own re-charge) on Mase's real hardware — and a build tuned until the
libei test looped fired an unpredictable **2–7 times** for him. Removing unrelated debug logging
also shifted the (timing-sensitive) recharge — see the mod's `main.cpp` note and `../docs/ideas.md`.

Why: libei emits one button-down then holds the state; Skyrim's per-frame input poll and the
attack/cast handler's edge-vs-held bookkeeping treat that differently from a real device's continuous
stream, and the synthetic `ButtonEvent`s the mod injects interact differently with each.

**Takeaway:** libei is trustworthy for menu nav, single taps, and presence/held gates. For mechanics
driven by the charge/draw/cast state machine while held, the headless rig can confirm _the mechanism
exists_ but **not that it's reliable — validate timing/reliability on real hardware.** And drive such
loops off an engine **state** read (e.g. `RE::MagicCaster::state`), not off input timing.

## 16. `ready`/`inWorld` fires before the save is actually loaded — wait for a player-loaded signal

`skytest ready` (the probe's `status.world.inWorld`) returned in-world within ~5 s of launch, but the
StartOnSave autoload of `SkytestBase` didn't finish until ~50 s — the player's 3D wasn't loaded until
then. So `ready` returning is **not** "the player is in the world and interactive"; acting on it (set
up state, drive input, read actor state) can hit a half-loaded game.

Reliable "truly loaded" signals: the mod's own `...registered on player` / anim-sink-attach log line
(it fires on `kPostLoadGame` once 3D is up), or a probe `dump player` / `anim-trace player` that
actually resolves the player actor (the probe logs `actor 3D not loaded yet` until it does).
**Takeaway:** after `skytest test`, poll for an actual player-loaded signal before driving.

## 17. Headless test-iteration cheat-sheet (cast/input mods)

- **Stage game state with direct-call probe commands, not console `exec`.** Programmatic `exec`/
  CompileAndRun AVs (SEH-caught → "faulted"). **Cause pinned 2026-06-16 — see #18: it's a stale
  CommonLib binding on game 1.6.1170, NOT a headless/console-subsystem limit, and it would fault in
  a normal windowed game too.** Use SkytestProbe `give-spell` (add + equip to a hand) and `set-av`
  (e.g. magicka) — they call the engine directly. Every console verb is just an engine-call wrapper,
  so direct-call is the general staging path; add a probe helper per need.
- **Bump `REL::Version` every debug rebuild.** A new DLL loads only at SKSE startup, so testing a
  build = `skytest stop` + `skytest test` (relaunch). The log truncates on load, but if two builds
  share a version the load-line is identical and a readiness `grep` can't tell stale content from
  fresh (it false-passes on the old log before the new DLL loads). A distinct version (grep
  `1-0-7-0 loaded`) makes "is the new build up?" unambiguous.
- **Launch `skytest` from the repo root.** `cd mods/X && ./build.sh && skytest test mods/X/build/X.dll`
  fails — the `cd` left cwd in the mod dir so the relative mod path no longer resolves (and a relative
  path that _does_ resolve gets symlinked verbatim into the profile, where it dangles — now realpath'd,
  but launch from root regardless). Build, then `cd` back to root to launch.
- **A button _hold_ is one `drive raw` call.** `drive raw btn <code> 1 sleep <ms> btn <code> 0`
  (272 = LMB = right-hand cast, 273 = RMB = left-hand). Press, `sleep`, and release must be in the
  **same** invocation — the button-down state does not persist across eidriver process lifetimes.

## 18. PINNED — `exec` faults because CommonLib is older than the game (stale binding), not headless

The long-running "programmatic `exec`/CompileAndRun faults, cause unpinned and moot" item (#17,
old probe comments) was **wrong about the cause and wrong that it's headless-specific**. Pinned
2026-06-16 by adding fault-capture to SkytestProbe's SEH handler (it previously discarded the
exception — _that_ is why nobody could pin it) and reproducing live in a headless in-world session.

**Evidence (SkytestProbe.log, game 1.6.1170):** `exec FAULT: code=0xc0000005 ip_rva=0xce9843
access_addr=0xffffffffffffffff` — an access violation **inside SkyrimSE.exe** reading -1, identical
across `tgm` / `set timescale` / `player.additem`, and **identical with the console menu OPEN**.
`consolelog` was non-null throughout. So: not off-thread (exec runs main-thread via the task queue),
not a missing ConsoleLog/"console subsystem", and the "interactive console works → so it's the
programmatic path" reasoning is invalid — typing a command dispatches through the console **command
table**, while `Script::CompileAndRun` invokes the **script compiler**; different engine code.

**Root cause — a dependency version skew:**

- This CommonLibSSE-NG (`CharmedBaryon/CommonLibSSE-NG`, **v3.7.0-129, committed 2024-09-03**) is
  **older than the installed runtime, 1.6.1170**.
- It binds `Script::CompileAndRun` to Address Library **AE id 21890** (`Offset::Script::CompileAndRun
= RELOCATION_ID(21416, 21890)`).
- **AE id 21890 is absent from `versionlib-1-6-1170-0.bin`.** Verified by parsing the versionlib with
  a decoder ported from CommonLib's own `REL/ID.h`: of the 711 AE ids CommonLib binds, 705 (99.16%)
  are present; 21890 is in the absent set of 6 (also absent: `Console::SelectedRef` id 405935,
  `GetCachedString` 69188, `Set_CStr` 11044, an InventoryChanges fn 16118, a Scaleform fn 82317).
- CommonLib's id lookup (`REL/ID.h::id2offset`) only validates `it->id == a_id` **for VR builds**
  (the check is gated behind `Module::IsVR()`). On a non-VR AE build a **missing id silently resolves
  to the next id's address** — here 21891 — so CompileAndRun calls the **wrong function** → AV.

**Consequences:** exec would fault the same way in a **normal Steam-launched 1.6.1170 game** — the
old "runs for real in a windowed game" claim is almost certainly false on this version. The probe's
other commands are unaffected (none use the 6 absent ids; `give-spell`/`set-av`/`status`/`mcm-*` all
verified working in the same session).

**Decision: retire `exec`, don't fix it.** Staging is done with direct-call probe commands, which are
more reliable here anyway (no compiler dependency, main-thread, null-safe, structured acks). exec is
left in place but SEH-guarded (harmless — returns an error ack). Bumping the shared CommonLib to
chase one binding would risk regressing all six verified mods, so it's deliberately NOT done. The
fault-capture instrumentation stays in SkytestProbe as the permanent record. To actually restore
exec later: update CommonLibSSE-NG to a build that matches the runtime (or hardcode the correct
1.6.1170 CompileAndRun offset in the probe), then re-run the headless `test` + `exec` repro.

## 19. Former full-profile wrapper gotchas — readiness, autosave, boot-to-menu

These were learned 2026-06-18 chasing the DBVO dialogue mouth-snap through the former full-profile
gamescope wrapper. That wrapper has since been reintroduced as **`skytest play agent [--headless]`**
(`cmd_play`, `skytest:838`, now with SkytestProbe injected + IO reset); the gotchas below still
apply to it (the readiness one is even encoded — `play agent` does not poll `ready` on `full`). Keep
this as historical evidence for any future full-profile automation design.

- **`ready`/`gs_wait_ready` was unreliable on `full`** — it polled SkytestProbe's `status`, but on the
  full profile it never returned cleanly. Don't poll it; just **wait ~10 s** after the save loads, or
  watch `trace.jsonl` for `"src":"face"`/event lines to confirm you're live.
- **`full` booted to the MAIN MENU** (no StartOnSave there) — drive **E, E** (first E dismisses the
  Anniversary notice, second = Continue → newest save). There's a ~200 ms double-press guard on
  Skyrim dialogue, so rapid E taps aren't truly instant.
- **Autosave can clobber/teleport your test save.** An autosave fired mid-test, overwrote the prepared
  save, and the target NPC teleported. **Disable autosaves** (Settings → Gameplay) before driving, or
  reload + delete the bad autosave.

The removed wrapper had started refreshing SkytestProbe into `full` + resetting probe IO on launch.
If a full-profile automation path returns, keep that stale-DLL/stale-IO lesson.

## 20. A self-re-queuing SKSE task HARD-FREEZES the game — pace per-frame work from a thread

Learned 2026-06-18 building SkytestProbe's `facegen-ramp` (cost two sessions). To drive a ~150 ms
per-frame effect you need ~60 Hz, far faster than the 250 ms command-poll `MainTick`. The intuitive
move — a task that re-enqueues itself via `SKSE::GetTaskInterface()->AddTask` — **freezes the game on
the first run**: the SKSE runtime drains its task queue _to empty_ within one frame, so a task that
adds another task during that drain spins the frame forever. (DBVODialogueTweaks' own v5 comment notes
the same trap.) The trace signature is dead-giveaway: the arm line writes, then nothing, then frozen.

**Fix — the codebase's existing safe pattern:** an EXTERNAL pacer thread that `sleep`s, then enqueues
exactly ONE one-shot `AddTask` step that does NOT re-enqueue (the command poll thread paces `MainTick`
this way). SkytestProbe's ramp ticker sleeps 16 ms (active) / 150 ms (idle) and enqueues one step per
wake. One-shot `AddTask` (e.g. `CutNpcReply`) is always fine — only _self-re-queue_ spins.

## 21. AddTask-scheduled writes LOSE the per-frame race to the lip pump — hook the apply instead

Also 2026-06-18, same mouth-snap chase. The visible NPC mouth lives in `BSFaceGenAnimationData`'s
`transitionTargetKeyFrame` (offset 0x18). To ease it shut you must out-write the engine's audio-driven
lip pump, which rewrites that keyframe every frame. An `AddTask`-scheduled write **runs too early in the
frame** — the pump overwrites it afterward (probe trace: our `maxAfter` decays, but next frame's
`maxBefore` is pinned back at the pump's value). `SetSpeakingDone(true)` does NOT stop the pump while
audio plays; the snap is the pump _releasing_ the keyframe (0.5→0 in one frame) when the cut audio
finally stops (~200 ms after `FadeOutAndRelease`). `Reset(0.0)` is a red herring — it doesn't touch
`transitionTarget`. The only seam that wins is a **per-frame hook at the morph-APPLY point** (scale the
keyframe after the pump writes, before the mesh reads). **Open:** a vtable hook on
`BSFaceGenNiNode::UpdateDownwardPass` (idx 0x2C) overrides the keyframe cleanly but still snaps —
that's a _transform_ pass, not the morph apply. Finding the real apply/write seam (Ghidra) is the
open work — see `docs/plans/dbvo-mouth-snap-handoff.md`.

## 22. CONFIRMED: per-step screenshots work under `--headless` (real composited frames)

2026-06-21. Closes the long-uncertain "do headless shots actually composite" question from #13 (which
only corrected the _misdiagnosis_). A real `skytest replay <mod> <s>.steps --headless` run produced a
full per-step filmstrip: every in-world frame was a ~3 MB AVIF→PNG at mean-brightness **~0.16** (a real
frame; the empty/black frame from #13 is ~1.2 KB, mean ~3e-6). The one black frame is `00-start.png`,
snapped during boot _before_ the `until:inworld` gate — expected. The cast step's frame even spiked to
mean **0.65** (the Fireball lighting the room). So the per-step auto-shot filmstrip (`replay`, default
on, `<probe-io>/replay-shots/NN-verb.png`, `--no-shots` to disable) is fully headless-capable — no need
for the visible backend to get visual evidence.

## 23. A FOCUSED visible gamescope session forwards your REAL seat mouse — it corrupts probe-driven aim

2026-06-21, calibrating a GhostAllies cast test on the **visible** backend. libei _input_ is isolated to
the session, but the gamescope **window**, while focused, still forwards the host pointer to the game —
so moving your mouse (e.g. dragging the window to another workspace) rotates the player camera mid-test.
That silently wrecked two casts (the projectile follows the player's look direction): one cast went wide,
another "hit the floor", and the runs looked like nondeterministic engine behavior until the mouse was
ruled out. **Lesson:** for any aim/camera-sensitive driven test, run `--headless` (no window to grab the
pointer) — it's deterministic _and_ off your screen. If you must run visible, don't touch the window.

## 24. Probe-driven `CastSpellImmediate` has no crosshair — it aims at the target's ORIGIN (feet/floor)

2026-06-21, building the SkytestProbe `cast` command (`MagicCaster::CastSpellImmediate`). With no real
crosshair, `cast … target:<ref>` aims at the target ref's **origin**, which sits at the actor's feet ≈
floor level. Consequences, all observed live:

- **Precise bolts hit the floor and do nothing.** Ice Spike (real FormID `0x0002B96C`) cast point-blank
  at an actor → 0 damage; the thin bolt lands at the feet/floor and misses the capsule. A free cast (no
  target, along facing) missed too.
- **Only AoE spells reliably "land"** — Fireball's explosion catches the actor even when the projectile
  itself lands near it. But AoE is its own problem (next point).
- **Projectiles drop over distance** (aimed slightly down at feet) → a target much past ~250–300 units is
  never reached; the shot detonates on the floor mid-flight.

**Implication for a GhostAllies pass-through test:** the rear-witness-HP idea is a dead end. The mechanism
fires perfectly (ally enrolls → `systemGroup` flips to `0xFEED`, projectile stamped, `AddTarget` refuses —
ally takes no damage), but you can't prove the projectile _continued to a witness behind the ally_ via the
witness's HP: a precise bolt won't reach it, and an AoE blast damages the front ally too (and won't bridge
to a far one). The robust sensor is a **projectile-flight probe** — hook `Projectile::UpdateImpl` (the seam
GhostAllies uses) and record the player projectile's **max distance reached before it dies** (control: dies
at the ally ~250; pass-through: travels past). Deferred — see `docs/ideas.md`. `cast` is still correct for
_firing_ a real player projectile (it exercises the stamp); it's just not a precise-hit instrument.

**FormID footgun (don't guess them):** read names from the engine (`give-spell` then `dump` the equip) —
this session's "knowns" were all wrong: `0x0001C789` = **Fireball** (big AoE, not Firebolt), `0x0002B96B`
= **Frostbite** (a stream, not Ice Spike), `0x0002B96C` = **Ice Spike**. Player-clone bases (`0x00000007`,
a handy always-present collidable humanoid) also carry **50% Resist Frost**, another reason frost is a poor
test bolt.
