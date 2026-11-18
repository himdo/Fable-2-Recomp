using System.IO;
using System.Net.Http;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace Fable2Launcher;

// The release zip, when the release has exactly one and GitHub published its SHA-256.
public sealed record ReleasePackage(string DownloadUrl, long Size, string Sha256);

// Package is null when the release can only be installed by hand from its page.
public sealed record AvailableUpdate(string Version, string PageUrl, ReleasePackage? Package = null);

public static class UpdateChecker
{
    private const string LatestReleaseApi = "https://api.github.com/repos/himdo/Fable-2-Recomp/releases/latest";
    private const string ReleasePagePrefix = "https://github.com/himdo/Fable-2-Recomp/releases/";
    private const string DownloadPrefix = "https://github.com/himdo/Fable-2-Recomp/releases/download/";
    // Releases are about 100 MB. Anything far beyond that is not one of ours.
    public const long MaxPackageBytes = 1L << 30;
    // TODO: pick the zip for the running OS once the launcher runs on more than Windows.
    private static readonly Regex PackageName = new(@"^Fable_2_recomp_v[0-9A-Za-z._-]+_windows\.zip$");
    private static readonly Regex Sha256Digest = new(@"^sha256:([0-9a-fA-F]{64})$");
    private static readonly HttpClient Downloads = CreateClient(Timeout.InfiniteTimeSpan);

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
            ? new AvailableUpdate(tag, page, FindPackage(document.RootElement)) : null;
    }

    // The in-place upgrade only ever downloads from this repo's own release downloads, and
    // only a file whose size and SHA-256 GitHub states, so a cut-off or swapped download is
    // caught. Anything unexpected means "no package": the update button then opens the page.
    private static ReleasePackage? FindPackage(JsonElement release)
    {
        if (!release.TryGetProperty("assets", out JsonElement assets) || assets.ValueKind != JsonValueKind.Array)
            return null;
        JsonElement[] zips = assets.EnumerateArray()
            .Where(asset => asset.ValueKind == JsonValueKind.Object && Text(asset, "name") is string name &&
                            PackageName.IsMatch(name))
            .ToArray();
        if (zips.Length != 1) return null;

        string? url = Text(zips[0], "browser_download_url");
        Match digest = Sha256Digest.Match(Text(zips[0], "digest") ?? "");
        if (url is null || !url.StartsWith(DownloadPrefix, StringComparison.Ordinal) || !digest.Success) return null;
        if (!zips[0].TryGetProperty("size", out JsonElement size) || !size.TryGetInt64(out long bytes) ||
            bytes <= 0 || bytes > MaxPackageBytes) return null;
        return new ReleasePackage(url, bytes, digest.Groups[1].Value.ToLowerInvariant());
    }

    private static string? Text(JsonElement element, string property) =>
        element.TryGetProperty(property, out JsonElement value) && value.ValueKind == JsonValueKind.String
            ? value.GetString() : null;

    // A failed check never gets in the way of playing, so every failure means "no update".
    public static async Task<AvailableUpdate?> CheckAsync(string installedVersion)
    {
#if DEBUG
        if (await Task.Run(() => TestUpdate(installedVersion)) is AvailableUpdate test) return test;
#endif
        try
        {
            using HttpClient client = CreateClient(TimeSpan.FromSeconds(5));
            return FindUpdate(installedVersion, await client.GetStringAsync(LatestReleaseApi));
        }
        catch (Exception exception) when (exception is HttpRequestException or TaskCanceledException
                                              or JsonException or KeyNotFoundException or InvalidOperationException)
        {
            return null;
        }
    }

    // Opens the release zip for the in-place upgrade. GitHub redirects the download to its
    // file servers; the client follows HTTPS redirects only, and the upgrade checks the
    // bytes against the SHA-256 from the release, whichever server sent them.
    public static async Task<Stream> OpenDownloadAsync(string url, CancellationToken cancel)
    {
#if DEBUG
        if (url == Environment.GetEnvironmentVariable(TestZipVariable)) return File.OpenRead(url);
#endif
        if (!url.StartsWith(DownloadPrefix, StringComparison.Ordinal))
            throw new HttpRequestException("The download link does not belong to this project's releases.");
        using var connect = CancellationTokenSource.CreateLinkedTokenSource(cancel);
        connect.CancelAfter(TimeSpan.FromSeconds(30));
        try
        {
            HttpResponseMessage response = await Downloads.GetAsync(url, HttpCompletionOption.ResponseHeadersRead,
                connect.Token);
            response.EnsureSuccessStatusCode();
            return await response.Content.ReadAsStreamAsync(cancel);
        }
        catch (OperationCanceledException) when (!cancel.IsCancellationRequested)
        {
            throw new HttpRequestException("GitHub did not answer in time.");
        }
    }

#if DEBUG
    // Debug builds only, never in a release: offer a local zip as the update, to try the
    // in-place upgrade without publishing anything. The version comes from the file name.
    //   set FABLE2_TEST_UPDATE_ZIP=C:\...\Fable_2_recomp_v9.0.0_windows.zip
    // tools/make_upgrade_test_zip.py makes such a zip; tests/README.md has the steps.
    private const string TestZipVariable = "FABLE2_TEST_UPDATE_ZIP";

    private static AvailableUpdate? TestUpdate(string installedVersion)
    {
        string? zip = Environment.GetEnvironmentVariable(TestZipVariable);
        if (zip is null || !File.Exists(zip)) return null;
        Match name = Regex.Match(Path.GetFileName(zip), @"^Fable_2_recomp_v(.+)_windows\.zip$");
        if (!name.Success || ParseVersion(name.Groups[1].Value) is not Version version ||
            ParseVersion(installedVersion) is not Version installed || version <= installed) return null;
        using FileStream stream = File.OpenRead(zip);
        string sha = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(stream)).ToLowerInvariant();
        return new AvailableUpdate(name.Groups[1].Value, ReleasePagePrefix + "latest",
            new ReleasePackage(zip, stream.Length, sha));
    }
#endif

    private static HttpClient CreateClient(TimeSpan timeout)
    {
        var client = new HttpClient { Timeout = timeout };
        client.DefaultRequestHeaders.UserAgent.ParseAdd("Fable2Launcher");
        return client;
    }

    // "1.5.0-16-g55e38c4" (git describe) counts as 1.5.0.
    public static Version? ParseVersion(string text)
    {
        Match match = Regex.Match(text, @"^v?(\d+)\.(\d+)(?:\.(\d+))?");
        if (!match.Success) return null;
        string patch = match.Groups[3].Success ? match.Groups[3].Value : "0";
        return Version.TryParse($"{match.Groups[1].Value}.{match.Groups[2].Value}.{patch}", out Version? version)
            ? version : null;
    }
}
