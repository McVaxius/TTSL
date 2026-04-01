using System;
using Dalamud.Configuration;

namespace TTSL;

[Serializable]
public sealed class Configuration : IPluginConfiguration
{
    public int Version { get; set; } = 1;
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
    public float RadarScaleYalms { get; set; } = 35f;
    public bool RemoteServerEnabled { get; set; } = false;
    public string RemoteServerUrl { get; set; } = "http://127.0.0.1:69420";
    public int RemotePositionIntervalMs { get; set; } = 250;
    public int RemoteFullSnapshotIntervalMs { get; set; } = 2000;

    public void Save()
        => Plugin.PluginInterface.SavePluginConfig(this);
}
