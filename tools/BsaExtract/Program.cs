using Mutagen.Bethesda;
using Mutagen.Bethesda.Archives;

// Extract a file (default: controlmap.txt) from a Bethesda BSA.
// args[0] = bsa path, args[1] = substring to match (case-insensitive), args[2] = output path
var bsaPath = args[0];
var match = args.Length > 1 ? args[1].ToLowerInvariant() : "controlmap";
var outPath = args.Length > 2 ? args[2] : "/tmp/controlmap.txt";

var reader = Archive.CreateReader(GameRelease.SkyrimSE, bsaPath);
var hit = false;
foreach (var file in reader.Files)
{
    if (file.Path.ToLowerInvariant().Contains(match))
    {
        System.IO.File.WriteAllBytes(outPath, file.GetSpan().ToArray());
        System.Console.WriteLine($"EXTRACTED: {file.Path} -> {outPath}");
        hit = true;
    }
}
if (!hit) System.Console.WriteLine("No match. (run with match='' to list all)");
