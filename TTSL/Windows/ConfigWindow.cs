using System;
using TTSL.Ui;
using AethertekUI;
using System.Diagnostics;
using System.Numerics;
using Dalamud.Bindings.ImGui;
using Dalamud.Interface.Windowing;

namespace TTSL.Windows;

public sealed class ConfigWindow : PositionedWindow, IDisposable
{
    private readonly AethertekUI.Dalamud.MaterialSupportLog supportLog = new();
    private static readonly string[] DtrModes = { "Text Only", "Icon+Text", "Icon Only" };
    private const string IconGuideUrl = "https://na.finalfantasyxiv.com/lodestone/character/22423564/blog/4393835";
    private const string NativeServerReleaseUrl = "https://github.com/McVaxius/TTSL/releases/latest";

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
        WindowMotion.DrawChrome();
        UiGui.Title(PluginInfo.DisplayName+" Settings",PluginInfo.DisplayName+" "+UiText.T("Settings"));
        var settingsRoot = ImGui.GetID("");
        using var tabs = MaterialTabs.Begin("TtslSettingsTabs", new[] { UiText.T("Settings"), UiText.T("Window appearance") }, ImGuiTabBarFlags.FittingPolicyScroll);
        if (tabs.Visible)
        {
            using (var general = MaterialTabs.Item(UiText.T("Settings") + "###Settings", ImGuiTabItemFlags.NoPushId))
                if (general.Visible)
                {
                    ImGuiP.PushOverrideID(settingsRoot);
                    try { DrawGeneralSettings(); }
                    finally { ImGui.PopID(); }
                }
            using (var appearance = MaterialTabs.Item(UiText.T("Window appearance") + "###WindowAppearance", ImGuiTabItemFlags.NoPushId))
            {
                if (appearance.Visible)
                {
                    ImGuiP.PushOverrideID(settingsRoot);
                    try
                    {
                        plugin.Appearance.DrawSelector();
                        var compact = plugin.Configuration.UiCompact;
                        if (UiGui.Checkbox("C##CompactMode", ref compact))
                        {
                            plugin.Configuration.UiCompact = compact;
                            plugin.SaveConfiguration();
                        }
                        if (ImGui.IsItemHovered()) UiGui.SetTooltip("Compact mode");
                        plugin.Appearance.DrawWindowSettings();
                    }
                    finally { ImGui.PopID(); }
                }
            }
        }
        FinalizePendingWindowPlacement();
    }

    private void DrawGeneralSettings()
    {
        supportLog.Draw(Plugin.PluginInterface, key => UiText.T(key),
            path => System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo { FileName = path, UseShellExecute = true }), ex => Plugin.Log.Error(ex, "Dalamud log export failed."), Plugin.CommandManager);
        var cfg = plugin.Configuration;
        var changed = false;

        if (UiGui.Button("Setup Wizard"))
            plugin.OpenSetupWizard();
        ImGui.SameLine();
        UiGui.TextDisabled("Guided local HUD and web publisher setup");
        ImGui.Separator();

        UiGui.TextColored(MaterialTheme.Current.Colors.Primary, "Overlay");
        var overlayEnabled = cfg.OverlayEnabled;
        if (UiGui.Checkbox("Enable Thick Thighs Save Lives HUD", ref overlayEnabled))
        {
            cfg.OverlayEnabled = overlayEnabled;
            changed = true;
        }

        var krangleEnabled = cfg.KrangleEnabled;
        if (UiGui.Checkbox("Krangle displayed player names", ref krangleEnabled))
        {
            plugin.SetKrangleEnabled(krangleEnabled, "config");
            changed = false;
        }

        var showConditionPanel = cfg.ShowConditionPanel;
        if (UiGui.Checkbox("Show condition panel", ref showConditionPanel))
        {
            cfg.ShowConditionPanel = showConditionPanel;
            changed = true;
        }

        var showRepairSummary = cfg.ShowRepairSummary;
        if (UiGui.Checkbox("Show repair summary", ref showRepairSummary))
        {
            cfg.ShowRepairSummary = showRepairSummary;
            changed = true;
        }

        var showPartyStatus = cfg.ShowPartyStatus;
        if (UiGui.Checkbox("Show party status list", ref showPartyStatus))
        {
            cfg.ShowPartyStatus = showPartyStatus;
            changed = true;
        }

        var showPartyRadar = cfg.ShowPartyRadar;
        if (UiGui.Checkbox("Show party radar", ref showPartyRadar))
        {
            cfg.ShowPartyRadar = showPartyRadar;
            changed = true;
        }

        var enumeratePartyMembers = cfg.EnumeratePartyMembers;
        if (UiGui.Checkbox("Enumerate party members for radar labels", ref enumeratePartyMembers))
        {
            cfg.EnumeratePartyMembers = enumeratePartyMembers;
            changed = true;
        }

        var radarBoxSize = (int)MathF.Round(cfg.RadarBoxSizePixels);
        if (UiGui.InputInt("Radar box size (px)", ref radarBoxSize, 8, 24))
        {
            cfg.RadarBoxSizePixels = Math.Clamp(radarBoxSize, 96, 320);
            changed = true;
        }
        UiGui.TextDisabled("Display size of the local HUD radar box.");

        var radarCombatWidth = cfg.RadarCombatWidthYalms;
        if (UiGui.InputFloat("Combat radar width (yalms)", ref radarCombatWidth, 1f, 5f, "%.0f"))
        {
            cfg.RadarCombatWidthYalms = Math.Clamp(radarCombatWidth, 5f, 300f);
            changed = true;
        }

        var radarCombatHeight = cfg.RadarCombatHeightYalms;
        if (UiGui.InputFloat("Combat radar height (yalms)", ref radarCombatHeight, 1f, 5f, "%.0f"))
        {
            cfg.RadarCombatHeightYalms = Math.Clamp(radarCombatHeight, 5f, 300f);
            changed = true;
        }

        var radarTravelWidth = cfg.RadarOutOfCombatWidthYalms;
        if (UiGui.InputFloat("Travel radar width (yalms)", ref radarTravelWidth, 1f, 5f, "%.0f"))
        {
            cfg.RadarOutOfCombatWidthYalms = Math.Clamp(radarTravelWidth, 5f, 500f);
            changed = true;
        }

        var radarTravelHeight = cfg.RadarOutOfCombatHeightYalms;
        if (UiGui.InputFloat("Travel radar height (yalms)", ref radarTravelHeight, 1f, 5f, "%.0f"))
        {
            cfg.RadarOutOfCombatHeightYalms = Math.Clamp(radarTravelHeight, 5f, 500f);
            changed = true;
        }

        UiGui.TextDisabled("Default view is 20y x 20y in combat and 50y x 50y out of combat.");

        ImGui.Separator();
        UiGui.TextColored(MaterialTheme.Current.Colors.Primary, "Remote HUD Server");

        var remoteEnabled = cfg.RemoteServerEnabled;
        if (UiGui.Checkbox("Publish HUD snapshots to remote server", ref remoteEnabled))
        {
            cfg.RemoteServerEnabled = remoteEnabled;
            changed = true;
        }
        ImGui.SameLine();
        HelpMarker("Sends local HUD snapshots to the Python mini-server so multiple clients can be viewed in one browser.");

        var remoteUrl = cfg.RemoteServerUrl;
        ImGui.SetNextItemWidth(340f);
        if (UiGui.InputText("Server URL", ref remoteUrl, 256))
        {
            cfg.RemoteServerUrl = remoteUrl.Trim();
            changed = true;
        }
        ImGui.SameLine();
        if (UiGui.SmallButton("Use Local Default"))
        {
            cfg.RemoteServerUrl = "http://127.0.0.1:6942";
            changed = true;
        }
        ImGui.SameLine();
        if (UiGui.SmallButton("Open Web HUD"))
            plugin.OpenRemoteViewer();

        if (UiGui.Button("Download Native Server"))
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = NativeServerReleaseUrl,
                UseShellExecute = true,
            });
        }
        ImGui.SameLine();
        UiGui.TextDisabled("Release asset: latestServer.zip");

        var launchCommand = plugin.GetSuggestedServerLaunchCommand();
        ImGui.SetNextItemWidth(-115f);
        UiGui.InputText("Python launch command", ref launchCommand, 1024, ImGuiInputTextFlags.ReadOnly);
        ImGui.SameLine();
        if (UiGui.SmallButton("Copy Command"))
        {
            ImGui.SetClipboardText(launchCommand);
            Plugin.Log.Information("[TTSL] Copied Python server launch command to clipboard.");
        }
        if (ImGui.IsItemHovered())
            UiGui.SetTooltip("Copies the best local server-launch command TTSL could resolve from this install.");

        var loadedDllPath = typeof(Plugin).Assembly.Location;
        ImGui.SetNextItemWidth(-130f);
        UiGui.InputText("Loaded DLL", ref loadedDllPath, 1024, ImGuiInputTextFlags.ReadOnly);
        ImGui.SameLine();
        if (UiGui.SmallButton("Copy DLL Path"))
        {
            ImGui.SetClipboardText(loadedDllPath);
            Plugin.Log.Information("[TTSL] Copied loaded DLL path to clipboard.");
        }

        var positionIntervalMs = cfg.RemotePositionIntervalMs;
        if (UiGui.InputInt("Fast position interval (ms)", ref positionIntervalMs, 25, 100))
        {
            cfg.RemotePositionIntervalMs = Math.Clamp(positionIntervalMs, 100, 10000);
            changed = true;
        }

        var fullSnapshotIntervalMs = cfg.RemoteFullSnapshotIntervalMs;
        if (UiGui.InputInt("Full snapshot interval (ms)", ref fullSnapshotIntervalMs, 100, 500))
        {
            cfg.RemoteFullSnapshotIntervalMs = Math.Clamp(fullSnapshotIntervalMs, 500, 30000);
            changed = true;
        }

        ImGui.Spacing();
        UiGui.TextColored(MaterialTheme.Current.Colors.Primary, "Web Viewer Policy");

        var allowWebEchoCommands = cfg.AllowWebEchoCommands;
        if (UiGui.Checkbox("Allow web viewer text and slash commands", ref allowWebEchoCommands))
        {
            cfg.AllowWebEchoCommands = allowWebEchoCommands;
            changed = true;
        }
        ImGui.SameLine();
        HelpMarker("Plain text is sent to /echo with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim.");

        var allowWebScreenshotRequests = cfg.AllowWebScreenshotRequests;
        if (UiGui.Checkbox("Allow web viewer screenshot requests", ref allowWebScreenshotRequests))
        {
            cfg.AllowWebScreenshotRequests = allowWebScreenshotRequests;
            changed = true;
        }
        ImGui.SameLine();
        HelpMarker("Captures the current FFXIV game-window client area and uploads it to the Python server.");

        var allowWebCctvStreaming = cfg.AllowWebCctvStreaming;
        if (UiGui.Checkbox("Allow web viewer CCTV mode", ref allowWebCctvStreaming))
        {
            cfg.AllowWebCctvStreaming = allowWebCctvStreaming;
            changed = true;
        }
        ImGui.SameLine();
        HelpMarker("Allows the browser HUD to replace the map pane with a rolling live feed using low, medium, or high capture presets.");

        var enablePluginFullBodyFallback = cfg.EnablePluginFullBodyFallback;
        if (UiGui.Checkbox("Enable plugin full-body fallback", ref enablePluginFullBodyFallback))
        {
            cfg.EnablePluginFullBodyFallback = enablePluginFullBodyFallback;
            changed = true;
        }
        ImGui.SameLine();
        HelpMarker("Off by default. When enabled, the web HUD can ask this client to use CharacterInspect preview capture only as a fallback when Lodestone body art is unavailable.");

        UiGui.TextDisabled("Edit the copied command if you want LAN viewers: change --host 127.0.0.1 to --host 0.0.0.0.");
        UiGui.TextDisabled("Clients are grouped by incoming account ID and character on the server page.");
        UiGui.TextDisabled("The browser HUD now has live box-size and combat/travel yalm controls in its top toolbar.");
        UiGui.TextDisabled("For future sheet/icon extraction, at least one client on the same PC as the Python monitor must connect first.");
        UiGui.TextDisabled("The server will cache the first same-PC game path it sees for the rest of that monitoring session.");
        UiGui.TextDisabled("Plain web text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim, screenshot buttons use the uploader/source client for aggregate-party stranger data, and CCTV uses a rolling same-client frame cache.");
        UiGui.TextDisabled("TTSL settings are now stored per account ID once a live account is detected.");
        UiGui.TextDisabled(UiText.F("Current account ID: {0}", MainWindow.CurrentAccountDisplay(plugin.GetCurrentAccountId())));

        var remoteStatusColor = cfg.RemoteServerEnabled
            ? new Vector4(0.35f, 0.95f, 0.55f, 1f)
            : new Vector4(0.8f, 0.8f, 0.8f, 1f);
        UiGui.TextColored(remoteStatusColor, UiText.F("Publisher: {0}",UiText.T(plugin.RemoteHudPublisher.StatusText)));
        if (!string.IsNullOrWhiteSpace(plugin.RemoteHudPublisher.LastError))
            UiGui.TextColored(new Vector4(1f, 0.55f, 0.4f, 1f), $"Last error: {plugin.RemoteHudPublisher.LastError}");

        ImGui.Separator();
        UiGui.TextColored(MaterialTheme.Current.Colors.Primary, "DTR");

        var dtrEnabled = cfg.DtrBarEnabled;
        if (UiGui.Checkbox("DTR Bar Enabled", ref dtrEnabled))
        {
            cfg.DtrBarEnabled = dtrEnabled;
            changed = true;
        }
        ImGui.SameLine();
        HelpMarker("Show or hide the server-info bar entry for TTSL.");

        var dtrMode = cfg.DtrBarMode;
        ImGui.SetNextItemWidth(150f);
        if (UiGui.Combo("DTR Bar Mode", ref dtrMode, DtrModes, DtrModes.Length))
        {
            cfg.DtrBarMode = dtrMode;
            changed = true;
        }
        ImGui.SameLine();
        HelpMarker("Text Only: 'TTSL: On/Off'\nIcon+Text: '<icon> TTSL'\nIcon Only: '<icon>'");

        ImGui.Spacing();
        UiGui.Text("DTR Icons (max 3 characters)");
        ImGui.SameLine();
        HelpMarker("Customize the glyphs used when TTSL is on or off.");
        ImGui.SameLine();
        if (UiGui.SmallButton("Copy Icon Guide Link"))
        {
            ImGui.SetClipboardText(IconGuideUrl);
            Plugin.Log.Information("[TTSL] Copied icon guide link to clipboard.");
        }
        if (ImGui.IsItemHovered())
            UiGui.SetTooltip("Copies the Lodestone blog link with suggested glyphs.");

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

    }

    private static bool DrawIconInput(string label, ref string value, string fallback)
    {
        var changed = false;
        var iconValue = string.IsNullOrEmpty(value) ? fallback : value;
        ImGui.SetNextItemWidth(120f);
        if (UiGui.InputText($"{label} Icon", ref iconValue, 4))
        {
            if (iconValue.Length > 3)
                iconValue = iconValue[..3];

            value = iconValue;
            changed = true;
        }

        ImGui.SameLine();
        UiGui.TextDisabled($"Preview: {iconValue}");
        return changed;
    }

    private static void HelpMarker(string text)
    {
        UiGui.TextDisabled("(?)");
        if (ImGui.IsItemHovered())
            UiGui.SetTooltip(text);
    }

}
