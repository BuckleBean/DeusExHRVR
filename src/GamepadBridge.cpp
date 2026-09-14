#include "GamepadBridge.h"
#include "DirectionConfig.h"
#include <Xinput.h>
#include <mutex>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
namespace GamepadBridge {
namespace {
std::mutex guard;
Transport::Header* channel{};
Transport::Tracking latest{};
using GetState=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
using SetState=DWORD(WINAPI*)(DWORD,XINPUT_VIBRATION*);
GetState originalGet{};SetState originalSet{};
bool enabled{},configured{},installed{},connected{};
DWORD packet{};XINPUT_GAMEPAD previous{};
uint64_t polls{},changes{},retryTick{};
bool Read(XINPUT_GAMEPAD& pad) {
    if(!enabled || !channel)return false;
    Transport::Tracking sample{};
    if(Transport::ReadTracking(channel,sample))latest=sample;
    auto now=GetTickCount64();
    bool fresh=latest.tick && now>=latest.tick && now-latest.tick<250;
    if(fresh && latest.gamepad.valid) {
        static_assert(sizeof(pad)==sizeof(Transport::Gamepad)-sizeof(uint32_t));
        std::memcpy(&pad,&latest.gamepad.buttons,sizeof(pad));connected=true;
    }
    // Keep the device connected after first activation, but release every
    // button/axis on focus loss, sleeping controllers or a stalled companion.
    return connected;
}
SHORT Stronger(SHORT a,SHORT b){return std::abs(int(a))>=std::abs(int(b))?a:b;}
DWORD WINAPI GetHook(DWORD index,XINPUT_STATE* state) {
    DWORD result=originalGet(index,state);
    if(index || !state)return result;
    std::lock_guard lock(guard);XINPUT_GAMEPAD pad{};
    if(!Read(pad))return result;
    ++polls;
    if(result==ERROR_SUCCESS) {
        const auto& real=state->Gamepad;
        pad.wButtons|=real.wButtons;
        pad.bLeftTrigger=std::max(pad.bLeftTrigger,real.bLeftTrigger);
        pad.bRightTrigger=std::max(pad.bRightTrigger,real.bRightTrigger);
        pad.sThumbLX=Stronger(pad.sThumbLX,real.sThumbLX);pad.sThumbLY=Stronger(pad.sThumbLY,real.sThumbLY);
        pad.sThumbRX=Stronger(pad.sThumbRX,real.sThumbRX);pad.sThumbRY=Stronger(pad.sThumbRY,real.sThumbRY);
    }
    if(std::memcmp(&pad,&previous,sizeof(pad))){previous=pad;++packet;++changes;}
    state->Gamepad=pad;state->dwPacketNumber=packet;
    return ERROR_SUCCESS;
}
DWORD WINAPI SetHook(DWORD index,XINPUT_VIBRATION* vibration) {
    DWORD result=originalSet(index,vibration);
    if(index || !vibration || result==ERROR_SUCCESS)return result;
    std::lock_guard lock(guard);XINPUT_GAMEPAD pad{};
    return Read(pad)?ERROR_SUCCESS:result; // Native rumble has no haptic mapping yet.
}
void Install() {
    if(!enabled || installed || !channel || GetTickCount64()<retryTick)return;
    retryTick=GetTickCount64()+1000;
    auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    if(nt->FileHeader.TimeDateStamp!=0x52840914 || nt->OptionalHeader.SizeOfImage!=0x1c54000)return;
    auto va=[&](uintptr_t p){return base+p-0x400000;};
    // Supported game's PCXInputProducer resolves just GetState/SetState at
    // 0x4b3a20 and polls all four slots, including disconnected devices.
    auto module=*reinterpret_cast<HMODULE*>(va(0xe514d0));if(!module)return;
    auto get=reinterpret_cast<GetState>(GetProcAddress(module,"XInputGetState"));
    auto set=reinterpret_cast<SetState>(GetProcAddress(module,"XInputSetState"));
    if(!get || !set || *reinterpret_cast<GetState*>(va(0xe514c4))!=get || *reinterpret_cast<SetState*>(va(0xe514c8))!=set)return;
    originalGet=get;originalSet=set;
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(va(0xe514c4)),reinterpret_cast<void*>(&GetHook));
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(va(0xe514c8)),reinterpret_cast<void*>(&SetHook));
    installed=true;
    FILE* f{};if(!fopen_s(&f,"DeusExHRVR-input.log","a")){fprintf(f,"Xbox input bridge installed: slot 0, motionControls=1\n");fclose(f);}
}
}
void SetChannel(Transport::Header* header) {
    std::lock_guard lock(guard);
    if(!configured) {
        wchar_t path[MAX_PATH]{};GetFullPathNameW(L"DeusExHRVR.ini",MAX_PATH,path,nullptr);
        enabled=DirectionConfig::MotionEnabled(path);configured=true;
    }
    channel=header;latest={};connected=false;
}
void OnPresent(bool capture) {
    std::lock_guard lock(guard);Install();
    if(capture) {
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-input.log","a")) {
            fprintf(f,"enabled=%d installed=%d connected=%d polls=%llu changes=%llu packet=%lu lastButtons=%04x sticks=%d,%d/%d,%d triggers=%u,%u\n",
                enabled,installed,connected,polls,changes,packet,previous.wButtons,previous.sThumbLX,previous.sThumbLY,
                previous.sThumbRX,previous.sThumbRY,previous.bLeftTrigger,previous.bRightTrigger);fclose(f);
        }
    }
}
}
