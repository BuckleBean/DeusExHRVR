#pragma once
#include <cstdint>
#include <mutex>
#include <unordered_set>

// Supported DXHRDC 2.0.66.0 layouts. Video objects are registered after their
// constructor and removed before destruction, so status reads never outlive them.
class ScreenMode {
    std::mutex mutex;
    std::unordered_set<const unsigned char*> players;
public:
    static bool GameOver(uintptr_t base) {
        // NsDeathMenuMovieController::activate (0x7e0800) installs this
        // pointer; deactivate (0x7e0550) clears it before disposal.
        auto p=*reinterpret_cast<const uintptr_t*>(base+0x1cace84-0x400000);
        return p && *reinterpret_cast<const uintptr_t*>(p)==base+0xaba11c-0x400000;
    }
    static bool Scope(uintptr_t base,const unsigned char* manager) {
        // IronSight enter/leave set/clear this flag. Ordinary iron sights
        // share it, so also require the equipped weapon's native scope bit.
        if(!*reinterpret_cast<const unsigned char*>(base+0x1c8fe8d-0x400000))return false;
        auto sight=manager+0x6f0+0x640;
        if(*reinterpret_cast<const uintptr_t*>(sight)!=base+0xaa7a74-0x400000)return false;
        auto entity=*reinterpret_cast<void* const*>(sight+4);if(!entity)return false;
        using Equipped=unsigned char*(__cdecl*)(void*);
        auto holder=reinterpret_cast<Equipped>(base+0x66af40-0x400000)(entity);
        auto weapon=holder?*reinterpret_cast<unsigned char**>(holder+0x14):nullptr;
        auto data=weapon?*reinterpret_cast<unsigned char**>(weapon+0x7c):nullptr;
        // IsScope (0x74d6e0), used to select IronSight_Scope at 0x699559.
        return data && data[0x425]!=0;
    }
    void Add(void* p) {std::lock_guard lock(mutex);players.insert(static_cast<unsigned char*>(p));}
    void Remove(void* p) {std::lock_guard lock(mutex);players.erase(static_cast<unsigned char*>(p));}
    bool Video() {
        std::lock_guard lock(mutex);
        for(auto p:players) {
            // Native playback states: opening/ready/starting/playing/seeking;
            // zero is uninitialized, six and above are stopped/finished/errors.
            auto status=*reinterpret_cast<const uint32_t*>(p+4);
            if(status>=1 && status<=5)return true;
        }
        return false;
    }
    static bool Menu(uintptr_t base) {
        auto manager=reinterpret_cast<const unsigned char*>(base+0xe126e4-0x400000);
        if(*reinterpret_cast<const uintptr_t*>(manager)!=base+0xabfcb0-0x400000)return false;
        auto isScreenMenu=[base](uintptr_t p){
            if(!p)return false;
            auto type=*reinterpret_cast<const uintptr_t*>(p);
            return type==base+0xabfeac-0x400000 || type==base+0xabfe14-0x400000; // TitleMenu / PauseMenu
        };
        auto count=*reinterpret_cast<const int*>(manager+0x14);
        if(count<0 || count>4)return false;
        for(int i=0;i<count;i++)if(isScreenMenu(*reinterpret_cast<const uintptr_t*>(manager+4+i*4)))return true;
        return isScreenMenu(*reinterpret_cast<const uintptr_t*>(manager+0x18));
    }
};
