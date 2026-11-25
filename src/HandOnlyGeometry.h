#pragma once
#include "HandMeshDiagnostic.h"
#include <vector>
#include <memory>
#include <cmath>

// Experimental mesh-specific right-hand selection. Never changes shared game
// buffers: draws selected contiguous ranges from the original index buffer.
namespace HandOnlyGeometry {
struct Run {uint32_t start{},triangles{};};
inline bool rideArmsEnabled=false; // set once at startup when FirstPersonTakedowns=1
struct ArmPart {unsigned group{},triangles{};uintptr_t batch{};std::vector<Run> runs[2];};
struct Plan {
    std::vector<ArmPart> arms;bool armsReady{};
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
// Read-only arm selection for the takedown ride-along (from the October full-arm work, with
// its October 31 jacket-layer correction). Stores draw ranges only; game buffers are untouched.
inline bool PrepareArms(Plan& p,const uint32_t* h){
    unsigned groupCount=h[25]&65535,batchCount=h[25]>>16;
    if(groupCount>25||batchCount>9)return false;
    std::vector<uint16_t> indices(h[3]);
    if(!ReadBlock(h[24],indices.data(),indices.size()*2))return false;
    unsigned first=0,total=0;
    for(unsigned batch=0;batch<batchCount;batch++){
        uint32_t b[24]{},fmt[4]{};
        auto address=uintptr_t(h[22])+batch*96;
        if(!ReadBlock(address,b,sizeof(b))||!ReadBlock(b[19],fmt,sizeof(fmt)))return false;
        unsigned stride=(fmt[2]>>16)&255,attrs=fmt[2]&65535;
        if((stride!=48&&stride!=52)||attrs>16||b[20]>20000||b[12]>groupCount-first)return false;
        uint32_t attributes[32]{};if(!ReadBlock(b[19]+16,attributes,attrs*8))return false;
        bool position=false,weights=false,bones=false;
        for(unsigned a=0;a<attrs;a++){
            position|=attributes[a*2]==0xd2f7d823&&attributes[a*2+1]==0x00020000;
            weights|=attributes[a*2]==0x48e691c0&&attributes[a*2+1]==0x0006001c;
            bones|=attributes[a*2]==0x5156d8d3&&attributes[a*2+1]==0x00070020;
        }
        if(!position||!weights||!bones)return false;
        unsigned gatherCount=uint16_t(b[13]);if(!gatherCount||gatherCount>223)return false;
        std::vector<uint32_t> gather(gatherCount);
        std::vector<unsigned char> vertices(size_t(b[20])*stride);
        if(!ReadBlock(b[14],gather.data(),gather.size()*4)||!ReadBlock(b[15],vertices.data(),vertices.size()))return false;
        for(auto bone:gather)if(bone>=p.boneCount)return false;
        for(unsigned gi=first;gi<first+b[12];gi++){
            bool selected=p.boneCount==223?(gi==1||gi==8):(gi==2||gi==4||gi==5||gi==6);
            if(!selected)continue;
            uint32_t g[16]{};if(!ReadBlock(h[21]+gi*64,g,sizeof(g))||uint64_t(g[4])+uint64_t(g[5])*3>indices.size())return false;
            ArmPart part;part.group=gi;part.batch=address;part.triangles=g[5];
            for(unsigned side=0;side<2;side++)for(unsigned tri=0;tri<g[5];tri++){
                bool keep=true;
                for(unsigned j=0;j<3;j++){
                    auto id=indices[g[4]+tri*3+j];if(id>=b[20])return false;
                    auto v=vertices.data()+id*stride;float x{};std::memcpy(&x,v,4);
                    if(!std::isfinite(x))return false;
                    keep&=side?x>52:x< -52;bool arm=false;unsigned sum=0;
                    for(unsigned k=0;k<4;k++)if(v[28+k]){
                        sum+=v[28+k];if(v[32+k]>=gatherCount)return false;
                        auto bone=gather[v[32+k]];
                        arm|=side?(bone>=96&&bone<=123):(bone>=125&&bone<=152);
                    }
                    if(sum!=255)return false;keep&=arm;
                }
                if(!keep)continue;
                auto& runs=part.runs[side];uint32_t at=g[4]+tri*3;
                if(!runs.empty()&&runs.back().start+runs.back().triangles*3==at)++runs.back().triangles;
                else runs.push_back({at,1});
                if(runs.size()>512)return false;++total;
            }
            p.arms.push_back(std::move(part));
        }
        first+=b[12];
    }
    return first==groupCount&&total==(p.boneCount==223?1486u:10029u);
}
inline void PrepareRideArms(Plan& p,const uint32_t* h) {
    if(!rideArmsEnabled)return;
    p.armsReady=PrepareArms(p,h);
    if(!p.armsReady)p.arms.clear();
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
    PrepareRideArms(*p,h);
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
    PrepareRideArms(*p,h);
    std::shared_ptr<const Plan> result=p;cached.store(result);retryAfter=0;rejectionStage=0;return result;
}
struct Tag {bool left{},hidden{},arms{},mirrored{};uintptr_t render{};unsigned witnessBone=130;uintptr_t pose{};uint64_t frame{};std::shared_ptr<const Plan> plan;std::array<unsigned char,64> wrist{};};
inline std::mutex tagMutex;
inline std::array<Tag,64> tags{};
inline unsigned cursor{};
inline void Invalidate(uintptr_t pose) {
    std::lock_guard lock(tagMutex);
    for(auto& t:tags)if(t.pose==pose){t.pose=0;t.plan.reset();}
}
// Left-handed mode: mirrored hand runs are drawn with the game's cull flipped, since a
// reflected mesh's triangles wind the other way. Set by EngineCamera; begin=true flips,
// false restores. Null means no flip is available.
inline void(*cullFlip)(void* state,bool begin)=nullptr;
inline void Activate(uintptr_t pose,uint64_t frame,std::shared_ptr<const Plan> plan,const void* wrist,bool left=false,bool arms=false,bool mirrored=false) {
    std::lock_guard lock(tagMutex);
    for(auto& old:tags)if(old.pose==pose){old.pose=0;old.plan.reset();}
    auto& t=tags[cursor++%tags.size()];
    t.arms=arms;t.mirrored=mirrored;t.hidden=false;t.witnessBone=130;t.render=plan->render;t.left=left;t.pose=pose;t.frame=frame;t.plan=std::move(plan);std::memcpy(t.wrist.data(),wrist,64);if(DiagnosticOptions::hands)++posesMoved;
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
inline std::shared_ptr<const Plan> Match(uintptr_t pose,uint64_t frame,bool* left=nullptr,bool* arms=nullptr,bool* mirrored=nullptr) {
    std::lock_guard lock(tagMutex);
    for(const auto& t:tags)if(t.pose==pose && t.plan && frame>=t.frame && frame-t.frame<=1) {
        std::array<unsigned char,64> now{};
        if(HandMeshDiagnostic::Read(pose+16+130*64,now.data(),64) && now==t.wrist){if(left)*left=t.left;if(arms)*arms=t.arms;if(mirrored)*mirrored=t.mirrored;return t.plan;}
    }
    return {};
}
template<class Draw> inline bool DrawSelected(void* drawable,void* device,void* state,void* stream,bool twice,uint64_t frame,Draw original) {
    auto words=static_cast<const uint32_t*>(drawable);
    if(Hidden(words[8],frame,words[3])){if(DiagnosticOptions::hands)++hiddenDraws;return true;}
    if(!stream)return false;
    // Match the tagged plan, not the diagnostic capture (which stops after four models).
    if(!cached.load())return false;
    bool left=false,arms=false,mirrored=false;auto p=Match(words[8],frame,&left,&arms,&mirrored);
    if(!p || words[3]!=p->render){if(DiagnosticOptions::hands)++bypassed;return false;}
    auto gp=uintptr_t(words[6]);
    if(gp<p->groups || (gp-p->groups)%64)return false;
    auto group=(gp-p->groups)/64;
    if(arms)for(const auto& part:p->arms)if(group==part.group){
        if(words[5]!=part.batch || (words[13]>>8)!=part.triangles)return true;
        alignas(16) uint32_t armDrawable[14],armGroup[16];
        if(!HandMeshDiagnostic::Read(gp,armGroup,sizeof(armGroup)))return true;
        std::memcpy(armDrawable,drawable,sizeof(armDrawable));armDrawable[6]=uint32_t(uintptr_t(armGroup));
        for(unsigned side=0;side<2;side++)for(auto run:part.runs[side]){
            armGroup[4]=run.start;armGroup[5]=run.triangles;armDrawable[13]=(words[13]&255)|(run.triangles<<8);
            original(armDrawable,device,state,stream,twice);
        }
        return true;
    }
    if(group==p->hiddenGroups[0] || group==p->hiddenGroups[1]){if(DiagnosticOptions::hands)++sleevesSkipped;return true;}
    unsigned slot=0;while(slot<p->partCount && p->groupIds[slot]!=group)++slot;
    // In the ride-along everything that is not an arm or hand stays undrawn.
    if(slot==p->partCount || words[5]!=p->partBatches[slot])return arms;
    if((words[13]>>8)!=p->triangleCounts[slot])return arms;
    alignas(16) uint32_t localDrawable[14],localGroup[16];
    if(!HandMeshDiagnostic::Read(gp,localGroup,sizeof(localGroup)))return arms;
    std::memcpy(localDrawable,drawable,sizeof(localDrawable));localDrawable[6]=uint32_t(uintptr_t(localGroup));
    auto drawRuns=[&](const std::vector<Run>& runs){for(auto run:runs) {
        localGroup[4]=run.start;localGroup[5]=run.triangles;
        localDrawable[13]=(words[13]&255)|(run.triangles<<8);
        original(localDrawable,device,state,stream,twice);
    }
    };
    const bool flip=mirrored&&cullFlip;
    if(flip)cullFlip(state,true);
    drawRuns(p->runs[slot]);if(left){drawRuns(p->leftRuns[slot]);if(DiagnosticOptions::hands)++leftDraws;}
    if(flip)cullFlip(state,false);
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

