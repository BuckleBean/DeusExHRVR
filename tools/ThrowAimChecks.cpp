// Offline checks for ThrowAim.h. Reads DXHRDC.exe (read-only) and confirms
// every link of the statically traced throw path that the hook relies on,
// then checks the small helpers. Fails loudly if any byte differs.
#include "ThrowAim.h"
#include "CarriedThrowAim.h"
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
    // 1. Hook target: SecondaryWeapon throw prologue, exactly what ThrowAim.h compares.
    {size_t o=Offset(ThrowAim::throwAddress);Check(!std::memcmp(&image[o],ThrowAim::throwBytes,sizeof(ThrowAim::throwBytes)),"0x75D560 prologue");}
    Check(U32(0xAB1A8C)!=0 && U32(0xAB1A8C-4)==0xB09900,"SecondaryWeapon vtable/locator");
    // 2. The release handler calls the throw.
    Check(CallTo(0x66F095,0x75D560),"0x66F095 calls the throw");
    // 3. The throw builds its launch frame with 0x75CB80 (the non-target branch).
    Check(CallTo(0x75DB80,0x75CB80),"0x75DB80 calls the launch frame");
    // 4. Player branch of the launch frame: CameraManager lookup, slot +0x88(1), slot +0x44.
    Check(CallTo(0x75CF37,0x69FBE0),"player branch looks up the CameraManager");
    Check(Bytes(0x75CF43,{0x8b,0x82,0x88,0x00,0x00,0x00,0x6a,0x01,0xff,0xd0}),"slot +0x88 with index 1");
    Check(Bytes(0x75CF51,{0x8b,0x42,0x44,0xff,0xd0}),"slot +0x44 on the returned camera");
    Check(CallTo(0x75CF57+2,0x45D5F0),"camera matrix copied into the launch frame");
    // Only the release position is restored afterwards (movaps [ebx+30h],xmm3).
    Check(Bytes(0x75CFA8,{0x0f,0x29,0x5b,0x30}),"release position restored from the hand");
    // 5. The camera chain: 0x69FBE0 reads the CameraManager array; slot +0x88 returns
    //    [mgr+idx*4+4]; the constructor puts mgr+0x6F0 at mgr+8; PlayerCamera +0x44 is 0x69FC90.
    Check(Bytes(0x69FC06,{0x8b,0x04,0x85,0xc0,0xce,0xc7,0x01}),"0x69FBE0 returns the CameraManager");
    Check(U32(0xAA75A4+0x88)==0x5FE880 && Bytes(0x5FE880,{0x8b,0x44,0x24,0x04,0x8b,0x44,0x81,0x04}),"CameraManager +0x88 returns [mgr+idx*4+4]");
    Check(Bytes(0x6A1543,{0x8d,0x85,0xf0,0x06,0x00,0x00,0x89,0x45,0x08}),"constructor stores mgr+0x6F0 at mgr+8");
    Check(ThrowAim::playerCameraOffset==0x6F0,"offset constant");
    Check(U32(0xAA8524+0x44)==0x69FC90,"PlayerCamera +0x44 is 0x69FC90");
    Check(Bytes(0x69FC90,{0x8d,0x41,0x40,0xc3}),"0x69FC90 body (PlayerWorldHook bytes)");
    std::printf("PASS ThrowAim static chain (12 links)\n");

    // Landing marker: every game address and constant the prediction relies on.
    {size_t o=Offset(ThrowAim::launchSetup);Check(!std::memcmp(&image[o],ThrowAim::launchSetupBytes,sizeof(ThrowAim::launchSetupBytes)),"0x75CB80 prologue");}
    {size_t o=Offset(ThrowAim::launchFrame);Check(!std::memcmp(&image[o],ThrowAim::launchFrameBytes,sizeof(ThrowAim::launchFrameBytes)),"0x763D90 prologue");}
    // The real throw calls the launch setup with bone selector 0 (push edi, edi=0 at 0x75DB64).
    Check(Bytes(0x75DB64,{0x33,0xff,0x57}),"throw passes bone selector 0");
    // The launch setup writes speed = data+0x18 * (1/30) and the player branch angle = data+0x14.
    Check(Bytes(0x75CB9F,{0xd9,0x41,0x18}) && Bytes(0x75CBA9,{0xdc,0x0d,0xf0,0xf0,0xa8,0x00}),"speed from grenade data +0x18 times 0xA8F0F0");
    {double k;std::memcpy(&k,&image[Offset(0xA8F0F0)],8);Check(std::fabs(k-1.0/30)<1e-7,"0xA8F0F0 is 1/30");}
    Check(Bytes(0x75CFB2,{0xd9,0x41,0x14,0xd9,0x1a}),"player angle from grenade data +0x14");
    // Default angle 45 and the stored candidate index at +0x84 (0x75DB85-0x75DBA4).
    {float a;std::memcpy(&a,&image[Offset(0xA898E4)],4);Check(a==45.f,"default angle 45");}
    Check(Bytes(0x75DB93,{0x8b,0x8e,0x84,0x00,0x00,0x00}),"candidate index at weapon+0x84");
    // Path stepping constants of 0x75C370.
    {float g;std::memcpy(&g,&image[Offset(0xA91FD8)],4);Check(g==ThrowAim::gravityZ,"gravity -3.44");}
    {float t0;std::memcpy(&t0,&image[Offset(0xA89CF4)],4);Check(t0==ThrowAim::firstT,"first t 5");}
    {double dt;std::memcpy(&dt,&image[Offset(0xA898E8)],8);Check(dt==ThrowAim::stepT,"t step 5");}
    {float tmax;std::memcpy(&tmax,&image[Offset(0xAA6AD8)],4);Check(tmax==ThrowAim::lastT,"last t 300");}
    {float two;std::memcpy(&two,&image[Offset(0xA89668)],4);Check(two==2.f,"gravity term halved");}
    // The collision query as 0x75C370 calls it: owner = [this+64h], &out at +0x50,
    // segment end and start, the hash, vtable +0x58 of [0x1C7BF64]; hit point at out+0x10.
    Check(Bytes(0x75C4B5,{0x8b,0x40,0x64,0x8b,0x0d,0x64,0xbf,0xc7,0x01,0x8b,0x11,0x8b,0x52,0x58}),"collision object and vtable +0x58");
    Check(Bytes(0x75C4C3,{0x50,0x8d,0x44,0x24,0x54,0x50,0x8d,0x44,0x24,0x28,0x50,0x8d,0x44,0x24,0x1c,0x50,0x68,0xa3,0x1b,0x2c,0xc7,0xff,0xd2}),"collision arguments and hash 0xC72C1BA3");
    Check(Bytes(0x75C54C,{0x0f,0x28,0x44,0x24,0x60}),"hit point read 0x10 past the result block");
    // Player and inventory lookups the mod already uses: 0x6032C0 reads [0x1A07B1C],
    // and 0x66AF40 is the inventory getter; the grenade slot is inventory+0x18 (0x816249).
    Check(Bytes(0x6032CB,{0x8b,0x0d,0x1c,0x7b,0xa0,0x01}),"player controllers at 0x1A07B1C");
    Check(Bytes(0x66AF40,{0x8b,0x44,0x24,0x04,0x50,0xe8}),"inventory getter");
    Check(Bytes(0x816249,{0x8b,0x70,0x18}),"grenade slot at inventory+0x18");
    // Remote detonate: 0x66BC70 = (inventory+0x98 != 0), and the weapon HUD shows
    // "Weapon_Detonate" only when it is true for the player's inventory.
    Check(Bytes(ThrowAim::detonateCheck,{0x33,0xc0,0x39,0x81,0x98,0x00,0x00,0x00,0x0f,0x95,0xc0,0xc3}) && ThrowAim::detonateField==0x98,"0x66BC70 tests inventory+0x98");
    Check(CallTo(0x7BA5B7,0x66AF40) && CallTo(0x7BA5C1,ThrowAim::detonateCheck) && Bytes(0x7BA5C6,{0x84,0xc0,0x74,0x1e,0x68,0xf0,0x5d,0xab,0x00}),"detonate prompt shown only when 0x66BC70 is true");
    Check(!std::memcmp(&image[Offset(0xAB5DF0)],"Weapon_Detonate",16),"prompt name Weapon_Detonate");
    Check(Bytes(0x75BF10,{0x53,0x8b,0xd9}),"grenade count prologue");
    Check(CallTo(0x75C088,0x75BF10) && Bytes(0x75C08D,{0x85,0xc0,0x0f,0x8e}),"the game's own throw check refuses a count <= 0");
    // The real throw creates the projectile through 0x765080 -> 0x7648F0 -> 0x763D90.
    Check(CallTo(0x75DD18,0x765080),"throw spawns through 0x765080");
    Check(CallTo(0x76523F,0x7648F0),"0x765080 calls 0x7648F0");
    Check(CallTo(0x7649F3,0x763D90),"0x7648F0 builds the launch frame with 0x763D90");
    std::printf("PASS ThrowAim marker links (launch setup, constants, collision query, lookups, launch frame, detonate prompt)\n");

    // Helpers.
    float a[3]{1,0,0},b[3]{0,1,0},c[3]{2,0,0},z[3]{};
    Check(std::abs(ThrowAim::RowAngleDeg(a,b)-90.f)<1e-3f,"90 degrees");
    Check(std::abs(ThrowAim::RowAngleDeg(a,c))<1e-3f,"parallel");
    Check(ThrowAim::RowAngleDeg(a,z)==-1.f,"zero vector reported");
    int camera[1];
    auto self=reinterpret_cast<uintptr_t>(camera);
    Check(ThrowAim::ManagerOf(camera)==self-0x6F0,"manager from camera");
    Check(ThrowAim::ManagerOf(reinterpret_cast<void*>(0x10))==0,"low pointer refused");
    std::printf("PASS ThrowAim helpers\n");
    Check(Bytes(0x65f7c0,{0x55,0x8b,0xec,0x83,0xe4,0xf0,0x83,0xec,0x2c}),"carried throw builder prologue");
    Check(CallTo(0x661e13,CarriedThrowAim::Address)&&CallTo(0x66210f,CarriedThrowAim::Address),"rigid object and ragdoll throw direction calls");
    Check(CarriedThrowAim::Caller(0x661e18)&&CarriedThrowAim::Caller(0x662114)&&!CarriedThrowAim::Caller(0x75d560),"only carried throws use the new direction");
    float direction[4]{1,0,0,7},forward[3]{0,3,4};
    Check(CarriedThrowAim::Direction(direction,forward)&&std::abs(direction[1]-.6f)<1e-6f&&std::abs(direction[2]-.8f)<1e-6f&&direction[3]==7,"hand aim normalized with native fourth component preserved");
    float invalid[3]{};Check(!CarriedThrowAim::Direction(direction,invalid)&&direction[1]==.6f,"invalid hand direction leaves native result unchanged");
    std::printf("PASS carried-object throw native call sites and direction math\n");
    return 0;
}
