#pragma once
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

// Temporary identification probe. No game memory writes or new hooks.
// Layout hypotheses come from rr ika (github.com/rrika/cdcEngineDXHR): dtp::Model,
// PCDX11RenderModel and RenderMesh definitions; the live Mesh signature and
// bounded counts must validate before following array pointers. Raw words are
// retained when layouts differ, rather than calling an unverified virtual method.
namespace HandMeshDiagnostic {
struct Snapshot {
    uintptr_t model{}, render{}, mesh{};
    uint32_t paletteCount{}, modelWords[32]{}, renderWords[16]{}, meshWords[32]{};
    bool modelRead{}, renderRead{}, groupsRead{}, batchesRead{};
    uint16_t groups{}, batches{}, bones{};
    uint32_t groupWords[64][16]{}, batchWords[16][24]{};
    uint32_t gather[16][64]{};
    bool gatherRead[16]{};
};
inline std::mutex mutex;
inline std::array<Snapshot,4> snapshots{};
inline unsigned captured{}, written{};
inline std::atomic<unsigned> seen{};
inline std::atomic<bool> full{};
inline std::atomic<uintptr_t> targetRender{};

inline bool Read(uintptr_t address, void* destination, size_t size) {
    if(address<0x10000 || size>0x10000 || address>UINTPTR_MAX-size)return false;
    SIZE_T actual{};
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),
        destination,size,&actual)!=FALSE && actual==size;
}
inline bool Mesh(Snapshot& s,uintptr_t address) {
    uint32_t words[32]{};
    if(!Read(address,words,sizeof(words)) || words[0]!=0x6873654d)return false;
    auto groups=uint16_t(words[25]),batches=uint16_t(words[25]>>16);
    auto bones=uint16_t(words[26]);
    if(!groups || groups>64 || !batches || batches>16 || bones>512 ||
       !words[3] || words[3]>3000000)return false;
    s.mesh=address;s.groups=groups;s.batches=batches;s.bones=bones;
    std::memcpy(s.meshWords,words,sizeof(words));
    s.groupsRead=Read(words[21],s.groupWords,groups*64);
    s.batchesRead=Read(words[22],s.batchWords,batches*96);
    if(s.batchesRead)for(unsigned i=0;i<batches;i++) {
        unsigned n=uint16_t(s.batchWords[i][13]);
        if(n && n<=64)s.gatherRead[i]=Read(s.batchWords[i][14],s.gather[i],n*4);
    }
    return true;
}
inline void Capture(void* model,uint32_t paletteCount) {
    if(full.load(std::memory_order_relaxed))return;
    seen.fetch_add(1,std::memory_order_relaxed);
    std::unique_lock lock(mutex,std::try_to_lock);
    if(!lock.owns_lock())return;
    for(unsigned i=0;i<captured;i++)if(snapshots[i].model==uintptr_t(model))return;
    if(captured==snapshots.size()){full=true;return;}
    auto& s=snapshots[captured++];s.model=uintptr_t(model);s.paletteCount=paletteCount;
    s.modelRead=Read(s.model,s.modelWords,sizeof(s.modelWords));
    if(!s.modelRead)return;
    // dtp::Model::renderMesh at +0x64. Probe only the bounded render object
    // and one MeshHeader indirection; every read can fail without a fault.
    s.render=s.modelWords[25];
    s.renderRead=Read(s.render,s.renderWords,sizeof(s.renderWords));
    if(!s.renderRead)return;
    for(auto candidate:s.renderWords) {
        if(Mesh(s,candidate))break;
        uint32_t headerMesh{};
        if(Read(candidate,&headerMesh,sizeof(headerMesh)) && Mesh(s,headerMesh))break;
    }
    if(s.mesh && s.groupsRead && s.batchesRead)targetRender.store(s.render);
}
inline void Flush() {
    // Called only by the existing once-per-second config worker. Never retain
    // game pointers for later dereferencing: all reads occurred in Capture.
    static bool started{};
    static unsigned ticks{};
    std::array<Snapshot,4> pending{};
    unsigned end{},begin{};
    {std::lock_guard lock(mutex);begin=written;end=captured;pending=snapshots;}
    if(started && begin==end && ++ticks%10!=0)return;
    FILE* f{};
    if(fopen_s(&f,"DeusExHRVR-hand-mesh.log","a") || !f)return;
    if(!started){fprintf(f,"handMeshDiagnostic version=2026-10-15-1 pid=%lu maxModels=4\n",GetCurrentProcessId());started=true;}
    fprintf(f,"status handSkeletonCalls=%u captured=%u state=%s\n",seen.load(),end,
        end?"captured":"waiting_for_hand_skeleton");
    for(unsigned i=begin;i<end;i++) {
        const auto& s=pending[i];
        fprintf(f,"model sample=%u address=%08x paletteCount=%u modelRead=%d render=%08x renderRead=%d mesh=%08x groups=%u batches=%u bones=%u groupsRead=%d batchesRead=%d\n",
            i,unsigned(s.model),s.paletteCount,int(s.modelRead),unsigned(s.render),int(s.renderRead),unsigned(s.mesh),s.groups,s.batches,s.bones,int(s.groupsRead),int(s.batchesRead));
        auto words=[f](const char* name,const uint32_t* p,unsigned n){fprintf(f,"%s",name);for(unsigned j=0;j<n;j++)fprintf(f," %08x",p[j]);fprintf(f,"\n");};
        words("modelWords",s.modelWords,32);words("renderWords",s.renderWords,16);words("meshWords",s.meshWords,32);
        if(s.groupsRead)for(unsigned j=0;j<s.groups;j++) {
            fprintf(f,"group=%u startIndex=%u triangles=%u material=%08x flags=%08x\n",j,s.groupWords[j][4],s.groupWords[j][5],s.groupWords[j][10],s.groupWords[j][7]);
        }
        if(s.batchesRead)for(unsigned j=0;j<s.batches;j++) {
            fprintf(f,"batch=%u groups=%u vertices=%u startIndex=%u triangles=%u gatherRead=%d\n",j,s.batchWords[j][12],s.batchWords[j][20],s.batchWords[j][21],s.batchWords[j][22],int(s.gatherRead[j]));
            words("batchWords",s.batchWords[j],24);
            if(s.gatherRead[j])words("boneGather",s.gather[j],uint16_t(s.batchWords[j][13]));
        }
    }
    bool ok=!ferror(f);
    if(fclose(f)!=0)ok=false;
    if(ok){std::lock_guard lock(mutex);written=end;}
}
}
