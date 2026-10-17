using System.Globalization;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;

namespace Fable2Launcher;

/// <summary>
/// The data model for fable2_config.toml (the recomp's own user config, separate
/// from the ReXGlue SDK cvar config fable_2.toml). Defaults mirror the C++
/// fable2::config::Values struct in src/core/fable2_config.h.
/// </summary>
public class Fable2ConfigValues
{
    /// <summary>Built-in default keyboard -> guest gamepad map (see the C++ header).</summary>
    public const string DefaultKeyboardGamepadMap =
        "LMB:X,RMB:Y,MMB:B,Shift:A,E:A," +
        "Q:LT,Tab:RB,R:RT,Z:LB,B:B," +
        "W:StickUp,S:StickDown,A:StickLeft,D:StickRight," +
        "Escape:Pause,M:Select," +
        "Up:Up,Down:Down,Left:Left,Right:Right";

    // [input]
    public string KeyboardGamepadMap { get; set; } = DefaultKeyboardGamepadMap;
    public bool MouseLook { get; set; } = true;
    public int MouseLookScale { get; set; } = 256;  // range 1..4096 (cvar constraint)
    // Experimental: drive the camera directly (desktop-FPS orbit) instead of the
    // legacy right-stick mapping. Only applies when MouseLook is on.
    public bool DirectMouseLook { get; set; } = false;  // experimental
    public double MouseLookYawSens { get; set; } = -0.0022;
    public double MouseLookPitchSens { get; set; } = -0.0022;
    public int MouseLookYawAxis { get; set; } = 2;
    public int MouseLookPitchAxis { get; set; } = 0;
    public double MouseLookPitchFloor { get; set; } = 1.458;
    public double MouseLookPitchCeil { get; set; } = 4.067;
    public int CameraRotTgtOff { get; set; } = 160;
    public int CameraRotCurOff { get; set; } = 176;
    public int CameraEyeOff { get; set; } = 144;
    public double CameraDistance { get; set; } = 3.1;
    public bool MouseLookCameraDiag { get; set; } = false;
    public bool MouseLookCameraDump { get; set; } = false;

    // [patches]
    public bool UnlockWebsite { get; set; } = true;
    public bool UnlockCe { get; set; } = true;
    public bool SkipIntroVideos { get; set; } = true;
    public bool HighTickRate { get; set; } = false;       // unstable
    public bool HigherHfTickRate { get; set; } = false;    // unstable
    public bool DisableMotionBlur { get; set; } = false;
    public bool RealtimeTextureMorphing { get; set; } = true;
    public bool HeroDogTextureReadback { get; set; } = false;
    public bool DogFurDepthBias { get; set; } = true;

    // [perf]
    public int HotFuncYieldEvery { get; set; } = 1;  // 1 = original, 0 = never yield
}

/// <summary>One host-key -> guest-input binding (a "Key:Button" pair).</summary>
public class KeybindEntry
{
    public string Key { get; set; } = "";
    public string Button { get; set; } = "";
}

/// <summary>
/// Reads and writes fable2_config.toml non-destructively: it only updates the
/// managed keys (the [input] and [patches] values the launcher exposes),
/// preserving every comment, unknown key and unknown section in the file.
/// </summary>
public static class Fable2ConfigFile
{
    // Host key names understood by rex::ui::ParseVirtualKey (see
    // src/ui/keybinds.cpp) plus the mouse-button aliases from
    // src/input/keyboard_gamepad.h. Used to populate the key dropdowns.
    public static readonly string[] KeyNames =
    {
        "A","B","C","D","E","F","G","H","I","J","K","L","M",
        "N","O","P","Q","R","S","T","U","V","W","X","Y","Z",
        "0","1","2","3","4","5","6","7","8","9",
        "F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12","F13","F14",
        "F15","F16","F17","F18","F19","F20","F21","F22","F23","F24",
        "Backtick","Minus","Plus","Comma","Period","Semicolon","Slash","Backslash",
        "LBracket","RBracket","Quote",
        "Escape","Return","Space","Tab","Backspace","Delete","Insert","Home","End",
        "PageUp","PageDown","Left","Right","Up","Down","Shift","Control","Alt",
        "PrintScreen","Pause","CapsLock","NumLock","ScrollLock",
        "Numpad0","Numpad1","Numpad2","Numpad3","Numpad4","Numpad5","Numpad6",
        "Numpad7","Numpad8","Numpad9","NumpadEnter","NumpadPlus","NumpadMinus",
        "NumpadStar","NumpadSlash",
        "LMB","RMB","MMB","XMB1","XMB2",
    };

    // Guest-input names understood by FillGuestTarget (see
    // src/input/keyboard_gamepad.h). The canonical names are used when writing.
    public static readonly string[] ButtonNames =
    {
        "A","B","X","Y","LB","RB","LT","RT",
        "Up","Down","Left","Right","Pause","Select","L3","R3",
        "StickUp","StickDown","StickLeft","StickRight",
    };

    /// <summary>Read the fable2_config.toml values. Missing file -> built-in defaults.</summary>
    public static Fable2ConfigValues Read(string path)
    {
        var values = new Fable2ConfigValues();
        if (!File.Exists(path)) return values;

        string? section = null;
        foreach (string rawLine in File.ReadLines(path))
        {
            string line = rawLine.TrimStart();
            if (line.StartsWith('#')) continue;
            if (line.StartsWith('['))
            {
                section = line.TrimStart('[').TrimEnd(']').Trim();
                continue;
            }
            int eq = line.IndexOf('=');
            if (eq < 0) continue;
            string key = line.Substring(0, eq).Trim();
            string rawValue = line.Substring(eq + 1).Trim();
            int comment = rawValue.IndexOf('#');
            if (comment >= 0) rawValue = rawValue.Substring(0, comment).Trim();

            switch (section)
            {
                case "input":
                    if (key == "keyboard_gamepad_map") values.KeyboardGamepadMap = Unquote(rawValue);
                    else if (key == "mouse_look") values.MouseLook = ParseBool(rawValue, values.MouseLook);
                    else if (key == "mouse_look_scale") values.MouseLookScale = ParseInt(rawValue, values.MouseLookScale);
                    else if (key == "direct_mouse_look") values.DirectMouseLook = ParseBool(rawValue, values.DirectMouseLook);
                    else if (key == "mouse_look_yaw_sens") values.MouseLookYawSens = ParseDouble(rawValue, values.MouseLookYawSens);
                    else if (key == "mouse_look_pitch_sens") values.MouseLookPitchSens = ParseDouble(rawValue, values.MouseLookPitchSens);
                    else if (key == "mouse_look_yaw_axis") values.MouseLookYawAxis = ParseInt(rawValue, values.MouseLookYawAxis);
                    else if (key == "mouse_look_pitch_axis") values.MouseLookPitchAxis = ParseInt(rawValue, values.MouseLookPitchAxis);
                    else if (key == "mouse_look_pitch_floor") values.MouseLookPitchFloor = ParseDouble(rawValue, values.MouseLookPitchFloor);
                    else if (key == "mouse_look_pitch_ceil") values.MouseLookPitchCeil = ParseDouble(rawValue, values.MouseLookPitchCeil);
                    else if (key == "camera_rot_tgt_off") values.CameraRotTgtOff = ParseInt(rawValue, values.CameraRotTgtOff);
                    else if (key == "camera_rot_cur_off") values.CameraRotCurOff = ParseInt(rawValue, values.CameraRotCurOff);
                    else if (key == "camera_eye_off") values.CameraEyeOff = ParseInt(rawValue, values.CameraEyeOff);
                    else if (key == "camera_distance") values.CameraDistance = ParseDouble(rawValue, values.CameraDistance);
                    else if (key == "mouse_look_camera_diag") values.MouseLookCameraDiag = ParseBool(rawValue, values.MouseLookCameraDiag);
                    else if (key == "mouse_look_camera_dump") values.MouseLookCameraDump = ParseBool(rawValue, values.MouseLookCameraDump);
                    break;
                case "patches":
                    if (key == "unlock_website") values.UnlockWebsite = ParseBool(rawValue, values.UnlockWebsite);
                    else if (key == "unlock_ce") values.UnlockCe = ParseBool(rawValue, values.UnlockCe);
                    else if (key == "skip_intro_videos") values.SkipIntroVideos = ParseBool(rawValue, values.SkipIntroVideos);
                    else if (key == "high_tick_rate") values.HighTickRate = ParseBool(rawValue, values.HighTickRate);
                    else if (key == "higher_hf_tick_rate") values.HigherHfTickRate = ParseBool(rawValue, values.HigherHfTickRate);
                    else if (key == "disable_motion_blur") values.DisableMotionBlur = ParseBool(rawValue, values.DisableMotionBlur);
                    else if (key == "realtime_texture_morphing") values.RealtimeTextureMorphing = ParseBool(rawValue, values.RealtimeTextureMorphing);
                    else if (key == "hero_dog_texture_readback") values.HeroDogTextureReadback = ParseBool(rawValue, values.HeroDogTextureReadback);
                    else if (key == "dog_fur_depth_bias") values.DogFurDepthBias = ParseBool(rawValue, values.DogFurDepthBias);
                    break;
                case "perf":
                    if (key == "hotfunc_yield_every") values.HotFuncYieldEvery = ParseInt(rawValue, values.HotFuncYieldEvery);
                    break;
            }
        }
        return values;
    }

    /// <summary>
    /// Write the managed values to fable2_config.toml non-destructively: each
    /// managed key is updated in place (keeping its line's indentation, spacing
    /// and any inline comment); managed keys missing from the file are appended
    /// to their section (creating the section if it does not exist). Every other
    /// line (comments, unknown keys, unknown sections) is left untouched. A
    /// one-time .launcher-backup copy is made before the first write.
    /// </summary>
    public static void Write(string path, Fable2ConfigValues values)
    {
        string? directory = Path.GetDirectoryName(path);
        if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);

        string backupPath = path + ".launcher-backup";
        if (File.Exists(path) && !File.Exists(backupPath)) File.Copy(path, backupPath);

        List<string> lines = File.Exists(path)
            ? File.ReadAllLines(path).ToList()
            : new List<string> { "# fable2_config.toml - Fable 2 recomp user configuration" };

        var managed = new (string section, string key, string value)[]
        {
            // config_version is the format marker the game requires to load the file.
            ("general", "config_version", "1"),
            ("input", "keyboard_gamepad_map", Quote(values.KeyboardGamepadMap)),
            ("input", "mouse_look", values.MouseLook ? "true" : "false"),
            ("input", "mouse_look_scale", values.MouseLookScale.ToString(CultureInfo.InvariantCulture)),
            ("input", "direct_mouse_look", values.DirectMouseLook ? "true" : "false"),
            ("input", "mouse_look_yaw_sens", values.MouseLookYawSens.ToString(CultureInfo.InvariantCulture)),
            ("input", "mouse_look_pitch_sens", values.MouseLookPitchSens.ToString(CultureInfo.InvariantCulture)),
            ("input", "mouse_look_yaw_axis", values.MouseLookYawAxis.ToString(CultureInfo.InvariantCulture)),
            ("input", "mouse_look_pitch_axis", values.MouseLookPitchAxis.ToString(CultureInfo.InvariantCulture)),
            ("input", "mouse_look_pitch_floor", values.MouseLookPitchFloor.ToString(CultureInfo.InvariantCulture)),
            ("input", "mouse_look_pitch_ceil", values.MouseLookPitchCeil.ToString(CultureInfo.InvariantCulture)),
            ("input", "camera_rot_tgt_off", values.CameraRotTgtOff.ToString(CultureInfo.InvariantCulture)),
            ("input", "camera_rot_cur_off", values.CameraRotCurOff.ToString(CultureInfo.InvariantCulture)),
            ("input", "camera_eye_off", values.CameraEyeOff.ToString(CultureInfo.InvariantCulture)),
            ("input", "camera_distance", values.CameraDistance.ToString(CultureInfo.InvariantCulture)),
            ("input", "mouse_look_camera_diag", values.MouseLookCameraDiag ? "true" : "false"),
            ("input", "mouse_look_camera_dump", values.MouseLookCameraDump ? "true" : "false"),
            ("patches", "unlock_website", values.UnlockWebsite ? "true" : "false"),
            ("patches", "unlock_ce", values.UnlockCe ? "true" : "false"),
            ("patches", "skip_intro_videos", values.SkipIntroVideos ? "true" : "false"),
            ("patches", "high_tick_rate", values.HighTickRate ? "true" : "false"),
            ("patches", "higher_hf_tick_rate", values.HigherHfTickRate ? "true" : "false"),
            ("patches", "disable_motion_blur", values.DisableMotionBlur ? "true" : "false"),
            ("patches", "realtime_texture_morphing", values.RealtimeTextureMorphing ? "true" : "false"),
            ("patches", "hero_dog_texture_readback", values.HeroDogTextureReadback ? "true" : "false"),
            ("patches", "dog_fur_depth_bias", values.DogFurDepthBias ? "true" : "false"),
            ("perf", "hotfunc_yield_every", values.HotFuncYieldEvery.ToString(CultureInfo.InvariantCulture)),
        };

        var updated = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var (section, key, value) in managed)
            if (UpdateKeyInSection(lines, section, key, value)) updated.Add(key);
        foreach (var (section, key, value) in managed)
            if (!updated.Contains(key)) AddKeyToSection(lines, section, key, value);

        string tempPath = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.WriteAllText(tempPath, string.Join(Environment.NewLine, lines) + Environment.NewLine,
                new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
            File.Move(tempPath, path, overwrite: true);
        }
        finally
        {
            if (File.Exists(tempPath)) File.Delete(tempPath);
        }
    }

    /// <summary>Parse "Key:Button,Key:Button,..." into a list of bindings.</summary>
    public static List<KeybindEntry> ParseKeybinds(string map)
    {
        var entries = new List<KeybindEntry>();
        if (string.IsNullOrWhiteSpace(map)) return entries;
        foreach (string part in map.Split(','))
        {
            string trimmed = part.Trim();
            if (trimmed.Length == 0) continue;
            int colon = trimmed.IndexOf(':');
            if (colon < 0) continue;
            entries.Add(new KeybindEntry
            {
                Key = trimmed.Substring(0, colon).Trim(),
                Button = trimmed.Substring(colon + 1).Trim(),
            });
        }
        return entries;
    }

    /// <summary>Build "Key:Button,Key:Button,..." from a list of bindings.</summary>
    public static string BuildKeybinds(IEnumerable<KeybindEntry> entries)
    {
        var parts = new List<string>();
        foreach (KeybindEntry entry in entries)
        {
            if (string.IsNullOrWhiteSpace(entry.Key) && string.IsNullOrWhiteSpace(entry.Button)) continue;
            parts.Add($"{entry.Key.Trim()}:{entry.Button.Trim()}");
        }
        return string.Join(",", parts);
    }

    // ---- non-destructive write helpers ----

    private static (int start, int end) FindSectionRange(List<string> lines, string section)
    {
        int start = -1, end = lines.Count;
        for (int i = 0; i < lines.Count; i++)
        {
            string trimmed = lines[i].TrimStart();
            if (!trimmed.StartsWith('[') || trimmed.StartsWith("[#")) continue;
            string name = trimmed.TrimStart('[').TrimEnd(']').Trim();
            if (start < 0)
            {
                if (string.Equals(name, section, StringComparison.OrdinalIgnoreCase)) start = i;
            }
            else
            {
                end = i;
                break;
            }
        }
        return (start, end);
    }

    private static bool UpdateKeyInSection(List<string> lines, string section, string key, string value)
    {
        var (start, end) = FindSectionRange(lines, section);
        if (start < 0) return false;
        var pattern = new Regex(
            @"^(\s*)" + Regex.Escape(key) + @"(\s*=\s*)(.*?)(\s+#.*)?$",
            RegexOptions.Compiled);
        for (int i = start + 1; i < end; i++)
        {
            Match match = pattern.Match(lines[i]);
            if (match.Success)
            {
                // Keep indentation, spacing and any existing inline comment.
                lines[i] = match.Groups[1].Value + key + match.Groups[2].Value + value + match.Groups[4].Value;
                return true;
            }
        }
        return false;
    }

    private static void AddKeyToSection(List<string> lines, string section, string key, string value)
    {
        var (start, end) = FindSectionRange(lines, section);
        if (start < 0)
        {
            // Section does not exist: create it at the end of the file.
            if (lines.Count > 0 && !string.IsNullOrWhiteSpace(lines[^1])) lines.Add(string.Empty);
            lines.Add($"[{section}]");
            lines.Add($"{key} = {value}");
            return;
        }
        // Append at the end of the section, after the last non-blank line.
        int insertAt = end;
        while (insertAt > start + 1 && string.IsNullOrWhiteSpace(lines[insertAt - 1]))
            insertAt--;
        lines.Insert(insertAt, $"{key} = {value}");
    }

    // ---- value (de)serialization helpers ----

    private static bool ParseBool(string value, bool fallback)
    {
        value = value.Trim();
        return value.Equals("true", StringComparison.OrdinalIgnoreCase) ? true
             : value.Equals("false", StringComparison.OrdinalIgnoreCase) ? false
             : fallback;
    }

    private static int ParseInt(string value, int fallback)
    {
        return int.TryParse(value.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture,
            out int parsed) ? parsed : fallback;
    }

    private static double ParseDouble(string value, double fallback)
    {
        return double.TryParse(value.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture,
            out double parsed) ? parsed : fallback;
    }

    private static string Quote(string value)
    {
        return "\"" + value.Replace("\\", "\\\\").Replace("\"", "\\\"") + "\"";
    }

    private static string Unquote(string value)
    {
        value = value.Trim();
        if (value.Length >= 2 && value[0] == '"' && value[^1] == '"')
            return value.Substring(1, value.Length - 2).Replace("\\\"", "\"").Replace("\\\\", "\\");
        if (value.Length >= 2 && value[0] == '\'' && value[^1] == '\'')
            return value.Substring(1, value.Length - 2);  // TOML literal string (no escapes)
        return value;
    }
}
