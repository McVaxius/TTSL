using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Net.WebSockets;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading;
using System.Threading.Tasks;
using Dalamud.Game.ClientState.Objects.Enums;
using Dalamud.Game.ClientState.Objects.Types;
using FFXIVClientStructs.FFXIV.Client.UI.Agent;
using FFXIVClientStructs.FFXIV.Client.UI;
using FFXIVClientStructs.FFXIV.Client.System.String;
using Lumina.Excel;
using Lumina.Excel.Sheets;

namespace TTSL.Services;

internal sealed class RemoteHudPublisherService : IDisposable
{
    private const int MaxMana = 10000;
    private const int MaxCombatHostiles = 8;
    private const float CombatTelemetryRangeYalms = 55f;
    private const int CustomizeRaceIndex = 0;
    private const int CustomizeTribeIndex = 4;
    private static readonly TimeSpan HttpTimeout = TimeSpan.FromSeconds(10);
    private static readonly TimeSpan MaxRetryBackoff = TimeSpan.FromSeconds(15);
    private static readonly TimeSpan RepeatedFailureLogInterval = TimeSpan.FromMinutes(1);
    private const int MinimumReasonableCaptureWidth = 480;
    private const int MinimumReasonableCaptureHeight = 270;
    private const int CharacterVisualCaptureTimeoutMs = 12000;
    private const float InspectPreviewTopTrimFraction = 0.65f;
    private const float InspectPreviewBottomTrimFraction = 0.20f;
    private const int InspectPreviewExpandedHeightMultiplier = 2;

    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web)
    {
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    };

    private readonly Plugin plugin;
    private readonly HttpClient httpClient = new() { Timeout = HttpTimeout };
    private readonly CancellationTokenSource shutdownCts = new();
    private readonly ConcurrentQueue<string> pendingWebChatInputs = new();
    private readonly ConcurrentQueue<PendingCharacterVisualCapture> pendingCharacterVisualCaptures = new();
    private readonly object cctvLock = new();
    private readonly object cctvFrameCacheLock = new();
    private readonly WindowsGraphicsWindowCapture windowsGraphicsCapture = new();

    private int sendInFlight;
    private bool isDisposing;
    private int consecutiveFailureCount;
    private DateTime lastPositionUpdateUtc = DateTime.MinValue;
    private DateTime lastFullSnapshotUtc = DateTime.MinValue;
    private DateTime nextAttemptUtc = DateTime.MinValue;
    private DateTime lastFailureLogUtc = DateTime.MinValue;
    private DateTime? lastSuccessUtc;
    private bool lastAttemptFailed;
    private int suppressedFailureLogCount;
    private string? lastError;
    private string statusText = "Disabled";
    private ClientIdentity? lastIdentity;
    private string? lastLoggedMapSnapshotSignature;
    private string? lastLoggedCaptureSelectionSignature;
    private PendingCharacterVisualCapture? activeCharacterVisualCapture;
    private bool goodbyeSent;
    private ClientWebSocket? cctvWebSocket;
    private CancellationTokenSource? cctvConnectionCts;
    private Task? cctvConnectionTask;
    private string? cctvConnectionKey;
    private bool cctvCaptureActive;
    private CctvCapturePreset cctvActivePreset = ResolveCctvPreset("high");
    private DateTime nextCctvConnectAttemptUtc = DateTime.MinValue;
    private string? lastCctvSocketError;
    private DateTime lastCctvCaptureErrorLogUtc = DateTime.MinValue;
    private DateTime lastWgcCaptureErrorLogUtc = DateTime.MinValue;
    private long cctvFrameSequence;
    private Bitmap? lastGoodCctvBitmap;
    private CctvCaptureStatus? lastGoodCctvStatus;

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
            StopCctvWebSocket();
            statusText = "Disabled";
            ResetCadence();
            ResetFailureState();
            TrySendGoodbyeIfNeeded();
            return;
        }

        var identity = GetCurrentIdentity();
        if (identity == null)
        {
            StopCctvWebSocket();
            statusText = "Waiting for local player";
            ResetCadence();
            ResetFailureState();
            TrySendGoodbyeIfNeeded();
            return;
        }

        if (lastIdentity != null && !IdentityMatches(lastIdentity, identity))
        {
            StopCctvWebSocket();
            TrySendGoodbyeIfNeeded();
            ResetCadence();
            ResetFailureState();
        }

        goodbyeSent = false;
        lastIdentity = identity;
        MaintainCctvWebSocket(identity);
        ProcessQueuedWebChatInputs();
        ProcessCharacterVisualCaptures();

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
        StopCctvWebSocket();
        windowsGraphicsCapture.Dispose();
        ClearCctvFrameCache();
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

    private void MaintainCctvWebSocket(ClientIdentity identity)
    {
        if (!plugin.Configuration.AllowWebCctvStreaming)
        {
            StopCctvWebSocket();
            return;
        }

        var baseUrl = NormalizeBaseUrl(plugin.Configuration.RemoteServerUrl);
        if (string.IsNullOrWhiteSpace(baseUrl))
        {
            StopCctvWebSocket();
            return;
        }

        var connectionKey = $"{baseUrl}|{identity.AccountId}|{identity.CharacterName}|{identity.WorldName}";
        lock (cctvLock)
        {
            if (string.Equals(cctvConnectionKey, connectionKey, StringComparison.Ordinal) &&
                cctvConnectionTask is { IsCompleted: false })
            {
                return;
            }
        }

        if (DateTime.UtcNow < nextCctvConnectAttemptUtc)
            return;

        StopCctvWebSocket();
        var linkedCts = CancellationTokenSource.CreateLinkedTokenSource(shutdownCts.Token);
        lock (cctvLock)
        {
            cctvConnectionKey = connectionKey;
            cctvConnectionCts = linkedCts;
            cctvCaptureActive = false;
            cctvActivePreset = ResolveCctvPreset("high");
            cctvConnectionTask = RunCctvWebSocketAsync(baseUrl, identity, connectionKey, linkedCts);
        }
    }

    private void StopCctvWebSocket()
    {
        CancellationTokenSource? cts;
        ClientWebSocket? socket;
        lock (cctvLock)
        {
            cts = cctvConnectionCts;
            socket = cctvWebSocket;
            cctvConnectionKey = null;
            cctvConnectionTask = null;
            cctvConnectionCts = null;
            cctvWebSocket = null;
            cctvCaptureActive = false;
        }

        try
        {
            cts?.Cancel();
        }
        catch
        {
        }

        try
        {
            socket?.Abort();
        }
        catch
        {
        }
    }

    private async Task RunCctvWebSocketAsync(string baseUrl, ClientIdentity identity, string connectionKey, CancellationTokenSource connectionCts)
    {
        var token = connectionCts.Token;
        try
        {
            using var socket = new ClientWebSocket();
            socket.Options.KeepAliveInterval = TimeSpan.FromSeconds(15);
            lock (cctvLock)
            {
                if (!ReferenceEquals(cctvConnectionCts, connectionCts))
                    return;

                cctvWebSocket = socket;
            }

            await socket.ConnectAsync(BuildCctvPluginUri(baseUrl, identity), token).ConfigureAwait(false);
            lastCctvSocketError = null;

            var receiveTask = ReceiveCctvControlsAsync(socket, token);
            var captureTask = CaptureCctvFramesAsync(socket, identity, token);
            await Task.WhenAny(receiveTask, captureTask).ConfigureAwait(false);
            connectionCts.Cancel();
            try
            {
                await Task.WhenAll(receiveTask, captureTask).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (token.IsCancellationRequested || shutdownCts.IsCancellationRequested || isDisposing)
            {
            }
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested || shutdownCts.IsCancellationRequested || isDisposing)
        {
        }
        catch (ObjectDisposedException) when (token.IsCancellationRequested || shutdownCts.IsCancellationRequested || isDisposing)
        {
        }
        catch (Exception ex)
        {
            nextCctvConnectAttemptUtc = DateTime.UtcNow + TimeSpan.FromSeconds(5);
            if (!string.Equals(lastCctvSocketError, ex.Message, StringComparison.Ordinal))
            {
                lastCctvSocketError = ex.Message;
                Plugin.Log.Warning("[TTSL] CCTV socket failed: {Error}", ex.Message);
            }
        }
        finally
        {
            lock (cctvLock)
            {
                if (ReferenceEquals(cctvConnectionCts, connectionCts))
                {
                    cctvConnectionKey = null;
                    cctvConnectionTask = null;
                    cctvConnectionCts = null;
                    cctvWebSocket = null;
                    cctvCaptureActive = false;
                }
            }

            connectionCts.Dispose();
        }
    }

    private async Task ReceiveCctvControlsAsync(ClientWebSocket socket, CancellationToken token)
    {
        var buffer = new byte[4096];
        while (!token.IsCancellationRequested && socket.State == WebSocketState.Open)
        {
            using var message = new MemoryStream();
            WebSocketReceiveResult result;
            do
            {
                result = await socket.ReceiveAsync(new ArraySegment<byte>(buffer), token).ConfigureAwait(false);
                if (result.MessageType == WebSocketMessageType.Close)
                    return;

                if (message.Length + result.Count > 64 * 1024)
                    throw new InvalidOperationException("CCTV control message was too large.");

                message.Write(buffer, 0, result.Count);
            } while (!result.EndOfMessage);

            if (result.MessageType != WebSocketMessageType.Text)
                continue;

            var json = Encoding.UTF8.GetString(message.ToArray());
            HandleCctvControl(json);
        }
    }

    private void HandleCctvControl(string json)
    {
        CctvControlMessage? control;
        try
        {
            control = JsonSerializer.Deserialize<CctvControlMessage>(json, JsonOptions);
        }
        catch (Exception ex)
        {
            Plugin.Log.Debug(ex, "[TTSL] Ignoring invalid CCTV control payload.");
            return;
        }

        var type = control?.Type?.Trim().ToLowerInvariant();
        if (string.IsNullOrWhiteSpace(type))
            return;

        lock (cctvLock)
        {
            switch (type)
            {
                case "start":
                    cctvActivePreset = ResolveCctvPreset(control?.Quality);
                    cctvCaptureActive = true;
                    break;

                case "quality":
                    cctvActivePreset = ResolveCctvPreset(control?.Quality);
                    cctvCaptureActive = true;
                    break;

                case "stop":
                    cctvCaptureActive = false;
                    break;
            }
        }
    }

    private async Task CaptureCctvFramesAsync(ClientWebSocket socket, ClientIdentity identity, CancellationToken token)
    {
        var nextFrameUtc = DateTime.MinValue;
        while (!token.IsCancellationRequested && socket.State == WebSocketState.Open)
        {
            CctvCapturePreset preset;
            bool active;
            lock (cctvLock)
            {
                active = cctvCaptureActive;
                preset = cctvActivePreset;
            }

            if (!active || !plugin.Configuration.AllowWebCctvStreaming)
            {
                await Task.Delay(100, token).ConfigureAwait(false);
                continue;
            }

            var now = DateTime.UtcNow;
            if (now < nextFrameUtc)
            {
                await Task.Delay(nextFrameUtc - now, token).ConfigureAwait(false);
                continue;
            }

            try
            {
                byte[] imageBytes;
                int width;
                int height;
                CctvCaptureStatus captureStatus;
                using (var frame = await CaptureGameWindowFrameAsync(true, token).ConfigureAwait(false))
                using (var scaledBitmap = ResizeBitmap(frame.Bitmap, preset.Scale))
                {
                    width = scaledBitmap.Width;
                    height = scaledBitmap.Height;
                    imageBytes = EncodeBitmapAsJpeg(scaledBitmap, preset.JpegQuality);
                    captureStatus = frame.CaptureStatus;
                }

                var envelope = BuildCctvFrameEnvelope(identity, preset, imageBytes, width, height, captureStatus);
                await socket.SendAsync(new ArraySegment<byte>(envelope), WebSocketMessageType.Binary, true, token).ConfigureAwait(false);
                var frameIntervalMs = 1000d / Math.Max(1, preset.MaxFramesPerSecond);
                nextFrameUtc = DateTime.UtcNow + TimeSpan.FromMilliseconds(frameIntervalMs);
            }
            catch (OperationCanceledException) when (token.IsCancellationRequested || shutdownCts.IsCancellationRequested || isDisposing)
            {
                throw;
            }
            catch (Exception ex)
            {
                if ((DateTime.UtcNow - lastCctvCaptureErrorLogUtc).TotalSeconds >= 5)
                {
                    lastCctvCaptureErrorLogUtc = DateTime.UtcNow;
                    Plugin.Log.Warning(ex, "[TTSL] CCTV capture failed.");
                }

                await Task.Delay(1000, token).ConfigureAwait(false);
            }
        }
    }

    private static Uri BuildCctvPluginUri(string baseUrl, ClientIdentity identity)
    {
        var builder = new UriBuilder(baseUrl)
        {
            Scheme = baseUrl.StartsWith("https://", StringComparison.OrdinalIgnoreCase) ? "wss" : "ws",
            Path = "/ws/cctv/plugin",
            Query =
                $"accountId={Uri.EscapeDataString(identity.AccountId)}" +
                $"&characterName={Uri.EscapeDataString(identity.CharacterName)}" +
                $"&worldName={Uri.EscapeDataString(identity.WorldName)}",
        };
        return builder.Uri;
    }

    private byte[] BuildCctvFrameEnvelope(ClientIdentity identity, CctvCapturePreset preset, byte[] jpegBytes, int width, int height, CctvCaptureStatus captureStatus)
    {
        var metadata = new CctvFrameMetadata
        {
            AccountId = identity.AccountId,
            CharacterName = identity.CharacterName,
            WorldName = identity.WorldName,
            CapturedAtUtc = DateTime.UtcNow,
            Quality = preset.Name,
            ContentType = "image/jpeg",
            Width = width,
            Height = height,
            Sequence = Interlocked.Increment(ref cctvFrameSequence),
            CaptureStatus = captureStatus,
        };
        var metadataBytes = Encoding.UTF8.GetBytes(JsonSerializer.Serialize(metadata, JsonOptions));
        var envelope = new byte[4 + metadataBytes.Length + jpegBytes.Length];
        envelope[0] = (byte)((metadataBytes.Length >> 24) & 0xFF);
        envelope[1] = (byte)((metadataBytes.Length >> 16) & 0xFF);
        envelope[2] = (byte)((metadataBytes.Length >> 8) & 0xFF);
        envelope[3] = (byte)(metadataBytes.Length & 0xFF);
        Buffer.BlockCopy(metadataBytes, 0, envelope, 4, metadataBytes.Length);
        Buffer.BlockCopy(jpegBytes, 0, envelope, 4 + metadataBytes.Length, jpegBytes.Length);
        return envelope;
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
        MaybeLogMapSnapshot(Plugin.ClientState.TerritoryType, map);

        return new RemoteHudSnapshot
        {
            UpdateKind = "full",
            TimestampUtc = DateTime.UtcNow,
            AccountId = identity.AccountId,
            CharacterName = identity.CharacterName,
            WorldName = identity.WorldName,
            EntityId = localPlayer.EntityId,
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
            Position = new Vector3Snapshot(localPlayer.Position.X, localPlayer.Position.Y, localPlayer.Position.Z, localPlayer.Rotation),
            Player = new PlayerStatsSnapshot(localPlayer.CurrentHp, localPlayer.MaxHp, localPlayer.CurrentMp, MaxMana, localPlayer.Level),
            RaceId = GetCustomizeValue(localPlayer, CustomizeRaceIndex),
            TribeId = GetCustomizeValue(localPlayer, CustomizeTribeIndex),
            Policy = BuildRemoteControlPolicySnapshot(),
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
        MaybeLogMapSnapshot(Plugin.ClientState.TerritoryType, map);

        return new RemoteHudSnapshot
        {
            UpdateKind = "position",
            TimestampUtc = DateTime.UtcNow,
            AccountId = identity.AccountId,
            CharacterName = identity.CharacterName,
            WorldName = identity.WorldName,
            EntityId = localPlayer.EntityId,
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
            Position = new Vector3Snapshot(localPlayer.Position.X, localPlayer.Position.Y, localPlayer.Position.Z, localPlayer.Rotation),
            Player = new PlayerStatsSnapshot(localPlayer.CurrentHp, localPlayer.MaxHp, localPlayer.CurrentMp, MaxMana, localPlayer.Level),
            RaceId = GetCustomizeValue(localPlayer, CustomizeRaceIndex),
            TribeId = GetCustomizeValue(localPlayer, CustomizeTribeIndex),
            Policy = BuildRemoteControlPolicySnapshot(),
            Combat = BuildCombatSnapshot(localPlayer),
        };
    }

    private RemoteControlPolicySnapshot BuildRemoteControlPolicySnapshot()
    {
        return new RemoteControlPolicySnapshot
        {
            AllowEchoCommands = plugin.Configuration.AllowWebEchoCommands,
            AllowScreenshotRequests = plugin.Configuration.AllowWebScreenshotRequests,
            AllowCctvStreaming = plugin.Configuration.AllowWebCctvStreaming,
            AllowPluginFullBodyFallback = plugin.Configuration.EnablePluginFullBodyFallback,
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
            var worldName = member.World.IsValid ? member.World.Value.Name.ToString() : string.Empty;
            var contentId = member.ContentId > 0 ? ((ulong)member.ContentId).ToString("X16") : string.Empty;
            // Fallback Lodestone search shape if any future direct-ID shortcut ever becomes unreliable:
            // var searchUrl =
            //     $"https://na.finalfantasyxiv.com/lodestone/character/?q={Uri.EscapeDataString(originalName)}&worldname={Uri.EscapeDataString(worldName)}";

            members.Add(new RemotePartyMemberSnapshot
            {
                Slot = i + 1,
                ContentId = contentId,
                Name = originalName,
                WorldName = worldName,
                EntityId = character?.EntityId,
                KrangledName = KrangleService.KrangleName(string.IsNullOrWhiteSpace(worldName) ? originalName : $"{originalName}@{worldName}"),
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
                    : new Vector3Snapshot(character.Position.X, character.Position.Y, character.Position.Z, character.Rotation),
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
            Position = new Vector3Snapshot(hostile.Position.X, hostile.Position.Y, hostile.Position.Z, hostile.Rotation),
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
        lastFailureLogUtc = DateTime.MinValue;
        lastAttemptFailed = false;
        suppressedFailureLogCount = 0;
        lastError = null;
    }

    private void MaybeLogMapSnapshot(uint territoryId, RemoteMapSnapshot? map)
    {
        var signature = map == null
            ? $"territory:{territoryId}|map:none"
            : $"territory:{territoryId}|map:{map.MapId}|offset:{map.OffsetX},{map.OffsetY}|size:{map.SizeFactor}|texture:{map.TexturePath}";

        if (string.Equals(signature, lastLoggedMapSnapshotSignature, StringComparison.Ordinal))
            return;

        lastLoggedMapSnapshotSignature = signature;
        if (map == null)
        {
            Plugin.Log.Information("[TTSL] Map snapshot unresolved for territory {TerritoryId}.", territoryId);
            return;
        }

        Plugin.Log.Information(
            "[TTSL] Map snapshot resolved: territory {TerritoryId}, map {MapId}, offsetX {OffsetX}, offsetY {OffsetY}, sizeFactor {SizeFactor}, texture {TexturePath}.",
            territoryId,
            map.MapId,
            map.OffsetX,
            map.OffsetY,
            map.SizeFactor,
            map.TexturePath ?? "(none)");
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
        var resolvedTerritoryId = territoryId;
        var mapId = Plugin.ClientState.MapId;
        short offsetX = 0;
        short offsetY = 0;
        ushort sizeFactor = 0;
        var texturePathCandidates = BuildMapTexturePathCandidates(ref resolvedTerritoryId, ref mapId, ref offsetX, ref offsetY, ref sizeFactor);

        try
        {
            var mapSheet = Plugin.DataManager.GetExcelSheet<Map>();
            Map? resolvedMapRow = null;
            if (mapSheet != null && TryResolveMapRowFromCandidates(mapSheet, texturePathCandidates, out var candidateMapRow))
            {
                resolvedMapRow = candidateMapRow;
            }
            else if (mapId != 0 && mapSheet != null && mapSheet.TryGetRow(mapId, out var directMapRow))
            {
                resolvedMapRow = directMapRow;
            }

            if (resolvedMapRow != null)
            {
                var mapRow = resolvedMapRow.Value;
                mapId = mapRow.RowId;
                AddMapTextureCandidatesFromPathLike(texturePathCandidates, mapRow.Id.ToString());
                offsetX = mapRow.OffsetX;
                offsetY = mapRow.OffsetY;
                sizeFactor = mapRow.SizeFactor;
            }

            var territorySheet = Plugin.DataManager.GetExcelSheet<TerritoryType>();
            if (resolvedTerritoryId != 0 && territorySheet != null && territorySheet.TryGetRow(resolvedTerritoryId, out var territory) && territory.Map.IsValid)
            {
                var map = territory.Map.Value;
                if (mapId == 0)
                    mapId = territory.Map.RowId;

                AddMapTextureCandidatesFromPathLike(texturePathCandidates, map.Id.ToString());
                if (resolvedMapRow == null && sizeFactor == 0)
                {
                    offsetX = map.OffsetX;
                    offsetY = map.OffsetY;
                    sizeFactor = map.SizeFactor;
                }
            }
        }
        catch
        {
        }

        if (mapId == 0 && texturePathCandidates.Count == 0)
            return null;

        if (sizeFactor == 0)
            sizeFactor = 100;

        return new RemoteMapSnapshot
        {
            MapId = mapId,
            TexturePath = texturePathCandidates.FirstOrDefault(),
            TexturePathCandidates = texturePathCandidates,
            OffsetX = offsetX,
            OffsetY = offsetY,
            SizeFactor = sizeFactor,
        };
    }

    private static unsafe List<string> BuildMapTexturePathCandidates(ref uint territoryId, ref uint mapId, ref short offsetX, ref short offsetY, ref ushort sizeFactor)
    {
        var candidates = new List<string>();
        AddAgentMapTextureCandidates(candidates, ref territoryId, ref mapId, ref offsetX, ref offsetY, ref sizeFactor);
        return candidates;
    }

    private static unsafe void AddAgentMapTextureCandidates(List<string> candidates, ref uint territoryId, ref uint mapId, ref short offsetX, ref short offsetY, ref ushort sizeFactor)
    {
        try
        {
            var agentMap = AgentMap.Instance();
            if (agentMap == null)
                return;

            var addedCurrentCandidates = AddAgentMapTextureCandidatesFromState(
                candidates,
                agentMap->CurrentMapBgPath.ToString(),
                agentMap->CurrentMapPath.ToString(),
                agentMap->CurrentTerritoryId,
                agentMap->CurrentMapId,
                agentMap->CurrentOffsetX,
                agentMap->CurrentOffsetY,
                NormalizeMapSizeFactor(agentMap->CurrentMapSizeFactor),
                ref territoryId,
                ref mapId,
                ref offsetX,
                ref offsetY,
                ref sizeFactor);

            if (!addedCurrentCandidates)
            {
                AddAgentMapTextureCandidatesFromState(
                    candidates,
                    agentMap->SelectedMapBgPath.ToString(),
                    agentMap->SelectedMapPath.ToString(),
                    agentMap->SelectedTerritoryId,
                    agentMap->SelectedMapId,
                    agentMap->SelectedOffsetX,
                    agentMap->SelectedOffsetY,
                    NormalizeMapSizeFactor(agentMap->SelectedMapSizeFactor),
                    ref territoryId,
                    ref mapId,
                    ref offsetX,
                    ref offsetY,
                    ref sizeFactor);
            }
        }
        catch
        {
        }
    }

    private static bool AddAgentMapTextureCandidatesFromState(
        List<string> candidates,
        string? backgroundPath,
        string? mapPath,
        uint stateTerritoryId,
        uint stateMapId,
        short stateOffsetX,
        short stateOffsetY,
        ushort stateSizeFactor,
        ref uint territoryId,
        ref uint mapId,
        ref short offsetX,
        ref short offsetY,
        ref ushort sizeFactor)
    {
        var candidateCountBefore = candidates.Count;
        AddMapTextureCandidatesFromPathLike(candidates, mapPath);
        AddMapTextureCandidatesFromPathLike(candidates, backgroundPath);
        if (candidateCountBefore == candidates.Count)
            return false;

        if (territoryId == 0 && stateTerritoryId != 0)
            territoryId = stateTerritoryId;

        if (mapId == 0 && stateMapId != 0)
            mapId = stateMapId;

        if (sizeFactor == 0 && stateSizeFactor > 0)
        {
            offsetX = stateOffsetX;
            offsetY = stateOffsetY;
            sizeFactor = stateSizeFactor;
        }

        return true;
    }

    private static ushort NormalizeMapSizeFactor(short rawSizeFactor)
    {
        return rawSizeFactor > 0 ? (ushort)rawSizeFactor : (ushort)0;
    }

    private static bool TryResolveMapRowFromCandidates(ExcelSheet<Map> mapSheet, IReadOnlyList<string> texturePathCandidates, out Map mapRow)
    {
        var pathSignatures = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var pathKeys = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var candidate in texturePathCandidates)
        {
            var pathSignature = NormalizeMapPathSignature(candidate);
            if (!string.IsNullOrWhiteSpace(pathSignature))
                pathSignatures.Add(pathSignature);

            var pathKey = ExtractMapPathKey(candidate);
            if (!string.IsNullOrWhiteSpace(pathKey))
                pathKeys.Add(pathKey);
        }

        Map? directoryFallback = null;
        foreach (var row in mapSheet)
        {
            var rowSignature = NormalizeMapPathSignature(row.Id.ToString());
            if (!string.IsNullOrWhiteSpace(rowSignature) && pathSignatures.Contains(rowSignature))
            {
                mapRow = row;
                return true;
            }

            var rowKey = ExtractMapPathKey(row.Id.ToString());
            if (!string.IsNullOrWhiteSpace(rowKey) && pathKeys.Contains(rowKey) && directoryFallback == null)
                directoryFallback = row;
        }

        if (directoryFallback != null)
        {
            mapRow = directoryFallback.Value;
            return true;
        }

        mapRow = default;
        return false;
    }

    private static string? ExtractMapPathKey(string? texturePath)
    {
        var signature = NormalizeMapPathSignature(texturePath);
        if (string.IsNullOrWhiteSpace(signature))
            return null;

        var lastSlashIndex = signature.LastIndexOf('/');
        if (lastSlashIndex <= 0)
            return null;

        return signature[..lastSlashIndex];
    }

    private static string NormalizeMapPathSignature(string? rawPath)
    {
        var normalized = NormalizeMapPathLike(rawPath);
        if (string.IsNullOrWhiteSpace(normalized))
            return string.Empty;

        if (normalized.StartsWith("ui/map/", StringComparison.OrdinalIgnoreCase))
            normalized = normalized["ui/map/".Length..];

        if (normalized.EndsWith(".tex", StringComparison.OrdinalIgnoreCase))
            normalized = normalized[..^4];

        return normalized.Trim('/').ToLowerInvariant();
    }

    private static void AddMapTextureCandidatesFromPathLike(List<string> candidates, string? rawPath)
    {
        var normalized = NormalizeMapPathLike(rawPath);
        if (string.IsNullOrWhiteSpace(normalized))
            return;

        if (normalized.EndsWith(".tex", StringComparison.OrdinalIgnoreCase))
        {
            TryAddUniqueMapTextureCandidate(candidates, normalized);
            return;
        }

        var signature = NormalizeMapPathSignature(normalized);
        if (string.IsNullOrWhiteSpace(signature))
            return;

        var lastSlashIndex = signature.LastIndexOf('/');
        var directory = lastSlashIndex >= 0 ? signature[..lastSlashIndex] : string.Empty;
        var leaf = lastSlashIndex >= 0 ? signature[(lastSlashIndex + 1)..] : signature;

        if (leaf.EndsWith("_m", StringComparison.OrdinalIgnoreCase) || leaf.EndsWith("_s", StringComparison.OrdinalIgnoreCase))
        {
            var variantStem = leaf[..^2];
            var prefix = string.IsNullOrWhiteSpace(directory) ? "ui/map" : $"ui/map/{directory}";
            TryAddUniqueMapTextureCandidate(candidates, $"{prefix}/{variantStem}_m.tex");
            TryAddUniqueMapTextureCandidate(candidates, $"{prefix}/{variantStem}_s.tex");
            return;
        }

        var generatedStem = signature.Replace("/", string.Empty, StringComparison.Ordinal);
        if (string.IsNullOrWhiteSpace(generatedStem))
            return;

        TryAddUniqueMapTextureCandidate(candidates, $"ui/map/{signature}/{generatedStem}_m.tex");
        TryAddUniqueMapTextureCandidate(candidates, $"ui/map/{signature}/{generatedStem}_s.tex");
    }

    private static string? NormalizeMapPathLike(string? rawPath)
    {
        if (string.IsNullOrWhiteSpace(rawPath))
            return null;

        var value = rawPath.Replace('\\', '/').Trim().Trim('\0');
        if (string.IsNullOrWhiteSpace(value))
            return null;

        var mapRootIndex = value.IndexOf("ui/map/", StringComparison.OrdinalIgnoreCase);
        if (mapRootIndex >= 0)
            value = value[mapRootIndex..];

        if (value.StartsWith("ui/map/", StringComparison.OrdinalIgnoreCase))
            return value.TrimStart('/').ToLowerInvariant();

        return value.Trim('/').ToLowerInvariant();
    }

    private static void TryAddUniqueMapTextureCandidate(List<string> candidates, string? candidate)
    {
        if (string.IsNullOrWhiteSpace(candidate))
            return;

        if (candidates.Contains(candidate, StringComparer.OrdinalIgnoreCase))
            return;

        candidates.Add(candidate);
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
        var requestUrl = string.Empty;

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
            requestUrl = $"{baseUrl}{path}";
            using var response = await httpClient.PostAsync(requestUrl, content, shutdownCts.Token).ConfigureAwait(false);
            if (!response.IsSuccessStatusCode)
            {
                var errorBody = await response.Content.ReadAsStringAsync(shutdownCts.Token).ConfigureAwait(false);
                RecordFailure($"HTTP {(int)response.StatusCode} from {baseUrl}{path}: {TrimForLog(errorBody)}");
                return;
            }

            var responseBody = await response.Content.ReadAsStringAsync(shutdownCts.Token).ConfigureAwait(false);
            if (string.Equals(path, "/api/update", StringComparison.Ordinal))
                await HandleUpdateResponseAsync(baseUrl, responseBody).ConfigureAwait(false);

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
        catch (TaskCanceledException ex)
        {
            RecordFailure(BuildTimeoutFailureMessage(requestUrl, ex));
        }
        catch (HttpRequestException ex)
        {
            RecordFailure(BuildRequestFailureMessage(requestUrl, ex));
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
        lastFailureLogUtc = DateTime.MinValue;
        suppressedFailureLogCount = 0;
        lastError = null;
    }

    private void RecordFailure(string error)
    {
        var now = DateTime.UtcNow;
        consecutiveFailureCount = Math.Min(consecutiveFailureCount + 1, 5);
        var backoff = CalculateBackoff(consecutiveFailureCount);
        nextAttemptUtc = now + backoff;
        statusText = $"Retrying in {Math.Max(1, (int)Math.Ceiling(backoff.TotalSeconds))}s";

        var errorChanged = !string.Equals(lastError, error, StringComparison.Ordinal);
        var logRepeatedFailure = lastFailureLogUtc == DateTime.MinValue || now - lastFailureLogUtc >= RepeatedFailureLogInterval;
        if (!lastAttemptFailed || errorChanged || logRepeatedFailure)
        {
            var suppressedCount = errorChanged ? 0 : suppressedFailureLogCount;
            if (suppressedCount > 0)
            {
                Plugin.Log.Warning("[TTSL] Remote HUD publishing failed: {Error}. Backing off for {Seconds}s. Suppressed {SuppressedCount} repeated failures.",
                    error,
                    Math.Max(1, (int)Math.Ceiling(backoff.TotalSeconds)),
                    suppressedCount);
            }
            else
            {
                Plugin.Log.Warning("[TTSL] Remote HUD publishing failed: {Error}. Backing off for {Seconds}s.",
                    error,
                    Math.Max(1, (int)Math.Ceiling(backoff.TotalSeconds)));
            }

            lastFailureLogUtc = now;
            suppressedFailureLogCount = 0;
        }
        else
        {
            suppressedFailureLogCount++;
        }

        lastAttemptFailed = true;
        lastError = error;
    }

    private static string BuildTimeoutFailureMessage(string requestUrl, Exception ex)
    {
        var target = string.IsNullOrWhiteSpace(requestUrl) ? "remote HUD server" : requestUrl;
        return $"Timed out after {HttpTimeout.TotalSeconds:F0}s while contacting {target}. Server may be slow or offline. {ex.Message}";
    }

    private static string BuildRequestFailureMessage(string requestUrl, HttpRequestException ex)
    {
        if (string.IsNullOrWhiteSpace(requestUrl))
            return ex.Message;

        return $"Could not reach {requestUrl}: {ex.Message}";
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

    private async Task HandleUpdateResponseAsync(string baseUrl, string responseBody)
    {
        if (string.IsNullOrWhiteSpace(responseBody))
            return;

        RemoteUpdateResponse? updateResponse;
        try
        {
            updateResponse = JsonSerializer.Deserialize<RemoteUpdateResponse>(responseBody, JsonOptions);
        }
        catch (Exception ex)
        {
            Plugin.Log.Debug(ex, "[TTSL] Ignoring invalid remote HUD update response body.");
            return;
        }

        if (updateResponse?.Actions == null || updateResponse.Actions.Count == 0)
            return;

        foreach (var action in updateResponse.Actions)
        {
            if (action == null || string.IsNullOrWhiteSpace(action.ActionType))
                continue;

            try
            {
                switch (action.ActionType.Trim().ToLowerInvariant())
                {
                    case "echocommand":
                        QueueWebChatInput(action.Text);
                        break;

                    case "requestscreenshot":
                        await CaptureAndUploadScreenshotAsync(baseUrl, action.ActionId, action.CaptureMode, action.CaptureQuality).ConfigureAwait(false);
                        break;

                    case "requestcharactervisual":
                        QueueCharacterVisualCapture(baseUrl, action);
                        break;

                    default:
                        Plugin.Log.Warning("[TTSL] Ignoring unsupported web action type: {ActionType}", action.ActionType);
                        break;
                }
            }
            catch (Exception ex)
            {
                Plugin.Log.Warning(ex, "[TTSL] Failed to process web action {ActionType}.", action.ActionType);
            }
        }
    }

    private void QueueCharacterVisualCapture(string baseUrl, RemoteAction action)
    {
        if (!plugin.Configuration.EnablePluginFullBodyFallback)
        {
            Plugin.Log.Information("[TTSL] Ignored plugin full-body fallback request because the local policy is off.");
            return;
        }

        var targetName = string.IsNullOrWhiteSpace(action.TargetCharacterName)
            ? lastIdentity?.CharacterName ?? string.Empty
            : action.TargetCharacterName.Trim();
        var targetWorld = string.IsNullOrWhiteSpace(action.TargetWorldName)
            ? lastIdentity?.WorldName ?? string.Empty
            : action.TargetWorldName.Trim();
        if (string.IsNullOrWhiteSpace(targetName) || string.IsNullOrWhiteSpace(targetWorld))
        {
            Plugin.Log.Information("[TTSL] Ignored plugin full-body fallback request without a target character/world.");
            return;
        }

        pendingCharacterVisualCaptures.Enqueue(new PendingCharacterVisualCapture
        {
            BaseUrl = baseUrl,
            ActionId = string.IsNullOrWhiteSpace(action.ActionId) ? Guid.NewGuid().ToString("N") : action.ActionId,
            TargetCharacterName = targetName,
            TargetWorldName = targetWorld,
            TargetContentId = action.TargetContentId ?? string.Empty,
            TargetEntityId = action.TargetEntityId,
            RequestedAtUtc = DateTime.UtcNow,
        });
        Plugin.Log.Information("[TTSL] Queued plugin full-body fallback capture for {CharacterKey}.", $"{targetName}@{targetWorld}");
    }

    private void ProcessCharacterVisualCaptures()
    {
        if (!plugin.Configuration.EnablePluginFullBodyFallback)
        {
            activeCharacterVisualCapture = null;
            while (pendingCharacterVisualCaptures.TryDequeue(out _))
            {
            }
            return;
        }

        activeCharacterVisualCapture ??= pendingCharacterVisualCaptures.TryDequeue(out var next)
            ? next
            : null;
        var request = activeCharacterVisualCapture;
        if (request == null)
            return;

        if ((DateTime.UtcNow - request.RequestedAtUtc).TotalMilliseconds > CharacterVisualCaptureTimeoutMs)
        {
            Plugin.Log.Warning("[TTSL] Plugin full-body fallback timed out for {CharacterKey}.", request.CharacterKey);
            activeCharacterVisualCapture = null;
            return;
        }

        if (!TryResolveCharacterVisualTarget(request, out var entityId, out var targetReason))
        {
            Plugin.Log.Information("[TTSL] Plugin full-body fallback cannot resolve {CharacterKey}: {Reason}", request.CharacterKey, targetReason);
            activeCharacterVisualCapture = null;
            return;
        }

        if (!RequestOrConfirmInspectTarget(request, entityId, out var inspectReason))
        {
            request.LastStatus = inspectReason;
            return;
        }

        if (!TryCaptureInspectPreviewPng(entityId, out var imageBytes, out var captureReason))
        {
            request.LastStatus = captureReason;
            return;
        }

        activeCharacterVisualCapture = null;
        _ = UploadCharacterVisualAsync(request, imageBytes);
    }

    private static bool TryResolveCharacterVisualTarget(PendingCharacterVisualCapture request, out uint entityId, out string reason)
    {
        if (request.TargetEntityId is > 0)
        {
            entityId = request.TargetEntityId.Value;
            reason = string.Empty;
            return true;
        }

        var localPlayer = Plugin.ObjectTable.LocalPlayer;
        if (localPlayer != null &&
            string.Equals(localPlayer.Name.TextValue, request.TargetCharacterName, StringComparison.OrdinalIgnoreCase) &&
            string.Equals(localPlayer.HomeWorld.Value.Name.ToString(), request.TargetWorldName, StringComparison.OrdinalIgnoreCase))
        {
            entityId = localPlayer.EntityId;
            reason = string.Empty;
            return entityId != 0;
        }

        var partyMember = Plugin.PartyList
            .Where(member => member != null)
            .FirstOrDefault(member =>
                string.Equals(member!.Name.TextValue, request.TargetCharacterName, StringComparison.OrdinalIgnoreCase) &&
                (string.IsNullOrWhiteSpace(request.TargetWorldName) ||
                 !member.World.IsValid ||
                 string.Equals(member.World.Value.Name.ToString(), request.TargetWorldName, StringComparison.OrdinalIgnoreCase)));
        if (partyMember != null)
        {
            var character = FindPartyCharacter(partyMember.Address, request.TargetCharacterName);
            if (character != null && character.EntityId != 0)
            {
                entityId = character.EntityId;
                reason = string.Empty;
                return true;
            }
        }

        entityId = 0;
        reason = "target is not a visible local or party character.";
        return false;
    }

    private static unsafe bool RequestOrConfirmInspectTarget(PendingCharacterVisualCapture request, uint entityId, out string reason)
    {
        var agent = AgentInspect.Instance();
        if (agent == null)
        {
            reason = "AgentInspect is unavailable.";
            return false;
        }

        if (!request.InspectRequested)
        {
            agent->ExamineCharacter(entityId);
            request.InspectRequested = true;
            request.InspectRequestedAtUtc = DateTime.UtcNow;
            reason = "CharacterInspect request sent.";
            return false;
        }

        if (agent->CurrentEntityId != entityId)
        {
            reason = $"Waiting for CharacterInspect target 0x{entityId:X8}.";
            return false;
        }

        reason = string.Empty;
        return true;
    }

    private static unsafe bool TryCaptureInspectPreviewPng(uint entityId, out byte[] imageBytes, out string reason)
    {
        imageBytes = Array.Empty<byte>();
        var agent = AgentInspect.Instance();
        if (agent == null || agent->CurrentEntityId != entityId)
        {
            reason = "CharacterInspect has not settled on the requested target.";
            return false;
        }

        var charaView = &agent->CharaView;
        if (!charaView->CharacterLoaded && !charaView->CharacterDataCopied && charaView->State == 0)
        {
            reason = "CharacterInspect model data is not ready yet.";
            return false;
        }

        var addonPointer = Plugin.GameGui.GetAddonByName("CharacterInspect");
        if (addonPointer.IsNull)
            addonPointer = Plugin.GameGui.GetAddonByName("CharacterInspect", 1);
        var addonAddress = addonPointer.Address;
        if (addonAddress == nint.Zero)
        {
            reason = "CharacterInspect addon is not visible yet.";
            return false;
        }

        var addon = (AddonCharacterInspect*)addonAddress;
        var previewComponent = addon->PreviewController.Component;
        if (previewComponent == null || previewComponent->OwnerNode == null)
        {
            reason = "CharacterInspect preview component is not ready.";
            return false;
        }

        var ownerNode = &previewComponent->OwnerNode->AtkResNode;
        if (!ownerNode->IsVisible() || ownerNode->Width == 0 || ownerNode->Height == 0)
        {
            reason = "CharacterInspect preview node is not visible yet.";
            return false;
        }

        var windowHandle = ResolveGameWindowHandleForCapture();
        if (windowHandle == nint.Zero)
        {
            reason = "Could not resolve the game window handle.";
            return false;
        }

        if (!GetClientRect(windowHandle, out var clientRect))
        {
            reason = "Could not read the game client bounds.";
            return false;
        }

        var clientWidth = Math.Max(0, clientRect.Right - clientRect.Left);
        var clientHeight = Math.Max(0, clientRect.Bottom - clientRect.Top);
        var scaleX = NormalizeInspectPreviewScale(ownerNode->ScaleX);
        var scaleY = NormalizeInspectPreviewScale(ownerNode->ScaleY);
        var requestedX = RoundToInt(addon->X) + RoundToInt(ownerNode->X * scaleX);
        var requestedY = RoundToInt(addon->Y) + RoundToInt(ownerNode->Y * scaleY);
        var captureWidth = Math.Max(1, RoundToInt(ownerNode->Width * scaleX));
        var scaledPreviewHeight = Math.Max(1, RoundToInt(ownerNode->Height * scaleY));
        var sourceHeight = Math.Max(scaledPreviewHeight, scaledPreviewHeight * InspectPreviewExpandedHeightMultiplier);
        var topTrim = RoundToInt(sourceHeight * InspectPreviewTopTrimFraction);
        var bottomTrim = RoundToInt(sourceHeight * InspectPreviewBottomTrimFraction);
        var preferredY = requestedY + topTrim;
        var preferredHeight = Math.Max(1, sourceHeight - topTrim - bottomTrim);
        if (!TryClampClientRect(requestedX, preferredY, captureWidth, preferredHeight, clientWidth, clientHeight, out var captureRect))
        {
            reason = "CharacterInspect preview crop landed outside the game client area.";
            return false;
        }

        var screenPoint = new Win32Point(captureRect.X, captureRect.Y);
        if (!ClientToScreen(windowHandle, ref screenPoint))
        {
            reason = "Could not translate CharacterInspect preview bounds to screen coordinates.";
            return false;
        }

        using var bitmap = new Bitmap(captureRect.Width, captureRect.Height, PixelFormat.Format32bppArgb);
        using (var graphics = Graphics.FromImage(bitmap))
        {
            graphics.CopyFromScreen(
                screenPoint.X,
                screenPoint.Y,
                0,
                0,
                captureRect.Size,
                CopyPixelOperation.SourceCopy);
        }

        imageBytes = EncodeBitmapAsPng(bitmap);
        reason = string.Empty;
        return imageBytes.Length > 0;
    }

    private async Task UploadCharacterVisualAsync(PendingCharacterVisualCapture request, byte[] imageBytes)
    {
        try
        {
            var identity = lastIdentity ?? GetCurrentIdentity();
            if (identity == null)
                throw new InvalidOperationException("Cannot upload a character visual without a resolved local identity.");

            var payload = new CharacterVisualUploadRequest
            {
                AccountId = identity.AccountId,
                CharacterName = identity.CharacterName,
                WorldName = identity.WorldName,
                TargetCharacterName = request.TargetCharacterName,
                TargetWorldName = request.TargetWorldName,
                ActionId = request.ActionId,
                CapturedAtUtc = DateTime.UtcNow,
                ContentType = "image/png",
                CaptureKind = "portrait",
                ImageBase64 = Convert.ToBase64String(imageBytes),
            };

            var json = JsonSerializer.Serialize(payload, JsonOptions);
            using var content = new StringContent(json, Encoding.UTF8, "application/json");
            using var response = await httpClient.PostAsync($"{request.BaseUrl}/api/upload-character-visual", content, shutdownCts.Token).ConfigureAwait(false);
            if (!response.IsSuccessStatusCode)
            {
                var errorBody = await response.Content.ReadAsStringAsync(shutdownCts.Token).ConfigureAwait(false);
                throw new InvalidOperationException($"HTTP {(int)response.StatusCode} from {request.BaseUrl}/api/upload-character-visual: {TrimForLog(errorBody)}");
            }

            Plugin.Log.Information("[TTSL] Uploaded plugin full-body fallback for {CharacterKey}.", request.CharacterKey);
        }
        catch (OperationCanceledException) when (shutdownCts.IsCancellationRequested || isDisposing)
        {
        }
        catch (Exception ex)
        {
            Plugin.Log.Warning(ex, "[TTSL] Failed to upload plugin full-body fallback for {CharacterKey}.", request.CharacterKey);
        }
    }


    private void QueueWebChatInput(string? text)
    {
        if (!plugin.Configuration.AllowWebEchoCommands)
        {
            Plugin.Log.Information("[TTSL] Ignored web text/slash command because the local policy does not allow it.");
            return;
        }

        var sanitized = SanitizeWebChatInput(text);
        if (string.IsNullOrWhiteSpace(sanitized))
        {
            Plugin.Log.Information("[TTSL] Ignored empty web text/slash payload.");
            return;
        }

        pendingWebChatInputs.Enqueue(sanitized);
    }

    private void ProcessQueuedWebChatInputs()
    {
        while (pendingWebChatInputs.TryDequeue(out var input))
        {
            try
            {
                ExecuteWebChatInput(input);
            }
            catch (Exception ex)
            {
                Plugin.Log.Warning(ex, "[TTSL] Failed to execute queued web text/slash input.");
            }
        }
    }

    private void ExecuteWebChatInput(string input)
    {
        if (!plugin.Configuration.AllowWebEchoCommands)
        {
            Plugin.Log.Information("[TTSL] Discarded queued web text/slash input because the local policy was disabled before execution.");
            return;
        }

        var commandText = input.StartsWith("/", StringComparison.Ordinal)
            ? input
            : $"/echo [TTSL Web] {input}";

        if (!TryProcessChatBoxEntry(commandText))
            Plugin.Log.Warning("[TTSL] Failed to dispatch web text/slash input through the game chat box.");
    }

    private static unsafe bool TryProcessChatBoxEntry(string commandText)
    {
        var uiModule = UIModule.Instance();
        if (uiModule == null)
        {
            Plugin.Log.Warning("[TTSL] UIModule is null; cannot dispatch web chat input.");
            return false;
        }

        var bytes = Encoding.UTF8.GetBytes(commandText);
        var utf8String = Utf8String.FromSequence(bytes);
        uiModule->ProcessChatBoxEntry(utf8String, nint.Zero);
        return true;
    }

    private async Task CaptureAndUploadScreenshotAsync(string baseUrl, string? actionId, string? captureMode, string? captureQuality)
    {
        var isCctv = string.Equals(captureMode, "cctv", StringComparison.OrdinalIgnoreCase);
        if (isCctv)
        {
            if (!plugin.Configuration.AllowWebCctvStreaming)
            {
                Plugin.Log.Information("[TTSL] Ignored web CCTV request because the local policy does not allow it.");
                return;
            }
        }
        else if (!plugin.Configuration.AllowWebScreenshotRequests)
        {
            Plugin.Log.Information("[TTSL] Ignored web screenshot request because the local policy does not allow it.");
            return;
        }

        string contentType;
        byte[] imageBytes;
        string? resolvedQuality = null;
        CctvCaptureStatus? captureStatus = null;
        using (var frame = await CaptureGameWindowFrameAsync(isCctv, shutdownCts.Token).ConfigureAwait(false))
        {
            if (isCctv)
            {
                var preset = ResolveCctvPreset(captureQuality);
                resolvedQuality = preset.Name;
                captureStatus = frame.CaptureStatus;
                using var scaledBitmap = ResizeBitmap(frame.Bitmap, preset.Scale);
                imageBytes = EncodeBitmapAsJpeg(scaledBitmap, preset.JpegQuality);
                contentType = "image/jpeg";
            }
            else
            {
                imageBytes = EncodeBitmapAsPng(frame.Bitmap);
                contentType = "image/png";
            }
        }

        var identity = lastIdentity ?? GetCurrentIdentity();
        if (identity == null)
            throw new InvalidOperationException("Cannot upload a web screenshot without a resolved local identity.");

        var payload = new ScreenshotUploadRequest
        {
            AccountId = identity.AccountId,
            CharacterName = identity.CharacterName,
            WorldName = identity.WorldName,
            ActionId = string.IsNullOrWhiteSpace(actionId) ? Guid.NewGuid().ToString("N") : actionId,
            CapturedAtUtc = DateTime.UtcNow,
            ContentType = contentType,
            CaptureMode = isCctv ? "cctv" : "screenshot",
            CaptureQuality = resolvedQuality,
            CaptureStatus = captureStatus,
            FileName = isCctv
                ? $"ttsl_cctv_{DateTime.UtcNow:yyyyMMdd_HHmmss}.jpg"
                : $"ttsl_{DateTime.UtcNow:yyyyMMdd_HHmmss}.png",
            ImageBase64 = Convert.ToBase64String(imageBytes),
        };

        var json = JsonSerializer.Serialize(payload, JsonOptions);
        using var content = new StringContent(json, Encoding.UTF8, "application/json");
        using var response = await httpClient.PostAsync($"{baseUrl}/api/upload-screenshot", content, shutdownCts.Token).ConfigureAwait(false);
        if (!response.IsSuccessStatusCode)
        {
            var errorBody = await response.Content.ReadAsStringAsync(shutdownCts.Token).ConfigureAwait(false);
            throw new InvalidOperationException($"HTTP {(int)response.StatusCode} from {baseUrl}/api/upload-screenshot: {TrimForLog(errorBody)}");
        }

        if (!isCctv)
            Plugin.Log.Information("[TTSL] Uploaded requested web screenshot for {CharacterKey}.", $"{identity.CharacterName}@{identity.WorldName}");
    }

    private static string SanitizeWebChatInput(string? text)
    {
        if (string.IsNullOrWhiteSpace(text))
            return string.Empty;

        var normalized = text.Replace("\r", " ", StringComparison.Ordinal)
            .Replace("\n", " ", StringComparison.Ordinal)
            .Trim();
        if (normalized.Length > 220)
            normalized = normalized[..220];

        return normalized;
    }

    private async Task<GameCaptureFrame> CaptureGameWindowFrameAsync(bool isCctv, CancellationToken token)
    {
        if (!TryResolveBestCaptureSelection(out var selection, out var failureReason))
        {
            var windowHandle = ResolveGameWindowHandleForCapture();
            if (isCctv && windowHandle != nint.Zero && IsIconic(windowHandle))
                return CreateMinimizedCctvFrame(Rectangle.Empty);

            throw new InvalidOperationException($"FFXIV game-window capture selection is invalid. {failureReason}");
        }

        var selectionSignature =
            $"{selection.Source}|0x{selection.WindowHandle.ToInt64():X}|{selection.BoundsKind}|{selection.Bounds.Width}x{selection.Bounds.Height}";
        if (!isCctv &&
            !string.Equals(selectionSignature, lastLoggedCaptureSelectionSignature, StringComparison.Ordinal))
        {
            var message = string.Equals(selection.Source, "ProcessMainWindow", StringComparison.Ordinal)
                ? "[TTSL] Web capture selected {Source} HWND 0x{Handle:X} using {BoundsKind} bounds {Width}x{Height}."
                : "[TTSL] Web capture fell back to {Source} HWND 0x{Handle:X} using {BoundsKind} bounds {Width}x{Height}.";
            Plugin.Log.Information(
                message,
                selection.Source,
                selection.WindowHandle.ToInt64(),
                selection.BoundsKind,
                selection.Bounds.Width,
                selection.Bounds.Height);
            lastLoggedCaptureSelectionSignature = selectionSignature;
        }

        var windowState = GetCaptureWindowState(selection.WindowHandle);
        if (isCctv && string.Equals(windowState, "minimized", StringComparison.Ordinal))
            return CreateMinimizedCctvFrame(selection.Bounds);

        if (isCctv)
        {
            try
            {
                var wgcBitmap = await windowsGraphicsCapture
                    .CaptureAsync(selection.WindowHandle, TimeSpan.FromMilliseconds(500), token)
                    .ConfigureAwait(false);
                var captureStatus = new CctvCaptureStatus
                {
                    Backend = "wgc",
                    WindowState = windowState,
                    IsStale = false,
                    Message = "Live window capture.",
                };
                RememberLiveCctvFrame(wgcBitmap, captureStatus);
                return new GameCaptureFrame(wgcBitmap, captureStatus);
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                if ((DateTime.UtcNow - lastWgcCaptureErrorLogUtc).TotalSeconds >= 10)
                {
                    lastWgcCaptureErrorLogUtc = DateTime.UtcNow;
                    Plugin.Log.Warning(ex, "[TTSL] WGC CCTV capture failed; falling back to GDI.");
                }
            }
        }

        var bitmap = CaptureGdiBitmap(selection);
        var status = new CctvCaptureStatus
        {
            Backend = "gdi",
            WindowState = windowState,
            IsStale = false,
            Message = isCctv ? "GDI fallback capture." : "Desktop capture.",
        };
        if (isCctv)
            RememberLiveCctvFrame(bitmap, status);

        return new GameCaptureFrame(bitmap, status);
    }

    private static Bitmap CaptureGdiBitmap(CaptureSelection selection)
    {
        var bitmap = new Bitmap(selection.Bounds.Width, selection.Bounds.Height, PixelFormat.Format32bppArgb);
        using (var graphics = Graphics.FromImage(bitmap))
        {
            graphics.CopyFromScreen(selection.Bounds.X, selection.Bounds.Y, 0, 0, selection.Bounds.Size, CopyPixelOperation.SourceCopy);
        }

        return bitmap;
    }

    private GameCaptureFrame CreateMinimizedCctvFrame(Rectangle fallbackBounds)
    {
        lock (cctvFrameCacheLock)
        {
            if (lastGoodCctvBitmap != null)
            {
                var reusedStatus = new CctvCaptureStatus
                {
                    Backend = lastGoodCctvStatus?.Backend ?? "wgc",
                    WindowState = "minimized",
                    IsStale = true,
                    Message = "FFXIV minimized; showing last frame.",
                };
                return new GameCaptureFrame((Bitmap)lastGoodCctvBitmap.Clone(), reusedStatus);
            }
        }

        var width = fallbackBounds.Width >= MinimumReasonableCaptureWidth ? fallbackBounds.Width : 1280;
        var height = fallbackBounds.Height >= MinimumReasonableCaptureHeight ? fallbackBounds.Height : 720;
        var status = new CctvCaptureStatus
        {
            Backend = "wgc",
            WindowState = "minimized",
            IsStale = true,
            Message = "FFXIV minimized; waiting for first live frame.",
        };
        return new GameCaptureFrame(CreateMinimizedPlaceholderBitmap(width, height), status);
    }

    private void RememberLiveCctvFrame(Bitmap bitmap, CctvCaptureStatus status)
    {
        lock (cctvFrameCacheLock)
        {
            lastGoodCctvBitmap?.Dispose();
            lastGoodCctvBitmap = (Bitmap)bitmap.Clone();
            lastGoodCctvStatus = status;
        }
    }

    private void ClearCctvFrameCache()
    {
        lock (cctvFrameCacheLock)
        {
            lastGoodCctvBitmap?.Dispose();
            lastGoodCctvBitmap = null;
            lastGoodCctvStatus = null;
        }
    }

    private static Bitmap CreateMinimizedPlaceholderBitmap(int width, int height)
    {
        var bitmap = new Bitmap(width, height, PixelFormat.Format24bppRgb);
        using var graphics = Graphics.FromImage(bitmap);
        graphics.Clear(Color.FromArgb(7, 12, 18));
        using var borderPen = new Pen(Color.FromArgb(70, 110, 132));
        graphics.DrawRectangle(borderPen, 0, 0, width - 1, height - 1);
        var baseFont = SystemFonts.MessageBoxFont ?? SystemFonts.DefaultFont;
        using var font = new Font(baseFont.FontFamily, Math.Max(18f, Math.Min(width, height) / 18f), FontStyle.Bold);
        using var brush = new SolidBrush(Color.FromArgb(216, 226, 232));
        const string text = "(MINIMIZED)";
        var size = graphics.MeasureString(text, font);
        graphics.DrawString(text, font, brush, (width - size.Width) / 2f, (height - size.Height) / 2f);
        return bitmap;
    }

    private static string GetCaptureWindowState(nint windowHandle)
    {
        if (windowHandle == nint.Zero)
            return "unknown";

        if (IsIconic(windowHandle))
            return "minimized";

        return IsWindowVisible(windowHandle) ? "visible" : "unknown";
    }

    private static byte[] EncodeBitmapAsPng(Image image)
    {
        using var memoryStream = new MemoryStream();
        image.Save(memoryStream, ImageFormat.Png);
        return memoryStream.ToArray();
    }

    private static byte[] EncodeBitmapAsJpeg(Image image, long quality)
    {
        var encoder = ImageCodecInfo.GetImageEncoders().FirstOrDefault(candidate =>
            string.Equals(candidate.MimeType, "image/jpeg", StringComparison.OrdinalIgnoreCase));
        if (encoder == null)
            throw new InvalidOperationException("JPEG encoder is not available for CCTV capture.");

        using var encoderParameters = new EncoderParameters(1);
        encoderParameters.Param[0] = new EncoderParameter(System.Drawing.Imaging.Encoder.Quality, Math.Clamp(quality, 25L, 95L));
        using var memoryStream = new MemoryStream();
        image.Save(memoryStream, encoder, encoderParameters);
        return memoryStream.ToArray();
    }

    private static Bitmap ResizeBitmap(Bitmap source, float scale)
    {
        var clampedScale = Math.Clamp(scale, 0.2f, 1f);
        if (Math.Abs(clampedScale - 1f) < 0.001f)
            return (Bitmap)source.Clone();

        var width = Math.Max(1, (int)Math.Round(source.Width * clampedScale));
        var height = Math.Max(1, (int)Math.Round(source.Height * clampedScale));
        var scaled = new Bitmap(width, height, PixelFormat.Format24bppRgb);
        using var graphics = Graphics.FromImage(scaled);
        graphics.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
        graphics.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.HighQuality;
        graphics.PixelOffsetMode = System.Drawing.Drawing2D.PixelOffsetMode.HighQuality;
        graphics.DrawImage(source, new Rectangle(0, 0, width, height));
        return scaled;
    }

    private static CctvCapturePreset ResolveCctvPreset(string? quality)
    {
        return string.Equals(quality, "low", StringComparison.OrdinalIgnoreCase)
            ? new CctvCapturePreset("low", 0.5f, 60L, 3)
            : string.Equals(quality, "high", StringComparison.OrdinalIgnoreCase)
                ? new CctvCapturePreset("high", 1f, 90L, 10)
                : new CctvCapturePreset("medium", 0.75f, 78L, 6);
    }

    private static bool TryResolveBestCaptureSelection(out CaptureSelection selection, out string failureReason)
    {
        selection = null!;
        failureReason = "No capture-window candidates were available.";

        var diagnostics = new List<string>();
        var viableSelections = new List<CaptureSelection>();
        foreach (var candidate in EnumerateCaptureWindowCandidates())
        {
            if (TryResolveCaptureBounds(candidate.WindowHandle, out var bounds, out var boundsKind, out var reason))
            {
                viableSelections.Add(new CaptureSelection(candidate.WindowHandle, candidate.Source, bounds, boundsKind));
                diagnostics.Add($"{candidate.Source}=0x{candidate.WindowHandle.ToInt64():X} {boundsKind} {bounds.Width}x{bounds.Height}");
            }
            else
            {
                diagnostics.Add($"{candidate.Source}=0x{candidate.WindowHandle.ToInt64():X} failed ({reason})");
            }
        }

        var bestSelection = viableSelections
            .Where(candidate => candidate.Bounds.Width >= MinimumReasonableCaptureWidth &&
                                candidate.Bounds.Height >= MinimumReasonableCaptureHeight)
            .OrderByDescending(candidate => candidate.Bounds.Width * candidate.Bounds.Height)
            .FirstOrDefault();
        if (bestSelection != null)
        {
            selection = bestSelection;
            return true;
        }

        if (viableSelections.Count > 0)
        {
            var fallbackSelection = viableSelections
                .OrderByDescending(candidate => candidate.Bounds.Width * candidate.Bounds.Height)
                .First();
            failureReason =
                $"Best candidate was {fallbackSelection.Source} HWND 0x{fallbackSelection.WindowHandle.ToInt64():X} " +
                $"using {fallbackSelection.BoundsKind} bounds {fallbackSelection.Bounds.Width}x{fallbackSelection.Bounds.Height}, " +
                $"which is below the minimum {MinimumReasonableCaptureWidth}x{MinimumReasonableCaptureHeight}. " +
                $"Candidates: {string.Join(" | ", diagnostics)}";
            return false;
        }

        failureReason = $"No capture candidates resolved valid bounds. Candidates: {string.Join(" | ", diagnostics)}";
        return false;
    }

    private static List<WindowHandleCandidate> EnumerateCaptureWindowCandidates()
    {
        var currentProcessId = (uint)Process.GetCurrentProcess().Id;
        var seenHandles = new HashSet<nint>();
        var candidates = new List<WindowHandleCandidate>();

        void AddCandidate(nint windowHandle, string source)
        {
            if (windowHandle == nint.Zero || !seenHandles.Add(windowHandle))
                return;

            candidates.Add(new WindowHandleCandidate(windowHandle, source));
        }

        AddCandidate(Process.GetCurrentProcess().MainWindowHandle, "ProcessMainWindow");
        AddCandidate(Plugin.PluginInterface.UiBuilder.WindowHandlePtr, "UiBuilderWindowHandlePtr");

        EnumWindows((windowHandle, _) =>
        {
            if (!IsWindowVisible(windowHandle))
                return true;

            GetWindowThreadProcessId(windowHandle, out var windowProcessId);
            if (windowProcessId == currentProcessId)
                AddCandidate(windowHandle, "EnumWindows");

            return true;
        }, nint.Zero);

        return candidates;
    }

    private static bool TryResolveCaptureBounds(nint windowHandle, out Rectangle bounds, out string boundsKind, out string reason)
    {
        bounds = Rectangle.Empty;
        boundsKind = string.Empty;
        reason = string.Empty;

        if (GetClientRect(windowHandle, out var clientRect))
        {
            var topLeft = new Win32Point(0, 0);
            var bottomRight = new Win32Point(clientRect.Right, clientRect.Bottom);
            if (ClientToScreen(windowHandle, ref topLeft) && ClientToScreen(windowHandle, ref bottomRight))
            {
                var width = bottomRight.X - topLeft.X;
                var height = bottomRight.Y - topLeft.Y;
                if (width > 0 && height > 0)
                {
                    bounds = new Rectangle(topLeft.X, topLeft.Y, width, height);
                    boundsKind = "client";
                    return true;
                }

                reason = $"Client rect resolved to {width}x{height} for HWND 0x{windowHandle.ToInt64():X}.";
            }
            else
            {
                reason = $"ClientToScreen failed for HWND 0x{windowHandle.ToInt64():X}.";
            }
        }

        if (GetWindowRect(windowHandle, out var windowRect))
        {
            var width = windowRect.Right - windowRect.Left;
            var height = windowRect.Bottom - windowRect.Top;
            if (width > 0 && height > 0)
            {
                bounds = new Rectangle(windowRect.Left, windowRect.Top, width, height);
                boundsKind = "window";
                return true;
            }

            reason = $"{reason} Window rect resolved to {width}x{height}.";
        }

        return false;
    }

    private static nint ResolveGameWindowHandleForCapture()
    {
        var handle = Process.GetCurrentProcess().MainWindowHandle;
        if (handle != nint.Zero)
            return handle;

        var currentProcessId = (uint)Process.GetCurrentProcess().Id;
        nint discoveredHandle = nint.Zero;
        EnumWindows((windowHandle, _) =>
        {
            if (!IsWindowVisible(windowHandle))
                return true;

            GetWindowThreadProcessId(windowHandle, out var processId);
            if (processId != currentProcessId)
                return true;

            discoveredHandle = windowHandle;
            return false;
        }, nint.Zero);
        return discoveredHandle;
    }

    private static int RoundToInt(float value)
        => (int)MathF.Round(value, MidpointRounding.AwayFromZero);

    private static float NormalizeInspectPreviewScale(float scale)
        => float.IsFinite(scale) && scale > 0.05f && scale <= 4f ? scale : 1f;

    private static bool TryClampClientRect(
        int requestedX,
        int requestedY,
        int requestedWidth,
        int requestedHeight,
        int clientWidth,
        int clientHeight,
        out Rectangle rect)
    {
        var x = Math.Clamp(requestedX, 0, Math.Max(0, clientWidth - 1));
        var y = Math.Clamp(requestedY, 0, Math.Max(0, clientHeight - 1));
        var width = Math.Min(requestedWidth, clientWidth - x);
        var height = Math.Min(requestedHeight, clientHeight - y);
        if (width <= 0 || height <= 0)
        {
            rect = Rectangle.Empty;
            return false;
        }

        rect = new Rectangle(x, y, width, height);
        return true;
    }

    [DllImport("user32.dll")]
    private static extern bool GetClientRect(nint windowHandle, out Win32Rect rect);

    [DllImport("user32.dll")]
    private static extern bool ClientToScreen(nint windowHandle, ref Win32Point point);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc callback, nint lParam);

    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(nint windowHandle);

    [DllImport("user32.dll")]
    private static extern bool IsIconic(nint windowHandle);

    [DllImport("user32.dll")]
    private static extern bool GetWindowRect(nint windowHandle, out Win32Rect rect);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(nint windowHandle, out uint processId);

    private delegate bool EnumWindowsProc(nint windowHandle, nint lParam);

    private sealed class WindowHandleCandidate
    {
        public WindowHandleCandidate(nint windowHandle, string source)
        {
            WindowHandle = windowHandle;
            Source = source;
        }

        public nint WindowHandle { get; }
        public string Source { get; }
    }

    private sealed class CaptureSelection
    {
        public CaptureSelection(nint windowHandle, string source, Rectangle bounds, string boundsKind)
        {
            WindowHandle = windowHandle;
            Source = source;
            Bounds = bounds;
            BoundsKind = boundsKind;
        }

        public nint WindowHandle { get; }
        public string Source { get; }
        public Rectangle Bounds { get; }
        public string BoundsKind { get; }
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

    private sealed class RemoteUpdateResponse
    {
        public bool Ok { get; init; }
        public List<RemoteAction>? Actions { get; init; }
    }

    private sealed class RemoteAction
    {
        public string ActionType { get; init; } = string.Empty;
        public string? ActionId { get; init; }
        public string? Text { get; init; }
        public string? CaptureMode { get; init; }
        public string? CaptureQuality { get; init; }
        public string? CaptureKind { get; init; }
        public string? TargetCharacterName { get; init; }
        public string? TargetWorldName { get; init; }
        public string? TargetContentId { get; init; }
        public uint? TargetEntityId { get; init; }
    }

    private sealed class CctvControlMessage
    {
        public string Type { get; init; } = string.Empty;
        public string? Quality { get; init; }
    }

    private sealed class CctvFrameMetadata
    {
        public string AccountId { get; init; } = string.Empty;
        public string CharacterName { get; init; } = string.Empty;
        public string WorldName { get; init; } = string.Empty;
        public DateTime CapturedAtUtc { get; init; }
        public string Quality { get; init; } = "high";
        public string ContentType { get; init; } = "image/jpeg";
        public int Width { get; init; }
        public int Height { get; init; }
        public long Sequence { get; init; }
        public CctvCaptureStatus CaptureStatus { get; init; } = new();
    }

    private sealed class CctvCaptureStatus
    {
        public string Backend { get; init; } = "gdi";
        public string WindowState { get; init; } = "unknown";
        public bool IsStale { get; init; }
        public string Message { get; init; } = string.Empty;
    }

    private sealed class GameCaptureFrame : IDisposable
    {
        public GameCaptureFrame(Bitmap bitmap, CctvCaptureStatus captureStatus)
        {
            Bitmap = bitmap;
            CaptureStatus = captureStatus;
        }

        public Bitmap Bitmap { get; }
        public CctvCaptureStatus CaptureStatus { get; }

        public void Dispose()
            => Bitmap.Dispose();
    }

    private sealed class ScreenshotUploadRequest
    {
        public string AccountId { get; init; } = string.Empty;
        public string CharacterName { get; init; } = string.Empty;
        public string WorldName { get; init; } = string.Empty;
        public string ActionId { get; init; } = string.Empty;
        public DateTime CapturedAtUtc { get; init; }
        public string ContentType { get; init; } = "image/png";
        public string CaptureMode { get; init; } = "screenshot";
        public string? CaptureQuality { get; init; }
        public CctvCaptureStatus? CaptureStatus { get; init; }
        public string FileName { get; init; } = "ttsl.png";
        public string ImageBase64 { get; init; } = string.Empty;
    }

    private sealed class CharacterVisualUploadRequest
    {
        public string AccountId { get; init; } = string.Empty;
        public string CharacterName { get; init; } = string.Empty;
        public string WorldName { get; init; } = string.Empty;
        public string TargetCharacterName { get; init; } = string.Empty;
        public string TargetWorldName { get; init; } = string.Empty;
        public string ActionId { get; init; } = string.Empty;
        public DateTime CapturedAtUtc { get; init; }
        public string ContentType { get; init; } = "image/png";
        public string CaptureKind { get; init; } = "portrait";
        public string ImageBase64 { get; init; } = string.Empty;
    }

    private sealed class PendingCharacterVisualCapture
    {
        public string BaseUrl { get; init; } = string.Empty;
        public string ActionId { get; init; } = string.Empty;
        public string TargetCharacterName { get; init; } = string.Empty;
        public string TargetWorldName { get; init; } = string.Empty;
        public string TargetContentId { get; init; } = string.Empty;
        public uint? TargetEntityId { get; init; }
        public DateTime RequestedAtUtc { get; init; }
        public bool InspectRequested { get; set; }
        public DateTime? InspectRequestedAtUtc { get; set; }
        public string LastStatus { get; set; } = string.Empty;
        public string CharacterKey => $"{TargetCharacterName}@{TargetWorldName}";
    }

    private sealed class RemoteHudSnapshot
    {
        public string UpdateKind { get; init; } = "full";
        public DateTime TimestampUtc { get; init; }
        public string AccountId { get; init; } = string.Empty;
        public string CharacterName { get; init; } = string.Empty;
        public string WorldName { get; init; } = string.Empty;
        public uint EntityId { get; init; }
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
        public RemoteControlPolicySnapshot? Policy { get; init; }
        public RemoteConditionSnapshot? Conditions { get; init; }
        public RemoteRepairSnapshot? Repair { get; init; }
        public List<RemotePartyMemberSnapshot>? Party { get; init; }
        public RemoteCombatSnapshot? Combat { get; init; }
    }

    private sealed class RemoteControlPolicySnapshot
    {
        public bool AllowEchoCommands { get; init; }
        public bool AllowScreenshotRequests { get; init; }
        public bool AllowCctvStreaming { get; init; }
        public bool AllowPluginFullBodyFallback { get; init; }
    }

    private sealed class CctvCapturePreset
    {
        public CctvCapturePreset(string name, float scale, long jpegQuality, int maxFramesPerSecond)
        {
            Name = name;
            Scale = scale;
            JpegQuality = jpegQuality;
            MaxFramesPerSecond = maxFramesPerSecond;
        }

        public string Name { get; }
        public float Scale { get; }
        public long JpegQuality { get; }
        public int MaxFramesPerSecond { get; }
    }

    private sealed class Vector3Snapshot
    {
        public Vector3Snapshot(float x, float y, float z, float? rotation = null)
        {
            X = x;
            Y = y;
            Z = z;
            Rotation = rotation;
        }

        public float X { get; init; }
        public float Y { get; init; }
        public float Z { get; init; }
        public float? Rotation { get; init; }
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
        public List<string>? TexturePathCandidates { get; init; }
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
        public string ContentId { get; init; } = string.Empty;
        public string Name { get; init; } = string.Empty;
        public string WorldName { get; init; } = string.Empty;
        public uint? EntityId { get; init; }
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

    [StructLayout(LayoutKind.Sequential)]
    private struct Win32Point
    {
        public Win32Point(int x, int y)
        {
            X = x;
            Y = y;
        }

        public int X;
        public int Y;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Win32Rect
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }
}
