#pragma once
#include <algorithm>
#include <cmath>
// Keeps the VR view from rising into a low ceiling, such as a vent. Heights are in
// game units above the player's origin. Acts only while crouched. The view eases
// into and out of each change, so it never starts or stops moving abruptly.
namespace CeilingGuard {
inline constexpr float NoCeiling=1e30f;
inline constexpr float DownTime=.16f,UpTime=.24f;       // seconds; about 90% done in twice this
inline constexpr float DownSpeed=450.f,UpSpeed=240.f;   // top speed, units per second
// The game's camera sits about 566 units above the body standing and 253-268 crouched.
inline constexpr float CrouchLimit=400.f;
inline bool Crouched(float cameraHeight){return std::isfinite(cameraHeight)&&cameraHeight<CrouchLimit;}
struct State {
    float drop{},speed{};double last{};bool have{};
    void Reset(){drop=0;speed=0;last=0;have=false;}
    // eye: view height before the guard. room: ceiling height, or NoCeiling.
    float Update(float eye,float room,float margin,float maxDrop,double nowMs) {
        float want=0;
        if(std::isfinite(eye)&&std::isfinite(room)&&room<NoCeiling) {
            want=eye-(room-margin);
            if(want<0)want=0;
            if(want>maxDrop)want=maxDrop;
        }
        if(!have||nowMs<last||nowMs-last>500){have=true;last=nowMs;drop=want;speed=0;return drop;}
        const float dt=float(nowMs-last)/1000.f;last=nowMs;
        if(dt<=0)return drop;
        const bool down=want>drop;
        const float omega=2.f/(down?DownTime:UpTime),top=down?DownSpeed:UpSpeed;
        const float gap=drop-want,decay=std::exp(-omega*dt),term=(speed+omega*gap)*dt;
        const float next=want+(gap+term)*decay;
        const float step=std::clamp(next-drop,-top*dt,top*dt);
        speed=std::clamp((speed-omega*term)*decay,-top,top);
        drop+=step;
        if((gap>0&&drop<want)||(gap<0&&drop>want)||(std::abs(drop-want)<.05f&&std::abs(speed)<.5f)){drop=want;speed=0;}
        return drop;
    }
    // Wheel, menu, scope or lost tracking: stay exactly where the view is.
    float Hold(double nowMs){if(!have)return 0;last=nowMs;speed=0;return drop;}
    // Standing, cover, conversation or a special camera: ease away instead of cutting off.
    float Release(double nowMs){return Update(0,NoCeiling,0,0,nowMs);}
};
// Where the player will be in a moment, so a drop can begin before a low ceiling.
inline constexpr float AheadSeconds=.3f,AheadMax=150.f,AheadMinSpeed=40.f,AheadMaxSpeed=1500.f;
inline constexpr double AheadSampleMs=66;
struct Ahead {
    float lastX{},lastY{},vx{},vy{};double lastT{};bool have{};
    void Reset(){have=false;vx=vy=0;}
    bool Update(float x,float y,double nowMs,float& aheadX,float& aheadY) {
        if(!std::isfinite(x)||!std::isfinite(y)){Reset();return false;}
        if(!have||nowMs<lastT||nowMs-lastT>500){have=true;lastX=x;lastY=y;lastT=nowMs;vx=vy=0;return false;}
        if(nowMs-lastT>=AheadSampleMs) {
            const float dt=float(nowMs-lastT)/1000.f;
            vx=(x-lastX)/dt;vy=(y-lastY)/dt;lastX=x;lastY=y;lastT=nowMs;
        }
        const float moving=std::hypot(vx,vy);
        if(moving<AheadMinSpeed||moving>AheadMaxSpeed)return false; // still, or a teleport
        const float distance=std::min(moving*AheadSeconds,AheadMax);
        aheadX=x+vx/moving*distance;aheadY=y+vy/moving*distance;
        return true;
    }
};
}
