using System.Globalization;
using Fable2Launcher.Constants;

namespace Fable2Launcher;

public static class GraphicsSettings
{
    public static readonly string[] ManagedKeys =
    [
        "resolution", "window_width", "window_height", "resolution_scale",
        "anisotropic_override", "swap_post_effect", "vsync", "fullscreen", "fullscreen_exclusive",
        "frame_limit", "guest_vblank_pacing", "no_vsync_vblank_hz"
    ];

    public static Dictionary<string, string> Create(string resolution, string scale,
        string anisotropic, string antiAliasing, string displayMode, bool vsync,
        string frameLimit = "0")
    {
        (int width, int height) = resolution switch
        {
            "720p" => (1280, 720), "1080p" => (1920, 1080),
            "1440p" => (2560, 1440), "4k" => (3840, 2160),
            _ => throw new ArgumentException("Unknown output resolution")
        };
        if (!new[] { "1", "2", "3", "4" }.Contains(scale) ||
            !new[] { "-1", "1", "2", "3", "4", "5" }.Contains(anisotropic) ||
            !new[] { "none", "fxaa", "fxaa_extreme" }.Contains(antiAliasing) ||
            !new[] { "windowed", "borderless", "exclusive" }.Contains(displayMode) ||
            !GraphicsOptions.FrameLimits.Any(option => option.Value == frameLimit))
            throw new ArgumentException("Unsupported graphics setting");

        return new(StringComparer.OrdinalIgnoreCase)
        {
            // Presentation size is independent from the guest's original 720p mode.
            ["resolution"] = "\"\"",
            ["window_width"] = width.ToString(CultureInfo.InvariantCulture),
            ["window_height"] = height.ToString(CultureInfo.InvariantCulture),
            ["resolution_scale"] = scale,
            ["anisotropic_override"] = anisotropic,
            ["swap_post_effect"] = $"\"{antiAliasing}\"",
            ["vsync"] = vsync.ToString().ToLowerInvariant(),
            ["frame_limit"] = frameLimit,
            // Fable waits for two guest vblanks per frame. Legacy 60-Hz
            // guest pacing therefore caps it at 30 even with a 60-FPS limiter.
            // This does not disable the user's host presentation VSync.
            ["guest_vblank_pacing"] = (frameLimit == "30").ToString().ToLowerInvariant(),
            // While guest pacing is off the vblank worker ticks at
            // no_vsync_vblank_hz, so the guest's per-frame floor is 2/hz
            // seconds. The SDK default (120 Hz = 60 fps floor, 2x native)
            // keeps idle screens like the main menu from pinning the GPU;
            // "unlimited" restores the original fully-uncapped 1000 Hz, and a
            // numeric cap gets 2x its target plus headroom so the host swap
            // limiter - not the guest vblank gate - is what caps the rate.
            // (30 uses the 60-Hz pacing path, where this cvar is unused.)
            ["no_vsync_vblank_hz"] = int.TryParse(frameLimit, out int fps) && fps > 0 && fps != 30
                ? (2 * fps + 20).ToString(CultureInfo.InvariantCulture)
                : (frameLimit == "unlimited" ? "1000" : "120"),
            ["fullscreen"] = (displayMode != "windowed").ToString().ToLowerInvariant(),
            ["fullscreen_exclusive"] = (displayMode == "exclusive").ToString().ToLowerInvariant()
        };
    }
}
