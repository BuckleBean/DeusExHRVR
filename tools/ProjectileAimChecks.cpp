// Offline checks for ProjectileAim.h. Reads DXHRDC.exe (read-only) and confirms
// every link of the statically traced player-projectile path that the rocket
// aim relies on, then checks the barrel-camera helper. Fails on any mismatch.
#include "ProjectileAim.h"
#include "ThrowAim.h"
#include "CameraMath.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::vector<unsigned char> image;
struct Section{uint32_t va,size,raw;};
std::vector<Section> sections;
void Check(bool ok,const char* why){if(!ok){std::printf("FAIL %s\n",why);std::exit(1);}}
size_t Offset(uint32_t va) {
    for(auto& s:sections)if(va>=s.va && va<s.va+s.size)return s.raw+(va-s.va);
    Check(false,"address outside the image");return 0;
}
uint32_t U32(uint32_t va){uint32_t v;std::memcpy(&v,&image[Offset(va)],4);return v;}
bool Bytes(uint32_t va,std::initializer_list<int> b) {
    size_t o=Offset(va);size_t i=0;
    for(int x:b){if(image[o+i]!=unsigned(x))return false;++i;}
    return true;
}
// E8 rel32 call at va targets 'to'
bool CallTo(uint32_t va,uint32_t to) {
    if(image[Offset(va)]!=0xE8)return false;
    int32_t rel;std::memcpy(&rel,&image[Offset(va+1)],4);
    return va+5+uint32_t(rel)==to;
}
}

int main() {
    const char* path="E:\\SteamLibrary\\steamapps\\common\\Deus Ex Human Revolution Director's Cut\\DXHRDC.exe";
    std::ifstream in(path,std::ios::binary);Check(bool(in),"cannot open DXHRDC.exe");
    image.assign(std::istreambuf_iterator<char>(in),{});
    uint32_t pe;std::memcpy(&pe,&image[0x3c],4);
    uint16_t count,optSize;std::memcpy(&count,&image[pe+6],2);std::memcpy(&optSize,&image[pe+20],2);
    uint32_t stamp;std::memcpy(&stamp,&image[pe+8],4);
    Check(stamp==0x52840914,"unexpected executable (timestamp)");
    for(int i=0;i<count;i++){
        size_t o=pe+24+optSize+i*40;Section s;uint32_t va;
        std::memcpy(&s.size,&image[o+16],4);std::memcpy(&va,&image[o+12],4);std::memcpy(&s.raw,&image[o+20],4);
        s.va=va+0x400000;sections.push_back(s);
    }
    // 1. The fire hook target: PrimaryWeapon fire, thiscall with no stack
    //    arguments (both exits are a plain ret), called from two places.
    {size_t o=Offset(ProjectileAim::fireAddress);Check(!std::memcmp(&image[o],ProjectileAim::fireBytes,sizeof(ProjectileAim::fireBytes)),"0x752D10 prologue");}
    Check(Bytes(0x754253,{0xc3}) && Bytes(0x754265,{0xc3}),"0x752D10 returns with a plain ret");
    Check(CallTo(0x672120,0x752D10) && CallTo(0x7545DF,0x752D10),"the two fire callers");
    // 2. Fire type dispatch: weapon data +0 indexes 0x75427C; type 2 is the projectile branch.
    Check(Bytes(0x752FE5,{0xff,0x24,0x85,0x7c,0x42,0x75,0x00}),"fire type jump table 0x75427C");
    Check(U32(0x75427C+2*4)==0x753A13,"fire type 2 -> 0x753A13");
    // 3. Projectile branch: muzzle from 0x74F3E0 (the mod's WeaponMuzzleHook), spawns via 0x765080/0x765330.
    Check(CallTo(0x753A24,0x74F3E0),"projectile branch takes the muzzle from 0x74F3E0");
    Check(CallTo(0x753E50,0x765080) && CallTo(0x753EC2,0x765080) && CallTo(0x753DDD,0x765330),"projectile branch spawns via 0x765080 / 0x765330");
    // Target argument: only the aimed (flag) case passes &[esp+50h]; the other path passes 0.
    Check(Bytes(0x753E02,{0xf7,0xd9,0x1b,0xc9,0x8d,0x54,0x24,0x50,0x23,0xca,0x51,0x6a,0x00}),"target pointer only when aimed");
    Check(Bytes(0x753E7D,{0x6a,0x00,0x6a,0x00}),"no target on the other spawn path");
    // 4. Plumbing to the launch frame: 0x765080/0x765330 -> 0x7648F0 -> 0x763D90, target carried as the last argument.
    Check(CallTo(0x76523F,0x7648F0) && CallTo(0x765288,0x7648F0) && CallTo(0x7654B9,0x7648F0),"spawners call 0x7648F0");
    Check(Bytes(0x765200,{0x8b,0x43,0x30}) && Bytes(0x765249,{0x8b,0x4b,0x30}),"0x765080 passes its target argument on");
    Check(CallTo(0x7649F3,0x763D90) && Bytes(0x7649D1,{0x8b,0x53,0x3c,0x52}),"0x7648F0 passes it to 0x763D90");
    // 5. The launch frame, exactly what the hook compares, and its player branch.
    {size_t o=Offset(ThrowAim::launchFrame);Check(!std::memcmp(&image[o],ThrowAim::launchFrameBytes,sizeof(ThrowAim::launchFrameBytes)),"0x763D90 prologue");}
    Check(Bytes(0x763DC1,{0x8b,0x4b,0x14}) && CallTo(0x763DC4,0x45DB80),"muzzle position from the launch matrix");
    Check(Bytes(0x763DC9,{0x8b,0x4b,0x0c,0x51}) && CallTo(0x763DCD,0x607100),"player owner test");
    Check(Bytes(0x763DE0,{0x83,0x7b,0x20,0x00,0x0f,0x85}),"camera used only with no target");
    Check(CallTo(0x763DEE,0x69FBE0),"CameraManager lookup");
    Check(Bytes(0x763DF9,{0x6a,0x01,0x8b,0x4d,0xbc,0x8b,0x11,0x8b,0x4d,0xbc,0x8b,0x82,0x88,0x00,0x00,0x00,0xff,0xd0,0x89,0x45,0xb8,
                          0x8b,0x4d,0xb8,0x8b,0x11,0x8b,0x4d,0xb8,0x8b,0x42,0x44,0xff,0xd0,0xb9,0x10,0x00,0x00,0x00,0x8b,0xf0,
                          0x8d,0xbd,0x70,0xff,0xff,0xff,0xf3,0xa5}),"slot +0x88(1), slot +0x44, matrix copied");
    Check(U32(0xAA75A4+0x88)==0x5FE880,"CameraManager +0x88");
    Check(U32(ProjectileAim::playerCameraVtable+0x44)==0x69FC90 && Bytes(0x69FC90,{0x8d,0x41,0x40,0xc3}),"PlayerCamera +0x44 is 0x69FC90");
    Check(CallTo(0x763E37,0x45DB60) && Bytes(0x45DB69,{0x0f,0x28,0x41,0x20}),"camera forward is row 2");
    Check(CallTo(0x763F0F,0x45DB80) && Bytes(0x45DB89,{0x0f,0x28,0x41,0x30}),"camera position is row 3");
    Check(Bytes(0x763F14,{0xd9,0x05,0xf0,0x6d,0xaa,0x00}),"ray length constant");
    {float k;std::memcpy(&k,&image[Offset(0xAA6DF0)],4);Check(std::fabs(k-316050.f)<1.f,"ray length 316050");}
    Check(Bytes(0x763FD9,{0x68,0xa3,0x1b,0x2c,0xc7,0x8b,0x0d,0x64,0xbf,0xc7,0x01,0x8b,0x11,0x8b,0x0d,0x64,0xbf,0xc7,0x01,0x8b,0x42,0x54,0xff,0xd0}),"collision query slot +0x54");
    Check(Bytes(0x764028,{0x8d,0x45,0xc0,0x50,0x8d,0x8d,0x10,0xff,0xff,0xff,0x51}) && CallTo(0x76403A,0x45D6E0),"direction = ray end - muzzle");
    Check(Bytes(0x45D6E6,{0x8b,0x4d,0x0c,0x8b,0x55,0x10,0x0f,0x28,0x01,0x0f,0x28,0x0a,0x8b,0x45,0x08,0x0f,0x5c,0xc1}),"0x45D6E0 is a - b");
    // 6. Barrel convention: the hit-scan branch fires along -row 1 of the same muzzle matrix.
    Check(Bytes(0x753134,{0xf3,0x0f,0x10,0x05,0xb8,0x96,0xa8,0x00,0x0f,0x28,0x8c,0x24,0x70,0x01,0x00,0x00}),"hit-scan uses -1 * muzzle row 1");
    {float k;std::memcpy(&k,&image[Offset(0xA896B8)],4);Check(k==-1.f,"0xA896B8 is -1");}
    std::printf("PASS ProjectileAim static chain (fire hook, fire type 2, spawn plumbing, launch frame camera query, barrel axis)\n");

    // Helpers: the barrel camera equals CameraMath::FiringFromMuzzle and keeps the position.
    CameraMath::Matrix muzzle{};
    const float yaw=0.7f,pitch=-0.3f;
    // Rotation rows (right-handed), any orthonormal frame will do.
    float r0[3]{std::cos(yaw),std::sin(yaw),0},r1[3]{-std::sin(yaw)*std::cos(pitch),std::cos(yaw)*std::cos(pitch),std::sin(pitch)};
    float r2[3]{r0[1]*r1[2]-r0[2]*r1[1],r0[2]*r1[0]-r0[0]*r1[2],r0[0]*r1[1]-r0[1]*r1[0]};
    for(int j=0;j<3;j++){muzzle.m[j]=r0[j];muzzle.m[4+j]=r1[j];muzzle.m[8+j]=r2[j];}
    muzzle.m[12]=10;muzzle.m[13]=-20;muzzle.m[14]=30;muzzle.m[15]=1;
    float camera[16];ProjectileAim::CameraFromMuzzle(muzzle.m,camera);
    auto firing=CameraMath::FiringFromMuzzle(muzzle);
    for(int i=0;i<16;i++)Check(std::fabs(camera[i]-firing.m[i])<1e-6f,"camera matches FiringFromMuzzle");
    const float barrel[3]{-r1[0],-r1[1],-r1[2]};
    Check(ProjectileAim::AngleDeg(camera+8,barrel)<1e-3f,"camera forward is the barrel");
    Check(camera[12]==10 && camera[13]==-20 && camera[14]==30,"camera at the muzzle");
    // Right x down = forward, as the native camera (row0 x row1 = row2).
    float cr[3]{camera[1]*camera[6]-camera[2]*camera[5],camera[2]*camera[4]-camera[0]*camera[6],camera[0]*camera[5]-camera[1]*camera[4]};
    Check(ProjectileAim::AngleDeg(cr,camera+8)<1e-3f,"camera rows keep the native handedness");
    float a[3]{1,0,0},b[3]{0,1,0},z[3]{};
    Check(std::fabs(ProjectileAim::AngleDeg(a,b)-90.f)<1e-3f && ProjectileAim::AngleDeg(a,z)==-1.f,"angle helper");
    Check(!std::strcmp(ProjectileAim::ReasonName(ProjectileAim::Applied),"barrel") && !std::strcmp(ProjectileAim::ReasonName(99),"?"),"reason names");
    std::printf("PASS ProjectileAim helpers\n");
    return 0;
}
