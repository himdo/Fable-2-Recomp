using Microsoft.Win32;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;

namespace Fable2Launcher;

public partial class MainWindow : Window
{
    private readonly Dictionary<string, string> _values = new(StringComparer.OrdinalIgnoreCase);
    private string? _gameDirectory;
    private string? _executableDirectory;
    private bool _loading = true;

    // Advanced Options page state.
    private Fable2ConfigValues _advanced = new();
    private readonly List<Border> _keyFields = new();
    private readonly List<ComboBox> _buttonCombos = new();
    private readonly List<(string key, CheckBox check)> _patchChecks = new();
    private Border? _activeKeyField;
    private static readonly SolidColorBrush MutedBrush = new(Color.FromRgb(0x8A, 0x99, 0x92));
    // Red accent for unstable/experimental patches.
    private static readonly SolidColorBrush UnstableBrush = new(Color.FromRgb(0xE0, 0x66, 0x66));

    // Keybind capture field: the press-to-set host key box.
    private sealed class KeyCaptureField
    {
        public Border Border = null!;
        public TextBlock Text = null!;
        public string Value = "";
    }

    private static readonly Brush KeyIdleBackground = new SolidColorBrush(Color.FromRgb(0xF3, 0xEE, 0xE4));
    private static readonly Brush KeyIdleBorder = new SolidColorBrush(Color.FromArgb(0x88, 0x7C, 0x6A, 0x48));
    private static readonly Brush KeyListeningBackground = new SolidColorBrush(Color.FromRgb(0xE8, 0xF0, 0xF8));
    private static readonly Brush KeyListeningBorder = new SolidColorBrush(Color.FromRgb(0x4A, 0x90, 0xD9));
    private static readonly Brush KeyFieldText = new SolidColorBrush(Color.FromRgb(0x17, 0x20, 0x20));

    public MainWindow()
    {
        InitializeComponent();
        PreviewKeyDown += MainWindow_PreviewKeyDown;
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
        // No saved (or no longer valid) game folder: default the selected location
        // to the folder the launcher is running from, so it is never empty on first
        // launch. The status line then explains what is still missing (e.g. that
        // default.xex has not been extracted yet).
        SetGameDirectory(launcherDirectory);
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
            VsyncCheck.IsChecked == true, SelectedTag(FrameLimitCombo) ?? "0");
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
        if (_extracting) return;
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
            // The game is a separate process, so the launcher can close itself
            // now that it has started. Only close on a successful launch.
            Close();
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

    private bool _extracting;
    private volatile bool _stopRequested;
    // Non-null while the "unrecognized XEX" prompt is on screen; the worker
    // awaits its Task so the user can choose to extract anyways or stop.
    private TaskCompletionSource<bool>? _hashDecision;
    private readonly Dictionary<string, CheckBox> _rootChecks = new(StringComparer.OrdinalIgnoreCase);
    private long _lastUiPush;
    private double _lastPushedPercent = -1;
    private static readonly SolidColorBrush DoneBrush = new(Color.FromRgb(0x7F, 0xE0, 0xA6));
    private static readonly SolidColorBrush ItemBrush = new(Color.FromRgb(0xD0, 0xD8, 0xD2));

    private void ExtractClicked(object sender, RoutedEventArgs e)
    {
        if (_extracting) return;

        var isoDialog = new OpenFileDialog
        {
            Title = "Choose an ISO",
            Filter = "ISO (*.iso)|*.iso",
            CheckFileExists = true
        };
        if (isoDialog.ShowDialog(this) != true) return;
        string isoPath = isoDialog.FileName;

        // No destination prompt: extract straight into the folder the launcher is
        // running from, so the game data lands beside fable_2.exe and becomes
        // launchable in place.
        string outDir = AppContext.BaseDirectory;

        _extracting = true;
        _stopRequested = false;
        ExtractButton.IsEnabled = false;
        LaunchButton.IsEnabled = false;
        StopButton.IsEnabled = true;

        // Show the overlay immediately (indeterminate) so there is no perceptible
        // delay; the folder checklist and byte total fill in once the disc layout
        // has been read on the worker thread.
        ExtractionOverlay.Visibility = Visibility.Visible;
        ExtractionProgressBar.IsIndeterminate = true;
        ExtractionPercentText.Text = "…";
        ExtractionBytesText.Text = "Scanning…";
        ExtractionPhaseText.Text = "Scanning disc…";
        ExtractionFileText.Text = "Scanning for the GDFX header…";
        ExtractionFolderList.Children.Clear();
        _rootChecks.Clear();
        HashPrompt.Visibility = Visibility.Collapsed;

        Task.Run(async () =>
        {
            string? error = null;
            int files = 0;
            long bytes = 0;
            bool cancelled = false;
            try
            {
                using DiscExtraction disc = DiscExtraction.Open(isoPath, _ => { });
                List<RootItem> plan = disc.Plan.ToList();

                var progress = new ExtractionProgress(plan, () => _stopRequested);
                progress.Changed += p => Dispatcher.BeginInvoke(() => PushExtractionUi(p));
                progress.RootCompleted += name =>
                    Dispatcher.BeginInvoke(new Action(() => MarkRootDone(name)));

                // If Stop was pressed while the layout was being read, bail now.
                if (_stopRequested)
                    throw new ExtractionCancelledException();

                // The overlay is already on screen (indeterminate); now that the
                // layout is known, build the checklist and switch to the byte-
                // accurate determinate bar.
                Dispatcher.Invoke(() =>
                {
                    BuildFolderChecklist(plan);
                    ResetExtractionUi(progress);
                });

                // Extract the XEX under a temporary name so that a failed hash
                // check (or a user declining to extract) never clobbers an
                // existing default.xex already in the launcher folder.
                const string xexNewName = "default_new.xex";
                string xexNew = Path.Combine(outDir, xexNewName);
                string hash = disc.ExtractXex(outDir, _ => { }, progress, xexNewName);
                progress.SetPhase("Checking default.xex SHA-256…");
                (bool ok, string message) = GameCompatibilityInspector.CheckHash(hash);
                if (!ok)
                {
                    // The XEX is not a supported version. Instead of bailing out
                    // with a message box, show the reason on the overlay and let
                    // the user choose to extract the disc anyways (keeping the
                    // XEX) or stop.
                    var decision = new TaskCompletionSource<bool>(
                        TaskCreationOptions.RunContinuationsAsynchronously);
                    _hashDecision = decision;
                    Dispatcher.Invoke(() => ShowHashPrompt(message));
                    bool extractAnyways = await decision.Task;
                    if (ReferenceEquals(_hashDecision, decision))
                        _hashDecision = null;
                    if (!extractAnyways)
                    {
                        // Declined: remove the temporary XEX, leaving any existing
                        // default.xex in the launcher folder untouched.
                        try { File.Delete(xexNew); }
                        catch { /* best effort */ }
                        throw new ExtractionCancelledException();
                    }
                }

                // Promote the temporary XEX to default.xex (overwriting any
                // existing one) now that the extraction will continue.
                try
                {
                    File.Move(xexNew, Path.Combine(outDir, "default.xex"), overwrite: true);
                }
                catch (Exception moveEx)
                {
                    throw new InvalidDataException(
                        "Could not place default.xex: " + moveEx.Message);
                }

                (files, bytes) = disc.ExtractRemaining(outDir, _ => { }, progress);
                progress.SetPhase("Finalizing…");
            }
            catch (ExtractionCancelledException)
            {
                cancelled = true;
            }
            catch (Exception exception)
            {
                error = exception.Message;
            }

            Dispatcher.Invoke(() =>
            {
                _extracting = false;
                _stopRequested = false;
                ExtractButton.IsEnabled = true;
                LaunchButton.IsEnabled = true;
                if (cancelled)
                {
                    ExtractionOverlay.Visibility = Visibility.Collapsed;
                    ExtractionStatusText.Visibility = Visibility.Visible;
                    ExtractionStatusText.Text = "Extraction stopped.";
                    return;
                }
                if (error is not null)
                {
                    ExtractionOverlay.Visibility = Visibility.Collapsed;
                    ExtractionStatusText.Visibility = Visibility.Visible;
                    ExtractionStatusText.Text = "Extraction failed.";
                    MessageBox.Show(this, error, "Could not extract ISO",
                        MessageBoxButton.OK, MessageBoxImage.Error);
                    return;
                }
                ExtractionProgressBar.Value = 100;
                ExtractionPercentText.Text = "100%";
                ExtractionPhaseText.Text = "Done";
                ExtractionStatusText.Visibility = Visibility.Visible;
                ExtractionStatusText.Text =
                    $"Extracted {files:N0} files ({DiscExtraction.FormatSize(bytes)}) from {Path.GetFileName(isoPath)}.";
                // SetGameDirectory reports readiness (including whether fable_2.exe
                // is staged) in the status line and configuration badge.
                SetGameDirectory(outDir);
                HideOverlayAfter(900);
            });
        });
    }

    private void StopClicked(object sender, RoutedEventArgs e)
    {
        if (!_extracting) return;
        if (_hashDecision is not null)
        {
            DeclineHashDecision();
            return;
        }
        _stopRequested = true;
        StopButton.IsEnabled = false;
        ExtractionPhaseText.Text = "Stopping…";
    }

    // The large "Cancel" button on the "unrecognized XEX" prompt: declines the
    // extraction (same as the top Stop button while the prompt is showing).
    private void CancelClicked(object sender, RoutedEventArgs e)
    {
        DeclineHashDecision();
    }

    // Decline the "unrecognized XEX" prompt: the worker deletes the XEX and
    // reports the extraction as stopped.
    private void DeclineHashDecision()
    {
        if (_hashDecision is null) return;
        _hashDecision.TrySetResult(false);
        StopButton.IsEnabled = false;
        HashPrompt.Visibility = Visibility.Collapsed;
        ExtractionPhaseText.Text = "Stopping…";
    }

    private void ExtractAnywaysClicked(object sender, RoutedEventArgs e)
    {
        if (_hashDecision is null) return;
        _hashDecision.TrySetResult(true);
        HashPrompt.Visibility = Visibility.Collapsed;
        ExtractionPhaseText.Text = "Extracting…";
    }

    // Show the "unrecognized XEX" prompt on the overlay (called on the UI
    // thread via Dispatcher.Invoke from the worker).
    private void ShowHashPrompt(string message)
    {
        HashPromptText.Text = message;
        ExtractionPhaseText.Text = "Unrecognized default.xex";
        StopButton.IsEnabled = true;   // the user can still abort
        HashPrompt.Visibility = Visibility.Visible;
    }

    private void BuildFolderChecklist(List<RootItem> plan)
    {
        ExtractionFolderList.Children.Clear();
        _rootChecks.Clear();
        foreach (RootItem item in plan)
        {
            var box = new CheckBox
            {
                Content = item.Name,
                IsChecked = false,
                IsHitTestVisible = false,   // display-only progress checkbox
                FontSize = 14,
                FontWeight = item.IsDirectory ? FontWeights.SemiBold : FontWeights.Normal,
                Foreground = ItemBrush,
                Margin = new Thickness(0, 3, 0, 3),
                VerticalContentAlignment = VerticalAlignment.Center
            };
            ExtractionFolderList.Children.Add(box);
            _rootChecks[item.Name] = box;
        }
    }

    private void ResetExtractionUi(ExtractionProgress progress)
    {
        _lastPushedPercent = -1;
        ExtractionProgressBar.IsIndeterminate = false;
        ExtractionProgressBar.Value = 0;
        ExtractionPercentText.Text = "0%";
        ExtractionBytesText.Text =
            $"{DiscExtraction.FormatSize(0)} / {DiscExtraction.FormatSize(progress.TotalBytes)}";
        ExtractionPhaseText.Text = "Reading disc…";
        ExtractionFileText.Text = "Reading disc…";
    }

    private void PushExtractionUi(ExtractionProgress progress)
    {
        long now = Environment.TickCount64;
        double pct = progress.Percent;
        bool force = pct >= 100.0 || now - _lastUiPush >= 80 || pct - _lastPushedPercent >= 0.25;
        if (!force) return;
        _lastUiPush = now;
        _lastPushedPercent = pct;

        ExtractionProgressBar.Value = pct;
        ExtractionPercentText.Text = $"{pct:0}%";
        ExtractionBytesText.Text =
            $"{DiscExtraction.FormatSize(progress.CompletedBytes + progress.CurrentFileWritten)}"
            + $" / {DiscExtraction.FormatSize(progress.TotalBytes)}";
        ExtractionPhaseText.Text = progress.Phase;
        ExtractionFileText.Text = progress.CurrentFile ?? "…";
    }

    private void MarkRootDone(string name)
    {
        if (!_rootChecks.TryGetValue(name, out CheckBox? box)) return;
        box.IsChecked = true;
        box.Foreground = DoneBrush;
    }

    private void HideOverlayAfter(int delayMs)
    {
        var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(delayMs) };
        timer.Tick += (_, _) =>
        {
            timer.Stop();
            if (!_extracting)   // a new extraction may have started in the meantime
                ExtractionOverlay.Visibility = Visibility.Collapsed;
        };
        timer.Start();
    }

    private void SaveClicked(object sender, RoutedEventArgs e) => SaveConfiguration();
    private void LaunchClicked(object sender, RoutedEventArgs e) => LaunchGame();

    // ---- Advanced Options page ----

    private string AdvancedConfigPath() =>
        Path.Combine(_executableDirectory ?? AppContext.BaseDirectory, "fable2_config.toml");

    private void SetPage(bool advanced)
    {
        MainContent.Visibility = advanced ? Visibility.Collapsed : Visibility.Visible;
        AdvancedContent.Visibility = advanced ? Visibility.Visible : Visibility.Collapsed;
        MainFooter.Visibility = advanced ? Visibility.Collapsed : Visibility.Visible;
        AdvancedFooter.Visibility = advanced ? Visibility.Visible : Visibility.Collapsed;
        AdvancedOptionsButton.Visibility = advanced ? Visibility.Collapsed : Visibility.Visible;
    }

    private void AdvancedOptionsClicked(object sender, RoutedEventArgs e)
    {
        LoadAdvanced();
        SetPage(true);
    }

    private void BackClicked(object sender, RoutedEventArgs e) => SetPage(false);

    private void LoadAdvanced()
    {
        _advanced = Fable2ConfigFile.Read(AdvancedConfigPath());
        _loading = true;
        try
        {
            MouseLookCheck.IsChecked = _advanced.MouseLook;
            DirectMouseLookCheck.IsChecked = _advanced.DirectMouseLook;
            MouseSensitivitySlider.Value = _advanced.MouseLookScale;
            MouseSensitivityBox.Text = _advanced.MouseLookScale.ToString(CultureInfo.InvariantCulture);
            HotFuncYieldSlider.Value = Math.Clamp(_advanced.HotFuncYieldEvery, 0, 16);
            HotFuncYieldBox.Text = _advanced.HotFuncYieldEvery.ToString(CultureInfo.InvariantCulture);
            BuildKeybindRows(Fable2ConfigFile.ParseKeybinds(_advanced.KeyboardGamepadMap));
            BuildPatchRows();
        }
        finally { _loading = false; }
        AdvancedStatusText.Text = "Editing fable2_config.toml (unsaved)";
    }

    // Build one "press-to-capture host key -> guest input" row in the KeybindList.
    private void AddKeybindRow(string key, string button)
    {
        var grid = new Grid { Margin = new Thickness(0, 0, 0, 6) };
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(12) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

        // Press-to-capture host key field: click it, then press the key or mouse
        // button you want to bind (Escape cancels the capture).
        var capture = new KeyCaptureField { Value = key };
        var keyField = new Border
        {
            Background = KeyIdleBackground,
            BorderBrush = KeyIdleBorder,
            BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(4),
            MinHeight = 38,
            Padding = new Thickness(10, 4, 10, 4),
            Cursor = Cursors.Hand,
            SnapsToDevicePixels = true,
            Tag = capture,
        };
        capture.Border = keyField;
        capture.Text = new TextBlock
        {
            Text = string.IsNullOrWhiteSpace(key) ? "press a key…" : key,
            Foreground = KeyFieldText,
            HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Center,
            FontSize = 14,
        };
        keyField.Child = capture.Text;
        // A single MouseDown handler captures every mouse button via
        // e.ChangedButton (WPF doesn't expose per-button routed events for the
        // middle and X buttons on a plain Border). The first LMB press enters
        // listening mode; any press while listening sets that button as the key.
        keyField.MouseDown += (s, e) =>
        {
            string? button = e.ChangedButton switch
            {
                MouseButton.Left => "LMB",
                MouseButton.Right => "RMB",
                MouseButton.Middle => "MMB",
                MouseButton.XButton1 => "XMB1",
                MouseButton.XButton2 => "XMB2",
                _ => null
            };
            if (button == null) return;

            if (button == "LMB" && !ReferenceEquals(_activeKeyField, keyField))
            {
                BeginKeyCapture(keyField);   // first LMB click enters listening mode
                return;
            }
            if (ReferenceEquals(_activeKeyField, keyField))
                SetKeyCaptureValue(keyField, button);
        };


        var buttonCombo = BuildKeyCombo(Fable2ConfigFile.ButtonNames, button);

        var removeButton = new Button
        {
            Content = "✕",
            MinWidth = 30,
            ToolTip = "Remove this binding",
            Style = (Style)FindResource("SecondaryButton"),
        };
        removeButton.Click += (s, e) =>
        {
            KeybindList.Children.Remove(grid);
            _keyFields.Remove(keyField);
            _buttonCombos.Remove(buttonCombo);
            if (ReferenceEquals(_activeKeyField, keyField)) _activeKeyField = null;
        };

        Grid.SetColumn(keyField, 0);
        Grid.SetColumn(buttonCombo, 2);
        Grid.SetColumn(removeButton, 3);
        grid.Children.Add(keyField);
        grid.Children.Add(buttonCombo);
        grid.Children.Add(removeButton);

        KeybindList.Children.Add(grid);
        _keyFields.Add(keyField);
        _buttonCombos.Add(buttonCombo);
    }

    // Enter listening mode on the host key field: the next key or mouse button
    // press sets the binding. Only one field listens at a time.
    private void BeginKeyCapture(Border field)
    {
        if (_activeKeyField != null && _activeKeyField != field) SetKeyCaptureIdle(_activeKeyField);
        _activeKeyField = field;
        field.Background = KeyListeningBackground;
        field.BorderBrush = KeyListeningBorder;
        var capture = (KeyCaptureField)field.Tag!;
        capture.Text.Text = "listening…";
        capture.Text.Foreground = KeyFieldText;
    }

    private void SetKeyCaptureValue(Border field, string name)
    {
        var capture = (KeyCaptureField)field.Tag!;
        capture.Value = name;
        capture.Text.Text = name;
        capture.Text.Foreground = KeyFieldText;
        field.Background = KeyIdleBackground;
        field.BorderBrush = KeyIdleBorder;
        _activeKeyField = null;
    }

    private static void SetKeyCaptureIdle(Border field)
    {
        var capture = (KeyCaptureField)field.Tag!;
        capture.Text.Text = string.IsNullOrWhiteSpace(capture.Value) ? "press a key…" : capture.Value;
        capture.Text.Foreground = KeyFieldText;
        field.Background = KeyIdleBackground;
        field.BorderBrush = KeyIdleBorder;
    }

    // Map a WPF Key to the host-key name rex::ui::ParseVirtualKey expects (see
    // Fable2ConfigFile.KeyNames). Returns null for keys that cannot be bound.
    private static string? KeyToName(Key key)
    {
        if (key >= Key.A && key <= Key.Z) return ((char)('A' + (key - Key.A))).ToString();
        if (key >= Key.D0 && key <= Key.D9) return ((char)('0' + (key - Key.D0))).ToString();
        if (key >= Key.F1 && key <= Key.F24) return "F" + (int)(key - Key.F1 + 1);
        if (key >= Key.NumPad0 && key <= Key.NumPad9) return "Numpad" + (int)(key - Key.NumPad0);
        return key switch
        {
            Key.Add => "NumpadPlus",
            Key.Subtract => "NumpadMinus",
            Key.Multiply => "NumpadStar",
            Key.Divide => "NumpadSlash",
            Key.Left => "Left",
            Key.Right => "Right",
            Key.Up => "Up",
            Key.Down => "Down",
            Key.Home => "Home",
            Key.End => "End",
            Key.PageUp => "PageUp",
            Key.PageDown => "PageDown",
            Key.Delete => "Delete",
            Key.Insert => "Insert",
            Key.Back => "Backspace",
            Key.Return => "Return",
            Key.Space => "Space",
            Key.Tab => "Tab",
            Key.LeftShift => "Shift",
            Key.RightShift => "Shift",
            Key.LeftCtrl => "Control",
            Key.RightCtrl => "Control",
            Key.LeftAlt => "Alt",
            Key.RightAlt => "Alt",
            Key.PrintScreen => "PrintScreen",
            Key.Pause => "Pause",
            Key.CapsLock => "CapsLock",
            Key.NumLock => "NumLock",
            Key.Scroll => "ScrollLock",
            Key.OemTilde => "Backtick",
            Key.OemMinus => "Minus",
            Key.OemPlus => "Plus",
            Key.OemComma => "Comma",
            Key.OemPeriod => "Period",
            Key.OemSemicolon => "Semicolon",
            Key.OemQuestion => "Slash",
            Key.OemBackslash => "Backslash",
            Key.OemOpenBrackets => "LBracket",
            Key.OemCloseBrackets => "RBracket",
            Key.OemQuotes => "Quote",
            _ => null,
        };
    }

    private void MainWindow_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (_activeKeyField == null) return;
        if (e.Key == Key.Escape)
        {
            SetKeyCaptureIdle(_activeKeyField);
            _activeKeyField = null;
            e.Handled = true;
            return;
        }
        Key key = e.Key == Key.System ? e.SystemKey : e.Key;
        string? name = KeyToName(key);
        if (name != null)
        {
            SetKeyCaptureValue(_activeKeyField, name);
            e.Handled = true;
        }
    }

    private static ComboBox BuildKeyCombo(string[] names, string selected)
    {
        var combo = new ComboBox();
        foreach (string name in names)
            combo.Items.Add(new ComboBoxItem { Content = name, Tag = name });
        combo.SelectedItem = FindTag(combo, selected) ?? combo.Items[0] as ComboBoxItem;
        return combo;
    }

    private static ComboBoxItem? FindTag(ComboBox combo, string tag)
    {
        foreach (object item in combo.Items)
            if (item is ComboBoxItem comboItem &&
                string.Equals(comboItem.Tag?.ToString(), tag, StringComparison.OrdinalIgnoreCase))
                return comboItem;
        return null;
    }

    private void BuildKeybindRows(List<KeybindEntry> entries)
    {
        KeybindList.Children.Clear();
        _keyFields.Clear();
        _buttonCombos.Clear();
        _activeKeyField = null;
        foreach (KeybindEntry entry in entries)
            AddKeybindRow(entry.Key, entry.Button);
    }

    private void AddKeybindClicked(object sender, RoutedEventArgs e) =>
        AddKeybindRow("", Fable2ConfigFile.ButtonNames[0]);

    // Restore the built-in default keyboard -> gamepad map (rebuilds the rows
    // from the C++ default). The user still has to press Save to persist it.
    private void ResetKeybindsClicked(object sender, RoutedEventArgs e)
        => BuildKeybindRows(Fable2ConfigFile.ParseKeybinds(Fable2ConfigValues.DefaultKeyboardGamepadMap));

    private string CollectKeybinds()
    {
        var entries = new List<KeybindEntry>();
        for (int i = 0; i < _keyFields.Count; i++)
        {
            string key = ((KeyCaptureField)_keyFields[i].Tag!).Value;
            string button = (_buttonCombos[i].SelectedItem as ComboBoxItem)?.Tag?.ToString() ?? "";
            if (!string.IsNullOrWhiteSpace(key) || !string.IsNullOrWhiteSpace(button))
                entries.Add(new KeybindEntry { Key = key, Button = button });
        }
        return Fable2ConfigFile.BuildKeybinds(entries);
    }

    // Build one "checkbox + label + description" row in the PatchList.
    private void AddPatchRow(string name, string key, string description, bool value,
        bool unstable = false)
    {
        var grid = new Grid();
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

        var labelStack = new StackPanel();
        var nameBlock = new TextBlock
        {
            Text = unstable ? name + "  (unstable)" : name,
            FontWeight = FontWeights.SemiBold,
        };
        if (unstable) nameBlock.Foreground = UnstableBrush;
        labelStack.Children.Add(nameBlock);
        labelStack.Children.Add(new TextBlock
        {
            Text = description,
            FontSize = 12,
            Foreground = MutedBrush,
            TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(0, 2, 0, 0),
        });

        var check = new CheckBox
        {
            IsChecked = value,
            VerticalAlignment = VerticalAlignment.Top,
            Margin = new Thickness(10, 0, 0, 0),
        };

        Grid.SetColumn(labelStack, 0);
        Grid.SetColumn(check, 1);
        grid.Children.Add(labelStack);
        grid.Children.Add(check);

        var border = new Border
        {
            // Reddish tint for unstable patches, subtle green for the rest.
            Background = unstable
                ? new SolidColorBrush(Color.FromArgb(0x26, 0x4A, 0x2C, 0x2C))
                : new SolidColorBrush(Color.FromArgb(0x22, 0x2F, 0x49, 0x41)),
            CornerRadius = new CornerRadius(8),
            Padding = new Thickness(14, 10, 14, 10),
            Margin = new Thickness(0, 0, 0, 10),
            Child = grid,
        };
        PatchList.Children.Add(border);
        _patchChecks.Add((key, check));
    }

    private void BuildPatchRows()
    {
        PatchList.Children.Clear();
        _patchChecks.Clear();
        AddPatchRow("Unlock Website Items", "unlock_website",
            "Force-grant the Fable 2 website items at save load.", _advanced.UnlockWebsite);
        AddPatchRow("Unlock Collectors Edition", "unlock_ce",
            "Force-grant the Collectors Edition chest content at save load.", _advanced.UnlockCe);
        AddPatchRow("Skip Intro Videos", "skip_intro_videos",
            "Skip the Microsoft and Lionhead logo videos at boot.", _advanced.SkipIntroVideos);
        AddPatchRow("Interpolation", "interpolation",
            "Update subtitles and HUD text every frame, and keep cloth in step with the drawn body.",
            _advanced.Interpolation);
        AddPatchRow("Disable Motion Blur", "disable_motion_blur",
            "Zero the camera's full-screen motion blur amount each frame.", _advanced.DisableMotionBlur);
        AddPatchRow("Realtime Texture Morphing", "realtime_texture_morphing",
            "Render the hero/dog morphed skin textures on the GPU (fixes the black hero/dog).",
            _advanced.RealtimeTextureMorphing);
        AddPatchRow("Hero/Dog Texture Readback", "hero_dog_texture_readback",
            "CPU readback of the hero/dog texture resolve. Superseded by realtime morphing.",
            _advanced.HeroDogTextureReadback);
    }

    private void CollectPatches()
    {
        foreach (var (key, check) in _patchChecks)
        {
            bool value = check.IsChecked == true;
            switch (key)
            {
                case "unlock_website": _advanced.UnlockWebsite = value; break;
                case "unlock_ce": _advanced.UnlockCe = value; break;
                case "skip_intro_videos": _advanced.SkipIntroVideos = value; break;
                case "interpolation": _advanced.Interpolation = value; break;
                case "disable_motion_blur": _advanced.DisableMotionBlur = value; break;
                case "realtime_texture_morphing": _advanced.RealtimeTextureMorphing = value; break;
                case "hero_dog_texture_readback": _advanced.HeroDogTextureReadback = value; break;
            }
        }
    }

    private void MouseSensitivitySliderChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (MouseSensitivityBox is not null)
            MouseSensitivityBox.Text = ((int)e.NewValue).ToString(CultureInfo.InvariantCulture);
    }

    private void MouseSensitivityBoxLostFocus(object sender, RoutedEventArgs e)
    {
        if (int.TryParse(MouseSensitivityBox.Text, NumberStyles.Integer, CultureInfo.InvariantCulture,
            out int value))
        {
            value = Math.Clamp(value, 1, 4096);
            MouseSensitivitySlider.Value = value;
            MouseSensitivityBox.Text = value.ToString(CultureInfo.InvariantCulture);
        }
        else
        {
            MouseSensitivityBox.Text = ((int)MouseSensitivitySlider.Value).ToString(CultureInfo.InvariantCulture);
        }
    }

    private void HotFuncYieldSliderChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (HotFuncYieldBox is not null)
            HotFuncYieldBox.Text = ((int)e.NewValue).ToString(CultureInfo.InvariantCulture);
    }

    private void HotFuncYieldBoxLostFocus(object sender, RoutedEventArgs e)
    {
        if (int.TryParse(HotFuncYieldBox.Text, NumberStyles.Integer, CultureInfo.InvariantCulture,
            out int value))
        {
            value = Math.Clamp(value, 0, 4096);
            HotFuncYieldSlider.Value = Math.Clamp(value, 0, 16);
            HotFuncYieldBox.Text = value.ToString(CultureInfo.InvariantCulture);
        }
        else
        {
            HotFuncYieldBox.Text = ((int)HotFuncYieldSlider.Value).ToString(CultureInfo.InvariantCulture);
        }
    }

    private void SaveAdvancedClicked(object sender, RoutedEventArgs e)
    {
        if (_executableDirectory is null)
        {
            MessageBox.Show(this, "Choose the game folder first.", "Game not found",
                MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        _advanced.MouseLook = MouseLookCheck.IsChecked == true;
        _advanced.DirectMouseLook = DirectMouseLookCheck.IsChecked == true;
        _advanced.MouseLookScale = (int)MouseSensitivitySlider.Value;
        _advanced.HotFuncYieldEvery = int.TryParse(HotFuncYieldBox.Text, NumberStyles.Integer,
            CultureInfo.InvariantCulture, out int yieldEvery)
            ? Math.Clamp(yieldEvery, 0, 4096) : (int)HotFuncYieldSlider.Value;
        _advanced.KeyboardGamepadMap = CollectKeybinds();
        CollectPatches();

        string path = AdvancedConfigPath();
        try
        {
            Fable2ConfigFile.Write(path, _advanced);
            AdvancedStatusText.Text = $"Saved {Path.GetFileName(path)}.";
        }
        catch (Exception exception)
        {
            MessageBox.Show(this, exception.Message, "Could not save advanced settings",
                MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }
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
