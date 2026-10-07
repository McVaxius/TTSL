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
        var s=MaterialTheme.Metrics.Scale;var c=MaterialTheme.Current.Colors;
        var label=UiText.T(original);var size=new Vector2(Math.Max(156*s,MaterialText.Measure(label).X+76*s),TtslPresentation.ControlHeight*s);
        size.X=MaterialLayout.FitNextItemWidth(size.X,size.X);
        ImGui.PushStyleColor(ImGuiCol.Button,value?MaterialColor.Layer(c.SurfaceContainer,c.Primary,.15f):c.SurfaceContainerHigh);
        ImGui.PushStyleColor(ImGuiCol.ButtonHovered,MaterialColor.Layer(c.SurfaceContainerHigh,c.Primary,.22f));
        ImGui.PushStyleColor(ImGuiCol.ButtonActive,MaterialColor.Layer(c.SurfaceContainerHigh,c.Primary,.30f));
        ImGui.PushStyleColor(ImGuiCol.Text,Vector4.Zero);
        size.Y=Math.Max(size.Y,MaterialText.Measure(label).Y+2*ImGui.GetStyle().FramePadding.Y);
        var clicked=ImGui.Button(original,size);ImGui.PopStyleColor(4);
        var min=ImGui.GetItemRectMin();var max=ImGui.GetItemRectMax();var dl=ImGui.GetWindowDrawList();
        dl.AddRect(min,max,MaterialCanvas.Color(value?c.Primary:c.OutlineVariant),4*s);
        MaterialIcons.Draw(icon,min+new Vector2(14*s,(size.Y-24*s)*.5f),24*s,c.OnSurface);
        MaterialText.AddText(dl,min+new Vector2(50*s,(size.Y-MaterialText.Measure(label).Y)*.5f),MaterialCanvas.Color(c.OnSurface),label);
        if(clicked)value=!value;return clicked;
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
        var changed=MaterialShapedInput.SingleLine(label,"",ref value,length,flags); EndField(field); return changed;
    }
    internal static bool InputInt(string label,ref int value,int step=1,int fast=100,float? preferredPixels=null)
    {
        var visible=label.Split("##",2)[0];
        var translated=UiText.T(visible);
        if(preferredPixels is { } retainedWidth && !MaterialText.RequiresShaping(translated))
        {
            ImGui.SetNextItemWidth(retainedWidth);
            return ImGui.InputInt(translated+label[visible.Length..],ref value,step,fast);
        }
        using var height=MaterialText.PushLineHeight(translated);
        var minimum=preferredPixels ?? Math.Max(100*MaterialTheme.Metrics.Scale,MaterialText.Measure("-000000").X+2*ImGui.GetStyle().FramePadding.X);
        if(step>0) minimum+=2*(ImGui.GetFrameHeight()+ImGui.GetStyle().ItemInnerSpacing.X);
        var field=BeginField(label,preferredPixels ?? 240*MaterialTheme.Metrics.Scale,minimum);
        var changed=ImGui.InputInt(label,ref value,step,fast); EndField(field); return changed;
    }
    internal static bool InputFloat(string label,ref float value,float step=0,float fast=0,string format="%.3f")
    {
        using var height=MaterialText.PushLineHeight(UiText.T(label.Split("##",2)[0]));
        var minimum=Math.Max(100*MaterialTheme.Metrics.Scale,MaterialText.Measure("-000000").X+2*ImGui.GetStyle().FramePadding.X);
        if(step>0) minimum+=2*(ImGui.GetFrameHeight()+ImGui.GetStyle().ItemInnerSpacing.X);
        var field=BeginField(label,240*MaterialTheme.Metrics.Scale,minimum);
        var changed=ImGui.InputFloat(label,ref value,step,fast,format); EndField(field); return changed;
    }
    internal static bool RadioButton(string label,bool active)
    {
        var visible=label.Split("##",2)[0];var translated=UiText.T(visible);
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
        if(!translated.Take(count).Any(MaterialText.RequiresShaping))
        {
            var nativeChanged=ImGui.Combo(label,ref index,translated,count);
            EndField(field);return nativeChanged;
        }
        var changed=false;
        var id=ImGui.GetID(label);
        var open=MaterialText.BeginCombo(label,index>=0 && index<count?translated[index]:"");
        if(open)
        {
            try
            {
                for(var option=0;option<count;option++)
                {
                    ImGui.PushID(option);
                    try
                    {
                        var selected=index==option;
                        if(MaterialText.Selectable(translated[option],selected)) { index=option;changed=true; }
                        if(selected) ImGui.SetItemDefaultFocus();
                    }
                    finally { ImGui.PopID(); }
                }
            }
            finally { ImGui.EndCombo(); }
        }
        EndField(field);
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
        ImGui.PushTextWrapPos(0);
        MaterialText.TextColored(color, UiText.T(text));
        ImGui.PopTextWrapPos();
    }
    internal static void TextColored(Vector4 color, FormattableString text) => TextColored(color, UiText.F(text));
    internal static void SetTooltip(string text) => MaterialText.SetTooltip(UiText.T(text));
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
    internal static bool Button(string label,string? display=null)
    {
        var translated=display ?? UiText.T(label.Split("##",2)[0]);
        using var height=MaterialText.PushLineHeight(translated);
        var width=MaterialText.Measure(translated).X+2*ImGui.GetStyle().FramePadding.X;
        width=MaterialLayout.FitNextItemWidth(width,width);
        var foreground=ImGui.GetStyle().Colors[(int)ImGuiCol.Text];
        ImGui.PushStyleColor(ImGuiCol.Text,Vector4.Zero);
        var clicked=ImGui.Button(label,new Vector2(width,0));
        ImGui.PopStyleColor();
        var min=ImGui.GetItemRectMin(); var max=ImGui.GetItemRectMax();
        foreground.W*=ImGui.GetStyle().Alpha;
        ImGui.GetWindowDrawList().PushClipRect(min,max,true);
        MaterialText.AddText(ImGui.GetWindowDrawList(),min+(max-min-MaterialText.Measure(translated))*.5f,ImGui.ColorConvertFloat4ToU32(foreground),translated);
        ImGui.GetWindowDrawList().PopClipRect();
        return clicked;
    }
    internal static bool Button(string label, Vector2 pixels)
    {
        var translated = UiText.T(label.Split("##", 2)[0]);
        pixels.Y=Math.Max(pixels.Y,MaterialText.Measure(translated).Y+2*ImGui.GetStyle().FramePadding.Y);
        pixels.X=MaterialLayout.FitNextItemWidth(pixels.X,pixels.X);
        var color = ImGui.GetStyle().Colors[(int)ImGuiCol.Text];
        ImGui.PushStyleColor(ImGuiCol.Text, Vector4.Zero);
        var clicked = ImGui.Button(label, pixels);
        ImGui.PopStyleColor();
        var min = ImGui.GetItemRectMin(); var max = ImGui.GetItemRectMax();
        var dl = ImGui.GetWindowDrawList();
        dl.PushClipRect(min, max, true);
        MaterialText.AddText(dl,min + (max - min - MaterialText.Measure(translated)) * .5f, MaterialCanvas.Color(color), translated);
        dl.PopClipRect();
        return clicked;
    }
    internal static bool SmallButton(string label,string? display=null)
    {
        // Native small buttons use the same ID and behavior with zero vertical padding.
        ImGui.PushStyleVar(ImGuiStyleVar.FramePadding,new Vector2(ImGui.GetStyle().FramePadding.X,0));
        var clicked=Button(label,display);
        ImGui.PopStyleVar();
        return clicked;
    }
    internal static bool Checkbox(string label,ref bool value)
    {
        var visible=label.Split("##",2)[0];
        var translated=UiText.T(visible);
        if(label.Contains("###",StringComparison.Ordinal) && !MaterialText.RequiresShaping(translated))
            return ImGui.Checkbox(translated+label[visible.Length..],ref value);
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
        return changed;
    }
    internal static void Title(string original,string translated)
        => TitleWithButtons(original, translated, null);

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
                MaterialText.SetTooltip(translated);
        }
    }
}
