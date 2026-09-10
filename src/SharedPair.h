#pragma once
#include <windows.h>
#include <cstdint>
#include <cwchar>
namespace Transport {
inline constexpr uint32_t Magic=0x52485844, Version=2;
struct Quaternion {float x,y,z,w;};
struct Vector {float x,y,z;};
struct Pose {Quaternion orientation;Vector position;};
struct Eye {Pose pose;float left,right,up,down;};
struct alignas(8) Tracking {
    uint64_t id,tick;
    Pose head;
    Eye eyes[2];
    uint32_t valid;
};
struct RenderInfo {Tracking tracking;uint32_t mode,eyeMask;};
static_assert(sizeof(Tracking)==136 && sizeof(RenderInfo)==144);
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
};
static_assert(sizeof(Header)==352);
inline bool ReadTracking(Header* h,Tracking& result) {
    if(!h || InterlockedCompareExchange(&h->trackingLock,1,0))return false;
    result=h->tracking;InterlockedExchange(&h->trackingLock,0);return true;
}
inline void WriteTracking(Header* h,const Tracking& value) {
    if(!h || InterlockedCompareExchange(&h->trackingLock,1,0))return;
    h->tracking=value;InterlockedExchange(&h->trackingLock,0);
}
inline void Name(wchar_t (&name)[96],DWORD pid) {swprintf_s(name,L"Local\\DeusExHRVR-%lu",pid);}
}
