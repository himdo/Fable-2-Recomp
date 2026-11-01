using System.IO;

namespace Fable2Launcher;

public static class SaveImporter
{
    private const string SavesFolderName = "saves";

    public static string FindSavesFolder(string chosenFolder)
    {
        string nested = Path.Combine(chosenFolder, SavesFolderName);
        if (HasFiles(nested)) return Path.GetFullPath(nested);
        if (string.Equals(Path.GetFileName(Path.TrimEndingDirectorySeparator(chosenFolder)),
                SavesFolderName, StringComparison.OrdinalIgnoreCase) && HasFiles(chosenFolder))
            return Path.GetFullPath(chosenFolder);
        throw new InvalidOperationException("No saves were found in this folder.");
    }

    // Copies before touching the current saves, which are kept in a dated folder
    // if they hold anything. Returns that folder, or null if there was nothing to keep.
    public static string? Import(string sourceSaves, string executableDirectory)
    {
        string source = Path.GetFullPath(sourceSaves);
        string target = Path.GetFullPath(Path.Combine(executableDirectory, SavesFolderName));
        if (string.Equals(source, target, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("These are already the saves this version uses.");

        string staging = target + "-importing";
        if (Directory.Exists(staging)) Directory.Delete(staging, recursive: true);
        try
        {
            CopyFolder(source, staging);
            string? backup = null;
            if (HasFiles(target))
            {
                backup = $"{target}-before-import-{DateTime.Now:yyyyMMdd-HHmmss}";
                Directory.Move(target, backup);
            }
            else if (Directory.Exists(target))
            {
                Directory.Delete(target, recursive: true);
            }
            try
            {
                Directory.Move(staging, target);
            }
            catch when (backup is not null)
            {
                Directory.Move(backup, target);
                throw;
            }
            return backup;
        }
        finally
        {
            if (Directory.Exists(staging)) Directory.Delete(staging, recursive: true);
        }
    }

    private static bool HasFiles(string folder) =>
        Directory.Exists(folder) &&
        Directory.EnumerateFiles(folder, "*", SearchOption.AllDirectories).Any();

    private static void CopyFolder(string source, string destination)
    {
        Directory.CreateDirectory(destination);
        foreach (string folder in Directory.GetDirectories(source, "*", SearchOption.AllDirectories))
            Directory.CreateDirectory(Path.Combine(destination, Path.GetRelativePath(source, folder)));
        foreach (string file in Directory.GetFiles(source, "*", SearchOption.AllDirectories))
            File.Copy(file, Path.Combine(destination, Path.GetRelativePath(source, file)));
    }
}
