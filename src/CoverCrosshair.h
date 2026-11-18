#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
namespace CoverCrosshair {
inline constexpr uint32_t Size=128;
// Fixed angular size, at the actual target depth. No controller or head-centre aim.
inline float Width(float distance) {return distance*.012f;}
inline bool Valid(const float* p) {
    return std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2]);
}
inline bool Endpoint(const float* frame,float range,float* end) {
    if(!std::isfinite(range)||range<=0||range>1e8f)return false;
    const float n=frame[8]*frame[8]+frame[9]*frame[9]+frame[10]*frame[10];
    if(!std::isfinite(n)||std::abs(n-1.f)>.02f)return false;
    for(int i=0;i<3;i++)end[i]=frame[12+i]+frame[8+i]*range;
    return Valid(end)&&Valid(frame+12);
}
inline std::vector<uint32_t> Pixels() {
    std::vector<uint32_t> pixels(Size*Size);
    // White cross with a black outline and an open centre. Opaque pixels
    // are also premultiplied-alpha correct on either RGBA or BGRA textures.
    for(uint32_t y=0;y<Size;y++)for(uint32_t x=0;x<Size;x++) {
        const float a=std::abs(float(x)-63.5f),b=std::abs(float(y)-63.5f);
        bool inner=(a<=1.5f&&b>=9&&b<=45)||(b<=1.5f&&a>=9&&a<=45);
        bool outer=(a<=3.5f&&b>=7&&b<=47)||(b<=3.5f&&a>=7&&a<=47);
        pixels[y*Size+x]=inner?0xffffffffu:outer?0xff000000u:0;
    }
    return pixels;
}
}
