#include "FullArmIK.h"
#include "ArmPoseHistory.h"
#include "ArmBodyHeading.h"
#include "InstalledArmPoseHistory.h"
#include "HandOnlyGeometry.h"
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <cstdio>
void Check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main(int argc,char**argv){try{
    static_assert(sizeof(void*)==4);
    Check(argc==4,"mesh, skeleton, output required");
    std::ifstream f(argv[1],std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),{});
    Check(bytes.size()>128,"mesh missing");
    auto h=reinterpret_cast<uint32_t*>(bytes.data());auto addr=[](void*p){return uint32_t(uintptr_t(p));};
    auto patch=[&](uint32_t& o){Check(o<bytes.size(),"fixture offset");o+=addr(bytes.data());};
    patch(h[21]);patch(h[22]);patch(h[24]);
    for(unsigned i=0;i<(h[25]>>16);i++){
        auto b=reinterpret_cast<uint32_t*>(h[22])+i*24;patch(b[14]);patch(b[15]);patch(b[19]);
    }
    uint32_t model[32]{},render[16]{};model[25]=addr(render);render[6]=addr(h);
    HandOnlyGeometry::fullArmsEnabled=true;
    auto plan=HandOnlyGeometry::Prepare(model);Check(plan&&plan->armsReady,"arm mesh validation");
    unsigned total=0;for(const auto& part:plan->arms)for(const auto& side:part.runs)for(auto r:side)total+=r.triangles;
    Check(total==(h[26]==223?6543u:13439u),"full arm triangle count");
    std::ifstream s(argv[2],std::ios::binary);std::array<ModelHandAlignment::Segment,223> segments{};
    for(unsigned i=0;i<h[26];i++){float xyz[3]{};s.read(reinterpret_cast<char*>(xyz),12);s.read(reinterpret_cast<char*>(&segments[i].parent),4);for(int j=0;j<3;j++)segments[i].pivot[j]=xyz[j];}
    Check(bool(s),"skeleton missing");auto rig=FullArmIK::Build(segments,h[26]);Check(rig.valid,"rig validation");

    using namespace FullArmIK;
    Matrix head{{-1,0,0,0,0,0,-1,0,0,-1,0,0,0,0,587.326f,1}};
    std::ofstream out(argv[3],std::ios::binary);
    auto identity=CameraMath::Rotation({0,0,0,1});
    const V targets[]={{0,0,0},{65,-130,480},{100,-60,640},{65,-180,497.326},{65,10.9858,497.326}};
    for(unsigned pose=0;pose<5;pose++){
        std::array<Matrix,223> palette;palette.fill(identity);
        if(pose){
            for(unsigned sh:{96u,125u}){
                auto target=targets[pose];if(sh==125)target[0]*=-1;
                auto m=Map(identity,rig.p[sh+5],target);
                for(unsigned i=sh==96?101:130;i<=(sh==96?116u:145u);i++)palette[i]=m;
            }
            auto saved=palette;
            Check(Apply(palette.data(),h[26],rig,head,plan->armBones),"export pose rejected");
            for(unsigned sh:{96u,125u}){
                auto error=Sub(Point(rig.p[sh],palette[sh]),rig.p[sh]);
                Check(std::sqrt(Dot(error,error))<.002,"export shoulder moved");
            }
            for(unsigned i=0;i<h[26];i++)if(Hand(i))Check(!memcmp(&palette[i],&saved[i],64),"export changed hand");
        }
        out.write(reinterpret_cast<const char*>(palette.data()),h[26]*64);
    }
    Check(bool(out),"export write failed");
    printf("PASS reference plus forward, raised, extended, folded palettes; shoulders anchored and hands preserved.\n");
    return 0;
}catch(const std::exception& e){printf("FAIL: %s\n",e.what());return 1;}}
