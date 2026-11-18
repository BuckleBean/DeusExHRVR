#pragma once
// Companion side of the grenade landing marker: a flat ring lying on the
// ground at the point the game DLL predicted (RenderInfo::marker*, in the
// same OpenXR space and frame as the projection layer). Drawn as a quad
// composition layer, so it sits at the right place and depth in both eyes
// but is not hidden by objects in front of it.
#include <cmath>
#include <cstdint>
#include <vector>

namespace ThrowMarker {
inline constexpr uint32_t Size=256;         // texture, square
inline constexpr float DiameterMetres=0.7f; // in the headset's metres

// RGBA8 (byte order R,G,B,A in memory), colour premultiplied by alpha as the
// OpenXR compositor assumes for source-alpha layers: an amber ring with a
// darker outline for contrast on bright floors, and a small centre dot.
inline std::vector<uint32_t> Pixels() {
    std::vector<uint32_t> p(Size*Size,0);
    const float c=(Size-1)*.5f;
    for(uint32_t y=0;y<Size;y++)for(uint32_t x=0;x<Size;x++) {
        float r=std::hypot(float(x)-c,float(y)-c)/c; // 0 centre, 1 edge
        float ring=1.f-std::fabs(r-.82f)/.09f;       // band around 82% of the radius
        float outline=1.f-std::fabs(r-.82f)/.14f;
        float dot=1.f-r/.09f;
        float amber=std::fmax(ring,dot), dark=std::fmax(outline,dot*1.2f);
        if(amber<0)amber=0;if(dark<0)dark=0;if(amber>1)amber=1;if(dark>1)dark=1;
        float alpha=std::fmax(amber,dark*.55f);
        if(alpha<=0)continue;
        // amber (255,176,32) over dark (20,12,0)
        auto mix=[&](float a,float b){return uint32_t(std::lround((b+(a-b)*amber)*alpha));};
        uint32_t R=mix(255,20),G=mix(176,12),B=mix(32,0),A=uint32_t(std::lround(alpha*255));
        p[y*Size+x]=R|(G<<8)|(B<<16)|(A<<24);
    }
    return p;
}
// Same pixels for a BGRA swapchain.
inline void SwapRedBlue(std::vector<uint32_t>& p) {
    for(auto& v:p)v=(v&0xff00ff00u)|((v&0xffu)<<16)|((v>>16)&0xffu);
}
// A quad's plane is its local X/Y with the normal along +Z. Lying flat with
// the normal up (+Y in OpenXR) is a -90 degree turn about X.
struct Orientation{float x,y,z,w;};
inline Orientation Flat() {const float h=0.70710678f;return {-h,0,0,h};}
// RenderInfo::markerState bits (transport 21).
inline constexpr uint32_t StateShow=1,StateNormal=2;
// Turn the quad so its face (+Z) points along n (unit, OpenXR): flat on a
// floor (n up), upright on a wall, tilted on a slope. Shortest rotation from
// +Z; for n straight down (a ceiling) a half turn about X.
inline Orientation Facing(float nx,float ny,float nz) {
    float l=std::sqrt(nx*nx+ny*ny+nz*nz);
    if(!(l>1e-6f))return Flat();
    nx/=l;ny/=l;nz/=l;
    if(nz<-0.9999f)return {1,0,0,0};
    // q = (z x n, 1 + z.n), normalised; z x n = (-ny, nx, 0).
    float x=-ny,y=nx,z=0,w=1+nz,m=std::sqrt(x*x+y*y+z*z+w*w);
    return {x/m,y/m,z/m,w/m};
}
}
