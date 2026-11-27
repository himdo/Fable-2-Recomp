using System.IO;
using System.IO.Compression;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace Fable2Launcher;

// In-place upgrade: replaces a packaged release's files with those of a newer release,
// in the folder the launcher runs from.
//
// What it promises:
//   - Only files that belong to a release are written or removed. Saves, settings, logs
//     and the extracted game are never overwritten, also if a future zip contains them.
//   - Nothing in the folder changes until the whole download has been checked: its size
//     and SHA-256 against the GitHub release, each file against the list in the zip.
//   - The switch itself only renames files, keeps every old one until the end and is
//     undone when a step fails. If the launcher dies half-way, the next start undoes it.
//
// This code keeps running in installed launchers long after it was written, and it is
// the old launcher that installs the new release. So it is strict about what it accepts
// and refuses a layout it does not know; the update button then opens the release page.

// A failure the player can act on. The message is shown as it is.
public sealed class UpgradeException(string message, Exception? inner = null) : Exception(message, inner);

public enum UpgradePhase { Downloading, Verifying, Extracting, Checking, Installing }

public sealed record UpgradeProgress(UpgradePhase Phase, long Done = 0, long Total = 0);

public enum UpgradeAction { Add, Replace, Remove, Unchanged, KeepUserFile }

// Modified: a release file the player had changed since it was installed.
public sealed record PlannedFile(string Path, UpgradeAction Action, bool Modified = false);

// KeptFolder holds the previous copies of the files the player had changed, if any.
public sealed record UpgradeResult(string Version, IReadOnlyList<PlannedFile> Files, string? KeptFolder);

public enum UpgradeRecovery { None, CleanedUp, RolledBack, Busy, Failed }

public static class UpgradePaths
{
    public const string BuildInfo = "fable2_build.json";
    public const string Launcher = "Fable2Launcher.exe";
    public const string Game = "fable_2.exe";
    public const string WorkFolder = "launcher-update";
    public const string KeptFolder = "launcher-update-backup";
    public const string LockFile = "launcher-update.lock";

    // Written next to the exe by the game, the launcher or the disc extraction. A name
    // counts whether it is a folder or a file: the disc's nxeart is a file.
    private static readonly string[] UserRoots =
        ["saves", "cache", "logs", "screenshots", "nxeart", "$SystemUpdate", "launcher-game-path.txt", "default.xex"];
    private static readonly string[] BackupSuffixes = [".launcher-backup", ".bak"];
    // The one part of data/ that comes from a release instead of the disc.
    private const string ReleaseScripts = "data/scripts/recomp/";
    private static readonly string[] DeviceNames =
        ["CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
         "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"];

    // A path from a zip or a file list, as "folder/file". Throws for anything that could
    // land outside the install folder or under another name than the one written here:
    // "..", absolute paths, drive letters and NTFS streams (":"), device names, and the
    // trailing dots and spaces Windows drops.
    public static string Normalize(string name)
    {
        string path = name.Replace('\\', '/');
        if (path.Length == 0 || path.Length > 240) throw Unsafe(name);
        foreach (string segment in path.Split('/'))
        {
            if (segment.Length == 0 || segment is "." or ".." || segment[0] == ' ' || segment[^1] is '.' or ' ' ||
                segment.Any(c => c < ' ' || "<>:\"|?*".Contains(c)) ||
                DeviceNames.Contains(segment.Split('.')[0], StringComparer.OrdinalIgnoreCase))
                throw Unsafe(name);
        }
        return path;
    }

    private static UpgradeException Unsafe(string name) =>
        new($"The release contains a file name that is not safe to install: \"{name}\".");

    // The player's own files. An upgrade installs one only when it is missing and never
    // overwrites or removes it. Root *.toml files are settings: fable2_config.toml,
    // fable_2.toml and launcher-settings.toml today.
    public static bool IsUserOwned(string path)
    {
        string[] segments = path.Split('/');
        if (BackupSuffixes.Any(suffix => path.EndsWith(suffix, StringComparison.OrdinalIgnoreCase))) return true;
        if (UserRoots.Contains(segments[0], StringComparer.OrdinalIgnoreCase)) return true;
        if (segments.Length == 1) return path.EndsWith(".toml", StringComparison.OrdinalIgnoreCase);
        return segments[0].Equals("data", StringComparison.OrdinalIgnoreCase) &&
               !path.StartsWith(ReleaseScripts, StringComparison.OrdinalIgnoreCase);
    }

    // The upgrade's own working files. A release may not contain them.
    public static bool IsReserved(string path)
    {
        string first = path.Split('/')[0];
        return first.Equals(WorkFolder, StringComparison.OrdinalIgnoreCase) ||
               first.Equals(KeptFolder, StringComparison.OrdinalIgnoreCase) ||
               first.Equals(LockFile, StringComparison.OrdinalIgnoreCase);
    }
}

// The file list tools/package_release.py writes into a release's fable2_build.json.
public sealed class ReleaseManifest
{
    // Must match UPGRADE_FORMAT in tools/package_release.py.
    public const int SupportedFormat = 1;

    public required string Version { get; init; }
    // Every file of the release except fable2_build.json itself, with its SHA-256.
    public required IReadOnlyDictionary<string, string> Files { get; init; }
    // Files the release marks as the player's, on top of UpgradePaths.IsUserOwned.
    public required IReadOnlySet<string> UserFiles { get; init; }

    // Null when the file has no list: a local build, or a release from before 1.7.
    public static ReleaseManifest? Parse(string json)
    {
        using JsonDocument document = JsonDocument.Parse(json);
        JsonElement root = document.RootElement;
        if (root.ValueKind != JsonValueKind.Object ||
            !root.TryGetProperty("release", out JsonElement release) || release.ValueKind != JsonValueKind.Object)
            return null;
        if (!release.TryGetProperty("format", out JsonElement format) || !format.TryGetInt32(out int number) ||
            number != SupportedFormat)
            throw new UpgradeException("This release is packaged in a way this launcher cannot install by itself.");
        if (!root.TryGetProperty("version", out JsonElement version) || version.ValueKind != JsonValueKind.String ||
            !release.TryGetProperty("files", out JsonElement files) || files.ValueKind != JsonValueKind.Object)
            throw new UpgradeException("The release's file list is incomplete.");

        var hashes = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (JsonProperty file in files.EnumerateObject())
        {
            string? hash = file.Value.ValueKind == JsonValueKind.String ? file.Value.GetString() : null;
            if (hash is null || hash.Length != 64 || !hash.All(Uri.IsHexDigit) ||
                !hashes.TryAdd(UpgradePaths.Normalize(file.Name), hash.ToLowerInvariant()))
                throw new UpgradeException($"The release's file list has a bad entry for \"{file.Name}\".");
        }
        var userFiles = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        if (release.TryGetProperty("user_files", out JsonElement marked) && marked.ValueKind == JsonValueKind.Array)
            foreach (JsonElement item in marked.EnumerateArray())
                if (item.ValueKind == JsonValueKind.String) userFiles.Add(UpgradePaths.Normalize(item.GetString()!));
        return new ReleaseManifest { Version = version.GetString()!, Files = hashes, UserFiles = userFiles };
    }

    // The list of the release that is installed now. Without one the upgrade still works;
    // it then can't tell which old files to remove or which ones the player changed.
    public static ReleaseManifest? ReadInstalled(string folder)
    {
        try { return Parse(File.ReadAllText(Path.Combine(folder, UpgradePaths.BuildInfo))); }
        catch (Exception exception) when (exception is IOException or JsonException or UnauthorizedAccessException
                                              or UpgradeException)
        {
            return null;
        }
    }
}

public sealed record StagedFile(string Hash, long Size);

// A release unpacked into the working folder, every file checked against its list.
public sealed record StagedRelease(ReleaseManifest Manifest, IReadOnlyDictionary<string, StagedFile> Files);

public sealed class ReleaseUpgrader
{
    public const int MaxEntries = 10_000;
    public const long MaxUnpackedBytes = 4L << 30;
    private const long SpaceMargin = 64L << 20;
    private const string Applying = "applying";
    private const string Committed = "committed";

    private readonly string _install;
    private readonly string _work;
    private readonly string _staged;
    private readonly string _backup;
    private readonly string _trash;
    private readonly string _journal;
    private readonly Func<string, long?> _freeSpace;

    // A download that sends nothing for this long has failed.
    public TimeSpan StallTimeout { get; init; } = TimeSpan.FromSeconds(60);
    // Called before each file is switched. The tests use it to fail or die half-way.
    public Action<PlannedFile>? Installing { get; init; }

    public ReleaseUpgrader(string installDirectory, Func<string, long?>? freeSpace = null)
    {
        _install = Path.GetFullPath(installDirectory);
        _work = Path.Combine(_install, UpgradePaths.WorkFolder);
        _staged = Path.Combine(_work, "staged");
        _backup = Path.Combine(_work, "backup");
        _trash = Path.Combine(_work, "trash");
        _journal = Path.Combine(_work, "journal.json");
        _freeSpace = freeSpace ?? FreeSpace;
    }

    // Run when the launcher starts: removes what a finished upgrade left behind (the old
    // launcher can't delete itself while it runs) and undoes one that was cut short.
    public static UpgradeRecovery Recover(string installDirectory)
    {
        var upgrader = new ReleaseUpgrader(installDirectory);
        if (!Directory.Exists(upgrader._work)) return UpgradeRecovery.None;
        FileStream held;
        // Another launcher window is upgrading right now: its files are not leftovers.
        try { held = upgrader.Lock(); }
        catch (UpgradeException) { return UpgradeRecovery.Busy; }
        using (held)
        {
            try { return upgrader.RecoverLocked(); }
            catch (Exception exception) when (exception is UpgradeException or IOException or UnauthorizedAccessException)
            {
                return UpgradeRecovery.Failed;
            }
        }
    }

    public async Task<UpgradeResult> RunAsync(AvailableUpdate update,
        Func<string, CancellationToken, Task<Stream>> openDownload, IProgress<UpgradeProgress>? progress,
        CancellationToken cancel)
    {
        ReleasePackage package = update.Package
            ?? throw new UpgradeException("This release has to be installed by hand from its page.");
        string installed = UpdateChecker.ReadInstalledVersion(_install)
            ?? throw new UpgradeException("This folder does not hold a packaged release, so there is nothing to upgrade.");

        using FileStream held = Lock();
        try
        {
            RecoverLocked();
            // Old files left here would be put back by a later undo as if they were this upgrade's.
            if (Directory.Exists(_work))
                throw new UpgradeException("Files of an earlier update are still in use. Close every other " +
                                           "launcher window and try again.");
            Directory.CreateDirectory(_work);
            // The zip, plus its unpacked files next to it. Releases unpack to under three times their size.
            RequireSpace(package.Size * 4);
            string zip = await DownloadAsync(package, openDownload, progress, cancel);
            return await Task.Run(() =>
            {
                StagedRelease staged = Stage(zip, update.Version, installed, progress, cancel);
                File.Delete(zip);
                IReadOnlyList<PlannedFile> plan = Plan(staged, progress, cancel);
                // Last chance to stop. From here on the upgrade finishes or is undone.
                cancel.ThrowIfCancellationRequested();
                progress?.Report(new UpgradeProgress(UpgradePhase.Installing));
                return Apply(staged, plan, installed);
            }, CancellationToken.None);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException
                                              or InvalidDataException or HttpRequestException or JsonException)
        {
            throw new UpgradeException(Describe(exception), exception);
        }
        finally
        {
            // A journal that is still there means an undo failed: keep the old files for the next start.
            if (!File.Exists(_journal) || ReadJournalState() == Committed) DeleteBestEffort(_work);
        }
    }

    private static string Describe(Exception exception) => exception switch
    {
        HttpRequestException => "The download failed: " + exception.Message,
        InvalidDataException => "The downloaded file is damaged: " + exception.Message,
        JsonException => "The release's file list can't be read.",
        UnauthorizedAccessException => "Windows refused access to the launcher's folder: " + exception.Message,
        _ => exception.Message,
    };

    // ---- Lock and leftovers ----

    // One upgrade per folder at a time. The file disappears when it is closed.
    private FileStream Lock()
    {
        try
        {
            return new FileStream(Path.Combine(_install, UpgradePaths.LockFile), FileMode.OpenOrCreate,
                FileAccess.ReadWrite, FileShare.None, 1, FileOptions.DeleteOnClose);
        }
        catch (UnauthorizedAccessException exception)
        {
            throw new UpgradeException("The launcher's folder can't be written to, so it can't be upgraded in place. " +
                                       "Move it out of a protected folder, or install the release by hand.", exception);
        }
        catch (IOException exception)
        {
            throw new UpgradeException("Another launcher window is already updating this folder.", exception);
        }
    }

    private UpgradeRecovery RecoverLocked()
    {
        if (!Directory.Exists(_work)) return UpgradeRecovery.None;
        if (!File.Exists(_journal))
        {
            DeleteBestEffort(_work);
            return UpgradeRecovery.CleanedUp;
        }
        (string state, string[] added) = ReadJournal();
        if (state != Applying)
        {
            DeleteBestEffort(_work);
            return UpgradeRecovery.CleanedUp;
        }
        Rollback(added);
        File.Delete(_journal);
        DeleteBestEffort(_work);
        return UpgradeRecovery.RolledBack;
    }

    // ---- Download ----

    private void RequireSpace(long bytes)
    {
        long? free = _freeSpace(_install);
        if (free is long available && available < bytes + SpaceMargin)
            throw new UpgradeException($"Not enough free disk space: the update needs about " +
                                       $"{(bytes + SpaceMargin) / (1 << 20):N0} MB in the launcher's folder.");
    }

    private static long? FreeSpace(string folder)
    {
        try { return new DriveInfo(Path.GetPathRoot(folder)!).AvailableFreeSpace; }
        catch (Exception exception) when (exception is IOException or ArgumentException or UnauthorizedAccessException)
        {
            return null;   // a network share, for example: find out by trying
        }
    }

    private async Task<string> DownloadAsync(ReleasePackage package,
        Func<string, CancellationToken, Task<Stream>> openDownload, IProgress<UpgradeProgress>? progress,
        CancellationToken cancel)
    {
        string zip = Path.Combine(_work, "download.zip");
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        long total = 0;
        progress?.Report(new UpgradeProgress(UpgradePhase.Downloading, 0, package.Size));
        await using (Stream source = await openDownload(package.DownloadUrl, cancel))
        await using (var file = new FileStream(zip, FileMode.Create, FileAccess.Write, FileShare.None, 1 << 16, true))
        {
            byte[] buffer = new byte[1 << 16];
            while (true)
            {
                using var stall = CancellationTokenSource.CreateLinkedTokenSource(cancel);
                stall.CancelAfter(StallTimeout);
                int read;
                try { read = await source.ReadAsync(buffer, stall.Token); }
                catch (OperationCanceledException) when (!cancel.IsCancellationRequested)
                {
                    throw new UpgradeException("The download stopped responding. Check your connection and try again.");
                }
                if (read == 0) break;
                total += read;
                if (total > package.Size)
                    throw new UpgradeException("The download is larger than the release says it is.");
                hash.AppendData(buffer, 0, read);
                await file.WriteAsync(buffer.AsMemory(0, read), cancel);
                Report(progress, UpgradePhase.Downloading, total, package.Size);
            }
        }
        progress?.Report(new UpgradeProgress(UpgradePhase.Verifying));
        if (total != package.Size)
            throw new UpgradeException("The download was cut short. Try again.");
        if (!Convert.ToHexString(hash.GetHashAndReset()).Equals(package.Sha256, StringComparison.OrdinalIgnoreCase))
            throw new UpgradeException("The download does not match the checksum of the release, so it was not installed.");
        return zip;
    }

    // ---- Unpack and check ----

    public StagedRelease Stage(string zipPath, string expectedVersion, string installedVersion,
        IProgress<UpgradeProgress>? progress, CancellationToken cancel)
    {
        using ZipArchive archive = ZipFile.OpenRead(zipPath);
        if (archive.Entries.Count > MaxEntries)
            throw new UpgradeException("The download holds far more files than a release does.");

        // Folder entries carry no data. Release zips have one top folder ("Fable 2 1.6.1/").
        var entries = new Dictionary<string, ZipArchiveEntry>(StringComparer.OrdinalIgnoreCase);
        ZipArchiveEntry[] files = archive.Entries.Where(entry => !entry.FullName.EndsWith('/') &&
                                                                 !entry.FullName.EndsWith('\\')).ToArray();
        string prefix = TopFolder(files.Select(entry => entry.FullName.Replace('\\', '/')).ToArray());
        foreach (ZipArchiveEntry entry in files)
        {
            string path = UpgradePaths.Normalize(entry.FullName.Replace('\\', '/')[prefix.Length..]);
            if (UpgradePaths.IsReserved(path) || !entries.TryAdd(path, entry))
                throw new UpgradeException($"The release contains \"{path}\", which can't be installed.");
        }

        if (!entries.TryGetValue(UpgradePaths.BuildInfo, out ZipArchiveEntry? buildInfo))
            throw new UpgradeException("The download is not a Fable 2 Recompiled release.");
        ReleaseManifest manifest = ReleaseManifest.Parse(ReadText(buildInfo, 4 << 20))
            ?? throw new UpgradeException("This release has no file list, so it has to be installed by hand.");

        Version? packaged = UpdateChecker.ParseVersion(manifest.Version);
        if (packaged is null || packaged != UpdateChecker.ParseVersion(expectedVersion))
            throw new UpgradeException($"The download is version {manifest.Version}, not {expectedVersion}.");
        if (UpdateChecker.ParseVersion(installedVersion) is not Version installed || packaged <= installed)
            throw new UpgradeException($"Version {manifest.Version} is not newer than the installed {installedVersion}.");

        // The zip and its list must name exactly the same files.
        string? unlisted = entries.Keys.FirstOrDefault(path => path != UpgradePaths.BuildInfo &&
                                                               !manifest.Files.ContainsKey(path));
        string? missing = manifest.Files.Keys.FirstOrDefault(path => !entries.ContainsKey(path));
        if (unlisted is not null || missing is not null)
            throw new UpgradeException($"The release's file list does not match its contents (\"{unlisted ?? missing}\").");
        foreach (string required in new[] { UpgradePaths.Game, UpgradePaths.Launcher })
            if (!entries.ContainsKey(required))
                throw new UpgradeException($"The release is missing {required}.");

        long unpacked = entries.Values.Sum(entry => entry.Length);
        if (unpacked > MaxUnpackedBytes)
            throw new UpgradeException("The download unpacks to far more than a release does.");
        RequireSpace(unpacked);

        var staged = new Dictionary<string, StagedFile>(StringComparer.OrdinalIgnoreCase);
        long done = 0;
        byte[] buffer = new byte[1 << 16];
        foreach ((string path, ZipArchiveEntry entry) in entries)
        {
            cancel.ThrowIfCancellationRequested();
            string target = Local(_staged, path);
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
            long written = 0;
            using (Stream source = entry.Open())
            using (var file = new FileStream(target, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                int read;
                while ((read = source.Read(buffer, 0, buffer.Length)) > 0)
                {
                    cancel.ThrowIfCancellationRequested();
                    written += read;
                    // The size in a zip's directory is only a claim.
                    if (written > entry.Length) throw new UpgradeException($"\"{path}\" unpacks to more than the zip says.");
                    hash.AppendData(buffer, 0, read);
                    file.Write(buffer, 0, read);
                    Report(progress, UpgradePhase.Extracting, done + written, unpacked);
                }
                // On disk before it is renamed into place, so a power cut can't leave an empty exe.
                file.Flush(flushToDisk: true);
            }
            done += written;
            string actual = Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
            if (written != entry.Length ||
                (manifest.Files.TryGetValue(path, out string? expected) && expected != actual))
                throw new UpgradeException($"\"{path}\" in the download does not match the release's file list.");
            staged[path] = new StagedFile(actual, written);
        }
        return new StagedRelease(manifest, staged);
    }

    private static string ReadText(ZipArchiveEntry entry, int limit)
    {
        using Stream source = entry.Open();
        using var text = new MemoryStream();
        byte[] buffer = new byte[1 << 14];
        int read;
        while ((read = source.Read(buffer, 0, buffer.Length)) > 0)
        {
            if (text.Length + read > limit) throw new UpgradeException($"\"{entry.Name}\" is far too large.");
            text.Write(buffer, 0, read);
        }
        return Encoding.UTF8.GetString(text.GetBuffer(), 0, (int)text.Length).TrimStart('\uFEFF');
    }

    // "Fable 2 1.6.1/" when every file sits in the same top folder, "" for a flat zip.
    private static string TopFolder(string[] names)
    {
        if (names.Contains(UpgradePaths.BuildInfo)) return "";
        string? folder = names.Select(name => name.Split('/')[0]).Distinct().Take(2).ToArray() is [var only] ? only : null;
        if (folder is null || names.Any(name => !name.StartsWith(folder + "/", StringComparison.Ordinal)))
            throw new UpgradeException("The download is not a Fable 2 Recompiled release.");
        return folder + "/";
    }

    // ---- Decide what happens to each file ----

    public IReadOnlyList<PlannedFile> Plan(StagedRelease staged, IProgress<UpgradeProgress>? progress,
        CancellationToken cancel)
    {
        ReleaseManifest? previous = ReleaseManifest.ReadInstalled(_install);
        var plan = new List<PlannedFile>();
        bool IsUsers(string path) => UpgradePaths.IsUserOwned(path) || staged.Manifest.UserFiles.Contains(path) ||
                                     previous?.UserFiles.Contains(path) == true;

        // Files of the installed release that the new one no longer has.
        string[] stale = previous is null ? [] : previous.Files.Keys
            .Where(path => !staged.Files.ContainsKey(path) && !IsUsers(path) && !UpgradePaths.IsReserved(path) &&
                           File.Exists(Local(_install, path)))
            .ToArray();
        string[] existing = staged.Files.Keys.Where(path => !IsUsers(path) && File.Exists(Local(_install, path)))
            .Concat(stale).ToArray();
        // Before anything is read: a file the game holds open can't be hashed either, and
        // "in use" is the message the player can act on.
        foreach (string path in existing) RequireNotInUse(path);
        long total = existing.Sum(path => new FileInfo(Local(_install, path)).Length);
        long done = 0;
        string Existing(string path)
        {
            string hash = HashFile(Local(_install, path), read =>
            {
                cancel.ThrowIfCancellationRequested();
                Report(progress, UpgradePhase.Checking, done += read, total);
            });
            return hash;
        }
        // Changed since it was installed, as far as the installed release's list can tell.
        bool Modified(string path, string hash) =>
            previous is not null && previous.Files.TryGetValue(path, out string? original) && original != hash;

        foreach ((string path, StagedFile file) in staged.Files)
        {
            string target = Local(_install, path);
            RequireFilePath(path);
            if (!File.Exists(target)) plan.Add(new PlannedFile(path, UpgradeAction.Add));
            else if (IsUsers(path)) plan.Add(new PlannedFile(path, UpgradeAction.KeepUserFile));
            else
            {
                string hash = Existing(path);
                plan.Add(hash == file.Hash
                    ? new PlannedFile(path, UpgradeAction.Unchanged)
                    : new PlannedFile(path, UpgradeAction.Replace, Modified(path, hash)));
            }
        }
        foreach (string path in stale)
            plan.Add(new PlannedFile(path, UpgradeAction.Remove, Modified(path, Existing(path))));
        return plan;
    }

    // A folder where a file has to go, or a file where a folder has to go.
    private void RequireFilePath(string path)
    {
        string[] segments = path.Split('/');
        string current = _install;
        for (int i = 0; i < segments.Length; i++)
        {
            current = Path.Combine(current, segments[i]);
            bool last = i == segments.Length - 1;
            if (last ? Directory.Exists(current) : File.Exists(current))
                throw new UpgradeException($"\"{path}\" can't be installed: something else with that name is in the way.");
        }
    }

    // A running fable_2.exe can still be renamed, so without this check the upgrade would
    // swap the files under a running game. Windows refuses write access to a running program.
    private void RequireNotInUse(string path)
    {
        string target = Local(_install, path);
        if (string.Equals(target, Environment.ProcessPath, StringComparison.OrdinalIgnoreCase)) return;
        bool readOnly = File.GetAttributes(target).HasFlag(FileAttributes.ReadOnly);
        try
        {
            using var probe = new FileStream(target, FileMode.Open, readOnly ? FileAccess.Read : FileAccess.ReadWrite,
                FileShare.None);
        }
        catch (IOException exception)
        {
            throw new UpgradeException($"{path} is in use. Close Fable II and any other launcher window, then try again.",
                exception);
        }
    }

    // ---- Switch the files ----

    private UpgradeResult Apply(StagedRelease staged, IReadOnlyList<PlannedFile> plan, string installedVersion)
    {
        string[] added = plan.Where(file => file.Action == UpgradeAction.Add).Select(file => file.Path).ToArray();
        string? kept = null;
        WriteJournal(Applying, added);
        try
        {
            // The launcher goes last: it is the file the player needs to start a repair.
            foreach (PlannedFile file in plan.OrderBy(file => file.Path.Equals(UpgradePaths.Launcher,
                         StringComparison.OrdinalIgnoreCase)))
            {
                if (file.Action is UpgradeAction.Unchanged or UpgradeAction.KeepUserFile) continue;
                Installing?.Invoke(file);
                string target = Local(_install, file.Path);
                if (file.Action is UpgradeAction.Replace or UpgradeAction.Remove)
                    Move(target, Local(_backup, file.Path));
                if (file.Action is UpgradeAction.Replace or UpgradeAction.Add)
                    Move(Local(_staged, file.Path), target);
            }
            // Antivirus tools sometimes take a fresh exe away again.
            foreach (PlannedFile file in plan.Where(file => file.Action is UpgradeAction.Replace or UpgradeAction.Add))
            {
                var info = new FileInfo(Local(_install, file.Path));
                if (!info.Exists || info.Length != staged.Files[file.Path].Size)
                    throw new IOException($"{file.Path} disappeared right after it was installed. " +
                                          "An antivirus tool may have removed it.");
            }
            kept = KeepModified(plan, installedVersion);
            WriteJournal(Committed, []);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            try
            {
                Rollback(added);
                File.Delete(_journal);
            }
            catch (Exception again) when (again is IOException or UnauthorizedAccessException)
            {
                throw new UpgradeException(
                    $"The update failed half-way ({exception.Message}) and could not be undone ({again.Message}). " +
                    $"Your previous files are in \"{UpgradePaths.WorkFolder}\\backup\". Start the launcher again " +
                    "to retry, or copy them back over the launcher's folder.", exception);
            }
            throw new UpgradeException("The update could not be installed, so the previous version was put back. " +
                                       exception.Message, exception);
        }
        return new UpgradeResult(staged.Manifest.Version, plan, kept);
    }

    // Copies of the release files the player had changed, so an upgrade never loses an edit.
    private string? KeepModified(IReadOnlyList<PlannedFile> plan, string installedVersion)
    {
        PlannedFile[] modified = plan.Where(file => file.Modified).ToArray();
        if (modified.Length == 0) return null;
        string folder = Path.Combine(_install, UpgradePaths.KeptFolder,
            UpdateChecker.ParseVersion(installedVersion)?.ToString() ?? "previous");
        foreach (PlannedFile file in modified)
        {
            string copy = Local(folder, file.Path);
            Directory.CreateDirectory(Path.GetDirectoryName(copy)!);
            File.Copy(Local(_backup, file.Path), copy, overwrite: true);
        }
        return folder;
    }

    // Puts every file in the backup folder back and removes the files the upgrade added.
    // Safe to repeat: a file that is already back is no longer in the backup folder.
    private void Rollback(string[] added)
    {
        if (Directory.Exists(_backup))
            foreach (string file in Directory.EnumerateFiles(_backup, "*", SearchOption.AllDirectories).ToArray())
            {
                string target = Path.Combine(_install, Path.GetRelativePath(_backup, file));
                Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                if (File.Exists(target)) Discard(target);
                File.Move(file, target);
            }
        foreach (string path in added)
        {
            string target = Local(_install, UpgradePaths.Normalize(path));
            if (File.Exists(target)) Discard(target);
            // And the folders that were made for it, as long as nothing else is in them.
            for (string? folder = Path.GetDirectoryName(target);
                 folder is not null && folder.Length > _install.Length && Directory.Exists(folder) &&
                 !Directory.EnumerateFileSystemEntries(folder).Any();
                 folder = Path.GetDirectoryName(folder))
                Directory.Delete(folder);
        }
    }

    // A running program can't be deleted, but it can be renamed out of the way.
    private void Discard(string file)
    {
        try { File.Delete(file); }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            Directory.CreateDirectory(_trash);
            File.Move(file, Path.Combine(_trash, Guid.NewGuid().ToString("N")));
        }
    }

    private static void Move(string source, string target)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
        File.Move(source, target);
    }

    // ---- Journal: what to undo if the launcher dies during the switch ----

    private void WriteJournal(string state, string[] added)
    {
        string draft = _journal + ".tmp";
        using (var file = new FileStream(draft, FileMode.Create, FileAccess.Write, FileShare.None))
        {
            using (var writer = new Utf8JsonWriter(file))
            {
                writer.WriteStartObject();
                writer.WriteNumber("format", 1);
                writer.WriteString("state", state);
                writer.WriteStartArray("added");
                foreach (string path in added) writer.WriteStringValue(path);
                writer.WriteEndArray();
                writer.WriteEndObject();
            }
            file.Flush(flushToDisk: true);
        }
        File.Move(draft, _journal, overwrite: true);
    }

    private (string State, string[] Added) ReadJournal()
    {
        try
        {
            using JsonDocument document = JsonDocument.Parse(File.ReadAllText(_journal));
            JsonElement root = document.RootElement;
            if (root.GetProperty("format").GetInt32() != 1) throw new JsonException("unknown format");
            string state = root.GetProperty("state").GetString() ?? "";
            string[] added = root.GetProperty("added").EnumerateArray().Select(item => item.GetString() ?? "").ToArray();
            return (state, added);
        }
        catch (Exception exception) when (exception is JsonException or KeyNotFoundException
                                              or InvalidOperationException or FormatException)
        {
            throw new UpgradeException("The record of an unfinished update can't be read. Your previous files may " +
                                       $"be in \"{UpgradePaths.WorkFolder}\\backup\".", exception);
        }
    }

    private string? ReadJournalState()
    {
        try { return ReadJournal().State; }
        catch (Exception exception) when (exception is UpgradeException or IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    // ---- Helpers ----

    private (UpgradePhase Phase, long Step) _reported = (UpgradePhase.Installing, -1);

    // One report per half percent: the window does not need one for every block read.
    private void Report(IProgress<UpgradeProgress>? progress, UpgradePhase phase, long done, long total)
    {
        (UpgradePhase, long) step = (phase, total > 0 ? done * 200 / total : 0);
        if (progress is null || step == _reported) return;
        _reported = step;
        progress.Report(new UpgradeProgress(phase, done, total));
    }

    private static string Local(string root, string path) =>
        Path.Combine(root, path.Replace('/', Path.DirectorySeparatorChar));

    private static string HashFile(string file, Action<int> read)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        using var stream = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.Read, 1 << 16);
        byte[] buffer = new byte[1 << 16];
        int count;
        while ((count = stream.Read(buffer, 0, buffer.Length)) > 0)
        {
            hash.AppendData(buffer, 0, count);
            read(count);
        }
        return Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
    }

    // The old launcher is still running when an upgrade finishes and can't be deleted
    // yet. Whatever stays behind is removed by Recover on the next start.
    private static void DeleteBestEffort(string folder)
    {
        if (!Directory.Exists(folder)) return;
        foreach (string file in Directory.EnumerateFiles(folder, "*", SearchOption.AllDirectories).ToArray())
        {
            try
            {
                File.SetAttributes(file, FileAttributes.Normal);
                File.Delete(file);
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException) { }
        }
        try { Directory.Delete(folder, recursive: true); }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException) { }
    }
}
