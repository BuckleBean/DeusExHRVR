#include "HandOnlyGeometry.h"
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <iostream>

void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char** argv) {
    try {
        static_assert(sizeof(void*)==4);
        Check(argc==2,"expected mesh fixture path");
        std::ifstream f(argv[1],std::ios::binary);
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),{});
        Check(bytes.size()==1422080,"wrong fixture length");
        auto h=reinterpret_cast<uint32_t*>(bytes.data());
        auto address=[](void* p){return uint32_t(uintptr_t(p));};
        auto patch=[&](uint32_t& offset){Check(offset<bytes.size(),"offset outside fixture");offset+=address(bytes.data());};
        patch(h[21]);patch(h[22]);patch(h[24]);
        auto batches=reinterpret_cast<uint32_t*>(h[22]);
        for(unsigned i=0;i<9;i++){auto b=batches+i*24;patch(b[14]);patch(b[15]);patch(b[19]);}
        uint32_t model[32]{},render[16]{};model[25]=address(render);render[6]=address(h);
        HandMeshDiagnostic::targetRender=address(render);
        // Four incomplete loads must not permanently disable hands. A retry
        // inside the cooldown must do no further mesh work.
        auto signature=h[0];h[0]=0;
        for(unsigned i=0;i<6;i++) {
            HandOnlyGeometry::retryAfter=0; // advance the test's retry boundary
            Check(!HandOnlyGeometry::Prepare(model),"invalid mesh accepted");
            auto attempts=HandOnlyGeometry::builds.load();
            Check(!HandOnlyGeometry::Prepare(model),"cooldown accepted mesh");
            Check(HandOnlyGeometry::builds.load()==attempts,"cooldown rebuilt mesh");
        }
        h[0]=signature;HandOnlyGeometry::retryAfter=0;
        auto plan=HandOnlyGeometry::Prepare(model);
        Check(bool(plan),"installed asset failed runtime plan validation");
        uint32_t reloadedRender[16]{};reloadedRender[6]=address(h);
        for(unsigned i=0;i<6;i++) {
            model[25]=address(i%2?render:reloadedRender);
            Check(bool(HandOnlyGeometry::Prepare(model)),"level reload exhausted setup budget");
        }
        Check(plan->runs[0].size()==21 && plan->runs[1].size()==8,"unexpected run counts");
        unsigned total=0;for(const auto& list:plan->runs)for(auto r:list)total+=r.triangles;
        Check(total==3908,"wrong right-hand triangle count");
        Check(plan->bones[129] && plan->bones[130] && !plan->bones[128] && !plan->bones[146],"bone influence set");
        std::vector<unsigned char> pose(16+223*64);
        auto wrist=pose.data()+16+130*64;std::memset(wrist,0x35,64);
        auto poseAddress=uintptr_t(pose.data());
        HandOnlyGeometry::Activate(poseAddress,10,plan,wrist);
        Check(bool(HandOnlyGeometry::Match(poseAddress,10)),"exact-frame tag");
        Check(bool(HandOnlyGeometry::Match(poseAddress,11)),"deferred next-frame tag");
        Check(!HandOnlyGeometry::Match(poseAddress,12),"stale tag accepted");
        wrist[0]^=1;Check(!HandOnlyGeometry::Match(poseAddress,10),"changed palette accepted");wrist[0]^=1;
        uint32_t drawable[14]{};drawable[3]=address(render);drawable[5]=uint32_t(plan->batch);
        drawable[6]=uint32_t(plan->groups)+21*64;drawable[8]=address(pose.data());drawable[13]=(6594<<8)|2;
        auto originalBytes=bytes;
        unsigned calls=0,triangles=0;
        auto nativeDraw=[&](void* object,void*,void*,void*,bool) {
            auto d=static_cast<uint32_t*>(object),g=reinterpret_cast<uint32_t*>(d[6]);
            Check(object!=drawable,"shared drawable mutated");
            Check(d[6]!=drawable[6],"shared group mutated");
            Check((d[13]&255)==2,"flags changed");
            Check((d[13]>>8)==g[5],"triangle count mismatch");
            auto indices=reinterpret_cast<uint16_t*>(h[24]);auto vb=reinterpret_cast<unsigned char*>(batches[7*24+15]);
            for(unsigned i=0;i<g[5]*3;i++){float x;std::memcpy(&x,vb+indices[g[4]+i]*52,4);Check(x<0,"left-hand triangle submitted");}
            ++calls;triangles+=g[5];
        };
        Check(HandOnlyGeometry::DrawSelected(drawable,nullptr,nullptr,render,false,10,nativeDraw),"hand draw rejected");
        Check(calls==21 && triangles==3297,"main hand draw ranges");
        drawable[6]=uint32_t(plan->groups)+22*64;drawable[13]=(1222<<8)|2;
        Check(HandOnlyGeometry::DrawSelected(drawable,nullptr,nullptr,render,false,10,nativeDraw),"details draw rejected");
        Check(calls==29 && triangles==3908,"detail draw ranges");
        for(unsigned group: {23u,24u}) {
            drawable[6]=uint32_t(plan->groups)+group*64;
            Check(HandOnlyGeometry::DrawSelected(drawable,nullptr,nullptr,render,false,10,nativeDraw),"sleeve/cuff not suppressed");
        }
        Check(calls==29 && bytes==originalBytes,"shared mesh changed or sleeve drawn");
        HandOnlyGeometry::Activate(poseAddress,10,plan,wrist,true);
        unsigned both=0,leftTriangles=0;
        auto bothDraw=[&](void* object,void*,void*,void*,bool){
            auto d=static_cast<uint32_t*>(object),g=reinterpret_cast<uint32_t*>(d[6]);
            auto indices=reinterpret_cast<uint16_t*>(h[24]);auto vb=reinterpret_cast<unsigned char*>(batches[7*24+15]);
            float x;std::memcpy(&x,vb+indices[g[4]]*52,4);
            both+=g[5];if(x>0)leftTriangles+=g[5];
        };
        for(unsigned group: {21u,22u}){
            drawable[6]=uint32_t(plan->groups)+group*64;drawable[13]=((group==21?6594u:1222u)<<8)|2;
            Check(HandOnlyGeometry::DrawSelected(drawable,nullptr,nullptr,render,false,10,bothDraw),"both-hand draw");
        }
        Check(both==7816 && leftTriangles==3908 && bytes==originalBytes,"both hands complete without shared writes");
        HandOnlyGeometry::Invalidate(poseAddress);
        Check(!HandOnlyGeometry::DrawSelected(drawable,nullptr,nullptr,render,false,10,nativeDraw),"native palette filtered after invalidation");
        Check(!HandOnlyGeometry::DrawSelected(drawable,nullptr,nullptr,nullptr,false,10,nativeDraw),"null stream intercepted");
        drawable[3]=0;
        Check(!HandOnlyGeometry::DrawSelected(drawable,nullptr,nullptr,render,false,10,nativeDraw),"unrelated model intercepted");
        HandOnlyGeometry::Plan invalid;
        std::vector<unsigned char> verts(3*52);std::vector<uint16_t> ix{0,1,2};std::vector<uint32_t> gather{129};
        for(unsigned i=0;i<3;i++){float x=i?1.f:-1.f;std::memcpy(&verts[i*52],&x,4);verts[i*52+28]=255;}
        Check(!HandOnlyGeometry::Select(invalid,verts,52,ix,gather,0,0,1),"cross-center triangle accepted");
        for(unsigned i=0;i<3;i++){float x=-1.f;std::memcpy(&verts[i*52],&x,4);}
        gather[0]=128;Check(!HandOnlyGeometry::Select(invalid,verts,52,ix,gather,0,0,1),"foreign influence accepted");
        gather[0]=129;ix[2]=99;Check(!HandOnlyGeometry::Select(invalid,verts,52,ix,gather,0,0,1),"bad index accepted");
        // Unsupported-model fallback must hide only the tagged VR palette,
        // including when no supported plan has ever been cached.
        HandOnlyGeometry::cached.store({});
        drawable[3]=address(render);drawable[8]=address(pose.data());
        unsigned fallbackCalls=0;auto fallbackDraw=[&](void*,void*,void*,void*,bool){++fallbackCalls;};
        HandOnlyGeometry::Hide(uintptr_t(pose.data()),20,address(render),pose.data()+16);
        Check(HandOnlyGeometry::DrawSelected(drawable,nullptr,nullptr,render,false,20,fallbackDraw),"failed model not hidden");
        Check(HandOnlyGeometry::DrawSelected(drawable,nullptr,nullptr,render,false,21,fallbackDraw),"deferred failed model not hidden");
        Check(fallbackCalls==0,"native arms submitted during fallback");
        Check(!HandOnlyGeometry::DrawSelected(drawable,nullptr,nullptr,render,false,22,fallbackDraw),"stale fallback hid new draw");
        drawable[3]=0;
        Check(!HandOnlyGeometry::DrawSelected(drawable,nullptr,nullptr,render,false,20,fallbackDraw),"fallback hid different actor");
        drawable[3]=address(render);pose[16]^=1;
        Check(!HandOnlyGeometry::DrawSelected(drawable,nullptr,nullptr,render,false,20,fallbackDraw),"changed native palette hidden");
        pose[16]^=1;HandOnlyGeometry::Invalidate(uintptr_t(pose.data()));
        Check(!HandOnlyGeometry::DrawSelected(drawable,nullptr,nullptr,render,false,20,fallbackDraw),"native third-person palette hidden");
        HandOnlyGeometry::retryAfter=0;
        Check(bool(HandOnlyGeometry::Prepare(model)),"supported model did not recover after fallback");
        std::cout<<"PASS: actual mesh 87370; 3908 right-hand triangles in 29 ranges; no shared-buffer writes; sleeve/cuff removal; pose freshness/invalidation; malformed geometry rejection\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
