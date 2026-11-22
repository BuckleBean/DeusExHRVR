#pragma once
#include "SharedPair.h"
#include "LiveWorldScale.h"
#include "GamepadMapping.h"
#include "CoverCrosshair.h"
#include <cmath>
#include <algorithm>
#include <string>

namespace SettingsMenu {
constexpr int Width=1600,Height=900;
struct Rect {int x,y,w,h; bool Has(float px,float py) const {return px>=x&&py>=y&&px<x+w&&py<y+h;}};
// Closed VR SETTINGS shortcut: top-right corner of the game screen, smaller than
// before (was {1080,420,410,70}, over the Save/Load details; Mark, Nov 15: the
// strip below the screen was "bad", "Try the top right corner").
// Panel 830 px tall (was 740 from y 90) so the main page rows can breathe; the
// bottom buttons moved down with it on every page (Mark, Nov 16: "a bit cramped").
inline constexpr Rect Entry{1250,30,320,56},Panel{180,40,1240,830};
inline constexpr Rect ListPanel=Panel;
inline constexpr Rect ListSave{940,790,420,60},ListDiscard{490,790,420,60},ListBack{220,790,240,60};
// Main page in three tabs (Mark, Nov 17: the eleven-row page was full, and
// grenade aim, the landing ring, the cover camera and the immersive scope were
// to be added). One draft and one Save & Back cover all three tabs; the tab
// shown is remembered while the game runs. Rows are 52 px tall, 66 px apart
// from y 216, under the tab strip; at most six rows per tab.
inline constexpr int Tabs=3,TabMovement=0,TabControls=1,TabView=2;
inline constexpr Rect Tab(int i){return {240+i*380,128,360,56};}
inline int TabAt(float x,float y){for(int i=0;i<Tabs;++i)if(Tab(i).Has(x,y))return i;return -1;}
inline constexpr int MainRowY(int i){return 216+i*66;}
inline constexpr Rect MainRow(int i){return {760,MainRowY(i),530,52};}
inline constexpr int StatusY=MainRowY(6)+20; // status line, above the bottom buttons
// The menu's own scope zoom range. Below 3x the game's black scope surround no
// longer covers the view (Mark, Nov 16, Quest 3), so the menu stops at 3x; the
// INI still accepts 1-12, and a lower INI value is shown as it is.
inline constexpr float ZoomMin=3.f,ZoomMax=12.f,ZoomStep=.5f;
// Movement tab.
inline constexpr Rect TurnMode=MainRow(0);
inline constexpr Rect Minus{760,MainRowY(1),100,52},Plus{1190,MainRowY(1),100,52};
inline constexpr Rect Movement=MainRow(2),Response=MainRow(3),WorldScale=MainRow(4),Recenter=MainRow(5);
// Controls tab. Grenade aim cycles RIGHT HAND, LEFT HAND, HEAD, DEFAULT (the
// game's own throw); the landing ring needs a hand or the head.
inline constexpr Rect Interaction=MainRow(0),ThrowAimRow=MainRow(1),ThrowMarkerRow=MainRow(2),Remap=MainRow(3);
// View tab. Scope zoom 0.5x steps, menu range 3-12 (Mark, Nov 16).
inline constexpr Rect HandHud=MainRow(0),ScopeRow=MainRow(1);
inline constexpr Rect ZoomMinus{760,MainRowY(2),100,52},ZoomPlus{1190,MainRowY(2),100,52};
inline constexpr Rect CoverFollowRow=MainRow(3),Dialogue=MainRow(4),CrosshairRow=MainRow(5);
// Next grenade aim: transport values (Source + 1) 3 right, 4 left, 2 head, 1 game.
inline constexpr uint32_t NextThrowAim(uint32_t v){return v==3?4:v==4?2:v==2?1:3;}
inline constexpr Rect ResetBindings{1040,110,320,45};
inline constexpr Rect ScaleMinus{440,330,100,70},ScalePlus{1060,330,100,70},ScaleReset{565,470,470,60};
inline constexpr Rect PageUp{1320,220,60,60},PageDown{1320,620,60,60};
inline constexpr Rect Confirm=ListSave,Cancel=ListBack;
inline constexpr int Rows=8,Actions=18;
inline Rect Row(int index){return {760,220+index*57,530,49};}
inline std::wstring ControlName(size_t slot,const Transport::MenuState& game){
    if(slot>=18)return slot==18?L"Right stick up":L"Right stick down";
    if(slot>=10)return std::wstring(ButtonBindings::Labels[slot-10])+L" (tap)";
    return std::wstring(ButtonBindings::Labels[slot])+(slot<8&&game.holdMs[slot]?L" (hold)":L"");
}
inline std::wstring Assigned(uint32_t target,const ButtonBindings::Layout& layout,const Transport::MenuState& game){
    std::wstring text;
    for(size_t i=0;i<ButtonBindings::Count;++i)if(layout[i]==target){
        if(!text.empty())text+=L", ";text+=ControlName(i,game);
    }
    if(!text.empty())return text;
    if(target==XINPUT_GAMEPAD_BACK)return L"Left menu tap (fixed)";
    if(target==XINPUT_GAMEPAD_START)return L"Left menu hold (fixed)";
    if(target==XINPUT_GAMEPAD_DPAD_UP)return L"Right click + left stick up (fixed)";
    if(target==XINPUT_GAMEPAD_DPAD_DOWN)return L"Right click + left stick down (fixed)";
    if(target==XINPUT_GAMEPAD_DPAD_LEFT)return L"Right click + left stick left (fixed)";
    if(target==XINPUT_GAMEPAD_DPAD_RIGHT)return L"Right click + left stick right (fixed)";
    return L"Unassigned";
}
// Raw controller inputs, before gameplay mapping or menu suppression.
inline uint32_t Presses(const GamepadMapping::Input& in){
    uint32_t mask=0;const bool on[]={in.a,in.b,in.x,in.y,in.leftGrip>.5f,in.rightGrip>.5f,in.leftClick,in.rightClick,in.leftTrigger>.5f,in.rightTrigger>.5f};
    for(int i=0;i<10;++i)if(on[i])mask|=1u<<i;
    if(std::abs(in.rightX)<.3f){if(in.rightY>.6f)mask|=1u<<18;if(in.rightY<-.6f)mask|=1u<<19;}
    return mask;
}
inline constexpr Rect Back=ListBack,Save=ListSave,Discard=ListDiscard;
inline Transport::Vector Rotate(Transport::Quaternion q,Transport::Vector v) {
    Transport::Vector t{2*(q.y*v.z-q.z*v.y),2*(q.z*v.x-q.x*v.z),2*(q.x*v.y-q.y*v.x)};
    return {v.x+q.w*t.x+q.y*t.z-q.z*t.y,v.y+q.w*t.y+q.z*t.x-q.x*t.z,v.z+q.w*t.z+q.x*t.y-q.y*t.x};
}
// The panel's own plane: PanelLift metres in front of the game screen it covers,
// toward the viewer (the screen's local +Z, the side Hit accepts). With both on
// one plane SteamVR flickered the game menu through the panel (Mark, Nov 18);
// Virtual Desktop / VDXR did not. The pointer uses this same plane.
inline constexpr float PanelLift=.01f;
inline Transport::Pose PanelPose(Transport::Pose screen) {
    const auto n=Rotate(screen.orientation,{0,0,PanelLift});
    screen.position.x+=n.x;screen.position.y+=n.y;screen.position.z+=n.z;return screen;
}
// Aim and panel are in the same LOCAL space; panel is 3.2 x 1.8 metres.
inline bool Hit(const Transport::Pose& aim,const Transport::Pose& panel,float& x,float& y) {
    auto q=panel.orientation;q.x=-q.x;q.y=-q.y;q.z=-q.z;
    auto origin=Rotate(q,{aim.position.x-panel.position.x,aim.position.y-panel.position.y,aim.position.z-panel.position.z});
    auto direction=Rotate(q,Rotate(aim.orientation,{0,0,-1}));
    if(!std::isfinite(direction.z)||direction.z>=-0.0001f || origin.z<=0)return false;
    float t=-origin.z/direction.z;
    x=(origin.x+t*direction.x)/3.2f*Width+Width*.5f;
    y=Height*.5f-(origin.y+t*direction.y)/1.8f*Height;
    return std::isfinite(x)&&std::isfinite(y)&&x>=0&&x<Width&&y>=0&&y<Height;
}
inline bool Neutral(const GamepadMapping::Input& i) {
    return !i.a&&!i.b&&!i.x&&!i.y&&!i.menu&&!i.leftClick&&!i.rightClick&&
        i.leftTrigger<.05f&&i.rightTrigger<.05f&&i.leftGrip<.1f&&i.rightGrip<.1f&&
        std::abs(i.leftX)<.2f&&std::abs(i.leftY)<.2f&&std::abs(i.rightX)<.2f&&std::abs(i.rightY)<.2f;
}
inline bool Eligible(unsigned reasons) {
    return (reasons&2u) && !(reasons&(1u|4u|8u|16u|64u|128u));
}
inline bool ApplyAngle(float value,float& degrees,double& counts) {
    if(!std::isfinite(value)||value<5||value>180||!std::isfinite(degrees)||degrees<=0||
       !std::isfinite(counts)||counts<=0)return false;
    if(value!=degrees)counts=std::clamp(counts*double(value)/degrees,1.,20000.);
    degrees=value;return true;
}
struct InputGate {
    Transport::MenuState latest{};
    bool physicalRelease{};
    bool Blocked(uint64_t now) const {return Transport::Fresh(latest.hostTick,now)&&latest.blocked;}
    bool Physical(bool blocked,const XINPUT_GAMEPAD& pad) {
        if(blocked)physicalRelease=true;
        bool neutral=!pad.wButtons&&pad.bLeftTrigger<13&&pad.bRightTrigger<13&&
            std::abs(int(pad.sThumbLX))<6554&&std::abs(int(pad.sThumbLY))<6554&&
            std::abs(int(pad.sThumbRX))<6554&&std::abs(int(pad.sThumbRY))<6554;
        if(neutral)physicalRelease=false;
        return blocked||physicalRelease;
    }
};
struct State {
    bool nativePointer{},nativeDown{},nativeCapture{};
    uint64_t recenterAt{};int recenterSeconds{};bool recenterPulse{};
    bool stickNavigation{};float stickAnchorX{},stickAnchorY{};
    bool open{},blocked{},releaseGate{},triggerDown{},armed{},visible{},pointer{},stickArmed{};
    float x{},y{};uint32_t request{};float requested{};
    float draft{},original{};bool saving{},committed{};
    bool draftSnap{},originalSnap{},requestedSnap{};
    uint32_t draftMovement{},originalMovement{},requestedMovement{};
    uint32_t draftResponse{},originalResponse{},requestedResponse{};
    bool resetConfirm{},draftReset{},requestedReset{};
    uint32_t draftInteraction{},originalInteraction{},requestedInteraction{};
    bool draftDialogue{},originalDialogue{},requestedDialogue{};
    bool draftHandHud{},originalHandHud{},handHudAvailable{};uint32_t requestedHandHud{}; // requested 0 = unchanged, 1 off, 2 on
    // Transport 24 rows. Grenade aim as sent (Source + 1, 0 = unavailable); the
    // others as on/off with availability. requested 0 = unchanged.
    int tab{};
    uint32_t draftThrowAim{},originalThrowAim{},requestedThrowAim{};
    bool draftThrowMarker{},originalThrowMarker{},throwMarkerAvailable{};uint32_t requestedThrowMarker{};
    bool draftCoverFollow{},originalCoverFollow{},coverFollowAvailable{};uint32_t requestedCoverFollow{};
    bool draftScope{},originalScope{},scopeAvailable{};uint32_t requestedImmersiveScope{};
    uint32_t draftCrosshair{},originalCrosshair{},requestedCrosshair{}; // CoverCrosshair mode, 0 = unavailable
    void ResetTabDrafts(){draftThrowAim=originalThrowAim;draftThrowMarker=originalThrowMarker;draftCoverFollow=originalCoverFollow;draftScope=originalScope;draftCrosshair=originalCrosshair;}
    bool TabDirty() const {return draftThrowAim!=originalThrowAim||draftThrowMarker!=originalThrowMarker||draftCoverFollow!=originalCoverFollow||draftScope!=originalScope||draftCrosshair!=originalCrosshair;}
    void LoadTabDrafts(const Transport::MenuState& game){
        draftThrowAim=originalThrowAim=game.throwAim<=4?game.throwAim:0;
        throwMarkerAvailable=game.throwMarker==1||game.throwMarker==2;draftThrowMarker=originalThrowMarker=game.throwMarker==2;
        coverFollowAvailable=game.coverFollow==1||game.coverFollow==2;draftCoverFollow=originalCoverFollow=game.coverFollow==2;
        scopeAvailable=game.immersiveScope==1||game.immersiveScope==2;draftScope=originalScope=game.immersiveScope==2;
        draftCrosshair=originalCrosshair=game.crosshair<=4?game.crosshair:0;
    }
    float draftZoom{},originalZoom{},requestedZoom{}; // requestedZoom 0 = unchanged
    bool scalePage{};float draftScale{LiveWorldScale::Default},originalScale{LiveWorldScale::Default},requestedScale{LiveWorldScale::Default};
    bool remapping{};size_t selected{};int page{};
    bool listening{},captureReady{},confirming{},captureRelease{},scrollArmed{};
    int candidate{-1},pressedSlot{-1};uint64_t pressAt{};
    Rect Area() const {return open?(remapping?ListPanel:Panel):Entry;}
    void Submit(){requestedCrosshair=originalCrosshair&&draftCrosshair!=originalCrosshair?draftCrosshair:0u;requestedThrowAim=originalThrowAim&&draftThrowAim!=originalThrowAim?draftThrowAim:0u;requestedThrowMarker=throwMarkerAvailable&&draftThrowMarker!=originalThrowMarker?(draftThrowMarker?2u:1u):0u;requestedCoverFollow=coverFollowAvailable&&draftCoverFollow!=originalCoverFollow?(draftCoverFollow?2u:1u):0u;requestedImmersiveScope=scopeAvailable&&draftScope!=originalScope?(draftScope?2u:1u):0u;requestedHandHud=handHudAvailable&&draftHandHud!=originalHandHud?(draftHandHud?2u:1u):0u;requestedZoom=draftZoom!=originalZoom?draftZoom:0.f;requestedDialogue=draftDialogue;requestedScale=draftScale;requestedInteraction=draftInteraction;requestedReset=draftReset;requestedResponse=draftResponse;requested=draft;requestedSnap=draftSnap;requestedMovement=draftMovement;requestedBindings=draftBindings;if(draftReset)requestedBindingMask=(1u<<ButtonBindings::Count)-1;requestedBindingMask|=ButtonBindings::Changes(draftBindings,originalBindings);++request;saving=true;committed=true;}
    void Assign(){
        const auto target=ButtonBindings::Targets[selected].value;
        for(auto& value:draftBindings)if(value==target)value=0;
        draftBindings[size_t(candidate)]=target;confirming=false;candidate=-1;
    }
    ButtonBindings::Layout draftBindings{},originalBindings{},requestedBindings{};
    uint32_t requestedBindingMask{};
    bool Dirty() const {return TabDirty()||draftHandHud!=originalHandHud||draftZoom!=originalZoom||draftDialogue!=originalDialogue||draftScale!=originalScale|| draftReset||draftInteraction!=originalInteraction|| draftResponse!=originalResponse||draft!=original||draftSnap!=originalSnap||draftMovement!=originalMovement||draftBindings!=originalBindings||committed;}
    void Update(bool eligible,bool focused,bool ray,float px,float py,const GamepadMapping::Input& in,const Transport::MenuState& game,uint64_t now=GetTickCount64()) {
        recenterPulse=false;
        visible=eligible&&focused;pointer=visible&&ray;x=px;y=py;
        if(!visible||!in.active||!open)recenterAt=0;
        const bool wasOpen=open;
        // Stick navigation owns focus until deliberate aim movement or a trigger.
        if(!open&&(std::abs(in.leftX)>.2f||std::abs(in.leftY)>.2f)){
            stickNavigation=true;stickAnchorX=x;stickAnchorY=y;
        }else if(stickNavigation&&pointer&&
            (std::hypot(x-stickAnchorX,y-stickAnchorY)>35.f||in.rightTrigger>.05f))stickNavigation=false;
        if(stickNavigation&&!open)pointer=false;
        if(saving&&game.acknowledged==request){
            if(game.saved==request&&game.result==0){saving=false;open=false;armed=false;}
            else if(game.result==2||game.result==3)saving=false;
        }
        if(!visible){ResetTabDrafts();draftHandHud=originalHandHud;draftZoom=originalZoom;draftDialogue=originalDialogue;scalePage=false;draftScale=originalScale;open=false;remapping=false;listening=confirming=captureReady=resetConfirm=false;candidate=pressedSlot=-1;draftInteraction=originalInteraction;draftReset=false;draftResponse=originalResponse;draft=original;draftSnap=originalSnap;draftMovement=originalMovement;draftBindings=originalBindings;armed=false;nativeCapture=false;}
        if(visible&&in.rightTrigger<.05f)armed=true;
        bool hit=pointer&&Area().Has(x,y);
        // Capture from first trigger movement, before the game's low threshold.
        if(visible&&hit&&in.rightTrigger>.05f)releaseGate=true;
        // Lock a press to the native menu or the VR panel until trigger release.
        if(in.rightTrigger<.05f)nativeCapture=false;
        nativePointer=pointer;
        bool nativeHit=pointer&&!open&&!Entry.Has(x,y)&&!releaseGate;
        if(nativeHit&&armed&&in.rightTrigger>.05f)nativeCapture=true;
        nativeDown=nativeHit&&nativeCapture&&in.rightTrigger>.5f;
        const bool down=in.rightTrigger>.5f;
        // A capture consumes the whole press/release, including the pointer trigger.
        // Wait for neutral after opening so the click that chose the row cannot bind itself.
        bool capturing=listening||captureRelease;
        if(captureRelease&&Neutral(in))captureRelease=false;
        if(listening&&!in.active){listening=false;captureReady=false;pressedSlot=-1;captureRelease=true;}
        if(listening&&visible){
            if(in.menu||(captureReady&&pointer&&Cancel.Has(x,y)&&down&&!triggerDown)){
                listening=false;captureRelease=true;
            }else if(!captureReady){if(Neutral(in))captureReady=true;}
            else {
                const auto presses=Presses(in);
                if(presses&&(presses&(presses-1))){pressedSlot=-1;captureReady=false;}
                else if(pressedSlot<0&&presses){
                    for(int i=0;i<20;++i)if(presses&(1u<<i)){pressedSlot=i;pressAt=now;break;}
                }else if(pressedSlot>=0&&presses&&presses!=(1u<<pressedSlot)){
                    pressedSlot=-1;captureReady=false;
                }else if(pressedSlot>=0&&!presses){
                    candidate=pressedSlot;
                    if(candidate<8&&!draftReset&&game.holdMs[candidate]&&now-pressAt<game.holdMs[candidate])candidate+=10;
                    pressedSlot=-1;listening=false;confirming=true;captureRelease=true;
                }
            }
        }
        const bool counting=recenterAt!=0;
        if(counting){
            if(in.menu||(armed&&down&&!triggerDown&&hit&&Back.Has(x,y)))recenterAt=0;
            else if(now>=recenterAt){recenterAt=0;recenterPulse=true;armed=false;releaseGate=true;}
            recenterSeconds=recenterAt?int((recenterAt-now+999)/1000):0;
        }
        if(!counting&&visible&&armed&&down&&!triggerDown&&hit&&!nativeCapture&&!capturing) {
            if(!open){LoadTabDrafts(game);handHudAvailable=game.handHud!=0;draftHandHud=originalHandHud=game.handHud==2;draftZoom=originalZoom=game.scopeZoom;draftDialogue=originalDialogue=game.dialogue!=0;scalePage=false;draftScale=originalScale=game.worldScale;open=true;remapping=false;listening=confirming=captureRelease=resetConfirm=draftReset=false;draftInteraction=originalInteraction=game.interaction;page=0;selected=0;requestedBindingMask=0;releaseGate=true;draftResponse=originalResponse=game.response;draft=original=game.degrees;draftSnap=originalSnap=game.snapEnabled!=0;draftMovement=originalMovement=game.movement;draftBindings=originalBindings=game.bindings;committed=false;}
            else if(scalePage&&!saving){
                if(Back.Has(x,y))scalePage=false;
                else if(Save.Has(x,y)&&Dirty())Submit();
                else if(Discard.Has(x,y)){open=false;armed=false;}
                else if(ScaleReset.Has(x,y))draftScale=LiveWorldScale::Default;
                else if(ScaleMinus.Has(x,y))draftScale=std::clamp(draftScale-10.f,10.f,1000.f);
                else if(ScalePlus.Has(x,y))draftScale=std::clamp(draftScale+10.f,10.f,1000.f);
            }
            else if(remapping&&!saving){
                if(resetConfirm){
                    if(Confirm.Has(x,y)){draftBindings=ButtonBindings::Defaults;draftReset=true;resetConfirm=false;}
                    else if(Cancel.Has(x,y))resetConfirm=false;
                }else if(ResetBindings.Has(x,y))resetConfirm=true;
                else if(confirming){
                    if(Confirm.Has(x,y))Assign();
                    else if(Cancel.Has(x,y)){confirming=false;candidate=-1;}
                }else if(ListSave.Has(x,y)&&Dirty())Submit();
                else if(ListDiscard.Has(x,y)){open=false;armed=false;}
                else if(ListBack.Has(x,y))remapping=false;
                else if(PageUp.Has(x,y))page=std::max(0,page-1);
                else if(PageDown.Has(x,y))page=std::min(Actions-Rows,page+1);
                else for(int row=0;row<Rows;++row)if(Row(row).Has(x,y)){
                    selected=size_t(page+row);listening=true;captureReady=false;pressedSlot=-1;break;
                }
            }
            else if(!saving&&((!Dirty()&&Back.Has(x,y))||(Dirty()&&Discard.Has(x,y)))){open=false;armed=false;}
            else if(!saving&&Dirty()&&Save.Has(x,y))Submit();
            else if(!saving&&TabAt(x,y)>=0)tab=TabAt(x,y);
            // Movement tab.
            else if(!saving&&tab==TabMovement&&Recenter.Has(x,y)){recenterAt=now+3000;recenterSeconds=3;}
            else if(!saving&&tab==TabMovement&&WorldScale.Has(x,y))scalePage=true;
            else if(!saving&&tab==TabMovement&&Response.Has(x,y)){draftResponse=(draftResponse+1)%3;}
            else if(!saving&&tab==TabMovement&&Movement.Has(x,y)){draftMovement=draftMovement==1?3:draftMovement==3?2:1;}
            else if(!saving&&tab==TabMovement&&TurnMode.Has(x,y)){draftSnap=!draftSnap;}
            else if(!saving&&tab==TabMovement&&draftSnap&&(Minus.Has(x,y)||Plus.Has(x,y))){
                draft=std::clamp(draft+(Minus.Has(x,y)?-15.f:15.f),5.f,180.f);
            }
            // Controls tab.
            else if(!saving&&tab==TabControls&&Interaction.Has(x,y)){draftInteraction=draftInteraction==1?2:1;}
            else if(!saving&&tab==TabControls&&originalThrowAim&&ThrowAimRow.Has(x,y)){draftThrowAim=NextThrowAim(draftThrowAim);}
            else if(!saving&&tab==TabControls&&throwMarkerAvailable&&ThrowMarkerRow.Has(x,y)){draftThrowMarker=!draftThrowMarker;}
            else if(!saving&&tab==TabControls&&Remap.Has(x,y)){remapping=true;}
            // View tab.
            else if(!saving&&tab==TabView&&handHudAvailable&&HandHud.Has(x,y)){draftHandHud=!draftHandHud;}
            else if(!saving&&tab==TabView&&scopeAvailable&&ScopeRow.Has(x,y)){draftScope=!draftScope;}
            else if(!saving&&tab==TabView&&game.scopeZoom>=1&&(ZoomMinus.Has(x,y)||ZoomPlus.Has(x,y))){
                // Minus never raises a value already below the menu floor (from the INI).
                if(ZoomPlus.Has(x,y))draftZoom=std::min(std::max(draftZoom+ZoomStep,ZoomMin),ZoomMax);
                else if(draftZoom>ZoomMin)draftZoom=std::max(draftZoom-ZoomStep,ZoomMin);
            }
            else if(!saving&&tab==TabView&&coverFollowAvailable&&CoverFollowRow.Has(x,y)){draftCoverFollow=!draftCoverFollow;}
            else if(!saving&&tab==TabView&&game.dialogueAvailable&&Dialogue.Has(x,y)){draftDialogue=!draftDialogue;}
            else if(!saving&&tab==TabView&&originalCrosshair&&CrosshairRow.Has(x,y)){draftCrosshair=CoverCrosshair::Next(draftCrosshair);}
        }
        // One step per stick deflection; no frame-rate-dependent repeats.
        if(std::abs(in.leftY)<.2f)scrollArmed=true;
        if(remapping&&!listening&&!confirming&&!resetConfirm&&!saving&&scrollArmed&&std::abs(in.leftY)>.6f){
            page=std::clamp(page+(in.leftY<0?1:-1),0,Actions-Rows);scrollArmed=false;
        }
        if(std::abs(in.rightX)<.2f)stickArmed=true;
        if(open&&pointer&&tab==TabMovement&&MainRow(1).Has(x,y)&&stickArmed&&std::abs(in.rightX)>.6f&&
           std::abs(in.rightX)>std::abs(in.rightY)&&in.rightTrigger<.05f&&
           !saving&&!remapping&&!scalePage&&!counting&&draftSnap) {
            draft=std::clamp(draft+(in.rightX>0?15.f:-15.f),5.f,180.f);
            stickArmed=false;
        }
        triggerDown=down;
        if(wasOpen&&!open)releaseGate=true;
        blocked=open||releaseGate||nativeCapture;
        if(!open&&!nativeCapture&&in.active&&Neutral(in)){releaseGate=false;blocked=false;}
    }
};
}
