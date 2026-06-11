using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;

// EspGen — generate a "script-host" plugin headlessly (no SSEEdit / Creation Kit).
//
// Produces a tiny Skyrim SE .esp containing one Start-Game-Enabled Quest that hosts a
// single Papyrus script with no properties. By default it has no aliases and no masters
// (the script is expected to resolve what it needs itself, e.g. Game.GetPlayer()). This
// is the minimal vehicle for "pure logic" script mods.
//
// SkyUI MCM scripts (extends SKI_ConfigBase) additionally need the quest to carry one
// player ReferenceAlias running SKI_PlayerLoadGameAlias, so the MCM re-registers on every
// save-load. Pass --player-alias <AliasScriptName> to add that alias: a ReferenceAlias
// (ID 0, name "PlayerAlias") forced to PlayerRef (Skyrim.esm:0x14) and hosting the named
// local script. This naturally adds Skyrim.esm as a master.
//
// Usage:
//   dotnet run -- <out.esp> <QuestEditorID> <ScriptName> [FullName] [--player-alias <AliasScriptName>]
// Example:
//   dotnet run -- RapidBowHoldQuest.esp RapidBowHoldQuest RapidBowHoldScript RapidBowHold
//   dotnet run -- MyMCM.esp MyMCMQuest MyMCMScript "My MCM" --player-alias SKI_PlayerLoadGameAlias

if (args.Length < 3)
{
    System.Console.Error.WriteLine(
        "usage: EspGen <out.esp> <QuestEditorID> <ScriptName> [FullName] [--player-alias <AliasScriptName>]");
    return 1;
}

var outPath = args[0];
var questEdid = args[1];
var scriptName = args[2];

// Parse the optional [FullName] positional and the --player-alias flag from args[3..].
// FullName is the first arg after ScriptName that is not the --player-alias flag (or its value).
string? fullNameArg = null;
string? playerAliasScript = null;
for (var i = 3; i < args.Length; i++)
{
    if (args[i] == "--player-alias")
    {
        if (i + 1 >= args.Length)
        {
            System.Console.Error.WriteLine("error: --player-alias requires an <AliasScriptName> argument");
            return 1;
        }
        playerAliasScript = args[++i];
    }
    else if (fullNameArg is null)
    {
        fullNameArg = args[i];
    }
    else
    {
        System.Console.Error.WriteLine($"error: unexpected argument '{args[i]}'");
        return 1;
    }
}
var fullName = fullNameArg ?? questEdid;

var modKey = ModKey.FromNameAndExtension(System.IO.Path.GetFileName(outPath));
var mod = new SkyrimMod(modKey, SkyrimRelease.SkyrimSE);

var quest = new Quest(mod.GetNextFormKey(), SkyrimRelease.SkyrimSE)
{
    EditorID = questEdid,
    Name = fullName,
    Flags = Quest.Flag.StartGameEnabled,
    VirtualMachineAdapter = new QuestAdapter(),
};
quest.VirtualMachineAdapter.Scripts.Add(new ScriptEntry
{
    Name = scriptName,
    Flags = ScriptEntry.Flag.Local,
});

if (playerAliasScript is not null)
{
    // PlayerRef — the canonical player PlacedNpc in the base game.
    var playerRef = FormKey.Factory("000014:Skyrim.esm");

    // ReferenceAlias: ID 0, forced to PlayerRef. Adds Skyrim.esm as a master.
    quest.Aliases.Add(new QuestAlias
    {
        ID = 0,
        Name = "PlayerAlias",
        ForcedReference = playerRef.ToNullableLink<IPlacedGetter>(),
    });

    // Alias VMAD: the alias hosts one local Papyrus script, bound to alias ID 0 of this quest.
    quest.VirtualMachineAdapter.Aliases.Add(new QuestFragmentAlias
    {
        Property = new ScriptObjectProperty
        {
            Object = quest.ToLink<ISkyrimMajorRecordGetter>(),
            Alias = 0,
        },
        Scripts =
        {
            new ScriptEntry
            {
                Name = playerAliasScript,
                Flags = ScriptEntry.Flag.Local,
            },
        },
    });
}

mod.Quests.Add(quest);

mod.WriteToBinary(outPath);
var aliasNote = playerAliasScript is not null
    ? $", + player alias hosting {playerAliasScript}"
    : "";
System.Console.WriteLine(
    $"Wrote {outPath}: quest {quest.EditorID} [{quest.FormKey}] hosting script {scriptName}{aliasNote}");
return 0;
