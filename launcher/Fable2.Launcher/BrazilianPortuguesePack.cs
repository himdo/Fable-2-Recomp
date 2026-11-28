using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace Fable2Launcher;

internal static class BrazilianPortuguesePack
{
    private const string StateFile = ".fable2-pt-br.json";
    private const string Cultures = "language/cultures.txt";
    private const string Manifest = "dir.manifest";
    private static readonly string[] Files =
    [
        "language/pt-br/text/book.babel", "language/pt-br/speechdoc/speech.adb",
        "pubgames/common/localization.pt.xml", "pubgames/keystone/localization.pt.xml",
        "pubgames/magedice/localization.pt.xml", "pubgames/wizardstower/localization.pt.xml",
        Cultures
    ];

    // Outside data/ so the game's manifest healer never indexes the backups.
    private sealed record Backup(byte[]? Original, string InstalledHash);
    private sealed record State(Dictionary<string, Backup> Files, byte[] Manifest, string[] NewDirectories);
    private static readonly UnicodeEncoding BigEndian = new(true, true, true);
    private static readonly UnicodeEncoding LittleEndian = new(false, true, true);

    internal static bool IsInstalled(string root) => File.Exists(Path.Combine(root, StateFile));

    internal static void Install(string root, string archivePath)
    {
        using var fileLock = Lock(root);
        if (IsInstalled(root)) throw new InvalidOperationException("Remove the existing PT-BR pack before installing another.");
        string data = Path.Combine(root, "data");
        if (!File.Exists(Path.Combine(root, "default.xex")) ||
            !File.Exists(Path.Combine(data, "language/en-uk/text/book.babel")) ||
            !File.Exists(Path.Combine(data, "language/en-uk/speech/speech.bnk")))
            throw new InvalidDataException("Select extracted game files with the original en-uk text and voice bank.");

        var payload = new Dictionary<string, byte[]>();
        using var zip = ZipFile.OpenRead(archivePath);
        const string anchor = "language/pt-br/text/book.babel";
        var anchors = zip.Entries.Where(e => e.FullName == anchor ||
            e.FullName.EndsWith("/" + anchor, StringComparison.Ordinal)).ToArray();
        if (anchors.Length != 1) throw new InvalidDataException("The ZIP must contain one PT-BR language pack.");
        string prefix = anchors[0].FullName[..^anchor.Length];
        foreach (string name in Files.Where(n => n != Cultures))
        {
            var entries = zip.Entries.Where(e => e.FullName == prefix + name).ToArray();
            if (entries.Length != 1 || entries[0].Length is <= 0 or > 16 * 1024 * 1024)
                throw new InvalidDataException($"Missing, duplicate or invalid pack file: {name}");
            using var input = entries[0].Open();
            byte[] bytes = new byte[(int)entries[0].Length];
            input.ReadExactly(bytes);
            if (input.ReadByte() != -1) throw new InvalidDataException($"Invalid pack size: {name}");
            payload.Add(name, bytes);
        }

        var originals = new Dictionary<string, Backup>();
        foreach (string name in Files)
        {
            string path = CheckedPath(data, name);
            byte[]? original = File.Exists(path) ? File.ReadAllBytes(path) : null;
            if (name == Cultures)
                payload.Add(name, MergeCultures(original ?? throw new FileNotFoundException(Cultures)));
            originals.Add(name, new(original, Convert.ToHexString(SHA256.HashData(payload[name]))));
        }
        var state = new State(originals, File.ReadAllBytes(CheckedPath(data, Manifest)),
            PackDirectories().Where(d => !Directory.Exists(Path.Combine(data, d))).ToArray());
        WriteAtomically(root, Path.Combine(root, StateFile), JsonSerializer.SerializeToUtf8Bytes(state), overwrite: false);
        try
        {
            foreach (string name in Files)
            {
                if (Equal(originals[name].Original, payload[name])) continue;
                string path = CheckedPath(data, name);
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                WriteAtomically(root, path, payload[name]);
            }
        }
        catch
        {
            Restore(root, state);
            throw;
        }
    }

    internal static void Remove(string root)
    {
        using var fileLock = Lock(root);
        if (!IsInstalled(root)) return;
        State state = JsonSerializer.Deserialize<State>(File.ReadAllBytes(Path.Combine(root, StateFile)))
            ?? throw new InvalidDataException("Invalid PT-BR backup.");
        Restore(root, state);
    }

    private static void Restore(string root, State state)
    {
        string data = Path.Combine(root, "data");
        // Check every target before writing. Both states are valid on a retry after interruption.
        var changed = new List<string>();
        foreach (string name in Files)
        {
            var backup = state.Files[name];
            string path = CheckedPath(data, name);
            byte[]? current = File.Exists(path) ? File.ReadAllBytes(path) : null;
            if (Equal(current, backup.Original)) continue;
            if (current == null || Convert.ToHexString(SHA256.HashData(current)) != backup.InstalledHash)
                throw new IOException($"Another change was found in {name}. Preserve that change and restore the pack version before removing PT-BR.");
            changed.Add(name);
        }

        string manifestPath = CheckedPath(data, Manifest);
        byte[] manifest = File.ReadAllBytes(manifestPath);
        byte[] cleaned = CleanManifest(manifest, state);
        foreach (string name in changed)
        {
            string path = CheckedPath(data, name);
            if (state.Files[name].Original is byte[] original) WriteAtomically(root, path, original);
            else File.Delete(path);
        }
        if (!manifest.AsSpan().SequenceEqual(cleaned)) WriteAtomically(root, manifestPath, cleaned);
        foreach (string dir in PackDirectories().Where(d => state.NewDirectories.Contains(d)))
        {
            string path = CheckedPath(data, dir);
            if (Directory.Exists(path) && !Directory.EnumerateFileSystemEntries(path).Any()) Directory.Delete(path);
        }
        File.Delete(Path.Combine(root, StateFile));
    }

    private static IEnumerable<string> PackDirectories()
    {
        var directories = new HashSet<string>();
        foreach (string name in Files)
        {
            for (int slash = name.IndexOf('/'); slash >= 0; slash = name.IndexOf('/', slash + 1))
                directories.Add(name[..slash]);
        }
        return directories.OrderByDescending(d => d.Length);
    }

    private static bool Equal(byte[]? a, byte[]? b) =>
        a == null || b == null ? a == b : a.AsSpan().SequenceEqual(b);

    private static FileStream Lock(string root) => new(Path.Combine(root, ".fable2-pt-br.lock"),
        FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None, 1, FileOptions.DeleteOnClose);

    private static string CheckedPath(string data, string relative)
    {
        string path = Path.Combine(data, relative);
        for (FileSystemInfo? node = new FileInfo(path); node != null; node = Directory.GetParent(node.FullName))
        {
            if (node.LinkTarget != null) throw new IOException($"Linked pack destination: {node.FullName}");
            if (string.Equals(node.FullName, data, StringComparison.OrdinalIgnoreCase)) break;
        }
        return path;
    }

    private static void WriteAtomically(string root, string path, byte[] bytes, bool overwrite = true)
    {
        string temp = Path.Combine(root, ".fable2-pt-br-" + Guid.NewGuid().ToString("N") + ".tmp");
        try
        {
            File.WriteAllBytes(temp, bytes);
            File.Move(temp, path, overwrite);
        }
        finally { File.Delete(temp); }
    }

    private static byte[] CleanManifest(byte[] current, State state)
    {
        // The runtime may have indexed other mods too. Remove only new rows for files we created.
        static string Key(string s) => s.TrimEnd('\r', '\n').Replace('\\', '/');
        string before = Encoding.Latin1.GetString(state.Manifest);
        var originalKeys = before.Split('\n').Select(Key).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var removed = Files.Where(f => state.Files[f].Original == null && !originalKeys.Contains(f))
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        string text = Encoding.Latin1.GetString(current);
        string clean = Regex.Replace(text, @"[^\n]+(?:\n|\z)", m => removed.Contains(Key(m.Value)) ? "" : m.Value);
        if (!before.EndsWith('\n') && (clean == before + "\r\n" || clean == before + "\n")) return state.Manifest;
        return Encoding.Latin1.GetBytes(clean);
    }

    internal static byte[] MergeCultures(byte[] bytes)
    {
        var encoding = bytes.AsSpan().StartsWith(BigEndian.GetPreamble()) ? BigEndian
            : bytes.AsSpan().StartsWith(LittleEndian.GetPreamble()) ? LittleEndian
            : throw new InvalidDataException("Expected a UTF-16 BOM in cultures.txt.");
        string text = encoding.GetString(bytes, 2, bytes.Length - 2);
        var header = Regex.Match(text, @"(?im)^;[^\r\n]*languages supported by the game[^\r\n]*");
        if (!header.Success) throw new InvalidDataException("Unrecognized language sections in cultures.txt.");
        string fallback = MergeSection(text[..header.Index], "en-uk", "en-uk");
        string supported = MergeSection(text[header.Index..], "en-us", "pt-br");
        return [.. encoding.GetPreamble(), .. encoding.GetBytes(fallback + supported)];
    }

    private static string MergeSection(string section, string anchor, string target)
    {
        var existing = new Regex(@"(?im)^([ \t]*pt-br[ \t]+[^ \t\r\n;]+[ \t]+)[^ \t\r\n;]+");
        if (existing.Matches(section).Count > 1) throw new InvalidDataException("Duplicate pt-br culture.");
        if (existing.IsMatch(section)) return existing.Replace(section, m => m.Groups[1].Value + target);
        var row = Regex.Match(section, @"(?im)^[ \t]*" + anchor + @"[ \t]+[^\r\n]+");
        if (!row.Success) throw new InvalidDataException($"Missing {anchor} in cultures.txt.");
        string eol = section.Contains("\r\n") ? "\r\n" : "\n";
        return section.Insert(row.Index + row.Length, eol + "pt-br\tGUI_SCREEN_OPTIONS_LANGUAGE_PT_BR\t" + target);
    }
}
