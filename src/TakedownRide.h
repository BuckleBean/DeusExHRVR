#pragma once
#include <algorithm>
#include <cmath>
#include "CameraMath.h"
// Takedown ride-along. The view follows Jensen's eyes and the way his chest faces, both
// smoothed, while the player's real head still looks around on top of it.
// Skeleton numbering is the same on all three player models. Bones 41 and 43 are the mirrored
// pair at eye height (inferred from position; the skeleton has no names). Bone 15 is the top
// of the spine. The model faces -Y, so a bone's forward is its second row, negated.
namespace TakedownRide {
using CameraMath::Matrix;
inline constexpr unsigned EyeA=41,EyeB=43,Chest=15;
inline constexpr float EyeSeconds=.045f,YawSeconds=.14f,BlendSeconds=.18f;
inline constexpr float MaxYawRate=2.6f; // radians per second, about 150 degrees
inline float Wrap(float a) {
    const float tau=6.2831853f;
    a=std::fmod(a,tau);if(a>3.1415927f)a-=tau;if(a<-3.1415927f)a+=tau;return a;
}
inline bool FromBones(const Matrix& a,const Matrix& b,const Matrix& chest,float* eye,float& yaw) {
    for(int j=0;j<3;j++)eye[j]=(a.m[12+j]+b.m[12+j])*.5f;
    const float x=-chest.m[4],y=-chest.m[5],flat=std::sqrt(x*x+y*y);
    if(!std::isfinite(eye[0])||!std::isfinite(eye[1])||!std::isfinite(eye[2])||!std::isfinite(flat)||flat<.2f||flat>1.1f)return false;
    yaw=std::atan2(y,x);return true;
}
struct Ride {
    bool have{};float startEye[3]{},eyeNow[3]{},startYaw{},yaw{},blend{},maxTravel{},maxYaw{};double last{};
    void Reset(){*this={};}
    void Begin(const float* eye,float chestYaw,double nowMs) {
        Reset();have=true;last=nowMs;startYaw=chestYaw;
        for(int j=0;j<3;j++)startEye[j]=eyeNow[j]=eye[j];
    }
    void Update(const float* eye,float chestYaw,double nowMs) {
        if(!have||nowMs<=last)return;
        const float dt=float((std::min)(nowMs-last,100.0))/1000.f;last=nowMs;
        const float follow=1-std::exp(-dt/EyeSeconds);
        float travel=0;
        for(int j=0;j<3;j++){eyeNow[j]+=(eye[j]-eyeNow[j])*follow;const float e=eyeNow[j]-startEye[j];travel+=e*e;}
        maxTravel=(std::max)(maxTravel,std::sqrt(travel));
        const float toward=Wrap(Wrap(chestYaw-startYaw)-yaw);
        const float limit=MaxYawRate*dt;
        yaw=Wrap(yaw+std::clamp(toward*(1-std::exp(-dt/YawSeconds)),-limit,limit));
        maxYaw=(std::max)(maxYaw,std::abs(yaw));
        blend=(std::min)(1.f,blend+dt/BlendSeconds);
    }
    // view: the first-person base held at the start of the takedown.
    void Apply(Matrix& view) const {
        if(!have)return;
        const float s=std::sin(yaw),c=std::cos(yaw);
        for(int r=0;r<3;r++){const float x=view.m[r*4],y=view.m[r*4+1];view.m[r*4]=c*x-s*y;view.m[r*4+1]=s*x+c*y;}
        const float t=blend*blend*(3-2*blend);
        for(int j=0;j<3;j++)view.m[12+j]+=(eyeNow[j]-view.m[12+j])*t;
    }
};
}
