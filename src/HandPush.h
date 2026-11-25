#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace HandPush {
constexpr float RadiusMetres=.035f;
inline bool Finite(const float* p){return std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2]);}
inline float Dot(const float* a,const float* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
struct Motion {
    float last[3]{},scale{};uint64_t tick{};
    void Reset(){*this={};}
    bool Step(const float* point,float units,uint64_t time,float* velocity) {
        if(!Finite(point)||!std::isfinite(units)||units<=0){Reset();return false;}
        if(time==tick)return false;
        const bool previous=tick&&time>tick&&time-tick<=80&&scale==units;
        const float seconds=previous?float(time-tick)*.001f:0;
        float d[3]{};for(int i=0;i<3;i++){d[i]=point[i]-last[i];last[i]=point[i];}
        tick=time;scale=units;
        if(!previous||seconds<.004f||Dot(d,d)>.15f*.15f*units*units)return false;
        for(int i=0;i<3;i++)velocity[i]=d[i]/seconds;
        const float speed=std::sqrt(Dot(velocity,velocity))/units;
        // Tracking jumps and fast swings prime history but never deliver a shove.
        if(!std::isfinite(speed)||speed>2.5f||speed<.025f)return false;
        const float cap=std::min(1.f,.75f/speed);
        for(int i=0;i<3;i++)velocity[i]*=cap;
        return true;
    }
};
// Normal points out of the object toward the hand. Only inward motion pushes.
// Preserve the native body's force response (dt*30), with conservative caps.
inline bool Force(const float* palm,const float* point,const float* outward,
                  const float* handVelocity,const float* bodyVelocity,float mass,
                  float scale,float dt,float* force) {
    if(!Finite(palm)||!Finite(point)||!Finite(outward)||!Finite(handVelocity)||!Finite(bodyVelocity)||
       !std::isfinite(mass)||mass<=0||mass>100000||!std::isfinite(scale)||scale<=0||
       !std::isfinite(dt)||dt<.001f||dt>.1f)return false;
    float delta[3]{};for(int i=0;i<3;i++)delta[i]=palm[i]-point[i];
    if(Dot(delta,delta)>RadiusMetres*RadiusMetres*scale*scale*1.1f)return false;
    const float norm=std::sqrt(Dot(outward,outward));if(norm<.9f||norm>1.1f)return false;
    float n[3]{};for(int i=0;i<3;i++)n[i]=outward[i]/norm;
    // Reject ambiguous inverted/inside contacts instead of flipping their normals.
    if(Dot(delta,n)<-.002f*scale)return false;
    const float inward=-Dot(handVelocity,n);
    if(inward<.025f*scale)return false;
    const float closing=inward+Dot(bodyVelocity,n);
    if(closing<=0)return false;
    const float magnitude=std::min({mass*closing/(dt*30.f),mass*scale*.5f,20.f*scale});
    if(!std::isfinite(magnitude)||magnitude<=0)return false;
    for(int i=0;i<3;i++)force[i]=-n[i]*magnitude;
    force[3]=0;return true;
}
}
