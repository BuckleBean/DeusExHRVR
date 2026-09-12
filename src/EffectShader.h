#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

// Bytecode stays owned by the engine. Recheck its pointer and DXBC checksum
// so a shader object reused during a level change cannot inherit a match.
class EffectShader {
    struct Entry { unsigned field{},offset{};uintptr_t pointer{};std::array<unsigned char,16> checksum{};uint64_t hash{}; };
    std::unordered_map<uintptr_t,Entry> cache;
    static bool Read(uintptr_t p,void* out,size_t n) {SIZE_T got{};return p&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),out,n,&got)&&got==n;}
public:
    uint64_t Identify(uintptr_t shader) {
        if(!shader)return 0;
        if(auto it=cache.find(shader);it!=cache.end()) {
            const auto& e=it->second;
            auto pointer=*reinterpret_cast<const uintptr_t*>(shader+e.field);
            if(pointer==e.pointer && !memcmp(reinterpret_cast<const void*>(pointer+e.offset+4),e.checksum.data(),16))return e.hash;
            cache.erase(it);
        }
        std::array<uint32_t,20> fields{};if(!Read(shader,fields.data(),sizeof(fields)))return 0;
        for(unsigned i=0;i<fields.size();i++)for(unsigned offset:{0u,16u}) {
            uintptr_t p=uintptr_t(fields[i])+offset;std::array<uint32_t,8> header{};
            if(!Read(p,header.data(),sizeof(header))||header[0]!=0x43425844||header[6]<32||header[6]>2*1024*1024)continue;
            std::vector<unsigned char> bytes(header[6]);if(!Read(p,bytes.data(),bytes.size()))continue;
            Entry e;e.field=i*4;e.offset=offset;e.pointer=fields[i];memcpy(e.checksum.data(),bytes.data()+4,16);
            e.hash=14695981039346656037ull;for(auto b:bytes){e.hash^=b;e.hash*=1099511628211ull;}
            cache[shader]=e;return e.hash;
        }
        return 0;
    }
    static bool UsesCentreViewMatrix(uint64_t hash) {
        // Captured projected light/shadow variants: InstanceParams[0..3]
        // are a row-vector affine transform from centre-view to effect space.
        switch(hash) {
        case 0xbd212814bce7c05bull:case 0xa606f2f5d3087a54ull:
        case 0x742494d780d985adull:case 0x3cce6194d56852bbull:
        case 0xcb940a898afb593full:return true;
        default:return false;
        }
    }
};
