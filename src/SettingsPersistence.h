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
#include "DirectionConfig.h"
#include "ButtonBindings.h"

// One worker owns menu-setting writes and learned snap calibration.
// Single-key profile writes preserve unrelated settings and existing comments.
class SettingsPersistence {
    std::mutex mutex;std::condition_variable wake;
    std::chrono::steady_clock::time_point saveAfter{};
    std::wstring path;bool pending{},anglePending{};float angle{};double counts{};uint32_t sequence{};int snapMode{-1},movementMode{-1},responseMode{-1},interactionMode{-1};bool resetPending{};
    ButtonBindings::Layout bindings{};uint32_t bindingMask{};
public:
    std::atomic<uint32_t> completed{},failed{};
    static bool Save(const std::wstring& path,bool angle,float degrees,double count,int snap=-1,int movement=-1,const ButtonBindings::Layout& bindings={},uint32_t mask=0,int response=-1,int interaction=-1,bool reset=false) {
        if(interaction < -1 || interaction > 3)return false;
        if(response < -1 || response > 1 || !ButtonBindings::Valid(bindings,mask))return false;
        FILE* f{};if(_wfopen_s(&f,path.c_str(),L"rb"))return false;
        unsigned char start[4]{};auto n=fread(start,1,4,f);fclose(f);
        // Existing project INIs are BOM-free and start with [VR]. Refuse other
        // encodings instead of rewriting a user's file into a different format.
        if(n!=4||memcmp(start,"[VR]",4))return false;
        wchar_t text[40];swprintf_s(text,L"%.3f",count);
        if(!WritePrivateProfileStringW(L"VR",L"SnapTurnMouseCounts",text,path.c_str()))return false;
        if(angle){swprintf_s(text,L"%.6g",double(degrees));if(!WritePrivateProfileStringW(L"VR",L"SnapTurnDegrees",text,path.c_str()))return false;}
        if(angle&&snap>=0&&!WritePrivateProfileStringW(L"VR",L"SnapTurn",snap?L"1":L"0",path.c_str()))return false;
        if(angle&&response>=0&&!WritePrivateProfileStringW(L"VR",L"MovementResponse",response?L"1":L"0",path.c_str()))return false;
        if(angle&&interaction>=0&&!WritePrivateProfileStringW(L"VR",L"InteractionAim",DirectionConfig::Value(unsigned(interaction)),path.c_str()))return false;
        if(angle&&reset)for(size_t i=0;i<8;++i){wchar_t key[48];swprintf_s(key,L"%lsHoldMs",ButtonBindings::Keys[i]);if(!WritePrivateProfileStringW(L"Buttons",key,L"0",path.c_str()))return false;}
        if(angle&&movement>=0&&!WritePrivateProfileStringW(L"VR",L"MovementDirection",DirectionConfig::Value(unsigned(movement)),path.c_str()))return false;
        if(angle)for(size_t i=0;i<ButtonBindings::Count;++i)if(mask&(1u<<i)){
            if(!WritePrivateProfileStringW(L"Buttons",ButtonBindings::Keys[i],ButtonBindings::Find(bindings[i])->key,path.c_str()))return false;
        }
        WritePrivateProfileStringW(nullptr,nullptr,nullptr,path.c_str());
        if(angle)for(size_t i=0;i<ButtonBindings::Count;++i)if(mask&(1u<<i)){
            GetPrivateProfileStringW(L"Buttons",ButtonBindings::Keys[i],L"",text,40,path.c_str());
            if(wcscmp(text,ButtonBindings::Find(bindings[i])->key))return false;
        }
        if(angle&&interaction>=0&&unsigned(DirectionConfig::Read(path.c_str(),L"InteractionAim"))!=unsigned(interaction))return false;
        if(angle&&reset)for(size_t i=0;i<8;++i){wchar_t key[48];swprintf_s(key,L"%lsHoldMs",ButtonBindings::Keys[i]);if(GetPrivateProfileIntW(L"Buttons",key,1,path.c_str())!=0)return false;}
        if(angle&&response>=0&&GetPrivateProfileIntW(L"VR",L"MovementResponse",2,path.c_str())!=unsigned(response))return false;
        if(angle&&movement>=0&&unsigned(DirectionConfig::Read(path.c_str(),L"MovementDirection"))!=unsigned(movement))return false;
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
            auto file=path;auto a=angle;auto c=counts;auto seq=sequence;bool saveAngle=anglePending;auto snap=snapMode;auto movement=movementMode;auto response=responseMode;auto interaction=interactionMode;auto reset=resetPending;resetPending=false;
            auto layout=bindings;auto mask=bindingMask;bindingMask=0;
            pending=anglePending=false;lock.unlock();
            bool ok=Save(file,saveAngle,a,c,snap,movement,layout,mask,response,interaction,reset);
            if(saveAngle){if(!ok)failed=seq;completed=seq;}
        }}).detach();
    }
    void Queue(const wchar_t* file,double count,uint32_t seq=0,float degrees=0,int snap=-1,int movement=-1,const ButtonBindings::Layout& layout={},uint32_t mask=0,int response=-1,int interaction=-1,bool reset=false) {
        std::lock_guard lock(mutex);path=file;counts=count;pending=true;
        if(seq){sequence=seq;angle=degrees;anglePending=true;snapMode=snap;movementMode=movement;responseMode=response;interactionMode=interaction;resetPending|=reset;
            for(size_t i=0;i<ButtonBindings::Count;++i)if(mask&(1u<<i))bindings[i]=layout[i];bindingMask|=mask;}
        saveAfter=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
        wake.notify_one();
    }
};
inline SettingsPersistence& SettingsWriter(){static auto* writer=new SettingsPersistence;return *writer;}
