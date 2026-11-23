#include "GamepadBridge.h"
#include "LeftCarry.h"
#include "DirectionConfig.h"
#include "EngineCamera.h"
#include "SettingsPersistence.h"
#include "SettingsMenu.h"
#include "NativeMenuPointer.h"
#include "ThrowAim.h"
#include <Xinput.h>
#include <mutex>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <algorithm>
#include <cstdlib>
namespace GamepadBridge {
namespace {
std::mutex guard;
std::atomic<bool> interactionVisualEnabled{};
std::atomic<uint64_t> interactionVisualPress{};
std::atomic<uint64_t> interactionPress{};
// `visual` drives the right hand's finger curl; a left-hand Interact must not curl it.
void PublishInteraction(bool down,uint64_t now,bool visual=true){
    interactionPress.store(down?now:0,std::memory_order_relaxed);
    if(interactionVisualEnabled.load(std::memory_order_relaxed))interactionVisualPress.store(down&&visual?now:0,std::memory_order_relaxed);
}
// Left grip: decided when the press starts and kept until release. While the left
// hand owns the prompt it is Interact, exactly like the Interact button; otherwise
// it does whatever the layout says. If Interact opens a screen with the grip still
// down, the rest of that press is swallowed so it cannot act as a new one.
enum class LeftGrip {Normal,Interact,Swallow};
bool leftGripInteract{},leftGripWeapon{},leftGripWait{},leftGripPrevious{};
LeftCarry::TriggerSwap carryTriggers;
std::atomic<bool> lastInteractLeft{};bool interactPrevious{};
LeftGrip LeftGripStep(bool on,bool wheel) {
    if(leftGripWait){if(!on)leftGripWait=false;leftGripPrevious=on;return LeftGrip::Swallow;}
    if(on&&!leftGripPrevious&&!wheel) {
        // A prompt comes first, so holding the weapon never makes interaction harder.
        leftGripInteract=EngineCamera::LeftHandOwnsPrompt();
        leftGripWeapon=!leftGripInteract&&EngineCamera::LeftHandNearWeapon();
        if(leftGripWeapon)EngineCamera::SetLeftHandOnWeapon(true);
    }
    leftGripPrevious=on;
    if(!on) {
        const bool was=leftGripInteract||leftGripWeapon;
        if(leftGripWeapon)EngineCamera::SetLeftHandOnWeapon(false);
        leftGripInteract=leftGripWeapon=false;
        return was?LeftGrip::Swallow:LeftGrip::Normal;
    }
    return leftGripInteract?LeftGrip::Interact:leftGripWeapon?LeftGrip::Swallow:LeftGrip::Normal;
}
void LeftGripOnScreen(XINPUT_GAMEPAD& pad) {
    const bool on=(pad.wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER)!=0;
    if(leftGripInteract){leftGripInteract=false;leftGripWait=true;}
    if(leftGripWeapon){leftGripWeapon=false;leftGripWait=true;EngineCamera::SetLeftHandOnWeapon(false);}
    if(leftGripWait) {
        if(on)pad.wButtons&=~XINPUT_GAMEPAD_LEFT_SHOULDER;
        else leftGripWait=false;
    }
    leftGripPrevious=on;
}
Transport::Header* channel{};
Transport::Tracking latest{};
using GetState=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
using SetState=DWORD(WINAPI*)(DWORD,XINPUT_VIBRATION*);
GetState originalGet{};SetState originalSet{};
bool enabled{},configured{},installed{},connected{};
DWORD packet{};XINPUT_GAMEPAD previous{};
uint64_t polls{},changes{},retryTick{};
bool calibrationInputBlocked{},menuBlocked{};
SettingsMenu::InputGate menuGate;
wchar_t configPath[MAX_PATH]{};
int imageBrightness{},imageContrast{}; // [VR] Brightness and Contrast

void InputLog(const char* format,...) {
    FILE* f{};if(fopen_s(&f,"DeusExHRVR-input.log","a"))return;
    va_list args;va_start(args,format);vfprintf(f,format,args);va_end(args);
    fputc('\n',f);fclose(f);
}

// ---- Local patch: configurable button layout ([Buttons] in DeusExHRVR.ini) ----
// Targets: low 16 bits are XInput button bits; these flags select analog triggers.
constexpr uint32_t TargetLT=1u<<16, TargetRT=1u<<17;
// QuickSave presses the game's fixed quicksave key, F5, which has no gamepad
// binding. The key is held while the target is, and only while the game is in front.
constexpr uint32_t TargetQuickSave=1u<<18;
constexpr uint32_t TargetHandCalibrate=1u<<19;
bool quickSaveWanted{},quickSaveDown{};uint64_t quickSaveSyncAt{};
struct ButtonSource {const wchar_t* key;uint16_t bit;};
// "bit" is what the stock mapper emits for that physical control.
constexpr ButtonSource buttonSources[]={
    {L"RightA",XINPUT_GAMEPAD_A},{L"RightB",XINPUT_GAMEPAD_Y},
    {L"LeftX",XINPUT_GAMEPAD_X},{L"LeftY",XINPUT_GAMEPAD_B},
    {L"LeftGrip",XINPUT_GAMEPAD_LEFT_SHOULDER},{L"RightGrip",XINPUT_GAMEPAD_RIGHT_SHOULDER},
    {L"LeftStickClick",XINPUT_GAMEPAD_LEFT_THUMB},{L"RightStickClick",XINPUT_GAMEPAD_RIGHT_THUMB},
};
constexpr size_t buttonCount=sizeof(buttonSources)/sizeof(buttonSources[0]);
uint32_t buttonTarget[buttonCount]{};
// Optional hold-to-activate per input (<Key>HoldMs): the output starts only after
// the input has been held that long, so accidental taps send nothing.
int buttonHoldMs[buttonCount]{};uint64_t buttonDownAt[buttonCount]{};bool buttonWasDown[buttonCount]{};
// With a hold delay, <Key>Tap sends another button when the input is released
// before the delay (e.g. tap = jump, hold = grenade). Sent as a 120 ms pulse.
uint32_t buttonTapTarget[buttonCount]{};uint64_t buttonTapUntil[buttonCount]{};
// Grenade landing marker: an input whose quick press throws (tap target RB, or
// RB itself) and whose hold does nothing else becomes hold-to-aim: held past
// its hold delay it shows the marker, and any release throws (one RB pulse).
ThrowAim::HoldThrow throwHold[buttonCount]{};
// A press that starts while a thrown remote device waits for its detonate input
// sends RB from its first moment until release, with no hold delay (the game's
// own "hold RB" already needs about a second), whatever the layout's tap/hold
// targets. Decided once per press, so detonating while still holding never
// turns the rest of that press into a throw.
bool plainPress[buttonCount]{};
bool ThrowInput(size_t i) {
    constexpr uint32_t rb=XINPUT_GAMEPAD_RIGHT_SHOULDER;
    return EngineCamera::ThrowMarkerEnabled() && (buttonTapTarget[i]==rb||buttonTarget[i]==rb) &&
           (buttonTarget[i]==0||buttonTarget[i]==rb);
}
void ResetThrowHold(){for(auto& h:throwHold)h.Reset();EngineCamera::SetThrowAiming(false);}
uint32_t leftTriggerTarget=TargetLT,rightTriggerTarget=TargetRT;
bool remapActive{};
// [ScreenButtons]: layout for everything that is not gameplay (menus, readers,
// terminals, hacking, videos, game over, loading screens).
uint32_t screenTarget[buttonCount]{};uint32_t screenLeftTrigger=TargetLT,screenRightTrigger=TargetRT;
bool screenRemapActive{};
uint32_t stickUpTarget{},stickDownTarget{}; // right stick up/down during gameplay (snap or smooth turn)

uint32_t ParseTarget(const wchar_t* key,uint32_t fallback,const wchar_t* section=L"Buttons") {
    wchar_t value[32]{};GetPrivateProfileStringW(section,key,L"",value,32,configPath);
    if(!*value)return fallback;
    struct Name {const wchar_t* name;uint32_t target;};
    static constexpr Name names[]={
        {L"A",XINPUT_GAMEPAD_A},{L"B",XINPUT_GAMEPAD_B},{L"X",XINPUT_GAMEPAD_X},{L"Y",XINPUT_GAMEPAD_Y},
        {L"LB",XINPUT_GAMEPAD_LEFT_SHOULDER},{L"RB",XINPUT_GAMEPAD_RIGHT_SHOULDER},
        {L"LS",XINPUT_GAMEPAD_LEFT_THUMB},{L"L3",XINPUT_GAMEPAD_LEFT_THUMB},
        {L"RS",XINPUT_GAMEPAD_RIGHT_THUMB},{L"R3",XINPUT_GAMEPAD_RIGHT_THUMB},
        {L"LT",TargetLT},{L"RT",TargetRT},
        {L"Back",XINPUT_GAMEPAD_BACK},{L"Start",XINPUT_GAMEPAD_START},
        {L"DPadUp",XINPUT_GAMEPAD_DPAD_UP},{L"DPadDown",XINPUT_GAMEPAD_DPAD_DOWN},
        {L"DPadLeft",XINPUT_GAMEPAD_DPAD_LEFT},{L"DPadRight",XINPUT_GAMEPAD_DPAD_RIGHT},
        {L"QuickSave",TargetQuickSave},{L"HandCalibrate",TargetHandCalibrate},{L"None",0},
    };
    for(const auto& n:names)if(!_wcsicmp(value,n.name))return n.target;
    InputLog("[%ls] %ls=%ls not recognized; keeping default",section,key,value);
    return fallback;
}
void LoadButtons() {
    remapActive=screenRemapActive=false;
    for(size_t i=0;i<buttonCount;i++) {
        buttonTarget[i]=ParseTarget(buttonSources[i].key,buttonSources[i].bit);
        if(buttonTarget[i]!=buttonSources[i].bit)remapActive=true;
        wchar_t key[48]{};swprintf_s(key,L"%lsHoldMs",buttonSources[i].key);
        buttonHoldMs[i]=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Buttons",key,0,configPath)),0,5000);
        if(buttonHoldMs[i]>0){remapActive=true;InputLog("hold %ls=%dms",key,buttonHoldMs[i]);}
        swprintf_s(key,L"%lsTap",buttonSources[i].key);
        buttonTapTarget[i]=buttonHoldMs[i]>0?ParseTarget(key,0):0;
        if(buttonTapTarget[i])InputLog("tap %ls=%x",key,buttonTapTarget[i]);
    }
    leftTriggerTarget=ParseTarget(L"LeftTrigger",TargetLT);
    rightTriggerTarget=ParseTarget(L"RightTrigger",TargetRT);
    if(leftTriggerTarget!=TargetLT || rightTriggerTarget!=TargetRT)remapActive=true;
    for(size_t i=0;i<buttonCount;i++) {
        screenTarget[i]=ParseTarget(buttonSources[i].key,buttonSources[i].bit,L"ScreenButtons");
        if(screenTarget[i]!=buttonSources[i].bit)screenRemapActive=true;
    }
    screenLeftTrigger=ParseTarget(L"LeftTrigger",TargetLT,L"ScreenButtons");
    screenRightTrigger=ParseTarget(L"RightTrigger",TargetRT,L"ScreenButtons");
    if(screenLeftTrigger!=TargetLT || screenRightTrigger!=TargetRT)screenRemapActive=true;
    InputLog("screen buttons remap=%d RightA=%x RightB=%x LeftX=%x LeftY=%x LeftTrigger=%x RightTrigger=%x",
        screenRemapActive,screenTarget[0],screenTarget[1],screenTarget[2],screenTarget[3],screenLeftTrigger,screenRightTrigger);
    stickUpTarget=ParseTarget(L"RightStickUp",0);
    stickDownTarget=ParseTarget(L"RightStickDown",0);
    InputLog("right stick up=%x down=%x",stickUpTarget,stickDownTarget);
    InputLog("buttons remap=%d RightA=%x RightB=%x LeftX=%x LeftY=%x LeftGrip=%x RightGrip=%x LeftStickClick=%x RightStickClick=%x LeftTrigger=%x RightTrigger=%x",
        remapActive,buttonTarget[0],buttonTarget[1],buttonTarget[2],buttonTarget[3],buttonTarget[4],buttonTarget[5],
        buttonTarget[6],buttonTarget[7],leftTriggerTarget,rightTriggerTarget);
}
void Emit(XINPUT_GAMEPAD& out,uint32_t target,bool pressed,BYTE analog) {
    if(pressed && (target&0xffff))out.wButtons|=static_cast<WORD>(target&0xffff);
    if(pressed && (target&TargetQuickSave))quickSaveWanted=true;
    // Idempotent while already armed, so a held button cannot starve the hold.
    if(pressed && (target&TargetHandCalibrate))EngineCamera::ArmHandCalibration();
    if(target&TargetLT)out.bLeftTrigger=std::max(out.bLeftTrigger,analog);
    if(target&TargetRT)out.bRightTrigger=std::max(out.bRightTrigger,analog);
}
void ApplyScreenButtons(XINPUT_GAMEPAD& pad) {
    if(!screenRemapActive)return;
    const XINPUT_GAMEPAD in=pad;
    WORD owned=0;for(const auto& s:buttonSources)owned|=s.bit;
    pad.wButtons=in.wButtons&~owned;
    pad.bLeftTrigger=pad.bRightTrigger=0;
    for(size_t i=0;i<buttonCount;i++) {
        bool on=(in.wButtons&buttonSources[i].bit)!=0;
        Emit(pad,screenTarget[i],on,on?BYTE(255):BYTE(0));
    }
    Emit(pad,screenLeftTrigger,in.bLeftTrigger>127,in.bLeftTrigger);
    Emit(pad,screenRightTrigger,in.bRightTrigger>127,in.bRightTrigger);
}
void ApplyButtons(XINPUT_GAMEPAD& pad) {
    // Gameplay (and scoped aiming) use [Buttons]; everything else uses
    // [ScreenButtons]: menus (including the in-game menu), the e-reader and news
    // reader, terminals, hacking, videos, game over, and any state not recognized
    // as gameplay (loading screens, cutscenes). A reader sits over live gameplay,
    // so it takes [ScreenButtons] even though snap turn still works. A wheel screen
    // (bit 128) is the reverse: it pauses the game with the camera stopped, which
    // looks like a loading screen, but the layout must stay on [Buttons] or the
    // button being held to show the wheel would stop being held. The wheel's own
    // prompt asks for a left stick click to deselect the consumable, and nothing
    // else in the wheel uses that click, so it is sent as the game expects it
    // whatever [Buttons] maps it to; otherwise the consumable stays selected and is
    // used along with the weapon.
    float gameplayYaw=0;
    unsigned allReasons=EngineCamera::CurrentScreenReasons();
    bool gameplay=(EngineCamera::SnapTurnView(gameplayYaw) && !(allReasons&64)) || (allReasons&128)!=0;
    unsigned reasons=gameplay?0:allReasons;
    if(!gameplay && reasons!=16) {
        for(size_t i=0;i<buttonCount;i++){buttonWasDown[i]=false;buttonTapUntil[i]=0;}ResetThrowHold();
        LeftGripOnScreen(pad);
        ApplyScreenButtons(pad);
        return;
    }
    if(!remapActive){
        ResetThrowHold();
        const auto grip=LeftGripStep((pad.wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER)!=0,(allReasons&128)!=0);
        if(grip!=LeftGrip::Normal)pad.wButtons&=~XINPUT_GAMEPAD_LEFT_SHOULDER;
        if(grip==LeftGrip::Interact)pad.wButtons|=XINPUT_GAMEPAD_X;
        carryTriggers.Apply(EngineCamera::LeftHandCarrying()&&!(allReasons&128),pad.bLeftTrigger,pad.bRightTrigger);
        return;
    }
    const XINPUT_GAMEPAD in=pad;
    WORD owned=0;for(const auto& s:buttonSources)owned|=s.bit;
    pad.wButtons=in.wButtons&~owned; // D-pad gestures, Back and Start pulses stay as-is
    pad.bLeftTrigger=pad.bRightTrigger=0;
    auto now=GetTickCount64();
    bool wheel=(allReasons&128)!=0;
    bool aiming=false;
    for(size_t i=0;i<buttonCount;i++) {
        bool on=(in.wButtons&buttonSources[i].bit)!=0;
        if(buttonSources[i].bit==XINPUT_GAMEPAD_LEFT_SHOULDER) {
            const auto grip=LeftGripStep(on,wheel);
            if(grip!=LeftGrip::Normal) {
                if(grip==LeftGrip::Interact)Emit(pad,XINPUT_GAMEPAD_X,true,BYTE(255));
                buttonWasDown[i]=on;buttonTapUntil[i]=0;plainPress[i]=false;
                continue;
            }
        }
        if(on && !buttonWasDown[i])plainPress[i]=EngineCamera::DetonateReady();
        if(!wheel && ThrowInput(i) && plainPress[i]) {
            if(on && !buttonWasDown[i])buttonDownAt[i]=now;
            if(!on && buttonWasDown[i]){plainPress[i]=false;EngineCamera::NoteDetonatePress(now-buttonDownAt[i]);}
            buttonWasDown[i]=on;buttonTapUntil[i]=0;
            Emit(pad,XINPUT_GAMEPAD_RIGHT_SHOULDER,on,on?BYTE(255):BYTE(0));
            continue;
        }
        if(!wheel && ThrowInput(i)) {
            // A quick press throws only if the layout makes a tap throw (RightATap=RB);
            // with RightA=RB and a hold delay, a tap still does nothing.
            auto r=throwHold[i].Update(on,now,uint64_t(buttonHoldMs[i]),buttonTapTarget[i]==XINPUT_GAMEPAD_RIGHT_SHOULDER);
            aiming|=r.aiming;
            if(r.heldMs)EngineCamera::NoteThrowRelease(r.heldMs);
            if(r.throwPulse)Emit(pad,XINPUT_GAMEPAD_RIGHT_SHOULDER,true,BYTE(255));
            buttonWasDown[i]=on;buttonTapUntil[i]=0;
            continue;
        }
        if(wheel && buttonSources[i].bit==XINPUT_GAMEPAD_LEFT_THUMB) { // the wheel's deselect
            buttonWasDown[i]=on;buttonTapUntil[i]=0;
            Emit(pad,XINPUT_GAMEPAD_LEFT_THUMB,on,on?BYTE(255):BYTE(0));
            continue;
        }
        if(on && !buttonWasDown[i])buttonDownAt[i]=now;
        if(!on && buttonWasDown[i])plainPress[i]=false;
        if(!on && buttonWasDown[i] && buttonTapTarget[i] && now-buttonDownAt[i]<uint64_t(buttonHoldMs[i]))
            buttonTapUntil[i]=now+120; // released before the hold delay: it was a tap
        buttonWasDown[i]=on;
        if(now<buttonTapUntil[i])Emit(pad,buttonTapTarget[i],true,BYTE(255));
        if(on && buttonHoldMs[i]>0 && now-buttonDownAt[i]<uint64_t(buttonHoldMs[i]))on=false;
        Emit(pad,buttonTarget[i],on,on?BYTE(255):BYTE(0));
    }
    EngineCamera::SetThrowAiming(aiming);
    BYTE leftValue=in.bLeftTrigger,rightValue=in.bRightTrigger;
    carryTriggers.Apply(EngineCamera::LeftHandCarrying()&&!wheel,leftValue,rightValue);
    Emit(pad,leftTriggerTarget,leftValue>127,leftValue);
    Emit(pad,rightTriggerTarget,rightValue>127,rightValue);
}

// ---- Local patch: snap turn via one injected relative mouse move ----
// Direct native-angle snaps: no mouse input and no left-stick suppression.
bool snapEnabled{},snapArmed{};
float snapDegrees=30.f;
double snapCounts=300.; // Legacy settings serialization only; never used to turn.
uint64_t snapCooldown{};
void LoadSnap() {
    snapEnabled=GetPrivateProfileIntW(L"VR",L"SnapTurn",0,configPath)!=0;
    EngineCamera::SetDirectSnapEnabled(snapEnabled);
    wchar_t text[32]{};
    GetPrivateProfileStringW(L"VR",L"SnapTurnDegrees",L"30",text,32,configPath);
    float degrees=static_cast<float>(_wtof(text));
    if(std::isfinite(degrees) && degrees>=5 && degrees<=180)snapDegrees=degrees;
    GetPrivateProfileStringW(L"VR",L"SnapTurnMouseCounts",L"300",text,32,configPath);
    double counts=_wtof(text);
    if(std::isfinite(counts) && counts>=1 && counts<=20000)snapCounts=counts;
    InputLog("snapTurn directNative enabled=%d degrees=%.1f",snapEnabled,snapDegrees);
}
bool GameInForeground() {
    DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId();
}
// Presses or releases F5 to match the QuickSave target (by scan code, which the
// game's keyboard input and window messages both see).
void SyncQuickSave() {
    quickSaveSyncAt=GetTickCount64();
    bool want=quickSaveWanted && GameInForeground();
    quickSaveWanted=false;
    if(want==quickSaveDown)return;
    INPUT input{};input.type=INPUT_KEYBOARD;input.ki.wScan=0x3f; // F5
    input.ki.dwFlags=KEYEVENTF_SCANCODE|(want?0:KEYEVENTF_KEYUP);
    bool sent=SendInput(1,&input,sizeof(input))==1;
    if(sent || !want)quickSaveDown=want;
    InputLog("quickSave F5 %s%s",want?"down":"up",sent?"":" (SendInput failed)");
}
void SnapTurn(XINPUT_GAMEPAD& pad) {
    auto now=GetTickCount64();float yaw=0;
    if(!EngineCamera::SnapTurnView(yaw) || !GameInForeground()){
        snapArmed=false;EngineCamera::CancelDirectSnap();return;
    }
    double rx=pad.sThumbRX/32767.,ry=pad.sThumbRY/32767.;
    double magnitude=std::hypot(rx,ry);
    pad.sThumbRX=pad.sThumbRY=0;
    if(magnitude>=.6 && std::abs(ry)>=magnitude*.866){
        auto target=ry>0?stickUpTarget:stickDownTarget;
        if(target)Emit(pad,target,true,BYTE(255));
    }
    // Cover aims with the controller (November 16). The right stick keeps its
    // first-person up/down buttons and does not aim; the game cannot apply a
    // direct snap outside first person, so none is queued, and the stick must
    // return to neutral after cover before the next snap.
    if(EngineCamera::NativeCoverControls()){snapArmed=false;EngineCamera::CancelDirectSnap();return;}
    if(magnitude<.35){snapArmed=true;return;}
    if(!snapArmed || now<snapCooldown || magnitude<.6 || std::abs(rx)<magnitude*.5)return;
    snapArmed=false;snapCooldown=now+60;
    EngineCamera::RequestDirectSnap(rx>0?snapDegrees:-snapDegrees);
}

// Without snap turn the right stick normally reaches the game untouched. When
// RightStickUp or RightStickDown is set, gameplay keeps the horizontal axis for
// smooth turning and gives the vertical axis to those buttons, with the same
// 30-degree cone as snap turn. A push inside the cone also holds the turn, so a
// jump or crouch doesn't drift the view.
void StickButtons(XINPUT_GAMEPAD& pad) {
    float yaw=0;
    if(!EngineCamera::SnapTurnView(yaw))return; // menus/terminals/scope keep the native right stick
    double rx=pad.sThumbRX/32767.,ry=pad.sThumbRY/32767.;
    double magnitude=std::hypot(rx,ry);
    pad.sThumbRY=0;
    if(magnitude>=.6 && std::abs(ry)>=magnitude*.866) {
        pad.sThumbRX=0;
        auto target=ry>0?stickUpTarget:stickDownTarget;
        if(target)Emit(pad,target,true,BYTE(255));
    }
}
bool Read(XINPUT_GAMEPAD& pad) {
    if(!enabled || !channel){PublishInteraction(false,0);return false;}
    Transport::Tracking sample{};
    if(Transport::ReadTracking(channel,sample))latest=sample;
    auto now=GetTickCount64();
    bool fresh=latest.tick && now>=latest.tick && now-latest.tick<250;
    Transport::ReadMenu(channel,menuGate.latest);
    menuBlocked=menuGate.Blocked(now);
    if(menuBlocked){
        PublishInteraction(false,0);
        pad={};quickSaveWanted=false;
        for(size_t i=0;i<buttonCount;i++){buttonWasDown[i]=false;buttonTapUntil[i]=0;}ResetThrowHold();
        snapArmed=false;EngineCamera::CancelDirectSnap();
        connected=true;return true;
    }
    if(fresh && latest.gamepad.valid) {
        static_assert(sizeof(pad)==sizeof(Transport::Gamepad)-sizeof(uint32_t));
        std::memcpy(&pad,&latest.gamepad.buttons,sizeof(pad));connected=true;
        if(latest.controllerButtons==3)calibrationInputBlocked=true;
        if(calibrationInputBlocked){
            PublishInteraction(false,0);
            pad={};
            for(size_t i=0;i<buttonCount;i++){buttonWasDown[i]=false;buttonTapUntil[i]=0;}ResetThrowHold();
            snapArmed=false;EngineCamera::CancelDirectSnap();
            if(!latest.controllerButtons)calibrationInputBlocked=false;
            return true;
        }
        ApplyButtons(pad);
        if(EngineCamera::ImmersiveScopeButton())pad.wButtons|=XINPUT_GAMEPAD_RIGHT_THUMB;
        if(snapEnabled)SnapTurn(pad);
        else if(stickUpTarget || stickDownTarget)StickButtons(pad);
    }
    if(!fresh || !latest.gamepad.valid || !GameInForeground()){snapArmed=false;EngineCamera::CancelDirectSnap();}
    {
        const bool interact=fresh&&latest.gamepad.valid&&(pad.wButtons&XINPUT_GAMEPAD_X)!=0;
        if(interact&&!interactPrevious)lastInteractLeft=leftGripInteract;
        interactPrevious=interact;
    }
    EngineCamera::SetGameSprintHeld(fresh&&latest.gamepad.valid&&(pad.wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER)!=0);
    PublishInteraction(fresh && latest.gamepad.valid && (pad.wButtons&XINPUT_GAMEPAD_X)!=0,now,!leftGripInteract);
    // Keep the device connected after first activation, but release every
    // button/axis on focus loss, sleeping controllers or a stalled companion.
    return connected;
}
SHORT Stronger(SHORT a,SHORT b){return std::abs(int(a))>=std::abs(int(b))?a:b;}
DWORD WINAPI GetHook(DWORD index,XINPUT_STATE* state) {
    DWORD result=originalGet(index,state);
    if(index || !state)return result;
    std::lock_guard lock(guard);XINPUT_GAMEPAD pad{};
    bool active=Read(pad);
    SyncQuickSave(); // also releases F5 when input stops or the game loses focus
    if(!active)return result;
    ++polls;
    if(result==ERROR_SUCCESS) {
        const auto& real=state->Gamepad;
        pad.wButtons|=real.wButtons;
        pad.bLeftTrigger=std::max(pad.bLeftTrigger,real.bLeftTrigger);
        pad.bRightTrigger=std::max(pad.bRightTrigger,real.bRightTrigger);
        pad.sThumbLX=Stronger(pad.sThumbLX,real.sThumbLX);pad.sThumbLY=Stronger(pad.sThumbLY,real.sThumbLY);
        pad.sThumbRX=Stronger(pad.sThumbRX,real.sThumbRX);pad.sThumbRY=Stronger(pad.sThumbRY,real.sThumbRY);
    }
    const XINPUT_GAMEPAD real=result==ERROR_SUCCESS?state->Gamepad:XINPUT_GAMEPAD{};
    if(menuGate.Physical(menuBlocked,real))pad={}; // includes physical pad release after closing
    if(std::memcmp(&pad,&previous,sizeof(pad))){previous=pad;++packet;++changes;}
    state->Gamepad=pad;state->dwPacketNumber=packet;
    return ERROR_SUCCESS;
}
DWORD WINAPI SetHook(DWORD index,XINPUT_VIBRATION* vibration) {
    DWORD result=originalSet(index,vibration);
    if(index || !vibration || result==ERROR_SUCCESS)return result;
    std::lock_guard lock(guard);
    return connected && enabled && channel?ERROR_SUCCESS:result; // Native rumble has no haptic mapping yet.
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
    InputLog("Xbox input bridge installed: slot 0, motionControls=1");
}
}
void EnableInteractionVisual(bool on){interactionVisualEnabled=on;interactionVisualPress=0;}
bool LastInteractWasLeft(){return lastInteractLeft.load();}
bool InteractionHeld(uint64_t now){auto tick=interactionPress.load(std::memory_order_relaxed);return tick&&now>=tick&&now-tick<250;}
bool InteractionVisualHeld(uint64_t now){auto tick=interactionVisualPress.load(std::memory_order_relaxed);return tick&&now>=tick&&now-tick<250;}
void SetChannel(Transport::Header* header) {
    std::lock_guard lock(guard);
    NativeMenuPointer::Update({},false,GetTickCount64());
    if(!configured) {
        GetFullPathNameW(L"DeusExHRVR.ini",MAX_PATH,configPath,nullptr);
        imageBrightness=ImageAdjust::Read(L"Brightness",configPath);imageContrast=ImageAdjust::Read(L"Contrast",configPath);
        enabled=DirectionConfig::MotionEnabled(configPath);configured=true;
        if(enabled){LoadButtons();LoadSnap();}
    }
    channel=header;latest={};connected=false;menuBlocked=false;menuGate={};
}
void MenuTick(unsigned screenReasons) {
    std::lock_guard lock(guard);
    if(!Transport::LockMenu(channel)){NativeMenuPointer::Update({},false,GetTickCount64());return;}
    auto& m=channel->menu;auto now=GetTickCount64();
    // Bit 2 is ScreenMode::Menu (TitleMenu/PauseMenu), not the inventory bit 32.
    m.eligible=SettingsMenu::Eligible(screenReasons);m.gameTick=now;
    m.dialogue=EngineCamera::DialogueEnabled();m.dialogueAvailable=EngineCamera::DialogueAvailable();
    m.handHud=EngineCamera::HandHudConfigured()?2:1;m.activeHandHud=EngineCamera::HandHudActive()?2:1;
    m.throwAim=EngineCamera::ThrowAimSetting();m.throwMarker=EngineCamera::ThrowMarkerSetting();
    m.brightness=ImageAdjust::Encode(imageBrightness);m.contrast=ImageAdjust::Encode(imageContrast);
    m.quickBar=EngineCamera::QuickBarSetting();m.crosshair=EngineCamera::CrosshairSetting();m.handPointers=EngineCamera::HandPointerSetting();m.coverFollow=EngineCamera::CoverFollowSetting();m.immersiveScope=EngineCamera::ImmersiveScopeSetting();
    m.response=EngineCamera::MovementResponse();
    m.interaction=EngineCamera::InteractionAim();
    m.worldScale=EngineCamera::ConfiguredWorldScale();m.activeWorldScale=EngineCamera::ActiveWorldScale();
    m.scopeZoom=EngineCamera::ScopeMagnification();
    m.snapEnabled=snapEnabled;m.degrees=snapDegrees;m.movement=EngineCamera::MovementDirection();
    if(m.recenterRequest!=m.recenterAck&&Transport::Fresh(m.hostTick,now)){
        m.recenterAck=m.recenterRequest;
        if(m.eligible&&m.blocked)EngineCamera::Recenter();
    }
    for(size_t i=0;i<buttonCount;++i){m.bindings[i]=buttonTarget[i];m.holdMs[i]=uint32_t(buttonHoldMs[i]);m.tapTargets[i]=buttonTapTarget[i];}
    m.bindings[8]=leftTriggerTarget;m.bindings[9]=rightTriggerTarget;
    for(size_t i=0;i<buttonCount;++i)m.bindings[10+i]=buttonTapTarget[i];
    m.bindings[18]=stickUpTarget;m.bindings[19]=stickDownTarget;
    if(m.request!=m.acknowledged && Transport::Fresh(m.hostTick,now)) {
        m.acknowledged=m.request;
        if(m.eligible&&m.blocked&&ImageAdjust::ValidWire(m.requestedBrightness)&&ImageAdjust::ValidWire(m.requestedContrast)&&m.requestedQuickBar<=2&&(!m.requestedQuickBar||m.quickBar)&&m.requestedCrosshair<=4&&m.requestedHandPointers<=4&&(!m.requestedHandPointers||m.handPointers)&&(m.requestedScopeZoom==0||(std::isfinite(m.requestedScopeZoom)&&m.requestedScopeZoom>=1&&m.requestedScopeZoom<=12))&&m.requestedDialogue<=1&&(!m.requestedDialogue||m.dialogueAvailable)&&m.requestedHandHud<=2&&m.requestedThrowAim<=4&&(!m.requestedThrowAim||m.throwAim)&&m.requestedThrowMarker<=2&&(!m.requestedThrowMarker||m.throwMarker)&&m.requestedCoverFollow<=2&&(!m.requestedCoverFollow||m.coverFollow)&&m.requestedImmersiveScope<=2&&(!m.requestedImmersiveScope||m.immersiveScope)&&std::isfinite(m.requestedWorldScale)&&m.requestedWorldScale>=10&&m.requestedWorldScale<=1000&&m.resetBindings<=1&&m.requestedInteraction<=3&&(m.requestedInteraction==1||m.requestedInteraction==2||m.requestedInteraction==m.interaction)&&m.requestedResponse<=2&&m.requestedSnap<=1&&m.requestedMovement<=3&&(m.requestedMovement!=0||m.movement==0)&&ButtonBindings::Valid(m.requestedBindings,m.bindingMask)&&SettingsMenu::ApplyAngle(m.requestedDegrees,snapDegrees,snapCounts)) {
            snapArmed=false;EngineCamera::CancelDirectSnap();
            EngineCamera::SetDialogueEnabled(m.requestedDialogue!=0);m.dialogue=m.requestedDialogue;
            if(m.requestedHandHud){EngineCamera::SetHandHudConfigured(m.requestedHandHud==2);m.handHud=m.requestedHandHud;}
            // Menu tabs (transport 24): each applies from the next frame. A throw
            // change drops any half-made hold so the next press starts clean.
            if(m.requestedThrowAim){EngineCamera::SetThrowAim(m.requestedThrowAim-1);m.throwAim=m.requestedThrowAim;}
            if(m.requestedThrowMarker){EngineCamera::SetThrowMarker(m.requestedThrowMarker==2);m.throwMarker=m.requestedThrowMarker;}
            if(m.requestedThrowAim||m.requestedThrowMarker)ResetThrowHold();
            if(m.requestedCoverFollow){EngineCamera::SetCoverFollow(m.requestedCoverFollow==2);m.coverFollow=m.requestedCoverFollow;}
            if(m.requestedImmersiveScope){EngineCamera::SetImmersiveScope(m.requestedImmersiveScope==2);m.immersiveScope=m.requestedImmersiveScope;}
            if(m.requestedCrosshair){EngineCamera::SetCrosshair(m.requestedCrosshair);m.crosshair=m.requestedCrosshair;}
            if(m.requestedHandPointers){EngineCamera::SetHandPointers(m.requestedHandPointers);m.handPointers=m.requestedHandPointers;}
            if(m.requestedQuickBar){EngineCamera::SetQuickBar(m.requestedQuickBar==2);m.quickBar=m.requestedQuickBar;}
            if(m.requestedBrightness){imageBrightness=ImageAdjust::Decode(m.requestedBrightness);m.brightness=m.requestedBrightness;}
            if(m.requestedContrast){imageContrast=ImageAdjust::Decode(m.requestedContrast);m.contrast=m.requestedContrast;}
            EngineCamera::SetConfiguredWorldScale(m.requestedWorldScale);m.worldScale=m.requestedWorldScale;
            if(m.requestedScopeZoom!=0){EngineCamera::SetScopeMagnification(m.requestedScopeZoom);m.scopeZoom=m.requestedScopeZoom;}
            EngineCamera::SetInteractionAim(m.requestedInteraction);m.interaction=m.requestedInteraction;
            if(m.resetBindings)for(auto& hold:buttonHoldMs)hold=0;
            EngineCamera::SetMovementResponse(m.requestedResponse);m.response=m.requestedResponse;
            snapEnabled=m.requestedSnap!=0;EngineCamera::SetDirectSnapEnabled(snapEnabled);m.snapEnabled=snapEnabled;
            EngineCamera::SetMovementDirection(m.requestedMovement);m.movement=m.requestedMovement;
            for(size_t i=0;i<ButtonBindings::Count;++i)if(m.bindingMask&(1u<<i))m.bindings[i]=m.requestedBindings[i];
            if(m.bindingMask){
                remapActive=false;
                for(size_t i=0;i<buttonCount;++i){
                    buttonTarget[i]=m.bindings[i];buttonTapTarget[i]=m.bindings[10+i];buttonWasDown[i]=false;buttonTapUntil[i]=buttonDownAt[i]=0;throwHold[i].Reset();
                    if(buttonTarget[i]!=buttonSources[i].bit||buttonHoldMs[i]>0)remapActive=true;
                }
                leftTriggerTarget=m.bindings[8];rightTriggerTarget=m.bindings[9];stickUpTarget=m.bindings[18];stickDownTarget=m.bindings[19];
                if(leftTriggerTarget!=TargetLT||rightTriggerTarget!=TargetRT)remapActive=true;
                quickSaveWanted=false;SyncQuickSave();
            }
            m.degrees=snapDegrees;m.result=1;
            SettingsWriter().Queue(configPath,snapCounts,m.request,snapDegrees,int(snapEnabled),int(m.movement),m.bindings,m.bindingMask,int(m.response),int(m.interaction),m.resetBindings!=0,m.worldScale,int(m.dialogue),m.requestedScopeZoom!=0?m.scopeZoom:-1.f,m.requestedHandHud?int(m.requestedHandHud==2):-1,
                m.requestedThrowAim?int(m.requestedThrowAim)-1:-1,m.requestedThrowMarker?int(m.requestedThrowMarker==2):-1,
                m.requestedCoverFollow?int(m.requestedCoverFollow==2):-1,m.requestedImmersiveScope?int(m.requestedImmersiveScope==2):-1,m.requestedCrosshair?int(m.requestedCrosshair):-1,m.requestedQuickBar?int(m.requestedQuickBar==2):-1,m.requestedBrightness,m.requestedContrast,m.requestedHandPointers?int(m.requestedHandPointers):-1);
        } else m.result=2;
    }
    auto& writer=SettingsWriter();
    if(m.acknowledged&&writer.completed.load()==m.acknowledged&&m.result!=2){
        if(writer.failed.load()==m.acknowledged)m.result=3;
        else {m.saved=m.acknowledged;m.result=0;}
    }
    auto pointer=m;
    Transport::UnlockMenu(channel);
    NativeMenuPointer::Update(pointer,pointer.eligible!=0,now);
}
void OnPresent(bool capture) {
    std::lock_guard lock(guard);Install();
    // If the game stops polling the controller while F5 is held, release it here.
    if(quickSaveDown && GetTickCount64()-quickSaveSyncAt>250){quickSaveWanted=false;SyncQuickSave();}
    if(capture) {
        InputLog("enabled=%d installed=%d connected=%d polls=%llu changes=%llu packet=%lu lastButtons=%04x sticks=%d,%d/%d,%d triggers=%u,%u",
            enabled,installed,connected,polls,changes,packet,previous.wButtons,previous.sThumbLX,previous.sThumbLY,
            previous.sThumbRX,previous.sThumbRY,previous.bLeftTrigger,previous.bRightTrigger);
    }
}
}
