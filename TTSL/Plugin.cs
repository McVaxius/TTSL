using System;
using Dalamud.Game.Command;
using Dalamud.Game.Gui.Dtr;
using Dalamud.Game.Text.SeStringHandling;
using Dalamud.Game.Text.SeStringHandling.Payloads;
using Dalamud.IoC;
using Dalamud.Interface.Windowing;
using Dalamud.Plugin;
using Dalamud.Plugin.Services;
using FFXIVClientStructs.FFXIV.Client.Game;
using Lumina.Excel.Sheets;
using TTSL.Services;
using TTSL.Windows;
using System.Diagnostics;
using System.IO;

namespace TTSL;

public sealed class Plugin : IDalamudPlugin
{
    [PluginService] internal static IDalamudPluginInterface PluginInterface { get; private set; } = null!;
    [PluginService] internal static ICommandManager CommandManager { get; private set; } = null!;
    [PluginService] internal static IClientState ClientState { get; private set; } = null!;
    [PluginService] internal static IPlayerState PlayerState { get; private set; } = null!;
    [PluginService] internal static ICondition Condition { get; private set; } = null!;
    [PluginService] internal static IChatGui ChatGui { get; private set; } = null!;
    [PluginService] internal static IFramework Framework { get; private set; } = null!;
    [PluginService] internal static IObjectTable ObjectTable { get; private set; } = null!;
    [PluginService] internal static IPartyList PartyList { get; private set; } = null!;
    [PluginService] internal static IDataManager DataManager { get; private set; } = null!;
    [PluginService] internal static ITargetManager TargetManager { get; private set; } = null!;
    [PluginService] internal static IDtrBar DtrBar { get; private set; } = null!;
    [PluginService] internal static IPluginLog Log { get; private set; } = null!;

    public Configuration Configuration { get; }
    public readonly WindowSystem WindowSystem = new(PluginInfo.InternalName);
    public MainWindow MainWindow { get; }
    public ConfigWindow ConfigWindow { get; }
    internal RemoteHudPublisherService RemoteHudPublisher { get; }

    private const string DefaultLocalServerHost = "127.0.0.1";
    private const int DefaultLocalServerPort = 6942;
    private IDtrBarEntry? dtrEntry;

    public Plugin()
    {
        Configuration = PluginInterface.GetPluginConfig() as Configuration ?? new Configuration();
        MigrateConfiguration();
        SyncActiveAccountConfiguration();
        RemoteHudPublisher = new RemoteHudPublisherService(this);

        MainWindow = new MainWindow(this);
        ConfigWindow = new ConfigWindow(this);
        WindowSystem.AddWindow(MainWindow);
        WindowSystem.AddWindow(ConfigWindow);

        CommandManager.AddHandler(PluginInfo.Command, new CommandInfo(OnCommand)
        {
            HelpMessage = "Thick Thighs Save Lives: /ttsl [config|toggle|ws|j]"
        });

        PluginInterface.UiBuilder.Draw += WindowSystem.Draw;
        PluginInterface.UiBuilder.OpenMainUi += ToggleMainUi;
        PluginInterface.UiBuilder.OpenConfigUi += ToggleConfigUi;
        Framework.Update += OnFrameworkUpdate;

        SetupDtrBar();
        UpdateDtrBar();

        Log.Information("===TTSL loaded!===");
    }

    public void Dispose()
    {
        Framework.Update -= OnFrameworkUpdate;
        PluginInterface.UiBuilder.Draw -= WindowSystem.Draw;
        PluginInterface.UiBuilder.OpenMainUi -= ToggleMainUi;
        PluginInterface.UiBuilder.OpenConfigUi -= ToggleConfigUi;

        WindowSystem.RemoveAllWindows();
        RemoteHudPublisher.Dispose();
        MainWindow.Dispose();
        ConfigWindow.Dispose();
        dtrEntry?.Remove();

        CommandManager.RemoveHandler(PluginInfo.Command);
        Log.Information("===TTSL unloaded!===");
    }

    public void SaveConfiguration()
    {
        Configuration.Save();
        UpdateDtrBar();
    }

    public void ToggleMainUi()
        => MainWindow.Toggle();

    public void ToggleConfigUi()
        => ConfigWindow.Toggle();

    public void SetOverlayEnabled(bool enabled, string source)
    {
        Configuration.OverlayEnabled = enabled;
        SaveConfiguration();
        Log.Information("[TTSL] Overlay {State} via {Source}.", enabled ? "enabled" : "disabled", source);
    }

    public void SetKrangleEnabled(bool enabled, string source)
    {
        Configuration.KrangleEnabled = enabled;
        KrangleService.ClearCache();
        SaveConfiguration();
        Log.Information("[TTSL] Krangle {State} via {Source}.", enabled ? "enabled" : "disabled", source);
    }

    public string GetDisplayName(string name)
        => Configuration.KrangleEnabled ? KrangleService.KrangleName(name) : name;

    public string GetCurrentAccountId()
        => PlayerState.ContentId == 0 ? "Unavailable" : PlayerState.ContentId.ToString("X16");

    public string GetRemoteViewerUrl()
    {
        var url = NormalizeRemoteViewerUrl(Configuration.RemoteServerUrl);
        return string.IsNullOrWhiteSpace(url) ? string.Empty : $"{url}/";
    }

    public void OpenRemoteViewer()
    {
        var url = GetRemoteViewerUrl();
        if (string.IsNullOrWhiteSpace(url))
        {
            Log.Warning("[TTSL] Cannot open the remote web HUD because the server URL is empty.");
            return;
        }

        Process.Start(new ProcessStartInfo
        {
            FileName = url,
            UseShellExecute = true,
        });
    }

    public string GetSuggestedServerLaunchCommand()
        => $"python \"{ResolveServerScriptPath()}\" --host {DefaultLocalServerHost} --port {DefaultLocalServerPort}";

    public void UpdateDtrBar()
    {
        if (dtrEntry == null)
            return;

        dtrEntry.Shown = Configuration.DtrBarEnabled;
        if (!Configuration.DtrBarEnabled)
            return;

        var iconEnabled = string.IsNullOrEmpty(Configuration.DtrIconEnabled) ? "\uE0BB" : Configuration.DtrIconEnabled;
        var iconDisabled = string.IsNullOrEmpty(Configuration.DtrIconDisabled) ? "\uE0BC" : Configuration.DtrIconDisabled;
        var icon = Configuration.OverlayEnabled ? iconEnabled : iconDisabled;
        var status = Configuration.OverlayEnabled ? "On" : "Off";
        dtrEntry.Text = Configuration.DtrBarMode switch
        {
            1 => new SeString(new TextPayload($"{icon} TTSL")),
            2 => new SeString(new TextPayload(icon)),
            _ => new SeString(new TextPayload($"TTSL: {status}")),
        };
        dtrEntry.Tooltip = new SeString(new TextPayload($"{PluginInfo.DisplayName} {status}. Click to toggle the HUD."));
    }

    public string GetTerritoryName(uint territoryId)
    {
        try
        {
            var sheet = DataManager.GetExcelSheet<TerritoryType>();
            if (sheet != null && sheet.TryGetRow(territoryId, out var territory))
            {
                var placeName = territory.PlaceName.Value.Name.ToString();
                if (!string.IsNullOrWhiteSpace(placeName))
                    return placeName;
            }
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "[TTSL] Failed to resolve territory name for {TerritoryId}.", territoryId);
        }

        return $"Territory {territoryId}";
    }

    public unsafe (int? MinCondition, int AverageCondition, int EquippedCount) GetRepairSummary()
    {
        try
        {
            var inventoryManager = InventoryManager.Instance();
            if (inventoryManager == null)
                return (null, 0, 0);

            var equippedContainer = inventoryManager->GetInventoryContainer(InventoryType.EquippedItems);
            if (equippedContainer == null)
                return (null, 0, 0);

            var minCondition = int.MaxValue;
            var totalCondition = 0;
            var count = 0;

            for (var i = 0; i < equippedContainer->Size; i++)
            {
                var item = equippedContainer->GetInventorySlot(i);
                if (item == null || item->ItemId == 0)
                    continue;

                var condition = item->Condition / 300;
                minCondition = Math.Min(minCondition, condition);
                totalCondition += condition;
                count++;
            }

            return count == 0
                ? (null, 0, 0)
                : (minCondition, totalCondition / count, count);
        }
        catch (Exception ex)
        {
            Log.Warning(ex, "[TTSL] Failed to read equipped-item durability summary.");
            return (null, 0, 0);
        }
    }

    private void SetupDtrBar()
    {
        dtrEntry = DtrBar.Get(PluginInfo.DisplayName);
        dtrEntry.OnClick = _ => SetOverlayEnabled(!Configuration.OverlayEnabled, "DTR");
    }

    private void MigrateConfiguration()
    {
        var changed = false;

        if (Configuration.Version < 2)
        {
            if (string.Equals(Configuration.RemoteServerUrl, "http://127.0.0.1:69420", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(Configuration.RemoteServerUrl, "127.0.0.1:69420", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(Configuration.RemoteServerUrl, "http://localhost:69420", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(Configuration.RemoteServerUrl, "localhost:69420", StringComparison.OrdinalIgnoreCase))
            {
                Configuration.RemoteServerUrl = "http://127.0.0.1:6942";
                changed = true;
            }

            Configuration.Version = 2;
            changed = true;
        }

        if (Configuration.Version < 3)
        {
            var migratedLegacyScale = Math.Clamp(Configuration.RadarScaleYalms, 5f, 200f);
            var useLegacyScale = Math.Abs(migratedLegacyScale - 35f) > 0.01f;

            if (Configuration.RadarBoxSizePixels <= 0f)
            {
                Configuration.RadarBoxSizePixels = 160f;
                changed = true;
            }

            if (Configuration.RadarCombatWidthYalms <= 0f)
            {
                Configuration.RadarCombatWidthYalms = useLegacyScale ? migratedLegacyScale : 20f;
                changed = true;
            }

            if (Configuration.RadarCombatHeightYalms <= 0f)
            {
                Configuration.RadarCombatHeightYalms = useLegacyScale ? migratedLegacyScale : 20f;
                changed = true;
            }

            if (Configuration.RadarOutOfCombatWidthYalms <= 0f)
            {
                Configuration.RadarOutOfCombatWidthYalms = useLegacyScale ? migratedLegacyScale : 50f;
                changed = true;
            }

            if (Configuration.RadarOutOfCombatHeightYalms <= 0f)
            {
                Configuration.RadarOutOfCombatHeightYalms = useLegacyScale ? migratedLegacyScale : 50f;
                changed = true;
            }

            Configuration.Version = 3;
            changed = true;
        }

        if (Configuration.Version < 4)
        {
            Configuration.Version = 4;
            changed = true;
        }

        if (Configuration.Version < 5)
        {
            Configuration.Version = 5;
            changed = true;
        }

        if (Configuration.Version < 6)
        {
            Configuration.Version = 6;
            changed = true;
        }

        if (changed)
            Configuration.Save();
    }

    private void OnFrameworkUpdate(IFramework framework)
    {
        SyncActiveAccountConfiguration();
        RemoteHudPublisher.Update();
    }

    private void SyncActiveAccountConfiguration()
    {
        var accountId = TryGetResolvedAccountId();
        if (string.IsNullOrWhiteSpace(accountId))
            return;

        if (!Configuration.EnsureActiveAccount(accountId))
            return;

        Log.Information("[TTSL] Activated account-scoped configuration for {AccountId}.", accountId);
        SaveConfiguration();
    }

    private string? TryGetResolvedAccountId()
    {
        var contentId = PlayerState.ContentId;
        return contentId == 0 ? null : contentId.ToString("X16");
    }

    private static string NormalizeRemoteViewerUrl(string url)
    {
        if (string.IsNullOrWhiteSpace(url))
            return string.Empty;

        var trimmed = url.Trim();
        if (!trimmed.StartsWith("http://", StringComparison.OrdinalIgnoreCase) &&
            !trimmed.StartsWith("https://", StringComparison.OrdinalIgnoreCase))
        {
            trimmed = $"http://{trimmed}";
        }

        return trimmed.TrimEnd('/');
    }

    private string ResolveServerScriptPath()
    {
        var pluginDirectory = PluginInterface.AssemblyLocation.Directory?.FullName;
        if (!string.IsNullOrWhiteSpace(pluginDirectory))
        {
            var resolved = ProbeServerScriptPath(pluginDirectory);
            if (!string.IsNullOrWhiteSpace(resolved))
                return resolved;
        }

        var codeDirectory = Path.GetDirectoryName(typeof(Plugin).Assembly.Location);
        if (!string.IsNullOrWhiteSpace(codeDirectory))
        {
            var resolved = ProbeServerScriptPath(codeDirectory);
            if (!string.IsNullOrWhiteSpace(resolved))
                return resolved;
        }

        return Path.GetFullPath(Path.Combine(pluginDirectory ?? Environment.CurrentDirectory, "server", "ttsl_server.py"));
    }

    private static string? ProbeServerScriptPath(string baseDirectory)
    {
        var relativeCandidates = new[]
        {
            Path.Combine("server", "ttsl_server.py"),
            Path.Combine("..", "server", "ttsl_server.py"),
            Path.Combine("..", "..", "server", "ttsl_server.py"),
            Path.Combine("..", "..", "..", "server", "ttsl_server.py"),
            Path.Combine("..", "..", "..", "..", "server", "ttsl_server.py"),
        };

        foreach (var relativePath in relativeCandidates)
        {
            var candidate = Path.GetFullPath(Path.Combine(baseDirectory, relativePath));
            if (File.Exists(candidate))
                return candidate;
        }

        return null;
    }

    private void OnCommand(string command, string args)
    {
        var arg = args.Trim().ToLowerInvariant();
        switch (arg)
        {
            case "config":
            case "settings":
                ConfigWindow.Toggle();
                break;

            case "toggle":
                SetOverlayEnabled(!Configuration.OverlayEnabled, "command");
                ChatGui.Print($"[TTSL] Overlay {(Configuration.OverlayEnabled ? "enabled" : "disabled")}.");
                break;

            case "ws":
                ResetWindowPositions();
                break;

            case "j":
                JumpWindowsToRandomVisibleLocations();
                break;

            default:
                MainWindow.Toggle();
                break;
        }
    }

    private void ResetWindowPositions()
    {
        MainWindow.QueueResetToOrigin();
        ConfigWindow.QueueResetToOrigin();
        MainWindow.IsOpen = true;
        ConfigWindow.IsOpen = true;
        ChatGui.Print("[TTSL] Queued main/config window reset to 1,1.");
    }

    private void JumpWindowsToRandomVisibleLocations()
    {
        MainWindow.QueueRandomVisibleJump();
        ConfigWindow.QueueRandomVisibleJump();
        MainWindow.IsOpen = true;
        ConfigWindow.IsOpen = true;
        ChatGui.Print("[TTSL] Queued random visible jumps for main/config windows.");
    }
}
