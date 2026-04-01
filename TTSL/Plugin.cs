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

namespace TTSL;

public sealed class Plugin : IDalamudPlugin
{
    [PluginService] internal static IDalamudPluginInterface PluginInterface { get; private set; } = null!;
    [PluginService] internal static ICommandManager CommandManager { get; private set; } = null!;
    [PluginService] internal static IClientState ClientState { get; private set; } = null!;
    [PluginService] internal static IPlayerState PlayerState { get; private set; } = null!;
    [PluginService] internal static ICondition Condition { get; private set; } = null!;
    [PluginService] internal static IChatGui ChatGui { get; private set; } = null!;
    [PluginService] internal static IObjectTable ObjectTable { get; private set; } = null!;
    [PluginService] internal static IPartyList PartyList { get; private set; } = null!;
    [PluginService] internal static IDataManager DataManager { get; private set; } = null!;
    [PluginService] internal static IDtrBar DtrBar { get; private set; } = null!;
    [PluginService] internal static IPluginLog Log { get; private set; } = null!;

    public Configuration Configuration { get; }
    public readonly WindowSystem WindowSystem = new(PluginInfo.InternalName);
    public MainWindow MainWindow { get; }
    public ConfigWindow ConfigWindow { get; }

    private IDtrBarEntry? dtrEntry;

    public Plugin()
    {
        Configuration = PluginInterface.GetPluginConfig() as Configuration ?? new Configuration();

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

        SetupDtrBar();
        UpdateDtrBar();

        Log.Information("===TTSL loaded!===");
    }

    public void Dispose()
    {
        PluginInterface.UiBuilder.Draw -= WindowSystem.Draw;
        PluginInterface.UiBuilder.OpenMainUi -= ToggleMainUi;
        PluginInterface.UiBuilder.OpenConfigUi -= ToggleConfigUi;

        WindowSystem.RemoveAllWindows();
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
