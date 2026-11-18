#pragma once
// Thrown grenades follow a chosen aim (ThrowAim, default Controller), and
// while the throw button is held a landing marker shows where the grenade
// should first hit (ThrowMarker, default on).
//
// Traced statically in DXHRDC 2.0.66.0 (preferred VAs), November 14-15 2026:
//   inventory release handler 0x66F095 -> SecondaryWeapon throw 0x75D560
//   -> launch frame 0x75CB80. For a player owner, 0x75CB80 asks CameraManager
//   (global 0x1C7CEC0) slot +0x88 with index 1, which returns manager+0x6F0
//   (the PlayerCamera; the constructor stores it at manager+8), and copies
//   that camera's slot +0x44 matrix, 0x69FC90 (lea eax,[ecx+40h]). It then
//   restores the release POSITION from the hand bone, so only the camera's
//   orientation decides where the grenade goes.
// 0x69FC90 is already hooked as PlayerWorldHook. While a throw (or a marker
// prediction) is running on this thread, PlayerWorldHook answers the
// PlayerCamera's query with the chosen aim instead of the level native camera.
//
// Prediction (marker): the same 0x75CB80 gives the release point, the aim
// frame, one launch angle (grenade data +0x14, degrees) and the speed
// (grenade data +0x18 / 30, units per 1/30 s). The path is stepped exactly
// like the game's own AI predictor 0x75C370: p(t) = start + v t + g t^2/2,
// g = (0,0,-3.44), t = 5,10,...,300, each segment asked of the game's
// collision query (global 0x1C7BF64, vtable +0x58, hash 0xC72C1BA3).
// The launch angle (data+0x14, 2 degrees for the grenades seen) is a random
// SPREAD: in the Nov 15 run every real launch direction (0x763D90's result)
// was 1.7-2.7 degrees from the hand aim, in a different direction each time,
// as 0x762E80's cone formula gives. The marker therefore launches along the
// aim itself (the centre of that scatter); the angle is only logged.
// Surface: the game's query result gives the contact point only, so the
// surface's facing is measured with two more segments parallel to the one
// that hit, offset sideways (SurfaceNormal); the ring is turned to match.
#include <cmath>
#include <cstdint>

namespace ThrowAim {
inline constexpr uintptr_t throwAddress=0x75D560;           // SecondaryWeapon throw (thiscall, 4 args, ret 10h)
inline constexpr unsigned char throwBytes[]={0x55,0x8b,0xec,0x83,0xe4,0xf0,0x81,0xec,0x44,0x01,0x00,0x00};
inline constexpr uintptr_t playerCameraOffset=0x6F0;        // CameraManager -> PlayerCamera
inline constexpr unsigned maxLogLines=300;                  // throws written per launch

// Game addresses the marker uses (preferred VAs; ThrowAimChecks confirms each).
inline constexpr uintptr_t playerControllers=0x1A07B1C;     // [[this]+0] = player controller; [controller+4] = actor
inline constexpr uintptr_t inventoryOf=0x66AF40;            // cdecl (actor) -> inventory; +0x18 = SecondaryWeapon
// 0x66BC70 (thiscall inventory): xor eax,eax; cmp [ecx+98h],eax; setne al; ret.
// The weapon HUD shows its "Weapon_Detonate" prompt (0x7BA5B0-0x7BA5E1) only
// when this is true for the player's inventory: a thrown remote device is
// waiting for the detonate input. The mod reads the field directly.
inline constexpr uintptr_t detonateCheck=0x66BC70;
inline constexpr uint32_t detonateField=0x98;
inline constexpr uintptr_t secondaryVtable=0xAB1A8C;
inline constexpr uintptr_t grenadeCount=0x75BF10;           // thiscall (weapon) -> count; <=0 nothing to throw
inline constexpr uintptr_t launchSetup=0x75CB80;            // thiscall (weapon, angles*, target*, frame*, speed*, bone) ret 14h
inline constexpr unsigned char launchSetupBytes[]={0x55,0x8b,0xec,0x83,0xe4,0xf0,0x81,0xec,0xb4,0x00,0x00,0x00};
inline constexpr uintptr_t launchFrame=0x763D90;            // cdecl, 7 args -> launch matrix (hooked only to LOG the real throw)
inline constexpr unsigned char launchFrameBytes[]={0x53,0x8b,0xdc,0x51,0x83,0xe4,0xf0,0x83,0xc4,0x04};
inline constexpr uintptr_t collisionWorld=0x1C7BF64;        // object; vtable +0x58 = segment query
inline constexpr uint32_t collisionHash=0xC72C1BA3;
inline constexpr float gravityZ=-3.44f;                     // per (1/30 s)^2, from 0x75C370
inline constexpr float stepT=5.f,firstT=5.f,lastT=300.f;   // 0x75C370's t loop
inline constexpr float speedDivisor=30.f;                   // 0x75CBA9: data+0x18 * (1/30)

// The CameraManager that owns this camera, if it is a PlayerCamera at all.
// DirectionWorld then refuses any manager that is not the current one, so a
// query from another camera keeps its native answer.
inline uintptr_t ManagerOf(const void* camera) {
    auto self=reinterpret_cast<uintptr_t>(camera);
    return self>playerCameraOffset?self-playerCameraOffset:0;
}

// Angle in degrees between two 3-vectors (matrix rows), for the throw log.
inline float RowAngleDeg(const float* a,const float* b) {
    double dot=0,aa=0,bb=0;
    for(int i=0;i<3;i++){dot+=double(a[i])*b[i];aa+=double(a[i])*a[i];bb+=double(b[i])*b[i];}
    if(!(aa>0)||!(bb>0))return -1.f;
    double c=dot/std::sqrt(aa*bb);
    if(c>1)c=1;if(c<-1)c=-1;
    return float(std::acos(c)*57.29577951308232);
}

// What one throw did, filled while the throw runs and written once after it.
struct Sample {
    unsigned queries{};     // PlayerCamera matrix requests during the throw
    unsigned applied{};     // of those, answered with the chosen aim
    float native[12]{};     // first request: native rows 0-2 (x,y,z each, row stride 4)
    float used[12]{};       // first request: rows actually returned
    // Marker check: what the prediction said at the moment of the throw, and
    // the launch frame the game actually built (0x763D90's result).
    bool predicted{},landed{},actualSeen{};
    float predictedStart[3]{},predictedDir[3]{},predictedLanding[3]{};
    float angleDeg{},speed{};
    float normal[3]{};bool normalValid{};
    float actualStart[3]{},actualForward[3]{};
    uint64_t heldMs{};
};
inline void CopyRows(float* dst,const float* src) {for(int i=0;i<12;i++)dst[i]=src[i];}

// Launch velocity from the aim frame (row-vector matrix, PlayerCamera axes:
// row 0 right, row 1 down, row 2 forward): forward tilted up by angleDeg.
inline void LaunchVelocity(const float* frame,float angleDeg,float speed,float* v) {
    const float a=angleDeg*0.01745329238f,c=std::cos(a),s=std::sin(a);
    for(int i=0;i<3;i++)v[i]=speed*(c*frame[8+i]-s*frame[4+i]);
    v[3]=0;
}

// Step the path like 0x75C370 and return the first contact. collide(p0,p1,hit)
// returns true with hit = contact point. Points are 16-byte aligned float[4].
// hitFrom/hitTo (optional) receive the segment that made contact.
template<class Collide>
bool PredictLanding(const float* start,const float* v,Collide&& collide,float* landing,unsigned* segments=nullptr,
                    float* hitFrom=nullptr,float* hitTo=nullptr) {
    alignas(16) float p0[4]{start[0],start[1],start[2],1},p1[4]{};
    unsigned n=0;
    for(float t=firstT;t<=lastT+0.001f;t+=stepT) {
        for(int i=0;i<3;i++)p1[i]=start[i]+v[i]*t+(i==2?gravityZ*t*t*.5f:0.f);
        p1[3]=1;++n;
        alignas(16) float hit[4]{};
        if(collide(p0,p1,hit)) {
            for(int i=0;i<3;i++)landing[i]=hit[i];
            if(hitFrom)for(int i=0;i<4;i++)hitFrom[i]=p0[i];
            if(hitTo)for(int i=0;i<4;i++)hitTo[i]=p1[i];
            if(segments)*segments=n;return true;
        }
        for(int i=0;i<4;i++)p0[i]=p1[i];
    }
    if(segments)*segments=n;
    return false;
}

// Facing of the surface at a contact. Two segments parallel to the one that
// hit, offset sideways by probeOffset in perpendicular directions, give two
// more contacts; the three points span the surface. The normal is turned to
// face the incoming throw. Refused (false) when a probe misses or lands far
// from the first contact (an edge or a different surface): the ring then lies
// flat as before.
inline constexpr float probeOffset=8.f;     // game units either side of the path
inline constexpr float probeReach=400.f;    // a probe contact farther than this from the first is another surface
inline void Cross(const float* a,const float* b,float* r){r[0]=a[1]*b[2]-a[2]*b[1];r[1]=a[2]*b[0]-a[0]*b[2];r[2]=a[0]*b[1]-a[1]*b[0];}
inline bool Normalize(float* v){float l=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);if(!(l>1e-6f))return false;for(int i=0;i<3;i++)v[i]/=l;return true;}
template<class Collide>
bool SurfaceNormal(const float* from,const float* to,const float* contact,Collide&& collide,float* normal) {
    float d[3]{to[0]-from[0],to[1]-from[1],to[2]-from[2]};
    if(!Normalize(d))return false;
    float helper[3]{0,0,1};if(std::fabs(d[2])>0.9f){helper[0]=1;helper[2]=0;}
    float u[3],w[3];Cross(d,helper,u);if(!Normalize(u))return false;Cross(d,u,w);if(!Normalize(w))return false;
    float hits[2][3];
    const float* sides[2]{u,w};
    for(int k=0;k<2;k++) {
        alignas(16) float a[4],b[4],h[4]{};
        for(int i=0;i<3;i++){a[i]=from[i]+sides[k][i]*probeOffset;b[i]=to[i]+sides[k][i]*probeOffset;}
        a[3]=b[3]=1;
        if(!collide(a,b,h))return false;
        float gap=0;for(int i=0;i<3;i++){hits[k][i]=h[i];gap+=(h[i]-contact[i])*(h[i]-contact[i]);}
        if(gap>probeReach*probeReach)return false;
    }
    float e1[3],e2[3];for(int i=0;i<3;i++){e1[i]=hits[0][i]-contact[i];e2[i]=hits[1][i]-contact[i];}
    Cross(e1,e2,normal);
    if(!Normalize(normal))return false;
    if(normal[0]*d[0]+normal[1]*d[1]+normal[2]*d[2]>0)for(int i=0;i<3;i++)normal[i]=-normal[i];
    return true;
}

// Game world point -> OpenXR position, for the frame drawn with this head
// world (PlayerCamera axes, right/down/forward, row 3 = position) from this
// head pose. Inverse of CameraMath::HeadWorld's position mapping.
struct Vec3{float x,y,z;};
struct Quat{float x,y,z,w;};
inline Vec3 RotateQ(Quat q,Vec3 v) {
    // v' = v + 2w(q x v) + 2 q x (q x v)
    Vec3 u{q.x,q.y,q.z};
    Vec3 t{2*(u.y*v.z-u.z*v.y),2*(u.z*v.x-u.x*v.z),2*(u.x*v.y-u.y*v.x)};
    return {v.x+q.w*t.x+(u.y*t.z-u.z*t.y),v.y+q.w*t.y+(u.z*t.x-u.x*t.z),v.z+q.w*t.z+(u.x*t.y-u.y*t.x)};
}
// Game world direction -> OpenXR direction for the same frame (no translation).
inline Vec3 DirectionToTracking(const float* direction,const float* headWorld,Quat headOrientation) {
    auto dot=[&](int row){return direction[0]*headWorld[row*4]+direction[1]*headWorld[row*4+1]+direction[2]*headWorld[row*4+2];};
    return RotateQ(headOrientation,{dot(0),-dot(1),-dot(2)});
}
inline Vec3 WorldToTracking(const float* world,const float* headWorld,Vec3 headPosition,Quat headOrientation,float scale) {
    float d[3];for(int i=0;i<3;i++)d[i]=world[i]-headWorld[12+i];
    auto dot=[&](int row){return d[0]*headWorld[row*4]+d[1]*headWorld[row*4+1]+d[2]*headWorld[row*4+2];};
    Vec3 local{dot(0)/scale,-dot(1)/scale,-dot(2)/scale}; // OpenXR right/up/back
    auto r=RotateQ(headOrientation,local);
    return {headPosition.x+r.x,headPosition.y+r.y,headPosition.z+r.z};
}

// Hold to aim, release to throw. Fed once per input poll for one input whose
// short press throws (tap target RB) or whose own target is RB.
struct HoldThrow {
    bool wasDown{};uint64_t downAt{},pulseUntil{};
    struct Result{bool aiming,throwPulse;uint64_t heldMs;};
    // holdMs: the input's hold delay (0 = none). Aiming starts once held that
    // long. A release after that throws with one 120 ms pulse. A release
    // before it throws only when the layout says a tap throws (tapThrows,
    // e.g. RightATap=RB); with RightA=RB and a hold delay a tap stays a
    // non-event, as the player set it up to avoid accidental throws.
    Result Update(bool down,uint64_t now,uint64_t holdMs,bool tapThrows) {
        uint64_t held=0;
        if(down && !wasDown)downAt=now;
        if(!down && wasDown) {
            held=now-downAt;
            if(held>=holdMs || tapThrows)pulseUntil=now+120;
            else held=0; // no throw: nothing for the throw log
        }
        wasDown=down;
        return {down && now-downAt>=holdMs,now<pulseUntil,held};
    }
    void Reset(){*this={};}
};
}
