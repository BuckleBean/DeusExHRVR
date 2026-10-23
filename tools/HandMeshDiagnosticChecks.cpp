#include "HandMeshDiagnostic.h"
#include "HandDrawDiagnostic.h"
#include <cstdio>

int main() {
    DiagnosticOptions::hands=true;
    using namespace HandMeshDiagnostic;
    static_assert(sizeof(void*)==4,"Probe must match the game architecture");
    uint32_t model[32]{},render[16]{},header[4]{},mesh[32]{};
    uint32_t groups[16]{},batch[24]{},gather[2]{129,130};
    auto ptr=[](void* p){return uint32_t(uintptr_t(p));};
    model[25]=ptr(render);render[5]=ptr(header);header[0]=ptr(mesh);
    mesh[0]=0x6873654d;mesh[3]=3;mesh[21]=ptr(groups);mesh[22]=ptr(batch);
    mesh[25]=1|(1<<16);mesh[26]=223;
    groups[5]=1;batch[12]=1;batch[13]=2;batch[14]=ptr(gather);batch[20]=3;batch[22]=1;
    Capture(model,223);
    if(captured!=1 || snapshots[0].mesh!=uintptr_t(mesh) ||
       !snapshots[0].groupsRead || !snapshots[0].batchesRead ||
       !snapshots[0].gatherRead[0] || snapshots[0].gather[0][1]!=130)return 1;
    Capture(model,223);
    if(captured!=1)return 2;
    uint32_t value{};
    if(Read(1,&value,4) || Read(UINTPTR_MAX-1,&value,4))return 3;
    void* inaccessible=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);
    if(!inaccessible)return 4;
    bool rejected=!Read(uintptr_t(inaccessible),&value,4);
    VirtualFree(inaccessible,0,MEM_RELEASE);
    if(!rejected)return 5;
    Snapshot s{};
    mesh[25]=65|(1<<16);
    if(Mesh(s,uintptr_t(mesh)))return 6;
    mesh[25]=1|(1<<16);mesh[21]=1;
    if(!Mesh(s,uintptr_t(mesh)) || s.groupsRead)return 7;
    mesh[0]=0;
    if(Mesh(s,uintptr_t(mesh)))return 8;
    uint32_t drawable[14]{};
    drawable[3]=ptr(render);drawable[5]=ptr(batch);drawable[6]=ptr(groups);
    drawable[8]=0x12340000;drawable[13]=1<<8;
    HandDrawDiagnostic::MarkPose(drawable[8],100);
    HandDrawDiagnostic::Observe(drawable,render,false,100);
    if(HandDrawDiagnostic::eventCount!=1 || !HandDrawDiagnostic::events[0].poseMatched ||
       HandDrawDiagnostic::events[0].groupIndex!=0 || HandDrawDiagnostic::events[0].batchIndex!=0)return 9;
    HandDrawDiagnostic::Observe(drawable,render,false,100);
    if(HandDrawDiagnostic::eventCount!=1)return 10;
    HandDrawDiagnostic::Observe(drawable,render,false,101);
    if(HandDrawDiagnostic::eventCount!=2 || HandDrawDiagnostic::events[1].poseFrame!=100)return 11;
    HandDrawDiagnostic::Observe(drawable,render,false,103);
    if(HandDrawDiagnostic::eventCount!=3 || HandDrawDiagnostic::events[2].poseMatched)return 12;
    HandDrawDiagnostic::Observe(drawable,nullptr,false,103);
    if(HandDrawDiagnostic::eventCount!=3 || HandDrawDiagnostic::nullStreams!=1)return 13;
    drawable[3]=0;
    HandDrawDiagnostic::Observe(drawable,render,false,103);
    if(HandDrawDiagnostic::eventCount!=3)return 14;
    std::puts("PASS: mesh identification, invalid memory/count rejection, group/batch matching, pose freshness, deduplication, null streams and unrelated models");
    return 0;
}
