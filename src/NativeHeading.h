#pragma once
#include "CameraMath.h"
#include <cstdint>

// The input hook supplies absolute aiming yaw, before skeletal camera animation.
// Never reuse a heading from an earlier engine update or a different player.
namespace NativeHeading {
struct Sample {
    uintptr_t manager{},instance{};
    uint32_t engineFrame{};
    float yaw{};
    bool valid{};
    bool Matches(uintptr_t m,uintptr_t p,uint32_t frame) const {
        return valid && m && p && manager==m && instance==p &&
            engineFrame==frame && std::isfinite(yaw);
    }
};
inline CameraMath::Matrix Base(float yaw,const CameraMath::Matrix& position) {
    // Native absolute yaw zero faces +Y; positive yaw rotates toward -X.
    // Captured yaw 0.418125629 -> final heading 113.95687 degrees.
    const float s=std::sin(yaw),c=std::cos(yaw);
    return {{c,s,0,0, 0,0,-1,0, -s,c,0,0,
             position.m[12],position.m[13],position.m[14],1}};
}
}
