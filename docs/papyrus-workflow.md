# Papyrus build / install / iterate

The build is easy. The _iterate_ loop has two gotchas that will eat hours if you don't know
them, both learned the hard way.

## Build

```bash
./mods/<Mod>/build.sh            # -> mods/<Mod>/build/  (esp + Scripts/*.pex)
./mods/<Mod>/build.sh --install  # also copy into live Data/ + activate in Plugins.txt
```

`--install` does three things to the live game:

1. copies `<Mod>.esp` into `Data/`
2. copies `Scripts/<Script>.pex` into `Data/Scripts/`
3. ensures the plugin is **active** in `Plugins.txt`

## Plugins.txt: `*` means ENABLED

In Skyrim SE / Fallout 4, `Plugins.txt` lists managed plugins and a leading **`*` marks the
active ones**. No prefix = present but **disabled**. (This is inverted from old Skyrim LE,
where the file only listed active plugins.) `--install` adds the `*`.

## Gotcha 1: a changed `.pex` needs a FULL game restart

The Papyrus VM loads each script's bytecode into memory **once per game session** and caches
it. Replacing the `.pex` on disk does nothing until the game **process fully exits and
relaunches**. Returning to the main menu is not enough; neither is reloading a save.

Proven the hard way: new `.pex` on disk + quest re-initialized in the same session = the
_old_ code still ran (zero new log output).

So every script change: **build → `--install` → quit Skyrim to desktop → relaunch.**

## Gotcha 2: scripts bake into saves, kick the quest

A Start-Game-Enabled quest's script instance is persisted into your save. Once it's run
`OnInit` there, it won't run it again, and a replaced script version may not cleanly
resurrect, leaving a dead "zombie" instance that produces no events.

After a fresh launch, force a clean restart of the quest from the console (`~`):

```
stopquest <QuestEditorID>
startquest <QuestEditorID>
```

That re-runs `OnInit` against the freshly-loaded bytecode. The cleanest alternative is to
test on a save that predates the mod (or a new game), where `OnInit` fires fresh anyway.

### Full iterate loop

```
edit .psc
./build.sh --install
quit Skyrim to desktop  →  relaunch  →  load save
console: stopquest <QuestEditorID> ; startquest <QuestEditorID>
test
```

## Debugging: read the Papyrus log

Enable `Debug.Trace` output and read it. Logs rotate at:

```
<prefix>/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/Logs/Script/Papyrus.0.log
```

(`Papyrus.0.log` = current/most recent session; `.1` = previous, etc.) Generous `Debug.Trace`
calls turn "it doesn't work" into "execution reached X but not Y", which is how every bug in
RapidBowHold was actually located (dead control hook, wrong API names, early-firing animation
event, and both gotchas above).

If logging is off, set in `Skyrim.ini` under `[Papyrus]`: `bEnableLogging=1`, `bEnableTrace=1`,
`bLoadDebugInformation=1`.
