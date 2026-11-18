#pragma once
// Shared by HandBand.h (the game) and tools/HandBandShaderCheck.cpp (offline).
#include <cstdint>
#include <cstring>
#include "CameraMath.h"
namespace HandBand {
// ShaderHash::ForEngineShader (CRC32) of the game's three HUD vertex shaders,
// computed from the blobs captured in DeusExHRVR-captures/ (November 12).
inline constexpr uint32_t kPositionOnly=0x44E5EEB5;  // vs 5412c91c6c582d9a
inline constexpr uint32_t kGeneratedUv=0x5227795C;   // vs 0e624d71c99a22c3
inline constexpr uint32_t kColorUv=0x77F343CC;       // vs b22b03cd660e6c41
inline constexpr UINT kSlot=11;

// Layout of cbuffer BandBuffer in HandBand.hlsl.
struct alignas(16) Constants {
    float axis[4], back[4], across[4], up[4], middle[4];
    float eyeProjection[16];
};
static_assert(sizeof(Constants)==144);
inline Constants Pack(const CameraMath::BandFrame& f) {
    Constants c{};
    auto put=[](float* d,CameraMath::Vector v,float w){d[0]=v.x;d[1]=v.y;d[2]=v.z;d[3]=w;};
    put(c.axis,f.axis,f.radius);put(c.back,f.back,f.metresX);
    put(c.across,f.across,f.metresY);put(c.up,f.up,f.middleX);
    c.middle[0]=f.middleY;c.middle[1]=f.maxTheta;c.middle[2]=f.xShift;
    memcpy(c.eyeProjection,f.eyeProjection.m,64);
    return c;
}
// Which bending shader replaces a game shader with this hash: 0, 1, 2, or -1.
inline int VariantFor(uint32_t hash) {
    return hash==kPositionOnly?0:hash==kGeneratedUv?1:hash==kColorUv?2:-1;
}

}
