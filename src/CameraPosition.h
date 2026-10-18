#pragma once
#include "CameraMath.h"

namespace CameraPosition {
// Render-only alternative to averaging the animated camera's horizontal offset.
// The caller must validate live Combat ownership. Preserve the existing 45-unit
// pull-back guard, stance-filtered Z and the headset's later positional tracking.
inline bool UseBodyXY(CameraMath::Matrix& camera,float x,float y) {
    if(!std::isfinite(x) || !std::isfinite(y) ||
       !std::isfinite(camera.m[12]) || !std::isfinite(camera.m[13]) ||
       std::hypot(camera.m[12]-x,camera.m[13]-y)>45.f)return false;
    camera.m[12]=x;camera.m[13]=y;return true;
}
}
