using System;
using TTSL.Ui;
using AethertekUI;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Numerics;
using System.Reflection;
using Dalamud.Bindings.ImGui;
using Dalamud.Game.ClientState.Conditions;
using Dalamud.Game.ClientState.Objects.Types;
using Dalamud.Interface;
using Dalamud.Interface.Windowing;

namespace TTSL.Windows;

public sealed class MainWindow : PositionedWindow, IDisposable
{
    private const int MaxMana = 10000;

    private readonly Plugin plugin;

    public MainWindow(Plugin plugin)
        : base($"{PluginInfo.DisplayName}##TTSLMain")
    {
        this.plugin = plugin;
        SizeConstraints = new WindowSizeConstraints
        {
            MinimumSize = new Vector2(540f, 360f),
            MaximumSize = new Vector2(2200f, 1800f),
        };
        Size = new Vector2(1480, 1040);
        SizeCondition = ImGuiCond.FirstUseEver;
        TitleBarButtons.Add(new()
        {
            Icon = FontAwesomeIcon.Cog, Priority = 0, IconOffset = new(2, 1),
            Click = button => { if (button == ImGuiMouseButton.Left) plugin.ToggleConfigUi(); },
            ShowTooltip = () => UiGui.SetTooltip("Settings"),
        });
        TitleBarButtons.Add(new()
        {
            Icon = FontAwesomeIcon.Wrench, Priority = -10, IconOffset = new(2, 1),
            Click = button => { if (button == ImGuiMouseButton.Left) plugin.OpenSetupWizard(); },
            ShowTooltip = () => UiGui.SetTooltip("Open the guided local/web HUD setup."),
        });
        TitleBarButtons.Add(new()
        {
            Icon = FontAwesomeIcon.Desktop, Priority = -20, IconOffset = new(2, 1),
            Click = button => { if (button == ImGuiMouseButton.Left) plugin.SetOverlayEnabled(!plugin.Configuration.OverlayEnabled, "main window"); },
            ShowTooltip = () => MaterialText.SetTooltip(UiText.T("HUD") + ": " + UiText.T(plugin.Configuration.OverlayEnabled ? "Enabled" : "Disabled")),
        });
    }

    public void Dispose()
    {
    }

    public override void Draw()
    {
        WindowMotion.DrawChrome();
        var cfg = plugin.Configuration;
        var version = Assembly.GetExecutingAssembly().GetName().Version?.ToString() ?? "0.0.0.0";
        var player = Plugin.ObjectTable.LocalPlayer;

        UiGui.TitleWithButtons(PluginInfo.DisplayName,PluginInfo.DisplayName+" "+version, this);

        DrawHeader(version);
        DrawToolbar(cfg);

        ImGui.Separator();

        if (player == null)
        {
            UiGui.TextDisabled("Local player is not available yet.");
            FinalizePendingWindowPlacement();
            return;
        }

        var snapshots = BuildPartySnapshots(player);

        var wide = ImGui.GetContentRegionAvail().X >= 900 * MaterialTheme.Metrics.Scale;
        if (ImGui.BeginTable("##TTSLMainLayout", wide ? 2 : 1, ImGuiTableFlags.SizingStretchProp | ImGuiTableFlags.NoSavedSettings))
        {
            ImGui.TableSetupColumn("Snapshot", ImGuiTableColumnFlags.WidthStretch, 1f);
            if(wide) ImGui.TableSetupColumn("Party", ImGuiTableColumnFlags.WidthStretch, 1f);
            ImGui.TableNextColumn();
            Panel(()=>DrawPlayerPanel(player),cfg.UiCompact?212:248);
            Panel(DrawRemoteHudPanel,cfg.UiCompact?286:330);
            if(cfg.ShowConditionPanel) Panel(DrawConditionPanel,cfg.UiCompact?104:128);
            if(cfg.ShowRepairSummary) Panel(DrawRepairPanel,cfg.UiCompact?104:126);
            if(wide) ImGui.TableNextColumn();
            if(cfg.ShowPartyStatus) Panel(()=>DrawPartyPanel(snapshots), (cfg.UiCompact?100:124)+(cfg.UiCompact?44:52)*Math.Max(1,snapshots.Count));
            if(cfg.ShowPartyRadar) Panel(()=>DrawRadarPanel(player,snapshots),cfg.UiCompact?360:440);
            ImGui.EndTable();
        }

        FinalizePendingWindowPlacement();
    }

    private void DrawHeader(string version)
    {
        var scale=MaterialTheme.Metrics.Scale; var start=ImGui.GetCursorScreenPos();
        TtslPresentation.Brand(start+new Vector2(0,8)*scale,44*scale);
        ImGui.SetCursorScreenPos(start+new Vector2(62,6)*scale);
        using(UiText.Font(plugin.Configuration.UiCompact?UiFontRole.CompactTitle:UiFontRole.Title)) MaterialText.Text(PluginInfo.DisplayName);
        ImGui.SameLine(); UiGui.TextDisabled("TTSL "+version);
        var right=ImGui.GetWindowPos().X+ImGui.GetWindowSize().X-ImGui.GetStyle().WindowPadding.X;
        var single=right-ImGui.GetItemRectMax().X>570*scale;
        ImGui.SetCursorScreenPos(single?new Vector2(right-570*scale,start.Y+12*scale):start+new Vector2(0,TtslPresentation.HeaderHeight)*scale);
        if (plugin.Configuration.UiCompactVisibleOnMainWindow)
        {
            var compact=plugin.Configuration.UiCompact;
            if(UiGui.Checkbox("C##CompactMode",ref compact)){plugin.Configuration.UiCompact=compact;plugin.SaveConfiguration();}
            if(ImGui.IsItemHovered()) UiGui.SetTooltip("Compact mode");
        }
        if (plugin.Configuration.UiLanguageVisibleOnMainWindow)
        { Flow("Language"); plugin.Appearance.DrawLanguageSelector(); }
        Flow("Transparency"); plugin.Appearance.DrawTransparencyToggle();
        Flow("Ko-fi"); if(UiGui.SmallButton("Ko-fi")) Process.Start(new ProcessStartInfo{FileName=PluginInfo.SupportUrl,UseShellExecute=true});
        Flow("Discord"); if(UiGui.SmallButton("Discord")) Process.Start(new ProcessStartInfo{FileName=PluginInfo.DiscordUrl,UseShellExecute=true});
        if(ImGui.IsItemHovered()) UiGui.SetTooltip(PluginInfo.DiscordFeedbackNote);
        ImGui.SetCursorScreenPos(start+new Vector2(0,(single?TtslPresentation.HeaderHeight:TtslPresentation.HeaderHeight+38))*scale);
        ImGui.Spacing();
    }

    private static void Flow(string label,bool action=false)
    {
        var labelWidth=MaterialText.Measure(UiText.T(label)).X;
        var needed=action?Math.Max(156*MaterialTheme.Metrics.Scale,labelWidth+76*MaterialTheme.Metrics.Scale):labelWidth+ImGui.GetFrameHeight()+32*MaterialTheme.Metrics.Scale;
        if(ImGui.GetItemRectMax().X+needed<ImGui.GetWindowPos().X+ImGui.GetWindowSize().X-ImGui.GetStyle().WindowPadding.X) ImGui.SameLine();
    }

    private static void Panel(Action draw,float logicalHeight)
    {
        var s=MaterialTheme.Metrics.Scale;var start=ImGui.GetCursorScreenPos();var width=ImGui.GetContentRegionAvail().X;
        var height=logicalHeight*s;
        var dl=ImGui.GetWindowDrawList();
        dl.ChannelsSplit(2);dl.ChannelsSetCurrent(1);
        ImGui.SetCursorScreenPos(start+new Vector2(16,12)*s);
        var window=ImGuiP.GetCurrentWindow();
        var previousWorkRect=window.WorkRect;var previousContentRect=window.ContentRegionRect;
        var innerRight=Math.Max(start.X+16*s,start.X+width-16*s);
        var innerWorkRect=previousWorkRect;innerWorkRect.Max.X=Math.Min(innerWorkRect.Max.X,innerRight);
        var innerContentRect=previousContentRect;innerContentRect.Max.X=Math.Min(innerContentRect.Max.X,innerRight);
        window.WorkRect=innerWorkRect;window.ContentRegionRect=innerContentRect;
        ImGui.PushClipRect(new Vector2(start.X+16*s,window.ClipRect.Min.Y),new Vector2(innerRight,window.ClipRect.Max.Y),true);
        ImGui.BeginGroup();
        ImGuiP.PushOverrideID(ImGuiP.GetCurrentWindow().ID);
        try { draw(); }
        finally
        {
            ImGui.PopID();ImGui.EndGroup();ImGui.PopClipRect();
            window.WorkRect=previousWorkRect;window.ContentRegionRect=previousContentRect;
        }
        var bottom=Math.Max(start.Y+height,ImGui.GetItemRectMax().Y+12*s);
        dl.ChannelsSetCurrent(0);TtslPresentation.Surface(start,new Vector2(start.X+width,bottom));dl.ChannelsMerge();
        ImGui.SetCursorScreenPos(new Vector2(start.X,bottom));ImGui.Dummy(new Vector2(width, TtslPresentation.Gap*s));
    }

    private void DrawToolbar(Configuration cfg)
    {
        using var actionFont=UiText.Font(UiFontRole.Action);
        using var actions=MaterialControls.Push(TtslPresentation.Controls());
        var enabled = cfg.OverlayEnabled;
        if (UiGui.ToggleAction("HUD", ref enabled, MaterialIcon.Monitor))
            plugin.SetOverlayEnabled(enabled, "main window");

        Flow("DTR",true);
        var dtrEnabled = cfg.DtrBarEnabled;
        if (UiGui.ToggleAction("DTR", ref dtrEnabled, MaterialIcon.Chart))
        {
            cfg.DtrBarEnabled = dtrEnabled;
            plugin.SaveConfiguration();
        }
        if (ImGui.IsItemHovered())
            UiGui.SetTooltip("Show TTSL status in the server info bar.");

        Flow("Krangle",true);
        var krangleEnabled = cfg.KrangleEnabled;
        if (UiGui.ToggleAction("Krangle", ref krangleEnabled, MaterialIcon.Refresh))
            plugin.SetKrangleEnabled(krangleEnabled, "main window");
        if (ImGui.IsItemHovered())
            UiGui.SetTooltip("Obfuscate displayed player names for screenshots.");

        Flow("Enumerate",true);
        var enumeratePartyMembers = cfg.EnumeratePartyMembers;
        if (UiGui.ToggleAction("Enumerate", ref enumeratePartyMembers, MaterialIcon.Table))
        {
            cfg.EnumeratePartyMembers = enumeratePartyMembers;
            plugin.SaveConfiguration();
        }
        if (ImGui.IsItemHovered())
            UiGui.SetTooltip("Use party slot numbers on the radar.");

        Flow("Setup",true);
        if (UiGui.Button("Setup",new Vector2(Math.Max(150,MaterialText.Measure(UiText.T("Setup")).X/MaterialTheme.Metrics.Scale+60)*MaterialTheme.Metrics.Scale,
            MaterialControlMetrics.Measure(MaterialTheme.Metrics, ImGui.GetTextLineHeight(), MaterialControlContext.Toolbar).Height)))
            plugin.OpenSetupWizard();
        if (ImGui.IsItemHovered())
            UiGui.SetTooltip("Open the guided local/web HUD setup.");

        Flow("Settings",true);
        if (UiGui.Button("Settings",new Vector2(Math.Max(150,MaterialText.Measure(UiText.T("Settings")).X/MaterialTheme.Metrics.Scale+60)*MaterialTheme.Metrics.Scale,
            MaterialControlMetrics.Measure(MaterialTheme.Metrics, ImGui.GetTextLineHeight(), MaterialControlContext.Toolbar).Height)))
            plugin.ToggleConfigUi();

        Flow("Settings");
        UiGui.TextDisabled("/ttsl ws | /ttsl j");
    }

    private void DrawPlayerPanel(ICharacter player)
    {
        Heading("Snapshot");
        using var metricRows = MetricRows();
        if(ImGui.BeginTable("##TTSLCoreMetrics",2,ImGuiTableFlags.SizingStretchProp|ImGuiTableFlags.NoSavedSettings))
        {
            MetricColumns("Character", "Area", "Position (X, Y, Z)", "HP", "MP", "Party Size");
            Pair("Character",plugin.GetDisplayName(player.Name.TextValue));
            var territoryId = Plugin.ClientState.TerritoryType;
            Pair("Area", AreaDisplay(plugin.GetResolvedTerritoryName(territoryId), territoryId));
            Pair("Position (X, Y, Z)",string.Format(UiText.Current.Culture,"{0:F1}, {1:F1}, {2:F1}",player.Position.X,player.Position.Y,player.Position.Z));
            ImGui.TableNextColumn();UiGui.Text("HP");ImGui.TableNextColumn();
            ImGui.ProgressBar(player.MaxHp>0?player.CurrentHp/(float)player.MaxHp:0,new Vector2(-1,18*MaterialTheme.Metrics.Scale),GetPercentText(player.CurrentHp,player.MaxHp));
            ImGui.TableNextColumn();UiGui.Text("MP");ImGui.TableNextColumn();
            ImGui.PushStyleColor(ImGuiCol.PlotHistogram,new Vector4(.25f,.8f,.42f,1));
            ImGui.ProgressBar(player.CurrentMp/(float)MaxMana,new Vector2(-1,18*MaterialTheme.Metrics.Scale),string.Format(UiText.Current.Culture,"{0:N0} / {1:N0}",player.CurrentMp,MaxMana));ImGui.PopStyleColor();
            Pair("Party Size",Plugin.PartyList.Length.ToString(UiText.Current.Culture));ImGui.EndTable();
        }
    }
    private static void Heading(string text)
    {
        var s=MaterialTheme.Metrics.Scale;var p=ImGui.GetCursorScreenPos();var ink=MaterialTheme.Current.Colors.OnSurface;
        var icon=text switch { "Snapshot" or "Party"=>MaterialIcon.Person, "Conditions"=>MaterialIcon.Document, "Equipment"=>MaterialIcon.Settings, _=>MaterialIcon.Link };
        if(text=="Party Radar")
        {
            var dl=ImGui.GetWindowDrawList();var centre=p+new Vector2(12,12)*s;var color=MaterialCanvas.Color(ink);
            dl.AddCircle(centre,10*s,color,24,2*s);dl.AddCircle(centre,5*s,color,20,2*s);
            dl.AddLine(centre-new Vector2(14,0)*s,centre+new Vector2(14,0)*s,color,s);
            dl.AddLine(centre-new Vector2(0,14)*s,centre+new Vector2(0,14)*s,color,s);
        }
        else MaterialIcons.Draw(icon,p,24*s,text=="Remote HUD"?MaterialTheme.Current.Colors.Primary:ink);
        ImGui.SetCursorScreenPos(p+new Vector2(36*s,0));
        using(UiText.Font(UiFontRole.PluginName)) UiGui.Text(text);ImGui.Separator();
    }
    private static void Pair(string label,string value){ImGui.TableNextColumn();UiGui.Text(label);ImGui.TableNextColumn();MaterialText.Text(value);}

    private static string AreaDisplay(string? resolvedName, uint territoryId)
        => resolvedName ?? UiText.T("Area") + " " + territoryId.ToString(UiText.Current.Culture);

    internal static string CurrentAccountDisplay(string account)
        => account == "Unavailable" ? UiText.T("Unavailable") : account;

    private static MaterialStyleScope MetricRows()
    {
        var scope = new MaterialStyleScope();
        scope.Style(ImGuiStyleVar.CellPadding, new Vector2(ImGui.GetStyle().CellPadding.X, (TtslPresentation.Compact ? 2 : 3) * MaterialTheme.Metrics.Scale));
        return scope;
    }

    private static void MetricColumns(params string[] labels)
    {
        var labelMinimum = labels.Max(label => MaterialText.Measure(UiText.T(label)).X);
        var preferred = ImGui.GetContentRegionAvail().X * .34f;
        ImGui.TableSetupColumn("##label", ImGuiTableColumnFlags.WidthFixed, Math.Max(preferred, MathF.Ceiling(labelMinimum)));
        ImGui.TableSetupColumn("##value", ImGuiTableColumnFlags.WidthStretch);
    }

    private void DrawRemoteHudPanel()
    {
        var cfg = plugin.Configuration;
        var publisher = plugin.RemoteHudPublisher;

        Heading("Remote HUD");

        using var metricRows = MetricRows();
        if (ImGui.BeginTable("##TTSLRemoteMetrics", 2, ImGuiTableFlags.SizingStretchProp | ImGuiTableFlags.NoSavedSettings))
        {
            MetricColumns("State", "Update Cadence", "Server", "Client", "Account", "Web Text", "Shot", "CCTV", "FB", "Last OK");
            Pair("State", UiText.T(GetRemoteStateText(cfg, publisher)));
            Pair("Update Cadence", string.Format(UiText.Current.Culture,"{0:N0} ms / {1:N0} ms",Math.Max(100,cfg.RemotePositionIntervalMs),Math.Max(500,cfg.RemoteFullSnapshotIntervalMs)));
            Pair("Server", cfg.RemoteServerUrl);
            Pair("Client", publisher.LastCharacterKey == null ? UiText.T("Waiting") : plugin.GetDisplayName(publisher.LastCharacterKey));
            Pair("Account", publisher.LastAccountId ?? CurrentAccountDisplay(plugin.GetCurrentAccountId()));
            Pair("Web Text",UiText.T(cfg.AllowWebEchoCommands?"On":"Off"));
            Pair("Shot",UiText.T(cfg.AllowWebScreenshotRequests?"On":"Off"));
            Pair("CCTV",UiText.T(cfg.AllowWebCctvStreaming?"On":"Off"));
            Pair("FB",UiText.T(cfg.EnablePluginFullBodyFallback?"On":"Off"));
            Pair("Last OK", publisher.LastSuccessUtc.HasValue
                ? publisher.LastSuccessUtc.Value.ToLocalTime().ToString("T",UiText.Current.Culture)
                : UiText.T("None"));
            ImGui.EndTable();
        }

        if (UiGui.SmallButton("Open Web HUD"))
            plugin.OpenRemoteViewer();
        if (ImGui.IsItemHovered())
            UiGui.SetTooltip("Open the Python remote HUD in your default browser.");

        if (!string.IsNullOrWhiteSpace(publisher.LastError))
            UiGui.TextColored(new Vector4(1f, 0.55f, 0.4f, 1f), $"Last error: {publisher.LastError}");
    }

    private void DrawConditionPanel()
    {
        Heading("Conditions");
        if (ImGui.BeginTable("##TTSLConditions", 3, ImGuiTableFlags.SizingStretchSame | ImGuiTableFlags.NoSavedSettings))
        {
            DrawConditionCell("Combat", Plugin.Condition[ConditionFlag.InCombat]);
            DrawConditionCell("Duty", Plugin.Condition[ConditionFlag.BoundByDuty] || Plugin.Condition[ConditionFlag.BoundByDuty56]);
            DrawConditionCell("Queue", Plugin.Condition[ConditionFlag.WaitingForDutyFinder]);
            DrawConditionCell("Mount", Plugin.Condition[ConditionFlag.Mounted]);
            DrawConditionCell("Cast", Plugin.Condition[ConditionFlag.Casting]);
            DrawConditionCell("Dead", Plugin.Condition[ConditionFlag.Unconscious]);
            ImGui.EndTable();
        }
    }

    private void DrawRepairPanel()
    {
        var summary = plugin.GetRepairSummary();
        Heading("Equipment");
        if (!summary.MinCondition.HasValue)
        {
            UiGui.TextDisabled("Durability unavailable.");
            return;
        }

        using var metricRows = MetricRows();
        if (ImGui.BeginTable("##TTSLRepair", 2, ImGuiTableFlags.SizingStretchProp | ImGuiTableFlags.NoSavedSettings))
        {
            MetricColumns("Min", "Avg", "Slots");
            Pair("Min", summary.MinCondition.Value.ToString("0",UiText.Current.Culture)+"%");
            Pair("Avg", summary.AverageCondition.ToString("0",UiText.Current.Culture)+"%");
            Pair("Slots", summary.EquippedCount.ToString(UiText.Current.Culture));
            ImGui.EndTable();
        }
    }

    private static void DrawPartyPanel(IReadOnlyList<PartySnapshot> snapshots)
    {
        Heading("Party");

        if (snapshots.Count == 0)
        {
            UiGui.TextDisabled("No party members detected.");
            return;
        }

        if (ImGui.BeginTable("##TTSLPartyTable", 5, ImGuiTableFlags.SizingStretchProp | ImGuiTableFlags.RowBg | ImGuiTableFlags.NoSavedSettings))
        {
            ImGui.TableSetupColumn("#", ImGuiTableColumnFlags.WidthFixed, 24f);
            ImGui.TableSetupColumn("Name", ImGuiTableColumnFlags.WidthStretch, 1.7f);
            ImGui.TableSetupColumn("Job", ImGuiTableColumnFlags.WidthFixed, 100f*MaterialTheme.Metrics.Scale);
            ImGui.TableSetupColumn("HP", ImGuiTableColumnFlags.WidthFixed, 100f*MaterialTheme.Metrics.Scale);
            ImGui.TableSetupColumn("Dist", ImGuiTableColumnFlags.WidthFixed, 120f*MaterialTheme.Metrics.Scale);
            UiGui.TableHeadersRow((TtslPresentation.Compact?40:48)*MaterialTheme.Metrics.Scale);

            foreach (var snapshot in snapshots)
            {
                ImGui.TableNextRow(ImGuiTableRowFlags.None,(TtslPresentation.Compact?44:52)*MaterialTheme.Metrics.Scale);

                ImGui.TableSetColumnIndex(0);
                UiGui.TextUnformatted(snapshot.SlotText);

                ImGui.TableSetColumnIndex(1);
                MaterialText.Text(snapshot.DisplayName);

                ImGui.TableSetColumnIndex(2);
                DrawJob(snapshot.Job);

                ImGui.TableSetColumnIndex(3);
                UiGui.TextUnformatted(snapshot.HpText);

                ImGui.TableSetColumnIndex(4);
                UiGui.TextUnformatted(snapshot.DistanceText);
            }

            ImGui.EndTable();
        }
    }

    private static void DrawJob(string job)
    {
        var s=MaterialTheme.Metrics.Scale;var p=ImGui.GetCursorScreenPos();var dl=ImGui.GetWindowDrawList();
        if(job is "GLA" or "MRD" or "PLD" or "WAR" or "DRK" or "GNB")
            MaterialIcons.Draw(MaterialIcon.Shield,p,22*s,new Vector4(.18f,.52f,1,1));
        else if(job is "CNJ" or "WHM" or "SCH" or "AST" or "SGE")
        {
            var color=MaterialCanvas.Color(new Vector4(.2f,.82f,.34f,1));
            dl.AddLine(p+new Vector2(11,1)*s,p+new Vector2(11,21)*s,color,5*s);
            dl.AddLine(p+new Vector2(1,11)*s,p+new Vector2(21,11)*s,color,5*s);
        }
        else if(job is "ARC" or "BRD" or "MCH" or "DNC")
        {
            var color=MaterialCanvas.Color(new Vector4(1,.75f,.19f,1));
            dl.PathLineTo(p+new Vector2(3,2)*s);dl.PathBezierCubicCurveTo(p+new Vector2(21,3)*s,p+new Vector2(21,19)*s,p+new Vector2(3,20)*s);dl.PathStroke(color,ImDrawFlags.None,2*s);
            dl.AddLine(p+new Vector2(3,2)*s,p+new Vector2(3,20)*s,color,s);dl.AddLine(p+new Vector2(1,18)*s,p+new Vector2(20,3)*s,color,2*s);
        }
        else if(job is "PGL" or "LNC" or "ROG" or "THM" or "ACN" or "MNK" or "DRG" or "NIN" or "SAM" or "RPR" or "VPR" or "BLM" or "SMN" or "RDM" or "PCT" or "BLU")
        {
            var color=MaterialCanvas.Color(new Vector4(1,.3f,.36f,1));
            dl.AddLine(p+new Vector2(3,19)*s,p+new Vector2(19,3)*s,color,4*s);dl.AddLine(p+new Vector2(3,12)*s,p+new Vector2(10,19)*s,color,2*s);
        }
        else { MaterialText.Text(job);return; }
        ImGui.SetCursorScreenPos(p+new Vector2(32*s,0));MaterialText.Text(job);
    }

    private void DrawRadarPanel(ICharacter localPlayer, IReadOnlyList<PartySnapshot> snapshots)
    {
        Heading("Party Radar");
        var inCombat = Plugin.Condition[ConditionFlag.InCombat];
        var spanWidth = MathF.Max(5f, inCombat
            ? plugin.Configuration.RadarCombatWidthYalms
            : plugin.Configuration.RadarOutOfCombatWidthYalms);
        var spanHeight = MathF.Max(5f, inCombat
            ? plugin.Configuration.RadarCombatHeightYalms
            : plugin.Configuration.RadarOutOfCombatHeightYalms);
        var radarMode = inCombat ? "combat" : "travel";
        UiGui.TextDisabled(UiText.F("Showing {0:F0}y x {1:F0}y ({2}).",spanWidth,spanHeight,UiText.T(radarMode)));

        var availableWidth = MathF.Max(140f, ImGui.GetContentRegionAvail().X);
        var desiredEdge = Math.Clamp(plugin.Configuration.RadarBoxSizePixels, 96f, 320f);
        var canvasEdge = MathF.Min(availableWidth, desiredEdge);
        var ratio=Math.Clamp(plugin.Configuration.RadarBoxSizePixels/160f,.6f,2f);
        var canvasSize = new Vector2(Math.Max(canvasEdge, availableWidth-32*MaterialTheme.Metrics.Scale), (plugin.Configuration.UiCompact ? 260 : 330)*ratio*MaterialTheme.Metrics.Scale);
        var drawList = ImGui.GetWindowDrawList();
        var topLeft = ImGui.GetCursorScreenPos();
        var bottomRight = topLeft + canvasSize;
        var center = topLeft + (canvasSize / 2f);
        var halfSpanWidth = spanWidth / 2f;
        var halfSpanHeight = spanHeight / 2f;
        var radius = canvasSize / 2f - new Vector2(16f);

        drawList.AddRectFilled(topLeft, bottomRight, MaterialCanvas.Color(MaterialTheme.Current.Colors.SurfaceContainerLowest), 6f);
        drawList.AddRect(topLeft, bottomRight, MaterialCanvas.Color(MaterialTheme.Current.Colors.OutlineVariant), 6f);
        drawList.AddLine(new Vector2(center.X, topLeft.Y + 6f), new Vector2(center.X, bottomRight.Y - 6f), MaterialCanvas.Color(MaterialTheme.Current.Colors.OutlineVariant));
        drawList.AddLine(new Vector2(topLeft.X + 6f, center.Y), new Vector2(bottomRight.X - 6f, center.Y), MaterialCanvas.Color(MaterialTheme.Current.Colors.OutlineVariant));
        var compass=MaterialCanvas.Color(MaterialTheme.Current.Colors.OnSurface);var north=MaterialText.Measure("N");var east=MaterialText.Measure("E");
        MaterialText.AddText(drawList,new Vector2(center.X-north.X*.5f,topLeft.Y+2),compass,"N");
        MaterialText.AddText(drawList,new Vector2(center.X-north.X*.5f,bottomRight.Y-north.Y-2),compass,"S");
        MaterialText.AddText(drawList,new Vector2(topLeft.X+2,center.Y-north.Y*.5f),compass,"W");
        MaterialText.AddText(drawList,new Vector2(bottomRight.X-east.X-2,center.Y-north.Y*.5f),compass,"E");
        drawList.AddCircle(center, 4f, ImGui.GetColorU32(new Vector4(0.4f, 1f, 0.5f, 1f)), 16, 2f);

        foreach (var snapshot in snapshots)
        {
            if (snapshot.Character == null || snapshot.Character.Address == localPlayer.Address)
                continue;

            var relative = snapshot.Character.Position - localPlayer.Position;
            var normalized = new Vector2(relative.X / halfSpanWidth, relative.Z / halfSpanHeight);
            normalized = Vector2.Clamp(normalized, new Vector2(-1f, -1f), new Vector2(1f, 1f));
            var dotPosition = center + new Vector2(normalized.X * radius.X, normalized.Y * radius.Y);

            drawList.AddCircleFilled(dotPosition, 4f, ImGui.GetColorU32(new Vector4(1f, 0.7f, 0.2f, 1f)));
            var labelSize = MaterialText.Measure(snapshot.RadarLabel);
            var labelMinimum = topLeft + new Vector2(4f);
            var labelMaximum = Vector2.Max(labelMinimum, bottomRight - labelSize - new Vector2(4f));
            var labelPosition = Vector2.Clamp(dotPosition + new Vector2(6f, -8f), labelMinimum, labelMaximum);
            drawList.PushClipRect(topLeft, bottomRight, true);
            MaterialText.AddText(drawList,labelPosition, MaterialCanvas.Color(MaterialTheme.Current.Colors.OnSurface), snapshot.RadarLabel);
            drawList.PopClipRect();
        }

        ImGui.Dummy(canvasSize);
    }

    private List<PartySnapshot> BuildPartySnapshots(ICharacter localPlayer)
    {
        var snapshots = new List<PartySnapshot>();

        for (var i = 0; i < Plugin.PartyList.Length; i++)
        {
            var member = Plugin.PartyList[i];
            if (member == null)
                continue;

            var originalName = member.Name.TextValue;
            if (string.IsNullOrWhiteSpace(originalName))
                continue;

            var foundCharacter = FindPartyCharacter(member.Address, originalName);
            var job = member.ClassJob.IsValid ? member.ClassJob.Value.Abbreviation.ToString() : "UNK";
            var slotNumber = i + 1;
            var displayName = plugin.GetDisplayName(originalName);
            var radarLabel = plugin.Configuration.EnumeratePartyMembers ? slotNumber.ToString() : displayName;
            var hpText = foundCharacter == null
                ? "off"
                : GetPercentText(foundCharacter.CurrentHp, foundCharacter.MaxHp);
            var distanceText = foundCharacter == null
                ? "--"
                : Vector3.Distance(localPlayer.Position, foundCharacter.Position).ToString("F1",UiText.Current.Culture)+"y";

            snapshots.Add(new PartySnapshot
            {
                Character = foundCharacter,
                SlotText = slotNumber.ToString(UiText.Current.Culture),
                DisplayName = displayName,
                Job = job,
                HpText = hpText,
                DistanceText = distanceText,
                RadarLabel = radarLabel,
            });
        }

        return snapshots;
    }

    private static void DrawMetricCell(string label, string value)
    {
        ImGui.TableNextColumn();
        UiGui.TextDisabled(label);
        UiGui.TextWrapped(value);
    }

    private static void DrawConditionCell(string label, bool active)
    {
        ImGui.TableNextColumn();
        UiGui.TextColored(active
            ? new Vector4(0.35f, 0.95f, 0.45f, 1f)
            : MaterialTheme.Current.Colors.OnSurfaceVariant, UiText.T(label)+"  "+UiText.T(active?"Yes":"No"));
    }

    private static string GetPercentText(long current, long max)
    {
        if (max <= 0)
            return "--";

        return ((current/(float)max)*100f).ToString("0",UiText.Current.Culture)+"%";
    }

    private static string GetRemoteStateText(Configuration cfg, Services.RemoteHudPublisherService publisher)
    {
        if (!cfg.RemoteServerEnabled)
            return "Disabled";

        if (!string.IsNullOrWhiteSpace(publisher.LastError))
            return "Publish failed";

        if (publisher.LastSuccessUtc.HasValue)
            return "Live";

        return publisher.StatusText;
    }

    private static ICharacter? FindPartyCharacter(nint memberAddress, string name)
    {
        if (memberAddress != 0)
        {
            var addressMatch = Plugin.ObjectTable
                .OfType<ICharacter>()
                .FirstOrDefault(obj => obj.Address == memberAddress);
            if (addressMatch != null)
                return addressMatch;
        }

        return Plugin.ObjectTable
            .OfType<ICharacter>()
            .FirstOrDefault(obj => string.Equals(obj.Name.TextValue, name, StringComparison.Ordinal));
    }

    private sealed class PartySnapshot
    {
        public ICharacter? Character { get; init; }
        public string SlotText { get; init; } = string.Empty;
        public string DisplayName { get; init; } = string.Empty;
        public string Job { get; init; } = string.Empty;
        public string HpText { get; init; } = string.Empty;
        public string DistanceText { get; init; } = string.Empty;
        public string RadarLabel { get; init; } = string.Empty;
    }
}
