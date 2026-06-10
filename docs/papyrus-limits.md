# Findings: what Papyrus can and cannot do (bow charge saga)

Hard-won conclusions from trying to build a full-power "hold to rapid-fire a bow" mod. Recorded
so no future session (human or AI) re-derives them the slow way. We tried every angle; these
are dead ends, with evidence.

## The core wall: Papyrus cannot fire a charged bow shot

In Skyrim a bow arrow's power (damage, speed, range) comes from the engine's **draw charge**,
and that charge is built **only by real attack input being held**. Nothing reachable from
Papyrus reproduces it:

| Approach tried                                                     | Result                 | Why                                                                                        |
| ------------------------------------------------------------------ | ---------------------- | ------------------------------------------------------------------------------------------ |
| `Debug.SendAnimationEvent(player, "attackRelease")`                | Weak arrow             | Animation events are **cosmetic** — they play the motion but don't set engine charge state |
| Scripting the draw (`bowAttackStart` etc.)                         | Weak + T-pose-ish      | Same: the draw animation plays but charge stays ~0                                         |
| `Weapon.Fire(player, ammo)` (SKSE native)                          | Weak "vending machine" | Spawns the projectile at base/zero charge — no draw multiplier applied                     |
| Letting the held button do a real draw, scripting only the release | Weak                   | `attackRelease` event still doesn't loose at the real charge                               |

**Verified in-game:** a manual mid-draw release out-ranges the mod's scripted "full draw" —
proof the scripted shots are uncharged. The charge→power multiplier is welded to real input
release and is not exposed to Papyrus (nor to PapyrusUtil; po3's Papyrus Extender isn't
installed and its projectile functions would hit the same wall).

## Why animation-replacer mods (Bow Rapid Combo) get full power

They don't script the release — they replace the **actual attack animation** (via OAR) with a
faster/double one. Because it's a _real_ attack animation, the engine charges and looses
normally; the release annotation in the `.hkx` does the real loose. Extra arrows/damage come
from animation-payload spell casts (PayloadInterpreter), sidestepping charge.

The catch: those custom animations need their **behavior-graph states registered** (Nemesis
patch or Behavior Data Injector config). Bow Rapid Combo v3 doesn't self-contain that
injection, so without it the engine can't find the `HKS_*` states and the player **T-poses**.
That is the exact failure every prior attempt hit. Getting it working means authoring/running
behavior injection — i.e. animation work, not code.

## Conclusion

- A self-contained, **scripted**, full-power rapid bow is **not achievable via Papyrus**.
- The animation route works but is animation/behavior authoring, not code.
- The only clean **code** path to engine-level bow control is a native **SKSE C++ plugin**.
  See [skse-tier-bringup.md](skse-tier-bringup.md).

## What DID work (keep)

The headless **Papyrus** toolchain itself is solid and proven: author `.esp` with Mutagen,
compile `.psc` with wine + PapyrusCompiler, debug from the Papyrus log. See
[papyrus-toolchain.md](papyrus-toolchain.md) and [papyrus-workflow.md](papyrus-workflow.md). It's the right tool for _data_
edits and _logic_ scripts — just not for things welded to real input / engine internals.
