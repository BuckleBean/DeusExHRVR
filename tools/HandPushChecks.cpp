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
    return fail?1:0;
}

