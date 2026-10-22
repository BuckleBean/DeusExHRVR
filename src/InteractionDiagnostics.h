#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cmath>

namespace InteractionDiagnostics {
struct Hit {
    uintptr_t site{},object{},descriptor{};
    uint32_t type{};float origin[3]{},point[3]{},range{},extraRangeSquared{};
    bool readable{},extended{};
};
// Native DXSense caller layout, validated at the supported executable call sites.
// Faults/invalid numeric data mark the sample unknown; never modify game memory.
inline bool ReadHit(Hit* h,const unsigned char* callerFrame,void* descriptor){
    __try {
        memcpy(h->origin,callerFrame+0x60,12);memcpy(h->point,callerFrame+0xd0,12);
        h->type=*static_cast<const uint32_t*>(descriptor);
        h->range=*reinterpret_cast<const float*>(static_cast<const unsigned char*>(descriptor)+4)*316.0500183105469f;
        h->extended=callerFrame[0x13]!=0;
        h->extraRangeSquared=*reinterpret_cast<const float*>(callerFrame+0x28);
        h->readable=std::isfinite(h->range)&&h->range>=0;
        for(unsigned i=0;i<3;++i)h->readable=h->readable&&std::isfinite(h->origin[i])&&std::isfinite(h->point[i]);
        return h->readable;
    } __except(EXCEPTION_EXECUTE_HANDLER){h->readable=false;return false;}
}
}
