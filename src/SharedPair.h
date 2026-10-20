#pragma once
#include <windows.h>
#include <cstdint>
#include <cwchar>
#include <cstddef>
namespace Transport {
inline constexpr uint32_t Magic=0x52485844, Version=8;
struct alignas(8) MenuState {
    uint64_t gameTick,hostTick;
    uint32_t eligible,blocked,snapEnabled,request;
    uint32_t acknowledged,saved,result,reserved;
    float degrees,requestedDegrees;
    float pointerX,pointerY;
    uint32_t pointerActive,pointerDown;
};
static_assert(sizeof(MenuState)==72);
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
    uint32_t reserved;
    GripPose leftGrip,rightGrip; // Unmodified OpenXR grip poses, same space/time as aim.
};
struct RenderInfo {Tracking tracking;uint32_t mode,eyeMask;};
static_assert(sizeof(Tracking)==256 && sizeof(RenderInfo)==264);
static_assert(offsetof(Tracking,rightController)==136 && offsetof(Tracking,gamepad)==168);
static_assert(offsetof(Tracking,leftGrip)==192 && offsetof(Tracking,rightGrip)==224);
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
static_assert(sizeof(Header)==672);
static_assert(offsetof(Header,tracking)==72 && offsetof(Header,rendered)==328);
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
inline void Name(wchar_t (&name)[96],DWORD pid) {swprintf_s(name,L"Local\\DeusExHRVR-%lu",pid);}
inline void FrameEventName(wchar_t (&name)[96],DWORD pid) {swprintf_s(name,L"Local\\DeusExHRVR-frame-%lu",pid);}
}
