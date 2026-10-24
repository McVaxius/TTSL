#pragma once
#include <cmath>
#include <commdlg.h>
#include <mmsystem.h>
#include <memory>
#include <vector>
#include <string_view>
#include "ui_strings.h"

namespace TtslUi {
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
