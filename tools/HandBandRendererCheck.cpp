// Checks the hand band's shader swap (src/HandBand.h) on WARP, November 13
// (failsafe candidate). Mark saw the HUD "revert to defaults after hacking",
// twice, while the run log showed the elements still parked. The swap
// remembered each game shader's identity by the address of the engine's shader
// object; these checks drive the real Renderer with stand-in engine shader
// objects (the layout ShaderHash::FindDxbc reads) whose DXBC blobs are forged
// to carry the CRCs of the game's three HUD vertex shaders, or another CRC.
// Also checked: the band shaders read only b3, b5 and b11, so the hiding
// matrix the game's own shader now gets (b0) cannot move them.
#define HUD_ISOLATION_TEST
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "HandBand.h"
using Microsoft::WRL::ComPtr;
static void Check(bool b,const char* s){if(!b){printf("FAIL %s\n",s);exit(1);}}

// CRC32 (ShaderHash::Crc32's polynomial): four bytes appended to `data` that
// make the whole come out as `target`.
static void Force(std::vector<uint8_t>& data,uint32_t target) {
    uint32_t table[256];
    for(uint32_t n=0;n<256;++n){uint32_t c=n;for(int k=0;k<8;++k)c=c&1?0xEDB88320u^(c>>1):c>>1;table[n]=c;}
    int top[256];for(int n=0;n<256;++n)top[table[n]>>24]=n;
    uint32_t reg=0xFFFFFFFFu;for(auto b:data)reg=table[(reg^b)&0xFF]^(reg>>8);
    uint32_t r=~target;int index[4];
    for(int k=3;k>=0;--k){const int j=top[r>>24];index[k]=j;r=(r^table[j])<<8;}
    for(int k=0;k<4;++k){const uint8_t b=uint8_t((reg^uint32_t(index[k]))&0xFF);data.push_back(b);reg=table[(reg^b)&0xFF]^(reg>>8);}
}
// A DXBC-shaped blob of `size` bytes whose CRC is `crc`.
static std::vector<uint8_t> Blob(uint32_t crc,uint8_t fill) {
    std::vector<uint8_t> b(60,fill);
    const uint32_t magic=ShaderHash::kDxbcMagic,size=64;
    memcpy(b.data(),&magic,4);memcpy(b.data()+24,&size,4);
    Force(b,crc);
    Check(ShaderHash::Crc32(b.data(),b.size())==crc,"forged blob carries the wanted CRC");
    return b;
}
struct EngineShader { uint32_t fields[20]{}; };
static void Point(EngineShader& e,const std::vector<uint8_t>& blob){e.fields[0]=uint32_t(reinterpret_cast<uintptr_t>(blob.data()));}

int main() {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0,got{};
    Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&level,1,D3D11_SDK_VERSION,&device,&got,&context)),"WARP device");

    // The band shaders bind b3, b5 and b11 only: nothing of the game's matrix.
    const void* blobs[3]={HandBandBlobs::BandPositionOnly,HandBandBlobs::BandGeneratedUv,HandBandBlobs::BandColorUv};
    const SIZE_T sizes[3]={sizeof(HandBandBlobs::BandPositionOnly),sizeof(HandBandBlobs::BandGeneratedUv),sizeof(HandBandBlobs::BandColorUv)};
    for(int i=0;i<3;++i) {
        ComPtr<ID3D11ShaderReflection> reflection;
        Check(SUCCEEDED(D3DReflect(blobs[i],sizes[i],IID_PPV_ARGS(&reflection))),"reflect band shader");
        D3D11_SHADER_DESC desc{};reflection->GetDesc(&desc);
        for(UINT k=0;k<desc.BoundResources;++k) {
            D3D11_SHADER_INPUT_BIND_DESC bind{};reflection->GetResourceBindingDesc(k,&bind);
            Check(bind.Type==D3D_SIT_CBUFFER && (bind.BindPoint==3 || bind.BindPoint==5 || bind.BindPoint==11),
                "band shader reads only b3, b5, b11 (not the game's matrix in b0)");
        }
    }

    // A stand-in for the game's bound HUD shader and its b11 buffer.
    const char* source="float4 main(float3 p:POSITION0):SV_POSITION{return float4(p,1);}";
    ComPtr<ID3DBlob> code,errors;
    Check(SUCCEEDED(D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&code,&errors)),"compile stand-in game shader");
    ComPtr<ID3D11VertexShader> game;
    Check(SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&game)),"create stand-in game shader");
    context->VSSetShader(game.Get(),nullptr,0);

    HandBand::Renderer renderer;renderer.SetDevice(device.Get());
    Check(renderer.Ready(),"renderer ready");
    auto bound=[&]{ComPtr<ID3D11VertexShader> s;context->VSGetShader(&s,nullptr,nullptr);return s;};
    const auto known=Blob(HandBand::kColorUv,0x11),other=Blob(0x12345678u,0x22),knownToo=Blob(HandBand::kPositionOnly,0x33);
    EngineShader a,b;
    const HandBand::Constants constants{};
    auto draw=[&](EngineShader& e,bool apply) {
        renderer.Prepare(constants);
        bool swapped=false;
        if(apply){renderer.Apply(reinterpret_cast<uintptr_t>(&e));swapped=bound().Get()!=game.Get();}
        renderer.Restore();
        Check(bound().Get()==game.Get(),"game shader bound again after the draw");
        return swapped;
    };

    // 1. A known HUD shader is swapped.
    Point(a,known);
    unsigned applied=HudIsolationCheck::bandApplied;
    Check(draw(a,true) && HudIsolationCheck::bandApplied==applied+1,"known HUD shader swapped for the band");
    // 2. The scenario behind "reverted to defaults": an address first holds a
    //    shader that is not the HUD's, then is reused for a HUD shader. A
    //    remembered failure would have refused that HUD shader for good
    //    (and, before this build, drawn it at its normal corner). It is swapped.
    Point(b,other);
    const unsigned unknown=HudIsolationCheck::bandUnknownShader;
    Check(!draw(b,true) && HudIsolationCheck::bandUnknownShader==unknown+1,"unknown shader refused");
    Point(b,knownToo);
    Check(draw(b,true),"address reused for a HUD shader: swapped, the refusal was not remembered");
    // 3. A remembered HUD identity is dropped when the HUD is rebuilt.
    Point(a,other);
    Check(draw(a,true),"before a rebuild the remembered identity is used (no re-read per draw)");
    ++HudIsolationCheck::hudRebuilds;
    Check(!draw(a,true),"after a HUD rebuild the shader is identified again: not the HUD's, refused");
    Point(a,known);++HudIsolationCheck::hudRebuilds;
    Check(draw(a,true),"rebuilt again with the HUD shader: swapped");
    // 4. A band prepared but never applied is counted as missed; refused and
    //    swapped draws are not.
    const unsigned missed=HudIsolationCheck::bandMissed;
    Check(!draw(a,false) && HudIsolationCheck::bandMissed==missed+1,"prepared but not swapped: counted missed");
    Point(b,other);draw(b,true);draw(a,true);
    Check(HudIsolationCheck::bandMissed==missed+1,"refused and swapped draws not counted missed");
    // 5. Nothing prepared: nothing swapped, nothing counted.
    renderer.Apply(reinterpret_cast<uintptr_t>(&a));renderer.Restore();
    Check(bound().Get()==game.Get() && HudIsolationCheck::bandMissed==missed+1,"no band, no swap");
    puts("PASS band swap on WARP: band shaders read only b3/b5/b11 (not the game's matrix); a known HUD shader is swapped and restored; an unknown one refused and the refusal not remembered, so a reused address holding a HUD shader is swapped; remembered identities dropped on each HUD rebuild; prepared-but-unswapped draws counted");
}
