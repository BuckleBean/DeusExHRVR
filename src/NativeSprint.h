#pragma once
#include <cstdint>
#include <cstddef>
// Supported DXHRDC executable: 0x788070 resolves entity+58 -> +14 (character),
// 0x7804e0 resolves character+44 (action controller), and 0x7af3d0 searches
// five 0x2c-byte action slots at controller+b0. 0x6544a0 checks the signed
// type byte at descriptor+5 and its ancestor bytes at +c (count at +8).
// Sprint's 0x69d090/100/140 handlers test enter 6c, leave 6d, and active 6b.
// Read only; missing/invalid state must not authorize a two-handed grab.
namespace NativeSprint {
struct Status {bool known{},active{};};
inline bool SprintType(int8_t type){return type>=0x6b&&type<=0x6d;}
template<class Reader> Status Read(uintptr_t entity,Reader read) {
    uint32_t component{},character{},owner{},controller{},slots{};
    if(!entity||!read(entity+0x58,&component,4)||!component||
       !read(uintptr_t(component)+0x14,&character,4)||!character||
       !read(uintptr_t(character)+0x40,&owner,4)||owner!=entity||
       !read(uintptr_t(character)+0x44,&controller,4)||!controller||
       !read(uintptr_t(controller)+0x8c,&owner,4)||owner!=entity||
       !read(uintptr_t(controller)+0xb0,&slots,4)||!slots)return {};
    bool active=false;
    for(unsigned i=0;i<5;i++) {
        uint32_t descriptor{};
        if(!read(uintptr_t(slots)+i*0x2c+8,&descriptor,4))return {};
        if(!descriptor)continue;
        int8_t type{};uint32_t count{},parents{};
        if(!read(uintptr_t(descriptor)+5,&type,1))return {};
        if(SprintType(type)){active=true;continue;}
        if(!read(uintptr_t(descriptor)+8,&count,4)||count>128)return {};
        if(!count)continue;
        int8_t types[128]{};
        if(!read(uintptr_t(descriptor)+12,&parents,4)||!parents||!read(parents,types,count))return {};
        for(unsigned j=0;j<count;j++)active=active||SprintType(types[j]);
    }
    return {true,active};
}
}
