#pragma once
#include <cmath>
#include <cstdint>
// Object contact and body drag handles at the hand. CarriedItemAtHand=0 disables both.
// The game positions a held object through 0x4F8D80 from two places in its grab code:
// while pulling it in (handle at manager+0x84) and once attached (body at manager+0xA0).
// States 2 to 5 are rigid objects; state 7 moves only the ragdoll drag handle.
namespace CarriedHand {
inline constexpr uintptr_t Address=0x4f8d80,PullCall=0x6611e1,HoldCall=0x66055e,PullReturn=0x6611e6,HoldReturn=0x660563;
inline constexpr unsigned char Bytes[]={0x56,0x57,0x8b,0x7c,0x24,0x0c,0x8b,0xf1};
inline constexpr float MaxJumpMetres=1.5f; // farther than this from the game's spot: keep the game's
inline bool BoxState(int state){return state>=2&&state<=5;}
inline bool Supported(int state,uintptr_t site){return BoxState(state)||(state==7&&site==PullReturn);}
inline bool Site(uintptr_t va){return va==PullReturn||va==HoldReturn;}
inline unsigned BodyOffset(uintptr_t va){return va==PullReturn?0x84u:0xa0u;}
// While pulling, the caller reads this same target again (0x661322) to set the handle's
// velocity, so the hand target must replace the caller's copy too.
inline bool ReplaceCallerTarget(uintptr_t va){return va==PullReturn;}
// How far the object's surface is from its center, from the game's accepted hit point at
// the grab. A missing or implausible measurement uses DefaultExtentMetres.
inline constexpr float DefaultExtentMetres=.12f,MinExtentMetres=.03f,MaxExtentMetres=.6f;
inline constexpr uint64_t MaxPointAgeMs=1000;
inline bool Fresh(uint64_t now,uint64_t tick){return tick&&now>=tick&&now-tick<=MaxPointAgeMs;}
// A contact vector is relative to the rigid body's origin, not an estimated radius.
// This retains the actual picked surface on long/uneven objects as they rotate.
inline bool Contact(const float* hit,const float* center,float scale,float* out) {
    float v[3],n=0;
    for(int i=0;i<3;i++){v[i]=hit[i]-center[i];if(!std::isfinite(v[i]))return false;n+=v[i]*v[i];}
    if(!std::isfinite(scale)||scale<=0||!std::isfinite(n)||n<.0001f*scale*scale||n>4.f*scale*scale)return false;
    for(int i=0;i<3;i++)out[i]=v[i];
    return true;
}
// Anchor is the native constraint pivot relative to the object's origin (zero
// for direct placement). Moving this target makes the picked surface meet the hand.
inline bool ContactTarget(const float* palm,const float* contact,const float* anchor,float gap,
                          const float* native,float maxJump,float* out) {
    const float n=std::sqrt(contact[0]*contact[0]+contact[1]*contact[1]+contact[2]*contact[2]);
    if(!std::isfinite(n)||n<1e-5f||!std::isfinite(gap)||gap<0)return false;
    float t[3],d2=0;
    for(int i=0;i<3;i++){
        t[i]=palm[i]-contact[i]*(1+gap/n)+anchor[i];
        if(!std::isfinite(t[i])||!std::isfinite(native[i]))return false;
        d2+=(t[i]-native[i])*(t[i]-native[i]);
    }
    if(!std::isfinite(maxJump)||maxJump<=0||d2>maxJump*maxJump)return false;
    for(int i=0;i<3;i++)out[i]=t[i];
    return true;
}
inline float Extent(const float* hit,const float* center,float scale,bool* measured=nullptr) {
    if(measured)*measured=false;
    const float fallback=DefaultExtentMetres*scale;
    float d2=0;
    for(int i=0;i<3;i++) {
        if(!std::isfinite(hit[i])||!std::isfinite(center[i]))return fallback;
        d2+=(hit[i]-center[i])*(hit[i]-center[i]);
    }
    const float d=std::sqrt(d2);
    if(!std::isfinite(d)||d>MaxExtentMetres*scale)return fallback;
    if(measured)*measured=true;
    return d<MinExtentMetres*scale?MinExtentMetres*scale:d;
}
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
