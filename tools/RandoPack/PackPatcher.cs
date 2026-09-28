using System.Globalization;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;

namespace RandoPack;

/// <summary>A WWHD Tools graphics pack folder: rules.txt plus patch_wwhd_tools.asm.</summary>
public sealed class ToolsPack
{
    public const string AsmFileName = "patch_wwhd_tools.asm";

    public required string Dir { get; init; }
    public string RulesPath => Path.Combine(Dir, "rules.txt");
    public string AsmPath => Path.Combine(Dir, AsmFileName);

    public string Name { get; private set; } = "";
    public List<string> TitleIds { get; } = new();

    /// <summary>Group name to the raw moduleMatches value, in file order.</summary>
    public List<(string Group, string Matches)> ModuleMatches { get; } = new();

    /// <summary>True when any group uses "moduleMatches = rpx", the seed-independent scheme of the CI rando pack.</summary>
    public bool UsesRpxMatch => ModuleMatches.Any(m => m.Matches.Trim().Equals("rpx", StringComparison.OrdinalIgnoreCase));

    public bool ListsRando => TitleIds.Contains(TitleScanner.RandoTitleId, StringComparer.OrdinalIgnoreCase);

    public static ToolsPack? Load(string dir)
    {
        var pack = new ToolsPack { Dir = Path.GetFullPath(dir) };
        if (!File.Exists(pack.RulesPath) || !File.Exists(pack.AsmPath))
            return null;
        pack.Reload();
        return pack;
    }

    public void Reload()
    {
        Name = "";
        TitleIds.Clear();
        ModuleMatches.Clear();
        foreach (var line in File.ReadLines(RulesPath))
        {
            var m = PackPatcher.KeyValue.Match(line);
            if (!m.Success) continue;
            var key = m.Groups[1].Value;
            var value = m.Groups[2].Value.Trim();
            if (key.Equals("name", StringComparison.OrdinalIgnoreCase))
                Name = value.Trim('"');
            else if (key.Equals("titleIds", StringComparison.OrdinalIgnoreCase))
                TitleIds.AddRange(PackPatcher.SplitList(value));
        }
        string group = "";
        foreach (var line in File.ReadLines(AsmPath))
        {
            var h = PackPatcher.GroupHeader.Match(line);
            if (h.Success) { group = h.Groups[1].Value; continue; }
            var m = PackPatcher.KeyValue.Match(line);
            if (m.Success && m.Groups[1].Value.Equals("moduleMatches", StringComparison.OrdinalIgnoreCase))
                ModuleMatches.Add((group, m.Groups[2].Value.Trim()));
        }
    }

    /// <summary>Every pack under graphicPacks that carries our asm file, however it is named.</summary>
    public static List<ToolsPack> FindAll(string graphicPacksDir)
    {
        var packs = new List<ToolsPack>();
        if (!Directory.Exists(graphicPacksDir))
            return packs;
        IEnumerable<string> asms;
        try
        {
            asms = Directory.EnumerateFiles(graphicPacksDir, AsmFileName, new EnumerationOptions
            {
                RecurseSubdirectories = true,
                MaxRecursionDepth = 4,
                IgnoreInaccessible = true,
            });
        }
        catch (Exception) { return packs; }
        foreach (var asm in asms)
        {
            var pack = Load(Path.GetDirectoryName(asm)!);
            if (pack != null) packs.Add(pack);
        }
        return packs;
    }
}

/// <summary>
/// Adds a randomizer seed to a WWHD Tools pack the way the pack once shipped
/// it: the rando title in rules.txt, and the seed's rpx checksum on the
/// shared and USA groups of the asm. The rando is the USA build plus
/// appended code, so the EUR group is left alone.
/// </summary>
public static class PackPatcher
{
    public static readonly Regex GroupHeader = new(@"^\s*\[(\w+)\]", RegexOptions.Compiled);
    public static readonly Regex KeyValue = new(@"^\s*(\w+)\s*=\s*(.*)$", RegexOptions.Compiled);

    /// <summary>Checksums of the retail cking.rpx; anything else on a hash group is an old seed.</summary>
    public static readonly uint[] RetailHashes = { 0x475bd29f, 0xb7e748de };

    public static readonly string[] HashGroups = { "WWHDv16", "WWHDv16_USA" };

    public sealed record Plan(string Rules, string Asm, List<string> Changes)
    {
        public bool IsNoop => Changes.Count == 0;
    }

    public static IEnumerable<string> SplitList(string value) =>
        value.Split(',').Select(s => s.Trim()).Where(s => s.Length > 0);

    public static bool TryParseHash(string token, out uint value) =>
        uint.TryParse(token.Trim().Replace("0x", "", StringComparison.OrdinalIgnoreCase),
                      NumberStyles.HexNumber, CultureInfo.InvariantCulture, out value);

    public static Plan Build(ToolsPack pack, uint seedHash, bool pruneOldSeeds)
    {
        var changes = new List<string>();
        var rules = Rewrite(File.ReadAllText(pack.RulesPath), line =>
        {
            var m = KeyValue.Match(line);
            if (!m.Success || !m.Groups[1].Value.Equals("titleIds", StringComparison.OrdinalIgnoreCase))
                return line;
            var ids = SplitList(m.Groups[2].Value).ToList();
            if (ids.Contains(TitleScanner.RandoTitleId, StringComparer.OrdinalIgnoreCase))
                return line;
            ids.Add(TitleScanner.RandoTitleId);
            changes.Add($"rules.txt: titleIds += {TitleScanner.RandoTitleId}");
            return $"titleIds = {string.Join(",", ids)}";
        });

        string group = "";
        var asm = Rewrite(File.ReadAllText(pack.AsmPath), line =>
        {
            var h = GroupHeader.Match(line);
            if (h.Success) { group = h.Groups[1].Value; return line; }
            var m = KeyValue.Match(line);
            if (!m.Success || !m.Groups[1].Value.Equals("moduleMatches", StringComparison.OrdinalIgnoreCase))
                return line;
            if (!HashGroups.Contains(group))
                return line;
            var value = m.Groups[2].Value.Trim();
            if (value.Equals("rpx", StringComparison.OrdinalIgnoreCase))
            {
                changes.Add($"[{group}] already matches any rpx; left as is");
                return line;
            }

            var tokens = SplitList(value).ToList();
            var kept = new List<string>();
            bool present = false;
            foreach (var tok in tokens)
            {
                if (!TryParseHash(tok, out var v)) { kept.Add(tok); continue; }
                if (v == seedHash) { present = true; kept.Add(tok); continue; }
                if (pruneOldSeeds && !RetailHashes.Contains(v))
                {
                    changes.Add($"[{group}] moduleMatches -= {tok} (old seed)");
                    continue;
                }
                kept.Add(tok);
            }
            if (!present)
            {
                kept.Add($"0x{seedHash:x8}");
                changes.Add($"[{group}] moduleMatches += 0x{seedHash:x8}");
            }
            if (kept.SequenceEqual(tokens))
                return line;
            return $"moduleMatches = {string.Join(", ", kept)}";
        });

        return new Plan(rules, asm, changes);
    }

    public static void Write(ToolsPack pack, Plan plan, bool backup)
    {
        var utf8 = new UTF8Encoding(encoderShouldEmitUTF8Identifier: false);
        if (backup)
        {
            File.Copy(pack.RulesPath, pack.RulesPath + ".bak", overwrite: true);
            File.Copy(pack.AsmPath, pack.AsmPath + ".bak", overwrite: true);
        }
        File.WriteAllText(pack.RulesPath, plan.Rules, utf8);
        File.WriteAllText(pack.AsmPath, plan.Asm, utf8);
        pack.Reload();
    }

    /// <summary>Line-wise rewrite that keeps the file's newline style and trailing newline.</summary>
    private static string Rewrite(string text, Func<string, string> map)
    {
        var nl = text.Contains("\r\n") ? "\r\n" : "\n";
        bool trailing = text.EndsWith('\n');
        var body = trailing ? text[..^1] : text;
        if (trailing && body.EndsWith('\r'))
            body = body[..^1];
        var lines = body.Split('\n');
        var sb = new StringBuilder(text.Length + 64);
        for (int i = 0; i < lines.Length; i++)
        {
            if (i > 0)
                sb.Append(nl);
            sb.Append(map(lines[i].TrimEnd('\r')));
        }
        if (trailing)
            sb.Append(nl);
        return sb.ToString();
    }
}
