using System;
using System.Collections.Generic;
using Dalamud.Configuration;

namespace TTSL;

[Serializable]
public sealed class Configuration : IPluginConfiguration
{
    public int Version { get; set; } = 5;
    public string LastAccountId { get; set; } = string.Empty;
    public Dictionary<string, AccountScopedConfiguration> Accounts { get; set; } = new();
    public bool OverlayEnabled { get; set; } = true;
    public bool DtrBarEnabled { get; set; } = true;
    public int DtrBarMode { get; set; } = 0;
    public string DtrIconEnabled { get; set; } = "\uE0BB";
    public string DtrIconDisabled { get; set; } = "\uE0BC";
    public bool KrangleEnabled { get; set; } = false;
    public bool ShowConditionPanel { get; set; } = true;
    public bool ShowRepairSummary { get; set; } = true;
    public bool ShowPartyStatus { get; set; } = true;
    public bool ShowPartyRadar { get; set; } = true;
    public bool EnumeratePartyMembers { get; set; } = false;
    public float RadarBoxSizePixels { get; set; } = 160f;
    public float RadarCombatWidthYalms { get; set; } = 20f;
    public float RadarCombatHeightYalms { get; set; } = 20f;
    public float RadarOutOfCombatWidthYalms { get; set; } = 50f;
    public float RadarOutOfCombatHeightYalms { get; set; } = 50f;
    // Legacy single-axis radar scale retained for config migration only.
    public float RadarScaleYalms { get; set; } = 35f;
    public bool RemoteServerEnabled { get; set; } = false;
    public string RemoteServerUrl { get; set; } = "http://127.0.0.1:6942";
    public int RemotePositionIntervalMs { get; set; } = 250;
    public int RemoteFullSnapshotIntervalMs { get; set; } = 2000;
    public bool AllowWebEchoCommands { get; set; } = false;
    public bool AllowWebScreenshotRequests { get; set; } = false;

    public void Save()
    {
        if (!string.IsNullOrWhiteSpace(LastAccountId))
            Accounts[LastAccountId] = AccountScopedConfiguration.FromConfiguration(this);

        Plugin.PluginInterface.SavePluginConfig(this);
    }

    public bool EnsureActiveAccount(string accountId)
    {
        if (string.IsNullOrWhiteSpace(accountId))
            return false;

        var changed = false;
        if (string.IsNullOrWhiteSpace(LastAccountId))
        {
            if (Accounts.Count == 0)
            {
                Accounts[accountId] = AccountScopedConfiguration.FromConfiguration(this);
                LastAccountId = accountId;
                return true;
            }

            LastAccountId = accountId;
            changed = true;
        }

        if (string.Equals(LastAccountId, accountId, StringComparison.Ordinal))
        {
            if (!Accounts.ContainsKey(accountId))
            {
                Accounts[accountId] = AccountScopedConfiguration.FromConfiguration(this);
                changed = true;
            }

            return changed;
        }

        Accounts[LastAccountId] = AccountScopedConfiguration.FromConfiguration(this);
        changed = true;

        if (!Accounts.TryGetValue(accountId, out var accountConfig))
        {
            accountConfig = new AccountScopedConfiguration();
            Accounts[accountId] = accountConfig;
        }

        accountConfig.ApplyTo(this);
        LastAccountId = accountId;
        return true;
    }
}

[Serializable]
public sealed class AccountScopedConfiguration
{
    public bool OverlayEnabled { get; set; } = true;
    public bool DtrBarEnabled { get; set; } = true;
    public int DtrBarMode { get; set; } = 0;
    public string DtrIconEnabled { get; set; } = "\uE0BB";
    public string DtrIconDisabled { get; set; } = "\uE0BC";
    public bool KrangleEnabled { get; set; } = false;
    public bool ShowConditionPanel { get; set; } = true;
    public bool ShowRepairSummary { get; set; } = true;
    public bool ShowPartyStatus { get; set; } = true;
    public bool ShowPartyRadar { get; set; } = true;
    public bool EnumeratePartyMembers { get; set; } = false;
    public float RadarBoxSizePixels { get; set; } = 160f;
    public float RadarCombatWidthYalms { get; set; } = 20f;
    public float RadarCombatHeightYalms { get; set; } = 20f;
    public float RadarOutOfCombatWidthYalms { get; set; } = 50f;
    public float RadarOutOfCombatHeightYalms { get; set; } = 50f;
    public float RadarScaleYalms { get; set; } = 35f;
    public bool RemoteServerEnabled { get; set; } = false;
    public string RemoteServerUrl { get; set; } = "http://127.0.0.1:6942";
    public int RemotePositionIntervalMs { get; set; } = 250;
    public int RemoteFullSnapshotIntervalMs { get; set; } = 2000;
    public bool AllowWebEchoCommands { get; set; } = false;
    public bool AllowWebScreenshotRequests { get; set; } = false;

    public static AccountScopedConfiguration FromConfiguration(Configuration source)
    {
        return new AccountScopedConfiguration
        {
            OverlayEnabled = source.OverlayEnabled,
            DtrBarEnabled = source.DtrBarEnabled,
            DtrBarMode = source.DtrBarMode,
            DtrIconEnabled = source.DtrIconEnabled,
            DtrIconDisabled = source.DtrIconDisabled,
            KrangleEnabled = source.KrangleEnabled,
            ShowConditionPanel = source.ShowConditionPanel,
            ShowRepairSummary = source.ShowRepairSummary,
            ShowPartyStatus = source.ShowPartyStatus,
            ShowPartyRadar = source.ShowPartyRadar,
            EnumeratePartyMembers = source.EnumeratePartyMembers,
            RadarBoxSizePixels = source.RadarBoxSizePixels,
            RadarCombatWidthYalms = source.RadarCombatWidthYalms,
            RadarCombatHeightYalms = source.RadarCombatHeightYalms,
            RadarOutOfCombatWidthYalms = source.RadarOutOfCombatWidthYalms,
            RadarOutOfCombatHeightYalms = source.RadarOutOfCombatHeightYalms,
            RadarScaleYalms = source.RadarScaleYalms,
            RemoteServerEnabled = source.RemoteServerEnabled,
            RemoteServerUrl = source.RemoteServerUrl,
            RemotePositionIntervalMs = source.RemotePositionIntervalMs,
            RemoteFullSnapshotIntervalMs = source.RemoteFullSnapshotIntervalMs,
            AllowWebEchoCommands = source.AllowWebEchoCommands,
            AllowWebScreenshotRequests = source.AllowWebScreenshotRequests,
        };
    }

    public void ApplyTo(Configuration target)
    {
        target.OverlayEnabled = OverlayEnabled;
        target.DtrBarEnabled = DtrBarEnabled;
        target.DtrBarMode = DtrBarMode;
        target.DtrIconEnabled = DtrIconEnabled;
        target.DtrIconDisabled = DtrIconDisabled;
        target.KrangleEnabled = KrangleEnabled;
        target.ShowConditionPanel = ShowConditionPanel;
        target.ShowRepairSummary = ShowRepairSummary;
        target.ShowPartyStatus = ShowPartyStatus;
        target.ShowPartyRadar = ShowPartyRadar;
        target.EnumeratePartyMembers = EnumeratePartyMembers;
        target.RadarBoxSizePixels = RadarBoxSizePixels;
        target.RadarCombatWidthYalms = RadarCombatWidthYalms;
        target.RadarCombatHeightYalms = RadarCombatHeightYalms;
        target.RadarOutOfCombatWidthYalms = RadarOutOfCombatWidthYalms;
        target.RadarOutOfCombatHeightYalms = RadarOutOfCombatHeightYalms;
        target.RadarScaleYalms = RadarScaleYalms;
        target.RemoteServerEnabled = RemoteServerEnabled;
        target.RemoteServerUrl = RemoteServerUrl;
        target.RemotePositionIntervalMs = RemotePositionIntervalMs;
        target.RemoteFullSnapshotIntervalMs = RemoteFullSnapshotIntervalMs;
        target.AllowWebEchoCommands = AllowWebEchoCommands;
        target.AllowWebScreenshotRequests = AllowWebScreenshotRequests;
    }
}
