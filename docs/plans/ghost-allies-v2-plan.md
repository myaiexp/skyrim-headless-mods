# GhostAllies v2 Implementation Plan

**Goal:** Extend GhostAllies so player-fired **spell projectiles** (and arrows) physically pass
through followers, and generalize from the single nearest follower to the **whole party**.

**Architecture:** One shared `StampProjectilePhantom(RE::Projectile*)` installed on `UpdateImpl`
(vtable slot **0xAB**) of each in-scope projectile subclass vtable — replacing v1's arrow-only
`GetPowerSpeedMult` (0xB0) hook. The stamp copies a Havok `systemGroup` onto the projectile's
phantom collidable so the projectile ignores that group exactly as it already ignores its own
shooter. v1's read-only nearest-follower stamp is preserved through Tasks 1–2 (proving the new hook
point with the verified mechanism) and then generalized in Task 3 to a shared "ghost" systemGroup
written onto every teammate (whole-party pass-through).

**Tech Stack:** C++23, CommonLibSSE-NG (FetchContent, AE+SE), spdlog; headless clang-cl + lld-link +
xwin cross-build (Linux→Windows DLL); Address Library for version-independent offsets.

**Domain note on verification:** this is a Windows DLL that only does anything inside a running
Skyrim — there is no host-side unit harness. Each task's "test cases" are therefore: (a) the build
succeeds and emits a PE32+ DLL, (b) the specified lines appear in `GhostAllies.log`, and (c) the
described in-game observation holds. Treat the log assertions as the executable spec. **Accepted
constraint (from the user):** whole-party multi-follower (Task 3) and flame/beam/cone pass-through
(Task 2) will ship **unverified in-game** — the user won't install to test them. Do not block on
their in-game verification; do verify build + log + the regression checks that *are* observable.

---

## Reference material

- Design: `docs/plans/ghost-allies-design.md` → read the **"## v2 design"** section first (it
  governs; sections below it in that doc are pre-pivot/v1 historical).
- v1 source (the thing being modified): `mods/GhostAllies/src/main.cpp` (151 lines, single file).
- Pattern source in-repo: `mods/AutoFireBow/{CMakeLists.txt,build.sh,src/main.cpp}` — vtable-hook
  idiom (`REL::Relocation::write_vfunc`), logging, SKSE boilerplate.
- Build/iterate: `./mods/GhostAllies/build.sh --install`, then restart the game and read
  `<SKSE log dir>/GhostAllies.log`.
- External precedent for the Task 3 char-controller group write: `powerof3/PapyrusExtenderSSE`
  (`GetCollisionFilterInfo`, system-group read/write), `adamhynek/activeragdoll` (custom
  systemGroups, per-actor group management).

## Grounded facts (verified against the FetchContent'd CommonLibSSE-NG headers)

- `ArrowProjectile : MissileProjectile : Projectile`; `Flame/Beam/Cone/Grenade/BarrierProjectile`
  derive from `Projectile`. `Projectile` is always the **first base**, so `T::VTABLE[0]` is the
  Projectile-primary vtable for every subclass (confirmed incl. `BeamProjectile`'s multiple
  inheritance — `Projectile` is base at offset 000).
- Every in-scope subclass overrides `void UpdateImpl(float a_delta)` at vtable slot **0xAB**.
- `unk0E0` (the `bhkSimpleShapePhantom`) and `shooter` live on the base `Projectile`
  (`GetProjectileRuntimeData()`), so the phantom-reach + shooter-check code is identical for all.
- VTABLE symbols all exist: `VTABLE_ArrowProjectile`, `VTABLE_MissileProjectile`,
  `VTABLE_FlameProjectile`, `VTABLE_BeamProjectile`, `VTABLE_ConeProjectile` (`Offsets_VTABLE.h`).
- **Per-subclass originals differ.** Each vtable holds its own `UpdateImpl` address, so the hook
  must keep a **distinct original per subclass** — a single shared `REL::Relocation func` (v1's
  arrow-only pattern) would call the wrong original. Use a template (one static `func` per
  instantiation) or one struct per subclass.

## File structure

| File | Responsibility |
|------|----------------|
| `mods/GhostAllies/src/main.cpp` | Entire plugin. v2 reshapes it into: log setup; `StampProjectilePhantom` free function (shared stamp logic); a per-subclass `UpdateImpl` hook (template); `InstallHooks` looping the in-scope vtables; (Task 3) ghost-group membership maintenance. Stays one file — well under the ~400-line split threshold. |

No new files. CMake/build.sh unchanged.

---

### Task 1: Unify on `UpdateImpl`, extract the shared stamp (behavior-preserving) [Mode: Direct]

Move the **arrow** effect from the `GetPowerSpeedMult` (0xB0) hook to the unified `UpdateImpl`
(0xAB) hook, with the v1 mechanism unchanged (read-only, single nearest follower). This proves the
new hook point against the already-verified behavior before any new feature is added.

**Files:**
- Modify: `mods/GhostAllies/src/main.cpp`

**Contracts:**
- Keep `FindNearestFollower(RE::Actor* player) -> RE::Actor*` as-is.
- Extract a free function `void StampProjectilePhantom(RE::Projectile* a_proj, const char* a_label)`
  containing v1's current stamp body verbatim: gate on `shooter && shooter->IsPlayerRef() &&
  runtime.unk0E0`; reach `unk0E0 → RE::bhkShapePhantom → RE::hkpShapePhantom →
  collidable.broadPhaseHandle.collisionFilterInfo`; find nearest follower; read
  `follower->GetCollisionFilterInfo(info); group = info >> 16`; stamp with the existing idempotence
  guard (`group != 0 && (filterInfo >> 16) != group` → clear top 16, OR in `group << 16`); log
  `stamped player {} -> follower …` with `a_label` for the type. **Read-only — must not modify the
  follower** (unchanged from v1).
- Per-subclass hook with a distinct original **and a per-type label** (the label gives clean
  per-type log lines, which Task 2 relies on to tell flame/beam/cone apart — preferred over decoding
  `GetFormType()`):
  ```cpp
  template <class T>
  struct StampHook {
      static inline const char* label = "projectile";
      static void thunk(RE::Projectile* a_this, float a_delta) {
          StampProjectilePhantom(a_this, label);
          func(a_this, a_delta);                // this subclass's original UpdateImpl
      }
      static inline REL::Relocation<decltype(thunk)> func;
  };
  template <class T>
  void InstallStamp(const char* a_label) {
      StampHook<T>::label = a_label;
      REL::Relocation<std::uintptr_t> vtbl{ T::VTABLE[0] };
      StampHook<T>::func = vtbl.write_vfunc(0xAB, StampHook<T>::thunk);
  }
  ```
- `InstallHooks()` for this task calls **only** `InstallStamp<RE::ArrowProjectile>("arrow");` and
  logs the install. **Remove** the old `GetPowerSpeedMult` (0xB0) `write_vfunc` and its `StampHook`
  struct.

**Test Cases (verification):**
- `./mods/GhostAllies/build.sh` exits 0; `file build/GhostAllies.dll` → `PE32+ executable (DLL)
  (console) x86-64, for MS Windows`. `--install` copies to `$GAME_DATA/SKSE/Plugins/`.
- In-game (regression of v1): follower between player and enemy → arrows pass through the follower
  and hit the enemy; `GhostAllies.log` shows the stamp line. No follower in line → vanilla.

**Constraints:**
- Behavior must match v1 exactly (only the hook point and code shape change). If arrows stop phasing
  after the move to `UpdateImpl`, **stop and report** before adding features — the per-subclass
  original wiring is the likely cause.
- `UpdateImpl` fires every frame per projectile; the idempotence guard must short-circuit after the
  first stamp so steady-state cost is one compare + original call.

**Commit after passing.**

---

### Task 2: Extend the unified hook to spell projectile subclasses [Mode: Direct]

Add the spell projectile types to the install loop. Mechanism still read-only nearest-follower
(Task 3 generalizes it). This is where "spells bypass followers" first works.

**Files:**
- Modify: `mods/GhostAllies/src/main.cpp`

**Contracts:**
- In `InstallHooks()`, add (alongside the arrow install), passing a per-type label:
  `InstallStamp<RE::MissileProjectile>("missile"); InstallStamp<RE::FlameProjectile>("flame");
  InstallStamp<RE::BeamProjectile>("beam"); InstallStamp<RE::ConeProjectile>("cone");`
  **Do not** install `GrenadeProjectile` (runes) or `BarrierProjectile` (walls) — out of scope.
- The per-type label (added to the template in Task 1) already makes the stamp log line distinguish
  missile/flame/beam/cone hits (`stamped player <label> -> follower …`) — no further change to
  `StampProjectilePhantom` in this task.

**Test Cases (verification):**
- Build/install succeed (PE32+).
- In-game, **missile spells are the must-pass case**: with a follower in the line, Firebolt / Ice
  Spike / Fireball pass through the follower and strike the enemy; log shows the missile stamp line.
- Flame / Beam / Cone: observe and **document per type** whether they phase (log will show the
  stamp firing regardless; pass-through is the open question for continuous types).
- Enemy-cast spells → unaffected (only `IsPlayerRef()` shooters are stamped).
- Runes (grenade) → still collide normally (out of scope, confirm unchanged).

**Constraints:**
- A continuous type (flame/beam/cone) **not** phasing is an expected, documented outcome — **not a
  blocker**. Record which types phase; the per-type `AddImpact` (slot 0xBD) skip fallback is
  deferred (`docs/ideas.md`), not part of this task.
- Still read-only: never modify any actor in this task.

**Commit after passing.**

---

### Task 3: Whole-party multi-follower via a shared "ghost" systemGroup [Mode: Delegated]

Generalize the stamp from the single nearest follower to the whole party by enrolling every current
teammate into one reserved systemGroup and stamping that group on player projectiles. **This is the
accepted-unverified feature** — verify build, log lines, and the single-follower regression; do not
block on in-game multi-follower observation.

**Files:**
- Modify: `mods/GhostAllies/src/main.cpp`

**Contracts:**
- Define a reserved 16-bit ghost group constant (e.g. `constexpr std::uint32_t kGhostGroup`), chosen
  high enough to not collide with engine-assigned per-object systemGroups. Document the choice and
  confirm against observed actor groups in the log (no clash).
- Membership maintenance, callable from the stamp path (no SKSE event hooking required):
  ```cpp
  // FormID -> the actor's original systemGroup, for actors we've enrolled.
  std::unordered_map<RE::FormID, std::uint32_t> g_enrolled;   // file-scope, single-threaded use
  void MaintainGhostGroup(RE::PlayerCharacter* a_player);
  ```
  Behavior: resolve current teammates (`IsPlayerTeammate()` over `ProcessLists` high actors). For
  each current teammate **not** in `g_enrolled`: read + save its original systemGroup, write
  `kGhostGroup` into its char-controller collision filter info, insert into `g_enrolled`. For each
  `g_enrolled` entry that is **no longer** a teammate (or whose actor is gone): restore its saved
  original group and erase it. Writes must be idempotent (skip an actor already at `kGhostGroup`).
- The actor systemGroup **write path** (read is `Actor::GetCollisionFilterInfo` / the
  char-controller's `GetCollisionFilterInfo` at slot 0x08): locate the writable char-controller
  filter-info field/setter (`Actor::GetCharController()` → `bhkCharacterController`). Mine
  `powerof3/PapyrusExtenderSSE` and `adamhynek/activeragdoll` for the exact member. Write only the
  top-16 systemGroup bits; preserve the low 16 (layer/subsystem) bits.
- `StampProjectilePhantom` change: for player-shot projectiles, call `MaintainGhostGroup(player)`
  once, then stamp the phantom with `kGhostGroup` (clear top 16, OR in `kGhostGroup << 16`); the
  idempotence guard now compares the phantom's top-16 bits against `kGhostGroup`.
- Logging: `enrolled teammate <name/id> (orig group <g>)`, `restored teammate <name/id>`, and
  `stamped player <type> -> ghost group` lines (low-rate).

**Test Cases (verification):**
- Build/install succeed (PE32+).
- `GhostAllies.log` shows `enrolled teammate …` for each active follower on the first player shot,
  `stamped … ghost group` on player projectiles, and `restored teammate …` after dismissing one.
- **Single-follower regression (observable):** with one follower in the line, arrows and missile
  spells still pass through and hit the enemy (the whole-effect must not regress for the 1-follower
  case).
- **Whole-party (accepted unverified):** with 2+ followers stacked in the line, projectiles are
  expected to phase through all — document if the user ever reports play data; not a gate.

**Constraints:**
- **Stop-and-report gates:** (1) if no writable char-controller systemGroup field/setter can be
  found, report rather than guessing a memory offset; (2) if the single-follower regression check
  fails (the ghost-group write breaks the previously-working single case), report — the documented
  fallback is to revert `StampProjectilePhantom` to Task 2's read-only nearest stamp (the ghost-group
  code is additive and removable).
- Only ever enroll player teammates and only ever stamp player-shot projectiles. Always restore an
  actor's original group when it leaves the teammate set — never leave actors permanently in the
  ghost group.
- Side effect (accepted, documented in the design): teammates sharing the ghost group may visually
  interpenetrate each other; world collision is preserved.

**Commit after passing.**

---

## Execution
**Skill:** superpowers:subagent-driven-development
- Mode A tasks (1, 2): Opus implements directly — well-specified, single-file edits over existing
  verified code.
- Mode B task (3): Dispatched to a subagent — requires CommonLibSSE-NG/precedent exploration to
  find the char-controller systemGroup write path, real membership/restore logic, and has
  stop-and-report gates.
