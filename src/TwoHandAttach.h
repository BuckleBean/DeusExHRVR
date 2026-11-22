#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
// Two-handed weapons. Holding the left grip anywhere near the gun makes the left hand
// take the pose the game's own animation gives it.
namespace TwoHandAttach {
inline constexpr float GrabMetres=.20f;   // how close to the gun the left controller must be
inline constexpr float MuzzleLimit=450.f; // a muzzle further than this from the hand is not used
inline constexpr float BreakMetres=.30f;  // pulling the hands this much further apart lets go
inline constexpr float MinSpread=15.f,MaxSpread=260.f; // game units between the game's two palms
inline constexpr float InSeconds=.11f,OutSeconds=.14f;
// A weapon qualifies when the game's left hand rests close to its right hand. A guess at
// the limit until each weapon's logged value has been seen.
inline bool Supported(float spread){return std::isfinite(spread)&&spread>=MinSpread&&spread<=MaxSpread;}
// The weapon is out and in the right hand. States 4 and 6 are holstered (6: it started out
// holstered); pending is a weapon change in progress.
inline bool WeaponReady(bool known,uint32_t state,uint32_t pending,bool throwing,bool carrying) {
    return known&&state!=4&&state!=6&&pending==0&&!throwing&&!carrying;
}
// Once on the weapon, the game's reload and firing animations may move its left hand
// outside the resting range without that meaning the weapon stopped supporting it.
inline bool Eligible(bool valid,bool sprinting,bool ready,bool attached,float spread) {
    return valid&&!sprinting&&ready&&(attached||Supported(spread));
}
inline float SegmentDistance(const float* p,const float* a,const float* b) {
    float ab[3],ap[3],len=0,dot=0;
    for(int i=0;i<3;i++){ab[i]=b[i]-a[i];ap[i]=p[i]-a[i];len+=ab[i]*ab[i];dot+=ab[i]*ap[i];}
    const float t=len>1e-6f?std::clamp(dot/len,0.f,1.f):0.f;
    float sum=0;for(int i=0;i<3;i++){const float e=ap[i]-ab[i]*t;sum+=e*e;}
    return std::sqrt(sum);
}
// How far the left hand is from the gun: the line from the right hand to the muzzle, and
// the line from the right hand to where the game puts the left hand.
inline float GrabDistance(const float* left,const float* right,const float* muzzle,const float* spot) {
    float best=SegmentDistance(left,right,spot),span=0;
    for(int i=0;i<3;i++){const float e=muzzle[i]-right[i];span+=e*e;}
    span=std::sqrt(span);
    if(std::isfinite(span)&&span<=MuzzleLimit){const float d=SegmentDistance(left,right,muzzle);if(d<best)best=d;}
    return best;
}
struct State {
    bool attached{},broken{},have{};float amount{},base{};double last{};
    // held: the grip press that began near the weapon. eligible: the pose is usable now.
    // gap: distance between the two controllers. Returns 0 (own pose) to 1 (on the weapon).
    // The weapon changed: let go, and stay off until the grip is pressed again.
    void Drop(bool held){attached=false;broken=held;}
    float Update(double nowMs,bool held,bool eligible,float gap,float scale) {
        if(!held){attached=false;broken=false;}
        else if(attached&&std::isfinite(gap)&&gap>base+BreakMetres*scale){attached=false;broken=true;}
        else if(!attached&&!broken&&eligible&&std::isfinite(gap)){attached=true;base=gap;}
        const bool show=attached&&eligible;
        if(!have||nowMs<last||nowMs-last>500){have=true;last=nowMs;amount=show?1.f:0.f;return amount;}
        const float dt=float(nowMs-last)/1000.f;last=nowMs;
        amount=std::clamp(amount+(show?dt/InSeconds:-dt/OutSeconds),0.f,1.f);
        return amount;
    }
};
}
