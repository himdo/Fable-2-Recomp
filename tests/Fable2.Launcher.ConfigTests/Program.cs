using Fable2Launcher;
using Fable2Launcher.Constants;
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

    Require(GraphicsSettings.Create("1080p", "2", "4", "none", "windowed", true)["gpu_backend"] == "\"d3d12\"",
        "default renderer is not D3D12");
    var vulkan = GraphicsSettings.Create("1080p", "2", "4", "none", "windowed", true, "0", "vulkan");
    Require(vulkan["gpu_backend"] == "\"vulkan\"", "Vulkan renderer mapping failed");
    LauncherConfigFile.WriteValues(configPath, vulkan, GraphicsSettings.ManagedKeys);
    Require(LauncherConfigFile.ReadValues(configPath)["gpu_backend"] == "\"vulkan\"", "renderer roundtrip failed");
    RequireThrows(() => GraphicsSettings.Create("1080p", "2", "4", "none", "windowed", true, "0", "opengl"),
        "unsupported renderer accepted");
    Console.WriteLine("Renderer selection mapping, validation and persistence passed.");

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
    foreach (GameVersion version in supported)
    {
        var result = GameCompatibilityInspector.Classify(version.Hash, _ => true, _ => false);
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
            var result = GameCompatibilityInspector.Classify(version.Hash, _ => true, _ => false);
            if (version == selected) GameLaunchPlanner.Create(testDirectory, testDirectory, result);
            else RequireThrows(() => GameLaunchPlanner.Create(testDirectory, testDirectory, result), "wrong single-edition descriptor accepted");
        }
    }
    File.WriteAllText(descriptorPath, System.Text.Json.JsonSerializer.Serialize(new { profiles = supported.Select(v => v.Id) }));
    foreach (GameVersion version in supported)
    {
        var result = GameCompatibilityInspector.Classify(version.Hash, _ => true, _ => false);
        var plan = GameLaunchPlanner.Create(testDirectory, testDirectory, result);
        Require(plan.GameRoot == Path.GetFullPath(testDirectory), "game root was not preserved");
    }
    File.WriteAllText(descriptorPath, "{malformed");
    RequireThrows(() => GameLaunchPlanner.Create(testDirectory, testDirectory,
        GameCompatibilityInspector.Classify(supported[0].Hash, _ => true, _ => false)), "malformed descriptor accepted");
    Console.WriteLine("Catalogue-driven allow/deny, marker, extra-locale and native-profile tests passed.");

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
