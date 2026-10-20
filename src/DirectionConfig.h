#pragma once
#include <windows.h>
#include <cwchar>
#include "SharedPair.h"
namespace DirectionConfig {
enum class Source {Mouse,Headset,Controller,LeftController};
inline bool MotionEnabled(const wchar_t* config) {
    return GetPrivateProfileIntW(L"VR",L"MotionControls",1,config)!=0;
}
inline Source Parse(const wchar_t* value,Source fallback) {
    if(!_wcsicmp(value,L"Mouse"))return Source::Mouse;
    if(!_wcsicmp(value,L"Headset"))return Source::Headset;
    if(!_wcsicmp(value,L"Controller")||!_wcsicmp(value,L"RightController"))return Source::Controller;
    if(!_wcsicmp(value,L"LeftController"))return Source::LeftController;
    return fallback;
}
inline Source Read(const wchar_t* config,const wchar_t* key,Source fallback=Source::Mouse) {
    wchar_t value[32]{};GetPrivateProfileStringW(L"VR",key,L"",value,32,config);
    return Parse(value,fallback);
}
inline const Transport::Controller* Aim(Source source,const Transport::Tracking& t) {
    const auto* aim=source==Source::LeftController?&t.leftController:source==Source::Controller?&t.rightController:nullptr;
    return aim&&aim->valid?aim:nullptr;
}
inline const wchar_t* Value(unsigned source) {
    return source==1?L"Headset":source==2?L"Controller":source==3?L"LeftController":L"Mouse";
}
inline const char* Name(Source source) {
    return source==Source::Headset?"Headset":source==Source::Controller?"Controller":source==Source::LeftController?"LeftController":"Mouse";
}
}
