#pragma once
#include <cmath>
#include <cstdint>
namespace DirectSnap {
constexpr float Pi=3.14159265358979323846f;
// Caller serializes access. Owner values are identity tokens, never dereferenced.
struct Request {
    float degrees{};uint64_t tick{};uintptr_t player{},manager{};bool pending{};
    void Clear(){pending=false;}
    bool Queue(float d,uint64_t now,uintptr_t p,uintptr_t m){
        if(pending || !p || !m || !std::isfinite(d) || std::abs(d)<5 || std::abs(d)>180)return false;
        degrees=d;tick=now;player=p;manager=m;pending=true;return true;
    }
    float Take(uint64_t now,uintptr_t p,uintptr_t m,bool eligible){
        if(!pending)return 0;
        pending=false;
        if(!eligible || now<tick || now-tick>100 || p!=player || m!=manager)return 0;
        return degrees;
    }
};
inline float Turn(float yaw,float rightDegrees){
    // Native yaw increases to the left. Preserve a bounded absolute angle.
    return std::remainder(yaw-rightDegrees*Pi/180.f,2*Pi);
}
}
