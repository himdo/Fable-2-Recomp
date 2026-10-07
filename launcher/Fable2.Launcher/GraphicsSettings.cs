using System.Globalization;
using Fable2Launcher.Constants;

namespace Fable2Launcher;

public static class GraphicsSettings
{
    public static readonly string[] ManagedKeys =
    [
        "resolution", "window_width", "window_height", "resolution_scale",
        "anisotropic_override", "swap_post_effect", "vsync", "fullscreen", "fullscreen_exclusive",
        "frame_limit", "guest_vblank_pacing", "gpu_backend"
    ];

    public static Dictionary<string, string> Create(string resolution, string scale,
        string anisotropic, string antiAliasing, string displayMode, bool vsync,
        string frameLimit = "0", string backend = "d3d12")
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
            !new[] { "d3d12", "vulkan" }.Contains(backend) ||
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
            ["fullscreen"] = (displayMode != "windowed").ToString().ToLowerInvariant(),
            ["fullscreen_exclusive"] = (displayMode == "exclusive").ToString().ToLowerInvariant(),
            // Both backends ship in one plugin; the game falls back to the
            // other if this one cannot start.
            ["gpu_backend"] = $"\"{backend}\""
        };
    }
}
