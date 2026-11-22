#pragma once
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cstdlib>
// [VR] ExperimentalRenderScale: multiplies the size the VR runtime recommends.
// 1.0 or absent is off. Settings file only, and read when the game starts.
namespace RenderScale {
inline constexpr float Off=1.f,Limit=2.f;
struct Size {uint32_t width,height;};
inline float Parse(const wchar_t* text) {
    wchar_t* end{};const float value=wcstof(text,&end);
    if(end==text||*end||!std::isfinite(value))return Off;
    return value<Off?Off:value>Limit?Limit:value;
}
inline Size Apply(uint32_t width,uint32_t height,uint32_t maxWidth,uint32_t maxHeight,float scale) {
    if(!width||!height||!std::isfinite(scale))return {width,height};
    double factor=scale;
    if(maxWidth&&double(maxWidth)/width<factor)factor=double(maxWidth)/width;
    if(maxHeight&&double(maxHeight)/height<factor)factor=double(maxHeight)/height;
    if(factor<=1)return {width,height};
    Size out{uint32_t(width*factor/2+.5)*2,uint32_t(height*factor/2+.5)*2};
    if(maxWidth&&out.width>maxWidth)out.width-=2;
    if(maxHeight&&out.height>maxHeight)out.height-=2;
    return out;
}
}
