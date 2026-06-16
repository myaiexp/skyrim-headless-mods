# papyrus-compiler: populate before first compile

This dir holds **Bethesda's Creation Kit Papyrus compiler** (`PapyrusCompiler.exe`,
`PapyrusAssembler.exe`, `PCompiler.dll`) and its runtime DLLs (Antlr, StringTemplate). It's
third-party IP, so it's git-ignored. Populate it locally from your own install, once.

The compiler ships with the **Creation Kit** (and with many modding tool bundles). Copy the
contents of the CK's `Papyrus Compiler` folder here:

```bash
cp "<Skyrim SE>/Papyrus Compiler/"* tools/papyrus-compiler/
```

Expected files after populating:

```
PapyrusCompiler.exe   PapyrusAssembler.exe   PCompiler.dll
Antlr3.Runtime.dll    Antlr3.Utility.dll     antlr.runtime.dll   StringTemplate.dll
```

`tools/compile-papyrus.sh` runs `PapyrusCompiler.exe` from here via wine.
