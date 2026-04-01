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
            MinimumSize = new Vector2(640f, 460f),
            MaximumSize = new Vector2(1400f, 1100f),
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

        var enabled = cfg.OverlayEnabled;
        if (ImGui.Checkbox("HUD Enabled", ref enabled))
            plugin.SetOverlayEnabled(enabled, "main window");

        ImGui.SameLine();
        var dtrEnabled = cfg.DtrBarEnabled;
        if (ImGui.Checkbox("DTR Bar", ref dtrEnabled))
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
        if (ImGui.Checkbox("Enumerate Party", ref enumeratePartyMembers))
        {
            cfg.EnumeratePartyMembers = enumeratePartyMembers;
            plugin.SaveConfiguration();
        }
        if (ImGui.IsItemHovered())
            ImGui.SetTooltip("Show party slot numbers on the radar while keeping the numbered mapping in the party snapshot.");

        ImGui.SameLine();
        if (ImGui.SmallButton("Settings"))
            plugin.ToggleConfigUi();

        ImGui.SameLine();
        ImGui.TextDisabled("Use /ttsl ws and /ttsl j for window recovery.");

        ImGui.Separator();

        if (player == null)
        {
            ImGui.TextDisabled("Local player is not available yet.");
            FinalizePendingWindowPlacement();
            return;
        }

        var snapshots = BuildPartySnapshots(player);

        DrawPlayerPanel(player);

        if (cfg.ShowConditionPanel)
        {
            ImGui.Separator();
            DrawConditionPanel();
        }

        if (cfg.ShowRepairSummary)
        {
            ImGui.Separator();
            DrawRepairPanel();
        }

        if (cfg.ShowPartyStatus)
        {
            ImGui.Separator();
            DrawPartyPanel(snapshots);
        }

        if (cfg.ShowPartyRadar)
        {
            ImGui.Separator();
            DrawRadarPanel(player, snapshots);
        }

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

    private void DrawPlayerPanel(ICharacter player)
    {
        ImGui.TextColored(new Vector4(0.95f, 0.75f, 0.35f, 1f), "Core Snapshot");
        ImGui.Text($"Name: {plugin.GetDisplayName(player.Name.TextValue)}");
        ImGui.Text($"Zone: {plugin.GetTerritoryName(Plugin.ClientState.TerritoryType)} ({Plugin.ClientState.TerritoryType})");
        ImGui.Text($"Position: X {player.Position.X:F1} | Y {player.Position.Y:F1} | Z {player.Position.Z:F1}");

        var hpFraction = player.MaxHp > 0 ? player.CurrentHp / (float)player.MaxHp : 0f;
        ImGui.Text($"HP: {player.CurrentHp:N0} / {player.MaxHp:N0}");
        ImGui.ProgressBar(hpFraction, new Vector2(-1f, 0f), $"{hpFraction * 100f:0.0}%");

        ImGui.Text($"MP: {player.CurrentMp:N0} / {MaxMana:N0}");
        ImGui.ProgressBar(player.CurrentMp / (float)MaxMana, new Vector2(-1f, 0f), $"{(player.CurrentMp / (float)MaxMana) * 100f:0.0}%");
    }

    private void DrawConditionPanel()
    {
        ImGui.TextColored(new Vector4(0.55f, 0.85f, 1f, 1f), "Conditions");
        DrawStatusLine("In combat", Plugin.Condition[ConditionFlag.InCombat]);
        DrawStatusLine("Bound by duty", Plugin.Condition[ConditionFlag.BoundByDuty] || Plugin.Condition[ConditionFlag.BoundByDuty56]);
        DrawStatusLine("In duty queue", Plugin.Condition[ConditionFlag.WaitingForDutyFinder]);
        DrawStatusLine("Mounted", Plugin.Condition[ConditionFlag.Mounted]);
        DrawStatusLine("Casting", Plugin.Condition[ConditionFlag.Casting]);
        DrawStatusLine("Dead", Plugin.Condition[ConditionFlag.Unconscious]);
    }

    private void DrawRepairPanel()
    {
        var summary = plugin.GetRepairSummary();
        ImGui.TextColored(new Vector4(0.8f, 1f, 0.45f, 1f), "Equipment");
        if (!summary.MinCondition.HasValue)
        {
            ImGui.TextDisabled("Equipped durability summary is unavailable.");
            return;
        }

        var minFraction = summary.MinCondition.Value / 100f;
        ImGui.Text($"Lowest durability: {summary.MinCondition.Value}%");
        ImGui.ProgressBar(minFraction, new Vector2(-1f, 0f), $"{summary.MinCondition.Value}%");
        ImGui.Text($"Average durability: {summary.AverageCondition}% across {summary.EquippedCount} equipped items");
    }

    private static void DrawPartyPanel(IReadOnlyList<PartySnapshot> snapshots)
    {
        ImGui.TextColored(new Vector4(1f, 0.6f, 0.8f, 1f), "Party Snapshot");

        if (snapshots.Count == 0)
        {
            ImGui.TextDisabled("No party members detected.");
            return;
        }

        foreach (var snapshot in snapshots)
        {
            ImGui.Bullet();
            ImGui.SameLine();
            ImGui.TextWrapped(snapshot.LineText);
        }
    }

    private void DrawRadarPanel(ICharacter localPlayer, IReadOnlyList<PartySnapshot> snapshots)
    {
        ImGui.TextColored(new Vector4(0.85f, 0.8f, 1f, 1f), "Party Radar");
        ImGui.TextDisabled(plugin.Configuration.EnumeratePartyMembers
            ? "Radar labels use party slot numbers. Match them to the numbered party snapshot above."
            : "Radar labels use party names from the live object table.");

        var canvasSize = new Vector2(MathF.Min(320f, ImGui.GetContentRegionAvail().X), 320f);
        var drawList = ImGui.GetWindowDrawList();
        var topLeft = ImGui.GetCursorScreenPos();
        var bottomRight = topLeft + canvasSize;
        var center = topLeft + (canvasSize / 2f);
        var scale = MathF.Max(5f, plugin.Configuration.RadarScaleYalms);
        var radius = (canvasSize.X / 2f) - 14f;

        drawList.AddRectFilled(topLeft, bottomRight, ImGui.GetColorU32(new Vector4(0.08f, 0.08f, 0.11f, 1f)), 6f);
        drawList.AddRect(topLeft, bottomRight, ImGui.GetColorU32(new Vector4(0.35f, 0.35f, 0.45f, 1f)), 6f);
        drawList.AddLine(new Vector2(center.X, topLeft.Y + 8f), new Vector2(center.X, bottomRight.Y - 8f), ImGui.GetColorU32(new Vector4(0.3f, 0.3f, 0.4f, 1f)));
        drawList.AddLine(new Vector2(topLeft.X + 8f, center.Y), new Vector2(bottomRight.X - 8f, center.Y), ImGui.GetColorU32(new Vector4(0.3f, 0.3f, 0.4f, 1f)));
        drawList.AddCircle(center, 4f, ImGui.GetColorU32(new Vector4(0.4f, 1f, 0.5f, 1f)), 16, 3f);

        foreach (var snapshot in snapshots)
        {
            if (snapshot.Character == null || snapshot.Character.Address == localPlayer.Address)
                continue;

            var relative = snapshot.Character.Position - localPlayer.Position;
            var normalized = new Vector2(relative.X / scale, relative.Z / scale);
            normalized = Vector2.Clamp(normalized, new Vector2(-1f, -1f), new Vector2(1f, 1f));
            var dotPosition = center + new Vector2(normalized.X * radius, normalized.Y * radius);

            drawList.AddCircleFilled(dotPosition, 5f, ImGui.GetColorU32(new Vector4(1f, 0.7f, 0.2f, 1f)));
            drawList.AddText(dotPosition + new Vector2(7f, -8f), ImGui.GetColorU32(new Vector4(1f, 1f, 1f, 1f)), snapshot.RadarLabel);
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
            var radarLabel = plugin.Configuration.EnumeratePartyMembers ? $"[{slotNumber}]" : displayName;

            var hpText = foundCharacter == null
                ? "off-table"
                : $"{foundCharacter.CurrentHp:N0}/{foundCharacter.MaxHp:N0}";
            var manaText = foundCharacter == null
                ? "off-table"
                : $"{foundCharacter.CurrentMp:N0}/{MaxMana:N0}";
            var positionText = foundCharacter == null
                ? "off-table"
                : $"{foundCharacter.Position.X:F1}, {foundCharacter.Position.Y:F1}, {foundCharacter.Position.Z:F1}";
            var distanceText = foundCharacter == null
                ? "off-table"
                : $"{Vector3.Distance(localPlayer.Position, foundCharacter.Position):F1}y";

            snapshots.Add(new PartySnapshot
            {
                Character = foundCharacter,
                LineText = $"[{slotNumber}] {displayName} [{job}], HP {hpText}, Mana {manaText}, XYZ {positionText}, Distance {distanceText}",
                RadarLabel = radarLabel,
            });
        }

        return snapshots;
    }

    private static void DrawStatusLine(string label, bool active)
    {
        var color = active
            ? new Vector4(0.25f, 0.95f, 0.45f, 1f)
            : new Vector4(0.95f, 0.35f, 0.35f, 1f);
        ImGui.TextColored(color, label);
        ImGui.SameLine();
        ImGui.TextDisabled(active ? "active" : "inactive");
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
        public string LineText { get; init; } = string.Empty;
        public string RadarLabel { get; init; } = string.Empty;
    }
}
