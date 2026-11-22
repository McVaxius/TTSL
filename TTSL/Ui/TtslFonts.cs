using Dalamud.Bindings.ImGui;
using Dalamud.Interface.ManagedFontAtlas;
using AethertekUI;
using AethertekUI.Dalamud;

namespace TTSL.Ui;

internal sealed class TtslFonts : IDisposable
{
    private readonly IFontHandle[] handles;
    private int generation;
    internal int Generation => System.Threading.Volatile.Read(ref generation);
    internal TtslFonts(IFontAtlas atlas, ushort[] ranges, string language, string assetRoot)
    {
        handles = TtslPresentation.FontSizes.Select((_, index) => MaterialFontAtlas.CreateFont(
            atlas,
            TtslPresentation.FontWeights[index],
            TtslPresentation.AtlasHeight((UiFontRole)index),
            ranges,
            language,
            assetRoot)).ToArray();
        foreach(var handle in handles) handle.ImFontChanged+=FontChanged;
    }
    private void FontChanged(IFontHandle handle,ILockedImFont font) => System.Threading.Interlocked.Increment(ref generation);
    internal bool Ready => handles.All(h=>h.Available && h.LoadException is null);
    internal Exception? LoadException => handles.FirstOrDefault(h=>h.LoadException is not null)?.LoadException;
    internal unsafe void CheckGlyphs(IEnumerable<string> strings)
    {
        for(var index=0; index<handles.Length; index++)
        {
            using var font=handles[index].Lock();
            foreach(var text in strings.Select(MaterialText.NativeGlyphText))
                foreach(var character in text.Where(c=>!char.IsControl(c)))
                    if(ImGui.FindGlyphNoFallback(font.ImFont,character).Handle==null)
                        throw new InvalidOperationException("Required UI glyph missing: U+"+((int)character).ToString("X4")+" in "+(UiFontRole)index);
        }
    }
    internal IDisposable Push(UiFontRole role)
    {
        var handle=handles[(int)role];
        if(!handle.Available || handle.LoadException is not null) throw new InvalidOperationException("Thick Thighs Save Lives fonts are not ready.",handle.LoadException);
        return handle.Push();
    }
    public void Dispose() { foreach(var handle in handles) { handle.ImFontChanged-=FontChanged;handle.Dispose(); } }
}
