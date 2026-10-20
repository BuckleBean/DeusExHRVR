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

// One worker owns all snap-setting writes, including learned calibration.
// Single-key profile writes preserve unrelated settings and existing comments.
class SettingsPersistence {
    std::mutex mutex;std::condition_variable wake;
    std::wstring path;bool pending{},anglePending{};float angle{};double counts{};uint32_t sequence{};
public:
    std::atomic<uint32_t> completed{},failed{};
    static bool Save(const std::wstring& path,bool angle,float degrees,double count) {
        FILE* f{};if(_wfopen_s(&f,path.c_str(),L"rb"))return false;
        unsigned char start[4]{};auto n=fread(start,1,4,f);fclose(f);
        // Existing project INIs are BOM-free and start with [VR]. Refuse other
        // encodings instead of rewriting a user's file into a different format.
        if(n!=4||memcmp(start,"[VR]",4))return false;
        wchar_t text[40];swprintf_s(text,L"%.3f",count);
        if(!WritePrivateProfileStringW(L"VR",L"SnapTurnMouseCounts",text,path.c_str()))return false;
        if(angle){swprintf_s(text,L"%.6g",double(degrees));if(!WritePrivateProfileStringW(L"VR",L"SnapTurnDegrees",text,path.c_str()))return false;}
        WritePrivateProfileStringW(nullptr,nullptr,nullptr,path.c_str());
        if(angle){GetPrivateProfileStringW(L"VR",L"SnapTurnDegrees",L"",text,40,path.c_str());if(std::abs(_wtof(text)-degrees)>.01)return false;}
        GetPrivateProfileStringW(L"VR",L"SnapTurnMouseCounts",L"",text,40,path.c_str());
        if(std::abs(_wtof(text)-count)>.01)return false;
        return true;
    }
    SettingsPersistence() {
        std::thread([this]{for(;;){
            std::unique_lock lock(mutex);wake.wait(lock,[&]{return pending;});
            auto file=path;auto a=angle;auto c=counts;auto seq=sequence;bool saveAngle=anglePending;
            pending=anglePending=false;lock.unlock();
            bool ok=Save(file,saveAngle,a,c);
            if(saveAngle){if(!ok)failed=seq;completed=seq;}
        }}).detach();
    }
    void Queue(const wchar_t* file,double count,uint32_t seq=0,float degrees=0) {
        std::lock_guard lock(mutex);path=file;counts=count;pending=true;
        if(seq){sequence=seq;angle=degrees;anglePending=true;}wake.notify_one();
    }
};
inline SettingsPersistence& SettingsWriter(){static auto* writer=new SettingsPersistence;return *writer;}
