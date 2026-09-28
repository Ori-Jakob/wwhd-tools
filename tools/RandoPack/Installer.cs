using System.ComponentModel;
using System.IO;

namespace RandoPack;

public enum InstallKind { Cemu, WiiU }

public sealed class InstallTarget : INotifyPropertyChanged
{
    public required InstallKind Kind { get; init; }
    public required string Dir { get; init; }

    private bool _selected;
    private string _status = "";

    public bool Selected
    {
        get => _selected;
        set { _selected = value; Changed(nameof(Selected)); }
    }

    public string Status
    {
        get => _status;
        set { _status = value; Changed(nameof(Status)); }
    }

    public string KindLabel => Kind == InstallKind.Cemu ? "Cemu pack" : "Wii U SD card";

    public event PropertyChangedEventHandler? PropertyChanged;

    private void Changed(string name) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}

public static class Installer
{
    public const string DefaultPackName = "WWHD-Tools";

    public static string CemuSource(string release) => Path.Combine(release, "Cemu", DefaultPackName);
    public static string WiiUSource(string release) => Path.Combine(release, "Wii U");

    public static bool IsWiiUSd(string root) => Directory.Exists(Path.Combine(root, "wiiu"));

    public static string SourceFor(InstallTarget t, string release) =>
        t.Kind == InstallKind.Cemu ? CemuSource(release) : WiiUSource(release);

    // rules.txt and the asm are compared without the seed lines Apply adds.
    public static bool IsCurrent(InstallTarget t, string release)
    {
        var src = SourceFor(t, release);
        foreach (var file in Directory.EnumerateFiles(src, "*", SearchOption.AllDirectories))
        {
            var dst = Path.Combine(t.Dir, Path.GetRelativePath(src, file));
            if (!File.Exists(dst))
                return false;
            var name = Path.GetFileName(file);
            bool same = t.Kind == InstallKind.Cemu &&
                        (name.Equals("rules.txt", StringComparison.OrdinalIgnoreCase) ||
                         name.Equals(ToolsPack.AsmFileName, StringComparison.OrdinalIgnoreCase))
                ? WithoutSeedLines(file) == WithoutSeedLines(dst)
                : File.ReadAllBytes(file).AsSpan().SequenceEqual(File.ReadAllBytes(dst));
            if (!same)
                return false;
        }
        return true;
    }

    public static void Update(InstallTarget t, string release, Action<string> log)
    {
        if (t.Kind == InstallKind.Cemu)
            UpdateCemuPack(t.Dir, release, log);
        else
            CopyTree(WiiUSource(release), t.Dir, log);
    }

    public static ToolsPack UpdateCemuPack(string dir, string release, Action<string> log)
    {
        var old = ToolsPack.Load(dir);
        var seeds = old is null ? new List<uint>() : SeedsOf(old);
        CopyTree(CemuSource(release), dir, log);
        var pack = ToolsPack.Load(dir) ?? throw new InvalidDataException($"{dir} has no pack after the copy");
        foreach (var seed in seeds)
        {
            var plan = PackPatcher.Build(pack, seed, pruneOldSeeds: false);
            if (!plan.IsNoop)
                PackPatcher.Write(pack, plan, backup: false);
            log($"  kept seed 0x{seed:x8}");
        }
        return pack;
    }

    public static List<uint> SeedsOf(ToolsPack pack) =>
        pack.ModuleMatches
            .Where(m => PackPatcher.HashGroups.Contains(m.Group))
            .SelectMany(m => PackPatcher.SplitList(m.Matches))
            .Select(tok => PackPatcher.TryParseHash(tok, out var v) ? v : 0u)
            .Where(v => v != 0 && !PackPatcher.RetailHashes.Contains(v))
            .Distinct()
            .ToList();

    private static void CopyTree(string src, string dst, Action<string> log)
    {
        foreach (var file in Directory.EnumerateFiles(src, "*", SearchOption.AllDirectories))
        {
            var target = Path.Combine(dst, Path.GetRelativePath(src, file));
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            File.Copy(file, target, overwrite: true);
            log($"  {target}");
        }
    }

    private static string WithoutSeedLines(string path) =>
        string.Join("\n", File.ReadLines(path)
            .Select(l => l.TrimEnd())
            .Where(l =>
            {
                var m = PackPatcher.KeyValue.Match(l);
                if (!m.Success) return true;
                var key = m.Groups[1].Value;
                return !key.Equals("moduleMatches", StringComparison.OrdinalIgnoreCase) &&
                       !key.Equals("titleIds", StringComparison.OrdinalIgnoreCase);
            }));
}
