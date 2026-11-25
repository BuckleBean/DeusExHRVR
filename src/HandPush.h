#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Hand nudges ([VR] HandPush=1). Pure math; the native calls are in EngineCamera.cpp.
namespace HandPush {
// Measured from the game's own walk-into-object push (BodyPushLog, November 24): the
// force is horizontal, at most 1200 and at most 4 times the object's mass, at world
// scale 300 units per metre.
constexpr float NativeMaxForce=1200.f,NativeMaxPerMass=4.f,NativeScale=300.f;
// Radius of the search around the hand ([VR] HandPushCm, 3 to 20).
constexpr float DefaultRadiusMetres=.08f;
constexpr float MaxSpeedMetres=8.f;
// Lever for the push point (the hand) beyond which it falls back to the object's centre.
constexpr float MaxLeverMetres=1.5f;
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
        // Swipes count (November 24: a 2.5 m/s limit discarded a quarter of real hand
        // movement). Tracking jumps are still refused above (more than 15 cm between
        // samples); MaxSpeedMetres only rejects values no hand can reach.
        if(!std::isfinite(speed)||speed>MaxSpeedMetres||speed<.025f){why=speed<.025f?StepTooSlow:StepTooFast;return false;}
        why=StepOk;return true;
    }
};
// A swipe in progress: hands pass through objects, so once the hand is past an object's
// centre "toward the centre" turns into "away" (November 24 log: 2,485 refusals). A
// contact keeps pushing while the hand keeps moving within 60 degrees of the direction it
// entered with, and stops when it turns back, so withdrawing never pulls.
constexpr uint64_t SwipeKeepMs=150;
constexpr float SwipeKeepCos=.5f;
struct Swipe{uintptr_t body{};float dir[2]{};uint64_t tick{};};
struct SwipeMemory {
    Swipe s[4]{};
    void Reset(){*this={};}
    const Swipe* Find(uintptr_t body,uint64_t now) const {
        for(const auto& e:s)if(e.body&&e.body==body&&now>=e.tick&&now-e.tick<=SwipeKeepMs)return &e;
        return nullptr;
    }
    // Starts a swipe (records the entry direction) or keeps an existing one alive.
    void Note(uintptr_t body,const float* velocity,uint64_t now) {
        for(auto& e:s)if(e.body==body&&now>=e.tick&&now-e.tick<=SwipeKeepMs){e.tick=now;return;}
        const float speed=std::sqrt(velocity[0]*velocity[0]+velocity[1]*velocity[1]);
        if(!std::isfinite(speed)||speed<=0)return;
        Swipe* slot=&s[0];
        for(auto& e:s)if(!e.body||e.tick<slot->tick)slot=&e;
        slot->body=body;slot->dir[0]=velocity[0]/speed;slot->dir[1]=velocity[1]/speed;slot->tick=now;
    }
};
// Why HorizontalForce refused, for the optional log.
enum ForceWhy:int{ForceOk=0,ForceInput,ForceNotInward,ForceReceding,ForceMagnitude,ForceWhyCount};
// A push like the walk push: horizontal, along the hand's horizontal movement, only
// while the hand moves toward the object's centre faster than the object moves away.
// strength ([VR] HandPushStrength) scales both caps.
// swipe: the entry direction of a swipe in progress (see SwipeMemory), or null for a new contact.
inline int HorizontalForce(const float* palm,const float* centre,const float* handVelocity,const float* bodyVelocity,
                           float mass,float scale,float dt,float* force,float* inwardOut=nullptr,float* magnitudeOut=nullptr,float strength=1.f,
                           const float* swipe=nullptr) {
    if(!Finite(palm)||!Finite(centre)||!Finite(handVelocity)||!Finite(bodyVelocity)||
       !std::isfinite(mass)||mass<=0||mass>100000||!std::isfinite(scale)||scale<=0||
       !std::isfinite(dt)||dt<.001f||dt>.1f||!std::isfinite(strength)||strength<=0)return ForceInput;
    const float v[2]{handVelocity[0],handVelocity[1]};
    const float speed=std::sqrt(v[0]*v[0]+v[1]*v[1]);
    if(!std::isfinite(speed)||speed<=0)return ForceNotInward;
    float n[2]{};
    if(swipe) {
        n[0]=swipe[0];n[1]=swipe[1];
        const float along=v[0]*n[0]+v[1]*n[1];
        if(inwardOut)*inwardOut=along;
        if(along<SwipeKeepCos*speed||along<.025f*scale)return ForceNotInward;
    }
    else {
        n[0]=centre[0]-palm[0];n[1]=centre[1]-palm[1];
        float length=std::sqrt(n[0]*n[0]+n[1]*n[1]);
        // Hand right over the object's centre: any horizontal movement counts as toward it.
        if(!std::isfinite(length)||length<.01f*scale){n[0]=v[0];n[1]=v[1];length=speed;}
        n[0]/=length;n[1]/=length;
        const float inward=v[0]*n[0]+v[1]*n[1];
        if(inwardOut)*inwardOut=inward;
        if(inward<.025f*scale)return ForceNotInward;
    }
    const float inward=v[0]*n[0]+v[1]*n[1];
    const float closing=inward-(bodyVelocity[0]*n[0]+bodyVelocity[1]*n[1]);
    if(closing<=0)return ForceReceding;
    const float units=scale/NativeScale*strength;
    const float magnitude=std::min({mass*closing/(dt*30.f),NativeMaxPerMass*mass*units,NativeMaxForce*units});
    if(magnitudeOut)*magnitudeOut=magnitude;
    if(!std::isfinite(magnitude)||magnitude<=0)return ForceMagnitude;
    force[0]=v[0]/speed*magnitude;force[1]=v[1]/speed*magnitude;force[2]=0;force[3]=0;
    return ForceOk;
}
}
