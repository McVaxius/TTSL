using System.Numerics;
using AethertekUI;
using Dalamud.Bindings.ImGui;
using Dalamud.Interface.Windowing;

namespace TTSL.Ui;

// Native widgets receive their original English labels/IDs. Only their visible label is painted in the selected locale.
// This also preserves English-derived helper IDs and existing saved window identities.
internal static class UiGui
{
    internal static void TextDisabled(string text)
    {
        ImGui.PushTextWrapPos(0); MaterialText.TextDisabled(UiText.T(text)); ImGui.PopTextWrapPos();
    }
    internal static void TextDisabled(FormattableString text) => TextDisabled(UiText.F(text));
    internal static void BulletText(string text) => MaterialText.BulletText(UiText.T(text));
    internal static bool ToggleAction(string original,ref bool value,MaterialIcon icon)
    {
        var material=TtslPresentation.HasMaterial;
        var alpha=ImGui.GetStyle().Alpha;
        using var nativeButtons = material ? MaterialSkin.NativeButtonStyle() : MaterialLcars.NativeButtonStyle();
        var s=MaterialTheme.Metrics.Scale;var c=MaterialTheme.Current.Colors;
        using var rounding=new MaterialStyleScope();
        if(material) rounding.Style(ImGuiStyleVar.FrameRounding,4*s);
        var label=UiText.T(original);var size=new Vector2(Math.Max(156*s,MaterialText.Measure(label).X+76*s),TtslPresentation.ControlHeight*s);
        size.X=MaterialLayout.FitNextItemWidth(size.X,size.X);
        ImGui.PushStyleColor(ImGuiCol.Button,MaterialTheme.Current.IsLcars ? MaterialLcars.Background : value?MaterialColor.Layer(c.SurfaceContainer,c.Primary,.15f):c.SurfaceContainerHigh);
        ImGui.PushStyleColor(ImGuiCol.ButtonHovered,MaterialColor.Layer(c.SurfaceContainerHigh,c.Primary,.22f));
        ImGui.PushStyleColor(ImGuiCol.ButtonActive,MaterialColor.Layer(c.SurfaceContainerHigh,c.Primary,.30f));
        size.Y=Math.Max(size.Y,MaterialText.Measure(label).Y+2*ImGui.GetStyle().FramePadding.Y);
        bool clicked;
        Vector2 min,max;
        try
        {
            ImGui.PushStyleColor(ImGuiCol.Text,Vector4.Zero);
            try
            {
                using var face=material ? MaterialSkin.TransparentButtonFace() : new MaterialStyleScope();
                clicked=ImGui.Button(original,size);
            }
            finally { ImGui.PopStyleColor(); }
            min=ImGui.GetItemRectMin();max=ImGui.GetItemRectMax();
            if(material) MaterialSkin.PaintButton(min,max,alpha,4*s);
        }
        finally { ImGui.PopStyleColor(3); }
        var dl=ImGui.GetWindowDrawList();
        dl.AddRect(min,max,MaterialCanvas.Color(value?c.Primary:c.OutlineVariant),4*s);
        MaterialIcons.Draw(icon,min+new Vector2(14*s,(size.Y-24*s)*.5f),24*s,c.OnSurface);
        MaterialText.AddText(dl,min+new Vector2(50*s,(size.Y-MaterialText.Measure(label).Y)*.5f),MaterialCanvas.Color(c.OnSurface),label);
        if(clicked)value=!value;MaterialLcars.Feedback(clicked, MaterialUiSoundCue.Toggle);return clicked;
    }
    private static (Vector2 Min, Vector2 PreviousMax, float Width, string Label) BeginField(string original, float preferredWidth, float minimum)
    {
        var translated=UiText.T(original.Split("##",2)[0]);
        var gap=translated.Length==0?0:ImGui.GetStyle().ItemInnerSpacing.X;
        minimum=MathF.Ceiling(minimum);
        var labelWidth=MaterialText.Measure(translated).X;
        MaterialLayout.FitNextItemWidth(preferredWidth+labelWidth+gap,minimum+labelWidth+gap);
        var available=ImGui.GetContentRegionAvail().X;
        if(labelWidth+gap+minimum>available)
        {
            MaterialText.Text(translated);
            translated=""; labelWidth=0; gap=0;
            available=ImGui.GetContentRegionAvail().X;
        }
        var width=MaterialLayout.FitNextItemWidth(Math.Min(preferredWidth,available-labelWidth-gap),minimum);
        ImGui.SetNextItemWidth(width);
        var min=ImGui.GetCursorScreenPos();
        var previousMax=ImGuiP.GetCurrentWindow().DC.CursorMaxPos;
        var window=ImGui.GetWindowPos();
        // Keep the original native ID and field contents; clip only its English label ink.
        ImGui.GetWindowDrawList().PushClipRect(new Vector2(min.X,window.Y),new Vector2(min.X+width,window.Y+ImGui.GetWindowSize().Y),true);
        return (min,previousMax,width,translated);
    }
    private static void EndField((Vector2 Min,Vector2 PreviousMax,float Width,string Label) field)
    {
        ImGui.GetWindowDrawList().PopClipRect();
        var right=field.Min.X+field.Width;
        if(field.Label.Length>0)
        {
            var p=new Vector2(right+ImGui.GetStyle().ItemInnerSpacing.X,field.Min.Y+(ImGui.GetFrameHeight()-MaterialText.Measure(field.Label).Y)*.5f);
            MaterialText.AddText(ImGui.GetWindowDrawList(),p,ImGui.GetColorU32(ImGuiCol.Text),field.Label);
            right=p.X+MaterialText.Measure(field.Label).X;
        }
        var window=ImGuiP.GetCurrentWindow();
        window.DC.CursorMaxPos=new Vector2(Math.Max(field.PreviousMax.X,right),window.DC.CursorMaxPos.Y);
        window.DC.CursorPosPrevLine=new Vector2(right,window.DC.CursorPosPrevLine.Y);
    }
    internal static bool InputText(string label,ref string value,int length,ImGuiInputTextFlags flags=ImGuiInputTextFlags.None)
    {
        using var height=MaterialText.PushLineHeight(value,UiText.T(label.Split("##",2)[0]));
        var preferredWidth=ImGui.CalcItemWidth();
        var minimum=Math.Max(100*MaterialTheme.Metrics.Scale,MaterialText.Measure("0000000000").X+2*ImGui.GetStyle().FramePadding.X);
        var field=BeginField(label,preferredWidth,minimum);
        using var nativeField=MaterialSkin.NativeFieldStyle();
        var changed=MaterialShapedInput.SingleLine(label,"",ref value,length,flags); EndField(field); return changed;
    }
    internal static bool InputInt(string label,ref int value,int step=1,int fast=100,float? preferredPixels=null)
    {
        var visible=label.Split("##",2)[0];
        var translated=UiText.T(visible);
        if(preferredPixels is { } retainedWidth && !MaterialText.RequiresShaping(translated))
        {
            ImGui.SetNextItemWidth(retainedWidth);
            using var compactField=MaterialSkin.NativeFieldStyle();
            return ImGui.InputInt(translated+label[visible.Length..],ref value,step,fast);
        }
        using var height=MaterialText.PushLineHeight(translated);
        var minimum=preferredPixels ?? Math.Max(100*MaterialTheme.Metrics.Scale,MaterialText.Measure("-000000").X+2*ImGui.GetStyle().FramePadding.X);
        if(step>0) minimum+=2*(ImGui.GetFrameHeight()+ImGui.GetStyle().ItemInnerSpacing.X);
        var field=BeginField(label,preferredPixels ?? 240*MaterialTheme.Metrics.Scale,minimum);
        using var nativeField=MaterialSkin.NativeFieldStyle();
        var changed=ImGui.InputInt(label,ref value,step,fast); EndField(field); return changed;
    }
    internal static bool InputFloat(string label,ref float value,float step=0,float fast=0,string format="%.3f")
    {
        using var height=MaterialText.PushLineHeight(UiText.T(label.Split("##",2)[0]));
        var minimum=Math.Max(100*MaterialTheme.Metrics.Scale,MaterialText.Measure("-000000").X+2*ImGui.GetStyle().FramePadding.X);
        if(step>0) minimum+=2*(ImGui.GetFrameHeight()+ImGui.GetStyle().ItemInnerSpacing.X);
        var field=BeginField(label,240*MaterialTheme.Metrics.Scale,minimum);
        using var nativeField=MaterialSkin.NativeFieldStyle();
        var changed=ImGui.InputFloat(label,ref value,step,fast,format); EndField(field); return changed;
    }
    internal static bool RadioButton(string label,bool active)
    {
        var visible=label.Split("##",2)[0];var translated=UiText.T(visible);
        if(TtslPresentation.HasMaterial)
        {
            using var materialHeight=MaterialText.PushLineHeight(translated);
            var materialGap=ImGui.GetStyle().ItemInnerSpacing;
            var alpha=ImGui.GetStyle().Alpha;
            var list=ImGui.GetWindowDrawList();
            var splitter=ImGui.ImDrawListSplitter();
            bool materialChanged;
            try
            {
                splitter.Split(list,2);splitter.SetCurrentChannel(list,1);
                using(var face=new MaterialStyleScope())
                {
                    face.Color(ImGuiCol.Text,Vector4.Zero);
                    face.Color(ImGuiCol.FrameBg,Vector4.Zero);
                    face.Color(ImGuiCol.FrameBgHovered,Vector4.Zero);
                    face.Color(ImGuiCol.FrameBgActive,Vector4.Zero);
                    face.Color(ImGuiCol.Border,Vector4.Zero);
                    if(MaterialText.RequiresShaping(translated))
                        face.Style(ImGuiStyleVar.ItemInnerSpacing,new Vector2(Math.Max(0,materialGap.X+MaterialText.Measure(translated).X-MaterialText.Measure(visible).X),materialGap.Y));
                    materialChanged=ImGui.RadioButton(label,active);
                }
                splitter.SetCurrentChannel(list,0);
                using var skin=MaterialSkin.NativeButtonStyle();
                using var colors=new MaterialStyleScope();
                colors.Color(ImGuiCol.Button,ImGui.GetStyle().Colors[(int)ImGuiCol.FrameBg]);
                colors.Color(ImGuiCol.ButtonHovered,ImGui.GetStyle().Colors[(int)ImGuiCol.FrameBgHovered]);
                colors.Color(ImGuiCol.ButtonActive,ImGui.GetStyle().Colors[(int)ImGuiCol.FrameBgActive]);
                var min=ImGui.GetItemRectMin();var side=ImGui.GetFrameHeight();
                MaterialSkin.PaintButton(min,min+new Vector2(side),alpha,side*.5f);
            }
            finally { splitter.Merge(list);splitter.Destroy(); }
            MaterialText.AddText(list,ImGui.GetItemRectMin()+new Vector2(ImGui.GetFrameHeight()+materialGap.X,(ImGui.GetFrameHeight()-MaterialText.Measure(translated).Y)*.5f),ImGui.GetColorU32(ImGuiCol.Text),translated);
            return materialChanged;
        }
        if(!MaterialText.RequiresShaping(translated))
        {
            var nativeChanged=ImGui.RadioButton(label,active);
            Label(label,ImGui.GetItemRectMin()+new Vector2(ImGui.GetFrameHeight()+ImGui.GetStyle().ItemInnerSpacing.X,ImGui.GetStyle().FramePadding.Y),MaterialTheme.Current.Colors.Background,MaterialTheme.Current.Colors.OnSurface);
            return nativeChanged;
        }
        using var height=MaterialText.PushLineHeight(translated);
        var gap=ImGui.GetStyle().ItemInnerSpacing;
        var foreground=ImGui.GetColorU32(ImGuiCol.Text);
        ImGui.PushStyleVar(ImGuiStyleVar.ItemInnerSpacing,new Vector2(Math.Max(0,gap.X+MaterialText.Measure(translated).X-MaterialText.Measure(visible).X),gap.Y));
        ImGui.PushStyleColor(ImGuiCol.Text,Vector4.Zero);
        var changed=ImGui.RadioButton(label,active);
        ImGui.PopStyleColor();ImGui.PopStyleVar();
        MaterialText.AddText(ImGui.GetWindowDrawList(),ImGui.GetItemRectMin()+new Vector2(ImGui.GetFrameHeight()+gap.X,(ImGui.GetFrameHeight()-MaterialText.Measure(translated).Y)*.5f),foreground,translated);
        return changed;
    }
    internal static bool Combo(string label,ref int index,string[] options,int count)
    {
        var preferredWidth=ImGui.CalcItemWidth();
        var translated=options.Select(UiText.T).ToArray();
        using var height=MaterialText.PushLineHeight(translated.Take(count).Append(UiText.T(label.Split("##",2)[0])).ToArray());
        var minimum=translated.Take(count).Select(option=>MaterialText.Measure(option).X).DefaultIfEmpty(0).Max()+ImGui.GetFrameHeight()+2*ImGui.GetStyle().FramePadding.X;
        var field=BeginField(label,preferredWidth,minimum);
        var shaped=translated.Take(count).Any(MaterialText.RequiresShaping);
        if(!TtslPresentation.HasMaterial && !shaped)
        {
            var nativeChanged=ImGui.Combo(label,ref index,translated,count);
            EndField(field);return nativeChanged;
        }
        var changed=false;
        var id=ImGui.GetID(label);
        var open=false;
        try
        {
            var preview=index>=0 && index<count?translated[index]:"";
            if(TtslPresentation.HasMaterial)
            {
                var list=ImGui.GetWindowDrawList();
                var font=ImGui.GetFont();var fontSize=ImGui.GetFontSize();
                var color=ImGui.GetColorU32(ImGuiCol.Text);
                var frameHeight=ImGui.GetFrameHeight();var padding=ImGui.GetStyle().FramePadding;
                var measured=MaterialText.Measure(preview);
                using(var nativeField=MaterialSkin.NativeFieldStyle())
                using(var backdrop=MaterialSkin.BackdropStyle(popup:true))
                    open=ImGui.BeginCombo(label,"");
                if(open) MaterialSkin.Backdrop();
                list.PushClipRect(field.Min,field.Min+new Vector2(Math.Max(padding.X,field.Width-frameHeight),frameHeight),true);
                try { MaterialText.AddText(list,font,fontSize,field.Min+new Vector2(padding.X,(frameHeight-measured.Y)*.5f),color,preview); }
                finally { list.PopClipRect(); }
            }
            else open=MaterialText.BeginCombo(label,preview);
            if(open)
            {
                for(var option=0;option<count;option++)
                {
                    // The unshaped path previously used native Combo's integer option IDs.
                    if(shaped) ImGui.PushID(option); else ImGuiNative.PushID(option);
                    try
                    {
                        var selected=index==option;
                        if(MaterialText.Selectable(translated[option],selected))
                        {
                            if(shaped || index!=option) changed=true;
                            index=option;
                        }
                        if(selected) ImGui.SetItemDefaultFocus();
                    }
                    finally { ImGui.PopID(); }
                }
            }
        }
        finally
        {
            if(open) ImGui.EndCombo();
            EndField(field);
        }
        if(changed) ImGuiP.MarkItemEdited(id);
        return changed;
    }
    internal static void TextUnformatted(string text) => MaterialText.Text(UiText.T(text));
    internal static void TextWrapped(string text) => MaterialText.TextWrapped(UiText.T(text));
    internal static void Text(string text)
    {
        ImGui.PushTextWrapPos(0);
        MaterialText.Text(UiText.T(text));
        ImGui.PopTextWrapPos();
    }
    internal static void Text(FormattableString text) => Text(UiText.F(text));
    internal static void TextColored(Vector4 color, string text)
    {
        if (MaterialTheme.Current.IsLcars && color == MaterialTheme.Current.Colors.Primary) { MaterialLcars.PanelHeading(UiText.T(text)); return; }
        if (TtslPresentation.HasMaterial && color == MaterialTheme.Current.Colors.Primary) { MaterialSkin.Heading(UiText.T(text)); return; }
        ImGui.PushTextWrapPos(0);
        MaterialText.TextColored(color, UiText.T(text));
        ImGui.PopTextWrapPos();
    }
    internal static void TextColored(Vector4 color, FormattableString text) => TextColored(color, UiText.F(text));
    internal static void SetTooltip(string text)
    {
        if(!TtslPresentation.HasMaterial) { MaterialText.SetTooltip(UiText.T(text));return; }
        using(MaterialSkin.BackdropStyle(popup:true)) ImGui.BeginTooltip();
        try { MaterialSkin.Backdrop();MaterialText.Text(UiText.T(text)); }
        finally { ImGui.EndTooltip(); }
    }
    private static void Label(string original,Vector2 position,Vector4 background,Vector4 foreground,Vector2? clip=null,string? display=null)
    {
        var visible=original.Split("##",2)[0];
        var translated=display ?? UiText.T(visible);
        if(translated==visible) return;
        var dl=ImGui.GetWindowDrawList();
        var width=Math.Max(MaterialText.Measure(visible).X,MaterialText.Measure(translated).X);
        if(clip is { } max) dl.PushClipRect(position,max,true);
        dl.AddRectFilled(position,position+new Vector2(width,ImGui.GetTextLineHeight()),ImGui.ColorConvertFloat4ToU32(background));
        foreground.W*=ImGui.GetStyle().Alpha;
        MaterialText.AddText(dl,position,ImGui.ColorConvertFloat4ToU32(foreground),translated);
        if(clip.HasValue) dl.PopClipRect();
    }
    internal static bool Button(string label,string? display=null,MaterialUiSoundCue cue=MaterialUiSoundCue.Click)
    {
        var translated=display ?? UiText.T(label.Split("##",2)[0]);
        using var controls = ImGui.GetStyle().FramePadding.Y == 0 || MaterialControls.Context == MaterialControlContext.Dense
            ? default(MaterialControls.ControlScope) : MaterialControls.Push(MaterialControlContext.Toolbar);
        var material=TtslPresentation.HasMaterial;
        var padding=ImGui.GetStyle().FramePadding;
        var radius=ImGui.GetStyle().FrameRounding;
        var alpha=ImGui.GetStyle().Alpha;
        using var nativeButtons=material ? MaterialSkin.NativeButtonStyle() : MaterialLcars.NativeButtonStyle();
        using var rounding=new MaterialStyleScope();
        if(material) rounding.Style(ImGuiStyleVar.FrameRounding,radius);
        using var height=MaterialText.PushLineHeight(translated);
        var width=MaterialText.Measure(translated).X+2*ImGui.GetStyle().FramePadding.X;
        width=MaterialLayout.FitNextItemWidth(width,width);
        var foreground=ImGui.GetStyle().Colors[(int)ImGuiCol.Text];
        ImGui.PushStyleColor(ImGuiCol.Text,Vector4.Zero);
        bool clicked;
        try
        {
            using var face=material ? MaterialSkin.TransparentButtonFace() : new MaterialStyleScope();
            var nativeHeight=material ? Math.Max(ImGui.GetTextLineHeight(),MaterialText.Measure(translated).Y)+2*padding.Y : 0;
            clicked=ImGui.Button(label,new Vector2(width,nativeHeight));
        }
        finally { ImGui.PopStyleColor(); }
        var min=ImGui.GetItemRectMin(); var max=ImGui.GetItemRectMax();
        if(material) MaterialSkin.PaintButton(min,max,alpha,radius);
        foreground.W*=ImGui.GetStyle().Alpha;
        ImGui.GetWindowDrawList().PushClipRect(min,max,true);
        MaterialText.AddText(ImGui.GetWindowDrawList(),min+(max-min-MaterialText.Measure(translated))*.5f,ImGui.ColorConvertFloat4ToU32(foreground),translated);
        ImGui.GetWindowDrawList().PopClipRect();
        MaterialLcars.Feedback(clicked, cue);
        return clicked;
    }
    internal static bool Button(string label, Vector2 pixels)
    {
        var translated = UiText.T(label.Split("##", 2)[0]);
        using var controls = ImGui.GetStyle().FramePadding.Y == 0 || MaterialControls.Context == MaterialControlContext.Dense
            ? default(MaterialControls.ControlScope) : MaterialControls.Push(MaterialControlContext.Toolbar);
        var material=TtslPresentation.HasMaterial;
        var radius=ImGui.GetStyle().FrameRounding;
        var alpha=ImGui.GetStyle().Alpha;
        using var nativeButtons=material ? MaterialSkin.NativeButtonStyle() : MaterialLcars.NativeButtonStyle();
        using var rounding=new MaterialStyleScope();
        if(material) rounding.Style(ImGuiStyleVar.FrameRounding,radius);
        var paddingY = ImGui.GetStyle().FramePadding.Y;
        using var height = MaterialText.PushLineHeight(translated);
        pixels.Y=Math.Max(pixels.Y,Math.Max(ImGui.GetTextLineHeight(),MaterialText.Measure(translated).Y)+2*paddingY);
        pixels.X=MaterialLayout.FitNextItemWidth(pixels.X,pixels.X);
        var color = ImGui.GetStyle().Colors[(int)ImGuiCol.Text];
        ImGui.PushStyleColor(ImGuiCol.Text, Vector4.Zero);
        bool clicked;
        try
        {
            using var face=material ? MaterialSkin.TransparentButtonFace() : new MaterialStyleScope();
            clicked=ImGui.Button(label,pixels);
        }
        finally { ImGui.PopStyleColor(); }
        var min = ImGui.GetItemRectMin(); var max = ImGui.GetItemRectMax();
        if(material) MaterialSkin.PaintButton(min,max,alpha,radius);
        var dl = ImGui.GetWindowDrawList();
        dl.PushClipRect(min, max, true);
        MaterialText.AddText(dl,min + (max - min - MaterialText.Measure(translated)) * .5f, MaterialCanvas.Color(color), translated);
        dl.PopClipRect();
        MaterialLcars.Feedback(clicked);
        return clicked;
    }
    internal static bool SmallButton(string label,string? display=null)
    {
        // Native small buttons use the same ID and behavior with zero vertical padding.
        ImGui.PushStyleVar(ImGuiStyleVar.FramePadding,new Vector2(ImGui.GetStyle().FramePadding.X,0));
        try { return Button(label,display); }
        finally { ImGui.PopStyleVar(); }
    }
    internal static bool Checkbox(string label,ref bool value)
    {
        var visible=label.Split("##",2)[0];
        var translated=UiText.T(visible);
        if(TtslPresentation.HasMaterial)
        {
            using var materialHeight=MaterialText.PushLineHeight(translated);
            var materialGap=ImGui.GetStyle().ItemInnerSpacing;
            var radius=ImGui.GetStyle().FrameRounding;
            var alpha=ImGui.GetStyle().Alpha;
            var list=ImGui.GetWindowDrawList();
            var splitter=ImGui.ImDrawListSplitter();
            var nativeLabel=label.Contains("###",StringComparison.Ordinal) && !MaterialText.RequiresShaping(translated)
                ? translated+label[visible.Length..] : label;
            bool materialChanged;
            try
            {
                splitter.Split(list,2);splitter.SetCurrentChannel(list,1);
                using(var face=new MaterialStyleScope())
                {
                    face.Color(ImGuiCol.Text,Vector4.Zero);
                    face.Color(ImGuiCol.FrameBg,Vector4.Zero);
                    face.Color(ImGuiCol.FrameBgHovered,Vector4.Zero);
                    face.Color(ImGuiCol.FrameBgActive,Vector4.Zero);
                    face.Color(ImGuiCol.Border,Vector4.Zero);
                    if(nativeLabel==label)
                        face.Style(ImGuiStyleVar.ItemInnerSpacing,new Vector2(Math.Max(0,materialGap.X+MaterialText.Measure(translated).X-MaterialText.Measure(visible).X),materialGap.Y));
                    materialChanged=ImGui.Checkbox(nativeLabel,ref value);
                }
                splitter.SetCurrentChannel(list,0);
                using var skin=MaterialSkin.NativeButtonStyle();
                using var colors=new MaterialStyleScope();
                colors.Color(ImGuiCol.Button,ImGui.GetStyle().Colors[(int)ImGuiCol.FrameBg]);
                colors.Color(ImGuiCol.ButtonHovered,ImGui.GetStyle().Colors[(int)ImGuiCol.FrameBgHovered]);
                colors.Color(ImGuiCol.ButtonActive,ImGui.GetStyle().Colors[(int)ImGuiCol.FrameBgActive]);
                var min=ImGui.GetItemRectMin();
                MaterialSkin.PaintButton(min,min+new Vector2(ImGui.GetFrameHeight()),alpha,radius);
            }
            finally { splitter.Merge(list);splitter.Destroy(); }
            MaterialText.AddText(list,ImGui.GetItemRectMin()+new Vector2(ImGui.GetFrameHeight()+materialGap.X,(ImGui.GetFrameHeight()-MaterialText.Measure(translated).Y)*.5f),ImGui.GetColorU32(ImGuiCol.Text),translated);
            MaterialLcars.Feedback(materialChanged,MaterialUiSoundCue.Toggle);
            return materialChanged;
        }
        if(label.Contains("###",StringComparison.Ordinal) && !MaterialText.RequiresShaping(translated))
        {
            var nativeChanged = ImGui.Checkbox(translated+label[visible.Length..],ref value);
            MaterialLcars.Feedback(nativeChanged, MaterialUiSoundCue.Toggle);
            return nativeChanged;
        }
        using var height=MaterialText.PushLineHeight(translated);
        var foreground=ImGui.GetStyle().Colors[(int)ImGuiCol.Text];
        var gap=ImGui.GetStyle().ItemInnerSpacing;
        // Native Checkbox sizes its hit area from the original label. Adjust that size for the
        // translated ink while keeping the native widget and its original ID.
        ImGui.PushStyleVar(ImGuiStyleVar.ItemInnerSpacing,new Vector2(Math.Max(0,gap.X+MaterialText.Measure(translated).X-MaterialText.Measure(visible).X),gap.Y));
        ImGui.PushStyleColor(ImGuiCol.Text,Vector4.Zero);
        var changed=ImGui.Checkbox(label,ref value);
        ImGui.PopStyleColor();
        ImGui.PopStyleVar();
        var p=ImGui.GetItemRectMin()+new Vector2(ImGui.GetFrameHeight()+gap.X,(ImGui.GetFrameHeight()-MaterialText.Measure(translated).Y)*.5f);
        foreground.W*=ImGui.GetStyle().Alpha;
        MaterialText.AddText(ImGui.GetWindowDrawList(),p,ImGui.ColorConvertFloat4ToU32(foreground),translated);
        MaterialLcars.Feedback(changed, MaterialUiSoundCue.Toggle);
        return changed;
    }
    internal static void Title(string original,string translated)
        => TitleWithButtons(original, translated, null);

    internal static unsafe void ImageTitle(Window owner, string visibleTitle,
        Dalamud.Interface.Textures.TextureWraps.IDalamudTextureWrap icon)
        => ImageTitleWithStatus(owner, visibleTitle, icon, null);

    internal static unsafe void ImageTitleWithStatus(Window owner, string visibleTitle,
        Dalamud.Interface.Textures.TextureWraps.IDalamudTextureWrap icon, MaterialTitlebarStatus? status)
    {
        var window = ImGuiP.FindWindowByName(owner.WindowName);
        if (window.Handle == null) return;
        var count = owner.TitleBarButtons.Count(button => !owner.IsClickthrough || button.AvailableClickthrough);
        if (owner.AllowPinning || owner.AllowClickthrough || owner.AllowBackgroundBlur) count++;
        var extraRight = count * (ImGuiP.CalcFontSize(window) + ImGui.GetStyle().ItemInnerSpacing.X);
        using var font = UiText.Font(UiFontRole.Body);
        if (status is { } summary)
            MaterialWindowHeader.PaintTitleWithStatus(window, visibleTitle, icon.Handle, icon.Size, summary, extraRight, owner.ShowCloseButton);
        else
            MaterialWindowHeader.PaintTitle(window, visibleTitle, icon.Handle, icon.Size, extraRight, owner.ShowCloseButton);
    }

    internal static void TitleWithButtons(string original,string translated, Window? owner)
    {
        var s=ImGui.GetStyle(); var size=ImGui.GetFontSize();var height=ImGui.GetFrameHeight();
        var flags=ImGuiP.GetCurrentWindow().Flags;
        var collapseOnLeft=(flags & (ImGuiWindowFlags.NoCollapse|ImGuiWindowFlags.Modal))==0 && s.WindowMenuButtonPosition==ImGuiDir.Left;
        var position=ImGui.GetWindowPos()+new Vector2(s.FramePadding.X+(collapseOnLeft?size+s.ItemInnerSpacing.X:0),s.FramePadding.Y);
        var originalWidth=MaterialText.Measure(original).X;
        using var font=UiText.Font(UiFontRole.Body);
        var translatedSize=MaterialText.Measure(translated)*size/ImGui.GetFontSize();
        var translatedWidth=translatedSize.X;
        if(MaterialText.RequiresShaping(translated)) position.Y=ImGui.GetWindowPos().Y+(height-translatedSize.Y)*.5f;
        var dl=ImGui.GetWindowDrawList();
        var reserved = 2 * height;
        if (owner is not null)
        {
            var count = owner.TitleBarButtons.Count(button => !owner.IsClickthrough || button.AvailableClickthrough);
            if (owner.AllowPinning || owner.AllowClickthrough || owner.AllowBackgroundBlur) count++;
            reserved = size + 2 * s.FramePadding.X + count * (size + s.ItemInnerSpacing.X);
            if ((flags & ImGuiWindowFlags.NoCollapse) == 0 && s.WindowMenuButtonPosition == ImGuiDir.Right)
                reserved += size + s.ItemInnerSpacing.X;
        }
        dl.PushClipRect(ImGui.GetWindowPos(),ImGui.GetWindowPos()+new Vector2(Math.Max(0,ImGui.GetWindowSize().X-reserved),height),false);
        var bg=s.Colors[(int)(ImGui.IsWindowFocused(ImGuiFocusedFlags.RootAndChildWindows)?ImGuiCol.TitleBgActive:ImGuiCol.TitleBg)];
        dl.AddRectFilled(position,position+new Vector2(Math.Max(originalWidth,translatedWidth),height-s.FramePadding.Y),ImGui.ColorConvertFloat4ToU32(bg));
        MaterialText.AddText(dl,ImGui.GetFont(),size,position,ImGui.ColorConvertFloat4ToU32(s.Colors[(int)ImGuiCol.Text]),translated);
        dl.PopClipRect();
    }
    internal static void TableHeadersRow(float height=0)
    {
        for(var index=0;index<ImGui.TableGetColumnCount();index++)
        {
            var original=ImGui.TableGetColumnName(index);
            var translated=UiText.T(original=="Dist"?"Distance":original);
            if(MaterialText.RequiresShaping(translated)) height=Math.Max(height,MaterialText.Measure(translated).Y+2*ImGui.GetStyle().CellPadding.Y);
        }
        ImGui.TableNextRow(ImGuiTableRowFlags.Headers,height);
        for(var index=0;index<ImGui.TableGetColumnCount();index++)
        {
            if(!ImGui.TableSetColumnIndex(index)) continue;
            var original=ImGui.TableGetColumnName(index);
            var position=ImGui.GetCursorScreenPos();
            var available=ImGui.GetContentRegionAvail().X;
            ImGui.TableHeader(original);
            var translated=UiText.T(original=="Dist"?"Distance":original);
            Label(original,position,ImGui.GetStyle().Colors[(int)ImGuiCol.TableHeaderBg],ImGui.GetStyle().Colors[(int)ImGuiCol.Text], position + new Vector2(Math.Max(1, available), Math.Max(ImGui.GetTextLineHeight(),MaterialText.Measure(translated).Y)),translated);
            if(translated!=original && MaterialText.Measure(translated).X>available-16*AethertekUI.MaterialTheme.Metrics.Scale && ImGui.IsItemHovered())
                SetTooltip(translated);
        }
    }
}
