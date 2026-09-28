using System.IO;
using System.Text.Json;

namespace RandoPack;

/// <summary>What the user told us that Cemu's own files cannot: extra Cemu installs and game folders.</summary>
public sealed class ToolSettings
{
    /// <summary>Cemu.exe or settings.xml paths the user browsed to.</summary>
    public List<string> CemuPaths { get; set; } = new();

    /// <summary>Folders to scan for titles besides Cemu's game paths and mlc.</summary>
    public List<string> GameFolders { get; set; } = new();

    /// <summary>Wii U SD card roots to keep up to date.</summary>
    public List<string> SdCards { get; set; } = new();

    private static string FilePath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "WwhdRandoPack", "settings.json");

    public static ToolSettings Load()
    {
        try
        {
            if (File.Exists(FilePath))
                return JsonSerializer.Deserialize<ToolSettings>(File.ReadAllText(FilePath)) ?? new ToolSettings();
        }
        catch (Exception) { /* a damaged file just means defaults */ }
        return new ToolSettings();
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
            File.WriteAllText(FilePath, JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
        }
        catch (Exception) { /* nothing the user can act on */ }
    }

    public bool Remember(List<string> list, string value)
    {
        if (list.Contains(value, StringComparer.OrdinalIgnoreCase))
            return false;
        list.Add(value);
        Save();
        return true;
    }
}
