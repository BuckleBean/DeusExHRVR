#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>

// F8-only inspection of engine-owned shader bytecode and common constants.
// ReadProcessMemory bounds-checks the version-dependent diagnostic pointers.
class EngineShaderTrace {
    std::filesystem::path folder;
    std::unordered_map<uintptr_t,uint64_t> shaders;
    std::ofstream draws;
    unsigned count{};
    static bool Read(uintptr_t address,void* out,size_t bytes) {
        SIZE_T got{};return address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),out,bytes,&got) && got==bytes;
    }
    uint64_t Shader(uintptr_t address,const char* stage) {
        if(!address)return 0;
        if(auto it=shaders.find(address);it!=shaders.end())return it->second;
        shaders[address]=0;
        std::array<uint32_t,20> fields{};if(!Read(address,fields.data(),sizeof(fields)))return 0;
        for(auto pointer:fields)for(unsigned offset:{0u,16u}) {
            std::array<uint32_t,8> header{};uintptr_t blob=uintptr_t(pointer)+offset;
            if(!Read(blob,header.data(),sizeof(header)) || header[0]!=0x43425844 || header[6]<32 || header[6]>2*1024*1024)continue;
            std::vector<char> bytes(header[6]);if(!Read(blob,bytes.data(),bytes.size()))continue;
            uint64_t hash=14695981039346656037ull;for(unsigned char b:bytes){hash^=b;hash*=1099511628211ull;}
            char name[80];sprintf_s(name,"%s-%016llx.dxbc",stage,hash);
            std::ofstream out(folder/name,std::ios::binary);out.write(bytes.data(),bytes.size());
            shaders[address]=hash;return hash;
        }
        return 0;
    }
public:
    void Begin(uint64_t frame) {
        draws.close();shaders.clear();count=0;
        char name[180];sprintf_s(name,"DeusExHRVR-captures/shaders-%lu-%llu",GetCurrentProcessId(),frame);
        folder=name;std::error_code ec;std::filesystem::create_directories(folder,ec);if(ec)return;
        draws.open(folder/"draws.csv");draws<<"draw,eye,tracked,ps,vs,cb0,cb1,cb2,cb3,cb4,skyEnabled,skyLayer\n";
    }
    void End(){draws.close();}
    void Record(unsigned char* state,bool tracked) {
        if(!draws.is_open() || count>=8192)return;
        auto ps=Shader(*reinterpret_cast<uintptr_t*>(state+0x198),"ps");
        auto vs=Shader(*reinterpret_cast<uintptr_t*>(state+0x19c),"vs");
        draws<<count<<','<<(state[0x5ea]?0:1)<<','<<tracked<<','<<std::hex<<ps<<','<<vs<<std::dec;
        for(unsigned slot=0;slot<=4;slot++) {
            uintptr_t cb=*reinterpret_cast<uintptr_t*>(state+0x5a8+slot*4);
            std::array<uint32_t,5> info{};std::array<float,256> data{};
            if(Read(cb,info.data(),sizeof(info))){auto rows=std::min(info[3],64u);if(Read(info[2],data.data(),rows*16)){
                draws<<",\"";for(unsigned i=0;i<rows*4;i++){if(i)draws<<' ';draws<<data[i];}draws<<'\"';continue;
            }}draws<<',';
        }
        draws<<','<<unsigned(state[0x5a4])<<','<<unsigned(state[0x5a5])<<'\n';++count;
    }
};
