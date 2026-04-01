using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Numerics;
using System.Reflection;
using Dalamud.Bindings.ImGui;
using Dalamud.Game.ClientState.Conditions;
using Dalamud.Game.ClientState.Objects.Types;
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
            MinimumSize = new Vector2(430f, 280f),
            MaximumSize = new Vector2(1100f, 820f),
        };
    }

    public void Dispose()
    {
    }

    public override void Draw()
    {
        var cfg = plugin.Configuration;
        var version = Assembly.GetExecutingAssembly().GetName().Version?.ToString() ?? "0.0.0.0";
        var player = Plugin.ObjectTable.LocalPlayer;

        DrawHeader(version);
        DrawToolbar(cfg);

        ImGui.Separator();

        if (player == null)
        {
            ImGui.TextDisabled("Local player is not available yet.");
            FinalizePendingWindowPlacement();
            return;
        }

        var snapshots = BuildPartySnapshots(player);

        ImGui.PushStyleVar(ImGuiStyleVar.ItemSpacing, new Vector2(4f, 2f));
        ImGui.PushStyleVar(ImGuiStyleVar.FramePadding, new Vector2(3f, 2f));
        ImGui.PushStyleVar(ImGuiStyleVar.CellPadding, new Vector2(3f, 2f));

        if (ImGui.BeginTable("##TTSLMainLayout", 2, ImGuiTableFlags.SizingStretchProp | ImGuiTableFlags.NoSavedSettings))
        {
            ImGui.TableSetupColumn("Snapshot", ImGuiTableColumnFlags.WidthStretch, 1.12f);
            ImGui.TableSetupColumn("Party", ImGuiTableColumnFlags.WidthStretch, 0.88f);

            ImGui.TableNextColumn();
            DrawPlayerPanel(player);
            DrawRemoteHudPanel();

            if (cfg.ShowConditionPanel)
                DrawConditionPanel();

            if (cfg.ShowRepairSummary)
                DrawRepairPanel();

            ImGui.TableNextColumn();

            if (cfg.ShowPartyStatus)
                DrawPartyPanel(snapshots);

            if (cfg.ShowPartyRadar)
                DrawRadarPanel(player, snapshots);

            ImGui.EndTable();
        }

        ImGui.PopStyleVar(3);

        FinalizePendingWindowPlacement();
    }

    private void DrawHeader(string version)
    {
        var discordWidth = ImGui.CalcTextSize("Discord").X + (ImGui.GetStyle().FramePadding.X * 2f);
        ImGui.Text($"{PluginInfo.DisplayName} v{version}");
        ImGui.SameLine(ImGui.GetWindowWidth() - (120f + discordWidth));
        if (ImGui.SmallButton("Ko-fi"))
            Process.Start(new ProcessStartInfo { FileName = PluginInfo.SupportUrl, UseShellExecute = true });
        ImGui.SameLine();
        if (ImGui.SmallButton("Discord"))
            Process.Start(new ProcessStartInfo { FileName = PluginInfo.DiscordUrl, UseShellExecute = true });
        if (ImGui.IsItemHovered())
            ImGui.SetTooltip(PluginInfo.DiscordFeedbackNote);
    }

    private void DrawToolbar(Configuration cfg)
    {
        var enabled = cfg.OverlayEnabled;
        if (ImGui.Checkbox("HUD", ref enabled))
            plugin.SetOverlayEnabled(enabled, "main window");

        ImGui.SameLine();
        var dtrEnabled = cfg.DtrBarEnabled;
        if (ImGui.Checkbox("DTR", ref dtrEnabled))
        {
            cfg.DtrBarEnabled = dtrEnabled;
            plugin.SaveConfiguration();
        }
        if (ImGui.IsItemHovered())
            ImGui.SetTooltip("Show TTSL status in the server info bar.");

        ImGui.SameLine();
        var krangleEnabled = cfg.KrangleEnabled;
        if (ImGui.Checkbox("Krangle", ref krangleEnabled))
            plugin.SetKrangleEnabled(krangleEnabled, "main window");
        if (ImGui.IsItemHovered())
            ImGui.SetTooltip("Obfuscate displayed player names for screenshots.");

        ImGui.SameLine();
        var enumeratePartyMembers = cfg.EnumeratePartyMembers;
        if (ImGui.Checkbox("Enumerate", ref enumeratePartyMembers))
        {
            cfg.EnumeratePartyMembers = enumeratePartyMembers;
            plugin.SaveConfiguration();
        }
        if (ImGui.IsItemHovered())
            ImGui.SetTooltip("Use party slot numbers on the radar.");

        ImGui.SameLine();
        if (ImGui.SmallButton("Settings"))
            plugin.ToggleConfigUi();

        ImGui.SameLine();
        ImGui.TextDisabled("/ttsl ws | /ttsl j");
    }

    private void DrawPlayerPanel(ICharacter player)
    {
        ImGui.TextColored(new Vector4(0.95f, 0.75f, 0.35f, 1f), "Snapshot");
        ImGui.Text(plugin.GetDisplayName(player.Name.TextValue));
        ImGui.TextDisabled($"{plugin.GetTerritoryName(Plugin.ClientState.TerritoryType)} ({Plugin.ClientState.TerritoryType})");

        if (ImGui.BeginTable("##TTSLCoreMetrics", 2, ImGuiTableFlags.SizingStretchProp | ImGuiTableFlags.NoSavedSettings))
        {
            DrawMetricCell("Position", $"X {player.Position.X:F1} | Y {player.Position.Y:F1} | Z {player.Position.Z:F1}");
            DrawMetricCell("HP", $"{player.CurrentHp:N0} / {player.MaxHp:N0} ({GetPercentText(player.CurrentHp, player.MaxHp)})");
            DrawMetricCell("MP", $"{player.CurrentMp:N0} / {MaxMana:N0} ({GetPercentText(player.CurrentMp, MaxMana)})");
            DrawMetricCell("Party", $"{Plugin.PartyList.Length} member(s) visible");
            ImGui.EndTable();
        }
    }

    private void DrawRemoteHudPanel()
    {
        var cfg = plugin.Configuration;
        var publisher = plugin.RemoteHudPublisher;
        var remoteHealthy = cfg.RemoteServerEnabled && string.IsNullOrWhiteSpace(publisher.LastError) && publisher.LastSuccessUtc.HasValue;

        ImGui.TextColored(remoteHealthy
            ? new Vector4(0.35f, 0.95f, 0.55f, 1f)
            : new Vector4(0.8f, 0.8f, 0.8f, 1f), "Remote HUD");

        if (ImGui.BeginTable("##TTSLRemoteMetrics", 2, ImGuiTableFlags.SizingStretchProp | ImGuiTableFlags.NoSavedSettings))
        {
            DrawMetricCell("State", GetRemoteStateText(cfg, publisher));
            DrawMetricCell("Cadence", $"{Math.Max(100, cfg.RemotePositionIntervalMs)} ms | {Math.Max(500, cfg.RemoteFullSnapshotIntervalMs)} ms");
            DrawMetricCell("Server", cfg.RemoteServerUrl);
            DrawMetricCell("Client", publisher.LastCharacterKey == null ? "Waiting" : plugin.GetDisplayName(publisher.LastCharacterKey));
            DrawMetricCell("Account", publisher.LastAccountId ?? plugin.GetCurrentAccountId());
            DrawMetricCell("Last OK", publisher.LastSuccessUtc.HasValue
                ? publisher.LastSuccessUtc.Value.ToLocalTime().ToString("HH:mm:ss")
                : "None");
            ImGui.EndTable();
        }

        if (!string.IsNullOrWhiteSpace(publisher.LastError))
            ImGui.TextColored(new Vector4(1f, 0.55f, 0.4f, 1f), $"Last error: {publisher.LastError}");
    }

    private void DrawConditionPanel()
    {
        ImGui.TextColored(new Vector4(0.55f, 0.85f, 1f, 1f), "Conditions");
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
        ImGui.TextColored(new Vector4(0.8f, 1f, 0.45f, 1f), "Equipment");
        if (!summary.MinCondition.HasValue)
        {
            ImGui.TextDisabled("Durability unavailable.");
            return;
        }

        if (ImGui.BeginTable("##TTSLRepair", 3, ImGuiTableFlags.SizingStretchSame | ImGuiTableFlags.NoSavedSettings))
        {
            DrawMetricCell("Min", $"{summary.MinCondition.Value}%");
            DrawMetricCell("Avg", $"{summary.AverageCondition}%");
            DrawMetricCell("Slots", summary.EquippedCount.ToString());
            ImGui.EndTable();
        }
    }

    private static void DrawPartyPanel(IReadOnlyList<PartySnapshot> snapshots)
    {
        ImGui.TextColored(new Vector4(1f, 0.6f, 0.8f, 1f), "Party");

        if (snapshots.Count == 0)
        {
            ImGui.TextDisabled("No party members detected.");
            return;
        }

        if (ImGui.BeginTable("##TTSLPartyTable", 5, ImGuiTableFlags.SizingStretchProp | ImGuiTableFlags.RowBg | ImGuiTableFlags.NoSavedSettings))
        {
            ImGui.TableSetupColumn("#", ImGuiTableColumnFlags.WidthFixed, 24f);
            ImGui.TableSetupColumn("Name", ImGuiTableColumnFlags.WidthStretch, 1.7f);
            ImGui.TableSetupColumn("Job", ImGuiTableColumnFlags.WidthFixed, 42f);
            ImGui.TableSetupColumn("HP", ImGuiTableColumnFlags.WidthFixed, 58f);
            ImGui.TableSetupColumn("Dist", ImGuiTableColumnFlags.WidthFixed, 48f);

            foreach (var snapshot in snapshots)
            {
                ImGui.TableNextRow();

                ImGui.TableSetColumnIndex(0);
                ImGui.TextUnformatted(snapshot.SlotText);

                ImGui.TableSetColumnIndex(1);
                ImGui.TextUnformatted(snapshot.DisplayName);

                ImGui.TableSetColumnIndex(2);
                ImGui.TextUnformatted(snapshot.Job);

                ImGui.TableSetColumnIndex(3);
                ImGui.TextUnformatted(snapshot.HpText);

                ImGui.TableSetColumnIndex(4);
                ImGui.TextUnformatted(snapshot.DistanceText);
            }

            ImGui.EndTable();
        }
    }

    private void DrawRadarPanel(ICharacter localPlayer, IReadOnlyList<PartySnapshot> snapshots)
    {
        ImGui.TextColored(new Vector4(0.85f, 0.8f, 1f, 1f), "Radar");
        ImGui.TextDisabled(plugin.Configuration.EnumeratePartyMembers ? "Labels use party slots." : "Labels use party names.");

        var availableWidth = MathF.Max(140f, ImGui.GetContentRegionAvail().X);
        var canvasEdge = MathF.Min(availableWidth, 156f);
        var canvasSize = new Vector2(canvasEdge, canvasEdge);
        var drawList = ImGui.GetWindowDrawList();
        var topLeft = ImGui.GetCursorScreenPos();
        var bottomRight = topLeft + canvasSize;
        var center = topLeft + (canvasSize / 2f);
        var scale = MathF.Max(5f, plugin.Configuration.RadarScaleYalms);
        var radius = (canvasSize.X / 2f) - 12f;

        drawList.AddRectFilled(topLeft, bottomRight, ImGui.GetColorU32(new Vector4(0.08f, 0.08f, 0.11f, 1f)), 6f);
        drawList.AddRect(topLeft, bottomRight, ImGui.GetColorU32(new Vector4(0.35f, 0.35f, 0.45f, 1f)), 6f);
        drawList.AddLine(new Vector2(center.X, topLeft.Y + 6f), new Vector2(center.X, bottomRight.Y - 6f), ImGui.GetColorU32(new Vector4(0.3f, 0.3f, 0.4f, 1f)));
        drawList.AddLine(new Vector2(topLeft.X + 6f, center.Y), new Vector2(bottomRight.X - 6f, center.Y), ImGui.GetColorU32(new Vector4(0.3f, 0.3f, 0.4f, 1f)));
        drawList.AddCircle(center, 4f, ImGui.GetColorU32(new Vector4(0.4f, 1f, 0.5f, 1f)), 16, 2f);

        foreach (var snapshot in snapshots)
        {
            if (snapshot.Character == null || snapshot.Character.Address == localPlayer.Address)
                continue;

            var relative = snapshot.Character.Position - localPlayer.Position;
            var normalized = new Vector2(relative.X / scale, relative.Z / scale);
            normalized = Vector2.Clamp(normalized, new Vector2(-1f, -1f), new Vector2(1f, 1f));
            var dotPosition = center + new Vector2(normalized.X * radius, normalized.Y * radius);

            drawList.AddCircleFilled(dotPosition, 4f, ImGui.GetColorU32(new Vector4(1f, 0.7f, 0.2f, 1f)));
            drawList.AddText(dotPosition + new Vector2(6f, -8f), ImGui.GetColorU32(new Vector4(1f, 1f, 1f, 1f)), snapshot.RadarLabel);
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
                : $"{Vector3.Distance(localPlayer.Position, foundCharacter.Position):F1}y";

            snapshots.Add(new PartySnapshot
            {
                Character = foundCharacter,
                SlotText = slotNumber.ToString(),
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
        ImGui.TextDisabled(label);
        ImGui.TextWrapped(value);
    }

    private static void DrawConditionCell(string label, bool active)
    {
        ImGui.TableNextColumn();
        ImGui.TextColored(active
            ? new Vector4(0.35f, 0.95f, 0.45f, 1f)
            : new Vector4(0.42f, 0.46f, 0.52f, 1f), label);
    }

    private static string GetPercentText(long current, long max)
    {
        if (max <= 0)
            return "--";

        return $"{(current / (float)max) * 100f:0}%";
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
