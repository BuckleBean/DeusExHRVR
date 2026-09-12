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
