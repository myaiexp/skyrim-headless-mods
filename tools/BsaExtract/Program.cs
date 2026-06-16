using Mutagen.Bethesda;
using Mutagen.Bethesda.Archives;

// Extract a file (default: controlmap.txt) from a Bethesda BSA.
// args[0] = bsa path, args[1] = substring to match (case-insensitive), args[2] = output path
var bsaPath = args[0];
var match = args.Length > 1 ? args[1].ToLowerInvariant() : "controlmap";
var outPath = args.Length > 2 ? args[2] : "/tmp/controlmap.txt";

var reader = Archive.CreateReader(GameRelease.SkyrimSE, bsaPath);

// Empty match = list mode: print every file path, write nothing.
if (match.Length == 0)
{
    foreach (var file in reader.Files)
        System.Console.WriteLine(file.Path);
    return;
}

var hit = false;
foreach (var file in reader.Files)
{
    if (file.Path.ToLowerInvariant().Contains(match))
    {
        if (hit)
        {
            // Single fixed outPath can only hold one file — don't silently overwrite the
            // first extraction. Warn and skip; narrow the match to get this one.
            System.Console.WriteLine($"SKIPPED (would overwrite {outPath}): {file.Path}");
            continue;
        }
        System.IO.File.WriteAllBytes(outPath, file.GetSpan().ToArray());
        System.Console.WriteLine($"EXTRACTED: {file.Path} -> {outPath}");
        hit = true;
    }
}
if (!hit) System.Console.WriteLine("No match. (run with match='' to list all)");
