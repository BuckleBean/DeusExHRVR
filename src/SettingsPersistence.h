#pragma once
#include <windows.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <array>
#include "DirectionConfig.h"
#include "ButtonBindings.h"
#include "CoverCrosshair.h"
#include "HandPointer.h"
#include "ImageAdjust.h"

// One worker owns menu-setting writes and learned snap calibration.
// Single-key profile writes preserve unrelated settings and existing comments.
class SettingsPersistence {
    std::mutex mutex;std::condition_variable wake;
    std::chrono::steady_clock::time_point saveAfter{};
    std::wstring path;bool pending{},anglePending{};float angle{};double counts{};uint32_t sequence{};int snapMode{-1},movementMode{-1},responseMode{-1},interactionMode{-1};bool resetPending{};float pendingScale{-1};int pendingDialogue{-1};float pendingZoom{-1};int pendingHandHud{-1};int pendingThrowAim{-1},pendingThrowMarker{-1},pendingCoverFollow{-1},pendingScope{-1},pendingCrosshair{-1},pendingHandPointers{-1},pendingQuickBar{-1};uint32_t pendingBrightness{},pendingContrast{};
    int pendingLeftHanded{-1},pendingSwapSticks{-1};std::array<uint32_t,8> pendingHolds{};uint32_t pendingHoldMask{};int pendingHudSize{-1};
    ButtonBindings::Layout bindings{};uint32_t bindingMask{};
public:
    std::atomic<uint32_t> completed{},failed{};
    static bool Save(const std::wstring& path,bool angle,float degrees,double count,int snap=-1,int movement=-1,const ButtonBindings::Layout& bindings={},uint32_t mask=0,int response=-1,int interaction=-1,bool reset=false,float worldScale=-1,int dialogue=-1,float scopeZoom=-1,int handHud=-1,int throwAim=-1,int throwMarker=-1,int coverFollow=-1,int immersiveScope=-1,int crosshair=-1,int quickBar=-1,uint32_t brightness=0,uint32_t contrast=0,int handPointers=-1,
                     int leftHanded=-1,int swapSticks=-1,const std::array<uint32_t,8>& holds={},uint32_t holdMask=0,int hudSize=-1) {
        if(dialogue < -1 || dialogue > 1)return false;
        if(hudSize!=-1&&(hudSize<50||hudSize>100))return false;
        if(leftHanded < -1 || leftHanded > 1 || swapSticks < -1 || swapSticks > 1 || (holdMask>>8))return false;
        for(size_t i=0;i<8;++i)if((holdMask&(1u<<i))&&holds[i]>5000)return false;
        if(throwAim < -1 || throwAim > 3)return false;
        for(int v:{throwMarker,coverFollow,immersiveScope})if(v < -1 || v > 1)return false;
        if(handHud < -1 || handHud > 1)return false;
        if(crosshair!=-1&&(crosshair<1||crosshair>4))return false;
        if(handPointers!=-1&&(handPointers<1||handPointers>4))return false;
        if(quickBar < -1 || quickBar > 1)return false;
        if(!ImageAdjust::ValidWire(brightness)||!ImageAdjust::ValidWire(contrast))return false;
        if(scopeZoom!=-1&&(!std::isfinite(scopeZoom)||scopeZoom<1||scopeZoom>12))return false;
        if(worldScale!=-1&&(!std::isfinite(worldScale)||worldScale<10||worldScale>1000))return false;
        if(interaction < -1 || interaction > 3)return false;
        if(response < -1 || response > 2 || !ButtonBindings::Valid(bindings,mask))return false;
        FILE* f{};if(_wfopen_s(&f,path.c_str(),L"rb"))return false;
        unsigned char start[4]{};auto n=fread(start,1,4,f);fclose(f);
        // Existing project INIs are BOM-free and start with [VR]. Refuse other
        // encodings instead of rewriting a user's file into a different format.
        if(n!=4||memcmp(start,"[VR]",4))return false;
        wchar_t text[40];swprintf_s(text,L"%.3f",count);
        if(!WritePrivateProfileStringW(L"VR",L"SnapTurnMouseCounts",text,path.c_str()))return false;
        if(angle){swprintf_s(text,L"%.6g",double(degrees));if(!WritePrivateProfileStringW(L"VR",L"SnapTurnDegrees",text,path.c_str()))return false;}
        if(angle&&worldScale>=0){swprintf_s(text,L"%.9g",double(worldScale));if(!WritePrivateProfileStringW(L"VR",L"WorldUnitsPerMetre",text,path.c_str()))return false;}
        if(angle&&scopeZoom>=0){swprintf_s(text,L"%.6g",double(scopeZoom));if(!WritePrivateProfileStringW(L"VR",L"ScopeMagnification",text,path.c_str()))return false;}
        if(angle&&hudSize>=0){swprintf_s(text,L"%d",hudSize);if(!WritePrivateProfileStringW(L"VR",L"HudSize",text,path.c_str()))return false;}
        if(angle&&dialogue>=0&&!WritePrivateProfileStringW(L"VR",L"FixedDialogueCamera",dialogue?L"1":L"0",path.c_str()))return false;
        if(angle&&handHud>=0&&!WritePrivateProfileStringW(L"VR",L"HandHud",handHud?L"1":L"0",path.c_str()))return false;
        if(angle&&throwAim>=0&&!WritePrivateProfileStringW(L"VR",L"ThrowAim",DirectionConfig::Value(unsigned(throwAim)),path.c_str()))return false;
        if(angle&&throwMarker>=0&&!WritePrivateProfileStringW(L"VR",L"ThrowMarker",throwMarker?L"1":L"0",path.c_str()))return false;
        if(angle&&coverFollow>=0&&!WritePrivateProfileStringW(L"VR",L"CoverCameraFollow",coverFollow?L"1":L"0",path.c_str()))return false;
        if(angle&&immersiveScope>=0&&!WritePrivateProfileStringW(L"VR",L"ImmersiveScope",immersiveScope?L"1":L"0",path.c_str()))return false;
        if(angle&&crosshair>=1&&!WritePrivateProfileStringW(L"VR",L"Crosshair",CoverCrosshair::Value(uint32_t(crosshair)),path.c_str()))return false;
        if(angle&&handPointers>=1&&!WritePrivateProfileStringW(L"VR",L"HandPointers",HandPointer::Value(uint32_t(handPointers)),path.c_str()))return false;
        if(angle&&quickBar>=0&&!WritePrivateProfileStringW(L"VR",L"QuickBarAutoHide",quickBar?L"1":L"0",path.c_str()))return false;
        if(angle&&brightness&&!ImageAdjust::Write(L"Brightness",brightness,path.c_str()))return false;
        if(angle&&contrast&&!ImageAdjust::Write(L"Contrast",contrast,path.c_str()))return false;
        if(angle&&snap>=0&&!WritePrivateProfileStringW(L"VR",L"SnapTurn",snap?L"1":L"0",path.c_str()))return false;
        if(angle&&response>=0&&!WritePrivateProfileStringW(L"VR",L"MovementResponse",response==2?L"2":response==1?L"1":L"0",path.c_str()))return false;
        if(angle&&interaction>=0&&!WritePrivateProfileStringW(L"VR",L"InteractionAim",DirectionConfig::Value(unsigned(interaction)),path.c_str()))return false;
        if(angle&&reset)for(size_t i=0;i<8;++i){wchar_t key[48];swprintf_s(key,L"%lsHoldMs",ButtonBindings::Keys[i]);if(!WritePrivateProfileStringW(L"Buttons",key,L"0",path.c_str()))return false;}
        if(angle)for(size_t i=0;i<8;++i)if(holdMask&(1u<<i)){wchar_t key[48],value[16];swprintf_s(key,L"%lsHoldMs",ButtonBindings::Keys[i]);swprintf_s(value,L"%u",holds[i]);if(!WritePrivateProfileStringW(L"Buttons",key,value,path.c_str()))return false;}
        if(angle&&leftHanded>=0&&!WritePrivateProfileStringW(L"VR",L"LeftHanded",leftHanded?L"1":L"0",path.c_str()))return false;
        if(angle&&swapSticks>=0&&!WritePrivateProfileStringW(L"VR",L"SwapSticks",swapSticks?L"1":L"0",path.c_str()))return false;
        if(angle&&movement>=0&&!WritePrivateProfileStringW(L"VR",L"MovementDirection",DirectionConfig::Value(unsigned(movement)),path.c_str()))return false;
        if(angle)for(size_t i=0;i<ButtonBindings::Count;++i)if(mask&(1u<<i)){
            if(!WritePrivateProfileStringW(L"Buttons",ButtonBindings::Keys[i],ButtonBindings::Find(bindings[i])->key,path.c_str()))return false;
        }
        WritePrivateProfileStringW(nullptr,nullptr,nullptr,path.c_str());
        if(angle)for(size_t i=0;i<ButtonBindings::Count;++i)if(mask&(1u<<i)){
            GetPrivateProfileStringW(L"Buttons",ButtonBindings::Keys[i],L"",text,40,path.c_str());
            if(wcscmp(text,ButtonBindings::Find(bindings[i])->key))return false;
        }
        if(angle&&worldScale>=0){GetPrivateProfileStringW(L"VR",L"WorldUnitsPerMetre",L"",text,40,path.c_str());if(std::abs(_wtof(text)-worldScale)>.001)return false;}
        if(angle&&scopeZoom>=0){GetPrivateProfileStringW(L"VR",L"ScopeMagnification",L"",text,40,path.c_str());if(std::abs(_wtof(text)-scopeZoom)>.001)return false;}
        if(angle&&interaction>=0&&unsigned(DirectionConfig::Read(path.c_str(),L"InteractionAim"))!=unsigned(interaction))return false;
        if(angle&&reset)for(size_t i=0;i<8;++i){wchar_t key[48];swprintf_s(key,L"%lsHoldMs",ButtonBindings::Keys[i]);if(GetPrivateProfileIntW(L"Buttons",key,1,path.c_str())!=0)return false;}
        if(angle)for(size_t i=0;i<8;++i)if(holdMask&(1u<<i)){wchar_t key[48];swprintf_s(key,L"%lsHoldMs",ButtonBindings::Keys[i]);if(GetPrivateProfileIntW(L"Buttons",key,9999,path.c_str())!=holds[i])return false;}
        if(angle&&leftHanded>=0&&GetPrivateProfileIntW(L"VR",L"LeftHanded",2,path.c_str())!=unsigned(leftHanded))return false;
        if(angle&&swapSticks>=0&&GetPrivateProfileIntW(L"VR",L"SwapSticks",2,path.c_str())!=unsigned(swapSticks))return false;
        if(angle&&response>=0&&GetPrivateProfileIntW(L"VR",L"MovementResponse",3,path.c_str())!=unsigned(response))return false;
        if(angle&&movement>=0&&unsigned(DirectionConfig::Read(path.c_str(),L"MovementDirection"))!=unsigned(movement))return false;
        if(angle&&hudSize>=0&&GetPrivateProfileIntW(L"VR",L"HudSize",0,path.c_str())!=unsigned(hudSize))return false;
        if(angle&&dialogue>=0&&GetPrivateProfileIntW(L"VR",L"FixedDialogueCamera",2,path.c_str())!=unsigned(dialogue))return false;
        if(angle&&handHud>=0&&GetPrivateProfileIntW(L"VR",L"HandHud",2,path.c_str())!=unsigned(handHud))return false;
        if(angle&&throwAim>=0&&unsigned(DirectionConfig::Read(path.c_str(),L"ThrowAim",DirectionConfig::Source::Controller))!=unsigned(throwAim))return false;
        if(angle&&throwMarker>=0&&GetPrivateProfileIntW(L"VR",L"ThrowMarker",2,path.c_str())!=unsigned(throwMarker))return false;
        if(angle&&coverFollow>=0&&GetPrivateProfileIntW(L"VR",L"CoverCameraFollow",2,path.c_str())!=unsigned(coverFollow))return false;
        if(angle&&immersiveScope>=0&&GetPrivateProfileIntW(L"VR",L"ImmersiveScope",2,path.c_str())!=unsigned(immersiveScope))return false;
        if(angle&&crosshair>=1){GetPrivateProfileStringW(L"VR",L"Crosshair",L"",text,40,path.c_str());if(CoverCrosshair::Parse(text)!=uint32_t(crosshair))return false;}
        if(angle&&handPointers>=1){GetPrivateProfileStringW(L"VR",L"HandPointers",L"",text,40,path.c_str());if(HandPointer::Parse(text)!=uint32_t(handPointers))return false;}
        if(angle&&quickBar>=0&&GetPrivateProfileIntW(L"VR",L"QuickBarAutoHide",2,path.c_str())!=unsigned(quickBar))return false;
        if(angle&&snap>=0&&GetPrivateProfileIntW(L"VR",L"SnapTurn",2,path.c_str())!=unsigned(snap))return false;
        if(angle){GetPrivateProfileStringW(L"VR",L"SnapTurnDegrees",L"",text,40,path.c_str());if(std::abs(_wtof(text)-degrees)>.01)return false;}
        GetPrivateProfileStringW(L"VR",L"SnapTurnMouseCounts",L"",text,40,path.c_str());
        if(std::abs(_wtof(text)-count)>.01)return false;
        return true;
    }
    SettingsPersistence() {
        std::thread([this]{for(;;){
            std::unique_lock lock(mutex);wake.wait(lock,[&]{return pending;});
            // Coalesce rapid adjustments without holding the producer lock during IO.
            while(std::chrono::steady_clock::now()<saveAfter){auto deadline=saveAfter;wake.wait_until(lock,deadline);}
            auto file=path;auto a=angle;auto c=counts;auto seq=sequence;bool saveAngle=anglePending;auto snap=snapMode;auto movement=movementMode;auto response=responseMode;auto interaction=interactionMode;auto scale=pendingScale;auto dialogue=pendingDialogue;auto zoom=pendingZoom;auto handHud=pendingHandHud;auto throwAimMode=pendingThrowAim;auto marker=pendingThrowMarker;auto follow=pendingCoverFollow;auto scope=pendingScope;auto crosshair=pendingCrosshair;auto pointers=pendingHandPointers;auto quickBarMode=pendingQuickBar;auto brightnessWire=pendingBrightness;auto contrastWire=pendingContrast;auto reset=resetPending;resetPending=false;
            auto leftHandedMode=pendingLeftHanded;auto swapMode=pendingSwapSticks;auto holds=pendingHolds;auto holdMask=pendingHoldMask;pendingHoldMask=0;auto hudSizeValue=pendingHudSize;
            auto layout=bindings;auto mask=bindingMask;bindingMask=0;
            pending=anglePending=false;lock.unlock();
            bool ok=Save(file,saveAngle,a,c,snap,movement,layout,mask,response,interaction,reset,scale,dialogue,zoom,handHud,throwAimMode,marker,follow,scope,crosshair,quickBarMode,brightnessWire,contrastWire,pointers,leftHandedMode,swapMode,holds,holdMask,hudSizeValue);
            if(saveAngle){if(!ok)failed=seq;completed=seq;}
        }}).detach();
    }
    void Queue(const wchar_t* file,double count,uint32_t seq=0,float degrees=0,int snap=-1,int movement=-1,const ButtonBindings::Layout& layout={},uint32_t mask=0,int response=-1,int interaction=-1,bool reset=false,float worldScale=-1,int dialogue=-1,float scopeZoom=-1,int handHud=-1,int throwAim=-1,int throwMarker=-1,int coverFollow=-1,int immersiveScope=-1,int crosshair=-1,int quickBar=-1,uint32_t brightness=0,uint32_t contrast=0,int handPointers=-1,
               int leftHanded=-1,int swapSticks=-1,const std::array<uint32_t,8>& holds={},uint32_t holdMask=0,int hudSize=-1) {
        std::lock_guard lock(mutex);path=file;counts=count;pending=true;
        if(seq){pendingHudSize=hudSize;pendingLeftHanded=leftHanded;pendingSwapSticks=swapSticks;for(size_t i=0;i<8;++i)if(holdMask&(1u<<i))pendingHolds[i]=holds[i];pendingHoldMask|=holdMask;}
        if(seq){sequence=seq;angle=degrees;anglePending=true;snapMode=snap;movementMode=movement;responseMode=response;interactionMode=interaction;resetPending|=reset;pendingScale=worldScale;pendingDialogue=dialogue;pendingZoom=scopeZoom;pendingHandHud=handHud;pendingThrowAim=throwAim;pendingThrowMarker=throwMarker;pendingCoverFollow=coverFollow;pendingScope=immersiveScope;pendingCrosshair=crosshair;pendingHandPointers=handPointers;pendingQuickBar=quickBar;pendingBrightness=brightness;pendingContrast=contrast;
            for(size_t i=0;i<ButtonBindings::Count;++i)if(mask&(1u<<i))bindings[i]=layout[i];bindingMask|=mask;}
        saveAfter=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
        wake.notify_one();
    }
};
inline SettingsPersistence& SettingsWriter(){static auto* writer=new SettingsPersistence;return *writer;}
