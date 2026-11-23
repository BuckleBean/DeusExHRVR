#pragma once
#include <cmath>
#include <cstdint>
// Carried boxes at the hand. [VR] CarriedItemAtHand=0 keeps them in front of the camera.
// The game positions a held object through 0x4F8D80 from two places in its grab code:
// while pulling it in (handle at manager+0x84) and once attached (body at manager+0xA0).
// States 2 to 5 use the rigid-object throw; state 7 is a body and is left alone.
namespace CarriedHand {
inline constexpr uintptr_t Address=0x4f8d80,PullCall=0x6611e1,HoldCall=0x66055e,PullReturn=0x6611e6,HoldReturn=0x660563;
inline constexpr unsigned char Bytes[]={0x56,0x57,0x8b,0x7c,0x24,0x0c,0x8b,0xf1};
inline constexpr float MaxJumpMetres=1.5f; // farther than this from the game's spot: keep the game's
inline bool BoxState(int state){return state>=2&&state<=5;}
inline bool Site(uintptr_t va){return va==PullReturn||va==HoldReturn;}
inline unsigned BodyOffset(uintptr_t va){return va==PullReturn?0x84u:0xa0u;}
inline bool Target(const float* palm,const float* direction,float forward,const float* native,float maxJump,float* out) {
    const float n=std::sqrt(direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2]);
    if(!std::isfinite(n)||n<1e-5f||!std::isfinite(forward)||forward<0)return false;
    float t[3],d2=0;
    for(int i=0;i<3;i++) {
        t[i]=palm[i]+direction[i]/n*forward;
        if(!std::isfinite(t[i])||!std::isfinite(native[i]))return false;
        d2+=(t[i]-native[i])*(t[i]-native[i]);
    }
    if(d2>maxJump*maxJump)return false;
    for(int i=0;i<3;i++)out[i]=t[i];
    return true;
}
}
