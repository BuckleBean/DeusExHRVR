#pragma once
#include "SharedPair.h"
#include "GamepadMapping.h"
#include <cmath>
#include <algorithm>

namespace SettingsMenu {
constexpr int Width=1600,Height=900;
struct Rect {int x,y,w,h; bool Has(float px,float py) const {return px>=x&&py>=y&&px<x+w&&py<y+h;}};
inline constexpr Rect Entry{1080,420,410,70},Panel{1020,230,530,550};
inline constexpr Rect Movement{1070,520,430,45};
inline constexpr Rect TurnMode{1070,330,430,45};
inline constexpr Rect Minus{1070,430,100,80},Plus{1400,430,100,80},Back{1070,650,430,70},Save{1070,625,430,60},Discard{1070,700,430,60};
inline Transport::Vector Rotate(Transport::Quaternion q,Transport::Vector v) {
    Transport::Vector t{2*(q.y*v.z-q.z*v.y),2*(q.z*v.x-q.x*v.z),2*(q.x*v.y-q.y*v.x)};
    return {v.x+q.w*t.x+q.y*t.z-q.z*t.y,v.y+q.w*t.y+q.z*t.x-q.x*t.z,v.z+q.w*t.z+q.x*t.y-q.y*t.x};
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
    bool stickNavigation{};float stickAnchorX{},stickAnchorY{};
    bool open{},blocked{},releaseGate{},triggerDown{},armed{},visible{},pointer{},stickArmed{};
    float x{},y{};uint32_t request{};float requested{};
    float draft{},original{};bool saving{},committed{};
    bool draftSnap{},originalSnap{},requestedSnap{};
    uint32_t draftMovement{},originalMovement{},requestedMovement{};
    bool Dirty() const {return draft!=original||draftSnap!=originalSnap||draftMovement!=originalMovement||committed;}
    void Update(bool eligible,bool focused,bool ray,float px,float py,const GamepadMapping::Input& in,const Transport::MenuState& game) {
        visible=eligible&&focused;pointer=visible&&ray;x=px;y=py;
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
        if(!visible){open=false;draft=original;draftSnap=originalSnap;draftMovement=originalMovement;armed=false;nativeCapture=false;}
        if(visible&&in.rightTrigger<.05f)armed=true;
        bool hit=pointer&&(open?Panel.Has(x,y):Entry.Has(x,y));
        // Capture from first trigger movement, before the game's low threshold.
        if(visible&&hit&&in.rightTrigger>.05f)releaseGate=true;
        // Lock a press to the native menu or the VR panel until trigger release.
        if(in.rightTrigger<.05f)nativeCapture=false;
        nativePointer=pointer;
        bool nativeHit=pointer&&!open&&!Entry.Has(x,y)&&!releaseGate;
        if(nativeHit&&armed&&in.rightTrigger>.05f)nativeCapture=true;
        nativeDown=nativeHit&&nativeCapture&&in.rightTrigger>.5f;
        const bool down=in.rightTrigger>.5f;
        if(visible&&armed&&down&&!triggerDown&&hit&&!nativeCapture) {
            if(!open){open=true;releaseGate=true;draft=original=game.degrees;draftSnap=originalSnap=game.snapEnabled!=0;draftMovement=originalMovement=game.movement;committed=false;}
            else if(!saving&&((!Dirty()&&Back.Has(x,y))||(Dirty()&&Discard.Has(x,y)))){open=false;armed=false;}
            else if(!saving&&Dirty()&&Save.Has(x,y)){requested=draft;requestedSnap=draftSnap;requestedMovement=draftMovement;++request;saving=true;committed=true;}
            else if(!saving&&Movement.Has(x,y)){draftMovement=draftMovement==1?3:draftMovement==3?2:1;}
            else if(!saving&&TurnMode.Has(x,y)){draftSnap=!draftSnap;}
            else if(!saving&&draftSnap) {
                int delta=Minus.Has(x,y)?-15:Plus.Has(x,y)?15:0;
                if(delta){draft=std::clamp(draft+delta,5.f,180.f);}
            }
        }
        // One step per stick deflection; no frame-rate-dependent repeats.
        // The first slice fits on one page, so no scrolling is needed yet.
        if(std::abs(in.rightX)<.2f)stickArmed=true;
        if(open&&pointer&&Panel.Has(x,y)&&stickArmed&&std::abs(in.rightX)>.6f&&
           std::abs(in.rightX)>std::abs(in.rightY)&&in.rightTrigger<.05f&&
           !saving&&draftSnap) {
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
