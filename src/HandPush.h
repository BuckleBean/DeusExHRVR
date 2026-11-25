#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace HandPush {
constexpr float RadiusMetres=.035f;
inline bool Finite(const float* p){return std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2]);}
inline float Dot(const float* a,const float* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
// Why Step gave no velocity, for the optional log (HandPushLog=1).
enum StepWhy:int{StepOk=0,StepInvalid,StepSameSample,StepNoHistory,StepTooSoon,StepJump,StepTooFast,StepTooSlow,StepWhyCount};
struct Motion {
    float last[3]{},scale{};uint64_t tick{};int why{};
    void Reset(){*this={};}
    bool Step(const float* point,float units,uint64_t time,float* velocity) {
        if(!Finite(point)||!std::isfinite(units)||units<=0){Reset();why=StepInvalid;return false;}
        if(time==tick){why=StepSameSample;return false;}
        const bool previous=tick&&time>tick&&time-tick<=80&&scale==units;
        const float seconds=previous?float(time-tick)*.001f:0;
        float d[3]{};for(int i=0;i<3;i++){d[i]=point[i]-last[i];last[i]=point[i];}
        tick=time;scale=units;
        if(!previous||seconds<.004f||Dot(d,d)>.15f*.15f*units*units) {
            why=!previous?StepNoHistory:seconds<.004f?StepTooSoon:StepJump;return false;
        }
        for(int i=0;i<3;i++)velocity[i]=d[i]/seconds;
        const float speed=std::sqrt(Dot(velocity,velocity))/units;
        // Tracking jumps and fast swings prime history but never deliver a shove.
        if(!std::isfinite(speed)||speed>2.5f||speed<.025f){why=speed<.025f?StepTooSlow:StepTooFast;return false;}
        const float cap=std::min(1.f,.75f/speed);
        for(int i=0;i<3;i++)velocity[i]*=cap;
        why=StepOk;return true;
    }
};
// Why Force refused, for the optional log. The decisions are unchanged.
enum ForceWhy:int{ForceOk=0,ForceInput,ForceFar,ForceNormal,ForceBehind,ForceNotInward,ForceReceding,ForceMagnitude,ForceWhyCount};
inline int ForceReason(const float* palm,const float* point,const float* outward,
                  const float* handVelocity,const float* bodyVelocity,float mass,
                  float scale,float dt,float* force,float* inwardOut=nullptr,float* magnitudeOut=nullptr) {
    if(!Finite(palm)||!Finite(point)||!Finite(outward)||!Finite(handVelocity)||!Finite(bodyVelocity)||
       !std::isfinite(mass)||mass<=0||mass>100000||!std::isfinite(scale)||scale<=0||
       !std::isfinite(dt)||dt<.001f||dt>.1f)return ForceInput;
    float delta[3]{};for(int i=0;i<3;i++)delta[i]=palm[i]-point[i];
    if(Dot(delta,delta)>RadiusMetres*RadiusMetres*scale*scale*1.1f)return ForceFar;
    const float norm=std::sqrt(Dot(outward,outward));if(norm<.9f||norm>1.1f)return ForceNormal;
    float n[3]{};for(int i=0;i<3;i++)n[i]=outward[i]/norm;
    // Reject ambiguous inverted/inside contacts instead of flipping their normals.
    if(Dot(delta,n)<-.002f*scale)return ForceBehind;
    const float inward=-Dot(handVelocity,n);
    if(inwardOut)*inwardOut=inward;
    if(inward<.025f*scale)return ForceNotInward;
    const float closing=inward+Dot(bodyVelocity,n);
    if(closing<=0)return ForceReceding;
    const float magnitude=std::min({mass*closing/(dt*30.f),mass*scale*.5f,20.f*scale});
    if(magnitudeOut)*magnitudeOut=magnitude;
    if(!std::isfinite(magnitude)||magnitude<=0)return ForceMagnitude;
    for(int i=0;i<3;i++)force[i]=-n[i]*magnitude;
    force[3]=0;return ForceOk;
}
// Normal points out of the object toward the hand. Only inward motion pushes.
// Preserve the native body's force response (dt*30), with conservative caps.
inline bool Force(const float* palm,const float* point,const float* outward,
                  const float* handVelocity,const float* bodyVelocity,float mass,
                  float scale,float dt,float* force) {
    return ForceReason(palm,point,outward,handVelocity,bodyVelocity,mass,scale,dt,force)==ForceOk;
}
}
