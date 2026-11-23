#pragma once
#include <cmath>
#include <cstdint>
// Grab object's throw-direction builder. Both rigid objects and ragdolls use it,
// then multiply its result by the game's own strength. Grenades use another path.
namespace CarriedThrowAim {
inline constexpr uintptr_t Address=0x65f7c0;
inline constexpr unsigned char Bytes[]={0x55,0x8b,0xec,0x83,0xe4,0xf0,0x83,0xec,0x2c};
inline bool Caller(uintptr_t va){return va==0x661e18||va==0x662114;}
inline bool Direction(float* result,const float* forward) {
    const float n=std::sqrt(forward[0]*forward[0]+forward[1]*forward[1]+forward[2]*forward[2]);
    if(!std::isfinite(n)||n<1e-5f)return false;
    for(int i=0;i<3;i++)result[i]=forward[i]/n;
    return true; // preserve the native fourth component
}
}
