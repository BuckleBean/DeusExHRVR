#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "CameraMath.h"
// Two-handed aim. When the left hand takes the weapon, the line from the right hand to
// the left is remembered relative to the right hand. If the left hand then moves off
// that line, the weapon swings around the right hand by the same amount. So grabbing
// causes no jump, and the right wrist still controls the weapon's twist.
namespace TwoHandSteer {
using CameraMath::Matrix;
inline constexpr float MinLineMetres=.08f;  // hands closer than this give no usable direction
inline constexpr float MaxAngle=1.0471976f; // 60 degrees
inline constexpr float InSeconds=.11f,OutSeconds=.18f;
// Where the wrist HUD should sit: the difference between the visible hand and the real
// controller, in the controller's own axes, so it can be applied to any grip pose.
struct HudShift {Transport::Quaternion turn{0,0,0,1};Transport::Vector move{};};
// m's rows are the images of the three axes, as CameraMath::Rotation builds them.
inline bool QuaternionFromRows(const Matrix& m,Transport::Quaternion& q) {
    const float a=m.m[0],b=m.m[4],c=m.m[8],d=m.m[1],e=m.m[5],f=m.m[9],g=m.m[2],h=m.m[6],i=m.m[10];
    const float det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if(!std::isfinite(det)||det<.9f||det>1.1f)return false;
    const float trace=a+e+i;
    if(trace>0){const float s=std::sqrt(trace+1)*2;q={(h-f)/s,(c-g)/s,(d-b)/s,.25f*s};}
    else if(a>=e&&a>=i){const float s=std::sqrt((std::max)(1+a-e-i,1e-6f))*2;q={.25f*s,(b+d)/s,(c+g)/s,(h-f)/s};}
    else if(e>=i){const float s=std::sqrt((std::max)(1+e-a-i,1e-6f))*2;q={(b+d)/s,.25f*s,(f+h)/s,(c-g)/s};}
    else {const float s=std::sqrt((std::max)(1+i-a-e,1e-6f))*2;q={(c+g)/s,(f+h)/s,.25f*s,(d-b)/s};}
    const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if(!std::isfinite(n)||n<.5f)return false;
    q={q.x/n,q.y/n,q.z/n,q.w/n};return true;
}
// visible and real are world frames in the grip's axes. amount 0 is no shift, 1 is all of it.
inline bool MakeShift(const Matrix& visible,const Matrix& real,float scale,float amount,HudShift& out) {
    if(!std::isfinite(scale)||scale<=0||!std::isfinite(amount))return false;
    const auto local=CameraMath::Multiply(visible,CameraMath::InverseRigid(real));
    Transport::Quaternion q;if(!QuaternionFromRows(local,q))return false;
    if(q.w<0)q={-q.x,-q.y,-q.z,-q.w};
    const float t=std::clamp(amount,0.f,1.f);
    q={q.x*t,q.y*t,q.z*t,1-t+q.w*t};
    const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if(!std::isfinite(n)||n<1e-4f)return false;
    out.turn={q.x/n,q.y/n,q.z/n,q.w/n};
    out.move={local.m[12]*t/scale,local.m[13]*t/scale,local.m[14]*t/scale};
    return std::isfinite(out.move.x)&&std::isfinite(out.move.y)&&std::isfinite(out.move.z);
}
inline Transport::Pose ApplyShift(const Transport::Pose& grip,const HudShift& shift) {
    Transport::Pose out=grip;
    const auto moved=CameraMath::Rotate(grip.orientation,shift.move);
    out.position={grip.position.x+moved.x,grip.position.y+moved.y,grip.position.z+moved.z};
    out.orientation=CameraMath::Multiply(grip.orientation,shift.turn);
    return out;
}
// The hands step aside while scoped, so the draw code stops confirming the hold. A hold that
// was steering when the scope opened carries through it while the grip stays held, and for a
// moment after, so aim does not jump to the right hand on the way in or out.
struct ScopeHold {
    bool held{};uint64_t graceUntil{};
    bool Update(bool scoped,bool steering,bool gripHeld,uint64_t now) {
        if(!gripHeld){held=false;graceUntil=0;return false;}
        if(scoped){if(steering)held=true;graceUntil=0;return held;}
        if(held){held=false;graceUntil=now+250;}
        return graceUntil&&now<graceUntil;
    }
};
struct Steer {
    bool have{},timed{};float local[3]{},axis[3]{0,0,1},angle{},amount{};double last{};
    void Reset(){have=false;timed=false;angle=0;amount=0;last=0;}
    // Returns true when the weapon should be turned; then call Apply.
    bool Update(double nowMs,bool show,const Matrix& right,const Matrix& left,float scale) {
        if(!timed||nowMs<last||nowMs-last>500){timed=true;last=nowMs;amount=0;angle=0;have=false;}
        else {
            const float dt=float(nowMs-last)/1000.f;last=nowMs;
            amount=std::clamp(amount+(show?dt/InSeconds:-dt/OutSeconds),0.f,1.f);
        }
        if(!show){have=false;return amount>0&&angle>0;}
        float line[3]{left.m[12]-right.m[12],left.m[13]-right.m[13],left.m[14]-right.m[14]};
        const float length=std::sqrt(line[0]*line[0]+line[1]*line[1]+line[2]*line[2]);
        if(!std::isfinite(length)||!(scale>0)||length<MinLineMetres*scale)return amount>0&&angle>0; // keep the last turn
        for(float& v:line)v/=length;
        float row[3][3];
        for(int r=0;r<3;r++) {
            for(int c=0;c<3;c++)row[r][c]=right.m[r*4+c];
            const float n=std::sqrt(row[r][0]*row[r][0]+row[r][1]*row[r][1]+row[r][2]*row[r][2]);
            if(!std::isfinite(n)||n<.5f)return amount>0&&angle>0;
            for(float& v:row[r])v/=n;
        }
        if(!have) {
            for(int r=0;r<3;r++)local[r]=row[r][0]*line[0]+row[r][1]*line[1]+row[r][2]*line[2];
            have=true;angle=0;return false;
        }
        float expected[3]{};
        for(int c=0;c<3;c++)expected[c]=local[0]*row[0][c]+local[1]*row[1][c]+local[2]*row[2][c];
        const float cross[3]{expected[1]*line[2]-expected[2]*line[1],expected[2]*line[0]-expected[0]*line[2],expected[0]*line[1]-expected[1]*line[0]};
        const float sine=std::sqrt(cross[0]*cross[0]+cross[1]*cross[1]+cross[2]*cross[2]);
        const float cosine=expected[0]*line[0]+expected[1]*line[1]+expected[2]*line[2];
        if(!std::isfinite(sine)||!std::isfinite(cosine))return amount>0&&angle>0;
        if(sine>1e-5f) {
            for(int c=0;c<3;c++)axis[c]=cross[c]/sine;
            angle=(std::min)(std::atan2(sine,cosine),MaxAngle);
        } else if(cosine>0)angle=0; // exactly opposite: keep the last turn
        return amount>0&&angle>0;
    }
    void Turn(float* v,float s,float c) const {
        const float d=axis[0]*v[0]+axis[1]*v[1]+axis[2]*v[2];
        const float x[3]{axis[1]*v[2]-axis[2]*v[1],axis[2]*v[0]-axis[0]*v[2],axis[0]*v[1]-axis[1]*v[0]};
        for(int j=0;j<3;j++)v[j]=v[j]*c+x[j]*s+axis[j]*d*(1-c);
    }
    // Turns a weapon or hand transform around the right hand's position.
    void Apply(Matrix& m,const Matrix& pivot) const {
        const float a=angle*amount,s=std::sin(a),c=std::cos(a);
        for(int r=0;r<3;r++) {
            float v[3]{m.m[r*4],m.m[r*4+1],m.m[r*4+2]};Turn(v,s,c);
            for(int j=0;j<3;j++)m.m[r*4+j]=v[j];
        }
        float p[3]{m.m[12]-pivot.m[12],m.m[13]-pivot.m[13],m.m[14]-pivot.m[14]};Turn(p,s,c);
        for(int j=0;j<3;j++)m.m[12+j]=pivot.m[12+j]+p[j];
    }
};
}
