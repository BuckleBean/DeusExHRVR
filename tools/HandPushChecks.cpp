#include "HandPush.h"
#include <cstdio>
#include <limits>
int main(){
    int fail=0;auto check=[&](bool ok,const char* label){printf("%s %s\n",ok?"PASS":"FAIL",label);if(!ok)++fail;};
    HandPush::Motion m;float p[3]{},v[3]{};
    check(!m.Step(p,300,1000,v),"first hand sample cannot push");
    p[0]=1.5f;check(m.Step(p,300,1010,v)&&std::abs(v[0]-150)<.001f,"normal hand movement yields velocity");
    check(!m.Step(p,300,1010,v),"duplicate tracking cannot deliver another push");
    p[0]=200;check(!m.Step(p,300,1020,v),"tracking jump rejected");
    check(!m.Step(p,300,1200,v),"tracking gap resets push history");
    p[0]=201;check(!m.Step(p,340,1210,v),"world scale change cannot push");
    float palm[3]{0,0,0},point[3]{9,0,0},normal[3]{-1,0,0},hand[3]{150,0,0},body[3]{},f[4]{};
    check(HandPush::Force(palm,point,normal,hand,body,10,300,.01f,f)&&f[0]>0&&f[1]==0,"inward touch pushes away from hand");
    check(f[0]<=1500,"force capped by mass and world scale");
    hand[0]=-150;check(!HandPush::Force(palm,point,normal,hand,body,10,300,.01f,f),"withdrawal does not pull object");
    hand[0]=0;body[0]=-300;check(!HandPush::Force(palm,point,normal,hand,body,10,300,.01f,f),"stationary hand does not act as motor");
    hand[0]=150;body[0]=200;check(!HandPush::Force(palm,point,normal,hand,body,10,300,.01f,f),"object moving away faster receives no push");
    body[0]=0;point[0]=30;check(!HandPush::Force(palm,point,normal,hand,body,10,300,.01f,f),"nearby object outside palm radius not pushed");
    point[0]=9;normal[0]=1;check(!HandPush::Force(palm,point,normal,hand,body,10,300,.01f,f),"inverted normal rejected");
    normal[0]=-1;check(!HandPush::Force(palm,point,normal,hand,body,0,300,.01f,f),"zero-mass body rejected");
    check(!HandPush::Force(palm,point,normal,hand,body,10,300,0,f),"paused update cannot push");
    check(!HandPush::Force(palm,point,normal,hand,body,10,300,.5f,f),"long game update cannot push");
    point[0]=std::numeric_limits<float>::quiet_NaN();check(!HandPush::Force(palm,point,normal,hand,body,10,300,.01f,f),"invalid native hit rejected");
    // Log reasons (HandPushLog): the same decisions, now named.
    {
        HandPush::Motion r;float q[3]{},w[3]{};
        r.Step(q,300,1000,w);check(r.why==HandPush::StepNoHistory,"log names missing history");
        q[0]=.01f;r.Step(q,300,1010,w);check(r.why==HandPush::StepTooSlow,"log names a resting hand");
        q[0]=1.5f;check(r.Step(q,300,1020,w)&&r.why==HandPush::StepOk,"log names a moving hand");
        q[0]=200;r.Step(q,300,1030,w);check(r.why==HandPush::StepJump,"log names a tracking jump");
        float pa[3]{},pt[3]{9,0,0},nm[3]{-1,0,0},hv[3]{150,0,0},bv[3]{},fo[4]{},in=0,mg=0;
        check(HandPush::ForceReason(pa,pt,nm,hv,bv,10,300,.01f,fo,&in,&mg)==HandPush::ForceOk&&in>0&&mg>0,"log reports accepted force and its size");
        pt[0]=30;check(HandPush::ForceReason(pa,pt,nm,hv,bv,10,300,.01f,fo)==HandPush::ForceFar,"log names a hit beyond the palm");
        pt[0]=9;nm[0]=1;check(HandPush::ForceReason(pa,pt,nm,hv,bv,10,300,.01f,fo)==HandPush::ForceBehind,"log names an inverted normal");
        nm[0]=-1;hv[0]=-150;check(HandPush::ForceReason(pa,pt,nm,hv,bv,10,300,.01f,fo)==HandPush::ForceNotInward,"log names a withdrawing hand");
        hv[0]=150;bv[0]=200;check(HandPush::ForceReason(pa,pt,nm,hv,bv,10,300,.01f,fo)==HandPush::ForceReceding,"log names a receding object");
    }
    // Pushes from items the pickup search met (no surface normal).
    {
        const float hand[3]{0,0,0};float centre[3]{30,0,0},hit[3]{200,0,0};
        check(HandPush::Touching(hand,centre,hit,300),"object position 10 cm from the hand counts as touching");
        centre[0]=90;check(!HandPush::Touching(hand,centre,hit,300),"object 30 cm away with a far hit point is not touching");
        hit[0]=30;check(HandPush::Touching(hand,centre,hit,300),"large object touching by its hit point");
        float v[3]{150,0,0},bv[3]{},fo[4]{},in=0,mg=0;centre[0]=30;
        check(HandPush::TowardForce(hand,centre,v,bv,10,300,.01f,fo,&in,&mg)==HandPush::ForceOk&&fo[0]>0&&mg>0,"hand moving toward object pushes along the hand's motion");
        v[0]=-150;check(HandPush::TowardForce(hand,centre,v,bv,10,300,.01f,fo)==HandPush::ForceNotInward,"hand moving away does not push");
        v[0]=0;v[1]=150;check(HandPush::TowardForce(hand,centre,v,bv,10,300,.01f,fo)==HandPush::ForceNotInward,"sideways hand past the object does not push");
        v[0]=150;v[1]=0;bv[0]=300;check(HandPush::TowardForce(hand,centre,v,bv,10,300,.01f,fo)==HandPush::ForceReceding,"object moving away faster is not pushed");
        bv[0]=0;check(HandPush::TowardForce(hand,centre,v,bv,10,300,.01f,fo,&in,&mg)==HandPush::ForceOk&&mg<=10*300*.5f+.01f,"force capped by mass and world scale");
        check(HandPush::TowardForce(hand,centre,v,bv,10,300,.5f,fo)==HandPush::ForceInput,"long game update cannot push");
    }
    return fail?1:0;
}

