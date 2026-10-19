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
        Check(bytes.size()>128,"wrong fixture length");
        auto h=reinterpret_cast<uint32_t*>(bytes.data());
        auto address=[](void* p){return uint32_t(uintptr_t(p));};
        auto patch=[&](uint32_t& offset){Check(offset<bytes.size(),"offset outside fixture");offset+=address(bytes.data());};
        patch(h[21]);patch(h[22]);patch(h[24]);
        auto batches=reinterpret_cast<uint32_t*>(h[22]);
        for(unsigned i=0;i<5;i++){auto b=batches+i*24;patch(b[14]);patch(b[15]);patch(b[19]);}
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
        Check(plan->boneCount==162 && plan->partCount==3,"wrong variant selected");
        unsigned right=0,left=0;for(unsigned slot=0;slot<3;slot++) {
            for(auto r:plan->runs[slot])right+=r.triangles;
            for(auto r:plan->leftRuns[slot])left+=r.triangles;
        }
        Check(right==3908 && left==3908,"incomplete hand selection");
        std::vector<unsigned char> pose(16+162*64);auto wrist=pose.data()+16+130*64;std::memset(wrist,0x35,64);
        HandOnlyGeometry::Activate(uintptr_t(pose.data()),10,plan,wrist,true);
        auto originalBytes=bytes;unsigned drawnRight=0,drawnLeft=0;
        uint32_t drawable[14]{};drawable[3]=address(render);drawable[8]=address(pose.data());
        HandMeshDiagnostic::targetRender=0; // capture stopping must not break rendering
        auto draw=[&](void* object,void*,void*,void*,bool){
            auto d=static_cast<uint32_t*>(object),g=reinterpret_cast<uint32_t*>(d[6]);
            Check(object!=drawable,"shared drawable changed");
            auto b=reinterpret_cast<uint32_t*>(d[5]);auto vb=reinterpret_cast<unsigned char*>(b[15]);
            auto gather=reinterpret_cast<uint32_t*>(b[14]);auto indices=reinterpret_cast<uint16_t*>(h[24]);
            for(unsigned t=0;t<g[5];t++) {
                bool r=true,l=true;
                for(unsigned j=0;j<3;j++) {
                    auto v=vb+indices[g[4]+t*3+j]*52;float x;std::memcpy(&x,v,4);
                    r &= x<0;l &= x>0;
                    for(unsigned k=0;k<4;k++)if(v[28+k]) {
                        auto bone=gather[v[32+k]];
                        Check(x<0?(bone>=129 && bone<=145):(bone>=100 && bone<=116),"foreign bone drawn");
                    }
                }
                Check(r!=l,"crossing triangle");drawnRight+=r;drawnLeft+=l;
            }
        };
        for(unsigned slot=0;slot<3;slot++) {
            drawable[5]=uint32_t(plan->partBatches[slot]);drawable[6]=uint32_t(plan->groups)+plan->groupIds[slot]*64;
            drawable[13]=plan->triangleCounts[slot]<<8|2;
            Check(HandOnlyGeometry::DrawSelected(drawable,nullptr,nullptr,render,false,10,draw),"variant group rejected");
        }
        Check(drawnRight==3908 && drawnLeft==3908 && bytes==originalBytes,"geometry mismatch/shared writes");
        for(auto gi:plan->hiddenGroups) {
            drawable[6]=uint32_t(plan->groups)+gi*64;
            Check(HandOnlyGeometry::DrawSelected(drawable,nullptr,nullptr,render,false,10,draw),"sleeves remain");
        }
        Check(drawnRight==3908 && drawnLeft==3908,"sleeve drawn");
        HandOnlyGeometry::Invalidate(uintptr_t(pose.data()));
        Check(!HandOnlyGeometry::DrawSelected(drawable,nullptr,nullptr,render,false,10,draw),"native pose filtered");
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
        std::cout<<"PASS: actual mesh 10233; 3908 triangles per hand; correct bone weights; sleeves hidden; native pose unaffected; no diagnostic dependency or shared-buffer writes\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
