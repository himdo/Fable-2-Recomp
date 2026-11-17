using System.Diagnostics;
using System.Text;

namespace X360Extract;

/// <summary>
/// A phase reported through the <see cref="DiscProgress"/> callback during a
/// scan or extraction.
/// </summary>
public enum DiscProgressPhase
{
    /// <summary>Scanning the image for the GDFX header. <see cref="DiscProgress.Current"/>/<see cref="DiscProgress.Total"/> are bytes scanned / image length.</summary>
    ScanningHeader,
    /// <summary>Beginning a GDFX directory. <see cref="DiscProgress.FileCount"/> is the number of files in that subtree.</summary>
    EnteringDirectory,
    /// <summary>Beginning to write a file (or one file inside an STFS container).</summary>
    WritingFile,
    /// <summary>Finished writing a file.</summary>
    FileWritten,
    /// <summary>Expanding an STFS container into a directory. <see cref="DiscProgress.EntryCount"/> is the number of STFS entries.</summary>
    ExpandingStfs,
}

/// <summary>
/// Structured progress reported by <see cref="X360Disc"/>. Consumers format it
/// however they like (console lines, UI status text, etc.).
/// </summary>
public readonly struct DiscProgress
{
    public readonly DiscProgressPhase Phase;

    /// <summary>Path of the item relative to the extraction root (files/dirs, '/'-separated), or null while scanning.</summary>
    public readonly string? Name;

    /// <summary>Declared size of the file being written (0 for non-file phases).</summary>
    public readonly long SizeBytes;

    /// <summary>Bytes scanned so far (only <see cref="DiscProgressPhase.ScanningHeader"/>).</summary>
    public readonly long Current;

    /// <summary>Total image length in bytes (only <see cref="DiscProgressPhase.ScanningHeader"/>).</summary>
    public readonly long Total;

    /// <summary>Files in a directory subtree (only <see cref="DiscProgressPhase.EnteringDirectory"/>).</summary>
    public readonly int FileCount;

    /// <summary>Entries in an STFS container (only <see cref="DiscProgressPhase.ExpandingStfs"/>).</summary>
    public readonly int EntryCount;

    internal DiscProgress(DiscProgressPhase phase, string? name, long sizeBytes,
        long current, long total, int fileCount, int entryCount)
    {
        Phase = phase;
        Name = name;
        SizeBytes = sizeBytes;
        Current = current;
        Total = total;
        FileCount = fileCount;
        EntryCount = entryCount;
    }

    internal static DiscProgress Header(long current, long total)
        => new(DiscProgressPhase.ScanningHeader, null, 0, current, total, 0, 0);

    internal static DiscProgress Directory(string name, int fileCount)
        => new(DiscProgressPhase.EnteringDirectory, name, 0, 0, 0, fileCount, 0);

    internal static DiscProgress FileStart(string name, long size)
        => new(DiscProgressPhase.WritingFile, name, size, 0, 0, 0, 0);

    internal static DiscProgress FileDone(string name, long size)
        => new(DiscProgressPhase.FileWritten, name, size, 0, 0, 0, 0);

    internal static DiscProgress Stfs(string name, int entryCount)
        => new(DiscProgressPhase.ExpandingStfs, name, 0, 0, 0, 0, entryCount);
}

/// <summary>
/// Options that control how <see cref="X360Disc.Extract"/> writes a disc to disk.
/// </summary>
public sealed class ExtractionOptions
{
    /// <summary>
    /// Only extract root entries matching these names (case-insensitive).
    /// Empty (the default) extracts the whole root.
    /// </summary>
    public IReadOnlyList<string> Files { get; init; } = Array.Empty<string>();

    /// <summary>
    /// When true, root-level STFS containers (e.g. <c>nxeart</c>) are expanded into a
    /// directory of the same name. When false (the default) they are copied as a single
    /// raw file, matching retail extraction tools. Nested files are always copied raw.
    /// </summary>
    public bool UnpackStfs { get; init; }

    /// <summary>Optional progress callback. Invoked on the calling thread.</summary>
    public Action<DiscProgress>? Progress { get; init; }

    /// <summary>
    /// Optional per-chunk progress while writing any file (root or nested). Invoked
    /// on the writing thread, roughly once per 1 MiB chunk, with the file's relative
    /// path, bytes written so far in that file, and the file's declared size. Lets a
    /// UI advance a progress bar smoothly inside a single large file.
    /// </summary>
    public Action<string, long, long>? FileChunk { get; init; }
}

/// <summary>Outcome of an extraction: files written, bytes written, elapsed time.</summary>
public readonly record struct ExtractionResult(int Files, long Bytes, TimeSpan Elapsed);

/// <summary>
/// An open Xbox 360 disc image (GDFX on ISO 9660) plus a high-level API to read and
/// extract it. This is the main entry point for consuming the library.
///
/// Typical use:
/// <code>
/// using X360Extract;
/// using var disc = X360Disc.Open("game.iso");
/// long bytes = disc.WriteFile(disc.DefaultXex.Value, "default.xex");
/// var result = disc.Extract("out");
/// </code>
/// </summary>
public sealed class X360Disc : IDisposable
{
    private static readonly byte[] GdfxMagic = "MICROSOFT*XBOX*MEDIA"u8.ToArray();

    private readonly IsoImage _iso;

    private X360Disc(string path, IsoImage iso, GdfxFilesystem gdfx, List<GdfxEntry> root)
    {
        ImagePath = path;
        _iso = iso;
        Gdfx = gdfx;
        RootEntries = root;
    }

    /// <summary>The disc image path this disc was opened from.</summary>
    public string ImagePath { get; }

    /// <summary>Low-level ISO 9660 access (for advanced/STFS scanning).</summary>
    public IsoImage Iso => _iso;

    /// <summary>Byte offset of the GDFX header within the image.</summary>
    public long HeaderOffset => Gdfx.HeaderOffset;

    /// <summary>The parsed GDFX filesystem.</summary>
    public GdfxFilesystem Gdfx { get; }

    /// <summary>The root directory entries of the disc.</summary>
    public IReadOnlyList<GdfxEntry> RootEntries { get; }

    /// <summary>The root-level <c>default.xex</c> entry, if this disc has one.</summary>
    public GdfxEntry? DefaultXex
    {
        get
        {
            foreach (GdfxEntry entry in RootEntries)
            {
                if (!entry.IsDirectory &&
                    string.Equals(entry.Name, "default.xex", StringComparison.OrdinalIgnoreCase))
                    return entry;
            }
            return null;
        }
    }

    /// <summary>
    /// Scan the image for the GDFX header, open the filesystem, and read the root
    /// directory. The returned disc must be <see cref="IDisposable.Dispose"/>d.
    /// </summary>
    public static X360Disc Open(string path, Action<DiscProgress>? progress = null)
    {
        if (!File.Exists(path))
            throw new FileNotFoundException("Disc image not found.", path);

        var iso = new IsoImage(path);
        try
        {
            long headerOffset = ScanForGdfxHeader(iso, progress);
            var gdfx = new GdfxFilesystem(iso, headerOffset);
            return new X360Disc(path, iso, gdfx, gdfx.ReadDirectory(gdfx.RootSector, gdfx.RootSize));
        }
        catch
        {
            iso.Dispose();
            throw;
        }
    }

    /// <summary>
    /// Scan an image for the GDFX header ("MICROSOFT*XBOX*MEDIA" magic) in 64 MiB
    /// chunks with overlap. Returns the header offset, or throws if not found.
    /// </summary>
    public static long ScanForGdfxHeader(IsoImage iso, Action<DiscProgress>? progress = null)
    {
        const int chunk = 1 << 26; // 64 MiB
        var buffer = new byte[chunk];
        long offset = 0;
        while (offset < iso.Length)
        {
            int n = (int)Math.Min((long)chunk, iso.Length - offset);
            iso.Read(offset, buffer, 0, n);
            int found = buffer.AsSpan(0, n).IndexOf(GdfxMagic);
            if (found >= 0)
                return offset + found;
            offset += n - GdfxMagic.Length; // overlap the window seam
            progress?.Invoke(DiscProgress.Header(offset, iso.Length));
        }
        throw new InvalidDataException(
            "GDFX header (MICROSOFT*XBOX*MEDIA) not found — is this a complete raw Xbox 360 disc image?");
    }

    /// <summary>
    /// Extract the disc to <paramref name="outDir"/>. Root entries are filtered by
    /// <see cref="ExtractionOptions.Files"/> when set.
    /// </summary>
    public ExtractionResult Extract(string outDir, ExtractionOptions? options = null)
    {
        options ??= new ExtractionOptions();
        Directory.CreateDirectory(outDir);

        IEnumerable<GdfxEntry> entries = RootEntries;
        if (options.Files is { Count: > 0 } files)
        {
            entries = RootEntries
                .Where(e => files.Any(f => string.Equals(f, e.Name, StringComparison.OrdinalIgnoreCase)))
                .ToList();
            if (!entries.Any())
                throw new InvalidDataException(
                    $"None of the requested root entries found: {string.Join(", ", files)}");
        }

        return ExtractEntries(entries, outDir, options);
    }

    /// <summary>
    /// Extract an explicit set of entries (usually a subset of the root) to
    /// <paramref name="outDir"/>. This is the flexible primitive behind
    /// <see cref="Extract"/>; e.g. pass everything except <c>default.xex</c> to
    /// extract "the rest of the disc".
    /// </summary>
    public ExtractionResult ExtractEntries(IEnumerable<GdfxEntry> entries, string outDir,
        ExtractionOptions? options = null)
    {
        options ??= new ExtractionOptions();
        Directory.CreateDirectory(outDir);

        var sw = Stopwatch.StartNew();
        int files = 0;
        long bytes = 0;

        foreach (GdfxEntry entry in entries)
        {
            if (entry.IsDirectory)
            {
                List<GdfxEntry> sub = Gdfx.ReadDirectory(entry.Sector, entry.Size);
                options.Progress?.Invoke(DiscProgress.Directory(entry.Name, CountFiles(sub)));
                ExtractDirectory(sub, Path.Combine(outDir, SanitizeFileName(entry.Name)), entry.Name, options,
                    ref files, ref bytes);
            }
            else
            {
                // Only root-level entries are candidates for STFS expansion.
                if (options.UnpackStfs &&
                    StfsContainer.TryOpen(_iso, Gdfx.SectorToOffset(entry.Sector), out StfsContainer container))
                {
                    options.Progress?.Invoke(DiscProgress.Stfs(entry.Name, container.Entries.Count));
                    ExtractStfs(container, Path.Combine(outDir, SanitizeFileName(entry.Name)), entry.Name, options,
                        ref files, ref bytes);
                }
                else
                {
                    WriteEntry(entry, Path.Combine(outDir, SanitizeFileName(entry.Name)), entry.Name, options,
                        ref files, ref bytes);
                }
            }
        }

        sw.Stop();
        return new ExtractionResult(files, bytes, sw.Elapsed);
    }

    /// <summary>
    /// Write a single file entry to <paramref name="path"/>, streaming the bytes in
    /// 1 MiB chunks. When <paramref name="onBytes"/> is given, every chunk is also
    /// passed to it (e.g. to hash the stream). Returns the declared file size.
    /// </summary>
    public long WriteFile(GdfxEntry entry, string path, Action<byte[], int, int>? onBytes = null,
        Action<long, long>? onChunk = null)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using FileStream fs = new(path, FileMode.Create, FileAccess.Write, FileShare.Read);
        fs.SetLength(entry.Size);
        long written = 0;
        Gdfx.ReadFile(entry.Sector, entry.Size, (b, off, cnt) =>
        {
            fs.Write(b, off, cnt);
            onBytes?.Invoke(b, off, cnt);
            written += cnt;
            onChunk?.Invoke(written, entry.Size);
        });
        return entry.Size;
    }

    /// <summary>
    /// Render the GDFX tree as an indented, human-readable string (like the CLI's
    /// <c>--list</c>).
    /// </summary>
    public string ListTree()
    {
        var sb = new StringBuilder();
        PrintTree(RootEntries, "", sb);
        return sb.ToString();
    }

    private void PrintTree(IReadOnlyList<GdfxEntry> entries, string prefix, StringBuilder sb)
    {
        foreach (GdfxEntry entry in entries)
        {
            string icon = entry.IsDirectory ? "dir " : "file";
            string size = entry.IsDirectory ? "" : $"  ({entry.Size:N0})";
            sb.Append(prefix).Append(icon).Append(' ').Append(entry.Name).AppendLine(size);
            if (entry.IsDirectory && entry.Size > 0)
                PrintTree(Gdfx.ReadDirectory(entry.Sector, entry.Size), prefix + "    ", sb);
        }
    }

    public void Dispose() => _iso.Dispose();

    /// <summary>
    /// Make a filesystem-safe name: invalid filename characters become <c>_</c>,
    /// leading/trailing spaces are trimmed, and a trailing dot is removed.
    /// </summary>
    public static string SanitizeFileName(string name)
    {
        char[] invalid = Path.GetInvalidFileNameChars();
        var sb = new StringBuilder(name.Length);
        foreach (char ch in name)
            sb.Append(Array.IndexOf(invalid, ch) >= 0 ? '_' : ch);
        string s = sb.ToString().Trim().TrimEnd('.');
        return s.Length == 0 ? "_" : s;
    }

    /// <summary>
    /// Resolve a path read from a disc image, rejecting anything outside <paramref name="root"/>.
    /// </summary>
    public static string ResolveInside(string root, string relativePath)
    {
        string fullRoot = Path.GetFullPath(root);
        if (!Path.EndsInDirectorySeparator(fullRoot))
            fullRoot += Path.DirectorySeparatorChar;
        string normalized = relativePath.Replace('/', Path.DirectorySeparatorChar).Replace('\\', Path.DirectorySeparatorChar);
        string full = Path.GetFullPath(Path.Combine(fullRoot, normalized));
        if (!full.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException($"unsafe path: {relativePath}");
        return full;
    }

    // ------------------------------------------------------------------
    //  Internals
    // ------------------------------------------------------------------

    private void ExtractDirectory(List<GdfxEntry> entries, string destDir, string relBase,
        ExtractionOptions options, ref int files, ref long bytes)
    {
        Directory.CreateDirectory(destDir);
        foreach (GdfxEntry entry in entries)
        {
            if (entry.IsDirectory)
            {
                List<GdfxEntry> sub = Gdfx.ReadDirectory(entry.Sector, entry.Size);
                string childBase = relBase + "/" + SanitizeFileName(entry.Name);
                options.Progress?.Invoke(DiscProgress.Directory(childBase, CountFiles(sub)));
                ExtractDirectory(sub, Path.Combine(destDir, SanitizeFileName(entry.Name)), childBase, options,
                    ref files, ref bytes);
            }
            else
            {
                // Nested files are always copied raw (no STFS expansion).
                string rel = relBase + "/" + SanitizeFileName(entry.Name);
                WriteEntry(entry, Path.Combine(destDir, SanitizeFileName(entry.Name)), rel, options,
                    ref files, ref bytes);
            }
        }
    }

    private void ExtractStfs(StfsContainer container, string destDir, string relBase,
        ExtractionOptions options, ref int files, ref long bytes)
    {
        Directory.CreateDirectory(destDir);
        foreach (StfsEntry e in container.Entries)
        {
            if (e.IsDirectory) continue;
            string rel = relBase + "/" + container.GetPath(e);
            string path = ResolveInside(destDir, container.GetPath(e));
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);

            long size = (long)e.FileSize;
            options.Progress?.Invoke(DiscProgress.FileStart(rel, size));
            long written = 0;
            using (FileStream fs = new(path, FileMode.Create, FileAccess.Write, FileShare.Read))
            {
                fs.SetLength(size);
                container.StreamFile(e, (b, off, cnt) =>
                {
                    fs.Write(b, off, cnt);
                    written += cnt;
                    options.FileChunk?.Invoke(rel, written, size);
                });
            }
            options.Progress?.Invoke(DiscProgress.FileDone(rel, size));
            files++;
            bytes += size;
        }
    }

    private void WriteEntry(GdfxEntry entry, string path, string rel, ExtractionOptions options,
        ref int files, ref long bytes)
    {
        options.Progress?.Invoke(DiscProgress.FileStart(rel, entry.Size));
        WriteFile(entry, path, null,
            (written, total) => options.FileChunk?.Invoke(rel, written, total));
        options.Progress?.Invoke(DiscProgress.FileDone(rel, entry.Size));
        files++;
        bytes += entry.Size;
    }

    private int CountFiles(IReadOnlyList<GdfxEntry> entries)
    {
        int count = 0;
        foreach (GdfxEntry entry in entries)
            count += entry.IsDirectory
                ? CountFiles(Gdfx.ReadDirectory(entry.Sector, entry.Size))
                : 1;
        return count;
    }
}
