#pragma once
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cstdlib>
// [VR] Brightness and Contrast: -50 to 50 in steps of 5. 0 and 0 is a plain copy of the
// game's image. Brightness bends the midtones and keeps black black and white white.
namespace ImageAdjust {
inline constexpr int Min=-50,Max=50,Step=5;
struct Settings {
    int brightness{},contrast{};
    constexpr bool Neutral() const {return brightness==0&&contrast==0;}
};
inline constexpr int Clean(int v) {
    if(v<Min)v=Min;
    if(v>Max)v=Max;
    return (v>=0?v+Step/2:v-Step/2)/Step*Step;
}
// Between the game and the companion: value + 101. 0 = unavailable or keep.
inline constexpr uint32_t Encode(int v) {return uint32_t(Clean(v)+101);}
inline constexpr int Decode(uint32_t wire) {return int(wire)-101;}
inline constexpr bool ValidWire(uint32_t wire) {
    return wire==0||(wire>=uint32_t(Min+101)&&wire<=uint32_t(Max+101)&&(int(wire)-101)%Step==0);
}
inline int Parse(const wchar_t* text) {
    wchar_t* end{};const long value=wcstol(text,&end,10);
    if(end==text||*end)return 0;
    return Clean(value<Min?Min:value>Max?Max:int(value));
}
inline float Exponent(int brightness) {return std::pow(2.f,-float(brightness)/50.f);}
inline float ContrastFactor(int contrast) {return 1.f+float(contrast)/100.f;}
// CPU mirror of the shader in ImagePass.h, on display-encoded 0-1 values.
inline float Apply(float c,const Settings& s) {
    c=c<0?0:c>1?1:c;
    c=std::pow(c,Exponent(s.brightness));
    c=(c-.5f)*ContrastFactor(s.contrast)+.5f;
    return c<0?0:c>1?1:c;
}
inline int Read(const wchar_t* key,const wchar_t* path) {
    wchar_t text[16]{};GetPrivateProfileStringW(L"VR",key,L"0",text,16,path);
    return Parse(text);
}
// GetPrivateProfileInt cannot read negative values, so this reads the text back.
inline bool Write(const wchar_t* key,uint32_t wire,const wchar_t* path) {
    wchar_t text[16];swprintf_s(text,L"%d",Decode(wire));
    if(!WritePrivateProfileStringW(L"VR",key,text,path))return false;
    wchar_t back[16]{};GetPrivateProfileStringW(L"VR",key,L"",back,16,path);
    return !wcscmp(text,back);
}
}
