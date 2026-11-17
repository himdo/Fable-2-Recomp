using System.IO;
using System.Net.Http;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace Fable2Launcher;

public sealed record AvailableUpdate(string Version, string PageUrl);

public static class UpdateChecker
{
    private const string LatestReleaseApi = "https://api.github.com/repos/himdo/Fable-2-Recomp/releases/latest";
    private const string ReleasePagePrefix = "https://github.com/himdo/Fable-2-Recomp/releases/";

    // Only packaged releases are stamped with a version, so local builds never check.
    public static string? ReadInstalledVersion(string folder)
    {
        try
        {
            using JsonDocument document = JsonDocument.Parse(File.ReadAllText(Path.Combine(folder, "fable2_build.json")));
            return document.RootElement.TryGetProperty("version", out JsonElement version) &&
                   version.ValueKind == JsonValueKind.String ? version.GetString() : null;
        }
        catch (Exception exception) when (exception is IOException or JsonException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    public static AvailableUpdate? FindUpdate(string installedVersion, string latestReleaseJson)
    {
        using JsonDocument document = JsonDocument.Parse(latestReleaseJson);
        string? tag = document.RootElement.GetProperty("tag_name").GetString();
        string? page = document.RootElement.GetProperty("html_url").GetString();
        if (tag is null || page is null || !page.StartsWith(ReleasePagePrefix, StringComparison.Ordinal)) return null;

        Version? installed = ParseVersion(installedVersion);
        Version? latest = ParseVersion(tag);
        return installed is not null && latest is not null && latest > installed
            ? new AvailableUpdate(tag, page) : null;
    }

    // A failed check never gets in the way of playing, so every failure means "no update".
    public static async Task<AvailableUpdate?> CheckAsync(string installedVersion)
    {
        try
        {
            using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(5) };
            client.DefaultRequestHeaders.UserAgent.ParseAdd("Fable2Launcher");
            return FindUpdate(installedVersion, await client.GetStringAsync(LatestReleaseApi));
        }
        catch (Exception exception) when (exception is HttpRequestException or TaskCanceledException
                                              or JsonException or KeyNotFoundException or InvalidOperationException)
        {
            return null;
        }
    }

    // "1.5.0-16-g55e38c4" (git describe) counts as 1.5.0.
    private static Version? ParseVersion(string text)
    {
        Match match = Regex.Match(text, @"^v?(\d+)\.(\d+)(?:\.(\d+))?");
        if (!match.Success) return null;
        string patch = match.Groups[3].Success ? match.Groups[3].Value : "0";
        return Version.TryParse($"{match.Groups[1].Value}.{match.Groups[2].Value}.{patch}", out Version? version)
            ? version : null;
    }
}
