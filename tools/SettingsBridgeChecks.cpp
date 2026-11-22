#include "LiveWorldScale.h"
// Exercise the actual game-side consumer and final XInput merge, without a game.
#include "../src/GamepadBridge.cpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>
namespace EngineCamera {
bool nativeCover{};bool NativeCoverControls(){return nativeCover;}
bool dialogue{},available=true;
bool DialogueEnabled(){return dialogue;}
bool DialogueAvailable(){return available;}
void SetDialogueEnabled(bool value){dialogue=value&&available;}
bool handHud=true,handHudActive=true;
bool HandHudConfigured(){return handHud;}bool HandHudActive(){return handHudActive;}void SetHandHudConfigured(bool value){handHud=handHudActive=value;}
// Transport 24 menu tabs: 0 = unavailable (setters then ignored, as in EngineCamera).
unsigned throwAimSetting=3,throwMarkerSetting=2,coverFollowSetting=2,scopeSetting=1;
unsigned ThrowAimSetting(){return throwAimSetting;}void SetThrowAim(unsigned s){if(throwAimSetting&&s<=3)throwAimSetting=s+1;}
unsigned ThrowMarkerSetting(){return throwMarkerSetting;}void SetThrowMarker(bool v){if(throwMarkerSetting)throwMarkerSetting=v?2:1;}
unsigned CoverFollowSetting(){return coverFollowSetting;}void SetCoverFollow(bool v){if(coverFollowSetting)coverFollowSetting=v?2:1;}
unsigned ImmersiveScopeSetting(){return scopeSetting;}void SetImmersiveScope(bool v){if(scopeSetting)scopeSetting=v?2:1;}
LiveWorldScale scale;
float ConfiguredWorldScale(){return scale.Configured();}
float ActiveWorldScale(){return scale.Active();}
void SetConfiguredWorldScale(float value){scale.Request(value);}
float zoom=4;
float ScopeMagnification(){return zoom;}
bool SetScopeMagnification(float value){if(!(value>=1&&value<=12))return false;zoom=value;return true;}
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
bool throwMarker{},throwAiming{};uint64_t throwHeld{};
bool ThrowMarkerEnabled(){return throwMarker;}void SetThrowAiming(bool v){throwAiming=v;}void NoteThrowRelease(uint64_t ms){throwHeld=ms;}
bool detonate{};uint64_t detonateHeld{};
bool leftOwns{};bool LeftHandOwnsPrompt(){return leftOwns;}
bool leftCarry{};bool LeftHandCarrying(){return leftCarry;}
bool DetonateReady(){return detonate;}void NoteDetonatePress(uint64_t ms){detonateHeld=ms;}
unsigned CurrentScreenReasons(){return reasons;}
bool SnapTurnView(float&){return reasons==0;}
bool directEnabled{},directPending{};
void SetDirectSnapEnabled(bool value){directEnabled=value;directPending=false;}
void CancelDirectSnap(){directPending=false;}
bool RequestDirectSnap(float){return true;}
int QuickBarAutoHide(bool){return 1;}
bool ImmersiveScopeButton(){return false;}
void ArmHandCalibration(){}
unsigned recenterCount{};void Recenter(){++recenterCount;}
}
XINPUT_GAMEPAD physical{};
DWORD WINAPI RealGet(DWORD,XINPUT_STATE* s){*s={};s->Gamepad=physical;return ERROR_SUCCESS;}
int wmain(int argc,wchar_t** argv) {
    EngineCamera::scale.Request(300);EngineCamera::scale.BeginCamera();
    assert(argc==2);std::filesystem::path dir=argv[1];std::filesystem::create_directories(dir);
    auto path=std::filesystem::absolute(dir/L"bridge-test.ini");
    {std::ofstream f(path);f<<"[VR]\nSnapTurn=1\nSnapTurnDegrees=30\nSnapTurnMouseCounts=300\n[Buttons]\nLeftX=Start\n";}
    Transport::Header h{};h.menu.requestedWorldScale=300;GamepadBridge::SetChannel(&h);
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
    EngineCamera::directPending=GamepadBridge::snapArmed=true;
    GamepadBridge::MenuTick(2);
    assert(!h.menu.snapEnabled&&!GamepadBridge::snapEnabled&&GamepadBridge::snapCounts==oldCounts);
    assert(!EngineCamera::directPending&&!EngineCamera::directEnabled&&!GamepadBridge::snapArmed);
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
    EngineCamera::nativeCover=true;mapped={};mapped.wButtons=XINPUT_GAMEPAD_X;mapped.bRightTrigger=200;
    mapped.sThumbRX=12345;mapped.sThumbRY=-23456;
    GamepadBridge::ApplyButtons(mapped);
    assert(mapped.wButtons==XINPUT_GAMEPAD_A&&mapped.bLeftTrigger==200&&!mapped.bRightTrigger);
    assert(mapped.sThumbRX==12345&&mapped.sThumbRY==-23456);
    EngineCamera::nativeCover=false;
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
    h.menu.request=26;h.menu.hostTick=GetTickCount64();h.menu.requestedResponse=3;
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
    h.menu.request=29;h.menu.hostTick=GetTickCount64();h.menu.requestedInteraction=1;h.menu.resetBindings=0;h.menu.bindingMask=0;h.menu.requestedWorldScale=272.727f;
    GamepadBridge::MenuTick(2);assert(h.menu.worldScale==272.727f&&h.menu.activeWorldScale==300);
    // Calibration writes must not drop the pending scale.
    SettingsWriter().Queue(path.c_str(),GamepadBridge::snapCounts);
    for(int n=0;n<200&&h.menu.saved!=29;++n){Sleep(10);GamepadBridge::MenuTick(2);}
    assert(h.menu.saved==29&&EngineCamera::ActiveWorldScale()==300);
    // Resume consumes the real pending-scale object; next menu tick reports it.
    EngineCamera::scale.BeginCamera();GamepadBridge::MenuTick(2);
    assert(h.menu.activeWorldScale==272.727f);
    wchar_t scaleText[40];GetPrivateProfileStringW(L"VR",L"WorldUnitsPerMetre",L"",scaleText,40,path.c_str());assert(std::abs(_wtof(scaleText)-272.727)<.001);
    for(float bad : {0.f,1001.f,std::numeric_limits<float>::quiet_NaN()}){
        ++h.menu.request;h.menu.hostTick=GetTickCount64();h.menu.requestedWorldScale=bad;
        GamepadBridge::MenuTick(2);assert(h.menu.result==2&&EngineCamera::ConfiguredWorldScale()==272.727f);
    }
    // Visual interaction observes remapped output, not raw X.
    EngineCamera::reasons=0;h.menu.blocked=0;GamepadBridge::menuGate={};
    GamepadBridge::EnableInteractionVisual(true);GamepadBridge::enabled=true;
    GamepadBridge::remapActive=true;GamepadBridge::snapEnabled=false;
    for(size_t i=0;i<GamepadBridge::buttonCount;i++){
        GamepadBridge::buttonTarget[i]=0;GamepadBridge::buttonTapTarget[i]=0;
        GamepadBridge::buttonHoldMs[i]=0;GamepadBridge::buttonWasDown[i]=false;
    }
    GamepadBridge::buttonTarget[0]=XINPUT_GAMEPAD_X;
    t={};t.tick=GetTickCount64();t.gamepad.valid=1;t.gamepad.buttons=XINPUT_GAMEPAD_A;
    Transport::WriteTracking(&h,t);XINPUT_GAMEPAD visualPad{};GamepadBridge::Read(visualPad);
    assert(visualPad.wButtons==XINPUT_GAMEPAD_X&&GamepadBridge::InteractionVisualHeld(GetTickCount64()));
    t.tick=GetTickCount64();t.gamepad.buttons=XINPUT_GAMEPAD_X;Transport::WriteTracking(&h,t);
    visualPad={};GamepadBridge::Read(visualPad);assert(!GamepadBridge::InteractionVisualHeld(GetTickCount64()));
    GamepadBridge::rightTriggerTarget=XINPUT_GAMEPAD_X;t.gamepad.buttons=0;t.gamepad.rightTrigger=255;
    Transport::WriteTracking(&h,t);visualPad={};GamepadBridge::Read(visualPad);assert(GamepadBridge::InteractionVisualHeld(GetTickCount64()));
    t.tick=1;Transport::WriteTracking(&h,t);visualPad={};GamepadBridge::Read(visualPad);assert(!GamepadBridge::InteractionVisualHeld(GetTickCount64()));
    // Left grip: Interact only for a press that starts while the left hand owns the prompt.
    {
        const WORD LB=XINPUT_GAMEPAD_LEFT_SHOULDER,X=XINPUT_GAMEPAD_X,Y=XINPUT_GAMEPAD_Y;
        const auto savedTrigger=GamepadBridge::rightTriggerTarget;GamepadBridge::rightTriggerTarget=0;
        GamepadBridge::buttonTarget[0]=0;GamepadBridge::buttonTarget[4]=Y;EngineCamera::reasons=0;
        auto grip=[&](bool down){XINPUT_GAMEPAD p{};if(down)p.wButtons=LB;GamepadBridge::ApplyButtons(p);return p.wButtons;};
        EngineCamera::leftOwns=false;assert(grip(true)==Y);assert(grip(false)==0);          // no left prompt: the wheel
        EngineCamera::leftOwns=true;assert(grip(true)==X);                                  // left prompt: Interact
        EngineCamera::leftOwns=false;assert(grip(true)==X);assert(grip(false)==0);          // kept until release, no stray wheel tap
        assert(grip(true)==Y);EngineCamera::leftOwns=true;assert(grip(true)==Y);assert(grip(false)==0); // a wheel press stays the wheel
        assert(grip(true)==X);EngineCamera::reasons=1;assert(!(grip(true)&(LB|Y)));         // Interact opened a screen
        EngineCamera::leftOwns=false;EngineCamera::reasons=0;assert(grip(true)==0);         // still held: not a new wheel press
        assert(grip(false)==0);assert(grip(true)==Y);assert(grip(false)==0);                // next press is normal
        GamepadBridge::remapActive=false;EngineCamera::leftOwns=true;assert(grip(true)==X);assert(grip(false)==0); // default layout
        EngineCamera::leftOwns=false;assert(grip(true)==LB);assert(grip(false)==0);
        GamepadBridge::remapActive=true;
        // A left-hand Interact counts as held but must not curl the right hand's fingers.
        EngineCamera::leftOwns=true;t={};t.tick=GetTickCount64();t.gamepad.valid=1;t.gamepad.buttons=LB;Transport::WriteTracking(&h,t);
        visualPad={};GamepadBridge::Read(visualPad);
        assert(visualPad.wButtons==X&&GamepadBridge::InteractionHeld(GetTickCount64())&&!GamepadBridge::InteractionVisualHeld(GetTickCount64()));
        assert(GamepadBridge::LastInteractWasLeft());
        EngineCamera::leftOwns=false;t.tick=GetTickCount64();t.gamepad.buttons=0;Transport::WriteTracking(&h,t);
        visualPad={};GamepadBridge::Read(visualPad);assert(!GamepadBridge::InteractionHeld(GetTickCount64()));
        // Carrying with the left hand: the left trigger throws, the right does the left's job.
        const auto savedLeftTrigger=GamepadBridge::leftTriggerTarget;
        GamepadBridge::leftTriggerTarget=GamepadBridge::TargetLT;GamepadBridge::rightTriggerTarget=GamepadBridge::TargetRT;
        auto trig=[&](BYTE l,BYTE r){XINPUT_GAMEPAD p{};p.bLeftTrigger=l;p.bRightTrigger=r;GamepadBridge::ApplyButtons(p);return std::pair<int,int>(p.bLeftTrigger,p.bRightTrigger);};
        assert((trig(200,0)==std::pair<int,int>(200,0)));assert((trig(0,200)==std::pair<int,int>(0,200)));   // normal
        EngineCamera::leftCarry=true;assert((trig(0,200)==std::pair<int,int>(0,0)));                          // already squeezed: nothing
        assert((trig(0,0)==std::pair<int,int>(0,0)));assert((trig(200,0)==std::pair<int,int>(0,200)));        // left trigger throws
        assert((trig(0,0)==std::pair<int,int>(0,0)));assert((trig(0,200)==std::pair<int,int>(200,0)));        // right does the left's job
        assert((trig(0,0)==std::pair<int,int>(0,0)));assert((trig(200,0)==std::pair<int,int>(0,200)));
        EngineCamera::leftCarry=false;assert((trig(200,0)==std::pair<int,int>(0,0)));                         // carry ended mid-squeeze: no cover
        assert((trig(0,0)==std::pair<int,int>(0,0)));assert((trig(200,0)==std::pair<int,int>(200,0)));assert((trig(0,0)==std::pair<int,int>(0,0)));
        GamepadBridge::remapActive=false;EngineCamera::leftCarry=true;trig(0,0);assert((trig(200,0)==std::pair<int,int>(0,200)));   // default layout
        trig(0,0);EngineCamera::leftCarry=false;trig(0,0);GamepadBridge::remapActive=true;
        GamepadBridge::leftTriggerTarget=savedLeftTrigger;
        GamepadBridge::buttonTarget[4]=0;GamepadBridge::buttonTarget[0]=X;GamepadBridge::rightTriggerTarget=savedTrigger;
    }
    GamepadBridge::EnableInteractionVisual(false);
    h.menu.requestedDegrees=30;h.menu.requestedSnap=1;h.menu.requestedMovement=1;
    h.menu.requestedInteraction=1;h.menu.requestedResponse=0;h.menu.requestedWorldScale=300;
    h.menu.resetBindings=0;h.menu.bindingMask=0;h.menu.requestedDialogue=1;
    request(100,30);assert(EngineCamera::dialogue&&h.menu.dialogue==1);
    for(int n=0;n<200&&h.menu.saved!=100;++n){Sleep(10);GamepadBridge::MenuTick(2);}
    assert(h.menu.saved==100&&GetPrivateProfileIntW(L"VR",L"FixedDialogueCamera",0,path.c_str())==1);
    EngineCamera::available=false;request(101,30);assert(h.menu.result==2);
    h.menu.requestedDialogue=0;request(102,30);assert(!EngineCamera::dialogue);
    for(int n=0;n<200&&h.menu.saved!=102;++n){Sleep(10);GamepadBridge::MenuTick(2);}
    assert(h.menu.saved==102&&GetPrivateProfileIntW(L"VR",L"FixedDialogueCamera",1,path.c_str())==0);
    h.menu.requestedDialogue=2;request(103,30);assert(h.menu.result==2);
    h.menu.requestedDialogue=0;h.menu.requestedResponse=2;EngineCamera::available=true;
    request(104,30);assert(h.menu.response==2&&EngineCamera::response==2);
    for(int n=0;n<200&&h.menu.saved!=104;++n){Sleep(10);GamepadBridge::MenuTick(2);}
    assert(h.menu.saved==104&&GetPrivateProfileIntW(L"VR",L"MovementResponse",3,path.c_str())==2);
    h.menu.requestedResponse=0;request(105,30);assert(EngineCamera::response==0);
    for(int n=0;n<200&&h.menu.saved!=105;++n){Sleep(10);GamepadBridge::MenuTick(2);}
    assert(h.menu.saved==105&&GetPrivateProfileIntW(L"VR",L"MovementResponse",3,path.c_str())==0);
    // HUD: reported as saved/active; a request saves HandHud and switches it live.
    GamepadBridge::MenuTick(2);assert(h.menu.handHud==2&&h.menu.activeHandHud==2);
    assert(GetPrivateProfileIntW(L"VR",L"HandHud",7,path.c_str())==7); // nothing written without a request
    h.menu.requestedHandHud=1;request(106,30);assert(!EngineCamera::handHud&&h.menu.handHud==1);
    GamepadBridge::MenuTick(2);assert(h.menu.activeHandHud==1);
    for(int n=0;n<200&&h.menu.saved!=106;++n){Sleep(10);GamepadBridge::MenuTick(2);}
    assert(h.menu.saved==106&&h.menu.result==0&&GetPrivateProfileIntW(L"VR",L"HandHud",7,path.c_str())==0);
    h.menu.requestedHandHud=0;request(107,30);assert(!EngineCamera::handHud&&h.menu.handHud==1); // 0 keeps it
    for(int n=0;n<200&&h.menu.saved!=107;++n){Sleep(10);GamepadBridge::MenuTick(2);}
    assert(GetPrivateProfileIntW(L"VR",L"HandHud",7,path.c_str())==0);
    h.menu.requestedHandHud=3;request(108,30);assert(h.menu.result==2&&!EngineCamera::handHud);  // refused
    h.menu.requestedHandHud=2;request(109,30);assert(EngineCamera::handHud&&h.menu.handHud==2);
    for(int n=0;n<200&&h.menu.saved!=109;++n){Sleep(10);GamepadBridge::MenuTick(2);}
    assert(h.menu.saved==109&&GetPrivateProfileIntW(L"VR",L"HandHud",7,path.c_str())==1);
    h.menu.requestedHandHud=0;
    // Scope zoom: applied at once, saved as ScopeMagnification, 0 keeps it, out of range refused.
    GamepadBridge::MenuTick(2);assert(h.menu.scopeZoom==4);
    h.menu.requestedScopeZoom=3;request(106,30);assert(EngineCamera::zoom==3&&h.menu.scopeZoom==3);
    for(int n=0;n<200&&h.menu.saved!=106;++n){Sleep(10);GamepadBridge::MenuTick(2);}
    {wchar_t z[40]{};GetPrivateProfileStringW(L"VR",L"ScopeMagnification",L"",z,40,path.c_str());assert(h.menu.saved==106&&_wtof(z)==3);}
    h.menu.requestedScopeZoom=0;request(107,30);assert(h.menu.result!=2&&EngineCamera::zoom==3);
    for(int n=0;n<200&&h.menu.saved!=107;++n){Sleep(10);GamepadBridge::MenuTick(2);}
    {wchar_t z[40]{};GetPrivateProfileStringW(L"VR",L"ScopeMagnification",L"",z,40,path.c_str());assert(h.menu.saved==107&&_wtof(z)==3);}
    for(float bad:{.5f,12.5f,std::numeric_limits<float>::quiet_NaN()}){h.menu.requestedScopeZoom=bad;request(108,30);assert(h.menu.result==2&&EngineCamera::zoom==3);}
    h.menu.requestedScopeZoom=0;
    // Menu tabs (transport 24): reported, applied at once and saved; 0 keeps each;
    // out-of-range and unavailable requests are refused whole.
    {
        auto text=[&](const wchar_t* key){static wchar_t v[40];GetPrivateProfileStringW(L"VR",key,L"",v,40,path.c_str());return std::wstring(v);};
        auto wait=[&](uint32_t id){for(int n=0;n<200&&h.menu.saved!=id;++n){Sleep(10);GamepadBridge::MenuTick(2);}assert(h.menu.saved==id&&h.menu.result==0);};
        GamepadBridge::MenuTick(2);
        assert(h.menu.throwAim==3&&h.menu.throwMarker==2&&h.menu.coverFollow==2&&h.menu.immersiveScope==1);
        assert(text(L"ThrowAim").empty()&&text(L"ThrowMarker").empty()&&text(L"CoverCameraFollow").empty()&&text(L"ImmersiveScope").empty()); // nothing written unasked
        EngineCamera::throwAiming=true;
        h.menu.requestedThrowAim=1;h.menu.requestedThrowMarker=1;h.menu.requestedCoverFollow=1;h.menu.requestedImmersiveScope=2;request(110,30);
        assert(h.menu.result!=2&&EngineCamera::throwAimSetting==1&&EngineCamera::throwMarkerSetting==1&&EngineCamera::coverFollowSetting==1&&EngineCamera::scopeSetting==2);
        assert(h.menu.throwAim==1&&h.menu.throwMarker==1&&h.menu.coverFollow==1&&h.menu.immersiveScope==2);
        assert(!EngineCamera::throwAiming); // a half-made throw hold is dropped
        wait(110);
        assert(text(L"ThrowAim")==L"Mouse"&&text(L"ThrowMarker")==L"0"&&text(L"CoverCameraFollow")==L"0"&&text(L"ImmersiveScope")==L"1");
        h.menu.requestedThrowAim=h.menu.requestedThrowMarker=h.menu.requestedCoverFollow=h.menu.requestedImmersiveScope=0;request(111,30);
        assert(EngineCamera::throwAimSetting==1&&EngineCamera::throwMarkerSetting==1&&EngineCamera::coverFollowSetting==1&&EngineCamera::scopeSetting==2);
        wait(111);assert(text(L"ThrowAim")==L"Mouse"&&text(L"ImmersiveScope")==L"1");
        h.menu.requestedThrowAim=4;h.menu.requestedThrowMarker=2;request(112,30);assert(EngineCamera::throwAimSetting==4&&EngineCamera::throwMarkerSetting==2);
        wait(112);assert(text(L"ThrowAim")==L"LeftController"&&text(L"ThrowMarker")==L"1");
        h.menu.requestedThrowAim=3;h.menu.requestedThrowMarker=0;h.menu.requestedCoverFollow=2;h.menu.requestedImmersiveScope=1;request(113,30);
        wait(113);assert(text(L"ThrowAim")==L"Controller"&&text(L"CoverCameraFollow")==L"1"&&text(L"ImmersiveScope")==L"0");
        h.menu.requestedThrowAim=h.menu.requestedCoverFollow=h.menu.requestedImmersiveScope=0;
        h.menu.requestedThrowAim=5;request(114,30);assert(h.menu.result==2&&EngineCamera::throwAimSetting==3);h.menu.requestedThrowAim=0; // still the right hand set by 113
        h.menu.requestedThrowMarker=3;request(115,30);assert(h.menu.result==2);h.menu.requestedThrowMarker=0;
        h.menu.requestedCoverFollow=3;request(116,30);assert(h.menu.result==2);h.menu.requestedCoverFollow=0;
        h.menu.requestedImmersiveScope=3;request(117,30);assert(h.menu.result==2);h.menu.requestedImmersiveScope=0;
        EngineCamera::coverFollowSetting=0;GamepadBridge::MenuTick(2);assert(h.menu.coverFollow==0);
        h.menu.requestedCoverFollow=2;request(118,30);assert(h.menu.result==2&&EngineCamera::coverFollowSetting==0); // unavailable: refused
        h.menu.requestedCoverFollow=0;EngineCamera::coverFollowSetting=2;
        EngineCamera::throwAimSetting=3;EngineCamera::throwMarkerSetting=2;EngineCamera::scopeSetting=1;
        request(119,30);wait(119);
        printf("PASS %zu-bit menu tabs: grenade aim, landing ring, cover camera and immersive scope reported, switched at once, saved (ThrowAim Mouse/LeftController/Controller), 0 keeps, bad or unavailable refused\n",sizeof(void*)*8);
    }
    // Grenade landing marker through the real remapper, with each layout.
    {
        EngineCamera::reasons=0;EngineCamera::throwMarker=true;
        auto poll=[&](bool down){XINPUT_GAMEPAD p{};if(down)p.wButtons=XINPUT_GAMEPAD_A;GamepadBridge::ApplyButtons(p);return (p.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER)!=0;};
        auto layout=[&](const wchar_t* a,const wchar_t* tap){
            WritePrivateProfileStringW(L"Buttons",L"RightA",a,path.c_str());
            WritePrivateProfileStringW(L"Buttons",L"RightAHoldMs",L"300",path.c_str());
            WritePrivateProfileStringW(L"Buttons",L"RightATap",tap,path.c_str());
            GamepadBridge::LoadButtons();assert(GamepadBridge::remapActive);};
        // Mark's layout since Nov 15: RightA=RB, RightAHoldMs=300, RightATap=None.
        layout(L"RB",L"None");
        assert(!poll(true));Sleep(50);assert(!poll(true));
        assert(!poll(false)&&!EngineCamera::throwAiming);Sleep(30);assert(!poll(false)); // a tap throws nothing
        assert(!poll(true));Sleep(350);assert(!poll(true)&&EngineCamera::throwAiming);  // a hold shows the ring, no throw yet
        assert(poll(false)&&!EngineCamera::throwAiming&&EngineCamera::throwHeld>=300);   // release throws
        Sleep(150);assert(!poll(false));                                                  // once
        // A thrown remote device waits for "hold RB": a press that starts then is RB at once, held, no delay.
        EngineCamera::detonate=true;
        assert(poll(true)&&!EngineCamera::throwAiming);Sleep(350);assert(poll(true)&&!EngineCamera::throwAiming); // RB from the first poll, no ring
        EngineCamera::detonate=false;assert(poll(true));                                 // detonated while held: still RB
        assert(!poll(false)&&EngineCamera::detonateHeld>=300);Sleep(150);assert(!poll(false)); // release: no throw
        EngineCamera::detonate=true;assert(poll(true));Sleep(30);assert(!poll(false));Sleep(150);assert(!poll(false)); // a short press: RB only while held
        EngineCamera::detonate=false;                                                     // device gone: throws again
        assert(!poll(true));Sleep(350);assert(!poll(true)&&EngineCamera::throwAiming);assert(poll(false));Sleep(150);assert(!poll(false));
        // Nov 14 layout: RightA=None, RightATap=RB: a tap throws on release, a hold aims.
        layout(L"None",L"RB");
        assert(!poll(true));Sleep(50);assert(poll(false));Sleep(150);assert(!poll(false));
        assert(!poll(true));Sleep(350);assert(!poll(true)&&EngineCamera::throwAiming);assert(poll(false));Sleep(150);poll(false);
        EngineCamera::detonate=true;assert(poll(true));Sleep(350);assert(poll(true));assert(!poll(false));Sleep(150);assert(!poll(false)); // detonate holds RB here too
        EngineCamera::detonate=false;
        // Marker off: the layout behaves as before the marker (RB while held past 300 ms).
        EngineCamera::throwMarker=false;layout(L"RB",L"None");
        assert(!poll(true));Sleep(350);assert(poll(true));assert(!poll(false));
        EngineCamera::reasons=2;
        printf("PASS %zu-bit grenade hold through the real remapper: RightA=RB hold 300 (tap throws nothing; hold shows the ring, throws on release; while a remote device waits, RB from the first moment until release, also with RightATap=RB), RightATap=RB (tap throws), marker off unchanged\n",sizeof(void*)*8);
    }
    GamepadBridge::SetChannel(nullptr);
    printf("PASS %zu-bit actual bridge: request/ack/save/error, duplicate/stale/refused edits, calibration scaling, virtual/physical input and focus recovery\n",sizeof(void*)*8);
}
