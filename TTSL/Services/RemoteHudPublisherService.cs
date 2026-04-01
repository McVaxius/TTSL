using System;
using System.Collections.Generic;
using System.Linq;
using System.Net.Http;
using System.Net.Http.Json;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading;
using System.Threading.Tasks;
using Dalamud.Game.ClientState.Objects.Types;

namespace TTSL.Services;

internal sealed class RemoteHudPublisherService : IDisposable
{
    private const int MaxMana = 10000;

    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web)
    {
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    };

    private readonly Plugin plugin;
    private readonly HttpClient httpClient = new() { Timeout = TimeSpan.FromSeconds(2) };
    private readonly SemaphoreSlim sendLock = new(1, 1);

    private DateTime lastPositionUpdateUtc = DateTime.MinValue;
    private DateTime lastFullSnapshotUtc = DateTime.MinValue;
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
            TrySendGoodbyeIfNeeded();
            return;
        }

        var identity = GetCurrentIdentity();
        if (identity == null)
        {
            statusText = "Waiting for local player";
            ResetCadence();
            TrySendGoodbyeIfNeeded();
            return;
        }

        if (lastIdentity != null && !IdentityMatches(lastIdentity, identity))
        {
            TrySendGoodbyeIfNeeded();
            ResetCadence();
        }

        goodbyeSent = false;
        lastIdentity = identity;

        var now = DateTime.UtcNow;
        if ((now - lastFullSnapshotUtc).TotalMilliseconds >= Math.Max(500, cfg.RemoteFullSnapshotIntervalMs))
        {
            lastFullSnapshotUtc = now;
            var fullSnapshot = BuildFullSnapshot(identity);
            if (fullSnapshot != null)
                _ = SendAsync("/api/update", fullSnapshot);
            return;
        }

        if ((now - lastPositionUpdateUtc).TotalMilliseconds >= Math.Max(100, cfg.RemotePositionIntervalMs))
        {
            lastPositionUpdateUtc = now;
            var positionUpdate = BuildPositionUpdate(identity);
            if (positionUpdate != null)
                _ = SendAsync("/api/update", positionUpdate);
        }
    }

    public void Dispose()
    {
        TrySendGoodbyeIfNeeded(force: true);
        httpClient.Dispose();
        sendLock.Dispose();
    }

    private void TrySendGoodbyeIfNeeded(bool force = false)
    {
        if (lastIdentity == null || goodbyeSent)
            return;

        goodbyeSent = true;

        if (force)
        {
            try
            {
                SendAsync("/api/goodbye", new RemoteGoodbyeRequest
                {
                    AccountId = lastIdentity.AccountId,
                    CharacterName = lastIdentity.CharacterName,
                    WorldName = lastIdentity.WorldName,
                }).GetAwaiter().GetResult();
            }
            catch
            {
                // Best-effort goodbye only.
            }

            return;
        }

        _ = SendAsync("/api/goodbye", new RemoteGoodbyeRequest
        {
            AccountId = lastIdentity.AccountId,
            CharacterName = lastIdentity.CharacterName,
            WorldName = lastIdentity.WorldName,
        });
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

        return new RemoteHudSnapshot
        {
            UpdateKind = "full",
            TimestampUtc = DateTime.UtcNow,
            AccountId = identity.AccountId,
            CharacterName = identity.CharacterName,
            WorldName = identity.WorldName,
            TerritoryId = Plugin.ClientState.TerritoryType,
            TerritoryName = plugin.GetTerritoryName(Plugin.ClientState.TerritoryType),
            Position = new Vector3Snapshot(localPlayer.Position.X, localPlayer.Position.Y, localPlayer.Position.Z),
            Player = new PlayerStatsSnapshot(localPlayer.CurrentHp, localPlayer.MaxHp, localPlayer.CurrentMp, MaxMana),
            Conditions = BuildConditionSnapshot(),
            Repair = BuildRepairSnapshot(),
            Party = BuildPartyMembers(localPlayer),
        };
    }

    private RemoteHudSnapshot? BuildPositionUpdate(ClientIdentity identity)
    {
        var localPlayer = Plugin.ObjectTable.LocalPlayer;
        if (localPlayer == null)
            return null;

        return new RemoteHudSnapshot
        {
            UpdateKind = "position",
            TimestampUtc = DateTime.UtcNow,
            AccountId = identity.AccountId,
            CharacterName = identity.CharacterName,
            WorldName = identity.WorldName,
            TerritoryId = Plugin.ClientState.TerritoryType,
            TerritoryName = plugin.GetTerritoryName(Plugin.ClientState.TerritoryType),
            Position = new Vector3Snapshot(localPlayer.Position.X, localPlayer.Position.Y, localPlayer.Position.Z),
            Player = new PlayerStatsSnapshot(localPlayer.CurrentHp, localPlayer.MaxHp, localPlayer.CurrentMp, MaxMana),
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

            members.Add(new RemotePartyMemberSnapshot
            {
                Slot = i + 1,
                Name = originalName,
                Job = job,
                CurrentHp = character?.CurrentHp,
                MaxHp = character?.MaxHp,
                CurrentMp = character?.CurrentMp,
                MaxMp = character == null ? null : MaxMana,
                Position = character == null
                    ? null
                    : new Vector3Snapshot(character.Position.X, character.Position.Y, character.Position.Z),
                Distance = character == null ? null : Vector3.Distance(localPlayer.Position, character.Position),
            });
        }

        return members;
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

    private static bool IdentityMatches(ClientIdentity left, ClientIdentity right)
    {
        return string.Equals(left.AccountId, right.AccountId, StringComparison.Ordinal) &&
               string.Equals(left.CharacterName, right.CharacterName, StringComparison.Ordinal) &&
               string.Equals(left.WorldName, right.WorldName, StringComparison.Ordinal);
    }

    private async Task SendAsync<T>(string path, T payload)
    {
        if (!await sendLock.WaitAsync(0).ConfigureAwait(false))
            return;

        try
        {
            var baseUrl = NormalizeBaseUrl(plugin.Configuration.RemoteServerUrl);
            if (string.IsNullOrWhiteSpace(baseUrl))
            {
                RecordFailure("Remote server URL is empty.");
                return;
            }

            using var response = await httpClient.PostAsJsonAsync($"{baseUrl}{path}", payload, JsonOptions).ConfigureAwait(false);
            if (!response.IsSuccessStatusCode)
            {
                RecordFailure($"HTTP {(int)response.StatusCode} from {baseUrl}{path}");
                return;
            }

            RecordSuccess(baseUrl);
        }
        catch (Exception ex)
        {
            RecordFailure(ex.Message);
        }
        finally
        {
            sendLock.Release();
        }
    }

    private void RecordSuccess(string baseUrl)
    {
        lastSuccessUtc = DateTime.UtcNow;
        statusText = $"Connected to {baseUrl}";
        if (lastAttemptFailed)
            Plugin.Log.Information("[TTSL] Remote HUD publishing recovered: {BaseUrl}", baseUrl);

        lastAttemptFailed = false;
        lastError = null;
    }

    private void RecordFailure(string error)
    {
        statusText = "Publish failed";
        if (!lastAttemptFailed || !string.Equals(lastError, error, StringComparison.Ordinal))
            Plugin.Log.Warning("[TTSL] Remote HUD publishing failed: {Error}", error);

        lastAttemptFailed = true;
        lastError = error;
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
        public uint TerritoryId { get; init; }
        public string TerritoryName { get; init; } = string.Empty;
        public Vector3Snapshot? Position { get; init; }
        public PlayerStatsSnapshot? Player { get; init; }
        public RemoteConditionSnapshot? Conditions { get; init; }
        public RemoteRepairSnapshot? Repair { get; init; }
        public List<RemotePartyMemberSnapshot>? Party { get; init; }
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
        public PlayerStatsSnapshot(uint currentHp, uint maxHp, uint currentMp, int maxMp)
        {
            CurrentHp = currentHp;
            MaxHp = maxHp;
            CurrentMp = currentMp;
            MaxMp = maxMp;
        }

        public uint CurrentHp { get; init; }
        public uint MaxHp { get; init; }
        public uint CurrentMp { get; init; }
        public int MaxMp { get; init; }
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
        public string Job { get; init; } = string.Empty;
        public uint? CurrentHp { get; init; }
        public uint? MaxHp { get; init; }
        public uint? CurrentMp { get; init; }
        public int? MaxMp { get; init; }
        public Vector3Snapshot? Position { get; init; }
        public float? Distance { get; init; }
    }
}
