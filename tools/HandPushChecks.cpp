#include "HandPush.h"
#include <cstdio>
#include <limits>
int main(){
    int fail=0;auto check=[&](bool ok,const char* label){printf("%s %s\n",ok?"PASS":"FAIL",label);if(!ok)++fail;};
    HandPush::Motion m;float p[3]{},v[3]{};
    check(!m.Step(p,300,1000,v)&&m.why==HandPush::StepNoHistory,"first hand sample cannot push");
    p[0]=1.5f;check(m.Step(p,300,1010,v)&&std::abs(v[0]-150)<.001f&&m.why==HandPush::StepOk,"normal hand movement yields velocity");
    check(!m.Step(p,300,1010,v)&&m.why==HandPush::StepSameSample,"duplicate tracking cannot deliver another push");
    p[0]=200;check(!m.Step(p,300,1020,v)&&m.why==HandPush::StepJump,"tracking jump rejected");
    check(!m.Step(p,300,1200,v),"tracking gap resets push history");
    p[0]=201;check(!m.Step(p,340,1210,v),"world scale change cannot push");
    {
        HandPush::Motion r;float q[3]{},w[3]{};
        r.Step(q,300,1000,w);q[0]=.01f;r.Step(q,300,1010,w);check(r.why==HandPush::StepTooSlow,"resting hand named in the log");
    }
    // Walk-push-like force: horizontal, along the hand, capped like the game's own.
    float palm[3]{0,0,0},centre[3]{30,0,50},hand[3]{150,0,-80},body[3]{},f[4]{},in=0,mg=0;
    check(HandPush::HorizontalForce(palm,centre,hand,body,75,300,.01f,f,&in,&mg)==HandPush::ForceOk&&f[0]>0&&f[1]==0&&f[2]==0,"hand moving toward object pushes horizontally");
    check(mg<=4.f*75+.01f,"force at most 4x mass, as the walk push");
    check(HandPush::HorizontalForce(palm,centre,hand,body,350,300,.01f,f,&in,&mg)==HandPush::ForceOk&&mg<=1200.01f,"force at most 1200, as the walk push");
    check(HandPush::HorizontalForce(palm,centre,hand,body,350,600,.01f,f,&in,&mg)==HandPush::ForceOk&&mg<=2400.01f,"caps follow world scale");
    hand[0]=-150;check(HandPush::HorizontalForce(palm,centre,hand,body,75,300,.01f,f)==HandPush::ForceNotInward,"hand moving away does not pull");
    hand[0]=0;hand[1]=150;check(HandPush::HorizontalForce(palm,centre,hand,body,75,300,.01f,f)==HandPush::ForceNotInward,"hand sliding past sideways does not push");
    hand[0]=0;hand[1]=0;hand[2]=150;check(HandPush::HorizontalForce(palm,centre,hand,body,75,300,.01f,f)==HandPush::ForceNotInward,"pressing straight down does not push");
    hand[0]=150;hand[1]=0;hand[2]=0;body[0]=300;check(HandPush::HorizontalForce(palm,centre,hand,body,75,300,.01f,f)==HandPush::ForceReceding,"object moving away faster is not pushed");
    body[0]=0;centre[0]=1;check(HandPush::HorizontalForce(palm,centre,hand,body,75,300,.01f,f)==HandPush::ForceOk,"hand over the object's centre still pushes");
    centre[0]=30;check(HandPush::HorizontalForce(palm,centre,hand,body,0,300,.01f,f)==HandPush::ForceInput,"zero-mass body rejected");
    check(HandPush::HorizontalForce(palm,centre,hand,body,75,300,0,f)==HandPush::ForceInput,"paused update cannot push");
    check(HandPush::HorizontalForce(palm,centre,hand,body,75,300,.5f,f)==HandPush::ForceInput,"long game update cannot push");
    centre[0]=std::numeric_limits<float>::quiet_NaN();check(HandPush::HorizontalForce(palm,centre,hand,body,75,300,.01f,f)==HandPush::ForceInput,"invalid native position rejected");
    // Swipes and strength (November 24 headset result).
    {
        HandPush::Motion s;float q[3]{},w[3]{};
        s.Step(q,300,1000,w);q[0]=9.f;
        check(s.Step(q,300,1010,w)&&std::abs(w[0]-900.f)<.01f,"a 3 m/s swipe counts at its full speed");
        q[0]=9.f+60.f;check(!s.Step(q,300,1020,w)&&s.why==HandPush::StepJump,"a 20 cm tracking jump is still refused");
        float pa[3]{},ce[3]{30,0,0},hv[3]{900,0,0},bv[3]{},fo[4]{},mg1=0,mg2=0;
        HandPush::HorizontalForce(pa,ce,hv,bv,75,300,.01f,fo,nullptr,&mg1,1.f);
        HandPush::HorizontalForce(pa,ce,hv,bv,75,300,.01f,fo,nullptr,&mg2,2.f);
        check(std::abs(mg1-300.f)<.01f&&std::abs(mg2-600.f)<.01f,"HandPushStrength=2 doubles the cap");
        check(HandPush::HorizontalForce(pa,ce,hv,bv,75,300,.01f,fo,nullptr,nullptr,0.f)==HandPush::ForceInput,"zero strength rejected");
    }
    // A swipe through an object keeps pushing past its centre, but never pulls.
    {
        HandPush::SwipeMemory mem;float pa[3]{0,0,0},ce[3]{30,0,0},v[3]{300,0,0},bv[3]{},fo[4]{};
        check(HandPush::HorizontalForce(pa,ce,v,bv,75,300,.01f,fo,nullptr,nullptr,1.f,nullptr)==HandPush::ForceOk,"swipe starts toward the object");
        mem.Note(7,v,1000);
        const float past[3]{60,0,0};
        check(HandPush::HorizontalForce(past,ce,v,bv,75,300,.01f,fo)==HandPush::ForceNotInward,"without the swipe rule, past the centre stops the push");
        auto* sw=mem.Find(7,1011);
        check(sw&&HandPush::HorizontalForce(past,ce,v,bv,75,300,.01f,fo,nullptr,nullptr,1.f,sw->dir)==HandPush::ForceOk&&fo[0]>0,"with it, the swipe keeps pushing past the centre");
        const float back[3]{-300,0,0};
        check(sw&&HandPush::HorizontalForce(past,ce,back,bv,75,300,.01f,fo,nullptr,nullptr,1.f,sw->dir)==HandPush::ForceNotInward,"pulling the hand back never drags the object");
        const float curve[3]{0,300,0};
        check(sw&&HandPush::HorizontalForce(past,ce,curve,bv,75,300,.01f,fo,nullptr,nullptr,1.f,sw->dir)==HandPush::ForceNotInward,"a 90 degree turn ends the swipe");
        check(!mem.Find(7,1000+HandPush::SwipeKeepMs+1),"a swipe ends after a short break in contact");
        mem.Note(7,v,1100);check(mem.Find(7,1200)!=nullptr,"continued contact keeps the swipe alive");
        check(!mem.Find(8,1100),"another object is a new contact");
    }
    return fail?1:0;
}
