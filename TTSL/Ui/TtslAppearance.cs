using System.Numerics;
using AethertekUI;
using AethertekUI.Dalamud;
using Dalamud.Bindings.ImGui;
using Dalamud.Interface.Utility;
using Dalamud.Interface.Windowing;
using Dalamud.Interface;

namespace TTSL.Ui;

internal sealed class TtslAppearance : IDisposable
{
    private readonly System.Collections.Generic.Dictionary<Dalamud.Interface.Windowing.IWindow, AethertekUI.MaterialWindowOpacity> windowOpacities = new();
    private readonly AethertekUI.MaterialWindowOpacity fontStatusOpacity = new();
    private readonly Plugin plugin;
    private readonly MaterialTextHost shapedText;
    private readonly MaterialUiSoundPlayer sounds = new();
    private bool soundsActive;
    private UiText text;
    private TtslFonts fonts;
    private MaterialTheme theme;
    private readonly MaterialWindowFold fontStatusMotion = new();
    private readonly MaterialWindowDecorations fontStatusDecorations = new();
    private readonly MaterialOptions<string> languages = new(UiText.Languages.Select(l => new MaterialOption<string>(l.Code, l.Code, l.Name)).ToArray());
    private string appliedLanguage = "";
    private uint appliedAccent;
    private Vector3 accentDraft;
    private MaterialColorSettings colorDraft = new();
    private MaterialColorSettings? appliedSavedColors;
    private MaterialColorSettings? appliedColors;
    private int checkedGeneration = -1;
    private int checkedHindiGeneration = -1;
    private bool fontIssueLogged;

    private void Apply()
    {
        var language = UiText.Languages.Any(l => l.Code == plugin.Configuration.UiLanguage) ? plugin.Configuration.UiLanguage : "en";
        if (language != appliedLanguage)
        {
            fonts?.Dispose();
            text?.Dispose();
            text = new(language, PushFont);
            fonts = new(Plugin.PluginInterface.UiBuilder.FontAtlas, text.GlyphRanges(), language);
            appliedLanguage = language;
            checkedGeneration = -1;
            checkedHindiGeneration = -1;
            fontIssueLogged = false;
            plugin.UpdateDtrBar();
        }
        var savedColors = plugin.Configuration.UiColors ?? new MaterialColorSettings();
        if (savedColors != appliedSavedColors)
        {
            colorDraft = savedColors;
            appliedSavedColors = savedColors;
        }
        if (theme is null || appliedAccent != (plugin.Configuration.UiAccentRgb & 0xFFFFFF) || colorDraft != appliedColors)
        {
            appliedAccent = plugin.Configuration.UiAccentRgb & 0xFFFFFF;
            appliedColors = colorDraft;
            theme = TtslPresentation.Theme(appliedAccent, colorDraft);
            theme.Feedback = PlaySound;
            var rgb = TtslPresentation.Rgb(appliedAccent);
            accentDraft = new(rgb.X, rgb.Y, rgb.Z);
        }
        theme.Density = plugin.Configuration.UiCompact ? MaterialDensity.Compact : MaterialDensity.Standard;
        var active = theme.IsLcars && !plugin.Configuration.UiSoundsMuted;
        if (soundsActive && !active) sounds.Stop();
        soundsActive = active;
    }

    internal void Draw(WindowSystem windows)
    {
        Apply();
        if (!windows.Windows.Any(window => window.IsOpen)) return;
        using var resources = text.Enter();
        using var shaping = shapedText.Push();
        if (fonts.Ready && checkedHindiGeneration != fonts.Generation)
        {
            var generation = fonts.Generation;
            var hindiAvailable = true;
            foreach (var role in Enum.GetValues<UiFontRole>())
                hindiAvailable &= shapedText.Renderer.TryCheckGlyphs(["हिन्दी"], TtslPresentation.AtlasHeight(role) * ImGuiHelpers.GlobalScale, out _);
            languages.Replace(UiText.Languages.Select(l => new MaterialOption<string>(l.Code, l.Code,
                l.Code == "hi" && !hindiAvailable ? "Hindi (unavailable)" : l.Name, l.Code == "hi" && !hindiAvailable)).ToArray());
            checkedHindiGeneration = generation;
        }
        if (fonts.Ready && checkedGeneration != fonts.Generation)
        {
            try
            {
                foreach (var role in Enum.GetValues<UiFontRole>())
                    shapedText.Renderer.CheckGlyphs(text.RequiredText, TtslPresentation.AtlasHeight(role) * ImGuiHelpers.GlobalScale);
                fonts.CheckGlyphs(text.RequiredText);
                checkedGeneration = fonts.Generation;
            }
            catch (Exception ex)
            {
                if (!fontIssueLogged) { Plugin.Log.Error(ex, "[TTSL] Required UI glyph coverage failed."); fontIssueLogged = true; }
            }
        }
        using var palette = MaterialTheme.Push(theme, ImGuiHelpers.GlobalScale, MaterialStyleMode.ColorsOnly);
        using var chrome = MaterialWindowChrome.Push();
        if (!fonts.Ready || checkedGeneration != fonts.Generation)
        {
            if (!fontIssueLogged && fonts.LoadException is { } error) { Plugin.Log.Error(error, "[TTSL] Required UI fonts failed to load."); fontIssueLogged = true; }
            ImGui.SetNextWindowSize(new Vector2(460 * ImGuiHelpers.GlobalScale, 0));
            fontStatusMotion.PreDraw("Thick Thighs Save Lives##FontStatus", null, null, reducedMotion: false, prepareDecorations: fontStatusDecorations.Prepare);
            bool statusVisible;
            using (MaterialSkin.BackdropStyle())
                statusVisible = ImGui.Begin("Thick Thighs Save Lives##FontStatus", ImGuiWindowFlags.AlwaysAutoResize);
            if (statusVisible)
            {
                MaterialSkin.Backdrop();
                fontStatusDecorations.Paint();
                if (theme.IsLcars) MaterialLcars.Header("TTSL");
                var loading = fonts.LoadException is null && !fontIssueLogged;
                if (appliedLanguage == "hi")
                {
                    ImGui.TextWrapped(loading ? "Loading Hindi UI fonts..." : "Hindi UI fonts are unavailable. See the plugin log.");
                    if (!loading && ImGui.Button("Use English")) { plugin.Configuration.UiLanguage = "en"; plugin.Configuration.Save(); }
                }
                else MaterialText.TextWrapped(UiText.T(loading ? "Loading UI fonts..." : "UI fonts failed to load. See the plugin log."));
                if (theme.IsLcars) MaterialLcars.Footer("TTSL");
            }
            ImGui.End();
            fontStatusDecorations.Paint();
            fontStatusMotion.PostDraw();
            ApplyWindowOpacity(fontStatusOpacity, "Thick Thighs Save Lives##FontStatus");
            return;
        }
        using var style = new MaterialStyleScope();
        var s = ImGuiHelpers.GlobalScale;
        style.Style(ImGuiStyleVar.WindowPadding, new Vector2(plugin.Configuration.UiCompact ? 12 : 20) * s);
        style.Style(ImGuiStyleVar.ItemSpacing, new Vector2(plugin.Configuration.UiCompact ? 8 : 12, plugin.Configuration.UiCompact ? 5 : 10) * s);
        style.Style(ImGuiStyleVar.FramePadding, new Vector2(plugin.Configuration.UiCompact ? 10 : 14, plugin.Configuration.UiCompact ? 4 : 7) * s);
        style.Style(ImGuiStyleVar.CellPadding, new Vector2(plugin.Configuration.UiCompact ? 6 : 10, plugin.Configuration.UiCompact ? 4 : 8) * s);
        style.Style(ImGuiStyleVar.FrameRounding, 4 * s);
        style.Style(ImGuiStyleVar.ChildRounding, 4 * s);
        using var body = fonts.Push(UiFontRole.Body);
        PrepareAppearanceTitleBars(windows);
        foreach (var actions in appearanceTitleBars.Values) { actions.SetLcarsIcons(theme.IsLcars); actions.RefreshVisibility(); }
        windows.Draw();
        foreach (var window in windows.Windows)
        {
            if (window is Window owner && appearanceTitleBars.TryGetValue(owner, out var actions)) actions.DrawPopups();
            if (!windowOpacities.TryGetValue(window, out var opacity))
                windowOpacities.Add(window, opacity = new());
            ApplyWindowOpacity(opacity, window.WindowName);
        }
    }

    private readonly System.Collections.Generic.Dictionary<Window, MaterialAppearanceTitleBar> appearanceTitleBars = new();
    private readonly System.Collections.Generic.Dictionary<Window, TitleBarButton> soundButtons = new();

    private void PrepareAppearanceTitleBars(WindowSystem windows)
    {
        foreach (var owner in windows.Windows.OfType<Window>())
        {
            if ((owner.Flags & ImGuiWindowFlags.NoTitleBar) != 0) continue;
            if (!appearanceTitleBars.ContainsKey(owner))
                appearanceTitleBars.Add(owner, MaterialAppearanceTitleBar.Register(owner,
                () => new(UiText.T("Language"), UiText.T("Compact mode"), UiText.T("Transparency"), UiText.T("Color")),
                DrawLanguageSelector,
                () => { plugin.Configuration.UiCompact = !plugin.Configuration.UiCompact; plugin.Configuration.Save(); },
                DrawWindowSettings, DrawTitleColour,
                () => plugin.SetKrangleEnabled(!plugin.Configuration.KrangleEnabled, "titlebar"),
                () => UiText.T("Krangle"),
                () => owner is TTSL.Windows.MainWindow
                    ? new(plugin.Configuration.UiLanguageVisibleOnMainWindow, plugin.Configuration.UiCompactVisibleOnMainWindow,
                        plugin.Configuration.UiTransparencyVisibleOnMainWindow, true)
                    : new(true, true, true, true)));
            if (!soundButtons.TryGetValue(owner, out var speaker))
            {
                speaker = new TitleBarButton
                {
                    Priority = owner.TitleBarButtons.Min(button => button.Priority) - 1,
                    IconOffset = new(2, 1),
                    Click = button => { if (button == ImGuiMouseButton.Left && theme.IsLcars) SetSoundsMuted(!plugin.Configuration.UiSoundsMuted); },
                    ShowTooltip = () => MaterialText.SetTooltip(UiText.T(plugin.Configuration.UiSoundsMuted ? "Unmute UI sounds" : "Mute UI sounds")),
                };
                soundButtons.Add(owner, speaker);
            }
            speaker.Icon = plugin.Configuration.UiSoundsMuted ? FontAwesomeIcon.VolumeMute : FontAwesomeIcon.VolumeUp;
            if (theme.IsLcars && !owner.TitleBarButtons.Contains(speaker)) owner.TitleBarButtons.Add(speaker);
            else if (!theme.IsLcars) owner.TitleBarButtons.Remove(speaker);
        }
    }

    private bool DrawColourEditor(string id)
    {
        var changed = MaterialAppearanceSelector.DrawColors(id, ref colorDraft, ref accentDraft,
            new(UiText.T("Color"), UiText.T("Language"), UiText.T("Teal"), UiText.T("Blue"), UiText.T("Pink"), UiText.T("Custom RGB")),
            new(UiText.T("Auto"), UiText.T("Picker"), UiText.T("Default"), UiText.T("Background, controls and text follow your theme."), UiText.T("Theme colour"), UiText.T("Choose each colour independently."), UiText.T("Background"), UiText.T("Controls"), UiText.T("Text"), UiText.T("Aa  Preview text"), UiText.T("Control"), UiText.T("Switch to Picker to set each colour yourself."), UiText.T("Switch back to Auto to use the automatic theme."))
            {
                Lcars = UiText.T("LCARS"), LcarsHelp = UiText.T("LCARS panels, bars and controls"),
                LcarsBar1 = UiText.T("LCARS bar 1"), LcarsBar2 = UiText.T("LCARS bar 2"), LcarsBar3 = UiText.T("LCARS bar 3"),
                Glassmorphism = UiText.T("Glassmorphism"), Spatial = UiText.T("Spatial"), Cyberpunk = UiText.T("Cyberpunk"),
                Reset = UiText.T("Reset"), ResetColors = UiText.T("Reset colours"),
                Animation = UiText.T("Animation"), AnimationSpeed = UiText.T("Animation speed (0.1–10x; default 1x)"),
                WaveColors = UiText.T("Wave colours"), GlassWave1 = UiText.T("Wave colour 1"),
                GlassWave2 = UiText.T("Wave colour 2"), GlassWave3 = UiText.T("Wave colour 3"), ResetWaveColors = UiText.T("Reset wave colours"),
            },
            new MaterialColorSettings(), MaterialColorSettings.Rgb(TtslPresentation.ReferenceAccent),
            plugin.Configuration.UiCompact ? 298 : 316);
        if (!changed) return false;
        plugin.Configuration.UiColors = colorDraft;
        plugin.Configuration.UiAccentRgb = MaterialColorSettings.ToRgb(accentDraft);
        return true;
    }

    private void DrawTitleColour()
    {
        using var controls = MaterialControls.Push(TtslPresentation.Controls(28));
        if (DrawColourEditor("titlebar-colour")) plugin.Configuration.Save();
    }

    internal void DrawSelector()
    {
        var language = appliedLanguage;
        using var controls = MaterialControls.Push(TtslPresentation.Controls(28));
        var colorChanged = DrawColourEditor("appearance");
        var languageChanged = MaterialAppearanceSelector.DrawLanguage("appearance", ref language, languages, 140);
        if (languageChanged) plugin.Configuration.UiLanguage = language;
        if (colorChanged || languageChanged) plugin.Configuration.Save();
    }

    internal TtslAppearance(Plugin plugin)
    {
        this.plugin = plugin;
        shapedText = new(Plugin.TextureProvider);
        appliedLanguage = UiText.Languages.Any(l => l.Code == plugin.Configuration.UiLanguage) ? plugin.Configuration.UiLanguage : "en";
        text = new(appliedLanguage, PushFont);
        fonts = new(Plugin.PluginInterface.UiBuilder.FontAtlas, text.GlyphRanges(), appliedLanguage);
        appliedAccent = plugin.Configuration.UiAccentRgb & 0xFFFFFF;
        theme = TtslPresentation.Theme(appliedAccent);
        var rgb = TtslPresentation.Rgb(appliedAccent);
        accentDraft = new(rgb.X, rgb.Y, rgb.Z);
    }
    private IDisposable PushFont(UiFontRole role) => fonts.Push(role);
    internal string Label(string key) => text.Label(key);
    // DTR is game-rendered and cannot use the ImGui shaping bridge.
    internal string NativeLabel(string key) => text.Language == "hi" ? key : text.Label(key);
    internal string Format(string key, params object?[] arguments) => text.Format(key, arguments);

    public void Dispose() { sounds.Dispose(); fonts?.Dispose(); text?.Dispose(); shapedText.Dispose(); }

    private void PlaySound(MaterialUiSoundCue cue)
    {
        if (theme.IsLcars && !plugin.Configuration.UiSoundsMuted) sounds.TryPlay(cue);
    }

    private void SetSoundsMuted(bool muted)
    {
        plugin.Configuration.UiSoundsMuted = muted;
        if (muted) sounds.Stop();
        foreach (var speaker in soundButtons.Values) speaker.Icon = muted ? FontAwesomeIcon.VolumeMute : FontAwesomeIcon.VolumeUp;
        plugin.Configuration.Save();
    }

    internal void ApplyWindowOpacity(AethertekUI.MaterialWindowOpacity opacity, string windowName)
    {
        var config = plugin.Configuration;
        opacity.Apply(windowName, config.UiWindowOpacityPercent / 100f, config.UiTransparencyEnabled,
            config.UiAutoFade, config.UiFadedOpacityPercent / 100f, config.UiUnfocusedDelaySeconds);
    }

    internal void DrawTransparencyToggle()
    {
        var enabled = plugin.Configuration.UiTransparencyEnabled;
        if (UiGui.Checkbox("Transparency###window-transparency-main", ref enabled))
        { plugin.Configuration.UiTransparencyEnabled = enabled; plugin.Configuration.Save(); }
    }

    internal void DrawWindowSettings()
    {
        var muted = plugin.Configuration.UiSoundsMuted;
        if (UiGui.Checkbox("Mute UI sounds###UiSoundsMuted", ref muted)) SetSoundsMuted(muted);
        ImGui.BeginDisabled(muted || !theme.IsLcars);
        UiGui.Button("Preview UI sound###ui-sound-preview", cue: MaterialUiSoundCue.Preview);
        ImGui.EndDisabled();
        if (sounds.Error is not null) UiGui.TextDisabled("Audio output is unavailable.");
        var showTitlebarStatus = plugin.Configuration.UiShowTitlebarStatus;
        if (UiGui.Checkbox(UiText.T("Show titlebar status") + "###UiShowTitlebarStatus", ref showTitlebarStatus))
        { plugin.Configuration.UiShowTitlebarStatus = showTitlebarStatus; plugin.Configuration.Save(); }
        var detailedTitlebarStatus = plugin.Configuration.UiDetailedTitlebarStatus;
        if (UiGui.Checkbox(UiText.T("Detailed titlebar status") + "###UiDetailedTitlebarStatus", ref detailedTitlebarStatus))
        { plugin.Configuration.UiDetailedTitlebarStatus = detailedTitlebarStatus; plugin.Configuration.Save(); }
        var config = plugin.Configuration;
        var changed = false;
        var compactVisible = config.UiCompactVisibleOnMainWindow;
        if (UiGui.Checkbox("Compact visible on main window###window-compact-visible", ref compactVisible))
        { config.UiCompactVisibleOnMainWindow = compactVisible; changed = true; }
        var transparencyVisible = config.UiTransparencyVisibleOnMainWindow;
        if (UiGui.Checkbox("Transparency visible on main window###window-transparency-visible", ref transparencyVisible))
        { config.UiTransparencyVisibleOnMainWindow = transparencyVisible; changed = true; }
        var languageVisible = config.UiLanguageVisibleOnMainWindow;
        if (UiGui.Checkbox("Language visible on main window###window-language-visible", ref languageVisible))
        { config.UiLanguageVisibleOnMainWindow = languageVisible; changed = true; }
        var enabled = config.UiTransparencyEnabled;
        if (UiGui.Checkbox("Transparency###window-transparency", ref enabled))
        { config.UiTransparencyEnabled = enabled; changed = true; }
        ImGui.BeginDisabled(!config.UiTransparencyEnabled);
        var normal = config.UiWindowOpacityPercent;
        if (UiGui.InputInt("Opacity (%)###window-opacity", ref normal, step: 0, preferredPixels: 96 * MaterialTheme.Metrics.Scale))
        { config.UiWindowOpacityPercent = normal; changed = true; }
        var autoFade = config.UiAutoFade;
        if (UiGui.Checkbox("Auto-fade when unfocused###window-auto-fade", ref autoFade))
        { config.UiAutoFade = autoFade; changed = true; }
        ImGui.BeginDisabled(!config.UiAutoFade);
        var faded = config.UiFadedOpacityPercent;
        if (UiGui.InputInt("Unfocused opacity (%)###window-faded-opacity", ref faded, step: 0, preferredPixels: 96 * MaterialTheme.Metrics.Scale))
        { config.UiFadedOpacityPercent = faded; changed = true; }
        var delay = config.UiUnfocusedDelaySeconds;
        if (UiGui.InputInt("Unfocused delay (seconds)###window-unfocused-delay", ref delay, step: 0, preferredPixels: 96 * MaterialTheme.Metrics.Scale))
        { config.UiUnfocusedDelaySeconds = delay; changed = true; }
        ImGui.EndDisabled();
        ImGui.EndDisabled();
        if (changed) plugin.Configuration.Save();
    }

    internal float LanguageWidth
    {
        get
        {
            using var controls = MaterialControls.Push(TtslPresentation.Controls(28));
            var metrics = MaterialControls.Metrics;
            return Math.Max(140 * MaterialTheme.Metrics.Scale, MathF.Ceiling(MaterialText.Measure(languages.LabelFor(appliedLanguage, "Select...")).X
                + metrics.Height + 3 * metrics.Gap + Math.Min(metrics.IconSize, metrics.Height)));
        }
    }

    internal void DrawLanguageSelector()
    {
        var language = appliedLanguage;
        using var controls = MaterialControls.Push(TtslPresentation.Controls(28));
        if (!MaterialAppearanceSelector.DrawLanguage("appearance", ref language, languages, 140)) return;
        plugin.Configuration.UiLanguage = language;
        plugin.Configuration.Save();
    }
}
