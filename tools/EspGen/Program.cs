using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;

// EspGen — generate a "script-host" plugin headlessly (no SSEEdit / Creation Kit).
//
// Produces a tiny Skyrim SE .esp containing one Start-Game-Enabled Quest that hosts a
// single Papyrus script with no properties, no aliases and no masters (the script is
// expected to resolve what it needs itself, e.g. Game.GetPlayer()). This is the minimal
// vehicle for "pure logic" script mods.
//
// Usage:
//   dotnet run -- <out.esp> <QuestEditorID> <ScriptName> [FullName]
// Example:
//   dotnet run -- RapidBowHoldQuest.esp RapidBowHoldQuest RapidBowHoldScript RapidBowHold

if (args.Length < 3)
{
    System.Console.Error.WriteLine("usage: EspGen <out.esp> <QuestEditorID> <ScriptName> [FullName]");
    return 1;
}

var outPath = args[0];
var questEdid = args[1];
var scriptName = args[2];
var fullName = args.Length > 3 ? args[3] : questEdid;

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
mod.Quests.Add(quest);

mod.WriteToBinary(outPath);
System.Console.WriteLine($"Wrote {outPath}: quest {quest.EditorID} [{quest.FormKey}] hosting script {scriptName}");
return 0;
