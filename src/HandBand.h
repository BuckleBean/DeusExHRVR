#pragma once
// Smooth wrist band for the hand HUD. For each owned HUD draw the mod swaps the
// game's HUD vertex shader for the matching bending one (shaders/HandBand.hlsl)
// just before the draw and puts the original back right after it. Render
// thread only, like the rest of the HUD path.
#include <d3d11.h>
#include <wrl/client.h>
#include <cstring>
#include <unordered_map>
#include "CameraMath.h"
#include "HandBandBlobs.h"
#include "HandBandConstants.h"
#include "HudIsolationCheck.h"
#include "ShaderHash.h"

namespace HandBand {
class Renderer {
public:
    void SetDevice(ID3D11Device* d) {
        if(!d || ready)return;
        const void* blobs[3]={HandBandBlobs::BandPositionOnly,HandBandBlobs::BandGeneratedUv,HandBandBlobs::BandColorUv};
        const SIZE_T sizes[3]={sizeof(HandBandBlobs::BandPositionOnly),sizeof(HandBandBlobs::BandGeneratedUv),sizeof(HandBandBlobs::BandColorUv)};
        for(int i=0;i<3;++i)if(FAILED(d->CreateVertexShader(blobs[i],sizes[i],nullptr,&shaders[i]))){
            HudIsolationCheck::Detail("hand-band shader %d FAILED to be created",i);return;}
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(Constants);desc.Usage=D3D11_USAGE_DYNAMIC;
        desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        if(FAILED(d->CreateBuffer(&desc,nullptr,&buffer))){HudIsolationCheck::Detail("hand-band buffer FAILED to be created");return;}
        d->GetImmediateContext(&context);
        ready=context!=nullptr;
    }
    bool Ready() const { return ready; }
    // From MatricesHook, for the draw about to be submitted.
    void Prepare(const Constants& c) { pending=c; hasPending=true; prepared=true; }
    void Cancel() { hasPending=false; }
    // From RenderStateHook, after the engine has bound its shaders.
    void Apply(uintptr_t engineShader) {
        if(!hasPending)return;
        hasPending=false;
        if(!ready || applied)return;
        // Engine shader objects are freed and their addresses reused when the
        // game rebuilds its UI (inference; a remembered identity could then
        // be wrong for good, surviving reloads). Remembered identities are
        // dropped on every HUD rebuild, and a failed identification is never
        // remembered.
        const unsigned rebuilds=HudIsolationCheck::hudRebuilds.load();
        if(rebuilds!=seenRebuilds){seenRebuilds=rebuilds;hashes.clear();}
        uint32_t hash=0;
        if(auto it=hashes.find(engineShader);it!=hashes.end())hash=it->second;
        else hash=ShaderHash::ForEngineShader(engineShader);
        const int variant=VariantFor(hash);
        if(variant<0) {
            ++HudIsolationCheck::bandUnknownShader;refused=true;
            if(unknownLogged<20){++unknownLogged;HudIsolationCheck::Note("hand-band FAILED to identify vertex shader crc=%08X engine-object=%p (draw hidden)",hash,reinterpret_cast<void*>(engineShader));}
            return;
        }
        hashes[engineShader]=hash;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if(FAILED(context->Map(buffer.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))){++HudIsolationCheck::bandFailed;return;}
        memcpy(mapped.pData,&pending,sizeof(pending));
        context->Unmap(buffer.Get(),0);
        context->VSGetShader(&savedShader,nullptr,nullptr);
        context->VSGetConstantBuffers(kSlot,1,&savedBuffer);
        ID3D11Buffer* ours=buffer.Get();
        context->VSSetConstantBuffers(kSlot,1,&ours);
        context->VSSetShader(shaders[variant].Get(),nullptr,0);
        applied=true;++HudIsolationCheck::bandApplied;
    }
    // From PrimitiveHook, after the draw.
    void Restore() {
        hasPending=false;
        // A draw that was given a band but whose shader was never swapped.
        if(prepared && !applied && !refused)++HudIsolationCheck::bandMissed;
        prepared=refused=false;
        if(!applied)return;
        context->VSSetShader(savedShader.Get(),nullptr,0);
        ID3D11Buffer* original=savedBuffer.Get();
        context->VSSetConstantBuffers(kSlot,1,&original);
        savedShader.Reset();savedBuffer.Reset();applied=false;
    }
private:
    bool ready{}, applied{}, hasPending{}, prepared{}, refused{};
    unsigned unknownLogged{}, seenRebuilds{};
    Constants pending{};
    Microsoft::WRL::ComPtr<ID3D11VertexShader> shaders[3];
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer, savedBuffer;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> savedShader;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    std::unordered_map<uintptr_t,uint32_t> hashes;
};
inline Renderer renderer;
}
