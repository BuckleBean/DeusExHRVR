#pragma once
#include <cmath>
// Cover camera follows controller aim (Mark, November 16: tie the camera's
// swing around Jensen to aiming with motion controls, the way mouse movement
// swings it). Yaw values are the native look controller's absolute yaw
// (0x7AC8C0 result [2]); NativeHeading::Base: yaw 0 faces +Y, forward =
// (-sin yaw, cos yaw).
namespace CoverFollow {
inline constexpr float Pi=3.14159265358979f;
inline float Wrap(float a){return std::remainder(a,2*Pi);}
// Yaw whose forward matches a horizontal world direction; false when the
// direction is (nearly) vertical or not finite.
inline bool YawFromForward(const float* f,float& yaw) {
    if(!std::isfinite(f[0])||!std::isfinite(f[1]))return false;
    const float h=std::hypot(f[0],f[1]);
    if(!(h>.2f))return false; // pointing almost straight up/down: hold the camera
    yaw=std::atan2(-f[0],f[1]);return true;
}
// Leash: while the aim stays within `leash` radians of the camera's yaw the
// camera holds still; beyond it, the camera is pulled along so the aim sits
// on the leash edge. The move is limited to maxStep radians per update.
// Pitch (positive = up) of a world direction in the game's Z-up frame.
inline bool PitchFromForward(const float* f,float& pitch) {
    if(!std::isfinite(f[0])||!std::isfinite(f[1])||!std::isfinite(f[2]))return false;
    const float h=std::hypot(f[0],f[1]);
    if(!(h>1e-4f||std::abs(f[2])>1e-4f))return false;
    pitch=std::atan2(f[2],h);return true;
}
// How far the camera's measured pitch must still move toward the aim's
// pitch: 0 inside the leash, otherwise the excess, capped at maxStep.
inline float PitchNeed(float camera,float desired,float leash,float maxStep) {
    if(!std::isfinite(camera)||!std::isfinite(desired)||!(maxStep>0))return 0;
    const float e=desired-camera;
    float need=e>leash?e-leash:e<-leash?e+leash:0;
    return need>maxStep?maxStep:need<-maxStep?-maxStep:need;
}
// Look pitch +1 tilts the camera up (measured in play, November 16).
inline float Follow(float current,float desired,float leash,float maxStep) {
    if(!std::isfinite(current))return current;
    if(!std::isfinite(desired)||!(maxStep>0))return current;
    const float diff=Wrap(desired-current);
    float need=0;
    if(diff>leash)need=diff-leash;
    else if(diff<-leash)need=diff+leash;
    if(need>maxStep)need=maxStep;
    if(need<-maxStep)need=-maxStep;
    return Wrap(current+need);
}
}
