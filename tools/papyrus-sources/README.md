# papyrus-sources — populate before first compile

The Papyrus compiler needs the full transitive type graph of everything a script
references, so it needs the vanilla + SKSE base API source trees. Two of these are
**third-party IP that this repo does not redistribute** and are git-ignored — you populate
them locally, once, from your own game and SKSE install.

| Dir / file                 | Source | In git? |
| -------------------------- | ------ | ------- |
| `vanilla/`                 | Bethesda's base API `.psc` stubs (the ~77 native types: `Actor`, `Form`, `Weapon`, …) | **no — populate** |
| `TESV_Papyrus_Flags.flg`   | Bethesda's user-flags file the compiler requires | **no — populate** |
| `skse/`                    | SKSE's augmented `.psc` sources (the scripts SKSE extends) | **no — populate** |
| `skyui/`                   | SkyUI MCM base classes (`SKI_ConfigBase`, …) — open-source, redistributable | yes (committed) |

## Populate

**vanilla/** + **`TESV_Papyrus_Flags.flg`** — both ship inside your game install. Extract
`Data/Scripts.zip` (the Papyrus *Source* tree) and copy the base API scripts + the `.flg`:

```bash
# from your Skyrim SE install
unzip -j "<Skyrim SE>/Data/Scripts.zip" -d /tmp/papyrus-src
cp /tmp/papyrus-src/TESV_Papyrus_Flags.flg tools/papyrus-sources/
# copy the base API scripts you need (or all of them — only the native types are used)
cp /tmp/papyrus-src/*.psc tools/papyrus-sources/vanilla/
```

The original `vanilla/` set here was the curated `SkyrimSE/vanilla` base-API subset from the
public `BellCubeDev/papyrus-index` repo (the native types only, not the full ~1300-file dump) —
either source works; the compiler only needs the base API types.

**skse/** — from the SKSE64 archive's `Data/Scripts/Source/`:

```bash
cp "<SKSE64 archive>/Data/Scripts/Source/"*.psc tools/papyrus-sources/skse/
```

That's it — `tools/compile-papyrus.sh` imports `skyui ; skse ; vanilla` (+ this dir for the
`.flg`) and resolves the rest.
