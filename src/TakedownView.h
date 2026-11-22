#pragma once
#include <algorithm>
// First-person takedowns. The game's animation always runs, because it is what applies the
// knockout, rewards and energy. Only the view changes: it stays at Jensen's eyes, then a
// short fade hides the move to wherever the animation left him.
namespace TakedownView {
enum class Phase {Idle,Armed,Hold,Out,Black,In};
inline constexpr float BodyLimit=45.f;  // game units: further than this, the camera has left the body
inline constexpr double ArmMs=600;      // measured: the camera left the body about 170 ms after the start
inline constexpr double ClearMs=100;    // the camera must stay back this long before the fade
inline constexpr double OutMs=160,BlackMinMs=60,BlackMaxMs=700,InMs=220;
inline constexpr double MaxHoldMs=20000; // never hold longer, whatever the game reports
struct Output {bool hold{},hide{};float darkness{};};
struct State {
    Phase phase=Phase::Idle;double last{},phaseMs{},clearMs{};bool timed{};
    void Reset(){phase=Phase::Idle;phaseMs=clearMs=0;timed=false;}
    // started/ended: the game's takedown routines ran since the last call. It runs the start
    // routine again at the end, so a start together with an end is not a new takedown.
    // scripted: the camera is off the body or on an animation bone. canHold: a recent
    // first-person view exists. viewGood: the game's camera is back at the player's eyes.
    Output Update(double nowMs,bool enabled,bool started,bool ended,bool scripted,bool dialogue,bool canHold,bool viewGood) {
        if(!enabled||dialogue){Reset();return {};}
        // Capped, so time spent in a pause menu does not count.
        const double dt=timed&&nowMs>=last?(std::min)(nowMs-last,100.0):0.0;
        timed=true;last=nowMs;
        switch(phase) {
        case Phase::Idle:
            if(started&&!ended&&canHold){phase=Phase::Armed;phaseMs=0;return {true,false,0.f};}
            return {};
        case Phase::Armed:
            phaseMs+=dt;
            if(ended||phaseMs>=ArmMs){phase=Phase::Idle;phaseMs=0;return {};}
            if(scripted){phase=Phase::Hold;phaseMs=clearMs=0;return {true,true,0.f};}
            return {true,false,0.f};
        case Phase::Hold:
            phaseMs+=dt;clearMs=scripted?0:clearMs+dt;
            if(ended||clearMs>=ClearMs||phaseMs>=MaxHoldMs){phase=Phase::Out;phaseMs=0;}
            return {true,true,0.f};
        case Phase::Out:
            phaseMs+=dt;
            if(phaseMs>=OutMs){phase=Phase::Black;phaseMs=0;return {false,false,1.f};}
            return {true,true,float(phaseMs/OutMs)};
        case Phase::Black:
            phaseMs+=dt;
            if((phaseMs>=BlackMinMs&&viewGood&&!scripted)||phaseMs>=BlackMaxMs){phase=Phase::In;phaseMs=0;}
            return {false,false,1.f};
        case Phase::In:
            phaseMs+=dt;
            if(phaseMs>=InMs){phase=Phase::Idle;phaseMs=0;return {};}
            return {false,false,float(1-phaseMs/InMs)};
        }
        return {};
    }
};
}
