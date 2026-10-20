// Exercise the actual game-side consumer and final XInput merge, without a game.
#include "../src/GamepadBridge.cpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <limits>
namespace EngineCamera {
unsigned CurrentScreenReasons(){return 2;}
bool SnapTurnView(float&){return false;}
int QuickBarAutoHide(bool){return 1;}
bool ImmersiveScopeButton(){return false;}
void ArmHandCalibration(){}
}
XINPUT_GAMEPAD physical{};
DWORD WINAPI RealGet(DWORD,XINPUT_STATE* s){*s={};s->Gamepad=physical;return ERROR_SUCCESS;}
int wmain(int argc,wchar_t** argv) {
    assert(argc==2);std::filesystem::path dir=argv[1];std::filesystem::create_directories(dir);
    auto path=std::filesystem::absolute(dir/L"bridge-test.ini");
    {std::ofstream f(path);f<<"[VR]\nSnapTurn=1\nSnapTurnDegrees=30\nSnapTurnMouseCounts=300\n[Buttons]\nLeftX=Start\n";}
    Transport::Header h{};GamepadBridge::SetChannel(&h);
    wcscpy_s(GamepadBridge::configPath,path.c_str());GamepadBridge::LoadSnap();
    GamepadBridge::originalGet=RealGet;
    GamepadBridge::MenuTick(2);assert(h.menu.eligible&&h.menu.snapEnabled&&h.menu.degrees==30);
    auto request=[&](uint32_t id,float angle,unsigned reasons=2){
        h.menu.hostTick=GetTickCount64();h.menu.blocked=1;h.menu.request=id;h.menu.requestedDegrees=angle;GamepadBridge::MenuTick(reasons);
    };
    request(1,45);assert(h.menu.acknowledged==1&&h.menu.degrees==45&&GamepadBridge::snapCounts==450);
    request(1,90);assert(h.menu.degrees==45); // duplicate request cannot reapply
    for(int n=0;n<200&&h.menu.saved!=1;++n){Sleep(10);GamepadBridge::MenuTick(2);}
    assert(h.menu.saved==1&&h.menu.result==0);
    assert(GetPrivateProfileIntW(L"VR",L"SnapTurnDegrees",0,path.c_str())==45);
    request(2,181);assert(h.menu.result==2&&h.menu.degrees==45);
    request(3,60,0);assert(h.menu.result==2&&h.menu.degrees==45);
    h.menu.hostTick=1;h.menu.request=4;h.menu.requestedDegrees=60;
    GamepadBridge::MenuTick(2);assert(h.menu.acknowledged==3); // stale request
    // Mixed physical + virtual input is blocked, including when virtual input goes inactive.
    Transport::Tracking t{};t.tick=GetTickCount64();t.gamepad.valid=1;t.gamepad.buttons=XINPUT_GAMEPAD_A;
    Transport::WriteTracking(&h,t);h.menu.hostTick=t.tick;h.menu.blocked=1;
    physical.wButtons=XINPUT_GAMEPAD_A;XINPUT_STATE state{};
    assert(GamepadBridge::GetHook(0,&state)==ERROR_SUCCESS&&!state.Gamepad.wButtons);
    t.gamepad.valid=0;Transport::WriteTracking(&h,t);
    assert(GamepadBridge::GetHook(0,&state)==ERROR_SUCCESS&&!state.Gamepad.wButtons);
    h.menu.blocked=0;assert(GamepadBridge::GetHook(0,&state)==ERROR_SUCCESS&&!state.Gamepad.wButtons);
    physical={};GamepadBridge::GetHook(0,&state);
    physical.wButtons=XINPUT_GAMEPAD_A;GamepadBridge::GetHook(0,&state);assert(state.Gamepad.wButtons==XINPUT_GAMEPAD_A);
    // A stale host must not hold game input forever.
    h.menu.blocked=1;h.menu.hostTick=1;physical={};GamepadBridge::GetHook(0,&state);assert(!GamepadBridge::menuBlocked);
    auto missing=std::filesystem::absolute(dir/L"missing-parent"/L"not-an-ini.ini");
    wcscpy_s(GamepadBridge::configPath,missing.c_str());request(5,60);
    for(int n=0;n<200&&h.menu.result!=3;++n){Sleep(10);GamepadBridge::MenuTick(2);}
    assert(h.menu.acknowledged==5&&h.menu.result==3&&h.menu.saved!=5&&h.menu.degrees==60);
    GamepadBridge::SetChannel(nullptr);
    printf("PASS %zu-bit actual bridge: request/ack/save/error, duplicate/stale/refused edits, calibration scaling, virtual/physical input and focus recovery\n",sizeof(void*)*8);
}
