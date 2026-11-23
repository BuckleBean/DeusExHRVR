#pragma once
#include <windows.h>
#include <cstdint>
#include <cwchar>
#include <cstddef>
#include "ButtonBindings.h"
namespace Transport {
inline constexpr uint32_t Magic=0x52485844, Version=29;
struct alignas(8) MenuState {
    uint64_t gameTick,hostTick;
    uint32_t eligible,blocked,snapEnabled,request;
    uint32_t acknowledged,saved,result,requestedSnap;
    float degrees,requestedDegrees;
    float pointerX,pointerY;
    uint32_t pointerActive,pointerDown;
    uint32_t movement,requestedMovement;
    ButtonBindings::Layout bindings,requestedBindings;
    uint32_t bindingMask;
    uint32_t holdMs[8],tapTargets[8];
    uint32_t recenterRequest,recenterAck;
    uint32_t response,requestedResponse;
    uint32_t interaction,requestedInteraction,resetBindings,crosshair; // crosshair: CoverCrosshair mode (transport 26)
    // scopeZoom: the game's live ScopeMagnification (0 from a DLL that predates it).
    // requestedScopeZoom: 1-12 (the menu offers 3-12), or 0 = keep (what a companion that predates it sends).
    // Both reuse former reserved slots, so the layout and Version are unchanged.
    float worldScale,requestedWorldScale,activeWorldScale,scopeZoom;
    uint32_t dialogue,requestedDialogue,dialogueAvailable;float requestedScopeZoom;
    // Hand HUD (transport 23): handHud = the saved HandHud setting, 1 off / 2 on;
    // activeHandHud = the HUD running now, 1 game HUD / 2 hand HUD (switched live
    // when saved; differs only if the hand HUD could not start); requestedHandHud 0 = keep.
    uint32_t handHud,activeHandHud,requestedHandHud,requestedCrosshair; // from offset 372 (former tail padding)
    // Transport 24, menu tabs (Mark, Nov 17): four more settings, each applied
    // when saved. Game side: 0 = unavailable. Requests: 0 = keep.
    // throwAim = DirectionConfig::Source + 1 (1 game's own, 2 head, 3 right hand,
    // 4 left hand); the others 1 off / 2 on.
    uint32_t throwAim,requestedThrowAim,throwMarker,requestedThrowMarker;
    uint32_t coverFollow,requestedCoverFollow,immersiveScope,requestedImmersiveScope;
    // Transport 27: quickbar. 0 unavailable, 1 always shown, 2 auto-hide; request 0 = keep.
    uint32_t quickBar,requestedQuickBar;
    // Transport 28: image. ImageAdjust wire values; 0 = unavailable or keep.
    uint32_t brightness,contrast,requestedBrightness,requestedContrast;
    // Transport 29: HandPointer mode. 0 unavailable or keep.
    uint32_t handPointers,requestedHandPointers;
};
static_assert(sizeof(MenuState)==456 && offsetof(MenuState,throwAim)==388);
struct Quaternion {float x,y,z,w;};
struct Vector {float x,y,z;};
struct Pose {Quaternion orientation;Vector position;};
struct Eye {Pose pose;float left,right,up,down;};
struct Controller {Pose aim;uint32_t valid;};
struct GripPose {Pose pose;uint32_t valid;};
static_assert(sizeof(Pose)==28 && sizeof(Controller)==32 && sizeof(GripPose)==32);
struct Gamepad {
    uint32_t valid;
    uint16_t buttons;uint8_t leftTrigger,rightTrigger;
    int16_t leftX,leftY,rightX,rightY;
};
static_assert(sizeof(Gamepad)==16);
struct alignas(8) Tracking {
    uint64_t id,tick;
    Pose head;
    Eye eyes[2];
    uint32_t valid;
    Controller rightController;
    Gamepad gamepad;
    uint32_t controllerButtons; // Raw stick clicks: bit 0 left, bit 1 right (focused input only).
    uint32_t reserved; // Transport 22: bit 0 = companion cover-crosshair texture ready.
                       // Transport 25: bits 8-15 left grip, 16-23 right grip (0-255).
    Controller leftController; // Left aim pose, independent of grip/weapon alignment.
    GripPose leftGrip,rightGrip; // Unmodified OpenXR grip poses, same space/time as aim.
};
// marker*: the grenade landing marker for this frame (transport 21), in the
// same OpenXR space as tracking. markerState bit 0 = show it; bit 1 =
// markerNormal holds the facing of the surface it lies on (unit, OpenXR).
struct RenderInfo {Tracking tracking;uint32_t mode,eyeMask;float markerPosition[3];uint32_t markerState;float markerNormal[3];uint32_t markerReserved/*fade darkness, 0 none to 255 black*/;float coverPosition[3];uint32_t coverState;
    // Transport 29: where each hand's interaction line lands, [0] right, [1] left, in tracking space. State bit 0 right, bit 1 left.
    float handPointer[2][3];uint32_t handPointerState,handPointerReserved;};
static_assert(sizeof(Tracking)==288 && sizeof(RenderInfo)==376);
static_assert(offsetof(Tracking,rightController)==136 && offsetof(Tracking,gamepad)==168);
static_assert(offsetof(Tracking,leftController)==192 && offsetof(Tracking,leftGrip)==224 && offsetof(Tracking,rightGrip)==256);
// Identical ABI in the x86 game and x64 companion. GPU keyed mutex protects
// texture contents + frameId; generation publishes a new texture description.
struct alignas(8) Header {
    uint32_t magic,version,pid,reserved;
    volatile LONG generation;
    uint32_t width,height,format;
    uint64_t sharedHandle;
    LUID adapter;
    uint64_t frameId;
    uint32_t swapEyes,reserved2;
    volatile LONG trackingLock;
    uint32_t reserved3;
    Tracking tracking;
    RenderInfo rendered;
    volatile LONG menuLock;
    uint32_t menuPadding;
    MenuState menu;
};
static_assert(sizeof(Header)==1200);
static_assert(offsetof(Header,tracking)==72 && offsetof(Header,rendered)==360);
inline bool Compatible(const Header* h,DWORD pid) {
    return h && h->magic==Magic && h->version==Version && h->pid==pid;
}
inline bool LockMenu(Header* h) {return h && !InterlockedCompareExchange(&h->menuLock,1,0);}
inline void UnlockMenu(Header* h) {InterlockedExchange(&h->menuLock,0);}
inline bool ReadMenu(Header* h,MenuState& value) {
    if(!LockMenu(h))return false;
    value=h->menu;UnlockMenu(h);return true;
}
inline bool Fresh(uint64_t tick,uint64_t now) {return tick && now>=tick && now-tick<500;}
inline bool ReadTracking(Header* h,Tracking& result) {
    if(!h || InterlockedCompareExchange(&h->trackingLock,1,0))return false;
    result=h->tracking;InterlockedExchange(&h->trackingLock,0);return true;
}
inline void WriteTracking(Header* h,const Tracking& value) {
    if(!h || InterlockedCompareExchange(&h->trackingLock,1,0))return;
    h->tracking=value;InterlockedExchange(&h->trackingLock,0);
}
// A contended nonblocking mailbox read is not loss of tracking. Preserve the
// last sample briefly, but honour newly received invalid data and sample age.
struct TrackingReader {
    Tracking latest{};
    uint64_t reused{},rejected{};
    bool Read(Header* h,Tracking& result,uint64_t now) {
        if(!h){latest={};++rejected;return false;}
        Tracking incoming{};
        if(ReadTracking(h,incoming))latest=incoming;
        else ++reused;
        if(!latest.valid || !latest.tick || now<latest.tick || now-latest.tick>=250){++rejected;return false;}
        result=latest;return true;
    }
};
inline uint32_t PackGrips(float left,float right) {
    auto byte=[](float v){return uint32_t((v>=0?(v<=1?v:1.f):0.f)*255.f+.5f);}; // NaN packs as 0
    return (byte(left)<<8)|(byte(right)<<16);
}
inline float LeftGrip(uint32_t reserved){return float((reserved>>8)&255u)/255.f;}
inline float RightGrip(uint32_t reserved){return float((reserved>>16)&255u)/255.f;}
inline void Name(wchar_t (&name)[96],DWORD pid) {swprintf_s(name,L"Local\\DeusExHRVR-%lu",pid);}
inline void FrameEventName(wchar_t (&name)[96],DWORD pid) {swprintf_s(name,L"Local\\DeusExHRVR-frame-%lu",pid);}
}
