#pragma once
#include <cmath>
// Keeps the VR view from rising into a low ceiling, such as a vent. Heights are in
// game units above the player's origin. The view eases down when a ceiling gets
// close and eases back when there is room again.
namespace CeilingGuard {
inline constexpr float NoCeiling=1e30f;
inline constexpr float DownRate=900.f,UpRate=300.f; // units per second
struct State {
    float drop{};double last{};bool have{};
    void Reset(){drop=0;last=0;have=false;}
    // eye: view height before the guard. room: ceiling height, or NoCeiling.
    float Update(float eye,float room,float margin,float maxDrop,double nowMs) {
        float want=0;
        if(std::isfinite(eye)&&std::isfinite(room)&&room<NoCeiling) {
            want=eye-(room-margin);
            if(want<0)want=0;
            if(want>maxDrop)want=maxDrop;
        }
        if(!have||nowMs<last||nowMs-last>500){have=true;last=nowMs;drop=want;return drop;}
        const float dt=float(nowMs-last)/1000.f;last=nowMs;
        if(want>drop){drop+=DownRate*dt;if(drop>want)drop=want;}
        else {drop-=UpRate*dt;if(drop<want)drop=want;}
        return drop;
    }
};
}
