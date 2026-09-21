using System.Diagnostics;
using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;

namespace RandoPack;

public partial class MainWindow : Window
{
    private readonly ToolSettings _settings = ToolSettings.Load();
    private CancellationTokenSource? _scanCts;
    private List<ToolsPack> _packs = new();
    private ToolsPack? _mainPack;
    private bool _settingCombo;

    public MainWindow()
    {
        InitializeComponent();
        Loaded += (_, _) => Rescan();
    }

    private CemuInstall? Current => cmbCemu.SelectedItem as CemuInstall;
    private FoundTitle? SelectedTitle => lstTitles.SelectedItem as FoundTitle;

    private void Log(string message)
    {
        txtLog.AppendText(message + Environment.NewLine);
        txtLog.ScrollToEnd();
    }

    // ------------------------------------------------------------ Cemu --

    private void Rescan()
    {
        var installs = CemuInstall.Discover(_settings.CemuPaths, Log);
        _settingCombo = true;
        cmbCemu.ItemsSource = installs;
        _settingCombo = false;

        if (installs.Count == 0)
        {
            txtCemuInfo.Text = "No Cemu settings.xml found in %APPDATA%\\Cemu or %LOCALAPPDATA%\\Cemu. Use Browse to point at Cemu.exe (portable installs keep settings next to the exe or in a portable folder).";
            lstTitles.ItemsSource = null;
            ShowPack(null);
            UpdateApplyState();
            return;
        }

        // Prefer the Cemu that already has the pack installed.
        var preferred = installs.FirstOrDefault(i => ToolsPack.FindAll(i.GraphicPacksDir).Any(p => !p.UsesRpxMatch)) ?? installs[0];
        _settingCombo = true;
        cmbCemu.SelectedItem = preferred;
        _settingCombo = false;
        LoadInstall(preferred);
    }

    private void CmbCemu_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (!_settingCombo)
            LoadInstall(Current);
    }

    private async void LoadInstall(CemuInstall? inst)
    {
        _scanCts?.Cancel();
        if (inst is null)
            return;

        var info = new StringBuilder();
        info.AppendLine($"settings      {inst.SettingsPath}");
        info.AppendLine($"graphic packs {inst.GraphicPacksDir}");
        info.AppendLine($"mlc           {inst.MlcDir}");
        info.Append($"game paths    {(inst.GamePaths.Count == 0 ? "(none)" : string.Join("  |  ", inst.GamePaths))}");
        txtCemuInfo.Text = info.ToString();

        ShowPack(inst);
        await ScanTitles(inst);
    }

    private void BtnRescan_Click(object sender, RoutedEventArgs e) => Rescan();

    private void BtnBrowseCemu_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog
        {
            Title = "Pick Cemu.exe (or its settings.xml)",
            Filter = "Cemu|Cemu.exe;settings.xml|All files|*.*",
        };
        if (dlg.ShowDialog(this) != true)
            return;
        CemuInstall? inst;
        try { inst = CemuInstall.FromPath(dlg.FileName); }
        catch (Exception ex) { Log($"{dlg.FileName}: {ex.Message}"); return; }
        if (inst is null)
        {
            Log($"no settings.xml belongs to {dlg.FileName}; run Cemu once so it writes one");
            return;
        }
        _settings.Remember(_settings.CemuPaths, dlg.FileName);
        Rescan();
        var match = cmbCemu.Items.OfType<CemuInstall>().FirstOrDefault(i => i.ConfigDir.Equals(inst.ConfigDir, StringComparison.OrdinalIgnoreCase));
        if (match != null && !ReferenceEquals(match, Current))
            cmbCemu.SelectedItem = match;
    }

    // ---------------------------------------------------------- titles --

    private async Task ScanTitles(CemuInstall inst)
    {
        var cts = new CancellationTokenSource();
        _scanCts = cts;

        var roots = inst.GamePaths
            .Append(Path.Combine(inst.MlcDir, "usr", "title"))
            .Concat(_settings.GameFolders)
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();

        lstTitles.ItemsSource = null;
        UpdateApplyState();
        txtScanState.Text = "scanning…";
        Log("scanning " + string.Join(", ", roots));

        List<FoundTitle> titles;
        try
        {
            titles = await Task.Run(
                () => TitleScanner.Scan(roots, m => Dispatcher.Invoke(() => Log(m)), cts.Token),
                cts.Token);
        }
        catch (OperationCanceledException) { return; }
        catch (Exception ex)
        {
            if (ReferenceEquals(cts, _scanCts)) { txtScanState.Text = "scan failed"; Log("scan failed: " + ex.Message); }
            return;
        }
        if (!ReferenceEquals(cts, _scanCts))
            return;

        txtScanState.Text = $"{titles.Count} title(s)";
        lstTitles.ItemsSource = titles;
        foreach (var t in titles)
            Log($"{t.KindLabel,-14} {t.ChecksumLabel,-12} {t.GameDir}");

        var rando = titles.FirstOrDefault(t => t.IsRandomizer && t.Checksum != null);
        if (rando != null)
            lstTitles.SelectedItem = rando;
        else
            Log("no randomizer found; add its folder (the one holding code/, content/ and meta/) with 'Add game folder'");
        UpdateApplyState();
    }

    private void LstTitles_SelectionChanged(object sender, SelectionChangedEventArgs e) => UpdateApplyState();

    private async void BtnAddFolder_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFolderDialog { Title = "Folder holding the randomizer (code/, content/, meta/)" };
        if (dlg.ShowDialog(this) != true)
            return;
        if (!_settings.Remember(_settings.GameFolders, dlg.FolderName))
            Log($"already scanning {dlg.FolderName}");
        if (Current is { } inst)
            await ScanTitles(inst);
    }

    private async void BtnChecksum_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog { Title = "Checksum an rpx", Filter = "Wii U executables|*.rpx;*.rpl|All files|*.*" };
        if (dlg.ShowDialog(this) != true)
            return;
        try
        {
            var crc = await Task.Run(() => CemuRplChecksum.Compute(dlg.FileName));
            Log($"0x{crc:x8}  {dlg.FileName}");
        }
        catch (Exception ex) { Log($"{dlg.FileName}: {ex.Message}"); }
    }

    // ------------------------------------------------------------ pack --

    private void ShowPack(CemuInstall? inst)
    {
        _packs = inst is null ? new List<ToolsPack>() : ToolsPack.FindAll(inst.GraphicPacksDir);
        _mainPack = _packs.FirstOrDefault(p => !p.UsesRpxMatch);
        RenderPack(inst);
    }

    private void RenderPack(CemuInstall? inst)
    {
        var sb = new StringBuilder();
        if (_mainPack is null)
        {
            sb.Append(inst is null
                ? "No Cemu selected."
                : $"No WWHD Tools pack under {inst.GraphicPacksDir}.\nCopy the Cemu\\WWHD-Tools folder of the release zip there, or use Pick pack.");
        }
        else
        {
            sb.AppendLine(_mainPack.Dir);
            sb.Append($"name          \"{_mainPack.Name}\"");
            if (inst != null)
                sb.Append(inst.IsPackEnabled(_mainPack.RulesPath) ? "   (enabled in Cemu)" : "   (NOT enabled in Cemu's graphic pack list)");
            sb.AppendLine();
            sb.AppendLine($"titleIds      {string.Join(",", _mainPack.TitleIds)}");
            foreach (var (group, matches) in _mainPack.ModuleMatches)
                sb.AppendLine($"[{group,-12}] moduleMatches = {matches}");
        }

        foreach (var other in _packs.Where(p => !ReferenceEquals(p, _mainPack) && p.ListsRando))
        {
            sb.AppendLine();
            sb.Append($"Also present: {other.Dir} (\"{other.Name}\"");
            if (inst != null)
                sb.Append(inst.IsPackEnabled(other.RulesPath) ? ", enabled" : ", not enabled");
            sb.AppendLine(").");
            sb.Append("It targets the randomizer too; keep only one of the two enabled in Cemu or the codecave is applied twice.");
        }
        txtPack.Text = sb.ToString().TrimEnd();
        btnOpenPack.IsEnabled = _mainPack != null;
        UpdateApplyState();
    }

    private void BtnOpenPack_Click(object sender, RoutedEventArgs e)
    {
        if (_mainPack is null)
            return;
        Process.Start(new ProcessStartInfo("explorer.exe", $"\"{_mainPack.Dir}\"") { UseShellExecute = true });
    }

    private void BtnPickPack_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFolderDialog { Title = "Pack folder holding rules.txt and patch_wwhd_tools.asm" };
        if (dlg.ShowDialog(this) != true)
            return;
        var pack = ToolsPack.Load(dlg.FolderName);
        if (pack is null)
        {
            Log($"{dlg.FolderName}: no rules.txt + {ToolsPack.AsmFileName} there");
            return;
        }
        if (pack.UsesRpxMatch)
            Log("note: this pack matches any rpx already, so it needs no seed checksum");
        _packs.RemoveAll(p => p.Dir.Equals(pack.Dir, StringComparison.OrdinalIgnoreCase));
        _packs.Insert(0, pack);
        _mainPack = pack;
        RenderPack(Current);
    }

    // ----------------------------------------------------------- apply --

    private void UpdateApplyState()
    {
        var t = SelectedTitle;
        btnApply.IsEnabled = _mainPack != null && t != null && t.IsRandomizer && t.Checksum != null;
    }

    private void BtnApply_Click(object sender, RoutedEventArgs e)
    {
        if (_mainPack is null || SelectedTitle is not { IsRandomizer: true, Checksum: uint hash } title)
            return;

        PackPatcher.Plan plan;
        try { plan = PackPatcher.Build(_mainPack, hash, chkPrune.IsChecked == true); }
        catch (Exception ex) { Log("could not read the pack: " + ex.Message); return; }

        if (plan.IsNoop)
        {
            Log($"pack already matches seed 0x{hash:x8}; nothing to write");
            MessageBox.Show(this, "The pack already lists this seed.", "Nothing to do", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }

        var question = $"Seed 0x{hash:x8} from\n{title.GameDir}\n\nWrite these changes to\n{_mainPack.Dir}?\n\n" + string.Join("\n", plan.Changes);
        if (MessageBox.Show(this, question, "Apply seed to pack", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes)
            return;

        try { PackPatcher.Write(_mainPack, plan, chkBackup.IsChecked == true); }
        catch (Exception ex)
        {
            Log("write failed: " + ex.Message);
            MessageBox.Show(this, ex.Message, "Write failed", MessageBoxButton.OK, MessageBoxImage.Error);
            return;
        }

        foreach (var c in plan.Changes)
            Log("  " + c);
        Log("written. Restart Cemu (or reload its graphic packs) so it re-reads the pack.");
        if (Current is { } inst && !inst.IsPackEnabled(_mainPack.RulesPath))
            Log("the pack is not enabled in Cemu; tick it under Graphic packs > The Legend of Zelda: The Wind Waker HD > Mods.");
        RenderPack(Current);
    }
}
