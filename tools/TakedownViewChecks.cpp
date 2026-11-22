// Offline checks of the first-person takedown timing. No game code runs.
#include <cstdio>
#include "TakedownView.h"
using namespace TakedownView;
static int failures=0;
static void Check(bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;}
// Replays a takedown with the timing logged at 4:34 PM (about 7 ms per frame).
struct Replay {bool heldThrough=true,hidden=false,darkened=false,heldAfter=false;Phase final=Phase::Idle;};
static Replay Run(int awayMs,int backMs,int endMs) {
    State s;Replay r;s.Update(1000,true,false,false,false,false,true,true);
    for(int ms=0;ms<=endMs+1500;ms+=7) {
        const bool start=ms==0||(ms<=endMs&&ms+7>endMs),end=ms<=endMs&&ms+7>endMs;
        const bool scripted=ms>=awayMs&&ms<backMs;
        auto v=s.Update(1007.0+ms,true,start,end,scripted,false,true,!scripted);
        if(ms<backMs&&!v.hold)r.heldThrough=false;
        r.hidden=r.hidden||v.hide;r.darkened=r.darkened||v.darkness>=1;
        if(ms>endMs+700&&v.hold)r.heldAfter=true;
    }
    r.final=s.phase;return r;
}
int main() {
    for(auto t:{Replay{},Replay{}}){(void)t;}
    {auto r=Run(170,2920,4375);Check(r.heldThrough,"logged takedown 1: first person from start to camera return");
     Check(r.hidden,"logged takedown 1: Jensen is hidden");Check(r.darkened,"logged takedown 1: the view moves in the dark");
     Check(!r.heldAfter&&r.final==Phase::Idle,"logged takedown 1: the repeated start at the end does not hold again");}
    {auto r=Run(160,2950,3190);Check(r.heldThrough&&r.hidden&&r.darkened,"logged takedown 2: held, hidden and faded");
     Check(!r.heldAfter&&r.final==Phase::Idle,"logged takedown 2: an end during the fade leaves nothing behind");}
    {State s;Check(!s.Update(1000,true,false,false,true,false,true,false).hold,"a camera off the body without a takedown is left alone");}
    {State s;s.Update(1000,true,false,false,false,false,true,true);auto a=s.Update(1010,true,true,false,false,false,true,true);
     Check(a.hold&&!a.hide&&s.phase==Phase::Armed,"the view is held from the first frame, before Jensen is hidden");
     double t=1020;for(;t<1700&&s.phase==Phase::Armed;t+=10)s.Update(t,true,false,false,false,false,true,true);
     Check(s.phase==Phase::Idle,"a start with no takedown camera lets go quickly");}
    {State s;s.Update(1000,true,false,false,false,false,true,true);Check(!s.Update(1010,true,true,true,false,false,true,true).hold,"a start together with an end is not a new takedown");}
    {State s;Check(!s.Update(1000,true,true,false,false,false,false,true).hold&&s.phase==Phase::Idle,"without a recent first-person view it uses the game's camera");}
    {State s;s.Update(1000,true,true,false,false,false,true,true);s.Update(1010,true,false,false,true,false,true,false);
     for(double t=1020;t<1060;t+=10)s.Update(t,true,false,false,false,false,true,true);s.Update(1060,true,false,false,true,false,true,false);
     Check(s.phase==Phase::Hold,"a brief camera pass near the body does not end the hold");}
    {State s;s.Update(1000,true,true,false,false,false,true,true);s.Update(1010,true,false,false,true,false,true,false);
     auto d=s.Update(1020,true,false,false,true,true,true,false);Check(!d.hold&&!d.hide&&d.darkness==0&&s.phase==Phase::Idle,"a conversation cancels it cleanly");}
    {State s;s.Update(1000,true,true,false,false,false,true,true);s.Update(1010,true,false,false,true,false,true,false);
     auto o=s.Update(1020,false,false,false,true,false,true,false);Check(!o.hold&&!o.hide&&o.darkness==0,"switched off, nothing happens");}
    {State s;s.Update(1000,true,true,false,false,false,true,true);s.Update(1010,true,false,false,true,false,true,false);
     double t=1020;for(int i=0;i<2100&&s.phase==Phase::Hold;i++,t+=10)s.Update(t,true,false,false,true,false,true,false);Check(s.phase==Phase::Out,"a stuck animation cannot hold forever");
     for(int i=0;i<200&&s.phase!=Phase::Idle;i++,t+=10)s.Update(t,true,false,false,true,false,true,false);Check(s.phase==Phase::Idle,"and a camera that never returns cannot leave the view dark");}
    {State s;s.Update(1000,true,true,false,false,false,true,true);s.Update(1010,true,false,false,true,false,true,false);s.Update(1020,true,false,true,true,false,true,false);
     s.Update(1030,true,false,false,false,false,true,true);s.Update(90000,true,false,false,false,false,true,true);
     Check(s.phase==Phase::Out,"time in a pause menu does not skip the fade");}
    return failures?1:0;
}
