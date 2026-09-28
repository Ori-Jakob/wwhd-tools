using System.IO;
using System.Xml.Linq;

namespace RandoPack;

public enum TitleKind { Usa, Eur, Jpn, Randomizer }

/// <summary>An extracted Wind Waker HD title (code/app.xml + cos.xml) found on disk.</summary>
public sealed class FoundTitle
{
    public required string TitleId { get; init; }
    public required TitleKind Kind { get; init; }
    public required bool IsUpdate { get; init; }
    public required string CodeDir { get; init; }
    public string? RpxPath { get; set; }
    public uint? Checksum { get; set; }
    public string? Error { get; set; }

    public bool IsRandomizer => Kind == TitleKind.Randomizer;

    public string KindLabel => Kind switch
    {
        TitleKind.Usa => "USA",
        TitleKind.Eur => "EUR",
        TitleKind.Jpn => "JPN",
        TitleKind.Randomizer => "Randomizer",
        _ => "?",
    } + (IsUpdate ? " (update)" : "");

    public string ChecksumLabel => Checksum is uint c ? $"0x{c:x8}" : Error ?? "";

    public string GameDir => Path.GetDirectoryName(CodeDir) ?? CodeDir;
}

public static class TitleScanner
{
    public const string RandoTitleId = "0005000010143599";

    // Cemu only enumerates a few levels below a game path, and the
    // mlc tree is usr/title/<hi>/<lo>/code, so this covers both.
    private const int MaxDepth = 6;

    public static List<FoundTitle> Scan(IEnumerable<string> roots, Action<string>? log, CancellationToken ct)
    {
        var titles = new List<FoundTitle>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var root in roots)
        {
            ct.ThrowIfCancellationRequested();
            if (!Directory.Exists(root))
            {
                log?.Invoke($"skipping missing folder {root}");
                continue;
            }
            Walk(root, 0, titles, seen, log, ct);
        }
        foreach (var t in titles)
        {
            ct.ThrowIfCancellationRequested();
            Hash(t);
        }
        return titles;
    }

    private static void Walk(string dir, int depth, List<FoundTitle> titles, HashSet<string> seen, Action<string>? log, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        var appXml = Path.Combine(dir, "app.xml");
        if (File.Exists(appXml))
        {
            if (seen.Add(Path.GetFullPath(dir)))
                Inspect(dir, appXml, titles, log);
            return;
        }
        if (depth >= MaxDepth)
            return;

        IEnumerable<string> subs;
        try { subs = Directory.EnumerateDirectories(dir); }
        catch (Exception e) { log?.Invoke($"{dir}: {e.Message}"); return; }

        foreach (var sub in subs)
        {
            var name = Path.GetFileName(sub);
            // Game data trees are huge and never hold a code folder.
            if (name.Equals("content", StringComparison.OrdinalIgnoreCase) ||
                name.Equals("meta", StringComparison.OrdinalIgnoreCase))
                continue;
            try
            {
                if ((File.GetAttributes(sub) & FileAttributes.ReparsePoint) != 0)
                    continue;
            }
            catch (Exception) { continue; }
            Walk(sub, depth + 1, titles, seen, log, ct);
        }
    }

    private static void Inspect(string codeDir, string appXml, List<FoundTitle> titles, Action<string>? log)
    {
        string? titleId;
        try { titleId = XDocument.Load(appXml).Root?.Element("title_id")?.Value.Trim().ToLowerInvariant(); }
        catch (Exception e) { log?.Invoke($"{appXml}: {e.Message}"); return; }
        if (titleId is null || titleId.Length != 16)
            return;

        var hi = titleId[..8];
        var lo = titleId[8..];
        bool isUpdate = hi == "0005000e";
        if (hi != "00050000" && !isUpdate)
            return;
        TitleKind kind;
        switch (lo)
        {
            case "10143500": kind = TitleKind.Usa; break;
            case "10143600": kind = TitleKind.Eur; break;
            case "10143400": kind = TitleKind.Jpn; break;
            case "10143599": kind = TitleKind.Randomizer; break;
            default: return;
        }

        var t = new FoundTitle { TitleId = titleId, Kind = kind, IsUpdate = isUpdate, CodeDir = codeDir };
        var cosXml = Path.Combine(codeDir, "cos.xml");
        try
        {
            var argstr = File.Exists(cosXml)
                ? XDocument.Load(cosXml).Root?.Element("argstr")?.Value.Trim()
                : null;
            // argstr can carry arguments after the executable name.
            var exe = string.IsNullOrEmpty(argstr) ? "cking.rpx" : argstr.Split(' ', 2)[0];
            t.RpxPath = Path.Combine(codeDir, exe);
        }
        catch (Exception e)
        {
            t.Error = $"cos.xml: {e.Message}";
        }
        titles.Add(t);
    }

    private static void Hash(FoundTitle t)
    {
        if (t.RpxPath is null)
            return;
        if (!File.Exists(t.RpxPath))
        {
            t.Error = $"missing {Path.GetFileName(t.RpxPath)}";
            return;
        }
        try { t.Checksum = CemuRplChecksum.Compute(t.RpxPath); }
        catch (Exception e) { t.Error = e.Message; }
    }
}
