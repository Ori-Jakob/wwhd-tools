using System.Diagnostics;
using System.IO;
using System.Xml.Linq;
using Microsoft.Win32;

namespace RandoPack;

/// <summary>
/// One Cemu configuration: the folder holding settings.xml, which is also
/// where Cemu keeps graphicPacks and, unless mlc_path overrides it, mlc01.
/// </summary>
public sealed class CemuInstall
{
    public required string ConfigDir { get; init; }
    public string? ExeDir { get; init; }

    public string SettingsPath => Path.Combine(ConfigDir, "settings.xml");
    public string GraphicPacksDir => Path.Combine(ConfigDir, "graphicPacks");
    public string MlcDir { get; private set; } = "";
    public List<string> GamePaths { get; } = new();

    /// <summary>rules.txt paths of enabled packs, relative to ConfigDir, as Cemu writes them.</summary>
    public List<string> EnabledPacks { get; } = new();

    public string Label => ExeDir is null ? ConfigDir : $"{ExeDir}   (config in {ConfigDir})";

    public override string ToString() => Label;

    public static CemuInstall? FromConfigDir(string dir, string? exeDir = null)
    {
        if (!File.Exists(Path.Combine(dir, "settings.xml")))
            return null;
        var inst = new CemuInstall { ConfigDir = Path.GetFullPath(dir), ExeDir = exeDir };
        inst.Load();
        return inst;
    }

    /// <summary>
    /// Cemu's own rule (ActiveSettings::LoadOnce): a "portable" folder next
    /// to the exe wins; the Cemu 2.0.x builds kept settings.xml next to the
    /// exe; otherwise everything lives in %APPDATA%\Cemu.
    /// </summary>
    public static CemuInstall? FromExe(string exePath)
    {
        var exeDir = Path.GetDirectoryName(Path.GetFullPath(exePath));
        if (exeDir is null)
            return null;
        return FromConfigDir(Path.Combine(exeDir, "portable"), exeDir)
            ?? FromConfigDir(exeDir, exeDir)
            ?? FromConfigDir(RoamingDir, exeDir);
    }

    /// <summary>Either a Cemu.exe or a settings.xml the user pointed us at.</summary>
    public static CemuInstall? FromPath(string path)
    {
        if (Path.GetFileName(path).Equals("settings.xml", StringComparison.OrdinalIgnoreCase))
            return FromConfigDir(Path.GetDirectoryName(Path.GetFullPath(path))!);
        return FromExe(path);
    }

    public static string RoamingDir =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Cemu");

    public static string LocalDir =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Cemu");

    /// <summary>
    /// Every Cemu configuration this machine is known to have: the exes the
    /// user pointed us at before, any Cemu that is running right now, and the
    /// two per-user folders Cemu defaults to.
    /// </summary>
    public static List<CemuInstall> Discover(IEnumerable<string> rememberedPaths, Action<string>? log = null)
    {
        var found = new List<CemuInstall>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        void Add(CemuInstall? inst)
        {
            if (inst != null && seen.Add(inst.ConfigDir))
                found.Add(inst);
        }

        foreach (var p in rememberedPaths)
        {
            try
            {
                if (File.Exists(p)) Add(FromPath(p));
                else log?.Invoke($"remembered Cemu is gone: {p}");
            }
            catch (Exception e) { log?.Invoke($"{p}: {e.Message}"); }
        }

        foreach (var p in Process.GetProcessesByName("Cemu"))
        {
            try
            {
                var exe = p.MainModule?.FileName;
                if (exe != null) Add(FromExe(exe));
            }
            catch (Exception) { /* elevated or exiting process; nothing to learn from it */ }
            finally { p.Dispose(); }
        }

        foreach (var exe in RecentlyRunCemuExes())
        {
            try { Add(FromExe(exe)); }
            catch (Exception e) { log?.Invoke($"{exe}: {e.Message}"); }
        }

        Add(FromConfigDir(RoamingDir));
        Add(FromConfigDir(LocalDir));
        return found;
    }

    /// <summary>
    /// Windows keeps the paths of executables it has run under a few per-user
    /// keys; that is the only trace a portable Cemu leaves outside its folder.
    /// </summary>
    private static List<string> RecentlyRunCemuExes()
    {
        var keys = new[]
        {
            @"Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Compatibility Assistant\Store",
            @"Software\Microsoft\Windows\CurrentVersion\Explorer\FeatureUsage\AppSwitched",
            @"Software\Microsoft\Windows\CurrentVersion\Explorer\FeatureUsage\ShowJumpView",
        };
        var result = new List<string>();
        foreach (var k in keys)
        {
            try
            {
                using var key = Registry.CurrentUser.OpenSubKey(k);
                if (key is null) continue;
                foreach (var name in key.GetValueNames())
                {
                    if (name.EndsWith(@"\Cemu.exe", StringComparison.OrdinalIgnoreCase) && File.Exists(name) &&
                        !result.Contains(name, StringComparer.OrdinalIgnoreCase))
                        result.Add(name);
                }
            }
            catch (Exception) { /* a missing or locked key just means no hint */ }
        }
        return result;
    }

    private void Load()
    {
        var root = XDocument.Load(SettingsPath).Root;
        var mlc = root?.Element("mlc_path")?.Value.Trim();
        MlcDir = string.IsNullOrEmpty(mlc) ? Path.Combine(ConfigDir, "mlc01") : mlc;

        GamePaths.Clear();
        foreach (var e in root?.Element("GamePaths")?.Elements("Entry") ?? Enumerable.Empty<XElement>())
        {
            var v = e.Value.Trim();
            if (v.Length > 0) GamePaths.Add(v);
        }

        EnabledPacks.Clear();
        foreach (var e in root?.Element("GraphicPack")?.Elements("Entry") ?? Enumerable.Empty<XElement>())
        {
            var v = (string?)e.Attribute("filename");
            if (!string.IsNullOrEmpty(v)) EnabledPacks.Add(v);
        }
    }

    public bool IsPackEnabled(string rulesPath)
    {
        var rel = Path.GetRelativePath(ConfigDir, rulesPath).Replace('\\', '/');
        return EnabledPacks.Any(p => string.Equals(p.Replace('\\', '/'), rel, StringComparison.OrdinalIgnoreCase));
    }
}
