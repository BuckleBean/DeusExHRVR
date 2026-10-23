// Exercise the actual game-side consumer and final XInput merge, without a game.
#include "../src/GamepadBridge.cpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <limits>
namespace EngineCamera {
unsigned interaction{};
unsigned InteractionAim(){return interaction;}
void SetInteractionAim(unsigned value){interaction=value;}
unsigned response{};
unsigned MovementResponse(){return response;}
void SetMovementResponse(unsigned value){response=value;}
unsigned movement{};
unsigned MovementDirection(){return movement;}
void SetMovementDirection(unsigned value){movement=value;}
unsigned reasons=2;
unsigned CurrentScreenReasons(){return reasons;}
bool SnapTurnView(float&){return reasons==0;}
int QuickBarAutoHide(bool){return 1;}
bool ImmersiveScopeButton(){return false;}
void ArmHandCalibration(){}
unsigned recenterCount{};void Recenter(){++recenterCount;}
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
    h.menu.hostTick=GetTickCount64();h.menu.blocked=1;h.menu.recenterRequest=1;
    GamepadBridge::MenuTick(2);assert(EngineCamera::recenterCount==1&&h.menu.recenterAck==1);
    GamepadBridge::MenuTick(2);assert(EngineCamera::recenterCount==1);
    h.menu.recenterRequest=2;GamepadBridge::MenuTick(0);assert(EngineCamera::recenterCount==1&&h.menu.recenterAck==2);
    h.menu.recenterRequest=3;h.menu.hostTick=1;GamepadBridge::MenuTick(2);assert(h.menu.recenterAck==2);
    h.menu.recenterRequest=2;
    auto request=[&](uint32_t id,float angle,unsigned reasons=2){
        h.menu.hostTick=GetTickCount64();h.menu.blocked=1;h.menu.request=id;h.menu.requestedDegrees=angle;h.menu.requestedSnap=1;GamepadBridge::MenuTick(reasons);
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
    wcscpy_s(GamepadBridge::configPath,path.c_str());
    auto oldCounts=GamepadBridge::snapCounts;auto oldAngle=GamepadBridge::snapDegrees;
    h.menu.hostTick=GetTickCount64();h.menu.request=6;h.menu.requestedSnap=0;h.menu.requestedDegrees=oldAngle;
    GamepadBridge::snapPending=GamepadBridge::snapMoving=GamepadBridge::snapArmed=true;
    GamepadBridge::MenuTick(2);
    assert(!h.menu.snapEnabled&&!GamepadBridge::snapEnabled&&GamepadBridge::snapCounts==oldCounts);
    assert(!GamepadBridge::snapPending&&!GamepadBridge::snapMoving&&!GamepadBridge::snapArmed);
    for(int n=0;n<200&&h.menu.saved!=6;++n){Sleep(10);GamepadBridge::MenuTick(2);}
    assert(h.menu.saved==6&&GetPrivateProfileIntW(L"VR",L"SnapTurn",2,path.c_str())==0);
    GamepadBridge::LoadSnap();assert(!GamepadBridge::snapEnabled&&GamepadBridge::snapDegrees==oldAngle);
    request(7,oldAngle);assert(h.menu.snapEnabled&&GamepadBridge::snapCounts==oldCounts);
    for(int n=0;n<200&&h.menu.saved!=7;++n){Sleep(10);GamepadBridge::MenuTick(2);}
    assert(h.menu.saved==7&&GetPrivateProfileIntW(L"VR",L"SnapTurn",2,path.c_str())==1);
    GamepadBridge::LoadSnap();assert(GamepadBridge::snapEnabled&&GamepadBridge::snapDegrees==oldAngle);
    h.menu.request=8;h.menu.requestedSnap=2;h.menu.hostTick=GetTickCount64();GamepadBridge::MenuTick(2);
    assert(h.menu.result==2&&h.menu.snapEnabled&&GamepadBridge::snapCounts==oldCounts);
    h.menu.requestedSnap=1;
    for(unsigned mode : {1u,3u,2u}){
        auto id=10+mode;h.menu.hostTick=GetTickCount64();h.menu.request=id;h.menu.requestedMovement=mode;
        GamepadBridge::MenuTick(2);assert(h.menu.movement==mode&&EngineCamera::movement==mode);
        for(int n=0;n<200&&h.menu.saved!=id;++n){Sleep(10);GamepadBridge::MenuTick(2);}
        assert(h.menu.saved==id&&unsigned(DirectionConfig::Read(path.c_str(),L"MovementDirection"))==mode);
    }
    h.menu.request=20;h.menu.requestedMovement=4;h.menu.hostTick=GetTickCount64();GamepadBridge::MenuTick(2);
    assert(h.menu.result==2&&EngineCamera::movement==2);
    // Real config loader, live remapper and persistence: preserve tap/hold,
    // screen layout and right-stick directions while changing selected inputs.
    WritePrivateProfileStringW(L"Buttons",L"RightAHoldMs",L"300",path.c_str());
    WritePrivateProfileStringW(L"Buttons",L"RightATap",L"QuickSave",path.c_str());
    WritePrivateProfileStringW(L"Buttons",L"RightStickUp",L"A",path.c_str());
    WritePrivateProfileStringW(L"ScreenButtons",L"RightA",L"X",path.c_str());
    GamepadBridge::LoadButtons();GamepadBridge::MenuTick(2);
    assert(h.menu.bindings[2]==XINPUT_GAMEPAD_START&&h.menu.holdMs[0]==300&&h.menu.tapTargets[0]==(1u<<18));
    h.menu.requestedMovement=2;h.menu.requestedBindings=h.menu.bindings;
    h.menu.requestedBindings[2]=XINPUT_GAMEPAD_A;h.menu.requestedBindings[9]=GamepadBridge::TargetLT;
    h.menu.bindingMask=(1u<<2)|(1u<<9);h.menu.request=21;h.menu.hostTick=GetTickCount64();
    GamepadBridge::buttonWasDown[2]=true;GamepadBridge::buttonTapUntil[2]=GetTickCount64()+1000;
    GamepadBridge::MenuTick(2);
    assert(h.menu.result==1&&!GamepadBridge::buttonWasDown[2]&&!GamepadBridge::buttonTapUntil[2]);
    assert(GamepadBridge::buttonHoldMs[0]==300&&GamepadBridge::buttonTapTarget[0]==(1u<<18));
    EngineCamera::reasons=0;XINPUT_GAMEPAD mapped{};mapped.wButtons=XINPUT_GAMEPAD_X;mapped.bRightTrigger=200;
    GamepadBridge::ApplyButtons(mapped);assert(mapped.wButtons==XINPUT_GAMEPAD_A&&mapped.bLeftTrigger==200&&!mapped.bRightTrigger);
    EngineCamera::reasons=2;mapped={};mapped.wButtons=XINPUT_GAMEPAD_A;GamepadBridge::ApplyButtons(mapped);
    assert(mapped.wButtons==XINPUT_GAMEPAD_X); // independent screen mapping
    for(int n=0;n<200&&h.menu.saved!=21;++n){Sleep(10);GamepadBridge::MenuTick(2);}
    assert(h.menu.saved==21);GamepadBridge::LoadButtons();
    assert(GamepadBridge::buttonTarget[2]==XINPUT_GAMEPAD_A&&GamepadBridge::rightTriggerTarget==GamepadBridge::TargetLT);
    assert(GamepadBridge::buttonHoldMs[0]==300&&GamepadBridge::buttonTapTarget[0]==(1u<<18)&&GamepadBridge::stickUpTarget==XINPUT_GAMEPAD_A);
    assert(GamepadBridge::screenTarget[0]==XINPUT_GAMEPAD_X);
    // Trigger-to-button and unassigned targets use the existing threshold;
    // returning to default targets recomputes remap activation on reload.
    EngineCamera::reasons=0;
    GamepadBridge::rightTriggerTarget=XINPUT_GAMEPAD_A;
    mapped={};mapped.bRightTrigger=127;GamepadBridge::ApplyButtons(mapped);assert(!mapped.wButtons&&!mapped.bRightTrigger);
    mapped={};mapped.bRightTrigger=128;GamepadBridge::ApplyButtons(mapped);assert(mapped.wButtons==XINPUT_GAMEPAD_A);
    GamepadBridge::rightTriggerTarget=0;mapped={};mapped.bRightTrigger=255;GamepadBridge::ApplyButtons(mapped);assert(!mapped.wButtons&&!mapped.bRightTrigger);
    EngineCamera::reasons=2;GamepadBridge::LoadButtons();
    // Invalid binding requests must not partially change the angle or layout.
    h.menu.request=22;h.menu.hostTick=GetTickCount64();h.menu.requestedBindings[2]=0xffffffff;h.menu.requestedDegrees=90;
    GamepadBridge::MenuTick(2);assert(h.menu.result==2&&GamepadBridge::snapDegrees==oldAngle&&GamepadBridge::buttonTarget[2]==XINPUT_GAMEPAD_A);
    h.menu.request=23;h.menu.bindingMask=1u<<20;h.menu.hostTick=GetTickCount64();GamepadBridge::MenuTick(2);assert(h.menu.result==2);
    // Tap and right-stick mappings round-trip through the real consumer.
    h.menu.requestedDegrees=oldAngle;h.menu.requestedBindings=h.menu.bindings;
    h.menu.requestedBindings[10]=XINPUT_GAMEPAD_X;h.menu.requestedBindings[18]=XINPUT_GAMEPAD_B;
    h.menu.bindingMask=(1u<<10)|(1u<<18);h.menu.request=24;h.menu.hostTick=GetTickCount64();
    GamepadBridge::MenuTick(2);assert(GamepadBridge::buttonTapTarget[0]==XINPUT_GAMEPAD_X&&GamepadBridge::stickUpTarget==XINPUT_GAMEPAD_B);
    for(int n=0;n<200&&h.menu.saved!=24;++n){Sleep(10);GamepadBridge::MenuTick(2);}
    assert(h.menu.saved==24);GamepadBridge::LoadButtons();
    assert(GamepadBridge::buttonTapTarget[0]==XINPUT_GAMEPAD_X&&GamepadBridge::stickUpTarget==XINPUT_GAMEPAD_B&&GamepadBridge::buttonHoldMs[0]==300);
    h.menu.bindingMask=0;h.menu.request=25;h.menu.hostTick=GetTickCount64();h.menu.requestedResponse=1;
    GamepadBridge::MenuTick(2);assert(h.menu.response==1&&EngineCamera::response==1);
    for(int n=0;n<200&&h.menu.saved!=25;++n){Sleep(10);GamepadBridge::MenuTick(2);}
    assert(h.menu.saved==25&&GetPrivateProfileIntW(L"VR",L"MovementResponse",0,path.c_str())==1);
    h.menu.request=26;h.menu.hostTick=GetTickCount64();h.menu.requestedResponse=2;
    GamepadBridge::MenuTick(2);assert(h.menu.result==2&&EngineCamera::response==1);
    h.menu.request=27;h.menu.hostTick=GetTickCount64();h.menu.requestedResponse=1;
    h.menu.requestedInteraction=1;h.menu.resetBindings=1;
    h.menu.requestedBindings=ButtonBindings::Defaults;h.menu.bindingMask=(1u<<ButtonBindings::Count)-1;
    GamepadBridge::MenuTick(2);assert(EngineCamera::interaction==1&&GamepadBridge::buttonHoldMs[0]==0);
    for(int n=0;n<200&&h.menu.saved!=27;++n){Sleep(10);GamepadBridge::MenuTick(2);}
    assert(h.menu.saved==27);GamepadBridge::LoadButtons();
    assert(!GamepadBridge::remapActive&&!GamepadBridge::buttonHoldMs[0]&&!GamepadBridge::buttonTapTarget[0]&&!GamepadBridge::stickUpTarget);
    assert(GamepadBridge::screenTarget[0]==XINPUT_GAMEPAD_X);
    assert(DirectionConfig::Read(path.c_str(),L"InteractionAim")==DirectionConfig::Source::Headset);
    h.menu.request=28;h.menu.hostTick=GetTickCount64();h.menu.requestedInteraction=4;
    GamepadBridge::MenuTick(2);assert(h.menu.result==2&&EngineCamera::interaction==1);
    GamepadBridge::SetChannel(nullptr);
    printf("PASS %zu-bit actual bridge: request/ack/save/error, duplicate/stale/refused edits, calibration scaling, virtual/physical input and focus recovery\n",sizeof(void*)*8);
}
