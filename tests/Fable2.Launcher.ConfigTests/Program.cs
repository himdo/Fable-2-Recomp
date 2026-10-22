using Fable2Launcher;
using Fable2Launcher.Constants;
using System.Security.Cryptography;
using System.Text;
using System.Xml.Linq;

string testDirectory = Path.Combine(Path.GetTempPath(), "fable2-launcher-config-test-" + Guid.NewGuid());
Directory.CreateDirectory(testDirectory);

try
{
    string configPath = Path.Combine(testDirectory, "fable_2.toml");
    const string original = "# existing config\nresolution = \"720p\" # keep this note\ncustom_mod = true\n";
    File.WriteAllText(configPath, original);

    var settings = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
    {
        ["resolution"] = "\"4k\"",
        ["window_width"] = "3840",
        ["resolution_scale"] = "3",
        ["vsync"] = "true"
    };
    string[] order = ["resolution", "window_width", "resolution_scale", "vsync"];

    LauncherConfigFile.WriteValues(configPath, settings, order);
    string updated = File.ReadAllText(configPath);
    Require(updated.Contains("resolution = \"4k\" # keep this note"), "managed value was not updated");
    Require(updated.Contains("custom_mod = true"), "unknown setting was not preserved");
    Require(updated.Contains("resolution_scale = 3"), "missing setting was not appended");
    Require(updated.Contains("window_width = 3840"), "output width was not appended");
    Require(File.ReadAllText(configPath + ".launcher-backup") == original, "backup differs from original");

    settings["resolution"] = "\"1080p\"";
    LauncherConfigFile.WriteValues(configPath, settings, order);
    Require(File.ReadAllText(configPath + ".launcher-backup") == original, "first backup was overwritten");

    Dictionary<string, string> roundTrip = LauncherConfigFile.ReadValues(configPath);
    Require(roundTrip["resolution"] == "\"1080p\"", "roundtrip resolution failed");
    Require(roundTrip["custom_mod"] == "true", "roundtrip unknown setting failed");
    Console.WriteLine("Launcher configuration roundtrip passed.");


    foreach (string aa in new[] { "none", "fxaa", "fxaa_extreme" })
    {
        var graphics = GraphicsSettings.Create("4k", "3", "5", aa, "borderless", true);
        Require(graphics["resolution"] == "\"\"", "guest resolution must remain native");
        Require(graphics["window_width"] == "3840" && graphics["window_height"] == "2160", "output dimensions failed");
        Require(graphics["resolution_scale"] == "3", "render scale failed");
        Require(graphics["anisotropic_override"] == "5", "16x filtering enum failed");
        Require(graphics["swap_post_effect"] == $"\"{aa}\"", "AA option failed");
        LauncherConfigFile.WriteValues(configPath, graphics, GraphicsSettings.ManagedKeys);
        Require(LauncherConfigFile.ReadValues(configPath)["swap_post_effect"] == $"\"{aa}\"", "AA roundtrip failed");
    }
    var windowed = GraphicsSettings.Create("720p", "1", "-1", "none", "windowed", false);
    Require(windowed["fullscreen"] == "false" && windowed["vsync"] == "false", "windowed/vsync failed");
    Require(GraphicsSettings.Create("1440p", "2", "4", "fxaa", "exclusive", true)["fullscreen_exclusive"] == "true", "exclusive fullscreen failed");
    RequireThrows(() => GraphicsSettings.Create("8k", "3", "5", "none", "windowed", true), "unsupported resolution accepted");
    RequireThrows(() => GraphicsSettings.Create("4k", "3", "5", "unknown", "windowed", true), "unsupported AA accepted");
    foreach (string cap in GraphicsOptions.FrameLimits.Select(option => option.Value))
    {
        var capped = GraphicsSettings.Create("1080p", "2", "4", "none", "windowed", false, cap);
        Require(capped["frame_limit"] == cap, "FPS limit mapping failed");
        Require(capped["guest_vblank_pacing"] == (cap == "30" ? "true" : "false"), "legacy guest 30-FPS gate was not handled");
        Require(capped["vsync"] == "false", "guest pacing changed host VSync");
        LauncherConfigFile.WriteValues(configPath, capped, GraphicsSettings.ManagedKeys);
        Require(LauncherConfigFile.ReadValues(configPath)["frame_limit"] == cap, "FPS limit roundtrip failed");
        Require(!capped.ContainsKey("video_mode_refresh_rate"), "FPS cap changed guest refresh mode");
    }
    foreach (string invalid in new[] { "-1", "241", "143", "144.5", "unknown" })
        RequireThrows(() => GraphicsSettings.Create("1080p", "2", "4", "none", "windowed", true, invalid), "unsupported FPS preset accepted");
    Require(GraphicsOptions.FrameLimits.Any(option => option.Value == "144"), "144 FPS preset missing");
    Console.WriteLine("30/60/120/144/165/240/unlimited mapping, validation and persistence passed.");

    var preferred = GraphicsSettings.Create("4k", "3", "4", "fxaa", "windowed", false, "60");
    string preferencesPath = Path.Combine(testDirectory, "launcher-settings.toml");
    LauncherConfigFile.WriteValues(preferencesPath, preferred, GraphicsSettings.ManagedKeys);
    // Simulate runtime serialization/replacement, then reopen the launcher.
    File.WriteAllText(configPath, "window_width = 1280\nwindow_height = 720\nresolution_scale = 1\ncustom_mod = true\n");
    var restored = LauncherConfigFile.ReadLauncherValues(testDirectory);
    foreach ((string key, string value) in preferred)
        Require(restored[key] == value, "saved launcher choice was lost: " + key);
    Require(restored["custom_mod"] == "true", "runtime-only option was lost");
    LauncherConfigFile.WriteValues(configPath, restored, GraphicsSettings.ManagedKeys);
    Require(LauncherConfigFile.ReadValues(configPath)["resolution_scale"] == "3", "launch writeback lost scale");
    File.Delete(preferencesPath);
    Require(LauncherConfigFile.ReadLauncherValues(testDirectory)["window_width"] == "3840", "legacy config import failed");
    Console.WriteLine("Launcher preferences survive runtime rewrites; legacy config import passed.");

    string tablePath = Path.Combine(testDirectory, "tables.toml");
    File.WriteAllText(tablePath, "custom = \"keep # literal\"\n[other]\nvsync = false\n");
    LauncherConfigFile.WriteValues(tablePath, windowed, GraphicsSettings.ManagedKeys);
    string tableContent = File.ReadAllText(tablePath);
    Require(tableContent.Contains("[other]\nvsync = false") || tableContent.Contains("[other]\r\nvsync = false"), "unrelated table was modified");
    Require(tableContent.Contains("custom = \"keep # literal\""), "unknown string was changed");
    Require(tableContent.IndexOf("window_width", StringComparison.Ordinal) < tableContent.IndexOf("[other]", StringComparison.Ordinal), "root key appended inside table");
    Require(LauncherConfigFile.ReadValues(tablePath)["vsync"] == "false", "root value not loaded");
    Console.WriteLine("Graphics options, anti-aliasing and table preservation passed.");

    string appXaml = Path.Combine(AppContext.BaseDirectory, "TestData", "App.xaml");
    XNamespace xamlNamespace = "http://schemas.microsoft.com/winfx/2006/xaml/presentation";
    var styles = XDocument.Load(appXaml).Descendants(xamlNamespace + "Style").ToList();
    var textStyle = styles.Single(style => (string?)style.Attribute("TargetType") == "TextBlock");
    Require(!textStyle.Elements(xamlNamespace + "Setter").Any(setter => (string?)setter.Attribute("Property") == "Foreground"),
        "global TextBlock foreground overrides dropdown text inheritance");
    foreach (string control in new[] { "ComboBox", "ComboBoxItem" })
    {
        var style = styles.Single(style => (string?)style.Attribute("TargetType") == control);
        Require(style.Elements(xamlNamespace + "Setter").Any(setter => (string?)setter.Attribute("Property") == "Foreground" && (string?)setter.Attribute("Value") == "#FF172020"), "dropdown foreground regression");
        Require(style.Elements(xamlNamespace + "Setter").Any(setter => (string?)setter.Attribute("Property") == "Background" && (string?)setter.Attribute("Value") == "#FFF3EEE4"), "dropdown background regression");
    }
    Console.WriteLine("Dropdown text inheritance and contrast styles passed.");

    var supported = GameVersions.All.Where(v => v.Compatible && v.CodeGroup == GameVersions.CodeGroup).ToArray();
    foreach (GameVersion version in GameVersions.All)
    {
        var files = version.RequiredFiles.ToHashSet(StringComparer.Ordinal);
        var directories = new HashSet<string>(StringComparer.Ordinal);
        var result = GameCompatibilityInspector.Classify(version.Hash, files.Contains, directories.Contains);
        Require(result.Supported == supported.Contains(version), "catalogue compatibility differs: " + version.Id);
        Require(result.Message == version.Reason, "catalogue reason differs: " + version.Id);
        if (!result.Supported) continue;
        Require(result.Profile == version.Id && result.Label == version.Name + " detected", "catalogue identity differs");
        Require(GameCompatibilityInspector.Classify(version.Hash.ToUpperInvariant(), files.Contains, directories.Contains).Supported,
            "case-insensitive SHA-256 recognition failed");
        foreach (string marker in version.RequiredFiles)
        {
            files.Remove(marker);
            Require(!GameCompatibilityInspector.Classify(version.Hash, files.Contains, directories.Contains).Supported,
                "missing marker accepted: " + marker);
            files.Add(marker);
        }
        if (version.RejectFiles.Length + version.RejectDirectories.Length > 0)
        {
            files.UnionWith(version.RejectFiles);
            directories.UnionWith(version.RejectDirectories);
            Require(!GameCompatibilityInspector.Classify(version.Hash, files.Contains, directories.Contains).Supported,
                "mixed content accepted: " + version.Id);
            foreach (string marker in version.RejectFiles)
            {
                files.Remove(marker);
                Require(GameCompatibilityInspector.Classify(version.Hash, files.Contains, directories.Contains).Supported,
                    "partial rejection rule rejected a matching dump");
                files.Add(marker);
            }
        }
    }
    Require(!GameCompatibilityInspector.Classify("unknown", _ => true, _ => true).Supported, "unknown hash accepted");

    // Extraction hash gate: the only signal available before any content exists.
    foreach (GameVersion version in GameVersions.All)
    {
        (bool gateOk, string gateMessage) = GameCompatibilityInspector.CheckHash(version.Hash);
        Require(gateOk == supported.Contains(version), "extraction hash gate differs: " + version.Id);
        if (gateOk) Require(gateMessage == version.Name, "extraction hash gate identity differs: " + version.Id);
        Require(GameCompatibilityInspector.CheckHash(version.Hash.ToUpperInvariant()).Ok == gateOk,
            "case-insensitive SHA-256 gate failed: " + version.Id);
    }
    (bool unknownGateOk, string unknownGateMessage) = GameCompatibilityInspector.CheckHash(new string('f', 64));
    Require(!unknownGateOk && unknownGateMessage.Contains(new string('f', 64)),
        "unknown hash gate accepted or hid the hash");
    Console.WriteLine("Extraction hash gate (known-good default.xex hashes) passed.");

    // Synthetic GDFX disc image: proves the two-phase, hash-gated extraction
    // path without needing a 6.3 GB retail dump.
    byte[] xexBytes = Encoding.UTF8.GetBytes("SYNTHETIC-DEFAULT-XEX-" + new string('x', 4096));
    byte[] nestedBytes = Encoding.UTF8.GetBytes("hello from data/nested.txt");
    byte[] artBytes = new byte[3000];
    Random.Shared.NextBytes(artBytes);
    // GDFX layout, one region per sector group (no overlaps):
    //   sector 0: root directory, sector 1: data/ directory,
    //   sectors 2-4: default.xex, sectors 5-6: nxeart, sector 7: data/nested.txt
    const long gdfxBase = 0x10000; // headerOffset 0x20000 - 0x10000
    string imagePath = Path.Combine(testDirectory, "synthetic.iso");
    string gdfxOut = Path.Combine(testDirectory, "gdfx-out");
    using (FileStream image = new(imagePath, FileMode.Create))
    {
        image.SetLength(0x20000 + 0x24); // must extend past the GDFX header
        image.Seek(gdfxBase + 0 * 2048, SeekOrigin.Begin);          // root directory (sector 0)
        WriteGdfxEntry(image, 2, (uint)xexBytes.Length, 0x80, "default.xex");
        WriteGdfxEntry(image, 1, 0x800, 0x10, "data");
        WriteGdfxEntry(image, 5, (uint)artBytes.Length, 0x80, "nxeart");
        image.Write(BitConverter.GetBytes(uint.MaxValue));
        image.Seek(gdfxBase + 1 * 2048, SeekOrigin.Begin);          // data/ directory (sector 1)
        WriteGdfxEntry(image, 7, (uint)nestedBytes.Length, 0x80, "nested.txt");
        image.Write(BitConverter.GetBytes(uint.MaxValue));
        image.Seek(gdfxBase + 2 * 2048, SeekOrigin.Begin); image.Write(xexBytes);
        image.Seek(gdfxBase + 5 * 2048, SeekOrigin.Begin); image.Write(artBytes);
        image.Seek(gdfxBase + 7 * 2048, SeekOrigin.Begin); image.Write(nestedBytes);
        image.Seek(0x20000, SeekOrigin.Begin);                      // GDFX header
        image.Write("MICROSOFT*XBOX*MEDIA"u8.ToArray());
        image.Write(BitConverter.GetBytes(0u));                     // rootSector
        image.Write(BitConverter.GetBytes(0x800u));                 // rootSize
        image.Write(BitConverter.GetBytes(0L));                     // creation time
    }
    using (DiscExtraction disc = DiscExtraction.Open(imagePath, _ => { }))
    {
        Require(disc.DefaultXex is not null, "synthetic default.xex was not found");
        var progress = new ExtractionProgress(disc.Plan);
        var rootsDone = new List<string>();
        progress.RootCompleted += name => rootsDone.Add(name);
        string xexHash = disc.ExtractXex(gdfxOut, _ => { }, progress);
        string expectedHash = Convert.ToHexString(SHA256.HashData(xexBytes)).ToLowerInvariant();
        Require(xexHash == expectedHash, "extracted default.xex hash does not match source bytes");
        Require(File.ReadAllBytes(Path.Combine(gdfxOut, "default.xex")).SequenceEqual(xexBytes),
            "extracted default.xex bytes differ");
        (bool syntheticGateOk, string syntheticGateMessage) = GameCompatibilityInspector.CheckHash(xexHash);
        Require(!syntheticGateOk && syntheticGateMessage.Contains(xexHash),
            "synthetic (unsupported) XEX passed the extraction gate");
        (int remainingFiles, long remainingBytes) = disc.ExtractRemaining(gdfxOut, _ => { }, progress);
        Require(remainingFiles == 2 && remainingBytes == nestedBytes.Length + artBytes.Length,
            "remaining extraction counts differ");
        Require(File.ReadAllBytes(Path.Combine(gdfxOut, "data/nested.txt")).SequenceEqual(nestedBytes),
            "nested extracted file bytes differ");
        Require(File.ReadAllBytes(Path.Combine(gdfxOut, "nxeart")).SequenceEqual(artBytes),
            "root extracted file bytes differ");
        // Structured progress: the whole disc is accounted for and every top-level
        // item is reported complete by the time both phases finish.
        Require(progress.TotalBytes == (long)xexBytes.Length + artBytes.Length + nestedBytes.Length,
            "progress total bytes differ");
        Require(progress.CompletedBytes == progress.TotalBytes, "completed bytes do not reach the total");
        Require(progress.Percent == 100.0, "progress did not reach 100%");
        Require(rootsDone.Count == 3, "expected 3 top-level items reported complete");
        foreach (string root in new[] { "default.xex", "data", "nxeart" })
            Require(rootsDone.Contains(root), "root item not reported complete: " + root);

        // Cancellation: flipping the stop flag mid-extraction aborts with a clean
        // stop (ExtractionCancelledException), not a generic error.
        bool stop = false;
        var cancelProgress = new ExtractionProgress(disc.Plan, () => stop);
        string _ = disc.ExtractXex(gdfxOut, _ => { }, cancelProgress);  // completes (not yet stopped)
        stop = true;
        Exception? cancelEx = null;
        try { disc.ExtractRemaining(gdfxOut, _ => { }, cancelProgress); }
        catch (Exception ex) { cancelEx = ex; }
        Require(cancelEx is ExtractionCancelledException,
            "cancellation did not abort with ExtractionCancelledException: "
            + (cancelEx?.GetType().Name ?? "no exception"));
    }
    Console.WriteLine("Synthetic GDFX two-phase extraction (hash-gated) passed.");
    // A synthetic extra locale proves that no classifier branch needs adding.
    GameVersion extra = supported[0] with { Id = "synthetic-locale", Hash = new string('a', 64),
        Name = "Synthetic locale", Language = 6 };
    Require(GameCompatibilityInspector.Classify(extra.Hash, _ => true, _ => false, [extra]).Supported,
        "extensible catalogue classification failed");
    Require(!GameCompatibilityInspector.Classify(extra.Hash, _ => true, _ => false,
        [extra with { Compatible = false }]).Supported, "explicitly denied extra locale accepted");
    Require(!GameCompatibilityInspector.Classify(extra.Hash, _ => true, _ => false,
        [extra with { CodeGroup = "unverified" }]).Supported, "unvalidated guest-code group accepted");

    File.WriteAllText(Path.Combine(testDirectory, "fable_2.exe"), "synthetic fixture, not an executable");
    string descriptorPath = Path.Combine(testDirectory, "fable2_build.json");
    // Marker predicates: every required file present, but never the version's
    // own reject markers, so classification cannot fall into "mixed content".
    bool HasFile(GameVersion version, string path) => !version.RejectFiles.Contains(path, StringComparer.Ordinal);
    bool HasDirectory(GameVersion version, string path) => !version.RejectDirectories.Contains(path, StringComparer.Ordinal);
    foreach (GameVersion version in supported)
    {
        var result = GameCompatibilityInspector.Classify(version.Hash, p => HasFile(version, p), p => HasDirectory(version, p));
        Require(result.Supported, "launch-planner fixture classified unsupported: " + version.Id);
        if (version.Id == GameVersions.DefaultProfile)
            GameLaunchPlanner.Create(testDirectory, testDirectory, result);
        else
            RequireThrows(() => GameLaunchPlanner.Create(testDirectory, testDirectory, result), "legacy executable accepted another profile");
    }
    foreach (GameVersion selected in supported)
    {
        File.WriteAllText(descriptorPath, System.Text.Json.JsonSerializer.Serialize(new { profiles = new[] { selected.Id } }));
        foreach (GameVersion version in supported)
        {
            var result = GameCompatibilityInspector.Classify(version.Hash, p => HasFile(version, p), p => HasDirectory(version, p));
            if (version == selected) GameLaunchPlanner.Create(testDirectory, testDirectory, result);
            else RequireThrows(() => GameLaunchPlanner.Create(testDirectory, testDirectory, result), "wrong single-edition descriptor accepted");
        }
    }
    File.WriteAllText(descriptorPath, System.Text.Json.JsonSerializer.Serialize(new { profiles = supported.Select(v => v.Id) }));
    foreach (GameVersion version in supported)
    {
        var result = GameCompatibilityInspector.Classify(version.Hash, p => HasFile(version, p), p => HasDirectory(version, p));
        var plan = GameLaunchPlanner.Create(testDirectory, testDirectory, result);
        Require(plan.GameRoot == Path.GetFullPath(testDirectory), "game root was not preserved");
    }
    GameVersion malformedProbe = supported[0];
    File.WriteAllText(descriptorPath, "{malformed");
    RequireThrows(() => GameLaunchPlanner.Create(testDirectory, testDirectory,
        GameCompatibilityInspector.Classify(malformedProbe.Hash, p => HasFile(malformedProbe, p), p => HasDirectory(malformedProbe, p))), "malformed descriptor accepted");
    Console.WriteLine("Catalogue-driven allow/deny, marker, extra-locale and native-profile tests passed.");

    // Fable2ConfigFile (the Advanced Options page config, fable2_config.toml).
    string advPath = Path.Combine(testDirectory, "fable2_config.toml");
    var advanced = new Fable2ConfigValues
    {
        KeyboardGamepadMap = "LMB:X,RMB:Y,Shift:A,E:A",
        MouseLook = true,
        MouseLookScale = 512,
        DirectMouseLook = true,
        UnlockWebsite = true,
        UnlockCe = false,
        SkipIntroVideos = true,
        Interpolation = false,
        DisableMotionBlur = true,
        RealtimeTextureMorphing = true,
        HeroDogTextureReadback = false,
        HotFuncYieldEvery = 4,
    };
    Fable2ConfigFile.Write(advPath, advanced);
    string writtenAdv = File.ReadAllText(advPath);
    Require(writtenAdv.Contains("config_version = 1"), "config_version was not written");
    var readBack = Fable2ConfigFile.Read(advPath);
    Require(readBack.MouseLook, "mouse look roundtrip failed");
    Require(readBack.MouseLookScale == 512, "mouse scale roundtrip failed");
    Require(readBack.DirectMouseLook, "direct mouse look roundtrip failed");
    Require(!readBack.UnlockCe, "unlock_ce roundtrip failed");
    Require(readBack.SkipIntroVideos, "skip_intro_videos roundtrip failed");
    Require(readBack.DisableMotionBlur, "disable_motion_blur roundtrip failed");
    Require(!readBack.Interpolation, "interpolation roundtrip failed");
    Require(readBack.HotFuncYieldEvery == 4, "hotfunc_yield_every roundtrip failed");
    Require(writtenAdv.Contains("[perf]"), "[perf] section was not written");
    List<KeybindEntry> parsed = Fable2ConfigFile.ParseKeybinds(readBack.KeyboardGamepadMap);
    Require(parsed.Count == 4, "keybind parse count failed");
    Require(parsed[0].Key == "LMB" && parsed[0].Button == "X" && parsed[3].Key == "E" && parsed[3].Button == "A",
        "keybind parse failed");
    Require(string.Equals(Fable2ConfigFile.BuildKeybinds(Fable2ConfigFile.ParseKeybinds("LMB:X,E:A")), "LMB:X,E:A"),
        "keybind build roundtrip failed");
    Require(string.IsNullOrEmpty(Fable2ConfigFile.BuildKeybinds(Fable2ConfigFile.ParseKeybinds(""))),
        "empty map should build to empty");
    // Second write: update a value and preserve unknown sections/keys.
    File.AppendAllText(advPath, "\n[custom_section]\nsome_key = \"keep me\"\n");
    advanced.MouseLookScale = 1024;
    Fable2ConfigFile.Write(advPath, advanced);
    string advContent = File.ReadAllText(advPath);
    Require(advContent.Contains("mouse_look_scale = 1024"), "mouse scale update failed");
    Require(advContent.Contains("[custom_section]") && advContent.Contains("some_key = \"keep me\""),
        "unknown section/keys not preserved");
    Require(Fable2ConfigFile.Read(advPath).MouseLookScale == 1024, "mouse scale re-read failed");
    Require(File.Exists(advPath + ".launcher-backup"), "fable2_config backup missing");
    Console.WriteLine("Advanced options (fable2_config.toml) roundtrip and preservation passed.");

    if (args.Length > 0)
    {
        GameCompatibility compatibility = GameCompatibilityInspector.Inspect(args[0]);
        Console.WriteLine($"Compatibility probe: {compatibility.Label} — {compatibility.Message}");
        if (args.Contains("--expect-german-goty", StringComparer.OrdinalIgnoreCase))
            Require(compatibility.Label == "German GOTY detected", "German GOTY profile was not recognized");
    }
}
finally
{
    Directory.Delete(testDirectory, recursive: true);
}

static void Require(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

static void RequireThrows(Action action, string message)
{
    try { action(); }
    catch { return; }
    throw new InvalidOperationException(message);
}

// GDFX directory entry: u32 unknown, u32 sector, u32 size, u8 attributes,
// u8 nameLen, name bytes (little-endian, per tools/x360extract/Gdfx.cs).
static void WriteGdfxEntry(Stream stream, uint sector, uint size, byte attributes, string name)
{
    byte[] nameBytes = Encoding.ASCII.GetBytes(name);
    stream.Write(BitConverter.GetBytes(0u));
    stream.Write(BitConverter.GetBytes(sector));
    stream.Write(BitConverter.GetBytes(size));
    stream.WriteByte(attributes);
    stream.WriteByte((byte)nameBytes.Length);
    stream.Write(nameBytes);
    // Entries are 4-byte aligned in GDFX directories.
    int padding = (4 - (14 + nameBytes.Length) % 4) % 4;
    for (int i = 0; i < padding; i++) stream.WriteByte(0);
}
