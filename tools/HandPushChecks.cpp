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
        q[0]=9.f+130.f;check(!s.Step(q,300,1020,w)&&s.why==HandPush::StepJump,"a 43 cm tracking jump is still refused");
        float pa[3]{},ce[3]{30,0,0},hv[3]{900,0,0},bv[3]{},fo[4]{},mg1=0,mg2=0;
        HandPush::HorizontalForce(pa,ce,hv,bv,75,300,.01f,fo,nullptr,&mg1,1.f);
        HandPush::HorizontalForce(pa,ce,hv,bv,75,300,.01f,fo,nullptr,&mg2,2.f);
        check(std::abs(mg1-300.f)<.01f&&std::abs(mg2-600.f)<.01f,"HandPushStrength=2 doubles the cap");
        check(HandPush::HorizontalForce(pa,ce,hv,bv,75,300,.01f,fo,nullptr,nullptr,0.f)==HandPush::ForceInput,"zero strength rejected");
    }
    // Instant shove on first contact.
    {
        const float hv[3]{300,0,0},still[4]{};float out[4]{},target=0;
        check(HandPush::Shove(hv,still,75,1,out,&target)&&std::abs(out[0]*HandPush::VelocityFieldFactor-300.f)<.01f&&out[1]==0,"a light object takes the hand's speed at once");
        check(HandPush::Shove(hv,still,350,1,out)&&std::abs(out[0]*HandPush::VelocityFieldFactor-300.f*75.f/350.f)<.01f,"a dumpster takes about a fifth of it");
        check(HandPush::Shove(hv,still,75,2,out)&&std::abs(out[0]*HandPush::VelocityFieldFactor-600.f)<.01f,"strength scales the shove");
        const float fast[4]{100,0,-5,0};
        check(!HandPush::Shove(hv,fast,75,1,out),"an object already moving faster is not slowed");
        const float falling[4]{0,0,-9,0};
        check(HandPush::Shove(hv,falling,75,1,out)&&out[2]==-9,"falling speed is kept; only sideways speed is added");
        const float up[3]{0,0,300};
        check(!HandPush::Shove(up,still,75,1,out),"straight up or down gives no shove");
        check(!HandPush::Shove(hv,still,0,1,out),"zero mass gives no shove");
    }
    // Fast swipes: the whole path counts, judged by the approach; strength stays capped.
    {
        HandPush::Motion m2;float q[3]{},w[3]{};
        m2.Step(q,300,1000,w);q[0]=90.f;
        check(m2.Step(q,300,1030,w)&&m2.pathStart[0]==0.f,"a 30 cm step in one sample is a fast swipe, with its start kept");
        q[0]=90.f+130.f;check(!m2.Step(q,300,1060,w)&&m2.why==HandPush::StepJump,"a 43 cm step is still a tracking jump");
        const float a3[3]{0,0,0},b3[3]{90,0,0},c3[3]{45,20,0};float n3[3]{};
        HandPush::Nearest(a3,b3,c3,n3);check(std::abs(n3[0]-45.f)<.01f&&n3[1]==0,"the contact is where the path passed nearest the object");
        const float behind[3]{-40,0,0};HandPush::Nearest(a3,b3,behind,n3);check(n3[0]==0.f,"an object behind the path's start meets it at the start");
        // A swipe from x=0 to x=90 past an object at x=45, 20 cm to the side (y=60 units).
        const float centre[3]{45,60,0},v[3]{5625,0,0},bv[3]{};float fo[4]{};
        check(HandPush::HorizontalForce(a3,centre,v,bv,75,300,.016f,fo)==HandPush::ForceOk,"a glancing swipe is judged by its approach and pushes");
        const float past[3]{60,0,0};
        check(HandPush::HorizontalForce(past,centre,v,bv,75,300,.016f,fo)==HandPush::ForceNotInward,"judged from a point already past the object, it would not");
        float pv[3]{};HandPush::PushVelocity(v,300,pv);
        check(std::abs(pv[0]-2400.f)<.01f,"pushing uses at most 8 m/s however fast the swipe");
        const float slow[3]{150,0,0};HandPush::PushVelocity(slow,300,pv);check(pv[0]==150.f,"slower hands push at their real speed");
    }
    return fail?1:0;
}
