using System;
using System.Numerics;
using AethertekUI;
using AethertekUI.Dalamud;
using Dalamud.Bindings.ImGui;
using Dalamud.Interface.Utility;
using Dalamud.Interface.Windowing;

namespace TTSL.Windows;

public abstract class PositionedWindow : Window
{
    protected readonly MaterialWindowMotion WindowMotion = new();
    private readonly MaterialWindowLock movementLock = new();
    private readonly Plugin ownerPlugin;
    private bool materialBackdropStylePushed;
    private Vector2? pendingWindowPosition;
    private bool pendingPositionConditionReset;

    protected PositionedWindow(Plugin plugin, string name, ImGuiWindowFlags flags = ImGuiWindowFlags.None)
        : base(name, flags)
    {
        ownerPlugin = plugin;
    }

    public void QueueResetToOrigin()
        => QueueWindowPosition(new Vector2(1f, 1f));

    public void QueueRandomVisibleJump()
        => QueueWindowPosition(GetRandomVisiblePosition());

    public override void PreDraw()
    {
        if (pendingWindowPosition.HasValue)
        {
            Position = pendingWindowPosition.Value;
            PositionCondition = ImGuiCond.Always;
            pendingWindowPosition = null;
            pendingPositionConditionReset = true;
        }
        if (Ui.TtslPresentation.HasMaterial || ownerPlugin.Appearance.BackgroundEnabled)
        {
            ImGui.PushStyleColor(ImGuiCol.WindowBg, Vector4.Zero);
            materialBackdropStylePushed = true;
        }
        WindowMotion.Prepare(this, reducedMotion: !ownerPlugin.Configuration.UiStylingEnabled, roundedCorners: ownerPlugin.Configuration.UiStylingEnabled);
        movementLock.Apply(this, ownerPlugin.Configuration.UiWindowsLocked);
    }

    public override void PostDraw()
    {
        movementLock.Restore(this);
        WindowMotion.Restore(this);
        ReleaseMaterialBackdropStyle();
    }

    protected void BeginMaterialContent()
    {
        ReleaseMaterialBackdropStyle();
        ownerPlugin.Appearance.PrepareBackgroundOpacity(this, ImGui.IsWindowAppearing());
        MaterialSkin.Backdrop();
    }

    private void ReleaseMaterialBackdropStyle()
    {
        if (!materialBackdropStylePushed) return;
        ImGui.PopStyleColor();
        materialBackdropStylePushed = false;
    }

    protected void FinalizePendingWindowPlacement()
    {
        if (!pendingPositionConditionReset)
            return;

        pendingPositionConditionReset = false;
        Position = null;
        PositionCondition = ImGuiCond.None;
    }

    private void QueueWindowPosition(Vector2 position)
        => pendingWindowPosition = position;

    private Vector2 GetRandomVisiblePosition()
    {
        var viewport = ImGuiHelpers.MainViewport;
        var currentSize = Size ?? Vector2.Zero;
        var minimumSize = SizeConstraints?.MinimumSize ?? Vector2.Zero;
        var width = MathF.Max(currentSize.X, minimumSize.X);
        var height = MathF.Max(currentSize.Y, minimumSize.Y);
        var maxX = MathF.Max(1f, viewport.Size.X - width - 20f);
        var maxY = MathF.Max(1f, viewport.Size.Y - height - 20f);
        return new Vector2(1f + (Random.Shared.NextSingle() * maxX), 1f + (Random.Shared.NextSingle() * maxY));
    }
}
