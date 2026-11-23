#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include <wchar.h>
// [VR] The interaction line from the visible hand, and a pointer showing where
// each hand's line lands (Mark, November 22). InteractionFromHand=0 restores the
// controller's aim pose; HandPointers=Off hides the pointers.
namespace HandPointer {
// Transport values; 0 means unavailable or "keep".
inline constexpr uint32_t Off=1,Right=2,Left=3,Both=4;
inline constexpr bool Shows(uint32_t m,bool left){return m==Both||m==(left?Left:Right);}
inline constexpr uint32_t Next(uint32_t m){return m==Off?Right:m==Right?Left:m==Left?Both:Off;}
inline uint32_t Parse(const wchar_t* v) {
    if(!_wcsicmp(v,L"Off")||!_wcsicmp(v,L"0"))return Off;
    if(!_wcsicmp(v,L"Right"))return Right;
    if(!_wcsicmp(v,L"Left"))return Left;
    if(!_wcsicmp(v,L"Both"))return Both;
    return Off; // absent or unknown
}
inline const wchar_t* Value(uint32_t m){return m==Off?L"Off":m==Right?L"Right":m==Left?L"Left":L"Both";}
inline const wchar_t* Label(uint32_t m){return m==Off?L"OFF":m==Right?L"RIGHT HAND":m==Left?L"LEFT HAND":L"BOTH HANDS";}
inline constexpr float RangeMetres=10.f;
// One texture, two cells: right hand cyan, left hand amber.
inline constexpr uint32_t Cell=64,AtlasWidth=Cell*2;
// Small up close, and never wider than 5 cm however far away the line lands.
inline constexpr float MaxWidthMetres=.05f;
inline float Width(float distance){const float w=distance*.015f;return w<MaxWidthMetres?w:MaxWidthMetres;}
inline std::vector<uint32_t> Pixels() {
    std::vector<uint32_t> pixels(AtlasWidth*Cell);
    const uint32_t colours[2]={0xffffdc28u,0xff00aaffu};
    for(uint32_t y=0;y<Cell;y++)for(uint32_t x=0;x<AtlasWidth;x++) {
        const uint32_t hand=x/Cell;
        const float a=float(x%Cell)-31.5f,b=float(y)-31.5f,r=std::sqrt(a*a+b*b);
        const bool colour=(r>=20&&r<=26)||r<=4;
        const bool outline=(r>=18&&r<=28)||r<=6;
        pixels[y*AtlasWidth+x]=colour?colours[hand]:outline?0xff000000u:0;
    }
    return pixels;
}
// `h` is CameraMath::HeadWorld of the grip pose: row 0 = grip +X, row 1 = grip -Y,
// row 2 = grip -Z. Measured from the hand skeleton, the visible fingers point along
// grip (0,-.999,-.047) on both hands; the palm faces grip -X (right), +X (left).
inline bool Line(const float* h,bool left,float tiltDegrees,float* origin,float* dir) {
    if(!std::isfinite(tiltDegrees))return false;
    const float t=tiltDegrees*.0174532925f,c=std::cos(t),s=std::sin(t),palm=left?1.f:-1.f;
    float n=0;
    for(int i=0;i<3;i++) {
        origin[i]=h[12+i];
        dir[i]=c*(.999f*h[4+i]+.047f*h[8+i])+s*palm*h[i];
        n+=dir[i]*dir[i];
        if(!std::isfinite(origin[i]))return false;
    }
    if(!std::isfinite(n)||n<.25f)return false;
    n=std::sqrt(n);
    for(int i=0;i<3;i++)dir[i]/=n;
    return true;
}
}
