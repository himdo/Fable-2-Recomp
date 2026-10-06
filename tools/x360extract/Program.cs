using System.Globalization;
using System.Text;

namespace X360Extract;

public sealed class Options
{
    public string IsoPath = "";
    public string OutDir = ".";
    public bool ListOnly;
    public bool Quiet;
    public bool UnpackStfs;              // --unpack-stfs: expand STFS containers into directories
    public List<string> Files = new();   // --files: comma-separated root entry names
}

public static class Program
{
    public static int Main(string[] args)
    {
        var opts = new Options();
        try
        {
            if (!ParseArgs(args, opts)) return 1;
        }
        catch (OptionException ex)
        {
            Console.Error.WriteLine($"x360extract: {ex.Message}");
            Console.Error.WriteLine(Usage());
            return 1;
        }

        try
        {
            return opts.ListOnly ? ListDisc(opts) : Extract(opts);
        }
        catch (OptionException ex)
        {
            Console.Error.WriteLine($"x360extract: {ex.Message}");
            return 1;
        }
        catch (InvalidDataException ex)
        {
            Console.Error.WriteLine($"x360extract: error: {ex.Message}");
            return 2;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"x360extract: error: {ex.Message}");
            return 3;
        }
    }

    private sealed class OptionException : Exception
    {
        public OptionException(string msg) : base(msg) { }
    }

    private static string Usage() =>
"""
Usage:
  x360extract <iso> [options]          Extract game content from an Xbox 360 ISO
  x360extract --list <iso>             List the GDFX file tree without extracting

Options:
  -o, --out <dir>       Output directory (default: current directory)
  --list                List the GDFX directory tree
  --files <a,b,c>       Only extract the named root entries (case-insensitive)
  --unpack-stfs         Expand STFS (PIRS/LIVE/CON) file entries into directories
                        (default: copy them as raw files, matching retail tools)
  --quiet               Suppress progress output
  -h, --help            Show this help

Behavior:
  Reads the GDFX (Game Disc Format for Xbox) filesystem from the ISO and
  extracts the file tree to the output directory.  Files are copied
  byte-for-byte; STFS containers (e.g. nxeart) stay raw unless
  --unpack-stfs is given.
"""
        ;

    private static bool ParseArgs(string[] args, Options o)
    {
        string? iso = null;
        for (int i = 0; i < args.Length; i++)
        {
            string a = args[i];
            switch (a)
            {
                case "-h":
                case "--help":
                    Console.WriteLine(Usage());
                    Environment.Exit(0);
                    break;
                case "-o":
                case "--out":
                    if (i + 1 >= args.Length) throw new OptionException("--out requires a directory");
                    o.OutDir = args[++i];
                    break;
                case "--list":
                    o.ListOnly = true;
                    break;
                case "--files":
                    if (i + 1 >= args.Length) throw new OptionException("--files requires a list");
                    o.Files = args[++i].Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries).ToList();
                    break;
                case "--unpack-stfs":
                    o.UnpackStfs = true;
                    break;
                case "--quiet":
                    o.Quiet = true;
                    break;
                default:
                    if (a.StartsWith('-'))
                        throw new OptionException($"unknown option '{a}'");
                    if (iso != null)
                        throw new OptionException("multiple ISO paths given");
                    iso = a;
                    break;
            }
        }
        if (iso == null)
            throw new OptionException("no ISO path given");
        o.IsoPath = iso;
        return true;
    }

    // ------------------------------------------------------------------
    //  GDFX header discovery
    // ------------------------------------------------------------------

    private static readonly byte[] GdfxMagic = "MICROSOFT*XBOX*MEDIA"u8.ToArray();

    /// <summary>
    /// Scan the ISO for the GDFX header ("MICROSOFT*XBOX*MEDIA" magic).
    /// Returns the offset of the header, or -1 if not found.
    /// </summary>
    private static long FindGdfxHeader(IsoImage iso, bool quiet)
    {
        const int chunk = 1 << 26; // 64 MiB
        byte[] buf = new byte[chunk];
        long off = 0;
        while (off < iso.Length)
        {
            int n = (int)Math.Min((long)chunk, iso.Length - off);
            iso.Read(off, buf, 0, n);
            var span = buf.AsSpan(0, n);
            int i = span.IndexOf(GdfxMagic);
            if (i >= 0)
                return off + i;
            off += n - GdfxMagic.Length; // overlap
            if (!quiet)
                Console.Write($"\r    scanning for GDFX header: {off / (1024 * 1024)} / {iso.Length / (1024 * 1024)} MiB   ");
        }
        if (!quiet) Console.WriteLine();
        return -1;
    }

    // ------------------------------------------------------------------
    //  Listing
    // ------------------------------------------------------------------

    private static int ListDisc(Options o)
    {
        if (!File.Exists(o.IsoPath))
            throw new OptionException($"ISO not found: {o.IsoPath}");
        using var iso = new IsoImage(o.IsoPath);

        if (!o.Quiet)
            Console.WriteLine($"Scanning {o.IsoPath} ({iso.Length / (1024 * 1024):N0} MiB) ...");
        long hdrOff = FindGdfxHeader(iso, o.Quiet);
        if (hdrOff < 0)
            throw new InvalidDataException("GDFX header not found in ISO");

        var gdfx = new GdfxFilesystem(iso, hdrOff);
        if (!o.Quiet)
        {
            Console.WriteLine($"\nGDFX header at {hdrOff:X8}  (base {gdfx.BaseOffset:X8})");
            Console.WriteLine($"Root sector: {gdfx.RootSector:X8}  size: {gdfx.RootSize}");
            Console.WriteLine();
        }

        var root = gdfx.ReadDirectory(gdfx.RootSector, gdfx.RootSize);
        PrintTree(gdfx, root, "", o);
        return 0;
    }

    private static void PrintTree(GdfxFilesystem gdfx, List<GdfxEntry> entries, string prefix, Options o)
    {
        foreach (var e in entries)
        {
            string icon = e.IsDirectory ? "dir " : "file";
            string sizeStr = e.IsDirectory ? "" : $"  ({e.Size:N0})";
            Console.WriteLine($"  {prefix}{icon} {e.Name}{sizeStr}");

            if (e.IsDirectory && e.Size > 0)
            {
                var sub = gdfx.ReadDirectory(e.Sector, e.Size);
                PrintTree(gdfx, sub, prefix + "    ", o);
            }
        }
    }

    // ------------------------------------------------------------------
    //  Extraction
    // ------------------------------------------------------------------

    private static int Extract(Options o)
    {
        if (!File.Exists(o.IsoPath))
            throw new OptionException($"ISO not found: {o.IsoPath}");
        Directory.CreateDirectory(o.OutDir);
        using var iso = new IsoImage(o.IsoPath);

        if (!o.Quiet)
            Console.WriteLine($"Scanning {o.IsoPath} ({iso.Length / (1024 * 1024):N0} MiB) ...");
        long hdrOff = FindGdfxHeader(iso, o.Quiet);
        if (hdrOff < 0)
            throw new InvalidDataException("GDFX header not found in ISO");

        var gdfx = new GdfxFilesystem(iso, hdrOff);
        var root = gdfx.ReadDirectory(gdfx.RootSector, gdfx.RootSize);

        // Filter root entries if --files was specified
        List<GdfxEntry> toExtract;
        if (o.Files.Count > 0)
        {
            toExtract = root.Where(e =>
                o.Files.Any(f => string.Equals(f, e.Name, StringComparison.OrdinalIgnoreCase))
            ).ToList();
            if (toExtract.Count == 0)
                throw new InvalidDataException($"None of the requested files found: {string.Join(", ", o.Files)}");
        }
        else
        {
            toExtract = root;
        }

        double startTs = Environment.TickCount64;
        ulong totalBytes = 0;
        int nFiles = 0;

        foreach (var entry in toExtract)
        {
            if (entry.IsDirectory)
            {
                if (!o.Quiet)
                    Console.WriteLine($"  [dir]  {entry.Name}/");
                var sub = gdfx.ReadDirectory(entry.Sector, entry.Size);
                ExtractDirectory(gdfx, sub, Path.Combine(o.OutDir, SanitizeName(entry.Name)), o, ref totalBytes, ref nFiles);
            }
            else
            {
                // STFS containers (e.g. nxeart) are copied raw by default - that
                // matches how retail extraction tools present the GDFX tree.  With
                // --unpack-stfs they are expanded into a directory of the same name.
                if (o.UnpackStfs && StfsContainer.TryOpen(iso, gdfx.SectorToOffset(entry.Sector), out var container))
                {
                    string destDir = Path.Combine(o.OutDir, SanitizeName(entry.Name));
                    if (!o.Quiet)
                        Console.WriteLine($"  [stfs] {entry.Name}/  ({container.Entries.Count} entries)");
                    ExtractStfs(container, destDir, o, ref totalBytes, ref nFiles);
                }
                else
                {
                    string path = Path.Combine(o.OutDir, SanitizeName(entry.Name));
                    if (!o.Quiet)
                        Console.Write($"  {entry.Name}  ({entry.Size:N0}) ... ");
                    WriteFile(gdfx, entry, path, o);
                    if (!o.Quiet)
                        Console.WriteLine("ok");
                    totalBytes += entry.Size;
                    nFiles++;
                }
            }
        }

        double secs = (Environment.TickCount64 - startTs) / 1000.0;
        if (!o.Quiet)
            Console.WriteLine($"Done: {nFiles} files, {totalBytes / (1024 * 1024):N0} MiB in {secs:F1}s -> {Path.GetFullPath(o.OutDir)}");
        return 0;
    }

    private static void ExtractDirectory(
        GdfxFilesystem gdfx, List<GdfxEntry> entries, string destDir,
        Options o, ref ulong totalBytes, ref int nFiles)
    {
        Directory.CreateDirectory(destDir);

        foreach (var entry in entries)
        {
            if (entry.IsDirectory)
            {
                if (!o.Quiet)
                    Console.WriteLine($"    [dir]  {entry.Name}/");
                var sub = gdfx.ReadDirectory(entry.Sector, entry.Size);
                ExtractDirectory(gdfx, sub, Path.Combine(destDir, SanitizeName(entry.Name)), o, ref totalBytes, ref nFiles);
            }
            else
            {
                // Nested files are always copied raw (e.g. $SystemUpdate/su20076000_00000000
                // stays a PIRS blob). Only root-level entries are unpacked as STFS.
                string path = Path.Combine(destDir, SanitizeName(entry.Name));
                if (!o.Quiet)
                    Console.Write($"    {entry.Name}  ({entry.Size:N0}) ... ");
                WriteFile(gdfx, entry, path, o);
                if (!o.Quiet)
                    Console.WriteLine("ok");
                totalBytes += entry.Size;
                nFiles++;
            }
        }
    }

    private static void ExtractStfs(
        StfsContainer container, string destDir,
        Options o, ref ulong totalBytes, ref int nFiles)
    {
        Directory.CreateDirectory(destDir);

        foreach (var e in container.Entries)
        {
            if (e.IsDirectory) continue;
            string rel = container.GetPath(e);
            string path = SafePath(Path.Combine(destDir, rel));
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);

            if (!o.Quiet)
                Console.Write($"      {rel}  ({e.FileSize:N0}) ... ");

            using (var fs = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.Read))
            {
                fs.SetLength((long)e.FileSize);
                long pos = 0;
                container.StreamFile(e, (b, off, cnt) =>
                {
                    fs.Write(b, off, cnt);
                    pos += cnt;
                });
            }

            if (!o.Quiet)
                Console.WriteLine("ok");
            totalBytes += e.FileSize;
            nFiles++;
        }
    }

    private static void WriteFile(GdfxFilesystem gdfx, GdfxEntry entry, string path, Options o)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var fs = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.Read);
        fs.SetLength(entry.Size);
        gdfx.ReadFile(entry.Sector, entry.Size, (b, off, cnt) => fs.Write(b, off, cnt));
    }

    // ------------------------------------------------------------------
    //  Helpers
    // ------------------------------------------------------------------

    private static string SanitizeName(string name)
    {
        char[] bad = Path.GetInvalidFileNameChars();
        var sb = new StringBuilder(name.Length);
        foreach (char ch in name)
            sb.Append(Array.IndexOf(bad, ch) >= 0 ? '_' : ch);
        string s = sb.ToString().Trim().TrimEnd('.');
        return s.Length == 0 ? "_" : s;
    }

    private static string SafePath(string path)
    {
        path = path.Replace('/', Path.DirectorySeparatorChar).Replace('\\', Path.DirectorySeparatorChar);
        var full = Path.GetFullPath(path);
        if (full.StartsWith("..", StringComparison.Ordinal) || full.Length == 0)
            throw new InvalidDataException($"unsafe path: {path}");
        return full;
    }
}
