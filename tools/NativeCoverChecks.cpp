#include "NativeCover.h"
#include <cassert>
#include <cstring>
#include <unordered_map>
#include <cstdio>
int main(){
    std::unordered_map<uintptr_t,unsigned char> bytes;
    auto write=[&](uintptr_t a,uint32_t v,unsigned n=4){for(unsigned i=0;i<n;i++)bytes[a+i]=static_cast<unsigned char>(v>>(8*i));};
    auto read=[&](uintptr_t a,void* dst,size_t n){for(size_t i=0;i<n;i++)if(!bytes.count(a+i))return false;
        for(size_t i=0;i<n;i++)static_cast<unsigned char*>(dst)[i]=bytes[a+i];return true;};
    constexpr uintptr_t base=0x500000,manager=0x1000000,pc=manager+0x6f0,owner=0x2000000;
    write(manager+0x30,pc);write(pc+0xaa0,owner);write(pc+0x774,3,2);
    write(pc+0x778+3*0x24+8,pc+0x144);write(pc+0x144,base+0xaa79ac-0x400000);
    auto cover=[&](){return NativeCover::Mode(base,manager,owner,read);};
    assert(cover());assert(!NativeCover::Mode(base,manager,owner+4,read));
    write(manager+0x30,pc+0x340);assert(!cover());write(manager+0x30,pc);
    for(uint32_t offset:{0xc0u,0x3a8u,0x330u,0x640u,0x230u}){
        write(pc+0x778+3*0x24+8,pc+offset);assert(!cover());
    }
    write(pc+0x778+3*0x24+8,pc+0x144);
    write(pc+0x144,base+0xaa7918-0x400000);assert(!cover());
    write(pc+0x144,base+0xaa79ac-0x400000);write(pc+0x774,19,2);assert(!cover());
    write(pc+0x774,3,2);assert(cover());bytes.erase(pc+0x144);assert(!cover());
    assert(!NativeCover::Mode(base,0,owner,read));
    // IronSight: its own subobject and vtable only; Cover is not IronSight and back.
    assert(!NativeCover::IronSight(base,manager,owner,read));
    write(pc+0x778+3*0x24+8,pc+0x640);write(pc+0x640,base+0xaa7a74-0x400000);
    assert(NativeCover::IronSight(base,manager,owner,read)&&!cover());
    write(pc+0x640,base+0xaa79ac-0x400000);assert(!NativeCover::IronSight(base,manager,owner,read));
    write(pc+0x640,base+0xaa7a74-0x400000);assert(!NativeCover::IronSight(base,manager,owner+4,read));
    puts("PASS native scope mode: IronSight subobject/vtable recognised, distinct from Cover, owner checked");
    puts("PASS native cover: owner, current camera, exact Cover pointer/vtable, stack bounds and failed reads; other modes remain excluded");
}
