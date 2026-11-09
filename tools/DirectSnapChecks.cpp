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
bool view=true;float requestedAngle{};unsigned requests{},cancels{};
bool DialogueEnabled(){return false;}bool DialogueAvailable(){return true;}void SetDialogueEnabled(bool){}
float ConfiguredWorldScale(){return 300;}float ActiveWorldScale(){return 300;}void SetConfiguredWorldScale(float){}
unsigned InteractionAim(){return 0;}void SetInteractionAim(unsigned){}
unsigned MovementResponse(){return 0;}void SetMovementResponse(unsigned){}
unsigned MovementDirection(){return 0;}void SetMovementDirection(unsigned){}
unsigned CurrentScreenReasons(){return view?0:2;}
bool SnapTurnView(float& yaw){yaw=0;return view;}
void SetDirectSnapEnabled(bool){}void CancelDirectSnap(){++cancels;}
bool RequestDirectSnap(float degrees){requestedAngle=degrees;++requests;return true;}
int QuickBarAutoHide(bool){assert(false);return 0;}
bool ImmersiveScopeButton(){return false;}void ArmHandCalibration(){}void Recenter(){}
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
    puts("PASS direct snap: one-shot/expiry/ownership/cancel, yaw direction/wrap, actual bridge rearm/focus/menu and untouched left stick");
}
