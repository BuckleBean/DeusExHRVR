#include "NativeHandWeapon.h"
#include <fstream>
#include <random>
#include <stdexcept>
#include <cstdio>
#include <map>
#include <cstring>
using namespace NativeHandWeapon;
void Check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
Matrix Identity(){Matrix m{};m.m[0]=m.m[5]=m.m[10]=m.m[15]=1;return m;}
int main(int argc,char** argv){try{
    Check(argc==2,"skeleton fixture required");std::ifstream f(argv[1],std::ios::binary);
    std::array<Segment,223> segments{};
    for(auto& s:segments){float xyz[3];f.read(reinterpret_cast<char*>(xyz),12);f.read(reinterpret_cast<char*>(&s.parent),4);for(int j=0;j<3;j++)s.pivot[j]=xyz[j];}
    Check(bool(f),"skeleton read");Matrix frame{};V pivot{};
    Check(Frame(segments,frame,pivot),"actual model frame");
    Check(std::abs(pivot[0]+209.548951)<.001,"actual accumulated palm pivot");
    std::mt19937 rng(87370);std::uniform_real_distribution<float> d(-1,1);
    auto random=[&]{Transport::Quaternion q{d(rng),d(rng),d(rng),d(rng)};float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);q.x/=n;q.y/=n;q.z/=n;q.w/=n;auto m=CameraMath::Rotation(q);for(int j=0;j<3;j++)m.m[12+j]=d(rng)*30000;return m;};
    std::array<bool,223> selected{};for(unsigned i=129;i<=145;i++)selected[i]=true;
    for(int trial=0;trial<1000;trial++){
        auto bone=random(),target=random(),nativeGun=random();Matrix skin{},delta{};
        Check(Skin(bone,pivot,skin)&&Delta(skin,frame,target,delta),"native frame derives delta");
        auto inverseBind=Identity();for(int j=0;j<3;j++)inverseBind.m[12+j]=-float(pivot[j]);
        Check(Difference(skin,CameraMath::Multiply(inverseBind,bone))<.01,"matches native inverse-bind construction");
        auto nativePalm=CameraMath::Multiply(frame,skin);
        Check(Difference(CameraMath::Multiply(nativePalm,delta),target)<.025,"palm reaches controller at game-world distances");
        auto movedGun=CameraMath::Multiply(nativeGun,delta);
        Matrix inv{};Check(Invert(delta,inv),"delta invertible");
        Check(Difference(CameraMath::Multiply(movedGun,inv),nativeGun)<.04,"native gun placement recovered without an offset table");
        std::array<Matrix,223> palette{};for(auto& m:palette)m=random();palette[130]=skin;
        auto before=palette;
        Check(MoveWithDelta(palette.data(),223,selected,delta),"same delta moves hand");
        for(unsigned i=0;i<223;i++)if(selected[i])Check(Difference(palette[i],CameraMath::MoveSkinMatrix(before[i],delta))==0,"every hand bone uses exact shared delta");else Check(!memcmp(&palette[i],&before[i],sizeof(Matrix)),"body unchanged");
        // Gun animation changes are honored immediately, even with a stationary hand.
        auto nextGun=nativeGun;nextGun.m[12]+=7.62f;
        auto next=CameraMath::Multiply(nextGun,delta);
        double distance=0;for(int j=12;j<15;j++)distance+=double(next.m[j]-movedGun.m[j])*(next.m[j]-movedGun.m[j]);
        Check(std::abs(std::sqrt(distance)-7.62)<.02,"native one-inch animation retained without settling");
        Check(Difference(CameraMath::FiringFromMuzzle(next),CameraMath::FiringFromMuzzle(CameraMath::Multiply(nextGun,delta)))==0,"firing uses transformed native muzzle");
    }
    Matrix skin{},delta{},bad=Identity();bad.m[0]=2;
    Check(!Skin(bad,pivot,skin),"scaled bone rejected");bad=Identity();bad.m[0]=-1;
    Check(!Skin(bad,pivot,skin),"reflected bone rejected");
    // Fake memory verifies exact native model-table branches and failed reads.
    std::map<uintptr_t,unsigned char> mem;
    auto put=[&](uintptr_t at,uint32_t value){for(int i=0;i<4;i++)mem[at+i]=static_cast<unsigned char>(value>>(8*i));};
    auto read=[&](uintptr_t at,void* out,size_t size){for(size_t i=0;i<size;i++){auto it=mem.find(at+i);if(it==mem.end())return false;static_cast<unsigned char*>(out)[i]=it->second;}return true;};
    put(0x1021c,0x20000);put(0x10220,1);put(0x100b0,0x30000);put(0x2005c,0x40000);put(0x40004,0x50000);
    uint32_t model{},bones{};Check(OwnerModel(0x10000,read,model,bones)&&model==0x50000&&bones==0x30000,"direct model table");
    put(0x2005c,0);put(0x2006c,0x60000);put(0x60040,0x40000);
    Check(OwnerModel(0x10000,read,model,bones),"alternate object table");
    put(0x2006c,0);put(0x20008,0x60000);Check(OwnerModel(0x10000,read,model,bones),"base object table");
    put(0x10220,255);Check(!OwnerModel(0x10000,read,model,bones),"negative index rejected");
    put(0x10220,32);Check(!OwnerModel(0x10000,read,model,bones),"oversized index rejected");
    put(0x10220,1);mem.erase(0x40004);Check(!OwnerModel(0x10000,read,model,bones),"unreadable model rejected");
    printf("PASS: actual skeleton, 1000 shared native transforms, animation changes without learning, firing, unchanged body, invalid matrices and native model lookup branches\n");return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
