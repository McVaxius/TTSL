#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <commdlg.h>
#include <mmsystem.h>
#include <memory>
#include <vector>
#include <string_view>
#include "ui_strings.h"

namespace TtslUi {
struct Palette { unsigned background,controls,text; };
inline constexpr Palette PickerDefaults{0x111F29,0x1CC9E6,0xE9F2F7};
inline constexpr Palette GlassDefaults{0x10232D,0x65C7D2,0xEDF7FA};
inline constexpr Palette SpatialDefaults{0xE8EDF4,0x6678A8,0x1C2538};
inline constexpr Palette CyberpunkDefaults{0x0B0D13,0x19DCE8,0xEAF8FF};
inline constexpr std::array<unsigned,3> GlassWaveDefaults={0x65C7D2,0xA3B2F2,0xDE9EC9};
inline COLORREF Color(unsigned rgb) { return RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255); }
inline COLORREF Mix(COLORREF first,COLORREF second,double amount) {
    amount=std::clamp(amount,0.,1.);
    const auto channel=[&](int a,int b){return static_cast<int>(std::lround(a+(b-a)*amount));};
    return RGB(channel(GetRValue(first),GetRValue(second)),channel(GetGValue(first),GetGValue(second)),channel(GetBValue(first),GetBValue(second)));
}
inline COLORREF Contrast(COLORREF color) {
    return .2126*GetRValue(color)+.7152*GetGValue(color)+.0722*GetBValue(color)>150?RGB(7,9,13):RGB(248,250,253);
}
// All material ink is bounded to the supplied owner/control rectangle; no desktop capture or blur.
inline void GlassBackdrop(HDC dc,RECT rect,const Palette& palette,const std::array<unsigned,3>& waves,double time) {
    constexpr int columns=96,rows=64;
    std::array<DWORD,columns*rows> pixels{};
    for(int y=0;y<rows;++y)for(int x=0;x<columns;++x) {
        const double u=x/double(columns-1),v=y/double(rows-1);
        const double d1=u-(.22+.38*v+.11*std::sin(v*5-time*.9));
        const double d2=u-(.84-.38*v+.13*std::sin(v*5.2+time*.72+.8));
        const double d3=v-(.80-.68*u+.14*std::sin(u*5.3-time*.83+.7));
        const double band1=std::exp(-d1*d1*45),band2=std::exp(-d2*d2*55),band3=std::exp(-d3*d3*65);
        auto color=Mix(Color(palette.background),Color(waves[1]),.14+.07*u+.04*v);
        color=Mix(color,Color(waves[0]),band1*.58);color=Mix(color,Color(waves[1]),band2*.50);color=Mix(color,Color(waves[2]),band3*.50);
        color=Mix(color,Color(palette.text),std::pow(std::max({band1,band2,band3}),8)*.10);
        pixels[y*columns+x]=(GetRValue(color)<<16)|(GetGValue(color)<<8)|GetBValue(color);
    }
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=columns;info.bmiHeader.biHeight=-rows;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    const int saved=SaveDC(dc);SetStretchBltMode(dc,HALFTONE);SetBrushOrgEx(dc,0,0,nullptr);
    StretchDIBits(dc,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,0,0,columns,rows,pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);RestoreDC(dc,saved);
}
inline void MaterialSurface(HDC dc,RECT rect,int mode,const Palette& palette,bool inset=false,bool pressed=false,bool disabled=false,bool emphasized=false) {
    if(rect.right<=rect.left||rect.bottom<=rect.top)return;
    const int saved=SaveDC(dc);IntersectClipRect(dc,rect.left,rect.top,rect.right,rect.bottom);
    const auto base=Color(palette.background),accent=Color(palette.controls),text=Color(palette.text);
    auto face=Mix(base,text,mode==4?(inset?.035:.12):mode==3?.13:.045);
    if(emphasized)face=accent;
    if(pressed)face=Mix(face,accent,.18);
    auto edge=Mix(base,accent,disabled?.16:mode==3?.60:.42);
    const int radius=mode==5?3:mode==4?(inset?6:12):11;
    if(mode==4) {
        auto shadow=CreateSolidBrush(Mix(base,RGB(0,0,0),inset?.23:.16));auto old=SelectObject(dc,shadow);SelectObject(dc,GetStockObject(NULL_PEN));
        RoundRect(dc,rect.left+2,rect.top+3,rect.right,rect.bottom,radius*2,radius*2);SelectObject(dc,old);DeleteObject(shadow);
        rect.right-=2;rect.bottom-=3;
    }
    auto brush=CreateSolidBrush(face);auto pen=CreatePen(PS_SOLID,1,edge);SelectObject(dc,brush);SelectObject(dc,pen);
    if(mode==5) {
        const int cut=std::min(10L,std::min(rect.right-rect.left,rect.bottom-rect.top)/3);
        POINT plate[]={{rect.left+cut,rect.top},{rect.right-1,rect.top},{rect.right-1,rect.bottom-cut},{rect.right-cut,rect.bottom-1},{rect.left,rect.bottom-1},{rect.left,rect.top+cut}};Polygon(dc,plate,6);
        const auto ice=Mix(accent,text,.52);SetDCPenColor(dc,ice);SelectObject(dc,GetStockObject(DC_PEN));
        MoveToEx(dc,rect.left+cut+4,rect.top+2,nullptr);LineTo(dc,std::min(rect.right-4,rect.left+cut+38),rect.top+2);
        const auto magenta=RGB(GetBValue(accent),GetRValue(accent)/2,GetGValue(accent));SetDCPenColor(dc,Mix(base,magenta,.72));
        MoveToEx(dc,rect.right-cut-20,rect.bottom-3,nullptr);LineTo(dc,rect.right-cut-3,rect.bottom-3);
        MoveToEx(dc,rect.right-18,rect.top+1,nullptr);LineTo(dc,rect.right-7,rect.top+1);LineTo(dc,rect.right-2,rect.top+6);LineTo(dc,rect.right-2,rect.top+13);
        SetDCPenColor(dc,accent);MoveToEx(dc,rect.left+1,rect.bottom-14,nullptr);LineTo(dc,rect.left+1,rect.bottom-7);LineTo(dc,rect.left+7,rect.bottom-2);LineTo(dc,rect.left+19,rect.bottom-2);
    } else {
        RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,radius*2,radius*2);
        if(mode==3){const int frosted=SaveDC(dc);auto clip=CreateRoundRectRgn(rect.left+1,rect.top+1,rect.right-1,rect.bottom-1,radius*2,radius*2);ExtSelectClipRgn(dc,clip,RGN_AND);DeleteObject(clip);
            for(int band=0;band<24;++band){RECT strip{rect.left,rect.top+(rect.bottom-rect.top)*band/24,rect.right,rect.top+(rect.bottom-rect.top)*(band+1)/24};SetDCBrushColor(dc,Mix(face,accent,.025+.065*(1-band/23.)));FillRect(dc,&strip,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));}RestoreDC(dc,frosted);}
        auto highlight=CreatePen(PS_SOLID,1,Mix(face,text,mode==3?.48:.65));SelectObject(dc,highlight);
        MoveToEx(dc,rect.left+radius,rect.top+1,nullptr);LineTo(dc,rect.right-radius,rect.top+1);
        if(mode==4&&inset){SelectObject(dc,pen);MoveToEx(dc,rect.left+2,rect.top+radius,nullptr);LineTo(dc,rect.left+2,rect.bottom-radius);}
        SelectObject(dc,pen);DeleteObject(highlight);
        if(mode==3&&!inset){auto glow=CreatePen(PS_SOLID,2,Mix(face,accent,.26));SelectObject(dc,glow);MoveToEx(dc,rect.left+radius,rect.bottom-3,nullptr);LineTo(dc,rect.right-radius,rect.bottom-3);SelectObject(dc,pen);DeleteObject(glow);}
    }
    RestoreDC(dc,saved);DeleteObject(brush);DeleteObject(pen);
}
// One optional native UI voice, independent of speech and other applications.
class SoundPlayer {
    struct Buffer { WAVEHDR header{}; std::vector<short> samples; };
    HWAVEOUT device_ = nullptr;
    std::unique_ptr<Buffer> buffer_;
public:
    bool unavailable = false;
    bool Stop() {
        if (!buffer_) return true;
        if (waveOutReset(device_) != MMSYSERR_NOERROR ||
            waveOutUnprepareHeader(device_, &buffer_->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
            return !(unavailable = true);
        buffer_.reset(); return true;
    }
    bool Play(bool preview = false) {
        if (!Stop()) return false;
        if (!device_) {
            WAVEFORMATEX format{WAVE_FORMAT_PCM, 1, 22050, 44100, 2, 16, 0};
            if (waveOutOpen(&device_, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
                return !(unavailable = true);
        }
        buffer_ = std::make_unique<Buffer>();
        const double duration = preview ? .21 : .075;
        buffer_->samples.resize(static_cast<size_t>(22050 * duration));
        double phase = 0;
        for (size_t i = 0; i < buffer_->samples.size(); ++i) {
            const double fraction = static_cast<double>(i) / (buffer_->samples.size() - 1);
            const double hz = preview ? (fraction < .34 ? 880 : fraction < .67 ? 1174 : 1568) : 1320;
            phase += 6.283185307179586 * hz / 22050;
            buffer_->samples[i] = static_cast<short>(32767 * .10 * std::sin(3.141592653589793 * fraction) *
                std::exp(-2 * fraction) * std::sin(phase));
        }
        auto& header = buffer_->header;
        header.lpData = reinterpret_cast<LPSTR>(buffer_->samples.data());
        header.dwBufferLength = static_cast<DWORD>(buffer_->samples.size() * sizeof(short));
        if (waveOutPrepareHeader(device_, &header, sizeof(header)) != MMSYSERR_NOERROR) {
            buffer_.reset(); return !(unavailable = true);
        }
        if (waveOutWrite(device_, &header, sizeof(header)) != MMSYSERR_NOERROR) {
            Stop(); return !(unavailable = true);
        }
        unavailable = false; return true;
    }
    ~SoundPlayer() {
        if (Stop()) { if (device_) waveOutClose(device_); }
        else buffer_.release(); // A failing driver may still own the submitted buffer.
    }
};
struct Lch { double l,c,h; };
inline Lch ToLch(unsigned rgb) {
    auto linear=[](double c){ return c<=.04045?c/12.92:std::pow((c+.055)/1.055,2.4); };
    auto r=linear(((rgb>>16)&255)/255.),g=linear(((rgb>>8)&255)/255.),b=linear((rgb&255)/255.);
    auto l=std::cbrt(.4122214708*r+.5363325363*g+.0514459929*b);
    auto m=std::cbrt(.2119034982*r+.6806995451*g+.1073969566*b);
    auto s=std::cbrt(.0883024619*r+.2817188376*g+.6299787005*b);
    auto a=1.9779984951*l-2.4285922050*m+.4505937099*s;
    auto bb=.0259040371*l+.7827717662*m-.8086757660*s;
    return {.2104542553*l+.7936177850*m-.0040720468*s,std::hypot(a,bb),std::atan2(bb,a)};
}
inline COLORREF Relative(unsigned reference,unsigned accent) {
    if(accent==0x1CC9E6) return RGB((reference>>16)&255,(reference>>8)&255,reference&255);
    auto value=ToLch(reference),seed=ToLch(accent),original=ToLch(0x1CC9E6);
    value.h+=seed.c<.001?0:seed.h-original.h;value.c*=seed.c<.001?0:seed.c/original.c;
    double r=0,g=0,b=0;
    for(int attempt=0;attempt<32;++attempt) {
        auto a=value.c*std::cos(value.h),bb=value.c*std::sin(value.h);
        auto l=value.l+.3963377774*a+.2158037573*bb,m=value.l-.1055613458*a-.0638541728*bb,s=value.l-.0894841775*a-1.2914855480*bb;
        l=l*l*l;m=m*m*m;s=s*s*s;
        r=4.0767416621*l-3.3077115913*m+.2309699292*s;g=-1.2684380046*l+2.6097574011*m-.3413193965*s;b=-.0041960863*l-.7034186147*m+1.7076147010*s;
        if(r>=0&&r<=1&&g>=0&&g<=1&&b>=0&&b<=1) break;
        value.c*=.85;
    }
    auto channel=[](double v){v=std::clamp(v,0.,1.);return static_cast<int>(std::round(255*(v<=.0031308?12.92*v:1.055*std::pow(v,1/2.4)-.055)));};
    return RGB(channel(r),channel(g),channel(b));
}
inline std::string_view Text(std::string_view english,std::string_view language) {
    size_t index=0;for(size_t i=0;i<Languages.size();++i) if(Languages[i]==language) index=i;
    for(const auto& row:Strings) if(row[0]==english) return row[index];
    return english;
}
}
