namespace Fable2Launcher.Constants;

public sealed record FrameLimitOption(string Value, string Label);
public static class GraphicsOptions
{
    // Host caps, not claims that every scene is timing-safe above 60 FPS.
    public static readonly FrameLimitOption[] FrameLimits =
    [
        new("30", "30 FPS cap"), new("60", "60 FPS cap"),
        new("120", "120 FPS cap — experimental"), new("144", "144 FPS cap — experimental"),
        new("165", "165 FPS cap — experimental"), new("240", "240 FPS cap — experimental"),
        new("0", "Unlimited — experimental")
    ];

    // Display-side caps; the game keeps its own rate, so none are experimental.
    public static readonly FrameLimitOption[] HostFrameLimits =
    [
        new("30", "30 FPS cap"), new("60", "60 FPS cap"), new("120", "120 FPS cap"),
        new("144", "144 FPS cap"), new("165", "165 FPS cap"), new("240", "240 FPS cap"),
        new("0", "Unlimited")
    ];
}
