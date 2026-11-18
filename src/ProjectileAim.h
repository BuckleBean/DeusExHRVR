#pragma once
// Player projectiles (the rocket launcher, and any other gun whose shot is a
// flying object) follow the gun barrel (ProjectileAim, default 1).
//
// Traced statically in DXHRDC 2.0.66.0 (preferred VAs), November 15 2026:
//   PrimaryWeapon fire 0x752D10, fire type 2 (table 0x75427C) = 0x753A13:
//   muzzle from 0x74F3E0 (the mod's WeaponMuzzleHook: the hand-held barrel),
//   then 0x765080 / 0x765330 -> 0x7648F0 -> launch frame 0x763D90.
//   0x763D90's 7th argument is a target point. It is non-null only when the
//   weapon's [+0x70]+0x1B flag is set, and then the fire branch has already
//   aimed it with the (hooked) gun aim ray. With no target and a player owner
//   (0x607100), 0x763D90 asks 0x69FBE0(owner) -> slot +0x88(1) -> slot +0x44,
//   i.e. the PlayerCamera matrix (0x69FC90 = PlayerWorldHook), takes its
//   position (row 3) and forward (row 2), casts a ray 316050 units along it
//   (collision 0x1C7BF64 slot +0x54) and launches from the muzzle position
//   toward the hit point. The mod keeps that camera level and turned only by
//   snap turn, so rockets flew where the body faces.
// Only shots launched inside the fire function of the gun the hand holds are
// changed (a hook on 0x752D10 marks it), so grenades and anything else that
// reaches 0x763D90 keep their own path.
// While 0x763D90 runs for such a shot, PlayerWorldHook answers with a camera
// built from the launch matrix itself: position = muzzle, forward = barrel.
// The ray then runs straight down the barrel. Speed, spread, damage and the
// explosion stay the game's.
#include <cmath>
#include <cstdint>

namespace ProjectileAim {
inline constexpr uintptr_t fireAddress=0x752D10;          // PrimaryWeapon fire (thiscall, no stack args, returns eax)
inline constexpr unsigned char fireBytes[]={0x55,0x8b,0xec,0x83,0xe4,0xf0,0x81,0xec,0x94,0x03,0x00,0x00};
inline constexpr uintptr_t playerCameraVtable=0xAA8524;   // slot +0x44 = 0x69FC90
inline constexpr unsigned maxLogLines=300;                // shots written per launch
inline constexpr float maxBarrelGapDeg=20.f;              // launch matrix vs hand muzzle

// Why a shot kept the game's own aim (0 = barrel aim used).
enum Reason : unsigned {Applied=0,Disabled=1,NotGunFire=2,NotPlayer=3,HasTarget=4,HandsInactive=5,NotRigid=6,BarrelMismatch=7,CoverPointing=8};
inline const char* ReasonName(unsigned r) {
    static const char* names[]{"barrel","off","not-gun-fire","not-player","has-target","hands-inactive","not-rigid","barrel-mismatch","cover-pointing"};
    return r<sizeof(names)/sizeof(names[0])?names[r]:"?";
}

// The gun's firing frame in PlayerCamera axes (row 0 right, row 2 forward,
// row 3 position), from a muzzle matrix whose barrel points along -row 1.
// Same rows as CameraMath::FiringFromMuzzle (0x750DC0's conversion).
inline void CameraFromMuzzle(const float* muzzle,float* camera) {
    for(int i=0;i<16;i++)camera[i]=muzzle[i];
    for(int j=0;j<3;j++){camera[4+j]=muzzle[8+j];camera[8+j]=-muzzle[4+j];}
}

// Angle in degrees between two 3-vectors; -1 if either is zero.
inline float AngleDeg(const float* a,const float* b) {
    double dot=0,aa=0,bb=0;
    for(int i=0;i<3;i++){dot+=double(a[i])*b[i];aa+=double(a[i])*a[i];bb+=double(b[i])*b[i];}
    if(!(aa>0)||!(bb>0))return -1.f;
    double c=dot/std::sqrt(aa*bb);
    if(c>1)c=1;if(c<-1)c=-1;
    return float(std::acos(c)*57.29577951308232);
}

// What one player projectile did, filled while 0x763D90 runs.
struct Shot {
    unsigned reason{};
    unsigned queries{};       // PlayerCamera requests during the launch frame
    unsigned applied{};       // of those, answered with the barrel camera
    float barrelGapDeg{-1};   // launch matrix forward vs the hand muzzle forward
    float nativeForward[3]{}; // camera forward the game would have used
    float usedForward[3]{};   // barrel forward
    float launchForward[3]{}; // the launch frame the game built (row 2 of the result)
    float start[3]{};
};
}
