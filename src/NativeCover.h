#pragma once
#include <cstdint>
namespace NativeCover {
// Exact PlayerCamera ownership and current mode from the supported executable:
// the mode stack's current entry must be the PlayerCamera subobject at
// `offset` with exactly `vtable` (preferred VA). No inference from the broader
// hands-suspension mask or camera distance.
template<class Read> bool ModeAt(uintptr_t base,uintptr_t manager,uintptr_t owner,Read read,uint32_t offset,uintptr_t vtablePreferred) {
    if(!base||!manager||!owner)return false;
    uint32_t active{},player{},modeObject{},vtable{};uint16_t index{};
    const uintptr_t pc=manager+0x6f0;
    return read(manager+0x30,&active,4)&&active==pc&&
        read(pc+0xaa0,&player,4)&&player==owner&&
        read(pc+0x774,&index,2)&&index<19&&
        read(pc+0x778+index*0x24+8,&modeObject,4)&&modeObject==pc+offset&&
        read(modeObject,&vtable,4)&&vtable==base+vtablePreferred-0x400000;
}
// Cover (PlayerCamera +0x144, vtable 0xAA79AC).
template<class Read> bool Mode(uintptr_t base,uintptr_t manager,uintptr_t owner,Read read) {
    return ModeAt(base,manager,owner,read,0x144,0xaa79ac);
}
// IronSight / scope (PlayerCamera +0x640, vtable 0xAA7A74).
template<class Read> bool IronSight(uintptr_t base,uintptr_t manager,uintptr_t owner,Read read) {
    return ModeAt(base,manager,owner,read,0x640,0xaa7a74);
}
}
