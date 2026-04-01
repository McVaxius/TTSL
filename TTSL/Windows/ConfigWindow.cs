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
