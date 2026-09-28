using System.IO;
using System.IO.Compression;
using System.Net.Http;
using System.Text.Json;

namespace RandoPack;

public sealed record ToolsRelease(string Tag, string ZipName, string ZipUrl, DateTimeOffset Published)
{
    public string Label => Published == DateTimeOffset.MinValue ? Tag : $"{Tag} ({Published.LocalDateTime:yyyy-MM-dd})";
}

public static class ReleaseFetcher
{
    public const string Repo = "Ori-Jakob/wwhd-tools";

    private static readonly HttpClient Http = CreateClient();

    private static HttpClient CreateClient()
    {
        var http = new HttpClient { Timeout = TimeSpan.FromSeconds(60) };
        http.DefaultRequestHeaders.UserAgent.ParseAdd("WwhdRandoPack");
        http.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");
        return http;
    }

    public static string CacheDir => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "WwhdRandoPack", "releases");

    public static async Task<ToolsRelease> GetLatestAsync(CancellationToken ct)
    {
        using var response = await Http.GetAsync($"https://api.github.com/repos/{Repo}/releases/latest", ct);
        if (response.StatusCode == System.Net.HttpStatusCode.NotFound)
            throw new InvalidDataException("no release published yet");
        response.EnsureSuccessStatusCode();
        await using var stream = await response.Content.ReadAsStreamAsync(ct);
        using var doc = await JsonDocument.ParseAsync(stream, cancellationToken: ct);
        var root = doc.RootElement;
        var tag = root.GetProperty("tag_name").GetString() ?? throw new InvalidDataException("the release has no tag");
        var published = root.TryGetProperty("published_at", out var p) && p.ValueKind == JsonValueKind.String
            ? p.GetDateTimeOffset() : DateTimeOffset.MinValue;
        foreach (var asset in root.GetProperty("assets").EnumerateArray())
        {
            var name = asset.GetProperty("name").GetString() ?? "";
            if (name.StartsWith("WWHD-Tools-", StringComparison.OrdinalIgnoreCase) &&
                name.EndsWith(".zip", StringComparison.OrdinalIgnoreCase) &&
                !name.Contains("RandoPack", StringComparison.OrdinalIgnoreCase))
                return new ToolsRelease(tag, name, asset.GetProperty("browser_download_url").GetString()!, published);
        }
        throw new InvalidDataException($"release {tag} has no WWHD-Tools zip");
    }

    public static async Task<string> DownloadAsync(ToolsRelease release, CancellationToken ct)
    {
        var dir = Path.Combine(CacheDir, string.Concat(release.Tag.Split(Path.GetInvalidFileNameChars())));
        var files = Path.Combine(dir, "files");
        var marker = Path.Combine(dir, ".complete");
        if (File.Exists(marker) && Directory.Exists(Installer.CemuSource(files)))
            return files;

        if (Directory.Exists(dir))
            Directory.Delete(dir, recursive: true);
        Directory.CreateDirectory(dir);

        var zipPath = Path.Combine(dir, release.ZipName);
        await using (var src = await Http.GetStreamAsync(release.ZipUrl, ct))
        await using (var dst = File.Create(zipPath))
            await src.CopyToAsync(dst, ct);

        ZipFile.ExtractToDirectory(zipPath, files);
        if (!File.Exists(Path.Combine(Installer.CemuSource(files), "rules.txt")))
            throw new InvalidDataException($"{release.ZipName} has no Cemu\\WWHD-Tools pack");
        File.WriteAllText(marker, release.Tag);
        return files;
    }
}
