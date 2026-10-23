#pragma once
#include "LiveWorldScale.h"
#include <windows.h>
#include <cstdlib>

namespace WorldScaleConfig {
// An empty Win32 fallback goes through the same numeric validation as invalid
// input, so there is no separate string literal that can drift from Default.
inline float Read(const wchar_t* path) {
    wchar_t text[32]{};
    GetPrivateProfileStringW(L"VR",L"WorldUnitsPerMetre",L"",text,32,path);
    const float value=static_cast<float>(_wtof(text));
    return LiveWorldScale::Valid(value)?value:LiveWorldScale::Default;
}
}
