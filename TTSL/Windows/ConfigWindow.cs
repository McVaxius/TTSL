using System;
using System.Numerics;
using Dalamud.Bindings.ImGui;
using Dalamud.Interface.Windowing;

namespace TTSL.Windows;

public sealed class ConfigWindow : PositionedWindow, IDisposable
{
    private static readonly string[] DtrModes = { "Text Only", "Icon+Text", "Icon Only" };
    private const string IconGuideUrl = "https://na.finalfantasyxiv.com/lodestone/character/22423564/blog/4393835";

    private readonly Plugin plugin;

    public ConfigWindow(Plugin plugin)
        : base($"{PluginInfo.DisplayName} Settings##TTSLConfig")
    {
        this.plugin = plugin;
        SizeConstraints = new WindowSizeConstraints
        {
            MinimumSize = new Vector2(520f, 420f),
            MaximumSize = new Vector2(980f, 860f),
        };
    }

    public void Dispose()
    {
    }

    public override void Draw()
    {
        var cfg = plugin.Configuration;
        var changed = false;

        ImGui.TextColored(new Vector4(0.95f, 0.75f, 0.35f, 1f), "Overlay");
        var overlayEnabled = cfg.OverlayEnabled;
        if (ImGui.Checkbox("Enable Thick Thighs Save Lives HUD", ref overlayEnabled))
        {
            cfg.OverlayEnabled = overlayEnabled;
            changed = true;
        }

        var krangleEnabled = cfg.KrangleEnabled;
        if (ImGui.Checkbox("Krangle displayed player names", ref krangleEnabled))
        {
            plugin.SetKrangleEnabled(krangleEnabled, "config");
            changed = false;
        }

        var showConditionPanel = cfg.ShowConditionPanel;
        if (ImGui.Checkbox("Show condition panel", ref showConditionPanel))
        {
            cfg.ShowConditionPanel = showConditionPanel;
            changed = true;
        }

        var showRepairSummary = cfg.ShowRepairSummary;
        if (ImGui.Checkbox("Show repair summary", ref showRepairSummary))
        {
            cfg.ShowRepairSummary = showRepairSummary;
            changed = true;
        }

        var showPartyStatus = cfg.ShowPartyStatus;
        if (ImGui.Checkbox("Show party status list", ref showPartyStatus))
        {
            cfg.ShowPartyStatus = showPartyStatus;
            changed = true;
        }

        var showPartyRadar = cfg.ShowPartyRadar;
        if (ImGui.Checkbox("Show party radar", ref showPartyRadar))
        {
            cfg.ShowPartyRadar = showPartyRadar;
            changed = true;
        }

        var enumeratePartyMembers = cfg.EnumeratePartyMembers;
        if (ImGui.Checkbox("Enumerate party members for radar labels", ref enumeratePartyMembers))
        {
            cfg.EnumeratePartyMembers = enumeratePartyMembers;
            changed = true;
        }

        var radarScale = cfg.RadarScaleYalms;
        if (ImGui.SliderFloat("Radar scale (yalms)", ref radarScale, 10f, 80f, "%.0f"))
        {
            cfg.RadarScaleYalms = radarScale;
            changed = true;
        }

        ImGui.Separator();
        ImGui.TextColored(new Vector4(0.85f, 0.55f, 1f, 1f), "Remote HUD Server");

        var remoteEnabled = cfg.RemoteServerEnabled;
        if (ImGui.Checkbox("Publish HUD snapshots to remote server", ref remoteEnabled))
        {
            cfg.RemoteServerEnabled = remoteEnabled;
            changed = true;
        }
        ImGui.SameLine();
        HelpMarker("Sends local HUD snapshots to the Python mini-server so multiple clients can be viewed in one browser.");

        var remoteUrl = cfg.RemoteServerUrl;
        ImGui.SetNextItemWidth(340f);
        if (ImGui.InputText("Server URL", ref remoteUrl, 256))
        {
            cfg.RemoteServerUrl = remoteUrl.Trim();
            changed = true;
        }
        ImGui.SameLine();
        if (ImGui.SmallButton("Use Local Default"))
        {
            cfg.RemoteServerUrl = "http://127.0.0.1:6942";
            changed = true;
        }

        var positionIntervalMs = cfg.RemotePositionIntervalMs;
        if (ImGui.InputInt("Fast position interval (ms)", ref positionIntervalMs, 25, 100))
        {
            cfg.RemotePositionIntervalMs = Math.Clamp(positionIntervalMs, 100, 10000);
            changed = true;
        }

        var fullSnapshotIntervalMs = cfg.RemoteFullSnapshotIntervalMs;
        if (ImGui.InputInt("Full snapshot interval (ms)", ref fullSnapshotIntervalMs, 100, 500))
        {
            cfg.RemoteFullSnapshotIntervalMs = Math.Clamp(fullSnapshotIntervalMs, 500, 30000);
            changed = true;
        }

        ImGui.TextDisabled("Default Python server: python .\\server\\ttsl_server.py --host 0.0.0.0 --port 6942");
        ImGui.TextDisabled("Clients are grouped by incoming account ID and character on the server page.");
        ImGui.TextDisabled("For future sheet/icon extraction, at least one client on the same PC as the Python monitor must connect first.");
        ImGui.TextDisabled("The server will cache the first same-PC game path it sees for the rest of that monitoring session.");
        ImGui.TextDisabled($"Current account ID: {plugin.GetCurrentAccountId()}");

        var remoteStatusColor = cfg.RemoteServerEnabled
            ? new Vector4(0.35f, 0.95f, 0.55f, 1f)
            : new Vector4(0.8f, 0.8f, 0.8f, 1f);
        ImGui.TextColored(remoteStatusColor, $"Publisher: {plugin.RemoteHudPublisher.StatusText}");
        if (!string.IsNullOrWhiteSpace(plugin.RemoteHudPublisher.LastError))
            ImGui.TextColored(new Vector4(1f, 0.55f, 0.4f, 1f), $"Last error: {plugin.RemoteHudPublisher.LastError}");

        ImGui.Separator();
        ImGui.TextColored(new Vector4(0.55f, 0.85f, 1f, 1f), "DTR");

        var dtrEnabled = cfg.DtrBarEnabled;
        if (ImGui.Checkbox("DTR Bar Enabled", ref dtrEnabled))
        {
            cfg.DtrBarEnabled = dtrEnabled;
            changed = true;
        }
        ImGui.SameLine();
        HelpMarker("Show or hide the server-info bar entry for TTSL.");

        var dtrMode = cfg.DtrBarMode;
        ImGui.SetNextItemWidth(150f);
        if (ImGui.Combo("DTR Bar Mode", ref dtrMode, DtrModes, DtrModes.Length))
        {
            cfg.DtrBarMode = dtrMode;
            changed = true;
        }
        ImGui.SameLine();
        HelpMarker("Text Only: 'TTSL: On/Off'\nIcon+Text: '<icon> TTSL'\nIcon Only: '<icon>'");

        ImGui.Spacing();
        ImGui.Text("DTR Icons (max 3 characters)");
        ImGui.SameLine();
        HelpMarker("Customize the glyphs used when TTSL is on or off.");
        ImGui.SameLine();
        if (ImGui.SmallButton("Copy Icon Guide Link"))
        {
            ImGui.SetClipboardText(IconGuideUrl);
            Plugin.Log.Information("[TTSL] Copied icon guide link to clipboard.");
        }
        if (ImGui.IsItemHovered())
            ImGui.SetTooltip("Copies the Lodestone blog link with suggested glyphs.");

        var enabledIcon = cfg.DtrIconEnabled;
        if (DrawIconInput("Enabled", ref enabledIcon, "\uE0BB"))
        {
            cfg.DtrIconEnabled = enabledIcon;
            changed = true;
        }

        var disabledIcon = cfg.DtrIconDisabled;
        if (DrawIconInput("Disabled", ref disabledIcon, "\uE0BC"))
        {
            cfg.DtrIconDisabled = disabledIcon;
            changed = true;
        }

        if (changed)
            plugin.SaveConfiguration();

        FinalizePendingWindowPlacement();
    }

    private static bool DrawIconInput(string label, ref string value, string fallback)
    {
        var changed = false;
        var iconValue = string.IsNullOrEmpty(value) ? fallback : value;
        ImGui.SetNextItemWidth(120f);
        if (ImGui.InputText($"{label} Icon", ref iconValue, 4))
        {
            if (iconValue.Length > 3)
                iconValue = iconValue[..3];

            value = iconValue;
            changed = true;
        }

        ImGui.SameLine();
        ImGui.TextDisabled($"Preview: {iconValue}");
        return changed;
    }

    private static void HelpMarker(string text)
    {
        ImGui.TextDisabled("(?)");
        if (ImGui.IsItemHovered())
            ImGui.SetTooltip(text);
    }
}
