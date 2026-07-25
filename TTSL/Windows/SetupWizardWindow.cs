using System;
using System.Numerics;
using Dalamud.Bindings.ImGui;
using Dalamud.Interface.Windowing;

namespace TTSL.Windows;

public sealed class SetupWizardWindow : Window, IDisposable
{
    private enum SetupMode
    {
        LocalHud,
        LocalAndWeb,
        WebOnly,
    }

    private sealed class WizardDraft
    {
        public SetupMode Mode;
        public bool ShowConditionPanel;
        public bool ShowRepairSummary;
        public bool ShowPartyStatus;
        public bool ShowPartyRadar;
        public bool KrangleEnabled;
        public bool DtrBarEnabled;
        public string RemoteServerUrl = string.Empty;

        public static WizardDraft From(Configuration configuration)
            => new()
            {
                Mode = configuration.RemoteServerEnabled
                    ? configuration.OverlayEnabled
                        ? SetupMode.LocalAndWeb
                        : SetupMode.WebOnly
                    : SetupMode.LocalHud,
                ShowConditionPanel = configuration.ShowConditionPanel,
                ShowRepairSummary = configuration.ShowRepairSummary,
                ShowPartyStatus = configuration.ShowPartyStatus,
                ShowPartyRadar = configuration.ShowPartyRadar,
                KrangleEnabled = configuration.KrangleEnabled,
                DtrBarEnabled = configuration.DtrBarEnabled,
                RemoteServerUrl = configuration.RemoteServerUrl ?? string.Empty,
            };
    }

    private readonly Plugin plugin;
    private WizardDraft? draft;
    private string accountId = string.Empty;
    private string finishError = string.Empty;
    private int step;
    private bool dismissFirstRunOnClose;

    public SetupWizardWindow(Plugin plugin)
        : base("TTSL Setup Wizard##TTSLSetupWizard")
    {
        this.plugin = plugin;
        Flags = ImGuiWindowFlags.NoCollapse | ImGuiWindowFlags.AlwaysAutoResize;
        Size = new Vector2(560f, 0f);
        SizeCondition = ImGuiCond.FirstUseEver;
    }

    public void Dispose()
    {
    }

    public void OpenWithFreshDraft(string resolvedAccountId, bool firstRun)
    {
        accountId = resolvedAccountId;
        draft = WizardDraft.From(plugin.Configuration);
        step = 0;
        finishError = string.Empty;
        dismissFirstRunOnClose = firstRun;
        IsOpen = true;
    }

    public override void OnClose()
    {
        DismissFirstRunIfNeeded();
        ResetDraft();
    }

    public override void Draw()
    {
        draft ??= WizardDraft.From(plugin.Configuration);

        ImGui.Text($"Step {step + 1} of 3");
        ImGui.Separator();
        ImGui.Spacing();

        switch (step)
        {
            case 0:
                DrawModeStep();
                break;
            case 1:
                DrawPanelsStep();
                break;
            default:
                DrawRemoteAndReviewStep();
                break;
        }

        if (!string.IsNullOrWhiteSpace(finishError))
        {
            ImGui.Spacing();
            ImGui.TextColored(new Vector4(1f, 0.45f, 0.35f, 1f), finishError);
        }

        ImGui.Spacing();
        ImGui.Separator();
        DrawNavigation();
    }

    private void DrawModeStep()
    {
        ImGui.Text("Where should TTSL show your HUD?");
        ImGui.TextWrapped("Choose a starting mode. The wizard changes only the settings shown here; advanced web permissions, refresh intervals, radar sizing, icons, and labels stay as they are.");
        ImGui.Spacing();

        DrawModeChoice(
            "Local HUD",
            SetupMode.LocalHud,
            "Show the in-game TTSL window without publishing to the web server.");
        DrawModeChoice(
            "Local + Web",
            SetupMode.LocalAndWeb,
            "Show the in-game TTSL window and publish snapshots to the configured web server.");
        DrawModeChoice(
            "Web only",
            SetupMode.WebOnly,
            "Publish snapshots to the web server while keeping the in-game HUD hidden.");
    }

    private void DrawModeChoice(string label, SetupMode mode, string description)
    {
        if (ImGui.RadioButton(label, draft!.Mode == mode))
            draft.Mode = mode;

        ImGui.Indent();
        ImGui.TextDisabled(description);
        ImGui.Unindent();
        ImGui.Spacing();
    }

    private void DrawPanelsStep()
    {
        ImGui.Text("Choose the local HUD details you want ready");
        ImGui.TextWrapped("These choices also control which sections are included when local HUD data is published.");
        ImGui.Spacing();

        DrawCheckbox("Condition panel", ref draft!.ShowConditionPanel);
        DrawCheckbox("Repair summary", ref draft.ShowRepairSummary);
        DrawCheckbox("Party status list", ref draft.ShowPartyStatus);
        DrawCheckbox("Party radar", ref draft.ShowPartyRadar);
        DrawCheckbox("Krangle displayed names", ref draft.KrangleEnabled);
        DrawCheckbox("DTR status entry", ref draft.DtrBarEnabled);
    }

    private void DrawRemoteAndReviewStep()
    {
        var usesRemote = draft!.Mode != SetupMode.LocalHud;

        ImGui.Text("Remote server and review");
        ImGui.TextWrapped(usesRemote
            ? "Confirm the web server address and copy the existing launch command if you need to start the local server."
            : "Local HUD mode does not publish snapshots. The existing remote URL is preserved for later.");
        ImGui.Spacing();

        ImGui.BeginDisabled(!usesRemote);
        var remoteUrl = draft.RemoteServerUrl;
        ImGui.SetNextItemWidth(390f);
        if (ImGui.InputText("Server URL", ref remoteUrl, 256))
            draft.RemoteServerUrl = remoteUrl;

        var launchCommand = plugin.GetSuggestedServerLaunchCommand();
        ImGui.SetNextItemWidth(390f);
        ImGui.InputText("Launch command", ref launchCommand, 1024, ImGuiInputTextFlags.ReadOnly);
        ImGui.SameLine();
        if (ImGui.SmallButton("Copy"))
        {
            ImGui.SetClipboardText(launchCommand);
            Plugin.Log.Information("[TTSL] Copied server launch command from setup wizard.");
        }
        ImGui.EndDisabled();

        ImGui.Spacing();
        ImGui.Text("Review");
        ImGui.BulletText($"Mode: {GetModeName(draft.Mode)}");
        ImGui.BulletText($"Condition panel: {OnOff(draft.ShowConditionPanel)}");
        ImGui.BulletText($"Repair summary: {OnOff(draft.ShowRepairSummary)}");
        ImGui.BulletText($"Party status: {OnOff(draft.ShowPartyStatus)}");
        ImGui.BulletText($"Party radar: {OnOff(draft.ShowPartyRadar)}");
        ImGui.BulletText($"Krangle names: {OnOff(draft.KrangleEnabled)}");
        ImGui.BulletText($"DTR entry: {OnOff(draft.DtrBarEnabled)}");
        if (usesRemote)
        {
            var displayedUrl = string.IsNullOrWhiteSpace(draft.RemoteServerUrl)
                ? "http://127.0.0.1:6942"
                : draft.RemoteServerUrl.Trim();
            ImGui.BulletText($"Server: {displayedUrl}");
        }
    }

    private void DrawNavigation()
    {
        if (ImGui.Button("Cancel"))
        {
            DismissFirstRunIfNeeded();
            ResetDraft();
            IsOpen = false;
            return;
        }

        if (step > 0)
        {
            ImGui.SameLine();
            if (ImGui.Button("Back"))
            {
                step--;
                finishError = string.Empty;
            }
        }

        ImGui.SameLine();
        if (step < 2)
        {
            if (ImGui.Button("Next"))
            {
                step++;
                finishError = string.Empty;
            }

            return;
        }

        if (!ImGui.Button("Finish"))
            return;

        var completed = draft!;
        var overlayEnabled = completed.Mode != SetupMode.WebOnly;
        var remoteEnabled = completed.Mode != SetupMode.LocalHud;
        if (!plugin.ApplySetupWizardSettings(
                accountId,
                overlayEnabled,
                remoteEnabled,
                completed.RemoteServerUrl,
                completed.ShowConditionPanel,
                completed.ShowRepairSummary,
                completed.ShowPartyStatus,
                completed.ShowPartyRadar,
                completed.KrangleEnabled,
                completed.DtrBarEnabled))
        {
            finishError = "The active account changed. Reopen the wizard and review that account's settings.";
            return;
        }

        dismissFirstRunOnClose = false;
        ResetDraft();
        IsOpen = false;
    }

    private void DismissFirstRunIfNeeded()
    {
        if (!dismissFirstRunOnClose || string.IsNullOrWhiteSpace(accountId))
            return;

        plugin.DismissSetupWizard(accountId);
        dismissFirstRunOnClose = false;
    }

    private void ResetDraft()
    {
        draft = null;
        accountId = string.Empty;
        finishError = string.Empty;
        step = 0;
    }

    private static void DrawCheckbox(string label, ref bool value)
        => ImGui.Checkbox(label, ref value);

    private static string OnOff(bool value)
        => value ? "On" : "Off";

    private static string GetModeName(SetupMode mode)
        => mode switch
        {
            SetupMode.LocalAndWeb => "Local + Web",
            SetupMode.WebOnly => "Web only",
            _ => "Local HUD",
        };
}
