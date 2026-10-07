using Microsoft.Win32;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Windows;
using System.Windows.Controls;

namespace Fable2Launcher;

public partial class MainWindow : Window
{
    private readonly Dictionary<string, string> _values = new(StringComparer.OrdinalIgnoreCase);
    private string? _gameDirectory;
    private string? _executableDirectory;
    private bool _loading = true;

    public MainWindow()
    {
        InitializeComponent();
        SetDefaults();
        DetectGameDirectory();
        _loading = false;
        RefreshSummary();
    }

    private void SetDefaults()
    {
        SelectByTag(ResolutionCombo, "1080p");
        SelectByTag(RenderScaleCombo, "2");
        SelectByTag(AnisotropicCombo, "5");
        SelectByTag(AntiAliasingCombo, "none");
        SelectByTag(DisplayModeCombo, "borderless");
        SelectByTag(BackendCombo, "d3d12");
        if (FrameLimitCombo.Items.Count == 0)
            foreach (var option in Constants.GraphicsOptions.FrameLimits)
                FrameLimitCombo.Items.Add(new ComboBoxItem { Tag = option.Value, Content = option.Label });
        SelectByTag(FrameLimitCombo, "0");
        VsyncCheck.IsChecked = true;
    }

    private void DetectGameDirectory()
    {
        string launcherDirectory = AppContext.BaseDirectory;
        string? configuredPath = LauncherState.LoadGameDirectory();
        if (!string.IsNullOrWhiteSpace(configuredPath) &&
            File.Exists(Path.Combine(configuredPath, "default.xex")) &&
            (File.Exists(Path.Combine(launcherDirectory, "fable_2.exe")) ||
             File.Exists(Path.Combine(configuredPath, "fable_2.exe"))))
        {
            SetGameDirectory(configuredPath);
            return;
        }
        StatusText.Text = "Choose your original gamefiles folder (default.xex and data).";
    }

    private void SetGameDirectory(string directory)
    {
        _gameDirectory = Path.GetFullPath(directory);
        _executableDirectory = File.Exists(Path.Combine(AppContext.BaseDirectory, "fable_2.exe"))
            ? AppContext.BaseDirectory : _gameDirectory;
        GamePathText.Text = _gameDirectory;
        LauncherState.SaveGameDirectory(_gameDirectory);
        try
        {
            LoadConfiguration();
            GameCompatibility compatibility = GameCompatibilityInspector.Inspect(_gameDirectory);
            GameLaunchPlanner.Create(_executableDirectory, _gameDirectory, compatibility);
            StatusText.Text = compatibility.Message;
            ConfigStateText.Text = compatibility.Label;
        }
        catch (Exception exception)
        {
            StatusText.Text = exception.Message;
            ConfigStateText.Text = "Not ready to launch";
        }
    }

    private void LoadConfiguration()
    {
        if (_gameDirectory is null) return;

        _loading = true;
        try
        {
            _values.Clear();
            SetDefaults();
            foreach ((string key, string value) in LauncherConfigFile.ReadLauncherValues(_executableDirectory!))
                _values[key] = value;

            string outputResolution = (GetValue("window_width", ""), GetValue("window_height", "")) switch
            {
                ("1280", "720") => "720p",
                ("1920", "1080") => "1080p",
                ("2560", "1440") => "1440p",
                ("3840", "2160") => "4k",
                _ => Unquote(GetValue("resolution", "1080p"))
            };
            SelectByTag(ResolutionCombo, outputResolution);
            SelectByTag(RenderScaleCombo, GetValue("resolution_scale", "2"));
            SelectByTag(AnisotropicCombo, GetValue("anisotropic_override", "5"));
            SelectByTag(AntiAliasingCombo, Unquote(GetValue("swap_post_effect", "none")));
            SelectByTag(FrameLimitCombo, GetValue("frame_limit", "0"));
            SelectByTag(BackendCombo, Unquote(GetValue("gpu_backend", "d3d12")));

            bool fullscreen = ParseBool(GetValue("fullscreen", "true"), true);
            bool exclusive = ParseBool(GetValue("fullscreen_exclusive", "false"), false);
            SelectByTag(DisplayModeCombo, !fullscreen ? "windowed" : exclusive ? "exclusive" : "borderless");
            VsyncCheck.IsChecked = ParseBool(GetValue("vsync", "true"), true);

        }
        finally { _loading = false; }
        RefreshSummary();
        ConfigStateText.Text = "Settings loaded";
    }

    private string GetValue(string key, string fallback) =>
        _values.TryGetValue(key, out string? value) ? value : fallback;

    private static string Unquote(string value) => value.Trim().Trim('"', '\'');

    private static bool ParseBool(string value, bool fallback) =>
        bool.TryParse(Unquote(value), out bool parsed) ? parsed : fallback;

    private static string? SelectedTag(ComboBox combo) =>
        (combo.SelectedItem as ComboBoxItem)?.Tag?.ToString();

    private static void SelectByTag(ComboBox combo, string tag)
    {
        foreach (object item in combo.Items)
        {
            if (item is ComboBoxItem comboItem &&
                string.Equals(comboItem.Tag?.ToString(), tag, StringComparison.OrdinalIgnoreCase))
            {
                combo.SelectedItem = comboItem;
                return;
            }
        }
        // An unknown/missing value must not silently select the first (lowest)
        // resolution or render scale. Keep the explicit default/current choice.
    }

    private Dictionary<string, string> CollectSettings()
    {
        return GraphicsSettings.Create(SelectedTag(ResolutionCombo) ?? "1080p",
            SelectedTag(RenderScaleCombo) ?? "2", SelectedTag(AnisotropicCombo) ?? "5",
            SelectedTag(AntiAliasingCombo) ?? "none", SelectedTag(DisplayModeCombo) ?? "borderless",
            VsyncCheck.IsChecked == true, SelectedTag(FrameLimitCombo) ?? "0",
            SelectedTag(BackendCombo) ?? "d3d12");
    }

    private bool SaveConfiguration()
    {
        if (_gameDirectory is null || _executableDirectory is null || !File.Exists(Path.Combine(_executableDirectory, "fable_2.exe")))
        {
            MessageBox.Show(this, "Choose the folder containing fable_2.exe first.",
                "Game not found", MessageBoxButton.OK, MessageBoxImage.Warning);
            return false;
        }

        string path = Path.Combine(_executableDirectory, "fable_2.toml");
        Dictionary<string, string> settings = CollectSettings();
        try
        {
            LauncherConfigFile.WriteValues(path, settings, GraphicsSettings.ManagedKeys);
            LauncherConfigFile.WriteValues(Path.Combine(_executableDirectory,
                "launcher-settings.toml"), settings, GraphicsSettings.ManagedKeys);
            _values.Clear();
            foreach ((string key, string value) in settings) _values[key] = value;
        }
        catch (Exception exception)
        {
            MessageBox.Show(this, exception.Message, "Could not save settings",
                MessageBoxButton.OK, MessageBoxImage.Error);
            return false;
        }

        StatusText.Text = $"Saved {Path.GetFileName(path)}.";
        ConfigStateText.Text = "Settings saved";
        return true;
    }

    private void LaunchGame()
    {
        if (_gameDirectory is null || _executableDirectory is null) return;

        try
        {
            var plan = GameLaunchPlanner.Create(_executableDirectory, _gameDirectory,
                GameCompatibilityInspector.Inspect(_gameDirectory));
            if (!SaveConfiguration()) return;
            var startInfo = new ProcessStartInfo
            {
                FileName = plan.Executable,
                WorkingDirectory = plan.WorkingDirectory,
                UseShellExecute = false
            };
            startInfo.ArgumentList.Add("--game_data_root=" + plan.GameRoot);
            Process.Start(startInfo);
            StatusText.Text = "Fable II launched.";
            ConfigStateText.Text = "Game running";
        }
        catch (Exception exception)
        {
            MessageBox.Show(this, exception.Message, "Could not launch Fable II",
                MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void RefreshSummary(bool markDirty = false)
    {
        int scale = int.TryParse(SelectedTag(RenderScaleCombo), NumberStyles.Integer,
            CultureInfo.InvariantCulture, out int parsedScale) ? parsedScale : 1;
        InternalResolutionText.Text = $"Internal: {1280 * scale:N0} × {720 * scale:N0}";

        string output = SelectedTag(ResolutionCombo) switch
        {
            "720p" => "1280 × 720",
            "1080p" => "1920 × 1080",
            "1440p" => "2560 × 1440",
            "4k" => "3840 × 2160",
            _ => "Automatic"
        };
        OutputResolutionText.Text = $"Output: {output}";

        if (!_loading && markDirty)
        {
            ConfigStateText.Text = "Unsaved changes";
        }
    }

    private void SettingChanged(object sender, RoutedEventArgs e)
    {
        if (!_loading) RefreshSummary(markDirty: true);
    }

    private void SettingChanged(object sender, SelectionChangedEventArgs e)
    {
        if (!_loading) RefreshSummary(markDirty: true);
    }

    private void PresetClicked(object sender, RoutedEventArgs e)
    {
        string preset = (sender as Button)?.Tag?.ToString() ?? "balanced";
        switch (preset)
        {
            case "original":
                SelectByTag(ResolutionCombo, "720p");
                SelectByTag(RenderScaleCombo, "1");
                SelectByTag(AnisotropicCombo, "-1");
                PresetDescription.Text = "Original Xbox 360 rendering: 720p internal and game-controlled texture filtering.";
                break;
            case "balanced":
                SelectByTag(ResolutionCombo, "1080p");
                SelectByTag(RenderScaleCombo, "2");
                SelectByTag(AnisotropicCombo, "4");
                PresetDescription.Text = "1440p internal rendering with 8× anisotropic filtering, presented at 1080p.";
                break;
            case "enhanced":
                SelectByTag(ResolutionCombo, "4k");
                SelectByTag(RenderScaleCombo, "3");
                SelectByTag(AnisotropicCombo, "5");
                PresetDescription.Text = "True 4K internal rendering with 16× anisotropic filtering. Recommended for modern GPUs.";
                break;
            case "ultra":
                SelectByTag(ResolutionCombo, "4k");
                SelectByTag(RenderScaleCombo, "4");
                SelectByTag(AnisotropicCombo, "5");
                PresetDescription.Text = "5K internal supersampling down to 4K. Experimental and very GPU-memory intensive.";
                break;
        }
        SelectByTag(DisplayModeCombo, "borderless");
        SelectByTag(AntiAliasingCombo, "none");
        VsyncCheck.IsChecked = true;
        RefreshSummary(markDirty: true);
    }

    private void BrowseClicked(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFolderDialog
        {
            Title = "Choose original gamefiles (default.xex and data)",
            Multiselect = false
        };
        if (dialog.ShowDialog(this) == true)
        {
            if (!File.Exists(Path.Combine(dialog.FolderName, "default.xex")))
            {
                MessageBox.Show(this, "This folder does not contain default.xex.",
                    "Game not found", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }
            SetGameDirectory(dialog.FolderName);
        }
    }

    private void SaveClicked(object sender, RoutedEventArgs e) => SaveConfiguration();
    private void LaunchClicked(object sender, RoutedEventArgs e) => LaunchGame();
}

internal static class LauncherState
{
    private static readonly string LocalStatePath = Path.Combine(AppContext.BaseDirectory,
        "launcher-game-path.txt");

    public static string? LoadGameDirectory()
    {
        try
        {
            if (File.Exists(LocalStatePath)) return File.ReadAllText(LocalStatePath).Trim();
            return null;
        }
        catch { return null; }
    }

    public static void SaveGameDirectory(string directory)
    {
        try
        {
            File.WriteAllText(LocalStatePath, directory);
            return;
        }
        catch
        {
            // Graphics saving reports write failures separately. No global
            // fallback: different installations must not inherit other paths.
        }
    }
}
