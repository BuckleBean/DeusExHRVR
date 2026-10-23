#pragma once
#include "CameraMath.h"
#include <array>
#include <mutex>
#include <cstdio>

// Capture paired draw stages on the same sampled frames, without engine calls
// or file IO on render threads. Matrices retain their original shader metadata.
namespace HandWeaponMotionDiagnostic {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4324) // Matrix requires 16-byte alignment.
#endif
struct Sample {
    uint64_t frame{},tick{},tracking{};
    uintptr_t weapon{},instance{};
    uint32_t key=~0u,count{};
    unsigned site{},sprint{},nativeValid{};
    CameraMath::Matrix before[16]{},after[16]{},skin{},delta{},target{},muzzle{},base{},viewWorld{};
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
inline std::mutex mutex;
inline std::array<Sample,128> queue{};
inline unsigned used{};
inline uint64_t dropped{},lastFrame[2]{UINT64_MAX,UINT64_MAX},written{};
// Off unless HandWeaponMotionLog=1 in [VR]. Left always-on through the hands
// diagnostics work, it wrote up to ~55 MB a session to
// DeusExHRVR-hand-weapon-motion.log for nobody. Nothing is queued while this
// is false, and Flush() returns early on an empty queue, so the file is never
// opened.
inline bool enabled{};
inline bool Wanted(unsigned site,uint64_t frame){
    if(!enabled || site>1 || frame%3)return false;
    std::lock_guard lock(mutex);
    if(written>=24000 || lastFrame[site]==frame)return false;
    lastFrame[site]=frame;return true;
}
inline void Push(const Sample& s){
    std::lock_guard lock(mutex);
    if(used<queue.size())queue[used++]=s;else ++dropped;
}
inline void Matrix(FILE* f,const char* name,const CameraMath::Matrix& m){
    fprintf(f," %s=",name);for(float v:m.m)fprintf(f," %.9g",double(v));
}
inline void Flush(){
    if(!enabled)return;
    // Static storage: do not put this large batch on the x86 watcher stack.
    static std::array<Sample,128> batch;
    unsigned n{};uint64_t lost{};
    {std::lock_guard lock(mutex);n=used;if(!n)return;
        for(unsigned i=0;i<n;i++)batch[i]=queue[i];used=0;lost=dropped;written+=n;}
    FILE* f{};if(fopen_s(&f,"DeusExHRVR-hand-weapon-motion.log","a") || !f)return;
    for(unsigned i=0;i<n;i++){
        const auto& s=batch[i];
        fprintf(f,"motion version=1 site=%u frame=%llu tick=%llu tracking=%llu weapon=%08x instance=%08x key=%08x sprint=%u nativeValid=%u count=%u captured=%u dropped=%llu",
            s.site,s.frame,s.tick,s.tracking,unsigned(s.weapon),unsigned(s.instance),s.key,s.sprint,s.nativeValid,s.count,(s.count<16?s.count:16),lost);
        Matrix(f,"skin",s.skin);Matrix(f,"delta",s.delta);Matrix(f,"target",s.target);
        Matrix(f,"muzzle",s.muzzle);Matrix(f,"base",s.base);Matrix(f,"viewWorld",s.viewWorld);
        for(unsigned j=0;j<s.count && j<16;j++){
            char label[24];sprintf_s(label,"before%u",j);Matrix(f,label,s.before[j]);
            sprintf_s(label,"after%u",j);Matrix(f,label,s.after[j]);
        }
        fprintf(f,"\n");
    }
    fclose(f);
}
}
