#pragma once
#include "HandMeshDiagnostic.h"

namespace HandDrawDiagnostic {
struct Pose {uintptr_t data{};uint64_t frame{};};
struct Event {
    uint64_t frame{}, poseFrame{};
    uintptr_t drawable{},pose{},group{},batch{};
    uint32_t start{},triangles{},groupTriangles{},flags{};
    int groupIndex{-1},batchIndex{-1};
    bool poseMatched{},twice{};
};
inline std::mutex mutex;
inline std::array<Pose,64> poses{};
inline unsigned poseCursor{},eventCount{},written{};
inline std::array<Event,512> events{};
inline std::atomic<unsigned> calls{},modelMatches{},nullStreams{},readFailures{},lockMisses{};
inline std::atomic<int> hookStatus{}; // 1 installed, -1 signature, -2 create, -3 enable

inline void MarkPose(uintptr_t data,uint64_t frame) {
    if(!DiagnosticOptions::hands)return;
    std::unique_lock lock(mutex,std::try_to_lock);
    if(!lock.owns_lock()){++lockMisses;return;}
    for(const auto& p:poses)if(p.data==data && p.frame==frame)return;
    poses[poseCursor++%poses.size()]={data,frame};
}
inline bool FindPose(uintptr_t data,uint64_t frame,uint64_t& foundFrame) {
    bool found=false;
    for(const auto& p:poses)if(p.data && p.data==data && p.frame<=frame &&
        frame-p.frame<=1 && (!found || p.frame>foundFrame)) {found=true;foundFrame=p.frame;}
    return found;
}
inline void Observe(void* drawable,void* stream,bool twice,uint64_t frame) {
    if(!DiagnosticOptions::hands)return;
    ++calls;
    auto target=HandMeshDiagnostic::targetRender.load(std::memory_order_relaxed);
    if(!target)return;
    // Valid native drawable at this function's entry; +0x0c is read by the
    // original function too (DXHRDC.exe 0x540160). No guessed pointer walk here.
    auto words=static_cast<const uint32_t*>(drawable);
    if(words[3]!=target)return;
    ++modelMatches;
    if(!stream){++nullStreams;return;}
    std::unique_lock lock(mutex,std::try_to_lock);
    if(!lock.owns_lock()){++lockMisses;return;}
    if(eventCount==events.size())return;
    Event e{};e.frame=frame;e.drawable=uintptr_t(drawable);
    e.batch=words[5];e.group=words[6];e.pose=words[8];e.flags=words[13];
    e.triangles=e.flags>>8;e.twice=twice;
    e.poseMatched=FindPose(e.pose,frame,e.poseFrame);
    uint32_t gr[6]{};
    if(!HandMeshDiagnostic::Read(e.group,gr,sizeof(gr))){++readFailures;return;}
    e.start=gr[4];e.groupTriangles=gr[5];
    {
        std::unique_lock modelLock(HandMeshDiagnostic::mutex,std::try_to_lock);
        if(!modelLock.owns_lock()){++lockMisses;return;}
        for(unsigned i=0;i<HandMeshDiagnostic::captured;i++) {
            const auto& s=HandMeshDiagnostic::snapshots[i];
            if(s.render!=target)continue;
            uintptr_t gb=s.meshWords[21],bb=s.meshWords[22];
            if(e.group>=gb && (e.group-gb)%64==0 && (e.group-gb)/64<s.groups)e.groupIndex=int((e.group-gb)/64);
            if(e.batch>=bb && (e.batch-bb)%96==0 && (e.batch-bb)/96<s.batches)e.batchIndex=int((e.batch-bb)/96);
        }
    }
    // Keep distinct combinations, not hundreds of identical stereo/pass calls.
    for(unsigned i=0;i<eventCount;i++) {
        const auto& old=events[i];
        if(old.group==e.group && old.batch==e.batch && old.start==e.start &&
           old.triangles==e.triangles && old.flags==e.flags && old.twice==e.twice &&
           old.poseMatched==e.poseMatched &&
           (!e.poseMatched || old.frame-old.poseFrame==e.frame-e.poseFrame))return;
    }
    events[eventCount++]=e;
}
inline void Flush() {
    if(!DiagnosticOptions::hands)return;
    static unsigned ticks{};
    std::array<Event,512> copy{};unsigned begin{},end{};
    {std::lock_guard lock(mutex);copy=events;begin=written;end=eventCount;}
    if(++ticks!=1 && begin==end && ticks%10!=0)return;
    FILE* f{};if(fopen_s(&f,"DeusExHRVR-hand-draw.log","a") || !f)return;
    fprintf(f,"handDraw version=2026-10-15-2 pid=%lu hook=%d calls=%u modelMatches=%u nullStreams=%u readFailures=%u lockMisses=%u unique=%u capacity=512\n",
        GetCurrentProcessId(),hookStatus.load(),calls.load(),modelMatches.load(),nullStreams.load(),readFailures.load(),lockMisses.load(),end);
    for(unsigned i=begin;i<end;i++) {
        const auto& e=copy[i];
        fprintf(f,"draw frame=%llu poseFrame=%llu poseMatched=%d group=%d batch=%d startIndex=%u triangles=%u groupTriangles=%u flags=%08x twice=%d drawable=%08x pose=%08x\n",
            e.frame,e.poseFrame,int(e.poseMatched),e.groupIndex,e.batchIndex,e.start,e.triangles,e.groupTriangles,e.flags,int(e.twice),unsigned(e.drawable),unsigned(e.pose));
    }
    bool ok=!ferror(f);if(fclose(f)!=0)ok=false;
    if(ok){std::lock_guard lock(mutex);written=end;}
}
}
