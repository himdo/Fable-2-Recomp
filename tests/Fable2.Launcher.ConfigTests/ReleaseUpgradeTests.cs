using Fable2Launcher;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

// In-place upgrade (launcher/Fable2.Launcher/ReleaseUpgrade.cs): a newer release replaces
// the installed one's files, and nothing of the player's is touched. Every refused or
// failed upgrade must leave the folder exactly as it was.
static class ReleaseUpgradeTests
{
    private const string DownloadUrl = "https://github.com/himdo/Fable-2-Recomp/releases/download/1.8.0/Fable_2_recomp_v1.8.0_windows.zip";

    public static async Task RunAsync(string testDirectory)
    {
        string root = Path.Combine(testDirectory, "upgrade");
        Directory.CreateDirectory(root);
        FindsThePackage();
        ChecksPaths();
        ReadsTheFileList();
        await UpgradesAndKeepsThePlayersFiles(root);
        await UpgradesAnInstallWithoutAFileList(root);
        await RefusesBadDownloads(root);
        await RefusesWhenTheFolderIsNotReady(root);
        await UndoesAFailedOrInterruptedSwitch(root);
        CleansUpLeftovers(root);
        Console.WriteLine("In-place upgrade tests passed.");
    }

    // ---- The update check finds the release zip ----

    private static void FindsThePackage()
    {
        string sha = new('a', 64);
        object Asset(string name, string? url = null, string? digest = null, long size = 1000) => new
        {
            name,
            browser_download_url = url ?? "https://github.com/himdo/Fable-2-Recomp/releases/download/1.8.0/" + name,
            digest = digest ?? "sha256:" + sha,
            size,
        };
        static string Json(params object[] assets) => JsonSerializer.Serialize(new
        {
            tag_name = "1.8.0", html_url = "https://github.com/himdo/Fable-2-Recomp/releases/tag/1.8.0", assets,
        });
        const string zip = "Fable_2_recomp_v1.8.0_windows.zip";

        ReleasePackage? package = UpdateChecker.FindUpdate("1.7.0", Json(Asset(zip)))?.Package;
        Require(package is not null && package.Size == 1000 && package.Sha256 == sha &&
                package.DownloadUrl.EndsWith(zip, StringComparison.Ordinal), "release zip was not found");
        Require(UpdateChecker.FindUpdate("1.7.0", Json(Asset(zip, digest: "sha256:" + sha.ToUpperInvariant())))
            ?.Package?.Sha256 == sha, "checksum was not normalised to lower case");
        Require(UpdateChecker.FindUpdate("1.7.0", Json(Asset("Fable_2_recomp_v1.8.0_symbols.zip"), Asset(zip)))
            ?.Package is not null, "symbols zip hid the release zip");

        // Without a package the update is still reported: the button then opens the page.
        (string Why, AvailableUpdate? Update)[] manual =
        [
            ("no assets", UpdateChecker.FindUpdate("1.7.0", Json())),
            ("two release zips", UpdateChecker.FindUpdate("1.7.0", Json(Asset(zip), Asset("Fable_2_recomp_v1.8.0b_windows.zip")))),
            ("download outside the repo", UpdateChecker.FindUpdate("1.7.0", Json(Asset(zip, url: "https://example.com/" + zip)))),
            ("plain http download", UpdateChecker.FindUpdate("1.7.0", Json(Asset(zip, url: "http://github.com/himdo/Fable-2-Recomp/releases/download/1.8.0/" + zip)))),
            ("no checksum", UpdateChecker.FindUpdate("1.7.0", Json(new { name = zip, browser_download_url = DownloadUrl, size = 1000 }))),
            ("checksum of another kind", UpdateChecker.FindUpdate("1.7.0", Json(Asset(zip, digest: "md5:" + new string('a', 32))))),
            ("empty file", UpdateChecker.FindUpdate("1.7.0", Json(Asset(zip, size: 0)))),
            ("oversized file", UpdateChecker.FindUpdate("1.7.0", Json(Asset(zip, size: UpdateChecker.MaxPackageBytes + 1)))),
            ("only a symbols zip", UpdateChecker.FindUpdate("1.7.0", Json(Asset("Fable_2_recomp_v1.8.0_symbols.zip")))),
        ];
        foreach ((string why, AvailableUpdate? update) in manual)
            Require(update is not null && update.Package is null, "in-place upgrade offered despite: " + why);
        Console.WriteLine("Update check finds the release zip and its checksum passed.");
    }

    // ---- Which paths are safe, and which belong to the player ----

    private static void ChecksPaths()
    {
        Require(UpgradePaths.Normalize(@"data\scripts\recomp\F5.lua") == "data/scripts/recomp/F5.lua", "backslashes were kept");
        Require(UpgradePaths.Normalize("assets/fonts/Google Sans/GoogleSans-VariableFont_GRAD,opsz,wght.ttf").Length > 0,
            "the bundled font's name was rejected");
        foreach (string unsafePath in new[]
                 {
                     "", "../evil.exe", "a/../../evil.exe", @"..\evil.exe", "/evil.exe", @"\evil.exe", "C:/evil.exe",
                     "a//b", "a/./b", "saves/slot1.sav:stream", "fable2_config.toml.", "fable2_config.toml ", " lead",
                     "NUL", "con.txt", "a/COM1.log", "a\tb", "what?.txt", new string('a', 300),
                 })
            RequireFails(() => UpgradePaths.Normalize(unsafePath), "not safe", "unsafe path accepted: " + unsafePath);

        foreach (string users in new[]
                 {
                     "fable2_config.toml", "fable_2.toml", "launcher-settings.toml", "FABLE_2.TOML", "some_future.toml",
                     "launcher-game-path.txt", "default.xex", "saves/profile/slot1.sav", "Saves/slot1.sav", "cache/shaders.bin",
                     "logs/fable_2_001.log", "screenshots/photo.png", "nxeart", "$SystemUpdate/su.bin",
                     "data/dir.manifest", "DATA/Dir.manifest", "data/art.pak", "data/scripts/quests/quest.lua",
                     "data/scripts/recomp.lua", "fable_2.toml.launcher-backup", "data/scripts/recomp/F5.lua.bak",
                 })
            Require(UpgradePaths.IsUserOwned(users), "player's file not protected: " + users);
        foreach (string release in new[]
                 {
                     "fable_2.exe", "Fable2Launcher.exe", "rexruntime.dll", "fable2_build.json", "README.md",
                     "licenses/XenosTiling-LICENSE.txt", "data/scripts/recomp/F5.lua", "assets/fonts/Google Sans/OFL.txt",
                 })
            Require(!UpgradePaths.IsUserOwned(release), "release file treated as the player's: " + release);
        foreach (string reserved in new[] { "launcher-update/backup/fable_2.exe", "launcher-update-backup/1.7.0/a", "launcher-update.lock" })
            Require(UpgradePaths.IsReserved(reserved), "upgrade working file not reserved: " + reserved);
        Console.WriteLine("Upgrade path safety and player-owned paths passed.");
    }

    private static void ReadsTheFileList()
    {
        var release = new Release("1.8.0") { Files = { ["fable_2.exe"] = "game" } };
        ReleaseManifest manifest = ReleaseManifest.Parse(release.BuildInfo())!;
        Require(manifest.Version == "1.8.0" && manifest.Files["FABLE_2.EXE"] == Sha("game") &&
                manifest.UserFiles.Contains("fable2_config.toml"), "file list was misread");
        Require(ReleaseManifest.Parse("{\"version\":\"1.6.1\",\"profiles\":[]}") is null, "a release without a list gave one");
        RequireFails(() => ReleaseManifest.Parse(new Release("1.8.0") { Format = 2 }.BuildInfo()),
            "cannot install by itself", "unknown package layout accepted");
        RequireFails(() => ReleaseManifest.Parse("{\"version\":\"1.8.0\",\"release\":{\"format\":1,\"files\":{\"a.exe\":\"abc\"}}}"),
            "bad entry", "short hash accepted");
        RequireFails(() => ReleaseManifest.Parse("{\"version\":\"1.8.0\",\"release\":{\"format\":1,\"files\":{\"../a.exe\":\"" + Sha("x") + "\"}}}"),
            "not safe", "path outside the folder accepted in the list");
        RequireFails(() => ReleaseManifest.Parse("{\"release\":{\"format\":1,\"files\":{}}}"), "incomplete", "list without version accepted");
        Console.WriteLine("Release file list parsing passed.");
    }

    // ---- The upgrade itself ----

    private static Release Installed() => new("1.7.0")
    {
        Files =
        {
            ["fable_2.exe"] = "game 1.7.0",
            ["rexruntime.dll"] = "runtime (same in both)",
            ["rexgpu-xenos.dll"] = "plugin 1.7.0",
            ["Fable2Launcher.exe"] = "launcher 1.7.0",
            ["README.md"] = "readme 1.7.0",
            ["fable2_config.toml"] = "default settings 1.7.0",
            ["licenses/XenosTiling-LICENSE.txt"] = "licence",
            ["assets/fonts/Google Sans/OFL.txt"] = "font licence",
            ["data/scripts/recomp/F5.lua"] = "f5 1.7.0",
            ["data/scripts/recomp/console_cmds.lua"] = "console 1.7.0",
            ["data/scripts/recomp/toggleFreecam.lua"] = "removed in 1.8.0",
            ["data/scripts/recomp/retired_but_edited.lua"] = "also removed in 1.8.0",
        },
    };

    private static Release Newer() => new("1.8.0")
    {
        Files =
        {
            ["fable_2.exe"] = "game 1.8.0",
            ["rexruntime.dll"] = "runtime (same in both)",
            ["rexgpu-xenos.dll"] = "plugin 1.8.0",
            ["Fable2Launcher.exe"] = "launcher 1.8.0",
            ["README.md"] = "readme 1.8.0",
            ["fable2_config.toml"] = "default settings 1.8.0",
            ["licenses/XenosTiling-LICENSE.txt"] = "licence",
            ["assets/fonts/Google Sans/OFL.txt"] = "font licence",
            ["assets/fonts/New Font/New.ttf"] = "a folder that did not exist before",
            ["data/scripts/recomp/F5.lua"] = "f5 1.8.0",
            ["data/scripts/recomp/console_cmds.lua"] = "console 1.8.0",
            ["data/scripts/recomp/console_step.lua"] = "new in 1.8.0",
        },
    };

    // What a player has next to the release after playing for a while.
    private static readonly Dictionary<string, string> PlayersFiles = new()
    {
        ["fable2_config.toml"] = "my keybinds",
        ["fable2_config.toml.launcher-backup"] = "settings backup",
        ["fable_2.toml"] = "my graphics",
        ["launcher-settings.toml"] = "my launcher settings",
        ["launcher-game-path.txt"] = "D:\\Games\\Fable 2",
        ["default.xex"] = "game code from my disc",
        ["saves/profile/slot1.sav"] = "40 hours",
        ["cache/default.xex.sha256"] = "cache",
        ["logs/fable_2_001.log"] = "log",
        ["screenshots/photo.png"] = "photo",
        ["data/dir.manifest"] = "the disc's real file index",
        ["data/art.pak"] = "game data",
        ["data/scripts/quests/quest.lua"] = "game script",
        ["data/scripts/recomp/my_mod.lua"] = "a script of my own",
        ["notes.txt"] = "something I keep here",
    };

    private static string NewInstall(string root, string name, Release? release = null, bool played = true)
    {
        string folder = Path.Combine(root, name);
        (release ?? Installed()).InstallTo(folder);
        if (!played) return folder;
        foreach ((string path, string text) in PlayersFiles) Write(folder, path, text);
        return folder;
    }

    private static async Task UpgradesAndKeepsThePlayersFiles(string root)
    {
        string folder = NewInstall(root, "happy");
        Write(folder, "data/scripts/recomp/F5.lua", "f5 with my own changes");
        Write(folder, "data/scripts/recomp/retired_but_edited.lua", "my edit of a script 1.8.0 drops");

        // 1.8.0 also carries files a release should never overwrite. 1.5.0 really shipped a
        // data/dir.manifest, and "mods.cfg" is one the release itself marks as the player's.
        Release newer = Newer();
        newer.Files["data/dir.manifest"] = "a stub index that must not replace the disc's";
        newer.Files["saves/readme.txt"] = "new file in a player's folder";
        newer.Files["mods.cfg"] = "default mod list";
        newer.UserFiles = ["fable2_config.toml", "mods.cfg"];
        Write(folder, "mods.cfg", "my mod list");

        var phases = new List<UpgradePhase>();
        UpgradeResult result = await newer.UpgradeAsync(folder, progress: new Collect(phases));

        Require(result.Version == "1.8.0" && UpdateChecker.ReadInstalledVersion(folder) == "1.8.0", "installed version was not updated");
        foreach ((string path, string text) in newer.Files)
        {
            if (path is "fable2_config.toml" or "data/dir.manifest" or "mods.cfg") continue;
            Require(Read(folder, path) == text, "release file was not installed: " + path);
        }
        foreach ((string path, string text) in PlayersFiles)
            Require(Read(folder, path) == text, "player's file was changed: " + path);
        Require(Read(folder, "mods.cfg") == "my mod list", "file the release marks as the player's was overwritten");
        Require(!File.Exists(Path.Combine(folder, "data/scripts/recomp/toggleFreecam.lua")), "script the release dropped was left behind");
        Require(!File.Exists(Path.Combine(folder, "data/scripts/recomp/retired_but_edited.lua")), "dropped script was left behind");

        // Edits to release files are replaced, but never lost.
        string kept = Path.Combine(folder, "launcher-update-backup", "1.7.0");
        Require(result.KeptFolder == kept, "the folder with the player's edits was not reported");
        Require(Read(kept, "data/scripts/recomp/F5.lua") == "f5 with my own changes", "edited script was not kept");
        Require(Read(kept, "data/scripts/recomp/retired_but_edited.lua") == "my edit of a script 1.8.0 drops", "edited dropped script was not kept");
        Require(Directory.GetFiles(kept, "*", SearchOption.AllDirectories).Length == 2, "untouched release files were kept as edits");

        Require(Action(result, "fable_2.exe") == UpgradeAction.Replace, "game was not replaced");
        Require(Action(result, "rexruntime.dll") == UpgradeAction.Unchanged, "identical file was rewritten");
        Require(Action(result, "data/scripts/recomp/console_step.lua") == UpgradeAction.Add, "new script was not added");
        Require(Action(result, "saves/readme.txt") == UpgradeAction.Add, "missing file in a player's folder was not added");
        Require(Action(result, "fable2_config.toml") == UpgradeAction.KeepUserFile, "settings were not kept");
        Require(Action(result, "data/dir.manifest") == UpgradeAction.KeepUserFile, "disc file index was not kept");
        Require(Action(result, "data/scripts/recomp/toggleFreecam.lua") == UpgradeAction.Remove, "dropped script was not removed");
        Require(result.Files.Single(file => file.Path == "data/scripts/recomp/F5.lua").Modified, "edit was not noticed");
        Require(!result.Files.Single(file => file.Path == "data/scripts/recomp/console_cmds.lua").Modified, "untouched file counted as edited");
        Require(phases.Contains(UpgradePhase.Downloading) && phases.Contains(UpgradePhase.Extracting) &&
                phases[^1] == UpgradePhase.Installing, "progress did not end with installing");
        RequireNoWorkingFiles(folder);

        // The same release again is not an upgrade.
        string[] before = Snapshot(folder);
        await RequireRefused(() => newer.UpgradeAsync(folder), "not newer", "same version was installed again");
        RequireSame(before, folder, "refused repeat upgrade");

        // The next upgrade works from the list the first one installed.
        var next = new Release("1.9.0") { Files = new(Newer().Files) { ["fable_2.exe"] = "game 1.9.0" } };
        next.Files.Remove("data/scripts/recomp/console_step.lua");
        UpgradeResult second = await next.UpgradeAsync(folder, tag: "v1.9.0");
        Require(Read(folder, "fable_2.exe") == "game 1.9.0" && second.KeptFolder is null, "second upgrade failed");
        Require(!File.Exists(Path.Combine(folder, "data/scripts/recomp/console_step.lua")), "second upgrade left a dropped script");
        Require(Read(folder, "saves/profile/slot1.sav") == "40 hours" && Read(folder, "fable2_config.toml") == "my keybinds",
            "second upgrade touched the player's files");
        Console.WriteLine("In-place upgrade replaces release files and keeps saves, settings and game data passed.");
    }

    private static async Task UpgradesAnInstallWithoutAFileList(string root)
    {
        // A folder without a list (1.6.x unzipped by hand): nothing can be told apart, so
        // nothing is removed and nothing is reported as edited.
        var old = new Release("1.6.1") { Manifest = false, Files = new(Installed().Files) };
        string folder = NewInstall(root, "no-list", old);
        UpgradeResult result = await Newer().UpgradeAsync(folder);
        Require(Read(folder, "fable_2.exe") == "game 1.8.0", "upgrade without an installed list failed");
        Require(File.Exists(Path.Combine(folder, "data/scripts/recomp/toggleFreecam.lua")), "file removed without a list to say it was the release's");
        Require(result.KeptFolder is null && !Directory.Exists(Path.Combine(folder, "launcher-update-backup")), "edits reported without a list");
        Require(Read(folder, "fable2_config.toml") == "my keybinds", "settings were overwritten");

        // Zips made by other tools: no top folder, or with entries for the folders (as 1.5.0 had).
        string flat = NewInstall(root, "flat-zip");
        await new Release("1.8.0") { Files = new(Newer().Files), TopFolder = "" }.UpgradeAsync(flat);
        Require(Read(flat, "fable_2.exe") == "game 1.8.0", "zip without a top folder was not installed");
        string folders = NewInstall(root, "folder-entries");
        await new Release("1.8.0") { Files = new(Newer().Files), FolderEntries = true }.UpgradeAsync(folders);
        Require(Read(folders, "data/scripts/recomp/console_step.lua") == "new in 1.8.0", "zip with folder entries was not installed");

        // Before the disc is extracted there is no data folder at all.
        string fresh = NewInstall(root, "fresh", played: false);
        await Newer().UpgradeAsync(fresh);
        Require(Read(fresh, "fable2_config.toml") == "default settings 1.7.0", "untouched default settings were replaced");
        Console.WriteLine("In-place upgrade without an installed file list, and of other zip layouts passed.");
    }

    private static async Task RefusesBadDownloads(string root)
    {
        string folder = NewInstall(root, "bad-downloads");
        string[] before = Snapshot(folder);
        string outside = Path.Combine(root, "evil.exe");

        async Task Refused(string expected, string what, Release release, Func<byte[], byte[]>? tamper = null,
            long? size = null, string? sha = null, string? tag = null, Func<Stream, Stream>? wrap = null)
        {
            await RequireRefused(() => release.UpgradeAsync(folder, tamper: tamper, size: size, sha: sha, tag: tag, wrap: wrap),
                expected, what);
            RequireSame(before, folder, what);
            RequireNoWorkingFiles(folder);
        }

        // The download itself.
        await Refused("checksum", "download with another checksum was installed", Newer(), sha: new string('0', 64));
        await Refused("cut short", "cut-off download was installed", Newer(),
            wrap: stream => new MemoryStream(((MemoryStream)stream).ToArray()[..200]));
        await Refused("larger than", "oversized download was installed", Newer(), size: 10);
        await Refused("damaged", "file that is not a zip was installed", Newer(), tamper: _ => Encoding.UTF8.GetBytes("<html>rate limited</html>"));
        await Refused("installed by hand", "release without a package was installed", Newer(), size: -1);

        // The zip's contents against its own list.
        await Refused("installed by hand", "release without a file list was installed", new Release("1.8.0") { Manifest = false, Files = new(Newer().Files) });
        await Refused("cannot install by itself", "unknown package layout was installed", new Release("1.8.0") { Format = 2, Files = new(Newer().Files) });
        var swapped = new Release("1.8.0") { Files = new(Newer().Files) };
        swapped.Listed = new(swapped.Files) { ["fable_2.exe"] = "what the list expects" };
        await Refused("does not match the release's file list", "file that differs from the list was installed", swapped);
        var extra = new Release("1.8.0") { Files = new(Newer().Files) { ["dropper.exe"] = "not in the list" } };
        extra.Listed = new(Newer().Files);
        await Refused("does not match its contents", "unlisted file was installed", extra);
        var incomplete = new Release("1.8.0") { Files = new(Newer().Files) };
        incomplete.Listed = new(Newer().Files) { ["rexaudio.dll"] = "listed but not in the zip" };
        await Refused("does not match its contents", "release with a missing file was installed", incomplete);
        var noGame = new Release("1.8.0") { Files = new(Newer().Files) };
        noGame.Files.Remove("fable_2.exe");
        await Refused("missing fable_2.exe", "release without the game was installed", noGame);
        await Refused("not a Fable 2 Recompiled release", "unrelated zip was installed", new Release("1.8.0") { Manifest = false, BuildInfoName = "other.json" });

        // Versions.
        await Refused("not 1.9.0", "download of another version than announced was installed", Newer(), tag: "1.9.0");
        await Refused("not newer", "older release was installed", new Release("1.6.0") { Files = new(Newer().Files) });

        // Paths that leave the folder, or that the upgrade uses itself.
        foreach (string unsafePath in new[] { "../evil.exe", "data/../../evil.exe", "C:/evil.exe", "fable_2.exe:hidden" })
            await Refused("not safe", "unsafe path was installed: " + unsafePath, new Release("1.8.0") { Files = new(Newer().Files) { [unsafePath] = "evil" } });
        Require(!File.Exists(outside), "a file was written outside the launcher's folder");
        await Refused("can't be installed", "release that overwrites the upgrade's own files was installed",
            new Release("1.8.0") { Files = new(Newer().Files) { ["launcher-update/backup/fable_2.exe"] = "evil" } });
        await Refused("can't be installed", "two files with the same name were installed",
            new Release("1.8.0") { Files = new(Newer().Files) { ["FABLE_2.exe"] = "second game" } });
        Console.WriteLine("In-place upgrade refuses damaged, mismatched and unsafe downloads without changing the folder passed.");
    }

    private static async Task RefusesWhenTheFolderIsNotReady(string root)
    {
        string folder = NewInstall(root, "not-ready");
        string[] before = Snapshot(folder);

        await RequireRefused(() => Newer().UpgradeAsync(folder, freeSpace: _ => 1 << 20), "free disk space", "upgrade started without disk space");
        RequireSame(before, folder, "upgrade without disk space");

        // A running game keeps its exe open.
        using (new FileStream(Path.Combine(folder, "fable_2.exe"), FileMode.Open, FileAccess.ReadWrite, FileShare.None))
            await RequireRefused(() => Newer().UpgradeAsync(folder), "fable_2.exe is in use", "upgrade ran under a running game");
        RequireSame(before, folder, "upgrade under a running game");
        RequireNoWorkingFiles(folder);

        // Another launcher window that is upgrading holds the lock.
        string lockFile = Path.Combine(folder, "launcher-update.lock");
        using (new FileStream(lockFile, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None))
        {
            await RequireRefused(() => Newer().UpgradeAsync(folder), "already updating", "two upgrades ran at once");
            Directory.CreateDirectory(Path.Combine(folder, "launcher-update"));
            Require(ReleaseUpgrader.Recover(folder) == UpgradeRecovery.Busy, "a running upgrade's files were treated as leftovers");
            Directory.Delete(Path.Combine(folder, "launcher-update"));
        }
        File.Delete(lockFile);
        RequireSame(before, folder, "second upgrade at once");

        // A folder in the way of a file, and a file in the way of a folder.
        string blocked = NewInstall(root, "blocked");
        Directory.CreateDirectory(Path.Combine(blocked, "data/scripts/recomp/console_step.lua"));
        string[] blockedBefore = Snapshot(blocked);
        await RequireRefused(() => Newer().UpgradeAsync(blocked), "in the way", "file was installed over a folder");
        RequireSame(blockedBefore, blocked, "upgrade with a folder in the way");
        Directory.Delete(Path.Combine(blocked, "data/scripts/recomp/console_step.lua"));
        Write(blocked, "assets/fonts/New Font", "a file where the release needs a folder");
        await RequireRefused(() => Newer().UpgradeAsync(blocked), "in the way", "folder was installed over a file");

        // A local build has no version, so there is nothing to upgrade.
        string local = Path.Combine(root, "local-build");
        Write(local, "fable2_build.json", "{\"profiles\":[]}");
        await RequireRefused(() => Newer().UpgradeAsync(local), "nothing to upgrade", "local build was upgraded");

        // Stopped by the player, or a connection that goes quiet.
        using var cancel = new CancellationTokenSource();
        bool cancelled = false;
        try { await Newer().UpgradeAsync(folder, cancel: cancel.Token, wrap: stream => new Interrupted(stream, cancel.Cancel)); }
        catch (OperationCanceledException) { cancelled = true; }
        Require(cancelled, "cancelling did not stop the upgrade");
        RequireSame(before, folder, "cancelled upgrade");
        RequireNoWorkingFiles(folder);
        await RequireRefused(() => Newer().UpgradeAsync(folder, stall: TimeSpan.FromMilliseconds(100),
            wrap: stream => new Interrupted(stream, null)), "stopped responding", "stalled download was not given up");
        RequireSame(before, folder, "stalled download");
        RequireNoWorkingFiles(folder);
        Console.WriteLine("In-place upgrade refuses a full disk, a running game, a second upgrade and a stopped download passed.");
    }

    private static async Task UndoesAFailedOrInterruptedSwitch(string root)
    {
        // A file can't be switched half-way through (antivirus, a lock, a full disk).
        string folder = NewInstall(root, "fails-half-way");
        Write(folder, "data/scripts/recomp/F5.lua", "f5 with my own changes");
        string[] before = Snapshot(folder);
        int switched = 0;
        await RequireRefused(() => Newer().UpgradeAsync(folder, installing: _ =>
        {
            if (++switched == 5) throw new IOException("Access to the path is denied.");
        }), "previous version was put back", "failed switch was not reported");
        Require(switched == 5, "the switch did not get half-way");
        RequireSame(before, folder, "failed switch");
        RequireNoWorkingFiles(folder);
        Require(!Directory.Exists(Path.Combine(folder, "launcher-update-backup")), "edits were kept for an upgrade that did not happen");

        // The very last file (the launcher) fails: everything before it goes back too.
        await RequireRefused(() => Newer().UpgradeAsync(folder, installing: file =>
        {
            if (file.Path == "Fable2Launcher.exe") throw new UnauthorizedAccessException("Access to the path is denied.");
        }), "previous version was put back", "failed launcher switch was not reported");
        RequireSame(before, folder, "failed launcher switch");

        // The launcher dies half-way (power cut, crash): the next start puts everything back.
        foreach (int dieAt in new[] { 1, 4, 9 })
        {
            switched = 0;
            bool died = false;
            try
            {
                await Newer().UpgradeAsync(folder, installing: _ =>
                {
                    if (++switched == dieAt) throw new PowerCut();
                });
            }
            catch (PowerCut) { died = true; }
            Require(died, "the simulated power cut did not happen");
            if (dieAt > 1) Require(!Snapshot(folder).SequenceEqual(before), "the power cut came before anything was switched");
            Require(ReleaseUpgrader.Recover(folder) == UpgradeRecovery.RolledBack, "interrupted upgrade was not undone on the next start");
            RequireSame(before, folder, "upgrade interrupted at file " + dieAt);
            RequireNoWorkingFiles(folder);
        }

        // An interrupted upgrade is also undone when the player simply clicks Update again.
        try { await Newer().UpgradeAsync(folder, installing: file => { if (file.Path == "README.md") throw new PowerCut(); }); }
        catch (PowerCut) { }
        await Newer().UpgradeAsync(folder);
        Require(Read(folder, "fable_2.exe") == "game 1.8.0" && Read(folder, "saves/profile/slot1.sav") == "40 hours",
            "upgrade after an interrupted one failed");
        Require(Read(folder, "launcher-update-backup/1.7.0/data/scripts/recomp/F5.lua") == "f5 with my own changes",
            "edit was lost across an interrupted upgrade");
        RequireNoWorkingFiles(folder);
        Console.WriteLine("In-place upgrade undoes a failed switch, and an interrupted one on the next start passed.");
    }

    private static void CleansUpLeftovers(string root)
    {
        string folder = NewInstall(root, "leftovers");
        string[] before = Snapshot(folder);
        Require(ReleaseUpgrader.Recover(folder) == UpgradeRecovery.None, "a clean folder needed recovery");

        // A download that never finished.
        Write(folder, "launcher-update/download.zip", "half a zip");
        Write(folder, "launcher-update/staged/fable_2.exe", "unpacked but never installed");
        Require(ReleaseUpgrader.Recover(folder) == UpgradeRecovery.CleanedUp, "unfinished download was not removed");
        RequireSame(before, folder, "clean-up of an unfinished download");

        // A finished upgrade leaves the old launcher behind: it could not delete itself.
        Write(folder, "launcher-update/journal.json", "{\"format\":1,\"state\":\"committed\",\"added\":[]}");
        Write(folder, "launcher-update/backup/Fable2Launcher.exe", "the previous launcher");
        Require(ReleaseUpgrader.Recover(folder) == UpgradeRecovery.CleanedUp, "finished upgrade's leftovers were not removed");
        RequireSame(before, folder, "clean-up after a finished upgrade");

        // A record this launcher can't read: leave everything for a person to look at.
        Write(folder, "launcher-update/journal.json", "{\"format\":99}");
        Write(folder, "launcher-update/backup/fable_2.exe", "a previous game");
        Require(ReleaseUpgrader.Recover(folder) == UpgradeRecovery.Failed, "unreadable record was acted on");
        Require(Read(folder, "launcher-update/backup/fable_2.exe") == "a previous game" && Read(folder, "fable_2.exe") == "game 1.7.0",
            "files were moved on an unreadable record");
        Console.WriteLine("Upgrade leftovers are cleaned up on start passed.");
    }

    // ---- A release, as tools/package_release.py builds it ----

    private sealed class Release(string version)
    {
        public Dictionary<string, string> Files { get; set; } = new();
        // What the file list claims, when it should differ from the zip's real contents.
        public Dictionary<string, string>? Listed { get; set; }
        public string[] UserFiles { get; set; } = ["fable2_config.toml"];
        public int Format { get; set; } = 1;
        public bool Manifest { get; set; } = true;
        public string? TopFolder { get; set; }
        public bool FolderEntries { get; set; }
        public string BuildInfoName { get; set; } = "fable2_build.json";

        public string BuildInfo()
        {
            var info = new Dictionary<string, object> { ["profiles"] = new[] { "goty-us-eu" }, ["version"] = version };
            if (Manifest)
                info["release"] = new Dictionary<string, object>
                {
                    ["format"] = Format,
                    ["user_files"] = UserFiles,
                    ["files"] = (Listed ?? Files).ToDictionary(file => file.Key, file => Sha(file.Value)),
                };
            return JsonSerializer.Serialize(info, new JsonSerializerOptions { WriteIndented = true });
        }

        public void InstallTo(string folder)
        {
            foreach ((string path, string text) in Files) Write(folder, path, text);
            Write(folder, BuildInfoName, BuildInfo());
        }

        public byte[] Zip()
        {
            string top = TopFolder ?? $"Fable 2 {version}/";
            using var bytes = new MemoryStream();
            using (var archive = new ZipArchive(bytes, ZipArchiveMode.Create, leaveOpen: true))
            {
                if (FolderEntries)
                    foreach (string folder in new[] { top, top + "data/", top + "data/scripts/", top + "data/scripts/recomp/" })
                        archive.CreateEntry(folder);
                foreach ((string path, string text) in Files.Append(new(BuildInfoName, BuildInfo())))
                {
                    using Stream entry = archive.CreateEntry(top + path).Open();
                    entry.Write(Encoding.UTF8.GetBytes(text));
                }
            }
            return bytes.ToArray();
        }

        // Upgrades `folder` to this release, served from memory in place of GitHub.
        // size: -1 stands for a release whose zip the update check did not accept.
        public Task<UpgradeResult> UpgradeAsync(string folder, Func<byte[], byte[]>? tamper = null, long? size = null,
            string? sha = null, string? tag = null, Func<string, long?>? freeSpace = null,
            Action<PlannedFile>? installing = null, IProgress<UpgradeProgress>? progress = null,
            CancellationToken cancel = default, Func<Stream, Stream>? wrap = null, TimeSpan? stall = null)
        {
            byte[] real = Zip();
            byte[] served = tamper is null ? real : tamper(real);
            ReleasePackage? package = size == -1 ? null
                : new ReleasePackage(DownloadUrl, size ?? served.Length, sha ?? Convert.ToHexString(SHA256.HashData(served)).ToLowerInvariant());
            var update = new AvailableUpdate(tag ?? version, "https://github.com/himdo/Fable-2-Recomp/releases/tag/" + version, package);
            var upgrader = new ReleaseUpgrader(folder, freeSpace ?? (_ => long.MaxValue))
            {
                Installing = installing,
                StallTimeout = stall ?? TimeSpan.FromSeconds(30),
            };
            return upgrader.RunAsync(update, (url, _) =>
            {
                Require(url == DownloadUrl, "the upgrade downloaded from another address");
                Stream stream = new MemoryStream(served);
                return Task.FromResult(wrap is null ? stream : wrap(stream));
            }, progress, cancel);
        }
    }

    // Reads one block, then calls `then` and never answers again (null: just goes quiet).
    private sealed class Interrupted(Stream inner, Action? then) : Stream
    {
        private bool _first = true;
        public override async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancel = default)
        {
            if (_first)
            {
                _first = false;
                return await inner.ReadAsync(buffer[..Math.Min(buffer.Length, 64)], cancel);
            }
            then?.Invoke();
            await Task.Delay(Timeout.Infinite, cancel);
            return 0;
        }
        public override bool CanRead => true;
        public override bool CanSeek => false;
        public override bool CanWrite => false;
        public override long Length => throw new NotSupportedException();
        public override long Position { get => throw new NotSupportedException(); set => throw new NotSupportedException(); }
        public override void Flush() { }
        public override int Read(byte[] buffer, int offset, int count) => throw new NotSupportedException();
        public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
        public override void SetLength(long value) => throw new NotSupportedException();
        public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();
    }

    private sealed class PowerCut : Exception;

    // Progress<T> posts to the thread pool; the tests want the reports in order, right away.
    private sealed class Collect(List<UpgradePhase> phases) : IProgress<UpgradeProgress>
    {
        public void Report(UpgradeProgress value)
        {
            lock (phases)
                if (phases.Count == 0 || phases[^1] != value.Phase) phases.Add(value.Phase);
        }
    }

    // ---- Helpers ----

    private static string Sha(string text) => Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(text))).ToLowerInvariant();

    private static void Write(string folder, string path, string text)
    {
        string file = Path.Combine(folder, path);
        Directory.CreateDirectory(Path.GetDirectoryName(file)!);
        File.WriteAllText(file, text);
    }

    private static string Read(string folder, string path) => File.ReadAllText(Path.Combine(folder, path));

    private static UpgradeAction Action(UpgradeResult result, string path) => result.Files.Single(file => file.Path == path).Action;

    // Every file in the folder with its contents, and every folder.
    private static string[] Snapshot(string folder) =>
        Directory.EnumerateFileSystemEntries(folder, "*", SearchOption.AllDirectories)
            .Select(entry => Path.GetRelativePath(folder, entry).Replace('\\', '/') +
                             (Directory.Exists(entry) ? "/" : " = " + Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(entry)))))
            .Order(StringComparer.Ordinal).ToArray();

    private static void RequireSame(string[] before, string folder, string what)
    {
        string[] after = Snapshot(folder);
        string? difference = before.Except(after).Concat(after.Except(before)).FirstOrDefault();
        Require(difference is null, $"the folder changed after: {what} ({difference})");
    }

    private static void RequireNoWorkingFiles(string folder)
    {
        Require(!Directory.Exists(Path.Combine(folder, "launcher-update")), "the upgrade's working folder was left behind");
        Require(!File.Exists(Path.Combine(folder, "launcher-update.lock")), "the upgrade's lock was left behind");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    private static void RequireFails(Action action, string expected, string message)
    {
        try { action(); }
        catch (UpgradeException exception)
        {
            if (exception.Message.Contains(expected, StringComparison.Ordinal)) return;
            throw new InvalidOperationException($"{message} (got: {exception.Message})");
        }
        throw new InvalidOperationException(message);
    }

    // The upgrade must stop with a message for the player that says `expected`.
    private static async Task RequireRefused(Func<Task> upgrade, string expected, string message)
    {
        try { await upgrade(); }
        catch (UpgradeException exception)
        {
            if (exception.Message.Contains(expected, StringComparison.Ordinal)) return;
            throw new InvalidOperationException($"{message} (got: {exception.Message})");
        }
        throw new InvalidOperationException(message);
    }
}
