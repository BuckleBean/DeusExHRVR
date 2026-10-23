#pragma once
#include "DiagnosticOptions.h"
#include "NativeHandWeapon.h"
#include <atomic>
#include <mutex>
#include <array>
#include <cstdio>

namespace MuzzleQueryDiagnostic {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4324) // CameraMath::Matrix deliberately has 16-byte alignment.
#endif
// Values label the existing consumers; diagnostics never alter their result.
enum Site {Camera,Attachment,Muzzle,Aim,Draw,SiteCount};
inline const char* names[]{"camera","attachment","muzzle","aim","draw"};
struct Sample {
    uint64_t tick{},frame{},poseAge{},weaponAge{};
    uintptr_t weapon{},instance{},owner{},nativeReturn{};
    uint32_t key=~0u,buttons{};
    int mode=-1,sourceOnOwner=-1,dualBarrel=-1,firstPersonFlag=-1,attachment=-1;
    bool relevant{},safe{};
    Transport::Gamepad pad{};
    CameraMath::Matrix matrix{};
};
struct Burst {bool active{};uint64_t calls{},frames{},lastFrame{},recoveredTick{};unsigned fault{};Sample first{},last{};};
inline std::mutex mutex;
inline std::array<Burst,SiteCount*2> bursts{};
inline std::array<std::atomic<bool>,SiteCount*2> pending{};
inline unsigned Fault(const CameraMath::Matrix& m){
    if(!ModelHandAlignment::Finite(m))return 1;
    if(std::abs(m.m[3])+std::abs(m.m[7])+std::abs(m.m[11])>.001f||std::abs(m.m[15]-1)>.001f)return 2;
    for(int i=0;i<3;i++)for(int j=0;j<3;j++){
        double d=0;for(int k=0;k<3;k++)d+=double(m.m[i*4+k])*m.m[j*4+k];
        if(std::abs(d-(i==j?1.:0.))>.02)return 3;
    }
    return NativeHandWeapon::Rigid(m)?0:4;
}
// Fixed-size output queue; no file IO or allocation on game/render threads.
struct Entry {unsigned site{},reason{};Burst burst{};};
inline std::array<Entry,128> queue{};
inline unsigned count{};inline std::atomic<uint64_t> dropped{};
inline void Queue(unsigned site,unsigned reason,Burst& burst){
    if(count<queue.size())queue[count++]={site,reason,burst};else ++dropped;
    burst={};
}
inline void Failure(Site site,const Sample& sample){
    if(!DiagnosticOptions::hands)return;
    unsigned slot=unsigned(site)*2+unsigned(sample.relevant);auto fault=Fault(sample.matrix);
    std::lock_guard lock(mutex);auto& b=bursts[slot];
    if(b.active && (b.first.weapon!=sample.weapon || b.first.key!=sample.key || b.fault!=fault))Queue(site,1,b);
    if(!b.active || !b.calls){b.active=true;b.first=sample;b.fault=fault;b.lastFrame=sample.frame;b.frames=1;}
    else if(b.lastFrame!=sample.frame){++b.frames;b.lastFrame=sample.frame;}
    b.last=sample;++b.calls;pending[slot]=true;
}
inline void Success(Site site,bool relevant,uintptr_t weapon,uint64_t tick){
    if(!DiagnosticOptions::hands)return;
    unsigned slot=unsigned(site)*2+unsigned(relevant);
    if(!pending[slot].load(std::memory_order_relaxed))return;
    std::lock_guard lock(mutex);auto& b=bursts[slot];
    if(b.active){b.recoveredTick=tick;Queue(site,b.last.weapon==weapon?2:3,b);}
    pending[slot]=false;
}
inline void Flush(){
    if(!DiagnosticOptions::hands)return;
    std::array<Entry,128> local{};unsigned n{};
    {std::lock_guard lock(mutex);n=count;for(unsigned i=0;i<n;i++)local[i]=queue[i];count=0;
        // Periodic active-window reporting makes a persistent failure visible.
        for(unsigned i=0;i<bursts.size() && n<local.size();i++)if(bursts[i].active && bursts[i].calls){
            local[n++]={i/2,0,bursts[i]};bursts[i].calls=0;bursts[i].frames=0;
        }}
    if(!n)return;FILE* f{};if(fopen_s(&f,"DeusExHRVR-muzzle-context.log","a")||!f)return;
    for(unsigned i=0;i<n;i++){
        const auto& e=local[i];const auto& b=e.burst;const auto& a=b.first;const auto& z=b.last;
        fprintf(f,"muzzleContext site=%s relevant=%d reason=%u fault=%u key=%08x calls=%llu frames=%llu firstTick=%llu lastTick=%llu recoveredTick=%llu firstFrame=%llu lastFrame=%llu weaponAgeMs=%llu poseAgeMs=%llu mode=%d safe=%d return=%p sourceOnOwner=%d dualBarrel=%d firstPersonFlag=%d attachment=%d buttons=%08x weapon=%p instance=%p owner=%p dropped=%llu matrix=",
            names[e.site],int(a.relevant),e.reason,b.fault,a.key,b.calls,b.frames,a.tick,z.tick,b.recoveredTick,a.frame,z.frame,
            a.weaponAge,a.poseAge,a.mode,int(a.safe),reinterpret_cast<void*>(a.nativeReturn),a.sourceOnOwner,a.dualBarrel,a.firstPersonFlag,a.attachment,a.buttons,
            reinterpret_cast<void*>(a.weapon),reinterpret_cast<void*>(a.instance),reinterpret_cast<void*>(a.owner),dropped.load());
        for(float v:a.matrix.m)fprintf(f," %.9g",double(v));
        fprintf(f," padValid=%u padButtons=%04x rightTrigger=%u leftTrigger=%u\n",a.pad.valid,unsigned(a.pad.buttons),unsigned(a.pad.rightTrigger),unsigned(a.pad.leftTrigger));
    }
    fclose(f);
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
