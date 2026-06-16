# skytest replay — deterministic test-setup playback (design)

State: **machinery built & verified 2026-06-16; world-staging goes via direct-call probe commands,
not console `exec`** — see `docs/plans/skytest-replay-handoff.md`. Plan →
`docs/plans/skytest-replay-plan.md`.

> ⚠ **Design correction (2026-06-16, resolved).** This design's world-staging via `exec <console>`
> (`coc` / `player.placeatme` / `player.addspell`) does **not** work: programmatic `CompileAndRun`
> faults in the gamescope test session — headless **and** visible, even fully in-world. (The
> _interactive_ console works there, so it's the programmatic call path, not a missing subsystem;
> cause unpinned and not worth pinning — `skytest/docs/headless-findings.md:320`.) This isn't a bug
> to chase: the harness model is **engine calls for staging, the drive layer for input**. Staging
> goes through **direct-call** SkytestProbe commands (`give-spell`/`set-av`, and `coc`/`placeatme`
> added per-need), not console. The parse / gate / input / `shot` machinery and the
> `until:inworld` + `until:menu:<NAME>` gates were all built and verified live. Full status,
> options, and the recommended path forward: the handoff doc.

## Problem

Verifying a mod in-engine means re-reaching the same setup state every build: boot, autoload
the test save, console-stage the world, then drive a fixed sequence of real input (open Favorites,
equip a spell, charge, release). For GhostAllies-summons the setup is, every single run:

> `coc` to a test cell → spawn a hostile downrange → give yourself a summon spell + Firebolt →
> Favorites → equip the summon → cast it → equip Firebolt → fire it through the summon.

That sequence is **identical every time** and is pure overhead on the actual test (does the
projectile phase through the summon?). Driving it by hand each iteration is wasted work; the only
part that changes between builds is the mod under test, not the setup.

## Goal / non-goals

- **Goal:** capture a repeatable test-setup as a small **step-script**, and a `skytest replay`
  interpreter that re-runs it to snap back to the exact target state — then I probe the mod live.
- **Non-goal — baked-in pass/fail.** Replay reaches a *state*; it does not assert. The actual mod
  check (fire, `watch` the summon's Health, judge) happens live afterward. (Consistent with dropping
  the `skytest smoke` auto-verdict idea — the CLI is CC-only, no unattended consumer to serve.)
- **Non-goal — a capture subsystem.** I author the script; see "author, not capture" below.
- **Non-goal — camera/pointer motion.** Discrete input only (keys + mouse buttons). We never move
  the pointer, which sidesteps the cursor-desync wall (`skytest/docs/headless-findings.md` #14).

## Approach: author, not capture

I am both the driver and the author. I drive a live session (`skytest test`, then `drive`/`exec`,
verifying with `shot`) to work out the steps, and **write the `.steps` file directly** as I go.
So the deliverable is *not* a recorder — it's a **step-script format + a `replay` interpreter**.
"Recording" reduces to "CC writes a text file," which it composes anyway while driving.

A tee-recorder (auto-append every `drive`/`exec` I issue into a draft) was considered and dropped:
its only real win is saving my authoring tokens, and it captures the trial-and-error flailing I'd
strip out by hand. Not worth the capture machinery when tokens aren't the constraint. (Raw
human-at-the-keyboard capture is a separate, harder feature — deferred, see `docs/ideas.md`.)

## The step-script format

Line-based text, one step per line; `#` comments; blank lines ignored. Chosen over JSON for being
greppable, hand-editable, and a fit with skytest's bash+file idiom. Scripts live next to the mod
they test: `mods/<mod>/<name>.steps`.

| Step | Meaning |
| --- | --- |
| `exec <console batch>` | Run the rest of the line as a console command via SkytestProbe's exec bridge. World-staging a human can't reproduce on cue (spawn a specific ref, grant a spell). |
| `tap <KEY>` | Press+release one key. |
| `key <K1> <K2> …` | A sequence of taps. |
| `hold <BTN\|KEY> <duration\|until:COND>` | Press down, gate (fixed duration **or** until COND), release. `BTN` ∈ `LMB`/`RMB`. |
| `wait <duration\|until:COND>` | Block until COND holds, or for a fixed duration. |
| `shot [name]` | Checkpoint screenshot — a replay debugging aid, not part of the tested behavior. |

Durations are explicit (`500ms`, `2s`). `until:COND` gates on observed game state (next section).

The GhostAllies-summons setup as a script (illustrative refs):

```
# setup — console world-staging (a human can't reproduce this on cue)
exec   coc WhiterunOrigin
wait   until:inworld                 # available today (IsInWorld)
exec   player.placeatme 0x1A2B3 1    # hostile, downrange
exec   player.addspell <SummonAtronach>
exec   player.addspell <Firebolt>

# interaction — real input, the human path
tap    Q                             # open Favorites
wait   until:menu:FavoritesMenu      # incremental probe gate
key    Down Down                     # navigate to the summon
tap    E                             # equip right hand
tap    Q                             # close Favorites
hold   LMB until:charged             # press, poll until charged, release → cast
wait   until:actorcount enemy=2      # summon present
```

`hold LMB until:charged` is the load-bearing move: it gates the **release** on observed charge
state, not a baked wall-clock. That is the direct lesson from AutoCastSpell — the failure there was
*fixed timing*, not input. State-gated holds are how replay stays robust across boot/frame jitter.

## Sync model — gate on state, not sleeps

Every `until:COND` is backed by a SkytestProbe query the interpreter polls (issue query → probe
writes the current value to `trace.jsonl` → interpreter tails it until the condition holds or a
timeout fires). This is the exact pattern `gs_wait_ready` already uses for in-world.

- **Available today:** `until:inworld` (the existing `IsInWorld` gate — no main/loading menu +
  player 3D loaded) and `watch`-based actor-value gates.
- **Incremental — each lands when a real script first needs it (YAGNI, no speculative vocabulary):**
  `until:menu:<MenuName>` (UI menu open/closed), `until:charged` (weapon/spell at full charge —
  `MagicCaster` ready / bow fully drawn), `until:actorcount <pred>` (population predicate for "the
  summon appeared").
- Fixed `duration` is permitted but discouraged — reserved for steps with no observable state to
  gate on. On timeout, replay **aborts with the offending step**, never silently proceeds (a missed
  gate would otherwise produce a bogus test from a wrong state). Gate polls reuse `gs_wait_ready`'s
  existing timeout convention (180s default) rather than inventing a new one; a per-gate override is
  deferred until a script needs it.

### SkytestProbe is the permanent home for these gates

Every sync-gate query this feature needs is added to **SkytestProbe as a permanent, reusable
command** — never temporary instrumentation removed after the GhostAllies test. SkytestProbe
accretes: a `until:charged` query added now serves every future spell/bow test, `until:menu:` serves
every menu-driven test, and so on. This is a standing project norm (SkytestProbe is maintained and
extended alongside the mods, and any probe/watch/query added for a test stays in it permanently),
not a one-off for this feature.

## Session & replay flow

`skytest replay <mod> <script.steps>`:

1. Boots a normal headless `skytest test <mod>` session (reusing the existing isolation + probe
   injection + StartOnSave autoload path — no new launch machinery).
2. Runs the script top to bottom, each `until:` gate polling the probe, aborting on timeout.
3. On completion, **leaves the session detached + ready** at the target state.

I then do the actual mod test live against that state (fire, `watch` the summon's Health, `shot`,
judge), or — once the test action itself stabilises — append those steps to the same script. The
format doesn't distinguish "setup" from "test"; the `#` comment headers do.

## Discoverability (so a future session reuses the steps)

The steer lives in three places so a later session can't miss it:

1. **`skytest test <mod>` output** prints a one-liner: *drove this by hand? persist it as
   `mods/<mod>/<name>.steps` and re-run with `skytest replay <mod> <name>.steps`.*
2. **`CLAUDE.md` "Testing a mod you built" section** — the norm: *first* test = drive live + author
   the `.steps`; *every test after* = `skytest replay`.
3. **`skytest/README.md`** — the `replay` verb reference + the same first-vs-subsequent guidance.

(No `--save` flag — without a tee-recorder there's nothing to auto-capture; authoring is writing
the file.)

## Out of scope / deferred

- **Tee-recorder** (auto-log my `drive`/`exec` into a draft) — dropped, rationale above.
- **Raw human-input capture** (a person plays, raw libei events recorded + re-synced) — separate,
  harder feature; would carry camera/pointer paths and wall-clock re-sync. Deferred to `docs/ideas.md`.
- **Baked-in assertions / verdicts** — out by design; replay reaches state, CC judges live.
- **Speculative gate vocabulary** — gates are added per the first script that needs them.

## Verb surface (decided)

- `skytest replay <mod> <script>` — a distinct verb (reads cleaner in the steer message than
  `test <mod> --replay …`). Internally it composes the existing `test` launch + a step interpreter.
- `<script>` resolves under `mods/<mod>/` when given as a bare name (`foo.steps` →
  `mods/<mod>/foo.steps`); an explicit path (contains `/`) is taken as-is. Scripts default to living
  at `mods/<mod>/<name>.steps`. Format: line-based text.
- Parser note for the plan: tokenize on whitespace, but `exec`'s argument is the **entire rest of
  the line** (console commands contain spaces), and `hold`'s grammar is `hold <target> <gate>` where
  `<gate>` is the second token (`<duration>` or `until:<COND>`) — don't mis-split it as another key.
