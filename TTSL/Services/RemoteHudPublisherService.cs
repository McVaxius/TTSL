using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Numerics;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading;
using System.Threading.Tasks;
using Dalamud.Game.ClientState.Objects.Enums;
using Dalamud.Game.ClientState.Objects.Types;
using Lumina.Excel.Sheets;

namespace TTSL.Services;

internal sealed class RemoteHudPublisherService : IDisposable
{
    private const int MaxMana = 10000;
    private const int MaxCombatHostiles = 8;
    private const float CombatTelemetryRangeYalms = 55f;
    private const int CustomizeRaceIndex = 0;
    private const int CustomizeTribeIndex = 4;
    private static readonly TimeSpan HttpTimeout = TimeSpan.FromSeconds(2);
    private static readonly TimeSpan MaxRetryBackoff = TimeSpan.FromSeconds(15);

    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web)
    {
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    };

    private readonly Plugin plugin;
    private readonly HttpClient httpClient = new() { Timeout = HttpTimeout };
    private readonly CancellationTokenSource shutdownCts = new();

    private int sendInFlight;
    private bool isDisposing;
    private int consecutiveFailureCount;
    private DateTime lastPositionUpdateUtc = DateTime.MinValue;
    private DateTime lastFullSnapshotUtc = DateTime.MinValue;
    private DateTime nextAttemptUtc = DateTime.MinValue;
    private DateTime? lastSuccessUtc;
    private bool lastAttemptFailed;
    private string? lastError;
    private string statusText = "Disabled";
    private ClientIdentity? lastIdentity;
    private bool goodbyeSent;

    public RemoteHudPublisherService(Plugin plugin)
    {
        this.plugin = plugin;
    }

    public string StatusText => statusText;
    public string? LastError => lastError;
    public DateTime? LastSuccessUtc => lastSuccessUtc;
    public string? LastAccountId => lastIdentity?.AccountId;
    public string? LastCharacterKey => lastIdentity == null ? null : $"{lastIdentity.CharacterName}@{lastIdentity.WorldName}";

    public void Update()
    {
        var cfg = plugin.Configuration;
        if (!cfg.RemoteServerEnabled)
        {
            statusText = "Disabled";
            ResetCadence();
            ResetFailureState();
            TrySendGoodbyeIfNeeded();
            return;
        }

        var identity = GetCurrentIdentity();
        if (identity == null)
        {
            statusText = "Waiting for local player";
            ResetCadence();
            ResetFailureState();
            TrySendGoodbyeIfNeeded();
            return;
        }

        if (lastIdentity != null && !IdentityMatches(lastIdentity, identity))
        {
            TrySendGoodbyeIfNeeded();
            ResetCadence();
            ResetFailureState();
        }

        goodbyeSent = false;
        lastIdentity = identity;

        var now = DateTime.UtcNow;
        if (lastAttemptFailed && now < nextAttemptUtc)
        {
            statusText = BuildBackoffStatus(now);
            return;
        }

        if ((now - lastFullSnapshotUtc).TotalMilliseconds >= Math.Max(500, cfg.RemoteFullSnapshotIntervalMs))
        {
            var fullSnapshot = BuildFullSnapshot(identity);
            if (fullSnapshot != null && TryQueueSend("/api/update", fullSnapshot))
                lastFullSnapshotUtc = now;
            return;
        }

        if ((now - lastPositionUpdateUtc).TotalMilliseconds >= Math.Max(100, cfg.RemotePositionIntervalMs))
        {
            var positionUpdate = BuildPositionUpdate(identity);
            if (positionUpdate != null && TryQueueSend("/api/update", positionUpdate))
                lastPositionUpdateUtc = now;
        }
    }

    public void Dispose()
    {
        isDisposing = true;
        TrySendGoodbyeIfNeeded();
        SpinWait.SpinUntil(() => Volatile.Read(ref sendInFlight) == 0, TimeSpan.FromMilliseconds(150));
        shutdownCts.Cancel();
        SpinWait.SpinUntil(() => Volatile.Read(ref sendInFlight) == 0, TimeSpan.FromMilliseconds(150));
        shutdownCts.Dispose();
        httpClient.Dispose();
    }

    private void TrySendGoodbyeIfNeeded()
    {
        if (lastIdentity == null || goodbyeSent)
            return;

        if (!TryBeginSend())
            return;

        goodbyeSent = true;
        _ = SendAsync("/api/goodbye", new RemoteGoodbyeRequest
        {
            AccountId = lastIdentity.AccountId,
            CharacterName = lastIdentity.CharacterName,
            WorldName = lastIdentity.WorldName,
        });
    }

    private bool TryQueueSend<T>(string path, T payload)
    {
        if (!TryBeginSend())
            return false;

        _ = SendAsync(path, payload);
        return true;
    }

    private bool TryBeginSend()
    {
        if (isDisposing || shutdownCts.IsCancellationRequested)
            return false;

        return Interlocked.CompareExchange(ref sendInFlight, 1, 0) == 0;
    }

    private ClientIdentity? GetCurrentIdentity()
    {
        var localPlayer = Plugin.ObjectTable.LocalPlayer;
        if (localPlayer == null)
            return null;

        var contentId = Plugin.PlayerState.ContentId;
        if (contentId == 0)
            return null;

        var characterName = localPlayer.Name.TextValue;
        var worldName = localPlayer.HomeWorld.Value.Name.ToString();
        if (string.IsNullOrWhiteSpace(characterName) || string.IsNullOrWhiteSpace(worldName))
            return null;

        return new ClientIdentity
        {
            AccountId = contentId.ToString("X16"),
            CharacterName = characterName,
            WorldName = worldName,
        };
    }

    private RemoteHudSnapshot? BuildFullSnapshot(ClientIdentity identity)
    {
        var localPlayer = Plugin.ObjectTable.LocalPlayer;
        if (localPlayer == null)
            return null;

        var map = GetMapSnapshot(Plugin.ClientState.TerritoryType);

        return new RemoteHudSnapshot
        {
            UpdateKind = "full",
            TimestampUtc = DateTime.UtcNow,
            AccountId = identity.AccountId,
            CharacterName = identity.CharacterName,
            WorldName = identity.WorldName,
            KrangledName = KrangleService.KrangleName($"{identity.CharacterName}@{identity.WorldName}"),
            HostName = Environment.MachineName,
            GameInstallPath = GetGameInstallPath(),
            EnumeratePartyMembers = plugin.Configuration.EnumeratePartyMembers,
            Job = GetJobAbbreviation(localPlayer),
            JobId = localPlayer.ClassJob.RowId,
            JobIconId = GetJobIconId(localPlayer.ClassJob.RowId),
            Gender = (byte)Plugin.PlayerState.Sex,
            TerritoryId = Plugin.ClientState.TerritoryType,
            TerritoryName = plugin.GetTerritoryName(Plugin.ClientState.TerritoryType),
            MapId = map?.MapId,
            Map = map,
            Position = new Vector3Snapshot(localPlayer.Position.X, localPlayer.Position.Y, localPlayer.Position.Z),
            Player = new PlayerStatsSnapshot(localPlayer.CurrentHp, localPlayer.MaxHp, localPlayer.CurrentMp, MaxMana, localPlayer.Level),
            RaceId = GetCustomizeValue(localPlayer, CustomizeRaceIndex),
            TribeId = GetCustomizeValue(localPlayer, CustomizeTribeIndex),
            Conditions = BuildConditionSnapshot(),
            Repair = BuildRepairSnapshot(),
            Party = BuildPartyMembers(localPlayer),
            Combat = BuildCombatSnapshot(localPlayer),
        };
    }

    private RemoteHudSnapshot? BuildPositionUpdate(ClientIdentity identity)
    {
        var localPlayer = Plugin.ObjectTable.LocalPlayer;
        if (localPlayer == null)
            return null;

        var map = GetMapSnapshot(Plugin.ClientState.TerritoryType);

        return new RemoteHudSnapshot
        {
            UpdateKind = "position",
            TimestampUtc = DateTime.UtcNow,
            AccountId = identity.AccountId,
            CharacterName = identity.CharacterName,
            WorldName = identity.WorldName,
            KrangledName = KrangleService.KrangleName($"{identity.CharacterName}@{identity.WorldName}"),
            HostName = Environment.MachineName,
            EnumeratePartyMembers = plugin.Configuration.EnumeratePartyMembers,
            Job = GetJobAbbreviation(localPlayer),
            JobId = localPlayer.ClassJob.RowId,
            JobIconId = GetJobIconId(localPlayer.ClassJob.RowId),
            Gender = (byte)Plugin.PlayerState.Sex,
            TerritoryId = Plugin.ClientState.TerritoryType,
            TerritoryName = plugin.GetTerritoryName(Plugin.ClientState.TerritoryType),
            MapId = map?.MapId,
            Map = map,
            Position = new Vector3Snapshot(localPlayer.Position.X, localPlayer.Position.Y, localPlayer.Position.Z),
            Player = new PlayerStatsSnapshot(localPlayer.CurrentHp, localPlayer.MaxHp, localPlayer.CurrentMp, MaxMana, localPlayer.Level),
            RaceId = GetCustomizeValue(localPlayer, CustomizeRaceIndex),
            TribeId = GetCustomizeValue(localPlayer, CustomizeTribeIndex),
            Combat = BuildCombatSnapshot(localPlayer),
        };
    }

    private RemoteConditionSnapshot BuildConditionSnapshot()
    {
        return new RemoteConditionSnapshot
        {
            InCombat = Plugin.Condition[Dalamud.Game.ClientState.Conditions.ConditionFlag.InCombat],
            BoundByDuty = Plugin.Condition[Dalamud.Game.ClientState.Conditions.ConditionFlag.BoundByDuty] ||
                          Plugin.Condition[Dalamud.Game.ClientState.Conditions.ConditionFlag.BoundByDuty56],
            WaitingForDuty = Plugin.Condition[Dalamud.Game.ClientState.Conditions.ConditionFlag.WaitingForDutyFinder],
            Mounted = Plugin.Condition[Dalamud.Game.ClientState.Conditions.ConditionFlag.Mounted],
            Casting = Plugin.Condition[Dalamud.Game.ClientState.Conditions.ConditionFlag.Casting],
            Dead = Plugin.Condition[Dalamud.Game.ClientState.Conditions.ConditionFlag.Unconscious],
        };
    }

    private RemoteRepairSnapshot? BuildRepairSnapshot()
    {
        var summary = plugin.GetRepairSummary();
        if (!summary.MinCondition.HasValue)
            return null;

        return new RemoteRepairSnapshot
        {
            MinCondition = summary.MinCondition.Value,
            AverageCondition = summary.AverageCondition,
            EquippedCount = summary.EquippedCount,
        };
    }

    private static List<RemotePartyMemberSnapshot> BuildPartyMembers(ICharacter localPlayer)
    {
        var members = new List<RemotePartyMemberSnapshot>();

        for (var i = 0; i < Plugin.PartyList.Length; i++)
        {
            var member = Plugin.PartyList[i];
            if (member == null)
                continue;

            var originalName = member.Name.TextValue;
            if (string.IsNullOrWhiteSpace(originalName))
                continue;

            var character = FindPartyCharacter(member.Address, originalName);
            var job = member.ClassJob.IsValid ? member.ClassJob.Value.Abbreviation.ToString() : "UNK";
            var jobId = member.ClassJob.RowId;

            members.Add(new RemotePartyMemberSnapshot
            {
                Slot = i + 1,
                Name = originalName,
                KrangledName = KrangleService.KrangleName(originalName),
                Job = job,
                JobId = jobId == 0 ? null : jobId,
                JobIconId = GetJobIconId(jobId),
                Level = member.Level,
                CurrentHp = character?.CurrentHp,
                MaxHp = character?.MaxHp,
                CurrentMp = character?.CurrentMp,
                MaxMp = character == null ? null : MaxMana,
                RaceId = character == null ? null : GetCustomizeValue(character, CustomizeRaceIndex),
                TribeId = character == null ? null : GetCustomizeValue(character, CustomizeTribeIndex),
                Position = character == null
                    ? null
                    : new Vector3Snapshot(character.Position.X, character.Position.Y, character.Position.Z),
                Distance = character == null ? null : Vector3.Distance(localPlayer.Position, character.Position),
            });
        }

        return members;
    }

    private RemoteCombatSnapshot? BuildCombatSnapshot(ICharacter localPlayer)
    {
        var trackedAddresses = BuildTrackedPartyAddresses();
        var hostileSnapshots = Plugin.ObjectTable
            .OfType<IBattleChara>()
            .Where(obj => obj.ObjectKind == ObjectKind.BattleNpc)
            .Where(obj => obj.Address != localPlayer.Address && obj.CurrentHp > 0)
            .Select(obj => BuildEnemySnapshot(localPlayer, obj, trackedAddresses, isCurrentTarget: Plugin.TargetManager.Target?.Address == obj.Address))
            .Where(snapshot => snapshot != null)
            .Cast<RemoteEnemySnapshot>()
            .OrderByDescending(snapshot => snapshot.IsTargetingTrackedParty)
            .ThenByDescending(snapshot => snapshot.IsCurrentTarget)
            .ThenBy(snapshot => snapshot.Distance ?? float.MaxValue)
            .Take(MaxCombatHostiles)
            .ToList();

        var currentTarget = Plugin.TargetManager.Target as IBattleChara;
        var currentTargetSnapshot = currentTarget == null || currentTarget.ObjectKind != ObjectKind.BattleNpc || currentTarget.CurrentHp == 0
            ? null
            : BuildEnemySnapshot(localPlayer, currentTarget, trackedAddresses, isCurrentTarget: true);

        if (currentTargetSnapshot == null && hostileSnapshots.Count == 0)
            return null;

        return new RemoteCombatSnapshot
        {
            CurrentTarget = currentTargetSnapshot,
            Hostiles = hostileSnapshots,
        };
    }

    private static HashSet<nint> BuildTrackedPartyAddresses()
    {
        var trackedAddresses = new HashSet<nint>();

        var localPlayer = Plugin.ObjectTable.LocalPlayer;
        if (localPlayer != null)
            trackedAddresses.Add(localPlayer.Address);

        for (var i = 0; i < Plugin.PartyList.Length; i++)
        {
            var member = Plugin.PartyList[i];
            if (member == null)
                continue;

            var name = member.Name.TextValue;
            if (string.IsNullOrWhiteSpace(name))
                continue;

            var character = FindPartyCharacter(member.Address, name);
            if (character != null)
                trackedAddresses.Add(character.Address);
        }

        return trackedAddresses;
    }

    private static RemoteEnemySnapshot? BuildEnemySnapshot(
        ICharacter localPlayer,
        IBattleChara hostile,
        HashSet<nint> trackedAddresses,
        bool isCurrentTarget)
    {
        var distance = Vector3.Distance(localPlayer.Position, hostile.Position);
        if (distance > CombatTelemetryRangeYalms)
            return null;

        var targetObject = hostile.TargetObject;
        var isTargetingTrackedParty = targetObject != null && trackedAddresses.Contains(targetObject.Address);
        var isTargetingLocalPlayer = targetObject?.Address == localPlayer.Address;
        var castRemaining = hostile.IsCasting ? Math.Max(0f, hostile.TotalCastTime - hostile.CurrentCastTime) : (float?)null;

        return new RemoteEnemySnapshot
        {
            Name = hostile.Name.TextValue,
            KrangledName = KrangleService.KrangleName(hostile.Name.TextValue),
            DataId = hostile.BaseId,
            CurrentHp = hostile.CurrentHp,
            MaxHp = hostile.MaxHp,
            Distance = distance,
            Position = new Vector3Snapshot(hostile.Position.X, hostile.Position.Y, hostile.Position.Z),
            IsCurrentTarget = isCurrentTarget,
            IsTargetingTrackedParty = isTargetingTrackedParty,
            IsTargetingLocalPlayer = isTargetingLocalPlayer,
            TargetName = targetObject?.Name.TextValue,
            KrangledTargetName = targetObject == null ? null : KrangleService.KrangleName(targetObject.Name.TextValue),
            IsCasting = hostile.IsCasting,
            CastActionId = hostile.IsCasting ? hostile.CastActionId : null,
            CastTimeRemaining = castRemaining,
        };
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

    private void ResetCadence()
    {
        lastPositionUpdateUtc = DateTime.MinValue;
        lastFullSnapshotUtc = DateTime.MinValue;
    }

    private void ResetFailureState()
    {
        consecutiveFailureCount = 0;
        nextAttemptUtc = DateTime.MinValue;
        lastAttemptFailed = false;
        lastError = null;
    }

    private static bool IdentityMatches(ClientIdentity left, ClientIdentity right)
    {
        return string.Equals(left.AccountId, right.AccountId, StringComparison.Ordinal) &&
               string.Equals(left.CharacterName, right.CharacterName, StringComparison.Ordinal) &&
               string.Equals(left.WorldName, right.WorldName, StringComparison.Ordinal);
    }

    private static uint? GetJobIconId(uint jobId)
    {
        if (jobId == 0)
            return null;
        return 62000u + jobId;
    }

    private static RemoteMapSnapshot? GetMapSnapshot(uint territoryId)
    {
        if (territoryId == 0)
            return null;

        try
        {
            var sheet = Plugin.DataManager.GetExcelSheet<TerritoryType>();
            if (sheet != null && sheet.TryGetRow(territoryId, out var territory) && territory.Map.IsValid)
            {
                var map = territory.Map.Value;
                return new RemoteMapSnapshot
                {
                    MapId = territory.Map.RowId,
                    TexturePath = null,
                    OffsetX = map.OffsetX,
                    OffsetY = map.OffsetY,
                    SizeFactor = map.SizeFactor,
                };
            }
        }
        catch
        {
        }

        return null;
    }

    private static byte? GetCustomizeValue(ICharacter character, int index)
    {
        try
        {
            var customize = character.Customize;
            if (index >= 0 && index < customize.Length)
                return customize[index];
        }
        catch
        {
        }

        return null;
    }

    private static string GetJobAbbreviation(ICharacter character)
        => character.ClassJob.IsValid ? character.ClassJob.Value.Abbreviation.ToString() : "UNK";

    private static string? GetGameInstallPath()
    {
        try
        {
            var executablePath = Process.GetCurrentProcess().MainModule?.FileName;
            if (string.IsNullOrWhiteSpace(executablePath))
                return null;

            var directory = Path.GetDirectoryName(executablePath);
            return string.IsNullOrWhiteSpace(directory) ? null : directory;
        }
        catch
        {
            return null;
        }
    }

    private async Task SendAsync<T>(string path, T payload)
    {
        try
        {
            if (shutdownCts.IsCancellationRequested || isDisposing)
                return;

            var baseUrl = NormalizeBaseUrl(plugin.Configuration.RemoteServerUrl);
            if (string.IsNullOrWhiteSpace(baseUrl))
            {
                RecordFailure("Remote server URL is empty.");
                return;
            }

            var json = JsonSerializer.Serialize(payload, JsonOptions);
            using var content = new StringContent(json, Encoding.UTF8, "application/json");
            using var response = await httpClient.PostAsync($"{baseUrl}{path}", content, shutdownCts.Token).ConfigureAwait(false);
            if (!response.IsSuccessStatusCode)
            {
                var errorBody = await response.Content.ReadAsStringAsync(shutdownCts.Token).ConfigureAwait(false);
                RecordFailure($"HTTP {(int)response.StatusCode} from {baseUrl}{path}: {TrimForLog(errorBody)}");
                return;
            }

            RecordSuccess(baseUrl);
        }
        catch (OperationCanceledException) when (shutdownCts.IsCancellationRequested || isDisposing)
        {
            // Shutdown/reload cancellation is expected during plugin unload.
        }
        catch (ObjectDisposedException) when (shutdownCts.IsCancellationRequested || isDisposing)
        {
            // Shutdown raced the request; safe to ignore during plugin unload.
        }
        catch (Exception ex)
        {
            RecordFailure(ex.Message);
        }
        finally
        {
            Interlocked.Exchange(ref sendInFlight, 0);
        }
    }

    private void RecordSuccess(string baseUrl)
    {
        lastSuccessUtc = DateTime.UtcNow;
        statusText = $"Connected to {baseUrl}";
        if (lastAttemptFailed)
            Plugin.Log.Information("[TTSL] Remote HUD publishing recovered: {BaseUrl}", baseUrl);

        consecutiveFailureCount = 0;
        nextAttemptUtc = DateTime.MinValue;
        lastAttemptFailed = false;
        lastError = null;
    }

    private void RecordFailure(string error)
    {
        consecutiveFailureCount = Math.Min(consecutiveFailureCount + 1, 5);
        var backoff = CalculateBackoff(consecutiveFailureCount);
        nextAttemptUtc = DateTime.UtcNow + backoff;
        statusText = $"Retrying in {Math.Max(1, (int)Math.Ceiling(backoff.TotalSeconds))}s";

        if (!lastAttemptFailed || !string.Equals(lastError, error, StringComparison.Ordinal))
        {
            Plugin.Log.Warning("[TTSL] Remote HUD publishing failed: {Error}. Backing off for {Seconds}s.",
                error,
                Math.Max(1, (int)Math.Ceiling(backoff.TotalSeconds)));
        }

        lastAttemptFailed = true;
        lastError = error;
    }

    private string BuildBackoffStatus(DateTime now)
    {
        var remaining = nextAttemptUtc - now;
        var seconds = Math.Max(1, (int)Math.Ceiling(Math.Max(0, remaining.TotalSeconds)));
        return $"Retrying in {seconds}s";
    }

    private static TimeSpan CalculateBackoff(int consecutiveFailures)
    {
        var seconds = Math.Min(MaxRetryBackoff.TotalSeconds, Math.Pow(2, Math.Max(0, consecutiveFailures - 1)));
        return TimeSpan.FromSeconds(Math.Max(1, seconds));
    }

    private static string NormalizeBaseUrl(string url)
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

    private static string TrimForLog(string errorBody)
    {
        if (string.IsNullOrWhiteSpace(errorBody))
            return "No response body";

        var trimmed = errorBody.Trim();
        return trimmed.Length <= 220 ? trimmed : $"{trimmed[..220]}...";
    }

    private sealed class ClientIdentity
    {
        public string AccountId { get; init; } = string.Empty;
        public string CharacterName { get; init; } = string.Empty;
        public string WorldName { get; init; } = string.Empty;
    }

    private sealed class RemoteGoodbyeRequest
    {
        public string AccountId { get; init; } = string.Empty;
        public string CharacterName { get; init; } = string.Empty;
        public string WorldName { get; init; } = string.Empty;
    }

    private sealed class RemoteHudSnapshot
    {
        public string UpdateKind { get; init; } = "full";
        public DateTime TimestampUtc { get; init; }
        public string AccountId { get; init; } = string.Empty;
        public string CharacterName { get; init; } = string.Empty;
        public string WorldName { get; init; } = string.Empty;
        public string KrangledName { get; init; } = string.Empty;
        public string HostName { get; init; } = string.Empty;
        public string? GameInstallPath { get; init; }
        public bool EnumeratePartyMembers { get; init; }
        public string Job { get; init; } = string.Empty;
        public uint JobId { get; init; }
        public uint? JobIconId { get; init; }
        public byte Gender { get; init; }
        public uint TerritoryId { get; init; }
        public string TerritoryName { get; init; } = string.Empty;
        public uint? MapId { get; init; }
        public RemoteMapSnapshot? Map { get; init; }
        public Vector3Snapshot? Position { get; init; }
        public PlayerStatsSnapshot? Player { get; init; }
        public byte? RaceId { get; init; }
        public byte? TribeId { get; init; }
        public RemoteConditionSnapshot? Conditions { get; init; }
        public RemoteRepairSnapshot? Repair { get; init; }
        public List<RemotePartyMemberSnapshot>? Party { get; init; }
        public RemoteCombatSnapshot? Combat { get; init; }
    }

    private sealed class Vector3Snapshot
    {
        public Vector3Snapshot(float x, float y, float z)
        {
            X = x;
            Y = y;
            Z = z;
        }

        public float X { get; init; }
        public float Y { get; init; }
        public float Z { get; init; }
    }

    private sealed class PlayerStatsSnapshot
    {
        public PlayerStatsSnapshot(uint currentHp, uint maxHp, uint currentMp, int maxMp, uint level)
        {
            CurrentHp = currentHp;
            MaxHp = maxHp;
            CurrentMp = currentMp;
            MaxMp = maxMp;
            Level = level;
        }

        public uint CurrentHp { get; init; }
        public uint MaxHp { get; init; }
        public uint CurrentMp { get; init; }
        public int MaxMp { get; init; }
        public uint Level { get; init; }
    }

    private sealed class RemoteMapSnapshot
    {
        public uint MapId { get; init; }
        public string? TexturePath { get; init; }
        public short OffsetX { get; init; }
        public short OffsetY { get; init; }
        public ushort SizeFactor { get; init; }
    }

    private sealed class RemoteConditionSnapshot
    {
        public bool InCombat { get; init; }
        public bool BoundByDuty { get; init; }
        public bool WaitingForDuty { get; init; }
        public bool Mounted { get; init; }
        public bool Casting { get; init; }
        public bool Dead { get; init; }
    }

    private sealed class RemoteRepairSnapshot
    {
        public int MinCondition { get; init; }
        public int AverageCondition { get; init; }
        public int EquippedCount { get; init; }
    }

    private sealed class RemotePartyMemberSnapshot
    {
        public int Slot { get; init; }
        public string Name { get; init; } = string.Empty;
        public string KrangledName { get; init; } = string.Empty;
        public string Job { get; init; } = string.Empty;
        public uint? JobId { get; init; }
        public uint? JobIconId { get; init; }
        public uint Level { get; init; }
        public uint? CurrentHp { get; init; }
        public uint? MaxHp { get; init; }
        public uint? CurrentMp { get; init; }
        public int? MaxMp { get; init; }
        public byte? RaceId { get; init; }
        public byte? TribeId { get; init; }
        public Vector3Snapshot? Position { get; init; }
        public float? Distance { get; init; }
    }

    private sealed class RemoteCombatSnapshot
    {
        public RemoteEnemySnapshot? CurrentTarget { get; init; }
        public List<RemoteEnemySnapshot> Hostiles { get; init; } = [];
    }

    private sealed class RemoteEnemySnapshot
    {
        public string Name { get; init; } = string.Empty;
        public string KrangledName { get; init; } = string.Empty;
        public uint DataId { get; init; }
        public uint CurrentHp { get; init; }
        public uint MaxHp { get; init; }
        public float? Distance { get; init; }
        public Vector3Snapshot? Position { get; init; }
        public bool IsCurrentTarget { get; init; }
        public bool IsTargetingTrackedParty { get; init; }
        public bool IsTargetingLocalPlayer { get; init; }
        public string? TargetName { get; init; }
        public string? KrangledTargetName { get; init; }
        public bool IsCasting { get; init; }
        public uint? CastActionId { get; init; }
        public float? CastTimeRemaining { get; init; }
    }
}
