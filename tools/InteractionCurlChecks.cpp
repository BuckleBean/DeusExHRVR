#include "InteractionCurl.h"
#include <fstream>
#include <cstdio>
#include <stdexcept>
#include <random>
using namespace InteractionCurl;
void Check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main(int argc,char** argv){try{
    Check(argc>=2,"skeleton fixtures required");
    std::mt19937 rotations(71);std::uniform_real_distribution<float> component(-1,1);
    for(int i=0;i<1000;i++){
        auto q=NormalizeQ({component(rotations),component(rotations),component(rotations),component(rotations)});
        auto a=CameraMath::Rotation(q),b=CameraMath::Rotation(RotationQ(a));
        for(int j=0;j<16;j++)Check(std::abs(a.m[j]-b.m[j])<.00001f,"quaternion round trip including large rotations");
    }
    for(int file=1;file<argc;file++){
        std::ifstream in(argv[file],std::ios::binary|std::ios::ate);auto size=in.tellg();in.seekg(0);
        unsigned count=unsigned(size)/16;Check(count==162||count==223,"fixture shape");
        std::array<Segment,223> segments{};
        for(unsigned i=0;i<count;i++){float p[3];in.read(reinterpret_cast<char*>(p),12);in.read(reinterpret_cast<char*>(&segments[i].parent),4);for(int j=0;j<3;j++)segments[i].pivot[j]=p[j];}
        auto d=Build(segments,count);Check(d.valid,"valid fixture");
        std::mt19937 rng(42);std::uniform_real_distribution<float> rand(-1,1);
        for(int trial=0;trial<100;trial++){
            std::array<Matrix,223> palette{};
            auto global=CameraMath::Rotation(NormalizeQ({rand(rng),rand(rng),rand(rng),rand(rng)}));
            global.m[12]=rand(rng)*10000;global.m[13]=rand(rng)*10000;global.m[14]=rand(rng)*10000;
            for(auto& m:palette)m=global;
            // Supply varied native articulation, not only the bind pose.
            Check(Apply(palette.data(),count,d,float(trial%7)/6),"native seed");
            for(auto& m:palette){m.m[3]=7;m.m[7]=8;m.m[11]=9;m.m[15]=10;}
            auto original=palette;
            Check(Apply(palette.data(),count,d,0),"zero accepted");Check(!memcmp(palette.data(),original.data(),sizeof(palette)),"zero byte identity");
            float amount=float(trial%11)/10;
            Check(Apply(palette.data(),count,d,amount),"live pose blend");
            for(unsigned i=0;i<count;i++){
                if(i<131||i>145)Check(!memcmp(&palette[i],&original[i],sizeof(Matrix)),"nonfinger identity");
                for(unsigned j:{3u,7u,11u,15u})Check(palette[i].m[j]==original[i].m[j],"metadata preserved");
            }
            for(unsigned i=1;i<16;i++){
                unsigned parent=(i-1)%3==0?0:i-1;
                auto local=[&](auto& a){return CameraMath::Multiply(CameraMath::Multiply(Translation(d.pivots[i]),Affine(a[130+i])),CameraMath::InverseRigid(CameraMath::Multiply(Translation(d.pivots[parent]),Affine(a[130+parent]))));};
                auto a=local(original),b=local(palette);
                for(int j=0;j<3;j++)Check(std::abs(a.m[12+j]-b.m[12+j])<.015f,"native joint translation preserved");
                Check(NativeHandWeapon::Rigid(Affine(palette[130+i])),"rigid output");
                if(amount==1){auto target=CameraMath::Rotation(d.target[i-1]);for(int r=0;r<3;r++)for(int c=0;c<3;c++)Check(std::abs(b.m[r*4+c]-target.m[r*4+c])<.001f,"full authored local rotation");}
            }
            palette=original;palette[140].m[0]=NAN;auto invalid=palette;
            Check(!Apply(palette.data(),count,d,.5f),"invalid rejected");Check(!memcmp(palette.data(),invalid.data(),sizeof(palette)),"no partial writes");
        }
    }
    for(unsigned flags=0;flags<512;flags++)Check(Eligible(true,true,flags,0)==(flags==4||flags==6),"strict state gate");
    Check(!Eligible(false,true,4,0)&&!Eligible(true,false,4,0)&&!Eligible(true,true,4,1)&&!Eligible(true,true,6,1),"unknown/pending rejected");
    Envelope e;Check(e.Update(100,1,1,true,true)==0,"start neutral");auto half=e.Update(180,2,1,true,true);Check(std::abs(half-.5f)<.001f,"half closing");Check(e.Update(181,2,1,true,true)==half,"same scene stable");Check(e.Update(260,3,1,true,true)==1,"closed");Check(std::abs(e.Update(370,4,1,true,false)-.5f)<.001f,"half releasing");Check(e.Update(480,5,1,true,false)==0,"released");e.Update(640,6,1,true,true);Check(e.Update(641,6,1,false,true)==0,"weapon drawn immediate cancel even same scene");
    e.Update(700,7,1,true,true);e.Update(860,8,1,true,true);Check(e.Update(861,9,2,true,true)==0,"identity reset");Check(e.Update(2000,10,2,true,true)==0,"stale reset");
    puts("PASS InteractionCurl: live poses, three layouts, metadata, strict eligibility, envelope");return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
