#pragma once
#include <array>
#include <cstdint>
#include <Xinput.h>

namespace ButtonBindings {
constexpr size_t Count=20;
using Layout=std::array<uint32_t,Count>;
inline constexpr Layout Defaults{XINPUT_GAMEPAD_A,XINPUT_GAMEPAD_Y,XINPUT_GAMEPAD_X,XINPUT_GAMEPAD_B,XINPUT_GAMEPAD_LEFT_SHOULDER,XINPUT_GAMEPAD_RIGHT_SHOULDER,XINPUT_GAMEPAD_LEFT_THUMB,XINPUT_GAMEPAD_RIGHT_THUMB,1u<<16,1u<<17};
inline constexpr const wchar_t* Keys[]={L"RightA",L"RightB",L"LeftX",L"LeftY",L"LeftGrip",L"RightGrip",L"LeftStickClick",L"RightStickClick",L"LeftTrigger",L"RightTrigger",L"RightATap",L"RightBTap",L"LeftXTap",L"LeftYTap",L"LeftGripTap",L"RightGripTap",L"LeftStickClickTap",L"RightStickClickTap",L"RightStickUp",L"RightStickDown"};
inline constexpr const wchar_t* Labels[]={L"RIGHT A",L"RIGHT B",L"LEFT X",L"LEFT Y",L"LEFT GRIP",L"RIGHT GRIP",L"LEFT STICK CLICK",L"RIGHT STICK CLICK",L"LEFT TRIGGER",L"RIGHT TRIGGER"};
struct Target {uint32_t value;const wchar_t* key;const wchar_t* label;};
inline constexpr Target Targets[]={
    // Default game actions documented in README's motion-controller table.
    {XINPUT_GAMEPAD_A,L"A",L"Jump"},{XINPUT_GAMEPAD_B,L"B",L"Takedown"},
    {XINPUT_GAMEPAD_X,L"X",L"Reload / interact"},{XINPUT_GAMEPAD_Y,L"Y",L"Holster / wheel"},
    {XINPUT_GAMEPAD_LEFT_SHOULDER,L"LB",L"Sprint"},{XINPUT_GAMEPAD_RIGHT_SHOULDER,L"RB",L"Grenade"},
    {XINPUT_GAMEPAD_LEFT_THUMB,L"LS",L"Crouch"},{XINPUT_GAMEPAD_RIGHT_THUMB,L"RS",L"Aim / scope"},
    {1u<<16,L"LT",L"Cover"},{1u<<17,L"RT",L"Fire"},
    {XINPUT_GAMEPAD_BACK,L"Back",L"Inventory / Back"},{XINPUT_GAMEPAD_START,L"Start",L"Pause / Start"},
    {XINPUT_GAMEPAD_DPAD_UP,L"DPadUp",L"Cloaking"},{XINPUT_GAMEPAD_DPAD_DOWN,L"DPadDown",L"Smart vision"},
    {XINPUT_GAMEPAD_DPAD_LEFT,L"DPadLeft",L"Move silently"},{XINPUT_GAMEPAD_DPAD_RIGHT,L"DPadRight",L"Typhoon"},
    {1u<<18,L"QuickSave",L"Quicksave"},{1u<<19,L"HandCalibrate",L"Hand calibration"},{0,L"None",L"Unassigned"}
};
inline const Target* Find(uint32_t value){for(const auto& t:Targets)if(t.value==value)return &t;return nullptr;}
inline const wchar_t* Label(uint32_t value){auto t=Find(value);return t?t->label:L"From config";}
inline uint32_t Cycle(uint32_t value,int delta){
    constexpr int n=int(sizeof(Targets)/sizeof(Targets[0]));
    for(int i=0;i<n;++i)if(Targets[i].value==value)return Targets[(i+delta+n)%n].value;
    return Targets[0].value;
}
inline uint32_t Changes(const Layout& a,const Layout& b){uint32_t mask=0;for(size_t i=0;i<Count;++i)if(a[i]!=b[i])mask|=1u<<i;return mask;}
inline bool Valid(const Layout& layout,uint32_t mask){
    if(mask>>Count)return false;
    for(size_t i=0;i<Count;++i)if((mask&(1u<<i))&&!Find(layout[i]))return false;
    return true;
}
}
