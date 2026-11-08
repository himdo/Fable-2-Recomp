using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace Fable2Launcher;

internal static class KeyboardPrompts
{
    private const string Textures = "data/art/gui/gui_textures.bnk";
    private const string Headers = "data/art/gui/gui_texture_headers.bnk";
    private const string ReferenceIcon = @"Art\GUI\Controller\icon_button_a.tex";

    private static readonly Dictionary<string, string> ButtonAliases = new(StringComparer.OrdinalIgnoreCase)
    {
        ["LShoulder"] = "LB", ["RShoulder"] = "RB", ["LTrigger"] = "LT", ["RTrigger"] = "RT",
        ["DUp"] = "Up", ["DDown"] = "Down", ["DLeft"] = "Left", ["DRight"] = "Right",
        ["Start"] = "Pause", ["Back"] = "Select", ["View"] = "Select",
        ["LThumb"] = "L3", ["RThumb"] = "R3",
        ["LU"] = "StickUp", ["LD"] = "StickDown", ["LL"] = "StickLeft", ["LR"] = "StickRight"
    };

    private static readonly Dictionary<string, string> MouseAliases = new(StringComparer.OrdinalIgnoreCase)
    {
        ["LMB"] = "LMB", ["LeftMouse"] = "LMB", ["RMB"] = "RMB", ["RightMouse"] = "RMB",
        ["MMB"] = "MMB", ["MiddleMouse"] = "MMB",
        ["XMB1"] = "XMB1", ["XButton1"] = "XMB1", ["SideButton1"] = "XMB1",
        ["XMB2"] = "XMB2", ["XButton2"] = "XMB2", ["SideButton2"] = "XMB2"
    };

    private static readonly Dictionary<string, string> ShortKeys = new(StringComparer.Ordinal)
    {
        ["Escape"] = "Esc", ["Return"] = "Enter", ["Backspace"] = "Bksp", ["Control"] = "Ctrl",
        ["PageUp"] = "PgUp", ["PageDown"] = "PgDn", ["PrintScreen"] = "PrtSc",
        ["CapsLock"] = "Caps", ["NumLock"] = "NumLk", ["ScrollLock"] = "ScrLk",
        ["Up"] = "↑", ["Down"] = "↓", ["Left"] = "←", ["Right"] = "→"
    };

    internal static Dictionary<string, string> ResolveLabels(string map, string reservedKey)
    {
        var labels = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var priorities = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        foreach (KeybindEntry binding in Fable2ConfigFile.ParseKeybinds(map))
        {
            bool isMouse = MouseAliases.TryGetValue(binding.Key, out string? key);
            key ??= binding.Key;
            if (!Fable2ConfigFile.KeyNames.Contains(key, StringComparer.Ordinal) || key == reservedKey)
                continue;
            string target = ButtonAliases.GetValueOrDefault(binding.Button, binding.Button);
            if (!Fable2ConfigFile.ButtonNames.Contains(target, StringComparer.OrdinalIgnoreCase))
                continue;

            // Prefer a regular key over an alternative modifier or mouse button
            // for the same action (for example E over Shift and B over MMB).
            int priority = isMouse || key is "Shift" or "Control" or "Alt" ? 1 : 2;
            if (priority <= priorities.GetValueOrDefault(target)) continue;
            priorities[target] = priority;
            labels[target] = ShortKeys.GetValueOrDefault(key, key);
        }
        return labels;
    }

    private static string? IconLabel(string name, Dictionary<string, string> labels, bool mouseLook)
    {
        const string prefix = @"Art\GUI\Controller\";
        if (!name.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) ||
            !name.EndsWith(".tex", StringComparison.OrdinalIgnoreCase)) return null;
        string leaf = name[(name.LastIndexOf('\\') + 1)..^4].ToLowerInvariant();
        if (leaf.StartsWith("anim", StringComparison.Ordinal)) leaf = leaf[4..];
        return leaf switch
        {
            "icon_button_a" => labels.GetValueOrDefault("A"),
            "icon_button_b" => labels.GetValueOrDefault("B"),
            "icon_button_x" => labels.GetValueOrDefault("X"),
            "icon_button_y" => labels.GetValueOrDefault("Y"),
            "leftbutton" => labels.GetValueOrDefault("LB"),
            "rightbutton" => labels.GetValueOrDefault("RB"),
            "lefttrigger" or "icon_button_lt" => labels.GetValueOrDefault("LT"),
            "righttrigger" => labels.GetValueOrDefault("RT"),
            "start" => labels.GetValueOrDefault("Pause"),
            "back" => labels.GetValueOrDefault("Select"),
            "leftup" => labels.GetValueOrDefault("StickUp"),
            "leftdown" => labels.GetValueOrDefault("StickDown"),
            "leftleft" => labels.GetValueOrDefault("StickLeft"),
            "leftright" => labels.GetValueOrDefault("StickRight"),
            "left" => Directions(labels, "Stick"),
            "dpad" => Directions(labels, ""),
            "right" or "rightup" or "rightdown" or "rightleft" or "rightright" => mouseLook ? "Mouse" : null,
            _ => null
        };
    }

    private static string? Directions(Dictionary<string, string> labels, string prefix)
    {
        string?[] keys = ["Up", "Left", "Down", "Right"];
        for (int i = 0; i < keys.Length; i++) keys[i] = labels.GetValueOrDefault(prefix + keys[i]);
        return keys.Any(key => key is null) ? null : $"{keys[0]} {keys[1]}\n{keys[2]} {keys[3]}";
    }

    internal static string Prepare(string gameRoot, string map, bool mouseLook, string reservedKey)
    {
        Dictionary<string, string> labels = ResolveLabels(map, reservedKey);
        if (labels.Count == 0 && !mouseLook) return gameRoot;

        string data = Path.Combine(gameRoot, "data");
        var files = EnumerateData(data).Append(Path.Combine(gameRoot, "default.xex"))
            .Concat(new[] { "build_version.txt", "nxeart" }.Select(file => Path.Combine(gameRoot, file)).Where(File.Exists))
            .Select(file => new FileInfo(file)).OrderBy(file => file.FullName, StringComparer.Ordinal).ToArray();

        byte[] textures = File.ReadAllBytes(Path.Combine(gameRoot, Textures));
        byte[] headers = File.ReadAllBytes(Path.Combine(gameRoot, Headers));
        // Cache directories are immutable once published, so a second launch
        // with another map cannot replace a bank while the first game is using it.
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        hash.AppendData(textures);
        hash.AppendData(headers);
        hash.AppendData(Encoding.UTF8.GetBytes($"keyboard-prompts-v1\n{map}\n{mouseLook}\n{reservedKey}\n"));
        foreach (FileInfo file in files)
            hash.AppendData(Encoding.UTF8.GetBytes($"{file.FullName}\n{file.Length}\n{file.LastWriteTimeUtc.Ticks}\n"));
        string cache = Path.Combine(gameRoot, "cache", "keyboard-prompts");
        string target = Path.Combine(cache, Convert.ToHexString(hash.GetHashAndReset())[..24]);
        if (Directory.Exists(target)) return target;

        string staging = Path.Combine(cache, Guid.NewGuid().ToString("N") + ".tmp");
        Directory.CreateDirectory(staging);
        try
        {
            var textureBank = new GuiTextureBank(textures);
            var headerBank = new GuiTextureBank(headers);
            if (!headerBank.Entries.TryGetValue(ReferenceIcon, out var reference) || reference.Length != 84 ||
                GuiTextureBank.ReadInt(headerBank.Payload(reference), 12) != 0xA2)
                throw new InvalidDataException("This disc does not use the expected controller texture format.");
            byte[] rawHeader = headerBank.Payload(reference).ToArray();
            var rendered = new Dictionary<(string Label, int Size), byte[]>();
            foreach ((string name, GuiTextureBank.Entry entry) in textureBank.Entries)
            {
                string? label = IconLabel(name, labels, mouseLook);
                if (label is null || !headerBank.Entries.TryGetValue(name, out var header) ||
                    header.Length != rawHeader.Length) continue;

                ReadOnlySpan<byte> metadata = headerBank.Payload(header);
                int size = GuiTextureBank.ReadInt(metadata, 16);
                if (size is not (32 or 64) || GuiTextureBank.ReadInt(metadata, 20) != size ||
                    GuiTextureBank.ReadInt(metadata, 4) != entry.Length) continue;
                if (!rendered.TryGetValue((label, size), out byte[]? image))
                    rendered[(label, size)] = image = RenderKeycap(label, size);
                textureBank.Replace(entry, image);
                byte[] replacementHeader = (byte[])rawHeader.Clone();
                GuiTextureBank.WriteInt(replacementHeader, 4, image.Length);
                GuiTextureBank.WriteInt(replacementHeader, 16, size);
                GuiTextureBank.WriteInt(replacementHeader, 20, size);
                headerBank.Replace(header, replacementHeader);
            }
            if (rendered.Count == 0) return gameRoot;

            foreach (FileInfo file in files)
            {
                string relative = Path.GetRelativePath(gameRoot, file.FullName);
                string portable = relative.Replace('\\', '/');
                if (portable.Equals(Textures, StringComparison.OrdinalIgnoreCase) ||
                    portable.Equals(Headers, StringComparison.OrdinalIgnoreCase)) continue;
                string output = Path.Combine(staging, relative);
                Directory.CreateDirectory(Path.GetDirectoryName(output)!);
                // The recomp may heal the manifest on startup. Give it a copy
                // so the healing cannot write through a hard link to the disc.
                if (portable.Equals("data/dir.manifest", StringComparison.OrdinalIgnoreCase))
                    File.Copy(file.FullName, output);
                else if (!CreateHardLink(output, file.FullName, IntPtr.Zero))
                    throw new IOException("Keyboard prompts need a writable game folder on an NTFS volume. " +
                        "Turn off keyboard prompts to launch from the original folder.");
            }
            Directory.CreateDirectory(Path.GetDirectoryName(Path.Combine(staging, Textures))!);
            textureBank.Write(Path.Combine(staging, Textures));
            headerBank.Write(Path.Combine(staging, Headers));

            try { Directory.Move(staging, target); }
            catch (IOException) when (Directory.Exists(target)) { } // another launcher published it
            return target;
        }
        finally
        {
            if (Directory.Exists(staging)) Directory.Delete(staging, recursive: true);
        }
    }

    private static IEnumerable<string> EnumerateData(string path)
    {
        if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
            throw new IOException("Keyboard prompts do not support redirected data folders.");
        foreach (string file in Directory.EnumerateFiles(path))
        {
            if ((File.GetAttributes(file) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("Keyboard prompts do not support redirected game files.");
            yield return file;
        }
        foreach (string child in Directory.EnumerateDirectories(path))
            foreach (string file in EnumerateData(child))
                yield return file;
    }

    private static byte[] RenderKeycap(string label, int size)
    {
        var drawing = new DrawingVisual();
        using (DrawingContext dc = drawing.RenderOpen())
        {
            dc.PushTransform(new ScaleTransform(size / 64.0, size / 64.0));
            var fill = new SolidColorBrush(Color.FromRgb(20, 25, 35));
            var edge = new Pen(new SolidColorBrush(Color.FromRgb(220, 231, 242)), 2);
            dc.DrawRoundedRectangle(fill, edge, new Rect(2, 8, 60, 48), 8, 8);
            string[] rows = label.Split('\n');
            var type = new Typeface(new FontFamily("Segoe UI"), FontStyles.Normal, FontWeights.Bold, FontStretches.Normal);
            double em = rows.Length == 1 ? 32 : 20;
            FormattedText[] text;
            do
            {
                text = rows.Select(row => new FormattedText(row, CultureInfo.InvariantCulture,
                    FlowDirection.LeftToRight, type, em, Brushes.White, 1.0)).ToArray();
                em--;
            } while (em >= 6 && text.Any(row => row.Width > 53));
            double y = (64 - text.Sum(row => row.Height)) / 2;
            foreach (FormattedText row in text)
            {
                dc.DrawText(row, new Point((64 - row.Width) / 2, y));
                y += row.Height;
            }
        }

        var bitmap = new RenderTargetBitmap(size, size, 96, 96, PixelFormats.Pbgra32);
        bitmap.Render(drawing);
        byte[] pixels = new byte[size * size * 4];
        bitmap.CopyPixels(pixels, size * 4, 0);
        byte[] result = new byte[4 + pixels.Length];
        GuiTextureBank.WriteInt(result, 0, pixels.Length);
        for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
        {
            int source = (y * size + x) * 4;
            int target = 4 + XenosTiling.TiledOffset(x, y, size);
            int alpha = pixels[source + 3];
            if (alpha == 0) continue;
            result[target] = (byte)alpha;
            for (int channel = 1; channel <= 3; channel++)
                result[target + channel] = (byte)((pixels[source + 3 - channel] * 255 + alpha / 2) / alpha);
        }
        return result;
    }

    [DllImport("kernel32.dll", EntryPoint = "CreateHardLinkW", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CreateHardLink(string newPath, string existingPath, IntPtr securityAttributes);
}
