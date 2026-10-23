#pragma once
#include "HandMeshDiagnostic.h"
#include <vector>
#include <memory>
#include <cmath>

// Experimental mesh-specific right-hand selection. Never changes shared game
// buffers: draws selected contiguous ranges from the original index buffer.
namespace HandOnlyGeometry {
struct Run {uint32_t start{},triangles{};};
struct Plan {
    uintptr_t render{},mesh{},groups{},batch{};
    std::vector<Run> runs[3],leftRuns[3];
    unsigned boneCount=223,partCount=2;
    unsigned groupIds[3]{21,22,0},triangleCounts[3]{6594,1222,0};
    unsigned hiddenGroups[2]{23,24};
    uintptr_t partBatches[3]{};
    std::array<bool,223> leftBones{};
    std::array<bool,223> bones{};
};
inline std::atomic<std::shared_ptr<const Plan>> cached;
inline std::atomic<unsigned> builds{},rejected{},posesMoved{},filtered{},sleevesSkipped{},bypassed{};
inline std::atomic<unsigned> leftPoses{},leftDraws{};
inline std::mutex prepareMutex;
inline uint64_t retryAfter{}; // guarded by prepareMutex
inline std::atomic<unsigned> rejectionStage{},observedStride{};
inline std::atomic<unsigned> observedBones{},observedLayout{},observedIndices{},hiddenDraws{};
inline bool ReadBlock(uintptr_t p,void* out,size_t n) {
    if(p>UINTPTR_MAX-n)return false;
    auto dst=static_cast<unsigned char*>(out);
    while(n){size_t k=(std::min)(n,size_t(65536));if(!HandMeshDiagnostic::Read(p,dst,k))return false;p+=k;dst+=k;n-=k;}
    return true;
}
inline bool Select(Plan& plan,const std::vector<unsigned char>& vertices,uint32_t stride,
    const std::vector<uint16_t>& indices,const std::vector<uint32_t>& gather,
    unsigned slot,uint32_t start,uint32_t triangles,bool left=false) {
    if(slot>2 || stride<36 || vertices.size()%stride || uint64_t(start)+uint64_t(triangles)*3>indices.size())return false;
    auto& runs=left?plan.leftRuns[slot]:plan.runs[slot];runs.clear();
    for(uint32_t t=0;t<triangles;t++) {
        bool negative=true,positive=true;
        for(unsigned j=0;j<3;j++) {
            auto v=indices[start+t*3+j];if(size_t(v)>=vertices.size()/stride)return false;
            float x{};std::memcpy(&x,&vertices[size_t(v)*stride],4);
            if(!std::isfinite(x))return false;
            negative&=x<0;positive&=x>0;
        }
        if(!negative && !positive)return false; // never cut a crossing triangle
        if(left?!positive:!negative)continue;
        for(unsigned j=0;j<3;j++) {
            auto p=&vertices[size_t(indices[start+t*3+j])*stride];unsigned sum=0;
            for(unsigned k=0;k<4;k++)if(p[28+k]) {
                sum+=p[28+k];auto local=p[32+k];if(local>=gather.size())return false;
                auto bone=gather[local];if(left?(bone<100 || bone>116):(bone<129 || bone>145))return false;
                (left?plan.leftBones:plan.bones)[bone]=true;
            }
            if(sum!=255)return false;
        }
        uint32_t at=start+t*3;
        if(!runs.empty() && runs.back().start+runs.back().triangles*3==at)++runs.back().triangles;
        else runs.push_back({at,1});
        if(runs.size()>64)return false;
    }
    return !runs.empty();
}
// The 162-bone player model uses the same wrist/finger landmarks, but
// divides its 3908 triangles per hand between groups 7, 8 and 9.
inline std::shared_ptr<const Plan> Prepare162(uintptr_t render,uintptr_t mesh,const uint32_t* h,unsigned& stage) {
    if(h[25]!=0x00050013 || h[26]!=162 || h[3]!=98868)return {};
    uint32_t groups[19][16]{},batches[5][24]{};
    stage=2;
    if(!ReadBlock(h[21],groups,sizeof(groups)) || !ReadBlock(h[22],batches,sizeof(batches)))return {};
    constexpr unsigned starts[]{44679,58914,62580},counts[]{4745,1222,1849};
    constexpr unsigned batchIds[]{1,1,2},rightTotals[]{1722,611,1575},leftTotals[]{3023,611,274};
    auto p=std::make_shared<Plan>();p->render=render;p->mesh=mesh;p->groups=h[21];
    p->boneCount=162;p->partCount=3;p->hiddenGroups[0]=4;p->hiddenGroups[1]=6;
    std::vector<uint16_t> indices(h[3]);stage=8;
    if(!ReadBlock(h[24],indices.data(),indices.size()*2))return {};
    for(unsigned slot=0;slot<3;slot++) {
        unsigned gi=7+slot,bi=batchIds[slot];auto b=batches[bi];stage=3;
        if(groups[gi][4]!=starts[slot] || groups[gi][5]!=counts[slot] ||
           b[20]!=(bi==1?9359u:2088u) || b[12]!=(bi==1?4u:1u) ||
           uint16_t(b[13])!=(bi==1?42u:16u))return {};
        uint32_t fmt[4]{},attrs[32]{};stage=4;
        if(!ReadBlock(b[19],fmt,sizeof(fmt)))return {};
        unsigned stride=(fmt[2]>>16)&255,n=fmt[2]&65535;observedStride=stride;stage=5;
        if(stride!=52 || n>16)return {};
        stage=6;if(!ReadBlock(b[19]+16,attrs,n*8))return {};
        bool position=false,weights=false,boneIndices=false;
        for(unsigned a=0;a<n;a++) {
            position|=attrs[a*2]==0xd2f7d823 && attrs[a*2+1]==0x00020000;
            weights|=attrs[a*2]==0x48e691c0 && attrs[a*2+1]==0x0006001c;
            boneIndices|=attrs[a*2]==0x5156d8d3 && attrs[a*2+1]==0x00070020;
        }
        stage=7;if(!position || !weights || !boneIndices)return {};
        std::vector<unsigned char> vertices(size_t(b[20])*stride);
        std::vector<uint32_t> gather(static_cast<size_t>(uint16_t(b[13])));stage=8;
        if(!ReadBlock(b[15],vertices.data(),vertices.size()) || !ReadBlock(b[14],gather.data(),gather.size()*4))return {};
        stage=9;if(!Select(*p,vertices,stride,indices,gather,slot,starts[slot],counts[slot]))return {};
        stage=10;if(!Select(*p,vertices,stride,indices,gather,slot,starts[slot],counts[slot],true))return {};
        unsigned rt=0,lt=0;for(auto r:p->runs[slot])rt+=r.triangles;for(auto r:p->leftRuns[slot])lt+=r.triangles;
        stage=11;if(rt!=rightTotals[slot] || lt!=leftTotals[slot])return {};
        p->groupIds[slot]=gi;p->triangleCounts[slot]=counts[slot];p->partBatches[slot]=h[22]+bi*96;
    }
    return p;
}
inline std::shared_ptr<const Plan> Prepare(void* model) {
    uint32_t render{},mesh{};
    if(!HandMeshDiagnostic::Read(uintptr_t(model)+0x64,&render,4) ||
       !HandMeshDiagnostic::Read(render+0x18,&mesh,4))return {};
    auto old=cached.load();
    if(old && old->render==render && old->mesh==mesh)return old;
    // Loading can expose an incomplete mesh. Never make that transient failure
    // permanent for the launch. Serialize builders and retry failures at most
    // once per second, keeping every geometry validation below intact.
    std::unique_lock lock(prepareMutex,std::try_to_lock);
    if(!lock.owns_lock())return {};
    old=cached.load();
    if(old && old->render==render && old->mesh==mesh)return old;
    if(GetTickCount64()<retryAfter)return {};
    if(DiagnosticOptions::hands)++builds;
    unsigned stage=1;
    auto fail=[&]()->std::shared_ptr<const Plan>{
        if(DiagnosticOptions::hands)++rejected;rejectionStage=stage;retryAfter=GetTickCount64()+1000;return {};
    };
    uint32_t h[32]{},g[25][16]{},b[24]{},fmt[4]{};
    if(!HandMeshDiagnostic::Read(mesh,h,sizeof(h)))return fail();
    observedBones=h[26];observedLayout=h[25];observedIndices=h[3];
    if(h[0]!=0x6873654d)return fail();
    if(h[26]==162) {
        auto result=Prepare162(render,mesh,h,stage);
        if(!result)return fail();
        cached.store(result);retryAfter=0;rejectionStage=0;return result;
    }
    if(h[25]!=0x00090019 || h[26]!=223 || h[3]!=76857)return fail();
    stage=2;
    if(!HandMeshDiagnostic::Read(h[21],g,sizeof(g)) ||
       !HandMeshDiagnostic::Read(h[22]+7*96,b,sizeof(b)))return fail();
    stage=3;
    if(g[21][4]!=49485 || g[21][5]!=6594 || g[22][4]!=69267 || g[22][5]!=1222 ||
       g[23][4]!=72933 || g[23][5]!=1036 || g[24][4]!=76041 || g[24][5]!=272 ||
       b[20]!=9158 || b[12]!=2 || uint16_t(b[13])!=34)return fail();
    stage=4;
    if(!HandMeshDiagnostic::Read(b[19],fmt,sizeof(fmt)))return fail();
    unsigned stride=(fmt[2]>>16)&255,attrs=fmt[2]&65535;
    observedStride=stride;stage=5;
    if(stride!=52 || attrs>16)return fail();
    uint32_t attr[32]{};bool position=false,weights=false,boneIndices=false;
    stage=6;
    if(!HandMeshDiagnostic::Read(b[19]+16,attr,attrs*8))return fail();
    for(unsigned i=0;i<attrs;i++) {
        auto hash=attr[i*2],desc=attr[i*2+1];unsigned offset=desc&65535,type=(desc>>16)&255;
        position|=hash==0xd2f7d823 && offset==0 && type==2;
        weights|=hash==0x48e691c0 && offset==28 && type==6;
        boneIndices|=hash==0x5156d8d3 && offset==32 && type==7;
    }
    stage=7;
    if(!position || !weights || !boneIndices)return fail();
    std::vector<unsigned char> vertices(size_t(b[20])*stride);
    std::vector<uint16_t> indices(h[3]);std::vector<uint32_t> gather(34);
    stage=8;
    if(!ReadBlock(b[15],vertices.data(),vertices.size()) || !ReadBlock(h[24],indices.data(),indices.size()*2) ||
       !ReadBlock(b[14],gather.data(),gather.size()*4))return fail();
    auto p=std::make_shared<Plan>();p->render=render;p->mesh=mesh;p->groups=h[21];p->batch=h[22]+7*96;p->partBatches[0]=p->partBatches[1]=p->batch;
    for(unsigned i=0;i<2;i++) {
        stage=9;
        if(!Select(*p,vertices,stride,indices,gather,i,g[21+i][4],g[21+i][5]))return fail();
        stage=10;
        if(!Select(*p,vertices,stride,indices,gather,i,g[21+i][4],g[21+i][5],true))return fail();
        stage=11;
        unsigned leftTotal=0;for(auto r:p->leftRuns[i])leftTotal+=r.triangles;
        if(leftTotal!=(i?611u:3297u))return fail();
        unsigned total=0;for(auto r:p->runs[i])total+=r.triangles;
        if(total!=(i?611u:3297u))return fail();
    }
    std::shared_ptr<const Plan> result=p;cached.store(result);retryAfter=0;rejectionStage=0;return result;
}
struct Tag {bool left{},hidden{};uintptr_t render{};unsigned witnessBone=130;uintptr_t pose{};uint64_t frame{};std::shared_ptr<const Plan> plan;std::array<unsigned char,64> wrist{};};
inline std::mutex tagMutex;
inline std::array<Tag,64> tags{};
inline unsigned cursor{};
inline void Invalidate(uintptr_t pose) {
    std::lock_guard lock(tagMutex);
    for(auto& t:tags)if(t.pose==pose){t.pose=0;t.plan.reset();}
}
inline void Activate(uintptr_t pose,uint64_t frame,std::shared_ptr<const Plan> plan,const void* wrist,bool left=false) {
    std::lock_guard lock(tagMutex);
    for(auto& old:tags)if(old.pose==pose){old.pose=0;old.plan.reset();}
    auto& t=tags[cursor++%tags.size()];
    t.hidden=false;t.witnessBone=130;t.render=plan->render;t.left=left;t.pose=pose;t.frame=frame;t.plan=std::move(plan);std::memcpy(t.wrist.data(),wrist,64);if(DiagnosticOptions::hands)++posesMoved;
    if(left)if(DiagnosticOptions::hands)++leftPoses;
}
// Suppress only this VR-generated player palette; native third-person draws
// invalidate it through SkeletonHook. Keep the original palette untouched.
inline void Hide(uintptr_t pose,uint64_t frame,uintptr_t render,const void* firstBone) {
    std::lock_guard lock(tagMutex);
    for(auto& old:tags)if(old.pose==pose){old.pose=0;old.plan.reset();}
    auto& t=tags[cursor++%tags.size()];t={};
    t.pose=pose;t.frame=frame;t.render=render;t.hidden=true;t.witnessBone=0;
    std::memcpy(t.wrist.data(),firstBone,64);
}
inline bool Hidden(uintptr_t pose,uint64_t frame,uintptr_t render) {
    std::lock_guard lock(tagMutex);
    for(const auto& t:tags)if(t.hidden && t.pose==pose && t.render==render &&
       frame>=t.frame && frame-t.frame<=1) {
        std::array<unsigned char,64> now{};
        if(HandMeshDiagnostic::Read(pose+16,now.data(),64) && now==t.wrist)return true;
    }
    return false;
}
inline std::shared_ptr<const Plan> Match(uintptr_t pose,uint64_t frame,bool* left=nullptr) {
    std::lock_guard lock(tagMutex);
    for(const auto& t:tags)if(t.pose==pose && t.plan && frame>=t.frame && frame-t.frame<=1) {
        std::array<unsigned char,64> now{};
        if(HandMeshDiagnostic::Read(pose+16+130*64,now.data(),64) && now==t.wrist){if(left)*left=t.left;return t.plan;}
    }
    return {};
}
template<class Draw> inline bool DrawSelected(void* drawable,void* device,void* state,void* stream,bool twice,uint64_t frame,Draw original) {
    auto words=static_cast<const uint32_t*>(drawable);
    if(Hidden(words[8],frame,words[3])){if(DiagnosticOptions::hands)++hiddenDraws;return true;}
    if(!stream)return false;
    // Match the tagged plan, not the diagnostic capture (which stops after four models).
    if(!cached.load())return false;
    bool left=false;auto p=Match(words[8],frame,&left);
    if(!p || words[3]!=p->render){if(DiagnosticOptions::hands)++bypassed;return false;}
    auto gp=uintptr_t(words[6]);
    if(gp<p->groups || (gp-p->groups)%64)return false;
    auto group=(gp-p->groups)/64;
    if(group==p->hiddenGroups[0] || group==p->hiddenGroups[1]){if(DiagnosticOptions::hands)++sleevesSkipped;return true;}
    unsigned slot=0;while(slot<p->partCount && p->groupIds[slot]!=group)++slot;
    if(slot==p->partCount || words[5]!=p->partBatches[slot])return false;
    if((words[13]>>8)!=p->triangleCounts[slot])return false;
    alignas(16) uint32_t localDrawable[14],localGroup[16];
    if(!HandMeshDiagnostic::Read(gp,localGroup,sizeof(localGroup)))return false;
    std::memcpy(localDrawable,drawable,sizeof(localDrawable));localDrawable[6]=uint32_t(uintptr_t(localGroup));
    auto drawRuns=[&](const std::vector<Run>& runs){for(auto run:runs) {
        localGroup[4]=run.start;localGroup[5]=run.triangles;
        localDrawable[13]=(words[13]&255)|(run.triangles<<8);
        original(localDrawable,device,state,stream,twice);
    }
    };
    drawRuns(p->runs[slot]);if(left){drawRuns(p->leftRuns[slot]);if(DiagnosticOptions::hands)++leftDraws;}
    if(DiagnosticOptions::hands)++filtered;return true;
}
inline void Flush() {
    if(!DiagnosticOptions::hands)return;
    static unsigned tick{};if(++tick!=1 && tick%5)return;
    FILE* f{};if(fopen_s(&f,"DeusExHRVR-hand-only.log","a") || !f)return;
    auto p=cached.load();
    fprintf(f,"handOnly version=3 ready=%d buildAttempts=%u rejected=%u poses=%u handDraws=%u sleeveDrawsSkipped=%u unmatchedDraws=%u runs=%u/%u rejectionStage=%u stride=%u bones=%u parts=%u observedBones=%u observedLayout=%08x observedIndices=%u fallbackDraws=%u\n",
        int(bool(p)),builds.load(),rejected.load(),posesMoved.load(),filtered.load(),sleevesSkipped.load(),bypassed.load(),
        p?unsigned(p->runs[0].size()):0,p?unsigned(p->runs[1].size()):0,rejectionStage.load(),observedStride.load(),p?p->boneCount:0,p?p->partCount:0,observedBones.load(),observedLayout.load(),observedIndices.load(),hiddenDraws.load());
    fclose(f);
    if(!fopen_s(&f,"DeusExHRVR-left-hand.log","a") && f){
        fprintf(f,"leftHand version=1 restPose=1 poses=%u draws=%u\n",leftPoses.load(),leftDraws.load());fclose(f);
    }
}
}

