#include <windows.h>
#include <cassert>
#include <limits>
#include "DirectSnap.h"
#include "NativeHeading.h"
bool testFocus=true;
HWND TestForeground(){return reinterpret_cast<HWND>(1);}
DWORD TestWindowThread(HWND,DWORD* pid){*pid=testFocus?GetCurrentProcessId():0;return 1;}
#define GetForegroundWindow TestForeground
#define GetWindowThreadProcessId TestWindowThread
#include "../src/GamepadBridge.cpp"
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
namespace EngineCamera {
bool nativeCover{};bool NativeCoverControls(){return nativeCover;}
bool view=true;float requestedAngle{};unsigned requests{},cancels{};
bool DialogueEnabled(){return false;}bool DialogueAvailable(){return true;}void SetDialogueEnabled(bool){}
bool HandHudConfigured(){return true;}bool HandHudActive(){return true;}void SetHandHudConfigured(bool){}
unsigned ThrowAimSetting(){return 0;}void SetThrowAim(unsigned){}unsigned ThrowMarkerSetting(){return 0;}void SetThrowMarker(bool){}
unsigned CrosshairSetting(){return 0;}void SetCrosshair(unsigned){}unsigned HandPointerSetting(){return 0;}void SetHandPointers(unsigned){}unsigned CoverFollowSetting(){return 0;}void SetCoverFollow(bool){}unsigned ImmersiveScopeSetting(){return 0;}void SetImmersiveScope(bool){}
float ConfiguredWorldScale(){return 300;}float ActiveWorldScale(){return 300;}void SetConfiguredWorldScale(float){}float ScopeMagnification(){return 4;}bool SetScopeMagnification(float){return true;}
unsigned InteractionAim(){return 0;}void SetInteractionAim(unsigned){}
unsigned MovementResponse(){return 0;}void SetMovementResponse(unsigned){}
unsigned MovementDirection(){return 0;}void SetMovementDirection(unsigned){}
unsigned CurrentScreenReasons(){return view?0:2;}
bool SnapTurnView(float& yaw){yaw=0;return view;}
void SetDirectSnapEnabled(bool){}void CancelDirectSnap(){++cancels;}
bool RequestDirectSnap(float degrees){requestedAngle=degrees;++requests;return true;}
int QuickBarAutoHide(bool){assert(false);return 0;}unsigned QuickBarSetting(){return 0;}void SetQuickBar(bool){}
bool ImmersiveScopeButton(){return false;}void ArmHandCalibration(){}void Recenter(){}
bool sprintBlocked{},nearWeapon{},weaponHeld{};
bool TwoHandSprintBlocked(){return sprintBlocked;}
bool ThrowMarkerEnabled(){return false;}void SetThrowAiming(bool){}void NoteThrowRelease(uint64_t){}bool LeftHandOwnsPrompt(){return false;}bool LeftHandCarrying(){return false;}bool RightHandCarrying(){return false;}bool LeftHandNearWeapon(){return nearWeapon;}void SetLeftHandOnWeapon(bool v){weaponHeld=v;}bool LeftHanded(){return false;}void SetGameSprintHeld(bool){}bool DetonateReady(){return false;}void NoteDetonatePress(uint64_t){}
}
int main(){
    DirectSnap::Request q;
    assert(!q.Queue(0,10,1,2));assert(!q.Queue(181,10,1,2));
    assert(!q.Queue(std::numeric_limits<float>::quiet_NaN(),10,1,2));
    assert(!q.Queue(30,10,0,2));
    assert(q.Queue(30,10,1,2));assert(!q.Queue(30,11,1,2));
    assert(q.Take(11,1,2,true)==30);assert(q.Take(12,1,2,true)==0);
    assert(q.Queue(-30,10,1,2));assert(q.Take(111,1,2,true)==0);
    assert(q.Queue(30,10,1,2));assert(q.Take(9,1,2,true)==0);
    assert(q.Queue(30,10,1,2));assert(q.Take(11,3,2,true)==0);
    assert(q.Queue(30,10,1,2));assert(q.Take(11,1,3,true)==0);
    assert(q.Queue(30,10,1,2));assert(q.Take(11,1,2,false)==0);
    assert(q.Queue(30,10,1,2));q.Clear();assert(q.Take(11,1,2,true)==0);
    CameraMath::Matrix origin{};
    for(float yaw:{-3.13f,-1.f,0.f,1.f,3.13f})for(float angle:{-180.f,-90.f,-30.f,30.f,90.f,180.f}){
        float turned=DirectSnap::Turn(yaw,angle);
        assert(std::isfinite(turned)&&std::abs(turned)<=DirectSnap::Pi);
        auto before=NativeHeading::Base(yaw,origin),after=NativeHeading::Base(turned,origin);
        float delta=CameraMath::HeadingDelta(before,after);
        assert(std::abs(std::remainder(delta+angle*DirectSnap::Pi/180,2*DirectSnap::Pi))<1e-5f);
    }
    // Exercise the real bridge snap path, with only engine and foreground APIs stubbed.
    XINPUT_GAMEPAD pad{};pad.sThumbLX=12345;pad.sThumbLY=-23456;
    auto tick=[&](SHORT x,SHORT y){pad.sThumbRX=x;pad.sThumbRY=y;GamepadBridge::SnapTurn(pad);
        assert(pad.sThumbLX==12345&&pad.sThumbLY==-23456);};
    GamepadBridge::snapDegrees=30;tick(0,0);tick(30000,0);
    assert(EngineCamera::requests==1&&EngineCamera::requestedAngle==30);
    tick(30000,0);assert(EngineCamera::requests==1);
    GamepadBridge::snapCooldown=0;tick(0,0);tick(-30000,0);
    assert(EngineCamera::requests==2&&EngineCamera::requestedAngle==-30);
    GamepadBridge::snapCooldown=0;tick(0,0);tick(0,30000);
    assert(EngineCamera::requests==2);
    EngineCamera::view=false;tick(30000,0);assert(pad.sThumbRX==30000&&!GamepadBridge::snapArmed);
    EngineCamera::view=true;tick(30000,0);assert(EngineCamera::requests==2);
    tick(0,0);testFocus=false;tick(30000,0);assert(!GamepadBridge::snapArmed);
    testFocus=true;tick(30000,0);assert(EngineCamera::requests==2&&EngineCamera::cancels>=2);
    EngineCamera::nativeCover=true;
    GamepadBridge::stickUpTarget=XINPUT_GAMEPAD_A;GamepadBridge::stickDownTarget=XINPUT_GAMEPAD_B;
    const unsigned previousRequests=EngineCamera::requests;
    // Cover aims with the controller: the right stick never reaches the game
    // as look/aim, keeps its up/down buttons, and queues no snap.
    GamepadBridge::snapArmed=true;tick(28000,17000);
    assert(pad.sThumbRX==0&&pad.sThumbRY==0&&!GamepadBridge::snapArmed);
    assert(EngineCamera::requests==previousRequests);
    GamepadBridge::snapCooldown=0;tick(0,0);assert(!GamepadBridge::snapArmed);tick(30000,0);
    assert(pad.sThumbRX==0&&EngineCamera::requests==previousRequests&&!GamepadBridge::snapArmed);
    pad.wButtons=0;tick(0,30000);assert(pad.sThumbRY==0&&(pad.wButtons&XINPUT_GAMEPAD_A));
    pad.wButtons=0;tick(0,-30000);assert(pad.sThumbRY==0&&(pad.wButtons&XINPUT_GAMEPAD_B));
    pad={};pad.sThumbRX=-11000;pad.sThumbRY=30000;GamepadBridge::StickButtons(pad);
    assert(pad.sThumbRX==0&&pad.sThumbRY==0&&(pad.wButtons&XINPUT_GAMEPAD_A));
    pad={};pad.sThumbRX=-30000;pad.sThumbRY=2000;GamepadBridge::StickButtons(pad);
    assert(pad.sThumbRX==-30000&&pad.sThumbRY==0&&pad.wButtons==0);
    EngineCamera::nativeCover=false;pad={};pad.sThumbLX=12345;pad.sThumbLY=-23456;
    GamepadBridge::snapCooldown=0;tick(30000,0);assert(EngineCamera::requests==previousRequests);
    tick(0,0);tick(30000,0);assert(EngineCamera::requests==previousRequests+1);
    puts("PASS cover stick: no stick aiming, no queued snap, first-person up/down buttons kept, smooth-turn axis kept, return to first person requires neutral rearm");
    puts("PASS direct snap: one-shot/expiry/ownership/cancel, yaw direction/wrap, actual bridge rearm/focus/menu and untouched left stick");
    for(bool remapped:{false,true}) {
        GamepadBridge::remapActive=remapped;
        for(size_t i=0;i<GamepadBridge::buttonCount;i++)GamepadBridge::buttonTarget[i]=GamepadBridge::buttonSources[i].bit;
        if(remapped)GamepadBridge::buttonTarget[2]=XINPUT_GAMEPAD_B; // Dante's LeftX override
        auto grip=[&](bool down){pad={};pad.wButtons=down?XINPUT_GAMEPAD_LEFT_SHOULDER:0;GamepadBridge::ApplyButtons(pad);};
        EngineCamera::nearWeapon=true;EngineCamera::sprintBlocked=false;
        grip(false);grip(true);assert(EngineCamera::weaponHeld&&pad.wButtons==0);
        EngineCamera::sprintBlocked=true;grip(true);
        assert(!EngineCamera::weaponHeld&&pad.wButtons==0);
        EngineCamera::sprintBlocked=false;grip(true);
        assert(!EngineCamera::weaponHeld&&pad.wButtons==0); // no automatic return
        grip(false);grip(true);assert(EngineCamera::weaponHeld);
        grip(false);EngineCamera::sprintBlocked=true;grip(true);
        assert(!EngineCamera::weaponHeld&&(pad.wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER));
        EngineCamera::sprintBlocked=false;grip(true);
        assert(!EngineCamera::weaponHeld&&(pad.wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER)); // same press stays sprint
        grip(false);EngineCamera::nearWeapon=false;grip(true);
        assert(!EngineCamera::weaponHeld&&(pad.wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER));
        grip(false);
        puts("PASS real bridge: sprint cancels hold, release required, sprint re-press never becomes a grab mid-press");
    }
}
