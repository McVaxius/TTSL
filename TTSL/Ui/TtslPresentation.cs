using System.Numerics;
using AethertekUI;
using Dalamud.Bindings.ImGui;

namespace TTSL.Ui;

internal enum UiFontRole { Body, BodyStrong, Title, PluginName, Counter, Action, CompactTitle }

internal static class TtslPresentation
{
    internal const uint ReferenceAccent = 0x1CC9E6;
    internal static readonly float[] FontSizes = [16, 16, 32, 22, 22, 18, 26];
    internal static readonly string[] FontFiles = ["segoeui.ttf", "seguisb.ttf", "segoeuib.ttf", "seguisb.ttf", "seguisb.ttf", "seguisb.ttf", "segoeuib.ttf"];
    internal static float AtlasHeight(UiFontRole role) => FontSizes[(int)role] * 4 / 3;
    internal static bool Compact => MaterialTheme.Current.Density == MaterialDensity.Compact;
    internal static float HeaderHeight => Compact ? 50 : 72;
    internal static float Gap => Compact ? 10 : 16;
    internal static float ControlHeight => Compact ? 40 : 52;
    internal static Vector4 Rgb(uint rgb) => new(((rgb >> 16) & 255) / 255f, ((rgb >> 8) & 255) / 255f, (rgb & 255) / 255f, 1);

    internal static MaterialTheme Theme(uint accent) => Theme(accent, new MaterialColorSettings());

    internal static MaterialTheme Theme(uint accent, MaterialColorSettings colorSettings)
    {
        accent &= 0xFFFFFF;
        var selected = Rgb(accent);
        var reference = Rgb(ReferenceAccent);
        var seed = MaterialColor.LabToLch(MaterialColor.SrgbToOklab(new(selected.X, selected.Y, selected.Z)));
        var original = MaterialColor.LabToLch(MaterialColor.SrgbToOklab(new(reference.X, reference.Y, reference.Z)));
        var hue = seed.Y < .001f ? 0 : seed.Z - original.Z;
        var chroma = seed.Y < .001f ? 0 : seed.Y / original.Y;
        Vector4 Relative(uint rgb)
        {
            var color = Rgb(rgb);
            if (accent == ReferenceAccent) return color;
            var lch = MaterialColor.LabToLch(MaterialColor.SrgbToOklab(new(color.X, color.Y, color.Z)));
            return new(MaterialColor.GamutMap(lch.X, lch.Y * chroma, lch.Z + hue), 1);
        }
        var background = Relative(0x111F29);
        var foreground = Relative(0xF0F1F4);
        var primary = Relative(ReferenceAccent);
        var palette = new OklchPaletteGenerator().Generate(new(selected.X, selected.Y, selected.Z));
        var colors = new MaterialColorScheme(palette)
        {
            Background = background, OnBackground = foreground,
            Surface = Relative(0x142630), OnSurface = foreground,
            SurfaceContainerLowest = Relative(0x10202B), SurfaceContainerLow = Relative(0x142630),
            SurfaceContainer = Relative(0x162832), SurfaceContainerHigh = Relative(0x1C303F), SurfaceContainerHighest = Relative(0x11212E),
            SurfaceVariant = Relative(0x2B4353), OnSurfaceVariant = Relative(0xB8BCC5),
            Outline = Relative(0x425D70), OutlineVariant = Relative(0x2C4657),
            Primary = primary, OnPrimary = MaterialColor.Contrast(primary, background) >= MaterialColor.Contrast(primary, foreground) ? background : foreground,
            PrimaryContainer = Relative(0x124653), OnPrimaryContainer = foreground,
            Secondary = Relative(0xB6D7DF), OnSecondary = background, SecondaryContainer = Relative(0x21313C), OnSecondaryContainer = foreground,
            Tertiary = Relative(0xAED1DC), OnTertiary = background, TertiaryContainer = Relative(0x223A47), OnTertiaryContainer = foreground,
            InverseSurface = foreground, InverseOnSurface = background, InversePrimary = Relative(0x0C6372),
        };
        return new(colorSettings.Apply(colors), MaterialDensity.Standard) { SurfaceOpacity = 1 };
    }

    internal static MaterialControlMetrics Controls(float height = 0)
    {
        if (height <= 0) height = ControlHeight;
        var s = MaterialTheme.Metrics.Scale;
        return new() { Height = height * s, Padding = new(12 * s, Math.Max(0, (height * s - ImGui.GetTextLineHeight()) * .5f)),
            Gap = 8 * s, IconSize = 22 * s, Rounding = 4 * s, ItemSpacing = new(10 * s, 6 * s), CellPadding = new(12 * s, 6 * s) };
    }

    internal static void Panel(Action draw,float logicalHeight)
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

    internal static void Surface(Vector2 min, Vector2 max)
    {
        if (MaterialTheme.Current.IsLcars) { MaterialLcars.PanelFrame(min, max); return; }
        var c = MaterialTheme.Current.Colors;
        MaterialCanvas.Surface(min, max, c.SurfaceContainerHigh, c.Surface, 4 * MaterialTheme.Metrics.Scale);
        ImGui.GetWindowDrawList().AddRect(min, max, MaterialCanvas.Color(c.OutlineVariant), 4 * MaterialTheme.Metrics.Scale);
    }

    internal static void Brand(Vector2 origin, float size)
    {
        var dl=ImGui.GetWindowDrawList(); var ink=MaterialCanvas.Color(MaterialTheme.Current.Colors.Primary);
        dl.PathLineTo(origin+new Vector2(.5f,.25f)*size);
        dl.PathBezierCubicCurveTo(origin+new Vector2(.02f,-.1f)*size,origin+new Vector2(-.2f,.48f)*size,origin+new Vector2(.5f,.95f)*size,24);
        dl.PathBezierCubicCurveTo(origin+new Vector2(1.2f,.48f)*size,origin+new Vector2(.98f,-.1f)*size,origin+new Vector2(.5f,.25f)*size,24);
        dl.PathFillConvex(ink);
        dl.AddLine(origin+new Vector2(.28f,.52f)*size,origin+new Vector2(.46f,.71f)*size,MaterialCanvas.Color(MaterialTheme.Current.Colors.Background),2.5f);
        dl.AddLine(origin+new Vector2(.46f,.71f)*size,origin+new Vector2(.78f,.36f)*size,MaterialCanvas.Color(MaterialTheme.Current.Colors.Background),2.5f);
    }
}
