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
    return fail?1:0;
}
